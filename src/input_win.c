#include "nearcade_internal.h"
#include <stdio.h>
#include <string.h>

int input_init(const nearcade_config *config)
{
    (void)config;
    LOG_WARN("Windows ViGEmBus backend (stub - requires vgamepad Python sidecar)");
    return NEARCADE_OK;
}

void input_shutdown(void)
{
    LOG_DEBUG("input_shutdown (Windows stub)");
}

int input_alloc_slot(uint8_t slot, uint16_t vendor, uint16_t product,
                     uint16_t version, const char *name)
{
    LOG_DEBUG("input_alloc_slot: slot=%d (Windows stub, no-op)", slot);
    (void)slot; (void)vendor; (void)product; (void)version; (void)name;
    return NEARCADE_OK;
}

int input_free_slot(uint8_t slot)
{
    LOG_DEBUG("input_free_slot: slot=%d (Windows stub, no-op)", slot);
    (void)slot;
    return NEARCADE_OK;
}

int input_submit_gamepad(const nearcade_gamepad_packet *pkt)
{
    LOG_TRACE("input_submit_gamepad: slot=%d (Windows stub, dropping)", pkt ? pkt->slot : -1);
    (void)pkt;
    return NEARCADE_OK;
}

int input_submit_kbm(const char *viewer_id, const char *event_type,
                     const char *key, int dx, int dy)
{
    LOG_TRACE("input_submit_kbm: viewer=%s event=%s (Windows stub, dropping)",
              viewer_id ? viewer_id : "NULL", event_type ? event_type : "NULL");
    (void)viewer_id; (void)event_type; (void)key; (void)dx; (void)dy;
    return NEARCADE_OK;
}

int input_flush_slot(uint8_t slot)
{
    LOG_DEBUG("input_flush_slot: slot=%d (Windows stub, no-op)", slot);
    (void)slot;
    return NEARCADE_OK;
}
