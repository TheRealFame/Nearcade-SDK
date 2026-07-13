#include "nearcade_internal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <linux/input.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <errno.h>

static int kbm_fd = -1;
static int gp_fds[MAX_SLOTS];
static char gp_names[MAX_SLOTS][NEARCADE_DEVICE_NAME_LEN];

static pthread_mutex_t input_lock = PTHREAD_MUTEX_INITIALIZER;

static void emit(int fd, uint16_t type, uint16_t code, int32_t val)
{
    if (fd < 0) {
        LOG_TRACE("emit: fd=%d invalid, skipping (type=%d code=%d val=%d)", fd, type, code, val);
        return;
    }
    struct input_event ie;
    memset(&ie, 0, sizeof(ie));
    ie.type = type;
    ie.code = code;
    ie.value = val;
    ssize_t w = write(fd, &ie, sizeof(ie));
    if (w < 0) {
        LOG_TRACE("emit: write failed on fd=%d: %s (type=%d code=%d val=%d)",
                  fd, strerror(errno), type, code, val);
    }
}

static void syn(int fd)
{
    if (fd >= 0) {
        emit(fd, EV_SYN, SYN_REPORT, 0);
    }
}

static void set_abs(struct uinput_user_dev *uud, int axis,
                    int32_t mn, int32_t mx, int32_t fuzz, int32_t flat)
{
    uud->absmin[axis]  = mn;
    uud->absmax[axis]  = mx;
    uud->absfuzz[axis] = fuzz;
    uud->absflat[axis] = flat;
}

static int create_kbm_device(void)
{
    LOG_DEBUG("create_kbm_device: opening /dev/uinput...");
    kbm_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (kbm_fd < 0) {
        LOG_ERROR("create_kbm_device: open /dev/uinput failed: %s", strerror(errno));
        return NEARCADE_ERR_PERMISSION;
    }

    ioctl(kbm_fd, UI_SET_EVBIT, EV_SYN);
    ioctl(kbm_fd, UI_SET_EVBIT, EV_KEY);
    for (int i = 1; i < 255; i++) ioctl(kbm_fd, UI_SET_KEYBIT, i);

    ioctl(kbm_fd, UI_SET_KEYBIT, BTN_LEFT);
    ioctl(kbm_fd, UI_SET_KEYBIT, BTN_RIGHT);
    ioctl(kbm_fd, UI_SET_KEYBIT, BTN_MIDDLE);

    ioctl(kbm_fd, UI_SET_EVBIT, EV_REL);
    ioctl(kbm_fd, UI_SET_RELBIT, REL_X);
    ioctl(kbm_fd, UI_SET_RELBIT, REL_Y);
    ioctl(kbm_fd, UI_SET_RELBIT, REL_WHEEL);

    struct uinput_user_dev uud;
    memset(&uud, 0, sizeof(uud));
    uud.id.bustype = BUS_USB;
    uud.id.vendor  = 0x1234;
    uud.id.product = 0x5678;
    uud.id.version = 1;
    snprintf(uud.name, UINPUT_MAX_NAME_SIZE, "Nearsec Virtual KBM");

    if (write(kbm_fd, &uud, sizeof(uud)) < 0) {
        LOG_ERROR("create_kbm_device: write uud failed: %s", strerror(errno));
        close(kbm_fd); kbm_fd = -1;
        return NEARCADE_ERR_NO_DEVICE;
    }

    if (ioctl(kbm_fd, UI_DEV_CREATE) < 0) {
        LOG_ERROR("create_kbm_device: UI_DEV_CREATE failed: %s", strerror(errno));
        close(kbm_fd); kbm_fd = -1;
        return NEARCADE_ERR_NO_DEVICE;
    }

    LOG_INFO("create_kbm_device: created 'Nearsec Virtual KBM' on fd=%d", kbm_fd);
    return NEARCADE_OK;
}

int input_init(const nearcade_config *config)
{
    LOG_DEBUG("input_init: entering (config=%p)", (void*)config);
    (void)config;
    memset(gp_fds, 0xFF, sizeof(gp_fds));
    memset(gp_names, 0, sizeof(gp_names));
    int rc = create_kbm_device();
    LOG_DEBUG("input_init: create_kbm_device returned %d", rc);
    return rc;
}

