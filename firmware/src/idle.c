#include "idle.h"

void idle_reset(idle_timer_t *t, uint32_t now_ms, uint32_t timeout_ms)
{
    t->last_active_ms = now_ms;
    t->timeout_ms = timeout_ms;
}

bool idle_update(idle_timer_t *t, uint32_t now_ms, bool active)
{
    if (active) {
        t->last_active_ms = now_ms;
        return false;
    }
    return (uint32_t)(now_ms - t->last_active_ms) >= t->timeout_ms;
}

bool idle_usb_unplugged(bool *was_plugged, bool plugged)
{
    bool unplugged = *was_plugged && !plugged;

    *was_plugged = plugged;
    return unplugged;
}
