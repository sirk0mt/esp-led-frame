
#ifndef STATIC_H
#define STATIC_H

#include <Arduino.h>

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

extern const char* static_params_file; 

extern color_struct current_static_color;   /* Current static mode color */
extern boolean      static_color_changed;   /* If static color was chaged */


void static_init_defaults();

void static_init_endpoints();

void static_start();

void static_stop();

void hex_to_rgb(String hex_color, struct color_struct *current_color);

/**
 * \brief         Function that sets color from color struct for whole LEDs
*/
void static_color_set();

/**
 * \brief         Save color to memory
*/
void static_color_save();


#endif    /* ifndef STATIC_H */