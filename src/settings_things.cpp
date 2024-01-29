#include "settings_things.h"

/* v0.0.0-dev_branch_name - v(OR).(dev test ver).(dev working ver - working branch) */
const char*       ver = "v0.1.1 - dev_rainbow_from_center";

Preferences       dev_settings;
Preferences       main_settings;

uint16_t          pixels_in_row;
uint16_t          pixels_rows; 
uint16_t          num_of_pixels;
uint16_t          color_order;
uint16_t          current_mode;

Adafruit_NeoPixel strip(1, LED_STRIP_PIN, NEO_RGB + NEO_KHZ800);

bool              change_mode = false;

void change_strip_color_order(uint16_t col_order) {
  switch (col_order) {
    case 0:
      strip.updateType(NEO_RGB + NEO_KHZ800);

      #if defined(DEBUG)
      Serial.println("[" + String(__func__) + "] Changed strip color order to RGB");
      #endif    /* defined(DEBUG) */

      break;
    case 1:
      strip.updateType(NEO_RBG + NEO_KHZ800);
      
      #if defined(DEBUG)
      Serial.println("[" + String(__func__) + "] Changed strip color order to RBG");
      #endif    /* defined(DEBUG) */

      break;
    case 2:
      strip.updateType(NEO_GRB + NEO_KHZ800);
      
      #if defined(DEBUG)
      Serial.println("[" + String(__func__) + "] Changed strip color order to GRB");
      #endif    /* defined(DEBUG) */

      break;
    case 3:
      strip.updateType(NEO_GBR + NEO_KHZ800);
      
      #if defined(DEBUG)
      Serial.println("[" + String(__func__) + "] Changed strip color order to GBR");
      #endif    /* defined(DEBUG) */

      break;
    case 4:
      strip.updateType(NEO_BRG + NEO_KHZ800);
      
      #if defined(DEBUG)
      Serial.println("[" + String(__func__) + "] Changed strip color order to BRG");
      #endif    /* defined(DEBUG) */

      break;
    case 5:
      strip.updateType(NEO_BGR + NEO_KHZ800);
      
      #if defined(DEBUG)
      Serial.println("[" + String(__func__) + "] Changed strip color order to BGR");
      #endif    /* defined(DEBUG) */

      break;
    default:
      break;
  }
  dev_settings.putUShort("colorOrder", col_order);
}

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
  rainbow_flow_params.begin("rflowParams", false);

  // Initialize default values (do it only once)  -->

  //dev_settings.putUShort("pixelsInRow",15);
  //dev_settings.putUShort("pixelsRows",10);
  //dev_settings.putString("host","obraz");
  //dev_settings.putString("ssid", "Siedziba_PIS11"); 
  //dev_settings.putString("password", "niepowiemci11");
  //dev_settings.putUShort("colorOrder", 2);
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
  //rainbow_flow_params.putUShort("rChRate",1);
  //rainbow_flow_params.putUShort("rChDeg",0);
  //rainbow_flow_params.putUShort("rGradDen",10);
  //rainbow_flow_params.putUShort("rMDel",0);

  // <-- End of initialize with default values

  // Getting data from memory -->
  pixels_in_row       = dev_settings.getUShort("pixelsInRow");
  pixels_rows         = dev_settings.getUShort("pixelsRows");
  mdns_host_name      = dev_settings.getString("host");
  saved_ssid          = dev_settings.getString("ssid"); 
  saved_password      = dev_settings.getString("password");
  color_order         = dev_settings.getUShort("colorOrder");
  

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

  rainbow_flow_change_rate      = rainbow_flow_params.getUShort("rChRate");
  rainbow_flow_change_degree    = rainbow_flow_params.getUShort("rChDeg");
  rainbow_flow_gradient_density = rainbow_flow_params.getUShort("rGradDen");
  rainbow_flow_master_delay     = rainbow_flow_params.getUShort("rMDel");

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
    Serial.println("--- rainbow_flow_params ---");
    Serial.println("rainbow_flow_change_rate: " + String(rainbow_flow_change_rate));
    Serial.println("rainbow_flow_change_degree: " + String(rainbow_flow_change_degree));
    Serial.println("rainbow_flow_gradient_density: " + String(rainbow_flow_gradient_density));
    Serial.println("rainbow_flow_master_delay: " + String(rainbow_flow_master_delay));
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