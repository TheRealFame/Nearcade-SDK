#include "nearcade_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <libwebsockets.h>

static struct lws_context *g_wsi = NULL;
static struct lws *g_host_ws = NULL;
static struct lws *g_viewer_ws[MAX_VIEWERS];
static int g_num_viewers = 0;
static pthread_t g_signaling_thread;
static int g_port = 3000;
static pthread_mutex_t g_ws_lock = PTHREAD_MUTEX_INITIALIZER;

enum protocols {
    PROTOCOL_HTTP = 0,
    PROTOCOL_SIGNALING,
    PROTOCOL_COUNT
};

/* ── JSON-escape a string for embedding in JSON values ─────────────────────── */
static void json_escape(const char *in, char *out, size_t out_size)
{
    size_t j = 0;
    for (size_t i = 0; in[i] && j < out_size - 1; i++) {
        switch (in[i]) {
            case '"':  if (j + 2 < out_size) { out[j++] = '\\'; out[j++] = '"'; } break;
            case '\\': if (j + 2 < out_size) { out[j++] = '\\'; out[j++] = '\\'; } break;
            case '\n': if (j + 2 < out_size) { out[j++] = '\\'; out[j++] = 'n'; } break;
            case '\r': if (j + 2 < out_size) { out[j++] = '\\'; out[j++] = 'r'; } break;
            case '\t': if (j + 2 < out_size) { out[j++] = '\\'; out[j++] = 't'; } break;
            default:   out[j++] = in[i]; break;
        }
    }
    out[j] = '\0';
}

/* ── Find viewer WS by viewer_id string "viewer_N" ────────────────────────── */
static struct lws *viewer_ws_by_id(const char *viewer_id)
{
    if (!viewer_id) return NULL;
    int idx = -1;
    if (sscanf(viewer_id, "viewer_%d", &idx) == 1 && idx >= 0 && idx < MAX_VIEWERS) {
        return g_viewer_ws[idx];
    }
    return NULL;
}

static int callback_signaling(struct lws *wsi, enum lws_callback_reasons reason,
                              void *user, void *in, size_t len)
{
    (void)user;

    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED: {
            char uri[256];
            lws_hdr_copy(wsi, uri, sizeof(uri), WSI_TOKEN_GET_URI);
            LOG_DEBUG("signaling: WebSocket connected: uri=%s", uri);

            pthread_mutex_lock(&g_ws_lock);
            if (strstr(uri, "/host")) {
                if (g_host_ws) {
                    LOG_WARN("signaling: host already connected, replacing old connection");
                }
                g_host_ws = wsi;
                LOG_INFO("signaling: host connected via %s", uri);
            } else if (strstr(uri, "/viewer")) {
                int found = -1;
                for (int i = 0; i < MAX_VIEWERS; i++) {
                    if (g_viewer_ws[i] == NULL) {
                        g_viewer_ws[i] = wsi;
                        g_num_viewers++;
                        found = i;
                        break;
                    }
                }
                if (found >= 0) {
                    LOG_INFO("signaling: viewer connected slot=%d total=%d", found, g_num_viewers);
                } else {
                    LOG_WARN("signaling: max viewers (%d) reached, rejecting", MAX_VIEWERS);
                }
            } else {
                LOG_WARN("signaling: unknown URI '%s', routing as viewer", uri);
                for (int i = 0; i < MAX_VIEWERS; i++) {
                    if (g_viewer_ws[i] == NULL) {
                        g_viewer_ws[i] = wsi;
                        g_num_viewers++;
                        break;
                    }
                }
            }
            pthread_mutex_unlock(&g_ws_lock);
            break;
        }

        case LWS_CALLBACK_CLOSED: {
            pthread_mutex_lock(&g_ws_lock);
            if (wsi == g_host_ws) {
                g_host_ws = NULL;
                LOG_INFO("signaling: host disconnected");
            } else {
                for (int i = 0; i < MAX_VIEWERS; i++) {
                    if (g_viewer_ws[i] == wsi) {
                        g_viewer_ws[i] = NULL;
                        g_num_viewers--;
                        LOG_DEBUG("signaling: viewer_%d disconnected (remaining=%d)", i, g_num_viewers);
                        break;
                    }
                }
            }
            pthread_mutex_unlock(&g_ws_lock);
            break;
        }

        case LWS_CALLBACK_RECEIVE: {
            char rx_buf[4096];
            size_t rx_len = len;
            if (rx_len > sizeof(rx_buf) - 1) rx_len = sizeof(rx_buf) - 1;
            memcpy(rx_buf, in, rx_len);
            rx_buf[rx_len] = '\0';

            LOG_DEBUG("signaling: received %zu bytes from %s",
                      rx_len,
                      (wsi == g_host_ws) ? "host" : "viewer");
            LOG_TRACE("signaling: raw data: %.*s", (int)rx_len, rx_buf);

            nearcade_event ev = { .type = NEARCADE_EVENT_SIGNALING };
            strncpy(ev.data.signaling.data, rx_buf, sizeof(ev.data.signaling.data) - 1);

            pthread_mutex_lock(&g_ws_lock);
            if (wsi == g_host_ws) {
                snprintf(ev.data.signaling.viewer_id, sizeof(ev.data.signaling.viewer_id), "host");
                LOG_TRACE("signaling: message from host");
            } else {
                int found = 0;
                for (int i = 0; i < MAX_VIEWERS; i++) {
                    if (g_viewer_ws[i] == wsi) {
                        snprintf(ev.data.signaling.viewer_id, sizeof(ev.data.signaling.viewer_id), "viewer_%d", i);
                        LOG_TRACE("signaling: message from viewer_%d", i);
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    LOG_WARN("signaling: message from unknown WS, dropping");
                    pthread_mutex_unlock(&g_ws_lock);
                    return 0;
                }
            }
            pthread_mutex_unlock(&g_ws_lock);

            fire_event(&ev);
            break;
        }

        default:
            break;
    }
    return 0;
}

