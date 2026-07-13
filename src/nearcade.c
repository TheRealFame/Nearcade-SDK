#include "nearcade_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <net/if.h>
#include <ifaddrs.h>

nearcade_state g_state;
int g_loglevel = NEARCADE_LOG_NONE;

static int log_level_from_env(void)
{
    const char *env = getenv("NEARCADE_LOG_LEVEL");
    if (!env) return NEARCADE_LOG_NONE;
    if (strcmp(env, "trace") == 0) return NEARCADE_LOG_TRACE;
    if (strcmp(env, "debug") == 0) return NEARCADE_LOG_DEBUG;
    if (strcmp(env, "info")  == 0) return NEARCADE_LOG_INFO;
    if (strcmp(env, "warn")  == 0) return NEARCADE_LOG_WARN;
    if (strcmp(env, "error") == 0) return NEARCADE_LOG_ERROR;
    long lvl = strtol(env, NULL, 10);
    if (lvl >= NEARCADE_LOG_NONE && lvl <= NEARCADE_LOG_TRACE) return (int)lvl;
    return NEARCADE_LOG_NONE;
}

static int seeded = 0;
static void generate_pin(char *buf, size_t len)
{
    if (!seeded) {
        srand((unsigned)(time(NULL) ^ ((uintptr_t)buf & 0xFFFFFFFF)));
        seeded = 1;
    }
    snprintf(buf, len, "%04d", 1000 + rand() % 9000);
}

static void get_lan_ip(char *buf, size_t len)
{
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) < 0) {
        LOG_WARN("getifaddrs failed, falling back to 127.0.0.1");
        strncpy(buf, "127.0.0.1", len - 1);
        return;
    }

    int found = 0;
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) {
            LOG_TRACE("get_lan_ip: skipping ifa=%s (no addr or not AF_INET)", ifa->ifa_name ? ifa->ifa_name : "?");
            continue;
        }
        if (ifa->ifa_flags & IFF_LOOPBACK) {
            LOG_TRACE("get_lan_ip: skipping loopback %s", ifa->ifa_name);
            continue;
        }
        struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
        inet_ntop(AF_INET, &sa->sin_addr, buf, (socklen_t)len);
        LOG_DEBUG("get_lan_ip: found %s on %s", buf, ifa->ifa_name);
        found = 1;
        break;
    }

    if (!found) {
        LOG_WARN("get_lan_ip: no non-loopback IPv4 interface found, using 127.0.0.1");
        strncpy(buf, "127.0.0.1", len - 1);
    }
    freeifaddrs(ifaddr);
}

