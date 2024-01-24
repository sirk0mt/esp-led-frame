#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>

#include "settings_things.h"


wifi_mode_t current_wifi_state = WIFI_MODE_NULL;

void setup() {
  #if defined(DEBUG)
    Serial.begin(115200);
  #endif    /* defined(DEBUG) */

  initialize_settings();

  num_of_pixels = pixels_in_row * pixels_rows;

  #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "] Number of pixels calculated to: " + String(num_of_pixels));
  #endif    /* defined(DEBUG) */
  
  change_strip_color_order(color_order);
  resize_pixels_struct(num_of_pixels);
  randomSeed(analogRead(34));
  strip_initialize();
  if (connect_saved_network()) {
    start_main_server();
    initialize_starting_mode();
  } else {
    show_triangle();
    create_config_network();
  }
  initialize_mdns();
  current_wifi_state = WiFi.getMode();

  #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "] End of function -----------");
  #endif    /* defined(DEBUG) */
}

void loop() {
  server.handleClient();
  if (current_wifi_state == WIFI_STA) {
    switch (current_mode) {
      case 0:
        if (change_mode == true) {
          /* ToDo if that if is necessary? */
          change_mode = false;
        }
        break;
      case 1:
        galaxy_mode();
        break;
      case 4:
        rainbow_mode();
        break;
      case 5:
        rainbow_flow();
        break;
      default:
        break;
    }
    /* if (mdns_del_curr = 0){
    *   MDNS.update();
    * }
    */
  }
  delay(1);
}