static struct lws_protocols protocols[] = {
    {
        .name = "http",
        .callback = callback_signaling,
        .per_session_data_size = 0,
        .rx_buffer_size = 4096,
    },
    {
        .name = "nearcade-signaling",
        .callback = callback_signaling,
        .per_session_data_size = 0,
        .rx_buffer_size = 4096,
    },
    { NULL, NULL, 0, 0 }
};

static void *signaling_thread(void *arg)
{
    (void)arg;
    LOG_DEBUG("signaling_thread: started");
    while (1) {
        pthread_mutex_lock(&g_ws_lock);
        int alive = (g_wsi != NULL);
        pthread_mutex_unlock(&g_ws_lock);
        if (!alive) break;
        lws_service(g_wsi, 50);
    }
    LOG_DEBUG("signaling_thread: exiting");
    return NULL;
}

int signaling_init(const nearcade_config *config)
{
    g_port = config->port;
    LOG_DEBUG("signaling_init: creating lws context on port %d", g_port);

    struct lws_context_creation_info info = {
        .port = g_port,
        .iface = NULL,
        .protocols = protocols,
        .extensions = NULL,
        .gid = -1,
        .uid = -1,
        .options = LWS_SERVER_OPTION_VALIDATE_UTF8,
        .max_http_header_pool = 64,
        .timeout_secs = 30,
    };

    g_wsi = lws_create_context(&info);
    if (!g_wsi) {
        LOG_ERROR("signaling_init: lws_create_context failed on port %d", g_port);
        return NEARCADE_ERR_NETWORK;
    }

    LOG_DEBUG("signaling_init: starting signaling thread...");
    if (pthread_create(&g_signaling_thread, NULL, signaling_thread, NULL) != 0) {
        LOG_ERROR("signaling_init: pthread_create failed");
        lws_context_destroy(g_wsi);
        g_wsi = NULL;
        return NEARCADE_ERR_INIT;
    }

    LOG_INFO("signaling: WebSocket signaling server on port %d", g_port);
    return NEARCADE_OK;
}

void signaling_shutdown(void)
{
    LOG_DEBUG("signaling_shutdown: entering");
    pthread_mutex_lock(&g_ws_lock);
    struct lws_context *ctx = g_wsi;
    g_wsi = NULL;
    g_host_ws = NULL;
    memset(g_viewer_ws, 0, sizeof(g_viewer_ws));
    g_num_viewers = 0;
    pthread_mutex_unlock(&g_ws_lock);

    if (ctx) {
        lws_context_destroy(ctx);
        LOG_DEBUG("signaling_shutdown: lws context destroyed");
    }
    /* The signaling thread will exit when g_wsi becomes NULL */
}

void signaling_get_url(char *buf, size_t buf_size)
{
    snprintf(buf, buf_size, "ws://%s:%d", g_state.lan_ip, g_port);
    LOG_TRACE("signaling_get_url: %s", buf);
}

