
#include "settings_things.h"
#include "static.h"

Preferences static_params;

color_struct current_static_color;

boolean static_color_changed = false;


void static_color_set() {
  for (uint16_t i = 0; i < num_of_pixels; ++i) {
    pixels[i].red   = current_static_color.red;
    pixels[i].green = current_static_color.green;
    pixels[i].blue  = current_static_color.blue;
    strip.setPixelColor(i, strip.Color(pixels[i].green, pixels[i].red, pixels[i].blue));
  }
  strip.show();

  //if (current_mode == 3){ // if current mode is static, save color to memory
  if (static_color_changed) {
    static_params.putUShort("R", ushort(current_static_color.red));
    static_params.putUShort("G", ushort(current_static_color.green));
    static_params.putUShort("B", ushort(current_static_color.blue));
    static_color_changed = false;

    #if defined(DEBUG)
      Serial.println("Static color saved to memory");
    #endif    /* defined(DEBUG) */
  } 
  //}
}
