#ifndef NEARCADE_H
#define NEARCADE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) || defined(_WIN64)
#  ifdef NEARCADE_BUILD_SHARED
#    define NEARCADE_API __declspec(dllexport)
#  else
#    define NEARCADE_API __declspec(dllimport)
#  endif
#else
#  define NEARCADE_API __attribute__((visibility("default")))
#endif

#ifndef NEARCADE_API
#  define NEARCADE_API
#endif

#define NEARCADE_VERSION_MAJOR 0
#define NEARCADE_VERSION_MINOR 2
#define NEARCADE_VERSION_PATCH 0

#define NEARCADE_MAX_VIEWERS     64
#define NEARCADE_MAX_SLOTS       16
#define NEARCADE_DEVICE_NAME_LEN 32
#define NEARCADE_VIEWER_ID_LEN   64

#define NEARCADE_BTN_A      (1<<0)
#define NEARCADE_BTN_B      (1<<1)
#define NEARCADE_BTN_Y      (1<<2)
#define NEARCADE_BTN_X      (1<<3)
#define NEARCADE_BTN_LB     (1<<4)
#define NEARCADE_BTN_RB     (1<<5)
#define NEARCADE_BTN_BACK   (1<<8)
#define NEARCADE_BTN_START  (1<<9)
#define NEARCADE_BTN_LS     (1<<10)
#define NEARCADE_BTN_RS     (1<<11)
#define NEARCADE_BTN_GUIDE  (1<<16)

typedef struct nearcade_config {
    int      port;
    int      screen_width;
    int      screen_height;
    int      max_bitrate;
    int      fps;
    int      enable_audio;
    const char *turn_url;
    const char *turn_username;
    const char *turn_credential;
} nearcade_config;

#define NEARCADE_DEFAULT_CONFIG { \
    3000,                         \
    1920,                         \
    1080,                         \
    8000000,                      \
    60,                           \
    0,                            \
    NULL,                         \
    NULL,                         \
    NULL                          \
}

typedef enum {
    NEARCADE_OK               = 0,
    NEARCADE_ERR_INIT         = -1,
    NEARCADE_ERR_PERMISSION   = -2,
    NEARCADE_ERR_NO_DEVICE    = -3,
    NEARCADE_ERR_SLOT_FULL    = -4,
    NEARCADE_ERR_INVALID_ARG  = -5,
    NEARCADE_ERR_NETWORK      = -6,
    NEARCADE_ERR_CAPTURE      = -7,
} nearcade_error;

typedef enum {
    NEARCADE_CTRL_XBOX360   = 0,
    NEARCADE_CTRL_XBOXONE   = 1,
    NEARCADE_CTRL_DS4       = 2,
    NEARCADE_CTRL_DUALSENSE = 3,
    NEARCADE_CTRL_SWITCH_PRO = 4,
} nearcade_ctrl_type;

typedef enum {
    NEARCADE_INPUT_GAMEPAD       = 0,
    NEARCADE_INPUT_KBM           = 1,
    NEARCADE_INPUT_KBM_EMULATED  = 2,
    NEARCADE_INPUT_DISABLED      = 3,
} nearcade_input_mode;

typedef enum {
    NEARCADE_EVENT_VIEWER_JOINED  = 0,
    NEARCADE_EVENT_VIEWER_LEFT    = 1,
    NEARCADE_EVENT_INPUT_PACKET   = 2,
    NEARCADE_EVENT_RUMBLE         = 3,
    NEARCADE_EVENT_SIGNALING      = 4,
    NEARCADE_EVENT_ERROR          = 5,
    NEARCADE_EVENT_STREAMING      = 6,
} nearcade_event_type;

typedef enum {
    NEARCADE_PIX_FMT_RGBA  = 0,
    NEARCADE_PIX_FMT_NV12  = 1,
    NEARCADE_PIX_FMT_I420  = 2,
} nearcade_pixel_format;