int nearcade_init(const nearcade_config *config)
{
    memset(&g_state, 0, sizeof(g_state));

    g_loglevel = log_level_from_env();
    LOG_INFO("Nearcade SDK v%d.%d.%d initializing, log_level=%d",
             NEARCADE_VERSION_MAJOR, NEARCADE_VERSION_MINOR,
             NEARCADE_VERSION_PATCH, g_loglevel);

    if (config) {
        g_state.config = *config;
        LOG_DEBUG("nearcade_init: using provided config (port=%d res=%dx%d bitrate=%d fps=%d audio=%d)",
                  config->port, config->screen_width, config->screen_height,
                  config->max_bitrate, config->fps, config->enable_audio);
    } else {
        g_state.config = (nearcade_config)NEARCADE_DEFAULT_CONFIG;
        LOG_DEBUG("nearcade_init: using default config");
    }

    if (g_state.config.port <= 0) {
        LOG_WARN("nearcade_init: invalid port %d, defaulting to 3000", g_state.config.port);
        g_state.config.port = 3000;
    }
    if (g_state.config.screen_width <= 0) {
        LOG_WARN("nearcade_init: invalid width %d, defaulting to 1920", g_state.config.screen_width);
        g_state.config.screen_width = 1920;
    }
    if (g_state.config.screen_height <= 0) {
        LOG_WARN("nearcade_init: invalid height %d, defaulting to 1080", g_state.config.screen_height);
        g_state.config.screen_height = 1080;
    }

    if (pthread_mutex_init(&g_state.viewer_lock, NULL) != 0) {
        LOG_ERROR("nearcade_init: viewer_lock init failed");
        return NEARCADE_ERR_INIT;
    }
    if (pthread_mutex_init(&g_state.slots.lock, NULL) != 0) {
        LOG_ERROR("nearcade_init: slots.lock init failed");
        pthread_mutex_destroy(&g_state.viewer_lock);
        return NEARCADE_ERR_INIT;
    }
    LOG_TRACE("nearcade_init: mutexes initialized");

    get_lan_ip(g_state.lan_ip, sizeof(g_state.lan_ip));
    generate_pin(g_state.pin, sizeof(g_state.pin));
    LOG_DEBUG("nearcade_init: LAN IP=%s PIN=%s", g_state.lan_ip, g_state.pin);

    LOG_DEBUG("nearcade_init: initializing input subsystem...");
    int rc = input_init(&g_state.config);
    LOG_DEBUG("nearcade_init: input_init returned %d", rc);
    if (rc != NEARCADE_OK) {
        LOG_ERROR("nearcade_init: input_init failed with error %d", rc);
        pthread_mutex_destroy(&g_state.slots.lock);
        pthread_mutex_destroy(&g_state.viewer_lock);
        return NEARCADE_ERR_INIT;
    }

    LOG_DEBUG("nearcade_init: initializing capture subsystem...");
    rc = capture_init(&g_state.config);
    LOG_DEBUG("nearcade_init: capture_init returned %d", rc);
    if (rc != NEARCADE_OK) {
        LOG_ERROR("nearcade_init: capture_init failed with error %d", rc);
        input_shutdown();
        pthread_mutex_destroy(&g_state.slots.lock);
        pthread_mutex_destroy(&g_state.viewer_lock);
        return NEARCADE_ERR_INIT;
    }

#ifdef NEARCADE_HAS_SIGNALING
    LOG_DEBUG("nearcade_init: initializing signaling subsystem...");
    rc = signaling_init(&g_state.config);
    LOG_DEBUG("nearcade_init: signaling_init returned %d", rc);
    if (rc != NEARCADE_OK) {
        LOG_ERROR("nearcade_init: signaling_init failed with error %d", rc);
        capture_shutdown();
        input_shutdown();
        pthread_mutex_destroy(&g_state.slots.lock);
        pthread_mutex_destroy(&g_state.viewer_lock);
        return NEARCADE_ERR_INIT;
    }
#else
    LOG_INFO("nearcade_init: signaling disabled (NEARCADE_HAS_SIGNALING not set)");
#endif

    atomic_store(&g_state.running, 1);
    LOG_INFO("Nearcade v%d.%d.%d initialized on port %d",
             NEARCADE_VERSION_MAJOR, NEARCADE_VERSION_MINOR,
             NEARCADE_VERSION_PATCH, g_state.config.port);
    LOG_INFO("LAN: %s  PIN: %s", g_state.lan_ip, g_state.pin);
    return NEARCADE_OK;
}

int nearcade_start_capture(void)
{
    LOG_TRACE("nearcade_start_capture: entering");
    if (atomic_load(&g_state.capturing)) {
        LOG_DEBUG("nearcade_start_capture: already capturing");
        return NEARCADE_OK;
    }
    LOG_DEBUG("nearcade_start_capture: calling capture_start()");
    int ret = capture_start();
    LOG_DEBUG("nearcade_start_capture: capture_start returned %d", ret);
    if (ret == NEARCADE_OK) {
        atomic_store(&g_state.capturing, 1);
        LOG_INFO("capture started");
    } else {
        LOG_ERROR("nearcade_start_capture: capture_start failed: %d", ret);
    }
    return ret;
}

int nearcade_stop_capture(void)
{
    LOG_TRACE("nearcade_stop_capture: entering");
    if (!atomic_load(&g_state.capturing)) {
        LOG_DEBUG("nearcade_stop_capture: not capturing");
        return NEARCADE_OK;
    }
    capture_stop();
    atomic_store(&g_state.capturing, 0);
    LOG_INFO("capture stopped");
    return NEARCADE_OK;
}

int nearcade_poll_events(int timeout_ms)
{
    /* STUB: no event loop implemented yet */
    LOG_TRACE("nearcade_poll_events: timeout_ms=%d (stub)", timeout_ms);
    return NEARCADE_OK;
}

