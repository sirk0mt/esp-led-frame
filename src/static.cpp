
#include "all_modes.h"
#include "settings_things.h"
#include "network_things.h"
#include "json.h"
#include "pixels.h"

const char* static_params_file = "/static.json"; 

color_struct current_static_color;

boolean static_color_changed = false;

void static_init_defaults() {
  current_static_color.red      = 20;
  mode_json_doc["r"]   = current_static_color.red;

  current_static_color.green    = 20;
  mode_json_doc["g"]      = current_static_color.green;

  current_static_color.blue     = 20;
  mode_json_doc["b"]      = current_static_color.blue;
}

void static_init_endpoints() {
  server.on("/staticSet", HTTP_POST, [](){
    if (current_mode == MODE_STATIC) {
      String paramName = server.argName(0); // Get the name of the parameter
      String paramValue = server.arg(0); // Get the value of the parameter
      if (paramName == "save") {
        static_color_save();
        server.send(200, "text/plain", "OK");
      }
      else if(paramName == "hex"){
        hex_to_rgb(paramValue, &current_static_color);
        static_color_changed = true;
        static_color_set();
        server.send(200, "text/plain", "OK");
      }
      else {
        server.send(200, "text/plain", "You're not in static mode!");
      }
    }
  });
  server.on("/staticGet", HTTP_GET, [](){
    if (current_mode == MODE_STATIC) {
      String paramName = server.arg("v");
      if(paramName == "hex"){
        server.send(200, "text/plain", String(rgb_to_hex(current_static_color.red, current_static_color.green, current_static_color.blue)));
      }
      else {
        server.send(200, "text/plain", "You're not in static mode!");
      }
    }
  });
}

void static_start() {
  // malloc dla każdej zmiennej oraz load parametów

  static_color_set();
}

void static_stop() {
  // free dla każdej zmiennej
}

void hex_to_rgb(String hex_color, struct color_struct *current_color) {
  #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "] Got HEX: " + hex_color);
  #endif    /* defined(DEBUG) */
  const char *temp = hex_color.c_str();
  uint8_t temp_red, temp_green, temp_blue;
  sscanf(temp, "%02x%02x%02x", &temp_red, &temp_green, &temp_blue);

  current_color->red = temp_red;
  current_color->green = temp_green;
  current_color->blue = temp_blue;

  #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "] R: " + String(current_color->red) + " G: " + String(current_color->green) + " B: "+ String(current_color->blue));
  #endif    /* defined(DEBUG) */
}

void static_color_set() {
  // for (uint16_t i = 0; i < num_of_pixels; ++i) {
  //   pixels[i].red   = current_static_color.red;
  //   pixels[i].green = current_static_color.green;
  //   pixels[i].blue  = current_static_color.blue;

  //   strip.setPixelColor(i, strip.Color(pixels[i].green, pixels[i].red, pixels[i].blue));
  // }
  strip.fill(reorder_color(strip.Color(current_static_color.red, current_static_color.green, current_static_color.blue)));
  strip.show();
}

void static_color_save() {
  #if defined(DEBUG)
    Serial.println("Colors to save - R: " + String(current_static_color.red) + " G: " + String(current_static_color.green) + " B: " + String(current_static_color.blue));
  #endif    /* defined(DEBUG) */

  if (json_load_uint16(mode_json_doc,"r") !=  current_static_color.red) {
    #if defined(DEBUG)
      Serial.println("Red value is different. Trying to change to new val: " + String(current_static_color.red));
    #endif    /* defined(DEBUG) */
    mode_json_doc["r"] = current_static_color.red;
  }

  if (json_load_uint16(mode_json_doc,"g") !=  current_static_color.green) {
    #if defined(DEBUG)
      Serial.println("Green value is different. Trying to change to new val: " + String(current_static_color.green));
    #endif    /* defined(DEBUG) */
    mode_json_doc["g"] = current_static_color.green;
  }
  if (json_load_uint16(mode_json_doc,"b") !=  current_static_color.blue) {
    #if defined(DEBUG)
      Serial.println("Blue value is different. Trying to change to new val: " + String(current_static_color.blue));
    #endif    /* defined(DEBUG) */
    mode_json_doc["b"] = current_static_color.blue;
  }

  #if defined(DEBUG)
    Serial.println("Static color change done");
  #endif    /* defined(DEBUG) */
}