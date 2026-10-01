#include "input.h"

#include <Arduino.h>

#include "config.h"

void input_begin(void)
{
    pinMode(BUTTON_POWER_PIN, INPUT_PULLUP);
}

bool input_power_pressed(void)
{
    return digitalRead(BUTTON_POWER_PIN) == LOW;
}

bool input_any_pressed(void)
{
    return input_power_pressed() || analogRead(BUTTON_FRONT_PIN) <= BUTTON_PRESSED_MAX ||
           analogRead(BUTTON_SIDE_PIN) <= BUTTON_PRESSED_MAX;
}
