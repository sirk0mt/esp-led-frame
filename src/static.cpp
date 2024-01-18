
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
}

void static_color_save(){
  #if defined(DEBUG)
    Serial.println("Colors to save - R: " + String(current_static_color.red) + " G: " + String(current_static_color.green) + " B: " + String(current_static_color.blue));
  #endif    /* defined(DEBUG) */

  if (static_params.getUChar("R") !=  current_static_color.red) {
    #if defined(DEBUG)
      Serial.println("Red value is different. Trying to change to new val: " + String(current_static_color.red));
    #endif    /* defined(DEBUG) */
    static_params.putUChar("R", current_static_color.red);
  }

  if (static_params.getUChar("G") !=  current_static_color.green) {
    #if defined(DEBUG)
      Serial.println("Green value is different. Trying to change to new val: " + String(current_static_color.green));
    #endif    /* defined(DEBUG) */
    static_params.putUChar("G", current_static_color.green);
  }
  if (static_params.getUChar("B") !=  current_static_color.blue) {
    #if defined(DEBUG)
      Serial.println("Blue value is different. Trying to change to new val: " + String(current_static_color.blue));
    #endif    /* defined(DEBUG) */
    static_params.putUChar("B", current_static_color.blue);
  }

  #if defined(DEBUG)
    Serial.println("Static color change done");
  #endif    /* defined(DEBUG) */
}