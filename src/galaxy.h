
#ifndef GALAXY_H
/* If first use this header, define all things */
#define GALAXY_H

#include <Arduino.h>

extern const char* galaxy_prefs_file; 

extern uint16_t     galaxy_master_delay;        /* Value of delay between main loop */
extern uint16_t     galaxy_min_del;             /* Minimum time of life LED workers */
extern uint16_t     galaxy_max_del;             /* Maximum time of life LED workers */
extern uint16_t     galaxy_led_workers;         /* Number of LED workers */

extern uint16_t     galaxy_curr_master_delay;   /* Value of current delay between algorithm changes smth */
extern uint16_t*    galaxy_curr_delay;          /* Table of current calculated delays between loop */

extern uint16_t     galaxy_dim_minus;           /* Value of single decrement for every LED color */

String galaxy_html();

void galaxy_handle_set();

void galaxy_init_defaults();

void galaxy_init_endpoints();

void galaxy_start();

void galaxy_stop();

/**
 * \brief               Main function of galaxy mode.
*/
void galaxy_mode();

#endif      /* ifndef GALAXY_H */