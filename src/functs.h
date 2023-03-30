#include <Arduino.h>

extern String getModeName(uint8_t mode);
extern void sendPixelColor(uint16_t led);
extern void setLedColor(uint16_t led, uint8_t R, uint8_t G, uint8_t B);
extern void handleSetValue();
extern void handleAjax();
extern void galaxyMode();