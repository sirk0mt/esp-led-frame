#include "settings_things.h"

/* v0.0.0 - v(OR).(dev test ver).(dev working ver) */
const char*       ver = "v0.0.8";

Preferences       dev_settings;
Preferences       main_settings;

uint16_t          pixels_in_row;
uint16_t          pixels_rows; 
uint16_t          num_of_pixels;
uint16_t          current_mode;

Adafruit_NeoPixel strip(1, LED_STRIP_PIN, LED_STRIP_COLOR_ORDER + NEO_KHZ800);

bool              change_mode = false;

void handle_restart() {
  #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "] Restarting device...");
  #endif    /* defined(DEBUG) */

  ESP.restart();
}

void save_new_wifi_config(const String& ssid, const String& password) {
  dev_settings.putString("ssid", ssid); 
  dev_settings.putString("password", password);

  #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "] New network settings saved" +
                                              " SSID: " + dev_settings.getString("ssid") +
                                              " Password: " + dev_settings.getString("password"));
  #endif    /* defined(DEBUG) */
}

void initialize_settings() {
  //to format settings
  // nvs_flash_erase(); // erase the NVS partition and...
  // nvs_flash_init(); // initialize the NVS partition.

  dev_settings.begin("devSettings", false);
  main_settings.begin("mainSettings", false);
  galaxy_params.begin("galaxyParams", false);
  static_params.begin("staticParams", false);
  rainbow_params.begin("rainbowParams", false);

  // Initialize default values (do it only once)  -->

  //dev_settings.putUShort("pixelsInRow",15);
  //dev_settings.putUShort("pixelsRows",10);
  //dev_settings.putString("host","obraz");
  //dev_settings.putString("ssid", "Siedziba_PIS11"); 
  //dev_settings.putString("password", "niepowiemci11");
  //main_settings.putUShort("currentMode", 1);
  //galaxy_master_delay = galaxy_params.putUShort("MasterDel",30);
  //galaxy_min_del = galaxy_params.putUShort("MinDel",50);
  //galaxy_max_del = galaxy_params.putUShort("MaxDel",2000);
  //galaxy_led_workers = galaxy_params.putUShort("LedWorkers",600);
  //static_params.putUChar("R",10);
  //static_params.putUChar("G",10);
  //static_params.putUChar("B",10);
  //rainbow_params.putUShort("rMasterDel",10);
  //rainbow_params.putUShort("rMaxChange",10);

  // <-- End of initialize with default values

  // Getting data from memory -->
  pixels_in_row       = dev_settings.getUShort("pixelsInRow");
  pixels_rows         = dev_settings.getUShort("pixelsRows");
  mdns_host_name      = dev_settings.getString("host");
  saved_ssid          = dev_settings.getString("ssid"); 
  saved_password      = dev_settings.getString("password");

  current_mode        = main_settings.getUShort("currentMode");

  galaxy_master_delay = galaxy_params.getUShort("MasterDel");
  galaxy_min_del      = galaxy_params.getUShort("MinDel");
  galaxy_max_del      = galaxy_params.getUShort("MaxDel");
  galaxy_led_workers  = galaxy_params.getUShort("LedWorkers");
  galaxy_curr_delay   = resize_array(galaxy_curr_delay,1,galaxy_led_workers);

  current_static_color.red    = static_params.getUChar("R");
  current_static_color.green  = static_params.getUChar("G");
  current_static_color.blue   = static_params.getUChar("B");

  rainbow_master_delay      = rainbow_params.getUShort("rMasterDel");
  rainbow_max_change        = rainbow_params.getUShort("rMaxChange");
  rainbow_curr_master_delay = rainbow_master_delay;

  rainbow_color_change_rate   = 0.5;
  rainbow_color_change_degree = 90;
  rainbow_color_gradient_density = 1;

  #if defined(DEBUG)
    Serial.println("-----------------------------------");
    Serial.println("--- Settings readed from memory ---");
    Serial.println("--- dev_settings ---");
    Serial.println("pixels_in_row: " + String(pixels_in_row));
    Serial.println("pixels_rows: " + String(pixels_rows));
    Serial.println("mdns_host_name: " + String(mdns_host_name));
    Serial.println("saved_ssid: " + String(saved_ssid));
    Serial.println("saved_password: " + String(saved_password));
    Serial.println("--- main_settings ---");
    Serial.println("current_mode: " + String(current_mode));
    Serial.println("--- galaxy_params ---");
    Serial.println("galaxy_master_delay: " + String(galaxy_master_delay));
    Serial.println("galaxy_min_del: " + String(galaxy_min_del));
    Serial.println("galaxy_max_del: " + String(galaxy_max_del));
    Serial.println("galaxy_led_workers: " + String(galaxy_led_workers));
    Serial.println("--- static_params ---");
    Serial.println("R: " + String(current_static_color.red));
    Serial.println("G: " + String(current_static_color.green));
    Serial.println("B: " + String(current_static_color.blue));
    Serial.println("--- rainbow_params ---");
    Serial.println("rainbow_curr_master_delay: " + String(rainbow_curr_master_delay));
    Serial.println("rainbow_max_change: " + String(rainbow_max_change));
    Serial.println("-----------------------------------");
  #endif    /* defined(DEBUG) */

  // <-- End of getting data from memory
}

void initialize_starting_mode() {
  #if defined(DEBUG)
      Serial.println("Initialization of current_mode");
  #endif    /* defined(DEBUG) */

  switch(current_mode) {
    case 0:
      break;
    case 1: /* galaxy */
      galaxy_mode();

      #if defined(DEBUG)
        Serial.println("Galaxy mode initialized");
      #endif    /* defined(DEBUG) */

      break;
    case 3: /* static */
      static_color_set();

      #if defined(DEBUG)
        Serial.println("Static mode initialized");
      #endif    /* defined(DEBUG) */

      break;
    case 4: /* rainbow */
      rainbow_initialize();

      #if defined(DEBUG)
          Serial.println("Rainbow mode initialized");
      #endif    /* defined(DEBUG) */
      
      break;
    default:
      break;
  }
}