void input_shutdown(void)
{
    LOG_DEBUG("input_shutdown: entering");
    pthread_mutex_lock(&input_lock);

    for (int slot = 0; slot < MAX_SLOTS; slot++) {
        if (gp_fds[slot] >= 0) {
            LOG_DEBUG("input_shutdown: destroying slot %d (fd=%d)", slot, gp_fds[slot]);
            ioctl(gp_fds[slot], UI_DEV_DESTROY);
            close(gp_fds[slot]);
            gp_fds[slot] = -1;
        }
    }

    if (kbm_fd >= 0) {
        LOG_DEBUG("input_shutdown: destroying KBM device (fd=%d)", kbm_fd);
        ioctl(kbm_fd, UI_DEV_DESTROY);
        close(kbm_fd);
        kbm_fd = -1;
    }

    pthread_mutex_unlock(&input_lock);
    LOG_DEBUG("input_shutdown: done");
}

int input_alloc_slot(uint8_t slot, uint16_t vendor, uint16_t product,
                     uint16_t version, const char *name)
{
    LOG_DEBUG("input_alloc_slot: slot=%d vendor=0x%04x product=0x%04x ver=0x%04x name='%s'",
              slot, vendor, product, version, name ? name : "NULL");

    if (slot >= MAX_SLOTS) {
        LOG_ERROR("input_alloc_slot: slot %d >= MAX_SLOTS %d", slot, MAX_SLOTS);
        return NEARCADE_ERR_INVALID_ARG;
    }

    pthread_mutex_lock(&input_lock);

    if (gp_fds[slot] >= 0) {
        LOG_DEBUG("input_alloc_slot: slot %d already occupied, destroying old device", slot);
        ioctl(gp_fds[slot], UI_DEV_DESTROY);
        close(gp_fds[slot]);
        gp_fds[slot] = -1;
    }

    LOG_DEBUG("input_alloc_slot: opening /dev/uinput for slot %d", slot);
    int fd = open("/dev/uinput", O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        LOG_ERROR("input_alloc_slot: open /dev/uinput failed: %s", strerror(errno));
        pthread_mutex_unlock(&input_lock);
        return NEARCADE_ERR_PERMISSION;
    }

    /* Enable event types */
    ioctl(fd, UI_SET_EVBIT, EV_SYN);
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_KEYBIT, BTN_SOUTH);  ioctl(fd, UI_SET_KEYBIT, BTN_EAST);
    ioctl(fd, UI_SET_KEYBIT, BTN_NORTH);  ioctl(fd, UI_SET_KEYBIT, BTN_WEST);
    ioctl(fd, UI_SET_KEYBIT, BTN_TL);     ioctl(fd, UI_SET_KEYBIT, BTN_TR);
    ioctl(fd, UI_SET_KEYBIT, BTN_SELECT); ioctl(fd, UI_SET_KEYBIT, BTN_START);
    ioctl(fd, UI_SET_KEYBIT, BTN_MODE);
    ioctl(fd, UI_SET_KEYBIT, BTN_THUMBL); ioctl(fd, UI_SET_KEYBIT, BTN_THUMBR);
    LOG_TRACE("input_alloc_slot: EV_KEY bits set for slot %d", slot);

    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    ioctl(fd, UI_SET_ABSBIT, ABS_X);    ioctl(fd, UI_SET_ABSBIT, ABS_Y);
    ioctl(fd, UI_SET_ABSBIT, ABS_RX);   ioctl(fd, UI_SET_ABSBIT, ABS_RY);
    ioctl(fd, UI_SET_ABSBIT, ABS_Z);    ioctl(fd, UI_SET_ABSBIT, ABS_RZ);
    ioctl(fd, UI_SET_ABSBIT, ABS_HAT0X); ioctl(fd, UI_SET_ABSBIT, ABS_HAT0Y);
    LOG_TRACE("input_alloc_slot: EV_ABS bits set for slot %d", slot);

    ioctl(fd, UI_SET_EVBIT, EV_FF);
    ioctl(fd, UI_SET_FFBIT, FF_RUMBLE);
    LOG_TRACE("input_alloc_slot: EV_FF (rumble) set for slot %d", slot);

    struct uinput_user_dev uud;
    memset(&uud, 0, sizeof(uud));
    uud.id.bustype = BUS_USB;
    uud.id.vendor  = vendor;
    uud.id.product = product;
    uud.id.version = version;
    set_abs(&uud, ABS_X,    -32767, 32767, 16, 128);
    set_abs(&uud, ABS_Y,    -32767, 32767, 16, 128);
    set_abs(&uud, ABS_RX,   -32767, 32767, 16, 128);
    set_abs(&uud, ABS_RY,   -32767, 32767, 16, 128);
    set_abs(&uud, ABS_Z,         0,   255,  0,   0);
    set_abs(&uud, ABS_RZ,        0,   255,  0,   0);
    set_abs(&uud, ABS_HAT0X,    -1,     1,  0,   0);
    set_abs(&uud, ABS_HAT0Y,    -1,     1,  0,   0);
    uud.ff_effects_max = 16;

    snprintf(uud.name, UINPUT_MAX_NAME_SIZE, "%s", name ? name : "Nearcade Gamepad");

    LOG_DEBUG("input_alloc_slot: writing uinput_user_dev for slot %d ('%s')", slot, uud.name);
    if (write(fd, &uud, sizeof(uud)) < 0) {
        LOG_ERROR("input_alloc_slot: write uud failed: %s", strerror(errno));
        close(fd);
        pthread_mutex_unlock(&input_lock);
        return NEARCADE_ERR_NO_DEVICE;
    }

    if (ioctl(fd, UI_DEV_CREATE) < 0) {
        LOG_ERROR("input_alloc_slot: UI_DEV_CREATE failed: %s", strerror(errno));
        close(fd);
        pthread_mutex_unlock(&input_lock);
        return NEARCADE_ERR_NO_DEVICE;
    }

    gp_fds[slot] = fd;
    strncpy(gp_names[slot], name ? name : "Nearcade Gamepad", NEARCADE_DEVICE_NAME_LEN - 1);

    pthread_mutex_unlock(&input_lock);
    LOG_INFO("input_alloc_slot: slot %d created: '%s' (%04x:%04x v%04x) fd=%d",
             slot, uud.name, vendor, product, version, fd);
    return NEARCADE_OK;
}

