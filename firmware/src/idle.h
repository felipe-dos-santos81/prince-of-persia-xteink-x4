#ifndef IDLE_H
#define IDLE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Inactivity timer. Times are millis() values; only differences are used, so
   the 49-day wrap of millis() does not matter. */
typedef struct {
    uint32_t last_active_ms;
    uint32_t timeout_ms;
} idle_timer_t;

/* Start counting from now_ms (which counts as activity). */
void idle_reset(idle_timer_t *t, uint32_t now_ms, uint32_t timeout_ms);

/* Feed one sample. Returns true once timeout_ms has passed since the last
   sample with active set; an active sample restarts the count. */
bool idle_update(idle_timer_t *t, uint32_t now_ms, bool active);

/* True on the sample where a USB host that was connected is no longer
   connected: unplugging counts as activity. *was_plugged holds the previous
   sample and starts false. */
bool idle_usb_unplugged(bool *was_plugged, bool plugged);

#ifdef __cplusplus
}
#endif

#endif
