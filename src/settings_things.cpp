#include "settings_things.h"

//v0.0.0 - v(pelna wersja).(wstepnie gotowa).(skonczny dzien modow)
const char* ver = "v0.0.1a";

Preferences devSettings; //device settings
Preferences mainSettings; //main program settings

uint16_t pixelsInRow;
uint16_t pixelsRows; 
uint16_t NUMPIXELS;
uint16_t currentMode;

Adafruit_NeoPixel strip(1, PIN, NEO_GRB + NEO_KHZ800); //create instance for neopixel strip

bool changeMode = false;

void handleRestart() {
  #ifdef DEBUG
    Serial.println("Restarting device...");
  #endif
  ESP.restart();
}

void saveWiFiConfig(const String& ssid, const String& password) {
  devSettings.putString("ssid", ssid); 
  devSettings.putString("password", password);
  #ifdef DEBUG
    Serial.println("New network settings saved");
    Serial.println("SSID: " + devSettings.getString("ssid") + "  Password: " + devSettings.getString("password"));
  #endif
}

bool initializeSettings() {
  //to format settings
  // nvs_flash_erase(); // erase the NVS partition and...
  // nvs_flash_init(); // initialize the NVS partition.

  devSettings.begin("devSettings", false);
  mainSettings.begin("mainSettings", false);
  galaxyParams.begin("galaxyParams", false);
  staticParams.begin("staticParams", false);
  rainbowParams.begin("rainbowParams", false);

  // Initialize default values (do it only once)  -->

  //devSettings.putUShort("pixelsInRow",15);
  //devSettings.putUShort("pixelsRows",10);
  //devSettings.putString("host","obraz");
  //devSettings.putString("ssid", "Siedziba_PIS11"); 
  //devSettings.putString("password", "niepowiemci11");
  //mainSettings.putUShort("currentMode", 1);
  //galaxyMasterDelay = galaxyParams.putUShort("MasterDel",30);
  //galaxyMinDel = galaxyParams.putUShort("MinDel",50);
  //galaxyMaxDel = galaxyParams.putUShort("MaxDel",2000);
  //galaxyLedWorkers = galaxyParams.putUShort("LedWorkers",600);
  //staticParams.putUShort("R",10);
  //staticParams.putUShort("G",10);
  //staticParams.putUShort("B",10);
  //rainbowParams.putUShort("rMasterDel",10);
  //rainbowParams.putUShort("rMaxChange",10);

  // <-- End of initialize with default values

  // Getting data from memory -->
  pixelsInRow = devSettings.getUShort("pixelsInRow");
  pixelsRows = devSettings.getUShort("pixelsRows");
  host = devSettings.getString("host");
  ssid = devSettings.getString("ssid"); 
  password = devSettings.getString("password");

  currentMode = mainSettings.getUShort("currentMode");

  galaxyMasterDelay = galaxyParams.getUShort("MasterDel");
  galaxyMinDel = galaxyParams.getUShort("MinDel");
  galaxyMaxDel = galaxyParams.getUShort("MaxDel");
  galaxyLedWorkers = galaxyParams.getUShort("LedWorkers");
  galaxyCurrDelay = resizeArray(galaxyCurrDelay,1,galaxyLedWorkers);

  currentStatic.R = staticParams.getUShort("R");
  currentStatic.G = staticParams.getUShort("G");
  currentStatic.B = staticParams.getUShort("B");

  rainbowMasterDelay = rainbowParams.getUShort("rMasterDel");
  rainbowMaxChange= rainbowParams.getUShort("rMaxChange");
  rainbowCurrMasterDelay = rainbowMasterDelay;

  #ifdef DEBUG
    Serial.println("--- devSettings ---");
    Serial.println("pixelsInRow: " + String(pixelsInRow));
    Serial.println("pixelsRows: " + String(pixelsRows));
    Serial.println("host: " + String(host));
    Serial.println("ssid: " + String(ssid));
    Serial.println("password: " + String(password));
    Serial.println("--- mainSettings ---");
    Serial.println("currentMode: " + String(currentMode));
    Serial.println("--- galaxyParams ---");
    Serial.println("galaxyMasterDelay: " + String(galaxyMasterDelay));
    Serial.println("galaxyMinDel: " + String(galaxyMinDel));
    Serial.println("galaxyMaxDel: " + String(galaxyMaxDel));
    Serial.println("galaxyLedWorkers: " + String(galaxyLedWorkers));
    Serial.println("--- staticParams ---");
    Serial.println("R: " + String(currentStatic.R));
    Serial.println("G: " + String(currentStatic.G));
    Serial.println("B: " + String(currentStatic.B));
    Serial.println("--- rainbowParams ---");
    Serial.println("rainbowCurrMasterDelay: " + String(rainbowCurrMasterDelay));
    Serial.println("rainbowMaxChange: " + String(rainbowMaxChange));
  #endif

  // <-- End of getting data from memory
  return true;
}

void initializeMode() {
    #ifdef DEBUG
        Serial.println("Initialization of currentMode");
    #endif
    //Initialize for modes
    switch(currentMode) {
        case 0:
            break;
        case 1: //galaxy
            galaxyMode();
            #ifdef DEBUG
                Serial.println("Galaxy mode initialized");
            #endif
            break;
        case 3: //static
            staticSetColor();
            #ifdef DEBUG
                Serial.println("Static mode initialized");
            #endif
            break;
        case 4: //rainbow
            rainbowInitialize();
            #ifdef DEBUG
                Serial.println("Rainbow mode initialized");
            #endif
            break;
        default:
            break;
    }
}