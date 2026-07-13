#include "webrtc_internal.h"
#include <rtc/rtc.hpp>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <memory>
#include <mutex>

/* Replicate log macros here because nearcade_internal.h uses C11 atomics
   incompatible with C++17's <stdatomic.h>.  */
extern "C" int g_loglevel;
enum { LOG_L_ERROR=1, LOG_L_WARN=2, LOG_L_INFO=3, LOG_L_DEBUG=4, LOG_L_TRACE=5 };

#define LOG(level, fmt, ...) do { \
    if ((level) <= g_loglevel) { \
        fprintf(stderr, "[nearcade] %s:%d " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
    } \
} while(0)

#define LOG_ERROR(fmt, ...)  LOG(LOG_L_ERROR, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)   LOG(LOG_L_WARN,  fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)   LOG(LOG_L_INFO,  fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...)  LOG(LOG_L_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_TRACE(fmt, ...)  LOG(LOG_L_TRACE, fmt, ##__VA_ARGS__)

#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

struct webrtc_session {
    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::Track>          video_track;
    std::shared_ptr<rtc::DataChannel>    input_chan;
    int                                  viewer_idx;
    bool                                 connected;

    webrtc_session() : viewer_idx(-1), connected(false) {}
};

static std::mutex                              g_lock;
static std::vector<std::unique_ptr<webrtc_session>> g_sessions;
static bool                                     g_initialized = false;

static webrtc_send_cb   g_send_cb   = nullptr;
static void            *g_send_ud   = nullptr;
static webrtc_input_cb  g_input_cb  = nullptr;
static void            *g_input_ud  = nullptr;
static webrtc_viewer_cb g_viewer_cb = nullptr;
static void            *g_viewer_ud = nullptr;

static int g_stun_port = 0;

static int session_idx_for_viewer(int viewer_idx)
{
    for (size_t i = 0; i < g_sessions.size(); i++) {
        if (g_sessions[i] && g_sessions[i]->viewer_idx == viewer_idx)
            return (int)i;
    }
    return -1;
}

static void send_to_viewer(const char *viewer_id, const char *data)
{
    if (g_send_cb)
        g_send_cb(viewer_id, data, g_send_ud);
}

