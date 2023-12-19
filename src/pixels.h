

#ifndef PIXELS_H
/* If first use this header, define all things */
#define PIXELS_H

/**
 * \brief       Struct of color for LED pixel
*/
struct pixel_color{
  /**
   * \brief       Red color value 0-255
  */
  uint8_t red;

  /**
   * \brief       Green color value 0-255
  */
  uint8_t green;

  /**
   * \brief       Blue color value 0-255
  */
  uint8_t blue;
};

extern pixel_color* pixels;       /* Table of pixel_color elements */


/**
 * \brief               Function that initialize LED strip.
*/
void strip_initialize();

/**
 * \brief               Function resize pixels struct for new pixel number.
 * \param[new_size]     new_size: New number of pixels in LED strip.
*/
void resize_pixels_struct(uint16_t new_size);

/**
 * \brief               Function resize arrays of uint16_t type.
 * \param[old_array]    old_array: Array to resize
 * \param[old_size]     old_size: Size of old array
 * \param[new_size]     new_size: Size of desired array
 * \return              New resized to desire length array
*/
uint16_t* resize_array(uint16_t* old_array, uint16_t old_size, uint16_t new_size);

/**
 * \brief               Send one pixel from pixels table to LED strip
 * \param[led_to_send]  led_to_send: Number of pixel from pixels table to send
*/
void send_pixel(uint16_t led_to_send);

/**
 * \brief               Write one pixel color to table and LED strip without .show function
 * \param[led]          led: Number of pixel to write color
 * \param[r]            r: Red color value 0-255
 * \param[g]            g: Green color value 0-255
 * \param[b]            b: Blue color value 0-255
*/
void set_pixel_color(uint16_t led, uint8_t r, uint8_t g, uint8_t b);

/**
 * \brief               Function to show red triangle (info about device was created configuration network)
*/
void show_triangle();

#endif  /* ifndef PIXELS_H */