int input_free_slot(uint8_t slot)
{
    LOG_DEBUG("input_free_slot: slot=%d", slot);
    if (slot >= MAX_SLOTS) {
        LOG_ERROR("input_free_slot: slot %d >= MAX_SLOTS %d", slot, MAX_SLOTS);
        return NEARCADE_ERR_INVALID_ARG;
    }

    pthread_mutex_lock(&input_lock);
    if (gp_fds[slot] >= 0) {
        LOG_DEBUG("input_free_slot: destroying uinput device on slot %d (fd=%d)", slot, gp_fds[slot]);
        ioctl(gp_fds[slot], UI_DEV_DESTROY);
        close(gp_fds[slot]);
        gp_fds[slot] = -1;
        memset(gp_names[slot], 0, NEARCADE_DEVICE_NAME_LEN);
        LOG_INFO("input_free_slot: slot %d freed", slot);
    } else {
        LOG_DEBUG("input_free_slot: slot %d already free", slot);
    }
    pthread_mutex_unlock(&input_lock);
    return NEARCADE_OK;
}

int input_submit_gamepad(const nearcade_gamepad_packet *pkt)
{
    if (!pkt) {
        LOG_ERROR("input_submit_gamepad: NULL pkt");
        return NEARCADE_ERR_INVALID_ARG;
    }

    pthread_mutex_lock(&input_lock);

    int slot = pkt->slot;
    if (slot >= MAX_SLOTS) {
        LOG_ERROR("input_submit_gamepad: slot %d >= MAX_SLOTS %d", slot, MAX_SLOTS);
        pthread_mutex_unlock(&input_lock);
        return NEARCADE_ERR_INVALID_ARG;
    }

    if (gp_fds[slot] < 0) {
        LOG_WARN("input_submit_gamepad: slot %d has no uinput device (fd=%d)", slot, gp_fds[slot]);
        pthread_mutex_unlock(&input_lock);
        return NEARCADE_ERR_NO_DEVICE;
    }

    int fd = gp_fds[slot];
    LOG_TRACE("input_submit_gamepad: slot=%d fd=%d lx=%d ly=%d rx=%d ry=%d lt=%d rt=%d btn=0x%04x hx=%d hy=%d",
              slot, fd, pkt->lx, pkt->ly, pkt->rx, pkt->ry,
              pkt->lt, pkt->rt, pkt->buttons, pkt->hx, pkt->hy);

    emit(fd, EV_ABS, ABS_X,    pkt->lx);
    emit(fd, EV_ABS, ABS_Y,    pkt->ly);
    emit(fd, EV_ABS, ABS_RX,   pkt->rx);
    emit(fd, EV_ABS, ABS_RY,   pkt->ry);
    emit(fd, EV_ABS, ABS_Z,    pkt->lt);
    emit(fd, EV_ABS, ABS_RZ,   pkt->rt);
    emit(fd, EV_ABS, ABS_HAT0X, pkt->hx);
    emit(fd, EV_ABS, ABS_HAT0Y, pkt->hy);

    uint16_t btn = pkt->buttons;
    emit(fd, EV_KEY, BTN_SOUTH, (btn & W3C_A) ? 1 : 0);
    emit(fd, EV_KEY, BTN_EAST,  (btn & W3C_B) ? 1 : 0);
    emit(fd, EV_KEY, BTN_WEST,  (btn & W3C_Y) ? 1 : 0);
    emit(fd, EV_KEY, BTN_NORTH, (btn & W3C_X) ? 1 : 0);
    emit(fd, EV_KEY, BTN_TL,    (btn & W3C_LB) ? 1 : 0);
    emit(fd, EV_KEY, BTN_TR,    (btn & W3C_RB) ? 1 : 0);
    emit(fd, EV_KEY, BTN_SELECT,(btn & W3C_BACK) ? 1 : 0);
    emit(fd, EV_KEY, BTN_START, (btn & W3C_START) ? 1 : 0);
    emit(fd, EV_KEY, BTN_THUMBL,(btn & W3C_LS) ? 1 : 0);
    emit(fd, EV_KEY, BTN_THUMBR,(btn & W3C_RS) ? 1 : 0);
    emit(fd, EV_KEY, BTN_MODE,  (btn & W3C_GUIDE) ? 1 : 0);

    syn(fd);

    pthread_mutex_unlock(&input_lock);
    LOG_TRACE("input_submit_gamepad: slot %d complete", slot);
    return NEARCADE_OK;
}