static int create_session(int viewer_idx)
{
    char viewer_id[64];
    snprintf(viewer_id, sizeof(viewer_id), "viewer_%d", viewer_idx);

    auto sess = std::make_unique<webrtc_session>();
    sess->viewer_idx = viewer_idx;

    rtc::Configuration cfg;
    cfg.iceServers.emplace_back("stun:stun.l.google.com:19302");

    sess->pc = std::make_shared<rtc::PeerConnection>(cfg);

    /* ── State callback ──────────────────────────────────── */
    sess->pc->onStateChange([viewer_idx, viewer_id = std::string(viewer_id)](rtc::PeerConnection::State state) {
        LOG_DEBUG("webrtc: viewer_%d state=%d", viewer_idx, (int)state);
        if (state == rtc::PeerConnection::State::Connected) {
            if (g_viewer_cb)
                g_viewer_cb(viewer_idx, 1, g_viewer_ud);
        } else if (state == rtc::PeerConnection::State::Disconnected ||
                   state == rtc::PeerConnection::State::Failed ||
                   state == rtc::PeerConnection::State::Closed) {
            if (g_viewer_cb)
                g_viewer_cb(viewer_idx, 0, g_viewer_ud);
        }
    });

    /* ── Local SDP callback ──────────────────────────────── */
    std::string vid_str(viewer_id);
    sess->pc->onLocalDescription([vid_str](rtc::Description desc) {
        std::string sdp = std::string(desc);
        std::string type = desc.typeString();
        LOG_DEBUG("webrtc: local SDP (%s) for %s, len=%zu", type.c_str(), vid_str.c_str(), sdp.size());

        char buf[8192];
        int n = snprintf(buf, sizeof(buf),
            "{\"type\":\"%s\",\"sdp\":\"%s\"}", type.c_str(), sdp.c_str());
        if (n > 0 && n < (int)sizeof(buf))
            send_to_viewer(vid_str.c_str(), buf);
    });

    /* ── Local ICE candidate callback ────────────────────── */
    sess->pc->onLocalCandidate([vid_str](rtc::Candidate cand) {
        std::string c = std::string(cand);
        LOG_DEBUG("webrtc: local ICE candidate for %s: %s", vid_str.c_str(), c.c_str());

        char buf[4096];
        int n = snprintf(buf, sizeof(buf),
            "{\"type\":\"ice\",\"candidate\":\"%s\"}", c.c_str());
        if (n > 0 && n < (int)sizeof(buf))
            send_to_viewer(vid_str.c_str(), buf);
    });

    /* ── Data channel for receiving gamepad input ────────── */
    sess->input_chan = sess->pc->createDataChannel("nearcade-input");
    sess->input_chan->onMessage([viewer_idx](std::variant<rtc::binary, rtc::string> msg) {
        if (!std::holds_alternative<rtc::binary>(msg))
            return;
        const auto &bin = std::get<rtc::binary>(msg);
        if (bin.size() < sizeof(nearcade_gamepad_packet))
            return;

        nearcade_gamepad_packet pkt;
        memcpy(&pkt, bin.data(), sizeof(pkt));
        /* Convert from std::byte buffer — the nearcade_gamepad_packet is
           packed, so memcpy directly from the vector's underlying storage. */
        LOG_TRACE("webrtc: gamepad from viewer_%d slot=%d lx=%d ly=%d",
                  viewer_idx, pkt.slot, pkt.lx, pkt.ly);

        if (g_input_cb)
            g_input_cb(&pkt, g_input_ud);
    });

    /* ── Video track (send-only H.264) ────────────────────── */
    rtc::Description::Video video("video", rtc::Description::Direction::SendOnly);
    video.addH264Codec(96);
    sess->video_track = sess->pc->addTrack(video);

    sess->video_track->onMessage([](std::variant<rtc::binary, rtc::string> msg) {
        LOG_TRACE("webrtc: video track message (unexpected)");
    });

    LOG_DEBUG("webrtc: session created for viewer_%d", viewer_idx);

    std::lock_guard<std::mutex> lock(g_lock);
    g_sessions.push_back(std::move(sess));
    return (int)(g_sessions.size() - 1);
}

int webrtc_init(const nearcade_config *config)
{
    LOG_DEBUG("webrtc_init: entering");
    g_initialized = true;
    return NEARCADE_OK;
}

void webrtc_shutdown(void)
{
    LOG_DEBUG("webrtc_shutdown: entering");
    std::lock_guard<std::mutex> lock(g_lock);
    g_sessions.clear();
    g_initialized = false;
}

void webrtc_set_send_callback(webrtc_send_cb cb, void *userdata)
{
    g_send_cb = cb;
    g_send_ud = userdata;
}

void webrtc_set_input_callback(webrtc_input_cb cb, void *userdata)
{
    g_input_cb = cb;
    g_input_ud = userdata;
}

void webrtc_set_viewer_callback(webrtc_viewer_cb cb, void *userdata)
{
    g_viewer_cb = cb;
    g_viewer_ud = userdata;
}

int webrtc_viewer_joined(int viewer_idx, const char *viewer_id)
{
    LOG_DEBUG("webrtc_viewer_joined: idx=%d id=%s", viewer_idx, viewer_id ? viewer_id : "?");
    if (!g_initialized) {
        LOG_ERROR("webrtc_viewer_joined: not initialized");
        return NEARCADE_ERR_INIT;
    }
    create_session(viewer_idx);
    return NEARCADE_OK;
}

void webrtc_viewer_left(int viewer_idx)
{
    LOG_DEBUG("webrtc_viewer_left: idx=%d", viewer_idx);
    std::lock_guard<std::mutex> lock(g_lock);
    for (auto it = g_sessions.begin(); it != g_sessions.end(); ++it) {
        if (*it && (*it)->viewer_idx == viewer_idx) {
            g_sessions.erase(it);
            break;
        }
    }
}

