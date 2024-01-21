
#ifndef RAINBOW_FLOW_H
#define RAINBOW_FLOW_H

#include <Arduino.h>

/* DEV TEST functions ToDo*/

extern float rainbow_color_change_rate;

extern int rainbow_color_change_degree;

extern int rainbow_color_gradient_density;

void rainbow_flow();

uint32_t wheel(byte wheel_pos);

#endif  /* ifndef RAINBOW_FLOW_H */