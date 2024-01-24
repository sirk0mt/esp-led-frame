
#ifndef STATIC_H
#define STATIC_H

/**
 * \brief         Struct to store color
*/
struct color_struct {
  /**
   * \brief       Value of red color 0-255
  */
  uint8_t red;

  /**
   * \brief       Value of green color 0-255
  */
  uint8_t green;

  /**
   * \brief       Value of blue color 0-255
  */
  uint8_t blue;
};


extern Preferences  static_params;          /* Static mode settings class instance */

extern color_struct current_static_color;   /* Current static mode color */
extern boolean      static_color_changed;   /* If static color was chaged */


/**
 * \brief         Function that sets color from color struct for whole LEDs
*/
void static_color_set();

/**
 * \brief         Save color to memory
*/
void static_color_save();


#endif    /* ifndef STATIC_H */