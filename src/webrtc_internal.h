#ifndef NEARCADE_WEBRTC_INTERNAL_H
#define NEARCADE_WEBRTC_INTERNAL_H

#include "nearcade.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*webrtc_send_cb)(const char *viewer_id, const char *data, void *userdata);
typedef void (*webrtc_input_cb)(const nearcade_gamepad_packet *pkt, void *userdata);
typedef void (*webrtc_viewer_cb)(int viewer_idx, int connected, void *userdata);

int  webrtc_init(const nearcade_config *config);
void webrtc_shutdown(void);

void webrtc_set_send_callback(webrtc_send_cb cb, void *userdata);
void webrtc_set_input_callback(webrtc_input_cb cb, void *userdata);
void webrtc_set_viewer_callback(webrtc_viewer_cb cb, void *userdata);

int  webrtc_viewer_joined(int viewer_idx, const char *viewer_id);
void webrtc_viewer_left(int viewer_idx);
int  webrtc_handle_signaling(const char *viewer_id, const char *data);
int  webrtc_send_h264(const uint8_t *data, size_t size, int64_t timestamp_us);

#ifdef __cplusplus
}
#endif

#endif
