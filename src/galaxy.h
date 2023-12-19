
#ifndef GALAXY_H
/* If first use this header, define all things */
#define GALAXY_H

extern Preferences  galaxy_params;              /* Galaxy mode settings class instance */

extern uint16_t     galaxy_master_delay;        /* Value of delay between main loop */
extern uint16_t     galaxy_min_del;             /* Minimum time of life LED workers */
extern uint16_t     galaxy_max_del;             /* Maximum time of life LED workers */
extern uint16_t     galaxy_led_workers;         /* Number of LED workers */

extern uint16_t     galaxy_curr_master_delay;   /* Value of current delay between algorithm changes smth */
extern uint16_t*    galaxy_curr_delay;          /* Table of current calculated delays between loop */

extern uint16_t     galaxy_dim_minus;           /* Value of single decrement for every LED color */


/**
 * \brief               Main function of galaxy mode.
*/
void galaxy_mode();

#endif      /* ifndef GALAXY_H */