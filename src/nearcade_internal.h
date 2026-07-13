#ifndef NEARCADE_INTERNAL_H
#define NEARCADE_INTERNAL_H

#include "nearcade.h"
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define MAX_VIEWERS 64
#define MAX_SLOTS   16

/* ── Debug logging ─────────────────────────────────────────────────────────── */
enum nearcade_loglevel {
    NEARCADE_LOG_NONE  = 0,
    NEARCADE_LOG_ERROR = 1,
    NEARCADE_LOG_WARN  = 2,
    NEARCADE_LOG_INFO  = 3,
    NEARCADE_LOG_DEBUG = 4,
    NEARCADE_LOG_TRACE = 5,
};

extern int g_loglevel;

#define NEARCADE_LOG(level, fmt, ...) do { \
    if ((level) <= g_loglevel) { \
        fprintf(stderr, "[nearcade] %s:%d " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
    } \
} while(0)

/* Shorthands for common levels */
#define LOG_ERROR(fmt, ...)  NEARCADE_LOG(NEARCADE_LOG_ERROR, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)   NEARCADE_LOG(NEARCADE_LOG_WARN,  fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)   NEARCADE_LOG(NEARCADE_LOG_INFO,  fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...)  NEARCADE_LOG(NEARCADE_LOG_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_TRACE(fmt, ...)  NEARCADE_LOG(NEARCADE_LOG_TRACE, fmt, ##__VA_ARGS__)

#define PKT_GAMEPAD   0x01
#define PKT_MOUSE_REL 0x02
#define PKT_MOUSE_ABS 0x03
#define PKT_MOUSE_BTN 0x04
#define PKT_WHEEL     0x05
#define PKT_KEY       0x06
#define PKT_ALLOC_GP  0x10
#define PKT_FREE_GP   0x11
#define PKT_FLUSH     0x20
#define PKT_DESTROY   0xFF

#define W3C_A      (1<<0)
#define W3C_B      (1<<1)
#define W3C_Y      (1<<2)
#define W3C_X      (1<<3)
#define W3C_LB     (1<<4)
#define W3C_RB     (1<<5)
#define W3C_BACK   (1<<8)
#define W3C_START  (1<<9)
#define W3C_LS     (1<<10)
#define W3C_RS     (1<<11)
#define W3C_GUIDE  (1<<16)

typedef struct viewer_info {
    char  id[NEARCADE_VIEWER_ID_LEN];
    char  name[48];
    int   slot;
    int   active;
    nearcade_input_mode mode;
    nearcade_ctrl_type  ctrl_type;
    int   slot_allocated;
} viewer_info;

typedef struct {
    int slot_map[MAX_SLOTS];
    char slot_viewers[MAX_SLOTS][NEARCADE_VIEWER_ID_LEN];
    pthread_mutex_t lock;
} slot_manager;

typedef struct {
    nearcade_config config;
    atomic_int      running;
    atomic_int      capturing;
    viewer_info     viewers[MAX_VIEWERS];
    int             num_viewers;
    slot_manager    slots;
    nearcade_event_callback event_cb;
    void           *event_cb_userdata;
    pthread_mutex_t  viewer_lock;
    char            pin[8];
    char            lan_ip[64];
    pthread_t       event_thread;
} nearcade_state;

extern nearcade_state g_state;
extern int g_loglevel;

int input_init(const nearcade_config *config);
void input_shutdown(void);
int input_submit_gamepad(const nearcade_gamepad_packet *packet);
int input_submit_kbm(const char *viewer_id, const char *event_type,
                     const char *key, int dx, int dy);
int input_alloc_slot(uint8_t slot, uint16_t vendor, uint16_t product,
                     uint16_t version, const char *name);
int input_free_slot(uint8_t slot);
int input_flush_slot(uint8_t slot);

int capture_init(const nearcade_config *config);
int capture_start(void);
int capture_stop(void);
void capture_shutdown(void);

int signaling_init(const nearcade_config *config);
void signaling_shutdown(void);
int signaling_send_offer(const char *viewer_id, const char *sdp);
int signaling_send_answer(const char *viewer_id, const char *sdp);
int signaling_send_ice(const char *viewer_id, const char *candidate);
void signaling_get_url(char *buf, size_t buf_size);

