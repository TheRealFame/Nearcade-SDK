#include "nearcade_internal.h"
#include "webrtc_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#else
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <net/if.h>
#include <ifaddrs.h>
#endif

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
#ifdef _WIN32
    PIP_ADAPTER_ADDRESSES addrs = NULL, aa;
    ULONG sz = 0;
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, NULL, NULL, &sz) != ERROR_BUFFER_OVERFLOW) {
        LOG_WARN("get_lan_ip: GetAdaptersAddresses size query failed");
        strncpy(buf, "127.0.0.1", len - 1);
        return;
    }
    addrs = (PIP_ADAPTER_ADDRESSES)malloc(sz);
    if (!addrs) {
        LOG_WARN("get_lan_ip: malloc failed");
        strncpy(buf, "127.0.0.1", len - 1);
        return;
    }
    int found = 0;
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, NULL, addrs, &sz) == NO_ERROR) {
        for (aa = addrs; aa; aa = aa->Next) {
            if (aa->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            PIP_ADAPTER_UNICAST_ADDRESS ua = aa->FirstUnicastAddress;
            if (!ua) continue;
            struct sockaddr_in *sa = (struct sockaddr_in*)ua->Address.lpSockaddr;
            inet_ntop(AF_INET, &sa->sin_addr, buf, (socklen_t)len);
            found = 1;
            break;
        }
    }
    free(addrs);
    if (!found) {
        LOG_WARN("get_lan_ip: no non-loopback IPv4 interface found, using 127.0.0.1");
        strncpy(buf, "127.0.0.1", len - 1);
    }
#else
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) < 0) {
        LOG_WARN("getifaddrs failed, falling back to 127.0.0.1");
        strncpy(buf, "127.0.0.1", len - 1);
        return;
    }
    int found = 0;
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;
        struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
        inet_ntop(AF_INET, &sa->sin_addr, buf, (socklen_t)len);
        found = 1;
        break;
    }
    if (!found) {
        LOG_WARN("get_lan_ip: no non-loopback IPv4 interface found, using 127.0.0.1");
        strncpy(buf, "127.0.0.1", len - 1);
    }
    freeifaddrs(ifaddr);
#endif
}

/* ── WebRTC integration callbacks ──────────────────────────────────────── */

static void webrtc_send_via_signaling(const char *viewer_id, const char *data, void *userdata)
{
    (void)userdata;
#ifdef NEARCADE_HAS_SIGNALING
    signaling_send_to_viewer(viewer_id, data);
#else
    (void)viewer_id;
    (void)data;
#endif
}

#ifdef NEARCADE_HAS_SIGNALING
static void on_signaling_msg(const char *vid, const char *data, void *ud)
{
    (void)ud;
    webrtc_handle_signaling(vid, data);
}

static void on_signaling_viewer(int idx, int joined, void *ud)
{
    (void)ud;
    if (joined) {
        char vid[64];
        snprintf(vid, sizeof(vid), "viewer_%d", idx);
        webrtc_viewer_joined(idx, vid);
    } else {
        webrtc_viewer_left(idx);
    }
}
#endif

static void webrtc_received_input(const nearcade_gamepad_packet *pkt, void *userdata)
{
    (void)userdata;
    input_submit_gamepad(pkt);

    nearcade_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = NEARCADE_EVENT_INPUT_PACKET;
    ev.data.input_packet.packet = *pkt;
    fire_event(&ev);
}

static void webrtc_viewer_state(int viewer_idx, int connected, void *userdata)
{
    (void)userdata;
    nearcade_event ev;
    memset(&ev, 0, sizeof(ev));

    if (connected) {
        ev.type = NEARCADE_EVENT_VIEWER_JOINED;
        snprintf(ev.data.viewer_joined.viewer_id, sizeof(ev.data.viewer_joined.viewer_id),
                 "viewer_%d", viewer_idx);
        snprintf(ev.data.viewer_joined.name, sizeof(ev.data.viewer_joined.name),
                 "WebRTC Viewer %d", viewer_idx);
        LOG_INFO("WebRTC viewer_%d connected", viewer_idx);
    } else {
        ev.type = NEARCADE_EVENT_VIEWER_LEFT;
        snprintf(ev.data.viewer_left.viewer_id, sizeof(ev.data.viewer_left.viewer_id),
                 "viewer_%d", viewer_idx);
        LOG_INFO("WebRTC viewer_%d disconnected", viewer_idx);
    }

    fire_event(&ev);
}

