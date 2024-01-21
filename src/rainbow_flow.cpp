
#include "settings_things.h"
#include "rainbow_flow.h"

float rainbow_color_change_rate;

int rainbow_color_change_degree;

int rainbow_color_gradient_density;

void rainbow_flow() {
  static int offset = 0;
  float radianDirection = rainbow_color_change_degree * PI / 180.0; // Convert degrees to radians
  float dx = cos(radianDirection);
  float dy = sin(radianDirection);

  for(int x = 0; x < pixels_in_row; x++) {
    for(int y = 0; y < pixels_rows; y++) {
      int pixelIndex = xy_to_index(x, y);
      int colorIndex = (int)((x * dx + y * dy) * rainbow_color_gradient_density + offset) % 256;
      strip.setPixelColor(pixelIndex, wheel(colorIndex));
    }
  }
  strip.show();
  offset = (int)(offset + rainbow_color_change_rate) % 256; // Increment offset for the next frame
}

// Input a value 0 to 255 to get a color value.
// The colors are a transition r - g - b - back to r.
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
