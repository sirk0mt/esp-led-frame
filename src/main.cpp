#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>

#include "settings_things.h"

wifi_mode_t currentWiFiState = WIFI_MODE_NULL;

void setup() {
  #ifdef DEBUG
    Serial.begin(115200);
  #endif

  if (initializeSettings()){
    #ifdef DEBUG
      Serial.println("Settings initialized");
    #endif
  }

  delay(50);

  NUMPIXELS = pixelsInRow*pixelsRows;
  #ifdef DEBUG
    Serial.println("Number of pixels calculated to: " + String(NUMPIXELS));
  #endif

  if(resizePixelStruct(NUMPIXELS)){
    #ifdef DEBUG
      Serial.println("Pixel struct resized");
    #endif
  }

  delay(50);

  randomSeed(analogRead(34));

  if(stripInitialize()) {
    #ifdef DEBUG
      Serial.println("Strip initialized");
    #endif
  }
  

  if (connectToWiFi()) {
    
    startWebServer();

    initializeMDNS();

    initializeMode();

  } else {
    if(showTriangle()) {
      #ifdef DEBUG
        Serial.println("show warning triangle");
      #endif
    }
    delay(50);
    createAccessPoint();
  }
  currentWiFiState = WiFi.getMode();

  #ifdef DEBUG
    Serial.println("End of setup()");
  #endif
}

void loop() {
  server.handleClient();
  if(currentWiFiState == WIFI_STA) {
    switch(currentMode){
      case 0:
        if(changeMode == true){
          // do something
          changeMode = false;
        }
        break;
      case 1:
        galaxyMode();
        break;
      case 4:
        rainbowMode();
        break;
      default:
        break;
    }
    // if (mDNSdel_curr = 0){
    //   MDNS.update();
    // }
  }
  delay(1);
}