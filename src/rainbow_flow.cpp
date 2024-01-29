
#include "settings_things.h"
#include "rainbow_flow.h"

Preferences rainbow_flow_params; 

uint16_t    rainbow_flow_change_rate;

uint16_t    rainbow_flow_change_degree;

uint16_t    rainbow_flow_gradient_density;

uint16_t    rainbow_flow_master_delay;


void rainbow_flow() {
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
