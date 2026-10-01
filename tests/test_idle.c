#include "idle.h"
#include "test.h"

#define TIMEOUT 30000u

static void test_not_expired_before_timeout(void)
{
    idle_timer_t t;

    idle_reset(&t, 1000u, TIMEOUT);
    CHECK(!idle_update(&t, 1000u + TIMEOUT - 1u, false));
}

static void test_expires_at_timeout(void)
{
    idle_timer_t t;

    idle_reset(&t, 1000u, TIMEOUT);
    CHECK(idle_update(&t, 1000u + TIMEOUT, false));
    CHECK(idle_update(&t, 1000u + TIMEOUT + 5000u, false));
}

static void test_activity_restarts_the_count(void)
{
    idle_timer_t t;

    idle_reset(&t, 0u, TIMEOUT);
    CHECK(!idle_update(&t, 20000u, true));
    CHECK(!idle_update(&t, 20000u + TIMEOUT - 1u, false));
    CHECK(idle_update(&t, 20000u + TIMEOUT, false));
}

static void test_activity_after_expiry_restarts_the_count(void)
{
    idle_timer_t t;

    idle_reset(&t, 0u, TIMEOUT);
    CHECK(idle_update(&t, TIMEOUT, false));
    CHECK(!idle_update(&t, TIMEOUT + 10u, true));
    CHECK(!idle_update(&t, TIMEOUT + 20u, false));
}

static void test_survives_millis_wrap(void)
{
    idle_timer_t t;
    uint32_t start = 0xFFFFF000u; /* 4,096 ms before millis() wraps */

    idle_reset(&t, start, TIMEOUT);
    CHECK(!idle_update(&t, start + TIMEOUT - 1u, false));
    CHECK(idle_update(&t, start + TIMEOUT, false));
}

static void test_usb_unplug_is_seen_once(void)
{
    bool was_plugged = false;

    CHECK(!idle_usb_unplugged(&was_plugged, true));
    CHECK(!idle_usb_unplugged(&was_plugged, true));
    CHECK(idle_usb_unplugged(&was_plugged, false));
    CHECK(!idle_usb_unplugged(&was_plugged, false));
    CHECK(!idle_usb_unplugged(&was_plugged, true));
}

int main(void)
{
    test_not_expired_before_timeout();
    test_expires_at_timeout();
    test_activity_restarts_the_count();
    test_activity_after_expiry_restarts_the_count();
    test_survives_millis_wrap();
    test_usb_unplug_is_seen_once();
    return TEST_RESULT();
}
