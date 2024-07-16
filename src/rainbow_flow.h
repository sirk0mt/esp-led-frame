
#ifndef RAINBOW_FLOW_H
/* If first use this header, define all things */
#define RAINBOW_FLOW_H

#include <Arduino.h>

extern const char* rainbow_flow_prefs_file; 

extern uint16_t     rainbow_flow_change_rate;       /* Rainbow flow rate */

extern uint16_t     rainbow_flow_change_degree;     /* Degree of animation */

extern uint16_t     rainbow_flow_gradient_density;  /* Density of rainbow gradient */

extern uint16_t     rainbow_flow_master_delay;      /* Delay between animation cycles */

String rainbow_flow_html();

void rainbow_flow_init_defaults();

void rainbow_flow_init_endpoints();

void rainbow_flow_start();

void rainbow_flow_stop();

/**
 * \brief               Get color from color wheel
 * \param[wheel_pos]    wheel_pos: Byte position of wheel to get color.
 * \return              32-bit packed RGB value of color.
*/
uint32_t wheel(byte wheel_pos);

/**
 * \brief               Main function of rainbow flow mode
*/
void rainbow_flow();

#endif  /* ifndef RAINBOW_FLOW_H */