/* ── Public API ────────────────────────────────────────────────────────── */

int nearcade_init(const nearcade_config *config)
{
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        LOG_ERROR("nearcade_init: WSAStartup failed");
        return NEARCADE_ERR_NETWORK;
    }
#endif
    memset(&g_state, 0, sizeof(g_state));
    g_loglevel = log_level_from_env();
    LOG_INFO("Nearcade SDK v%d.%d.%d initializing, log_level=%d",
             NEARCADE_VERSION_MAJOR, NEARCADE_VERSION_MINOR,
             NEARCADE_VERSION_PATCH, g_loglevel);

    if (config) {
        g_state.config = *config;
    } else {
        g_state.config = (nearcade_config)NEARCADE_DEFAULT_CONFIG;
    }

    if (g_state.config.port <= 0) {
        LOG_WARN("nearcade_init: invalid port %d, defaulting to 3000", g_state.config.port);
        g_state.config.port = 3000;
    }
    if (g_state.config.screen_width <= 0) {
        g_state.config.screen_width = 1920;
    }
    if (g_state.config.screen_height <= 0) {
        g_state.config.screen_height = 1080;
    }

    if (nearcade_mutex_init(&g_state.viewer_lock) != 0) {
        LOG_ERROR("nearcade_init: viewer_lock init failed");
        return NEARCADE_ERR_INIT;
    }
    if (nearcade_mutex_init(&g_state.slots.lock) != 0) {
        nearcade_mutex_destroy(&g_state.viewer_lock);
        return NEARCADE_ERR_INIT;
    }

    get_lan_ip(g_state.lan_ip, sizeof(g_state.lan_ip));
    generate_pin(g_state.pin, sizeof(g_state.pin));

    int rc = input_init(&g_state.config);
    if (rc != NEARCADE_OK) {
        LOG_ERROR("nearcade_init: input_init failed: %d", rc);
        nearcade_mutex_destroy(&g_state.slots.lock);
        nearcade_mutex_destroy(&g_state.viewer_lock);
        return NEARCADE_ERR_INIT;
    }

    rc = capture_init(&g_state.config);
    if (rc != NEARCADE_OK) {
        LOG_ERROR("nearcade_init: capture_init failed: %d", rc);
        input_shutdown();
        nearcade_mutex_destroy(&g_state.slots.lock);
        nearcade_mutex_destroy(&g_state.viewer_lock);
        return NEARCADE_ERR_INIT;
    }

    rc = webrtc_init(&g_state.config);
    if (rc != NEARCADE_OK) {
        LOG_ERROR("nearcade_init: webrtc_init failed: %d", rc);
        capture_shutdown();
        input_shutdown();
        nearcade_mutex_destroy(&g_state.slots.lock);
        nearcade_mutex_destroy(&g_state.viewer_lock);
        return NEARCADE_ERR_INIT;
    }

    webrtc_set_send_callback(webrtc_send_via_signaling, NULL);
    webrtc_set_input_callback(webrtc_received_input, NULL);
    webrtc_set_viewer_callback(webrtc_viewer_state, NULL);

#ifdef NEARCADE_HAS_SIGNALING
    signaling_set_internal_callbacks(
        on_signaling_msg, NULL,
        on_signaling_viewer, NULL
    );

    rc = signaling_init(&g_state.config);
    if (rc != NEARCADE_OK) {
        LOG_ERROR("nearcade_init: signaling_init failed: %d", rc);
        webrtc_shutdown();
        capture_shutdown();
        input_shutdown();
        nearcade_mutex_destroy(&g_state.slots.lock);
        nearcade_mutex_destroy(&g_state.viewer_lock);
        return NEARCADE_ERR_INIT;
    }
