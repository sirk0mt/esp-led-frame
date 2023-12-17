#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>


#ifndef NETWORK_THINGS_H
#define NETWORK_THINGS_H

extern String my_ssid;
extern IPAddress staticIP;

extern String host;
extern String ssid;
extern String password;

extern WebServer server;

extern uint8_t mDNSdel;
extern uint8_t mDNSdel_curr;

bool connectToWiFi();

String listVisibleNetworks();

void createAccessPoint();

void initializeMDNS();

#endif