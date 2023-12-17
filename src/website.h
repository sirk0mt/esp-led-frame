#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>

#ifndef WEBSITE_H
#define WEBSITE_H

extern uint16_t ajaxCurrPixel;
extern uint8_t ajaxCurrR;
extern uint8_t ajaxCurrG;
extern uint8_t ajaxCurrB;

//string to add in HTML to automatic redirect to previours page
extern const char* autoBack; 

//update site
extern const char* updateIndex;

String getAvailableNetworksSite();

void handleRoot();

void startSettingsWebServer(); 

void startWebServer();

String getModeSettings(int mode);

void handleSetValue();

void handleClear();



#endif