#pragma pack(push, 1)
typedef struct nearcade_gamepad_packet {
    uint8_t  type;
    int16_t  lx;
    int16_t  ly;
    int16_t  rx;
    int16_t  ry;
    uint8_t  lt;
    uint8_t  rt;
    uint16_t buttons;
    int8_t   hx;
    int8_t   hy;
    uint8_t  slot;
} nearcade_gamepad_packet;
#pragma pack(pop)

typedef struct nearcade_event {
    nearcade_event_type type;
    union {
        struct {
            char viewer_id[NEARCADE_VIEWER_ID_LEN];
            char name[48];
        } viewer_joined;

        struct {
            char viewer_id[NEARCADE_VIEWER_ID_LEN];
        } viewer_left;

        struct {
            nearcade_gamepad_packet packet;
            char viewer_id[NEARCADE_VIEWER_ID_LEN];
        } input_packet;

        struct {
            uint8_t slot;
            float   strong;
            float   weak;
            int     duration;
        } rumble;

        struct {
            char viewer_id[NEARCADE_VIEWER_ID_LEN];
            char data[2048];
        } signaling;

        struct {
            int  code;
            char message[256];
        } error;

        struct {
            int  viewer_count;
        } streaming;
    } data;
} nearcade_event;

typedef void (*nearcade_event_callback)(const nearcade_event *event, void *userdata);

/* ── Lifecycle ─────────────────────────────────────────────────────────── */
NEARCADE_API int nearcade_init(const nearcade_config *config);
NEARCADE_API int nearcade_start_streaming(void);
NEARCADE_API int nearcade_stop_streaming(void);
NEARCADE_API int nearcade_poll_events(int timeout_ms);
NEARCADE_API void nearcade_shutdown(void);

/* ── Video (engine-pushed frames — the primary path) ───────────────────── */
NEARCADE_API int nearcade_send_h264(const uint8_t *data, size_t size, int64_t timestamp_us);
NEARCADE_API int nearcade_send_frame(const uint8_t *data, int width, int height,
                                     nearcade_pixel_format fmt, int64_t timestamp_us);

/* ── Legacy capture (convenience — FFmpeg screen capture) ──────────────── */
NEARCADE_API int nearcade_start_capture(void);
NEARCADE_API int nearcade_stop_capture(void);

/* ── Input injection ───────────────────────────────────────────────────── */
NEARCADE_API int nearcade_submit_gamepad(const nearcade_gamepad_packet *packet);
NEARCADE_API int nearcade_submit_kbm(const char *viewer_id, const char *event_type,
                                     const char *key, int dx, int dy);
NEARCADE_API int nearcade_flush_slot(uint8_t slot);
NEARCADE_API int nearcade_disconnect_viewer(const char *viewer_id);

/* ── Viewer settings ───────────────────────────────────────────────────── */
NEARCADE_API int nearcade_set_viewer_input_mode(const char *viewer_id,
                                                nearcade_input_mode mode);
NEARCADE_API int nearcade_set_controller_type(const char *viewer_id,
                                              nearcade_ctrl_type ctrl);

/* ── Event system ──────────────────────────────────────────────────────── */
NEARCADE_API int nearcade_set_event_callback(nearcade_event_callback cb, void *userdata);

/* ── Query ─────────────────────────────────────────────────────────────── */
NEARCADE_API int nearcade_get_signaling_url(char *buf, size_t buf_size);
NEARCADE_API int nearcade_get_lan_ip(char *buf, size_t buf_size);
NEARCADE_API int nearcade_get_pin(char *buf, size_t buf_size);
NEARCADE_API int nearcade_regenerate_pin(void);

/* ── Deprecated signaling relay (no-op — WebRTC is internal now) ───────── */
NEARCADE_API int nearcade_send_offer(const char *viewer_id, const char *sdp);
NEARCADE_API int nearcade_send_answer(const char *viewer_id, const char *sdp);
NEARCADE_API int nearcade_send_ice_candidate(const char *viewer_id,
                                             const char *candidate);

#ifdef __cplusplus
}
#endif

#endif /* NEARCADE_H */