void nearcade_shutdown(void)
{
    LOG_INFO("nearcade_shutdown: shutting down...");
    if (!atomic_load(&g_state.running)) {
        LOG_DEBUG("nearcade_shutdown: not running");
        return;
    }
    atomic_store(&g_state.running, 0);
    atomic_store(&g_state.capturing, 0);

    LOG_DEBUG("nearcade_shutdown: stopping capture...");
    capture_shutdown();
    LOG_DEBUG("nearcade_shutdown: shutting down input...");
    input_shutdown();
#ifdef NEARCADE_HAS_SIGNALING
    LOG_DEBUG("nearcade_shutdown: shutting down signaling...");
    signaling_shutdown();
#endif

    pthread_mutex_destroy(&g_state.viewer_lock);
    pthread_mutex_destroy(&g_state.slots.lock);

    LOG_INFO("Nearcade shut down");
}

int nearcade_set_event_callback(nearcade_event_callback cb, void *userdata)
{
    LOG_DEBUG("nearcade_set_event_callback: cb=%p userdata=%p", (void*)cb, userdata);
    g_state.event_cb = cb;
    g_state.event_cb_userdata = userdata;
    return NEARCADE_OK;
}

int nearcade_get_signaling_url(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) {
        LOG_ERROR("nearcade_get_signaling_url: NULL buffer or zero size");
        return NEARCADE_ERR_INVALID_ARG;
    }
#ifdef NEARCADE_HAS_SIGNALING
    signaling_get_url(buf, buf_size);
#else
    snprintf(buf, buf_size, "ws://%s:%d", g_state.lan_ip, g_state.config.port);
#endif
    LOG_DEBUG("nearcade_get_signaling_url: %s", buf);
    return NEARCADE_OK;
}

int nearcade_get_lan_ip(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return NEARCADE_ERR_INVALID_ARG;
    strncpy(buf, g_state.lan_ip, buf_size - 1);
    LOG_DEBUG("nearcade_get_lan_ip: %s", buf);
    return NEARCADE_OK;
}

int nearcade_get_pin(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return NEARCADE_ERR_INVALID_ARG;
    strncpy(buf, g_state.pin, buf_size - 1);
    LOG_DEBUG("nearcade_get_pin: %s", buf);
    return NEARCADE_OK;
}

int nearcade_regenerate_pin(void)
{
    generate_pin(g_state.pin, sizeof(g_state.pin));
    LOG_INFO("nearcade_regenerate_pin: new PIN=%s", g_state.pin);
    return NEARCADE_OK;
}

int nearcade_submit_gamepad(const nearcade_gamepad_packet *packet)
{
    if (!packet) {
        LOG_ERROR("nearcade_submit_gamepad: NULL packet");
        return NEARCADE_ERR_INVALID_ARG;
    }
    LOG_TRACE("nearcade_submit_gamepad: slot=%d lx=%d ly=%d rx=%d ry=%d lt=%d rt=%d buttons=0x%04x hx=%d hy=%d",
              packet->slot, packet->lx, packet->ly, packet->rx, packet->ry,
              packet->lt, packet->rt, packet->buttons, packet->hx, packet->hy);
    return input_submit_gamepad(packet);
}

int nearcade_submit_kbm(const char *viewer_id, const char *event_type,
                        const char *key, int dx, int dy)
{
    if (!viewer_id || !event_type) {
        LOG_ERROR("nearcade_submit_kbm: NULL args (viewer_id=%p event_type=%p)", (void*)viewer_id, (void*)event_type);
        return NEARCADE_ERR_INVALID_ARG;
    }
    LOG_TRACE("nearcade_submit_kbm: viewer=%s event=%s key=%s dx=%d dy=%d",
              viewer_id, event_type, key ? key : "NULL", dx, dy);
    return input_submit_kbm(viewer_id, event_type, key, dx, dy);
}

int nearcade_flush_slot(uint8_t slot)
{
    LOG_TRACE("nearcade_flush_slot: slot=%d", slot);
    return input_flush_slot(slot);
}

int nearcade_disconnect_viewer(const char *viewer_id)
{
    if (!viewer_id) {
        LOG_ERROR("nearcade_disconnect_viewer: NULL viewer_id");
        return NEARCADE_ERR_INVALID_ARG;
    }

    LOG_DEBUG("nearcade_disconnect_viewer: viewer=%s", viewer_id);
    pthread_mutex_lock(&g_state.viewer_lock);
    int found = 0;
    for (int i = 0; i < MAX_VIEWERS; i++) {
        if (g_state.viewers[i].active &&
            strcmp(g_state.viewers[i].id, viewer_id) == 0) {
            int slot = g_state.viewers[i].slot;
            LOG_DEBUG("nearcade_disconnect_viewer: found viewer at idx=%d slot=%d", i, slot);
            input_free_slot((uint8_t)slot);
            slot_release(slot);
            g_state.viewers[i].active = 0;
            g_state.num_viewers--;
            found = 1;
            break;
        }
    }
    if (!found) {
        LOG_WARN("nearcade_disconnect_viewer: viewer '%s' not found", viewer_id);
    }
    pthread_mutex_unlock(&g_state.viewer_lock);
    return NEARCADE_OK;
}