static int send_to_lws(struct lws *wsi, const char *data)
{
    if (!wsi || !data) {
        LOG_WARN("send_to_lws: NULL wsi=%p data=%p", (void*)wsi, (void*)data);
        return -1;
    }
    size_t len = strlen(data);
    if (len == 0) {
        LOG_WARN("send_to_lws: empty data");
        return -1;
    }

    unsigned char *buf = (unsigned char *)malloc(LWS_PRE + len);
    if (!buf) {
        LOG_ERROR("send_to_lws: malloc(%zu) failed", LWS_PRE + len);
        return -1;
    }
    memcpy(buf + LWS_PRE, data, len);

    int wrote = lws_write(wsi, buf + LWS_PRE, len, LWS_WRITE_TEXT);
    free(buf);

    if (wrote < 0) {
        LOG_ERROR("send_to_lws: lws_write returned %d", wrote);
        return -1;
    }
    LOG_TRACE("send_to_lws: wrote %d bytes: %.*s", wrote, (int)(len < 120 ? len : 120), data);
    return 0;
}

int signaling_send_offer(const char *viewer_id, const char *sdp)
{
    LOG_DEBUG("signaling_send_offer: viewer=%s sdp_len=%zu", viewer_id ? viewer_id : "NULL", sdp ? strlen(sdp) : 0);
    if (!sdp) {
        LOG_ERROR("signaling_send_offer: NULL sdp");
        return -1;
    }

    char escaped[4096];
    json_escape(sdp, escaped, sizeof(escaped));

    char buf[4096];
    int n = snprintf(buf, sizeof(buf), "{\"type\":\"offer\",\"sdp\":\"%s\"}", escaped);
    if (n >= (int)sizeof(buf)) {
        LOG_WARN("signaling_send_offer: message truncated (%d >= %zu)", n, sizeof(buf));
    }

    pthread_mutex_lock(&g_ws_lock);
    int ret = send_to_lws(g_host_ws, buf);
    pthread_mutex_unlock(&g_ws_lock);
    return ret;
}

int signaling_send_answer(const char *viewer_id, const char *sdp)
{
    LOG_DEBUG("signaling_send_answer: viewer=%s sdp_len=%zu", viewer_id ? viewer_id : "NULL", sdp ? strlen(sdp) : 0);
    if (!sdp) {
        LOG_ERROR("signaling_send_answer: NULL sdp");
        return -1;
    }

    char escaped[4096];
    json_escape(sdp, escaped, sizeof(escaped));

    char buf[4096];
    int n = snprintf(buf, sizeof(buf), "{\"type\":\"answer\",\"sdp\":\"%s\"}", escaped);
    if (n >= (int)sizeof(buf)) {
        LOG_WARN("signaling_send_answer: message truncated (%d >= %zu)", n, sizeof(buf));
    }

    pthread_mutex_lock(&g_ws_lock);
    int ret;
    if (viewer_id && strcmp(viewer_id, "host") == 0) {
        ret = send_to_lws(g_host_ws, buf);
    } else {
        struct lws *target = viewer_ws_by_id(viewer_id);
        if (target) {
            ret = send_to_lws(target, buf);
        } else {
            LOG_WARN("signaling_send_answer: viewer '%s' not connected, sending to host as fallback", viewer_id ? viewer_id : "NULL");
            ret = send_to_lws(g_host_ws, buf);
        }
    }
    pthread_mutex_unlock(&g_ws_lock);
    return ret;
}

int signaling_send_ice(const char *viewer_id, const char *candidate)
{
    LOG_DEBUG("signaling_send_ice: viewer=%s candidate_len=%zu", viewer_id ? viewer_id : "NULL", candidate ? strlen(candidate) : 0);
    if (!candidate) {
        LOG_ERROR("signaling_send_ice: NULL candidate");
        return -1;
    }

    char escaped[4096];
    json_escape(candidate, escaped, sizeof(escaped));

    char buf[4096];
    int n = snprintf(buf, sizeof(buf), "{\"type\":\"ice\",\"candidate\":\"%s\"}", escaped);
    if (n >= (int)sizeof(buf)) {
        LOG_WARN("signaling_send_ice: message truncated (%d >= %zu)", n, sizeof(buf));
    }

    pthread_mutex_lock(&g_ws_lock);
    int ret;
    if (viewer_id && strcmp(viewer_id, "host") == 0) {
        ret = send_to_lws(g_host_ws, buf);
    } else {
        struct lws *target = viewer_ws_by_id(viewer_id);
        if (target) {
            ret = send_to_lws(target, buf);
        } else {
            ret = send_to_lws(g_host_ws, buf);
        }
    }
    pthread_mutex_unlock(&g_ws_lock);
    return ret;
}
