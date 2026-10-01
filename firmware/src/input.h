#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>

/* Configure the button pins. */
void input_begin(void);

/* True while any of the seven buttons is held down. */
bool input_any_pressed(void);

/* True while the power button is held down. */
bool input_power_pressed(void);

#endif