int nearcade_set_viewer_input_mode(const char *viewer_id, nearcade_input_mode mode)
{
    if (!viewer_id) {
        LOG_ERROR("nearcade_set_viewer_input_mode: NULL viewer_id");
        return NEARCADE_ERR_INVALID_ARG;
    }

    LOG_DEBUG("nearcade_set_viewer_input_mode: viewer=%s mode=%d", viewer_id, mode);
    pthread_mutex_lock(&g_state.viewer_lock);
    for (int i = 0; i < MAX_VIEWERS; i++) {
        if (g_state.viewers[i].active &&
            strcmp(g_state.viewers[i].id, viewer_id) == 0) {
            g_state.viewers[i].mode = mode;
            LOG_DEBUG("nearcade_set_viewer_input_mode: updated viewer idx=%d mode=%d", i, mode);
            pthread_mutex_unlock(&g_state.viewer_lock);
            return NEARCADE_OK;
        }
    }
    LOG_WARN("nearcade_set_viewer_input_mode: viewer '%s' not found", viewer_id);
    pthread_mutex_unlock(&g_state.viewer_lock);
    return NEARCADE_ERR_INVALID_ARG;
}

int nearcade_set_controller_type(const char *viewer_id, nearcade_ctrl_type ctrl)
{
    if (!viewer_id) {
        LOG_ERROR("nearcade_set_controller_type: NULL viewer_id");
        return NEARCADE_ERR_INVALID_ARG;
    }

    LOG_DEBUG("nearcade_set_controller_type: viewer=%s ctrl=%d", viewer_id, ctrl);
    pthread_mutex_lock(&g_state.viewer_lock);
    for (int i = 0; i < MAX_VIEWERS; i++) {
        if (g_state.viewers[i].active &&
            strcmp(g_state.viewers[i].id, viewer_id) == 0) {
            g_state.viewers[i].ctrl_type = ctrl;
            LOG_DEBUG("nearcade_set_controller_type: updated viewer idx=%d ctrl=%d", i, ctrl);
            pthread_mutex_unlock(&g_state.viewer_lock);
            return NEARCADE_OK;
        }
    }
    LOG_WARN("nearcade_set_controller_type: viewer '%s' not found", viewer_id);
    pthread_mutex_unlock(&g_state.viewer_lock);
    return NEARCADE_ERR_INVALID_ARG;
}

int nearcade_send_offer(const char *viewer_id, const char *sdp)
{
    LOG_DEBUG("nearcade_send_offer: viewer=%s sdp_len=%zu", viewer_id ? viewer_id : "NULL", sdp ? strlen(sdp) : 0);
#ifdef NEARCADE_HAS_SIGNALING
    return signaling_send_offer(viewer_id, sdp);
#else
    (void)viewer_id; (void)sdp;
    LOG_WARN("nearcade_send_offer: signaling not compiled in");
    return NEARCADE_OK;
#endif
}

int nearcade_send_answer(const char *viewer_id, const char *sdp)
{
    LOG_DEBUG("nearcade_send_answer: viewer=%s sdp_len=%zu", viewer_id ? viewer_id : "NULL", sdp ? strlen(sdp) : 0);
#ifdef NEARCADE_HAS_SIGNALING
    return signaling_send_answer(viewer_id, sdp);
#else
    (void)viewer_id; (void)sdp;
    LOG_WARN("nearcade_send_answer: signaling not compiled in");
    return NEARCADE_OK;
#endif
}

int nearcade_send_ice_candidate(const char *viewer_id, const char *candidate)
{
    LOG_DEBUG("nearcade_send_ice_candidate: viewer=%s candidate_len=%zu", viewer_id ? viewer_id : "NULL", candidate ? strlen(candidate) : 0);
#ifdef NEARCADE_HAS_SIGNALING
    return signaling_send_ice(viewer_id, candidate);
#else
    (void)viewer_id; (void)candidate;
    LOG_WARN("nearcade_send_ice_candidate: signaling not compiled in");
    return NEARCADE_OK;
#endif
}
