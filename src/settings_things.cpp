#include "settings_things.h"
#include "json.h"
#include "pixels.h"
#include "network_things.h"

#include "all_modes.h"

/* v0.0.0-dev_branch_name - v(OR).(dev test ver).(dev working ver - working branch) */
const char*       ver = "v0.2.0";

const char* main_prefs_file = "/prefs.json";

uint16_t          pixels_in_row;
uint16_t          pixels_rows; 
uint16_t          num_of_pixels;
uint16_t          color_order;
uint16_t          current_mode;

Adafruit_NeoPixel strip(1, LED_STRIP_PIN, NEO_RGB + NEO_KHZ800);

bool              change_mode = false;

void save_default_settings(const char* prefs_file) {
  #if defined(DEBUG)
      Serial.println("[" + String(__func__) + "] trying to save tefaults to " + String(prefs_file));
  #endif    /* defined(DEBUG) */

  if (strcmp(prefs_file, main_prefs_file) == 0) {
    #if defined(DEBUG)
      Serial.println("[" + String(__func__) + "] writing main settings");
    #endif    /* defined(DEBUG) */

    main_json_doc.clear();
    // set default values like json_doc[key] = value
    pixels_in_row               = 15;
    main_json_doc["led_in_row"] = pixels_in_row ;

    pixels_rows               = 10;
    main_json_doc["led_rows"] = pixels_rows;

    color_order                  = GRB_ORDER;
    main_json_doc["color_order"] = color_order;
    
    mdns_host_name        = "obraz";
    main_json_doc["host"] = mdns_host_name;

    saved_ssid            = "Siedziba_PIS";
    main_json_doc["ssid"] = saved_ssid; 

    saved_password            = "niepowiemci";
    main_json_doc["wifipass"] = saved_password;

    current_mode                  = MODE_GALAXY;
    main_json_doc["current_mode"] = current_mode;

  } else if (strcmp(prefs_file, galaxy_prefs_file) == 0) {
    #if defined(DEBUG)
      Serial.println("[" + String(__func__) + "] writing galaxy settings");
    #endif    /* defined(DEBUG) */
    mode_json_doc.clear();
    galaxy_init_defaults();
  } else if (strcmp(prefs_file, rainbow_flow_prefs_file) == 0) {
    #if defined(DEBUG)
      Serial.println("[" + String(__func__) + "] writing rainbow_flow settings");
    #endif    /* defined(DEBUG) */
    mode_json_doc.clear();
    rainbow_flow_init_defaults();
  }


  json_save_prefs_file(prefs_file);
  #if defined(DEBUG)
      Serial.println("[" + String(__func__) + "] saved to " + String(prefs_file));
  #endif    /* defined(DEBUG) */
}

void change_strip_color_order(uint16_t col_order) {
  switch (col_order) {
    case RGB_ORDER:
      strip.updateType(NEO_RGB + NEO_KHZ800);

      #if defined(DEBUG)
      Serial.println("[" + String(__func__) + "] Changed strip color order to RGB");
      #endif    /* defined(DEBUG) */

      break;
    case RBG_ORDER:
      strip.updateType(NEO_RBG + NEO_KHZ800);
      
      #if defined(DEBUG)
      Serial.println("[" + String(__func__) + "] Changed strip color order to RBG");
      #endif    /* defined(DEBUG) */

      break;
    case GRB_ORDER:
      strip.updateType(NEO_GRB + NEO_KHZ800);
      
      #if defined(DEBUG)
      Serial.println("[" + String(__func__) + "] Changed strip color order to GRB");
      #endif    /* defined(DEBUG) */

      break;
    case GBR_ORDER:
      strip.updateType(NEO_GBR + NEO_KHZ800);
      
      #if defined(DEBUG)
      Serial.println("[" + String(__func__) + "] Changed strip color order to GBR");
      #endif    /* defined(DEBUG) */

      break;
    case BRG_ORDER:
      strip.updateType(NEO_BRG + NEO_KHZ800);
      
      #if defined(DEBUG)
      Serial.println("[" + String(__func__) + "] Changed strip color order to BRG");
      #endif    /* defined(DEBUG) */

      break;
    case BGR_ORDER:
      strip.updateType(NEO_BGR + NEO_KHZ800);
      
      #if defined(DEBUG)
      Serial.println("[" + String(__func__) + "] Changed strip color order to BGR");
      #endif    /* defined(DEBUG) */

      break;
    default:
      break;
  }
  // ToDo - move save to change handler
  // json_load_prefs_file(main_prefs_file);
  // main_json_doc["color_order"] = col_order;
  // json_save_prefs_file(main_prefs_file);
}

void handle_restart() {
  #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "] Restarting device...");
  #endif    /* defined(DEBUG) */

  ESP.restart();
}

void save_new_wifi_config(const String& ssid, const String& password) {
  json_load_prefs_file(main_prefs_file);

  main_json_doc["ssid"] = ssid; 
  main_json_doc["wifipass"] = password;

  #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "] New network settings saved" +
                                              " SSID: " + json_load_string(main_json_doc, "ssid") +
                                              " Password: " + json_load_string(main_json_doc, "wifipass"));
  #endif    /* defined(DEBUG) */

  json_save_prefs_file(main_prefs_file);
}

void initialize_settings() {
  json_load_prefs_file(main_prefs_file);

  pixels_in_row       = json_load_uint16(main_json_doc, "led_in_row");
  pixels_rows         = json_load_uint16(main_json_doc, "led_rows");
  color_order         = json_load_uint16(main_json_doc, "color_order");
  mdns_host_name      = json_load_string(main_json_doc, "host");
  saved_ssid          = json_load_string(main_json_doc, "ssid");
  saved_password      = json_load_string(main_json_doc, "wifipass");
  current_mode        = json_load_uint16(main_json_doc, "current_mode");

  switch (current_mode) {
    case MODE_GALAXY:
      galaxy_start();
      break;
    
    default:
      break;
  }

  // Getting data from memory -->


  current_static_color.red    = 20;
  current_static_color.green  = 20;
  current_static_color.blue   = 20;

  rainbow_master_delay      = 10;
  rainbow_max_change        = 5;
  rainbow_curr_master_delay = rainbow_master_delay;

  rainbow_flow_change_rate      = 10;
  rainbow_flow_change_degree    = 0;
  rainbow_flow_gradient_density = 10;
  rainbow_flow_master_delay     = 10;

  // <-- End of getting data from memory
}

void initialize_starting_mode() {
  #if defined(DEBUG)
      Serial.println("Initialization of current_mode");
  #endif    /* defined(DEBUG) */

  switch(current_mode) {
    case MODE_OFF:
      break;
    case MODE_GALAXY:
      galaxy_mode();

      #if defined(DEBUG)
        Serial.println("Galaxy mode initialized");
      #endif    /* defined(DEBUG) */

      break;
    case MODE_STATIC:
      static_color_set();

      #if defined(DEBUG)
        Serial.println("Static mode initialized");
      #endif    /* defined(DEBUG) */

      break;
    case MODE_RAINBOW:
      rainbow_start();

      #if defined(DEBUG)
          Serial.println("Rainbow mode initialized");
      #endif    /* defined(DEBUG) */
      
      break;
    default:
      break;
  }
}
