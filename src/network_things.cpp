#include "settings_things.h"
#include "network_things.h"

String host;
String ssid;
String password;

String my_ssid = "configAP";
IPAddress staticIP(192, 168, 0, 1);

uint8_t mDNSdel = 100;
uint8_t mDNSdel_curr = 100;

WebServer server(80);

bool connectToWiFi() {
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid.c_str(), password.c_str());

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 10) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    #ifdef DEBUG
      Serial.println("");
      Serial.print("Connected to ");
      Serial.println(ssid);
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
    #endif
    return true;
  } else {
    #ifdef DEBUG
      Serial.println("\nFailed to connect to saved WiFi network");
    #endif
    WiFi.disconnect();
    return false;
  }
}

String listVisibleNetworks() {
  String networks = "<ul>";
  int numNetworks = WiFi.scanNetworks();
  #ifdef DEBUG
    Serial.println("Found "+ String(numNetworks) + " networks");
  #endif
  for (int i = 0; i < numNetworks; i++) {
    networks += "<li onclick='copyText(this)'>" + WiFi.SSID(i) + "</li>";
    #ifdef DEBUG
        Serial.println("--[" + String(i) + "]-- "+ WiFi.SSID(i));
    #endif
  }
  networks += "</ul>";
  #ifdef DEBUG
    Serial.println("-------------------------");
  #endif
  return networks;
}

void createAccessPoint() {
  Serial.println("Creating Access Point...");
  WiFi.softAP(my_ssid);
  WiFi.softAPConfig(staticIP, staticIP, IPAddress(255, 255, 255, 0));
  Serial.println("Access Point created with IP: " + WiFi.softAPIP().toString());
  startSettingsWebServer();
}

void initializeMDNS() {
    /*use mdns for host name resolution*/
    if (!MDNS.begin(host.c_str())) { //http://obraz.local
      #ifdef DEBUG
        Serial.println("Error setting up MDNS responder!");
      #endif
      while (1) {
        delay(1000);
      }
    }
    MDNS.addService("http", "tcp", 80);
    
    #ifdef DEBUG
      Serial.println("");
      Serial.print("mDNS responder started at: ");
      Serial.print(host);
      Serial.println(".local");
    #endif
}