#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include "network_things.h"
#include "website.h"
#include "galaxy.h"
#include "rainbow.h"
#include "static.h"
#include "pixels.h"

// #include <nvs_flash.h> //to format settings

#ifndef SETTINGS_H
#define SETTINGS_H

//v0.0.0 - v(pelna wersja).(wstepnie gotowa).(skonczny dzien modow)
extern const char* ver;

#define PIN        2 // pin connected to LED strip

#define DEBUG

extern Preferences devSettings; //device settings
extern Preferences mainSettings; //main program settings

// LEDs settings
extern uint16_t pixelsInRow;
extern uint16_t pixelsRows; 

extern uint16_t NUMPIXELS;

extern Adafruit_NeoPixel strip;
// LEDs settings

extern uint16_t currentMode;

extern bool changeMode;

void saveWiFiConfig(const String& ssid, const String& password);

void handleRestart();

bool initializeSettings();

void initializeMode();


#endif