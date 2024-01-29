
#include "settings_things.h"
#include "rainbow_flow.h"

Preferences rainbow_flow_params; 

uint16_t    rainbow_flow_change_rate;

uint16_t    rainbow_flow_change_degree;

uint16_t    rainbow_flow_gradient_density;

uint16_t    rainbow_flow_master_delay;

bool        rainbow_flow_direction;

unsigned long start_time = millis();


void rainbow_flow1() {
  static int curr_del = 0;
  if (curr_del == 0) {
    static int offset = 0;
    float radianDirection = rainbow_flow_change_degree * PI / 180.0;
    float dx = cos(radianDirection);
    float dy = sin(radianDirection);

    for (int x = 0; x < pixels_in_row; x++) {
      for (int y = 0; y < pixels_rows; y++) {
        int pixelIndex = xy_to_index(x, y);
        int colorIndex = (int)((x * dx + y * dy) * rainbow_flow_gradient_density + offset) % 256;
        strip.setPixelColor(pixelIndex, wheel(colorIndex));
      }
    }
    strip.show();
    offset = (offset + rainbow_flow_change_rate) % 256;
    curr_del = rainbow_flow_master_delay;
  } else {
    --curr_del;
  }
  
}

void rainbow_flow() {
  // Center coordinates
  float center_x = pixels_in_row / 2.0;
  float center_y = pixels_rows / 2.0;

  // Maximum distance from the center to the farthest corner
  float maxDistance = sqrt(pow(center_x, 2) + pow(center_y, 2));

  // Time factor to create the animation
  static unsigned long time_factor;
  
  // Delay between frames
  delay(rainbow_flow_master_delay);

  // Update the time factor
  time_factor = (millis() - start_time) / rainbow_flow_change_rate; // Change the divisor to control speed

  for (int x = 0; x < pixels_in_row; ++x) {
      for (int y = 0; y < pixels_rows; ++y) {
          // Calculate distance from center
          float distance = sqrt(pow(x - center_x, 2) + pow(y - center_y, 2));

          // Normalize distance and calculate color
          int color_index;
          if (rainbow_flow_direction) {
              // Expanding rainbow
              color_index = (int)((distance / maxDistance) * rainbow_flow_gradient_density * 1 + time_factor) % 255;
          } else {
              // Contracting rainbow
              color_index = (int)((distance / maxDistance) * rainbow_flow_gradient_density * 1 - time_factor) % 255;
          }

          strip.setPixelColor(xy_to_index(x, y), wheel(color_index));
      }
  }

  // Update display
  strip.show();
  
}