static inline void fire_event(const nearcade_event *ev)
{
    if (!ev) {
        LOG_WARN("fire_event called with NULL event");
        return;
    }
    LOG_DEBUG("firing event type=%d", ev->type);
    if (g_state.event_cb) {
        g_state.event_cb(ev, g_state.event_cb_userdata);
    } else {
        LOG_TRACE("no event callback registered, event dropped");
    }
}

static inline int slot_claim(const char *viewer_id, nearcade_ctrl_type ctrl)
{
    (void)ctrl;
    if (!viewer_id) {
        LOG_WARN("slot_claim called with NULL viewer_id");
        return -1;
    }
    slot_manager *sm = &g_state.slots;
    pthread_mutex_lock(&sm->lock);

    for (int i = 0; i < MAX_SLOTS; i++) {
        if (sm->slot_map[i] == 0) {
            sm->slot_map[i] = 1;
            strncpy(sm->slot_viewers[i], viewer_id, NEARCADE_VIEWER_ID_LEN - 1);
            LOG_DEBUG("slot_claim: viewer=%s slot=%d", viewer_id, i);
            pthread_mutex_unlock(&sm->lock);
            return i;
        }
    }

    LOG_WARN("slot_claim: no free slots for viewer=%s", viewer_id);
    pthread_mutex_unlock(&sm->lock);
    return -1;
}

static inline void slot_release(int slot)
{
    if (slot < 0 || slot >= MAX_SLOTS) {
        LOG_WARN("slot_release: invalid slot=%d", slot);
        return;
    }
    slot_manager *sm = &g_state.slots;
    pthread_mutex_lock(&sm->lock);
    if (sm->slot_map[slot] == 0) {
        LOG_WARN("slot_release: slot=%d already free", slot);
        pthread_mutex_unlock(&sm->lock);
        return;
    }
    LOG_DEBUG("slot_release: slot=%d viewer=%s", slot, sm->slot_viewers[slot]);
    sm->slot_map[slot] = 0;
    memset(sm->slot_viewers[slot], 0, NEARCADE_VIEWER_ID_LEN);
    pthread_mutex_unlock(&sm->lock);
}

static inline const char *ctrl_name(nearcade_ctrl_type ctrl)
{
    switch (ctrl) {
        case NEARCADE_CTRL_XBOX360:   return "Xbox 360 Controller";
        case NEARCADE_CTRL_XBOXONE:   return "Xbox One Controller";
        case NEARCADE_CTRL_DS4:       return "Wireless Controller";
        case NEARCADE_CTRL_DUALSENSE: return "DualSense Wireless Controller";
        case NEARCADE_CTRL_SWITCH_PRO: return "Pro Controller";
        default: return "Xbox 360 Controller";
    }
}

static inline uint16_t ctrl_vendor(nearcade_ctrl_type ctrl)
{
    switch (ctrl) {
        case NEARCADE_CTRL_XBOX360:   return 0x045E;
        case NEARCADE_CTRL_XBOXONE:   return 0x045E;
        case NEARCADE_CTRL_DS4:       return 0x054C;
        case NEARCADE_CTRL_DUALSENSE: return 0x054C;
        case NEARCADE_CTRL_SWITCH_PRO: return 0x057E;
        default: return 0x045E;
    }
}

static inline uint16_t ctrl_product(nearcade_ctrl_type ctrl)
{
    switch (ctrl) {
        case NEARCADE_CTRL_XBOX360:   return 0x028E;
        case NEARCADE_CTRL_XBOXONE:   return 0x02EA;
        case NEARCADE_CTRL_DS4:       return 0x09CC;
        case NEARCADE_CTRL_DUALSENSE: return 0x0CE6;
        case NEARCADE_CTRL_SWITCH_PRO: return 0x2009;
        default: return 0x028E;
    }
}

static inline uint16_t ctrl_version(nearcade_ctrl_type ctrl)
{
    switch (ctrl) {
        case NEARCADE_CTRL_XBOX360:   return 0x0110;
        case NEARCADE_CTRL_XBOXONE:   return 0x0101;
        case NEARCADE_CTRL_DS4:       return 0x0100;
        case NEARCADE_CTRL_DUALSENSE: return 0x0100;
        case NEARCADE_CTRL_SWITCH_PRO: return 0x0001;
        default: return 0x0110;
    }
}

#endif