#else
    LOG_INFO("nearcade_init: signaling disabled (NEARCADE_HAS_SIGNALING not set)");
#endif

    atomic_store(&g_state.running, 1);
    LOG_INFO("Nearcade v%d.%d.%d initialized on port %d  LAN: %s  PIN: %s",
             NEARCADE_VERSION_MAJOR, NEARCADE_VERSION_MINOR,
             NEARCADE_VERSION_PATCH, g_state.config.port,
             g_state.lan_ip, g_state.pin);
    return NEARCADE_OK;
}

int nearcade_start_streaming(void)
{
    LOG_INFO("nearcade_start_streaming: accepting viewer connections");
    atomic_store(&g_state.streaming, 1);

    nearcade_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = NEARCADE_EVENT_STREAMING;
    ev.data.streaming.viewer_count = 0;
    fire_event(&ev);

    return NEARCADE_OK;
}

int nearcade_stop_streaming(void)
{
    LOG_INFO("nearcade_stop_streaming: stopping");
    atomic_store(&g_state.streaming, 0);
    return NEARCADE_OK;
}

int nearcade_send_h264(const uint8_t *data, size_t size, int64_t timestamp_us)
{
    return webrtc_send_h264(data, size, timestamp_us);
}

int nearcade_send_frame(const uint8_t *data, int width, int height,
                        nearcade_pixel_format fmt, int64_t timestamp_us)
{
    (void)data; (void)width; (void)height; (void)fmt; (void)timestamp_us;
    LOG_ERROR("nearcade_send_frame: not implemented yet (use nearcade_send_h264)");
    return NEARCADE_ERR_INIT;
}

int nearcade_start_capture(void)
{
    LOG_TRACE("nearcade_start_capture: entering");
    if (atomic_load(&g_state.capturing)) {
        LOG_DEBUG("nearcade_start_capture: already capturing");
        return NEARCADE_OK;
    }
    int ret = capture_start();
    if (ret == NEARCADE_OK) {
        atomic_store(&g_state.capturing, 1);
        LOG_INFO("legacy capture started");
    }
    return ret;
}

int nearcade_stop_capture(void)
{
    LOG_TRACE("nearcade_stop_capture: entering");
    if (!atomic_load(&g_state.capturing)) return NEARCADE_OK;
    capture_stop();
    atomic_store(&g_state.capturing, 0);
    LOG_INFO("legacy capture stopped");
    return NEARCADE_OK;
}

int nearcade_poll_events(int timeout_ms)
{
    LOG_TRACE("nearcade_poll_events: timeout_ms=%d (stub)", timeout_ms);
    return NEARCADE_OK;
}

void nearcade_shutdown(void)
{
    LOG_INFO("nearcade_shutdown: shutting down...");
    if (!atomic_load(&g_state.running)) return;
    atomic_store(&g_state.running, 0);
    atomic_store(&g_state.capturing, 0);
    atomic_store(&g_state.streaming, 0);

    webrtc_shutdown();
    capture_shutdown();
    input_shutdown();
#ifdef NEARCADE_HAS_SIGNALING
    signaling_shutdown();
#endif

    nearcade_mutex_destroy(&g_state.viewer_lock);
    nearcade_mutex_destroy(&g_state.slots.lock);
#ifdef _WIN32
    WSACleanup();
#endif
    LOG_INFO("Nearcade shut down");
}

int nearcade_set_event_callback(nearcade_event_callback cb, void *userdata)
{
    g_state.event_cb = cb;
    g_state.event_cb_userdata = userdata;
    return NEARCADE_OK;
}

int nearcade_get_signaling_url(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return NEARCADE_ERR_INVALID_ARG;
#ifdef NEARCADE_HAS_SIGNALING
    signaling_get_url(buf, buf_size);
#else
    snprintf(buf, buf_size, "ws://%s:%d", g_state.lan_ip, g_state.config.port);
#endif
    return NEARCADE_OK;
}

int nearcade_get_lan_ip(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return NEARCADE_ERR_INVALID_ARG;
    strncpy(buf, g_state.lan_ip, buf_size - 1);
    return NEARCADE_OK;
}