int webrtc_handle_signaling(const char *viewer_id, const char *data)
{
    if (!viewer_id || !data) return NEARCADE_ERR_INVALID_ARG;
    LOG_DEBUG("webrtc_handle_signaling: viewer=%s data_len=%zu", viewer_id, strlen(data));

    int viewer_idx = -1;
    if (sscanf(viewer_id, "viewer_%d", &viewer_idx) != 1 || viewer_idx < 0)
        return NEARCADE_ERR_INVALID_ARG;

    std::lock_guard<std::mutex> lock(g_lock);
    int idx = session_idx_for_viewer(viewer_idx);
    if (idx < 0) {
        LOG_WARN("webrtc_handle_signaling: no session for viewer_%d", viewer_idx);
        return NEARCADE_ERR_INVALID_ARG;
    }

    auto &sess = g_sessions[idx];
    if (!sess || !sess->pc) {
        LOG_ERROR("webrtc_handle_signaling: null session for viewer_%d", viewer_idx);
        return NEARCADE_ERR_INVALID_ARG;
    }

    /* Parse the JSON message from the viewer */
    const char *type_str = nullptr;
    const char *sdp_val  = nullptr;
    const char *cand_val = nullptr;
    const char *mid_val  = nullptr;

    if (strstr(data, "\"answer\""))
        type_str = "answer";
    else if (strstr(data, "\"offer\""))
        type_str = "offer";
    else if (strstr(data, "\"ice\""))
        type_str = "ice";

    if (!type_str) {
        LOG_WARN("webrtc_handle_signaling: unknown message type: %.100s", data);
        return NEARCADE_ERR_INVALID_ARG;
    }

    /* Naive JSON extraction (safe for our controlled format) */
    const char *sdp_key = strstr(data, "\"sdp\":\"");
    if (sdp_key) {
        sdp_key += 7;
        sdp_val = sdp_key;
    }

    const char *cand_key = strstr(data, "\"candidate\":\"");
    if (cand_key) {
        cand_key += 13;
        cand_val = cand_key;
    }

    try {
        if (strcmp(type_str, "answer") == 0) {
            if (!sdp_val) return NEARCADE_ERR_INVALID_ARG;
            std::string sdp_str;
            for (; *sdp_val && *sdp_val != '"'; sdp_val++) {
                if (*sdp_val == '\\' && *(sdp_val+1) == '"') {
                    sdp_str += '"';
                    sdp_val++;
                } else {
                    sdp_str += *sdp_val;
                }
            }
            rtc::Description answer(sdp_str, "answer");
            sess->pc->setRemoteDescription(answer);
            LOG_DEBUG("webrtc: set remote answer for viewer_%d", viewer_idx);
        } else if (strcmp(type_str, "ice") == 0) {
            if (!cand_val) return NEARCADE_ERR_INVALID_ARG;
            std::string cand_str;
            for (; *cand_val && *cand_val != '"'; cand_val++) {
                if (*cand_val == '\\' && *(cand_val+1) == '"') {
                    cand_str += '"';
                    cand_val++;
                } else {
                    cand_str += *cand_val;
                }
            }
            sess->pc->addRemoteCandidate(rtc::Candidate(cand_str));
            LOG_DEBUG("webrtc: added ICE candidate for viewer_%d", viewer_idx);
        }
    } catch (const std::exception &e) {
        LOG_ERROR("webrtc_handle_signaling: exception: %s", e.what());
        return NEARCADE_ERR_NETWORK;
    }

    return NEARCADE_OK;
}

int webrtc_send_h264(const uint8_t *data, size_t size, int64_t timestamp_us)
{
    (void)timestamp_us;
    if (!data || size == 0) return NEARCADE_ERR_INVALID_ARG;

    std::lock_guard<std::mutex> lock(g_lock);
    for (auto &sess : g_sessions) {
        if (sess && sess->video_track && sess->connected) {
            const auto *bin_begin = reinterpret_cast<const std::byte *>(data);
            const auto *bin_end   = bin_begin + size;
            rtc::binary bin_data(bin_begin, bin_end);
            sess->video_track->send(bin_data);
        }
    }
    return NEARCADE_OK;
}
