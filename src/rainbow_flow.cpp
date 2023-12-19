
#include "settings_things.h"
#include "rainbow_flow.h"

void rainbow_flow() {
  if (rainbow_curr_master_delay == 0) {
    for (int j = 0; j < 256; ++j) {
      for (int i = 0; i < strip.numPixels(); ++i) {
        strip.setPixelColor(i, wheel((i + j) & 255));
      }
      strip.show();
    }
    rainbow_curr_master_delay = rainbow_master_delay;
  } else {
    --rainbow_curr_master_delay;
  }
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