int nearcade_get_pin(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) return NEARCADE_ERR_INVALID_ARG;
    strncpy(buf, g_state.pin, buf_size - 1);
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
    return input_submit_gamepad(packet);
}

int nearcade_submit_kbm(const char *viewer_id, const char *event_type,
                        const char *key, int dx, int dy)
{
    if (!viewer_id || !event_type) {
        LOG_ERROR("nearcade_submit_kbm: NULL args");
        return NEARCADE_ERR_INVALID_ARG;
    }
    return input_submit_kbm(viewer_id, event_type, key, dx, dy);
}

int nearcade_flush_slot(uint8_t slot)
{
    return input_flush_slot(slot);
}

int nearcade_disconnect_viewer(const char *viewer_id)
{
    if (!viewer_id) return NEARCADE_ERR_INVALID_ARG;
    LOG_DEBUG("nearcade_disconnect_viewer: viewer=%s", viewer_id);
    nearcade_mutex_lock(&g_state.viewer_lock);
    for (int i = 0; i < MAX_VIEWERS; i++) {
        if (g_state.viewers[i].active &&
            strcmp(g_state.viewers[i].id, viewer_id) == 0) {
            int slot = g_state.viewers[i].slot;
            input_free_slot((uint8_t)slot);
            slot_release(slot);
            g_state.viewers[i].active = 0;
            g_state.num_viewers--;
            break;
        }
    }
    nearcade_mutex_unlock(&g_state.viewer_lock);
    return NEARCADE_OK;
}

int nearcade_set_viewer_input_mode(const char *viewer_id, nearcade_input_mode mode)
{
    if (!viewer_id) return NEARCADE_ERR_INVALID_ARG;
    nearcade_mutex_lock(&g_state.viewer_lock);
    for (int i = 0; i < MAX_VIEWERS; i++) {
        if (g_state.viewers[i].active &&
            strcmp(g_state.viewers[i].id, viewer_id) == 0) {
            g_state.viewers[i].mode = mode;
            nearcade_mutex_unlock(&g_state.viewer_lock);
            return NEARCADE_OK;
        }
    }
    nearcade_mutex_unlock(&g_state.viewer_lock);
    return NEARCADE_ERR_INVALID_ARG;
}

int nearcade_set_controller_type(const char *viewer_id, nearcade_ctrl_type ctrl)
{
    if (!viewer_id) return NEARCADE_ERR_INVALID_ARG;
    nearcade_mutex_lock(&g_state.viewer_lock);
    for (int i = 0; i < MAX_VIEWERS; i++) {
        if (g_state.viewers[i].active &&
            strcmp(g_state.viewers[i].id, viewer_id) == 0) {
            g_state.viewers[i].ctrl_type = ctrl;
            nearcade_mutex_unlock(&g_state.viewer_lock);
            return NEARCADE_OK;
        }
    }
    nearcade_mutex_unlock(&g_state.viewer_lock);
    return NEARCADE_ERR_INVALID_ARG;
}

int nearcade_send_offer(const char *viewer_id, const char *sdp)
{
    LOG_WARN("nearcade_send_offer: DEPRECATED — WebRTC is handled internally now");
    (void)viewer_id; (void)sdp;
    return NEARCADE_OK;
}

int nearcade_send_answer(const char *viewer_id, const char *sdp)
{
    LOG_WARN("nearcade_send_answer: DEPRECATED — WebRTC is handled internally now");
#ifdef NEARCADE_HAS_SIGNALING
    return signaling_send_answer(viewer_id, sdp);
#else
    (void)viewer_id; (void)sdp;
    return NEARCADE_OK;
#endif
}

int nearcade_send_ice_candidate(const char *viewer_id, const char *candidate)
{
    LOG_WARN("nearcade_send_ice_candidate: DEPRECATED — WebRTC is handled internally now");
#ifdef NEARCADE_HAS_SIGNALING
    return signaling_send_ice(viewer_id, candidate);
#else
    (void)viewer_id; (void)candidate;
    return NEARCADE_OK;
#endif
}
