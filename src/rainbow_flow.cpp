
#include "settings_things.h"
#include "rainbow_flow.h"

Preferences rainbow_flow_params; 

uint16_t    rainbow_flow_change_rate;

uint16_t    rainbow_flow_change_degree;

uint16_t    rainbow_flow_gradient_density;

uint16_t    rainbow_flow_master_delay;

/**
 * \brief             Input a value 0 to 255 to get a color value. The colors are a transition r - g - b - back to r.
 * \param[wheel_pos]  wheel_pos: Position of color wheel to get color.
*/
uint32_t wheel(byte wheel_pos) {
  wheel_pos = 255 - wheel_pos;
  if (wheel_pos < 85) {
    return strip.Color(255 - wheel_pos * 3, 0, wheel_pos * 3);
  }
  if (wheel_pos < 170) {
    wheel_pos -= 85;
    return strip.Color(0, wheel_pos * 3, 255 - wheel_pos * 3);
  }
  wheel_pos -= 170;
  return strip.Color(wheel_pos * 3, 255 - wheel_pos * 3, 0);
}

int xy_to_index(int x, int y) {
  return y * pixels_in_row + x;
}

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
