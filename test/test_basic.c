#include "nearcade.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int event_count = 0;
static void event_cb(const nearcade_event *ev, void *userdata)
{
    (void)userdata;
    if (!ev) return;
    event_count++;
    printf("[test] Event type=%d", ev->type);
    switch (ev->type) {
        case NEARCADE_EVENT_VIEWER_JOINED:
            printf(" viewer=%s name=%s", ev->data.viewer_joined.viewer_id, ev->data.viewer_joined.name);
            break;
        case NEARCADE_EVENT_VIEWER_LEFT:
            printf(" viewer=%s", ev->data.viewer_left.viewer_id);
            break;
        case NEARCADE_EVENT_INPUT_PACKET:
            printf(" viewer=%s slot=%d", ev->data.input_packet.viewer_id, ev->data.input_packet.packet.slot);
            break;
        case NEARCADE_EVENT_SIGNALING:
            printf(" viewer=%s data='%s'", ev->data.signaling.viewer_id, ev->data.signaling.data);
            break;
        case NEARCADE_EVENT_RUMBLE:
            printf(" slot=%d strong=%.1f weak=%.1f", ev->data.rumble.slot, ev->data.rumble.strong, ev->data.rumble.weak);
            break;
        case NEARCADE_EVENT_ERROR:
            printf(" code=%d msg='%s'", ev->data.error.code, ev->data.error.message);
            break;
        case NEARCADE_EVENT_STREAMING:
            printf(" viewers=%d", ev->data.streaming.viewer_count);
            break;
        default:
            printf(" (unknown)");
            break;
    }
    printf("\n");
}

