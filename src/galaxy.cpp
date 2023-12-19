#include "settings_things.h"
#include "galaxy.h"

Preferences galaxy_params;

uint16_t    galaxy_master_delay;
uint16_t    galaxy_min_del;
uint16_t    galaxy_max_del;
uint16_t    galaxy_led_workers        = 1;

uint16_t    galaxy_curr_master_delay  = 0;
uint16_t*   galaxy_curr_delay         = new uint16_t[galaxy_led_workers];

uint16_t    galaxy_dim_minus          = 1;


void galaxy_mode() {
  if (galaxy_curr_master_delay == 0) {
    for (int current_pixel = 0; current_pixel < num_of_pixels; ++current_pixel) {
      if (pixels[current_pixel].red >= galaxy_dim_minus) {
        pixels[current_pixel].red = pixels[current_pixel].red - galaxy_dim_minus;
      } else {
        pixels[current_pixel].red = 0;
      }    
      
      if (pixels[current_pixel].green >= galaxy_dim_minus) {
        pixels[current_pixel].green = pixels[current_pixel].green - galaxy_dim_minus;
      } else {
        pixels[current_pixel].green = 0;
      }

      if (pixels[current_pixel].blue >= galaxy_dim_minus) {
        pixels[current_pixel].blue = pixels[current_pixel].blue - galaxy_dim_minus;
      } else {
        pixels[current_pixel].blue = 0;
      }

      strip.setPixelColor(current_pixel, strip.Color(pixels[current_pixel].green, pixels[current_pixel].red, pixels[current_pixel].blue));
    }

    for (int current_worker = 0; current_worker < galaxy_led_workers; ++current_worker) {
      --galaxy_curr_delay[current_worker];
      if (galaxy_curr_delay[current_worker] <= 0) {
        galaxy_curr_delay[current_worker] = random(galaxy_min_del, galaxy_max_del + 1);
        set_pixel_color(uint16_t(random(0, num_of_pixels)), uint8_t(random(0, 255 + 1)), uint8_t(random(0, 255 + 1)), uint8_t(random(0, 255 + 1)));
      }
    }

    strip.show();
    galaxy_curr_master_delay = galaxy_master_delay;
  } else {
    --galaxy_curr_master_delay;
  }

}