/* Simple key name to Linux evdev keycode mapping */
static int key_name_to_code(const char *name)
{
    if (!name) return 0;
    /* Strip "KEY_" prefix if present */
    const char *k = name;
    if (strncmp(name, "KEY_", 4) == 0) k = name + 4;

    if (strcmp(k, "A") == 0) return KEY_A;
    if (strcmp(k, "B") == 0) return KEY_B;
    if (strcmp(k, "C") == 0) return KEY_C;
    if (strcmp(k, "D") == 0) return KEY_D;
    if (strcmp(k, "W") == 0) return KEY_W;
    if (strcmp(k, "S") == 0) return KEY_S;
    if (strcmp(k, "SPACE") == 0) return KEY_SPACE;
    if (strcmp(k, "LEFTSHIFT") == 0 || strcmp(k, "LSHIFT") == 0) return KEY_LEFTSHIFT;
    if (strcmp(k, "LEFTCTRL") == 0 || strcmp(k, "LCTRL") == 0) return KEY_LEFTCTRL;
    if (strcmp(k, "ESC") == 0) return KEY_ESC;
    if (strcmp(k, "TAB") == 0) return KEY_TAB;
    if (strcmp(k, "E") == 0) return KEY_E;
    if (strcmp(k, "R") == 0) return KEY_R;
    if (strcmp(k, "F") == 0) return KEY_F;
    if (strcmp(k, "G") == 0) return KEY_G;
    if (strcmp(k, "C") == 0) return KEY_C;
    if (strcmp(k, "UP") == 0) return KEY_UP;
    if (strcmp(k, "DOWN") == 0) return KEY_DOWN;
    if (strcmp(k, "LEFT") == 0) return KEY_LEFT;
    if (strcmp(k, "RIGHT") == 0) return KEY_RIGHT;
    if (strcmp(k, "ENTER") == 0) return KEY_ENTER;
    return 0;
}