int main(void)
{
    printf("Nearcade SDK v%d.%d.%d\n",
           NEARCADE_VERSION_MAJOR, NEARCADE_VERSION_MINOR,
           NEARCADE_VERSION_PATCH);

    int rc;

    /* Test 1: NULL config init */
    printf("--- Test 1: nearcade_init(NULL) ---\n");
    rc = nearcade_init(NULL);
    printf("nearcade_init(NULL): %d\n", rc);
    assert(rc == NEARCADE_OK);

    /* Test 2: Regenerate PIN */
    printf("--- Test 2: nearcade_regenerate_pin ---\n");
    rc = nearcade_regenerate_pin();
    printf("regenerate_pin: %d (assert OK) ", rc);
    assert(rc == NEARCADE_OK);
    printf("✓\n");

    /* Test 3: Get info */
    printf("--- Test 3: Get info ---\n");
    char url[256];
    rc = nearcade_get_signaling_url(url, sizeof(url));
    printf("get_signaling_url: %d url='%s'\n", rc, url);

    char ip[64];
    rc = nearcade_get_lan_ip(ip, sizeof(ip));
    printf("get_lan_ip: %d ip='%s'\n", rc, ip);

    char pin[8];
    rc = nearcade_get_pin(pin, sizeof(pin));
    printf("get_pin: %d pin='%s'\n", rc, pin);

    /* Test 4: Event callback */
    printf("--- Test 4: Event callback ---\n");
    rc = nearcade_set_event_callback(event_cb, NULL);
    printf("set_event_callback: %d\n", rc);
    assert(rc == NEARCADE_OK);

    /* Test 5: Submit gamepad on unallocated slot */
    printf("--- Test 5: Submit gamepad on slot 0 (no alloc yet) ---\n");
    nearcade_gamepad_packet pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type = 0x01;
    pkt.slot = 0;
    pkt.lx = 0;
    pkt.ly = -32767;
    pkt.buttons = NEARCADE_BTN_A;
    rc = nearcade_submit_gamepad(&pkt);
    printf("submit_gamepad on unallocated slot: %d ", rc);
    assert(rc == NEARCADE_ERR_NO_DEVICE);
    printf("✓ (expected: ERR_NO_DEVICE)\n");

    /* Test 6: Flush slot */
    printf("--- Test 6: Flush slot 0 ---\n");
    rc = nearcade_flush_slot(0);
    printf("flush_slot(0): %d\n", rc);

    /* Test 7: Submit KBM */
    printf("--- Test 7: Submit KBM ---\n");
    rc = nearcade_submit_kbm("viewer_0", "mousemove", NULL, 100, 50);
    printf("submit_kbm(mousemove): %d\n", rc);
    rc = nearcade_submit_kbm("viewer_0", "keydown", "KEY_W", 0, 0);
    printf("submit_kbm(keydown KEY_W): %d\n", rc);
    rc = nearcade_submit_kbm("viewer_0", "keyup", "KEY_W", 0, 0);
    printf("submit_kbm(keyup KEY_W): %d\n", rc);

    /* Test 8: NULL arg handling */
    printf("--- Test 8: NULL arg handling ---\n");
    rc = nearcade_submit_gamepad(NULL);
    assert(rc == NEARCADE_ERR_INVALID_ARG);
    printf("submit_gamepad(NULL): %d ✓\n", rc);

    rc = nearcade_submit_kbm(NULL, "mousemove", NULL, 0, 0);
    assert(rc == NEARCADE_ERR_INVALID_ARG);
    printf("submit_kbm(NULL,...): %d ✓\n", rc);

    rc = nearcade_disconnect_viewer(NULL);
    assert(rc == NEARCADE_ERR_INVALID_ARG);
    printf("disconnect_viewer(NULL): %d ✓\n", rc);

    /* Test 9: Disconnect non-existent viewer */
    printf("--- Test 9: Disconnect non-existent viewer ---\n");
    rc = nearcade_disconnect_viewer("nonexistent");
    printf("disconnect_viewer(nonexistent): %d (should be OK)\n", rc);
    assert(rc == NEARCADE_OK);

    /* Test 10: Set controller type for non-existent viewer */
    printf("--- Test 10: Set controller type ---\n");
    rc = nearcade_set_controller_type("viewer_0", NEARCADE_CTRL_DS4);
    assert(rc == NEARCADE_ERR_INVALID_ARG);
    printf("set_controller_type(nonexistent): %d ✓\n", rc);

    /* Test 11: Start streaming lifecycle */
    printf("--- Test 11: Start/stop streaming ---\n");
    rc = nearcade_start_streaming();
    printf("start_streaming: %d\n", rc);
    assert(rc == NEARCADE_OK);
    rc = nearcade_stop_streaming();
    printf("stop_streaming: %d\n", rc);
    assert(rc == NEARCADE_OK);

    /* Test 12: Send H.264 frame (no viewers connected, should be no-op) */
    printf("--- Test 12: Send H.264 frame ---\n");
    uint8_t fake_nal[] = {0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1e};
    rc = nearcade_send_h264(fake_nal, sizeof(fake_nal), 0);
    printf("send_h264 (no viewers): %d (should be OK)\n", rc);
    assert(rc == NEARCADE_OK);

    /* Test 13: Send frame (not implemented yet) */
    printf("--- Test 13: Send RGBA frame (stub) ---\n");
    uint8_t fake_rgba[4] = {0};
    rc = nearcade_send_frame(fake_rgba, 640, 480, NEARCADE_PIX_FMT_RGBA, 0);
    printf("send_frame (stub): %d (expected ERR_INIT)\n", rc);
    assert(rc == NEARCADE_ERR_INIT);

    /* Test 14: Legacy capture lifecycle */
    printf("--- Test 14: Capture lifecycle ---\n");
    rc = nearcade_start_capture();
    printf("start_capture: %d\n", rc);
    rc = nearcade_stop_capture();
    printf("stop_capture: %d\n", rc);

    /* Test 15: Poll events (stub) */
    printf("--- Test 15: Poll events stub ---\n");
    rc = nearcade_poll_events(0);
    printf("poll_events: %d\n", rc);

    /* Test 16: Deprecated signaling relay (no-op now) */
    printf("--- Test 16: Deprecated signaling relay ---\n");
    rc = nearcade_send_offer("viewer_0", "test_sdp_offer");
    printf("send_offer: %d (deprecated no-op)\n", rc);
    rc = nearcade_send_answer("host", "test_sdp_answer");
    printf("send_answer: %d (deprecated no-op)\n", rc);
    rc = nearcade_send_ice_candidate("viewer_0", "test_ice_candidate");
    printf("send_ice: %d (deprecated no-op)\n", rc);

    /* Test 17: Shutdown */
    printf("--- Test 17: nearcade_shutdown ---\n");
    nearcade_shutdown();
    printf("shutdown: OK\n");

    /* Test 18: Shutdown again (should be idempotent) */
    printf("--- Test 18: Double shutdown ---\n");
    nearcade_shutdown();
    printf("shutdown (2nd): OK\n");

    printf("\n=== All %d tests passed ===\n", 18);
    return 0;
}
