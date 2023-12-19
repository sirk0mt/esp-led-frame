
#ifndef RAINBOW_H
#define RAINBOW_H

/**
 * \brief       Struct of current color change and direction of changing
*/
struct color_ch_struct  {
  /**
   * \brief       Value of single change of red color
  */
  uint8_t red_change;

  /**
   * \brief       If true then red color raising up, false for falling down
  */
  bool    red_up;

  /**
   * \brief       Current value of red color
  */
  uint8_t red_current;

  /**
   * \brief       Value of single change of green color
  */
  uint8_t green_change;

  /**
   * \brief       If true then green color raising up, false for falling down
  */
  bool    green_up;

  /**
   * \brief       Current value of green color
  */
  uint8_t green_current;

  /**
   * \brief       Value of single change of blue color
  */
  uint8_t blue_change;

  /**
   * \brief       If true then blue color raising up, false for falling down
  */
  bool    blue_up;

  /**
   * \brief       Current value of blue color
  */
  uint8_t blue_current;
};


extern Preferences      rainbow_params;             /* Rainbow mode settings class instance */

extern uint16_t         rainbow_master_delay;       /* Value of delay between main loop */
extern uint16_t         rainbow_max_change;         /* Value of max change of color between cycles */

extern color_ch_struct  rainbow_change;             /* store current random color change for rainbow mode */

extern uint16_t         rainbow_curr_master_delay;  /* Value of current delay between algorithm changes smth */

/**
 * \brief         Initialize first random values of all mode parameters
*/
void rainbow_initialize();

/**
 * \brief         Main function of rainbow mode.
*/
void rainbow_mode();


#endif  /* ifndef RAINBOW_H */