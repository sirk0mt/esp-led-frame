
#ifndef RAINBOW_FLOW_H
/* If first use this header, define all things */
#define RAINBOW_FLOW_H

extern Preferences  rainbow_flow_params;            /* Rainbow flow mode settings class instance */

extern uint16_t     rainbow_flow_change_rate;       /* Rainbow flow rate */

extern uint16_t     rainbow_flow_change_degree;     /* Degree of animation */

extern uint16_t     rainbow_flow_gradient_density;  /* Density of rainbow gradient */

extern uint16_t     rainbow_flow_master_delay;      /* Delay between animation cycles */

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