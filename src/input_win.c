#include "nearcade_internal.h"
#include <stdio.h>
#include <string.h>

int input_init(const nearcade_config *config)
{
    (void)config;
    LOG_WARN("══════════════════════════════════════════════════════════════════");
    LOG_WARN("  Windows input backend requires ViGEmBus");
    LOG_WARN("  Install: https://github.com/ViGEm/ViGEmBus/releases");
    LOG_WARN("  Or use a Python sidecar with: pip install vgamepad");
    LOG_WARN("  Without it, gamepad/KBM injection will be NO-OP");
    LOG_WARN("══════════════════════════════════════════════════════════════════");
    return NEARCADE_OK;
}

void input_shutdown(void)
{
    LOG_DEBUG("input_shutdown (Windows stub)");
}

int input_alloc_slot(uint8_t slot, uint16_t vendor, uint16_t product,
                     uint16_t version, const char *name)
{
    LOG_WARN("input_alloc_slot: slot=%d SKIPPED — ViGEmBus not available", slot);
    LOG_WARN("  Install ViGEmBus from: https://github.com/ViGEm/ViGEmBus/releases");
    (void)slot; (void)vendor; (void)product; (void)version; (void)name;
    return NEARCADE_OK;
}

int input_free_slot(uint8_t slot)
{
    LOG_DEBUG("input_free_slot: slot=%d (Windows stub)", slot);
    (void)slot;
    return NEARCADE_OK;
}

int input_submit_gamepad(const nearcade_gamepad_packet *pkt)
{
    LOG_WARN("input_submit_gamepad: slot=%d SKIPPED — ViGEmBus not available", pkt ? pkt->slot : -1);
    (void)pkt;
    return NEARCADE_OK;
}

int input_submit_kbm(const char *viewer_id, const char *event_type,
                     const char *key, int dx, int dy)
{
    LOG_WARN("input_submit_kbm: SKIPPED — ViGEmBus not available");
    (void)viewer_id; (void)event_type; (void)key; (void)dx; (void)dy;
    return NEARCADE_OK;
}

int input_flush_slot(uint8_t slot)
{
    LOG_DEBUG("input_flush_slot: slot=%d (Windows stub)", slot);
    (void)slot;
    return NEARCADE_OK;
}