int input_submit_kbm(const char *viewer_id, const char *event_type,
                     const char *key, int dx, int dy)
{
    LOG_TRACE("input_submit_kbm: viewer=%s event=%s key=%s dx=%d dy=%d",
              viewer_id ? viewer_id : "NULL",
              event_type ? event_type : "NULL",
              key ? key : "NULL", dx, dy);

    pthread_mutex_lock(&input_lock);
    if (kbm_fd < 0) {
        LOG_WARN("input_submit_kbm: KBM device not initialized");
        pthread_mutex_unlock(&input_lock);
        return NEARCADE_ERR_NO_DEVICE;
    }

    if (strcmp(event_type, "mousemove") == 0) {
        LOG_TRACE("input_submit_kbm: mouse move dx=%d dy=%d", dx, dy);
        emit(kbm_fd, EV_REL, REL_X, dx);
        emit(kbm_fd, EV_REL, REL_Y, dy);
        syn(kbm_fd);
    } else if (strcmp(event_type, "keydown") == 0 || strcmp(event_type, "keyup") == 0) {
        int val = (strcmp(event_type, "keydown") == 0) ? 1 : 0;
        int code = key_name_to_code(key);
        if (code > 0) {
            LOG_TRACE("input_submit_kbm: key '%s' -> code=%d val=%d", key ? key : "?", code, val);
            emit(kbm_fd, EV_KEY, code, val);
            syn(kbm_fd);
        } else {
            LOG_WARN("input_submit_kbm: unmapped key '%s'", key ? key : "NULL");
        }
    } else {
        LOG_WARN("input_submit_kbm: unknown event type '%s'", event_type);
    }

    pthread_mutex_unlock(&input_lock);
    return NEARCADE_OK;
}

int input_flush_slot(uint8_t slot)
{
    LOG_TRACE("input_flush_slot: slot=%d", slot);
    pthread_mutex_lock(&input_lock);

    if (slot < MAX_SLOTS && gp_fds[slot] >= 0) {
        int fd = gp_fds[slot];
        LOG_DEBUG("input_flush_slot: resetting slot %d (fd=%d)", slot, fd);
        emit(fd, EV_ABS, ABS_X, 0); emit(fd, EV_ABS, ABS_Y, 0);
        emit(fd, EV_ABS, ABS_RX, 0); emit(fd, EV_ABS, ABS_RY, 0);
        emit(fd, EV_ABS, ABS_Z, 0); emit(fd, EV_ABS, ABS_RZ, 0);
        emit(fd, EV_ABS, ABS_HAT0X, 0); emit(fd, EV_ABS, ABS_HAT0Y, 0);
        int codes[] = {BTN_SOUTH, BTN_EAST, BTN_WEST, BTN_NORTH,
                       BTN_TL, BTN_TR, BTN_SELECT, BTN_START,
                       BTN_MODE, BTN_THUMBL, BTN_THUMBR};
        for (size_t i = 0; i < sizeof(codes)/sizeof(codes[0]); i++) {
            emit(fd, EV_KEY, codes[i], 0);
        }
        syn(fd);
        LOG_DEBUG("input_flush_slot: slot %d reset complete", slot);
    } else {
        LOG_DEBUG("input_flush_slot: slot %d invalid or not allocated", slot);
    }

    pthread_mutex_unlock(&input_lock);
    return NEARCADE_OK;
}
