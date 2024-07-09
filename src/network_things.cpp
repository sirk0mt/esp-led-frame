#include "network_things.h"
#include "settings_things.h"
#include "website.h"
#include "json.h"

String    mdns_host_name;
String    saved_ssid;
String    saved_password;

String    config_network_ssid = "configAP";
IPAddress config_network_ip(192, 168, 0, 1);

uint8_t   mdns_del            = 100;
uint8_t   mdns_del_curr       = 100;

WebServer server(80);

bool connect_saved_network() {
  int attempts = 0;

  #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "] Connecting to WiFi");
  #endif    /* defined(DEBUG) */

  WiFi.begin(saved_ssid.c_str(), saved_password.c_str());
  while (WiFi.status() != WL_CONNECTED && attempts < 10) {
    delay(500);
    #if defined(DEBUG)
      Serial.print(".");
    #endif    /* defined(DEBUG) */
    ++attempts;
  }

  if (WiFi.status() == WL_CONNECTED) {
    #if defined(DEBUG)
      Serial.println("[" + String(__func__) + "] Connected to " + saved_ssid + " with IP: " + WiFi.localIP().toString());
    #endif    /* defined(DEBUG) */
    return true;
  } else {
    #if defined(DEBUG)
      Serial.println("[" + String(__func__) + "] ERROR - Failed to connect to saved WiFi network");
    #endif    /* defined(DEBUG) */
    WiFi.disconnect();
    return false;
  }
}

String list_visible_networks() {
  String networks = "<div class='list-group'>";
  int numNetworks = WiFi.scanNetworks();

  #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "] Found "+ String(numNetworks) + " networks");
  #endif    /* defined(DEBUG) */

  for (int i = 0; i < numNetworks; ++i) {
    networks += "<button type='button' class='list-group-item list-group-item-action' onclick='copyText(this)'>" + WiFi.SSID(i) + "</button>";

    #if defined(DEBUG)
        Serial.println("--[" + String(i) + "]-- "+ WiFi.SSID(i));
    #endif    /* defined(DEBUG) */
  }

  networks += "</div>";

  #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "] Function end -----------");
  #endif    /* defined(DEBUG) */

  return networks;
}

void start_network_settings_server() {
  server.on("/", HTTP_GET, [](){
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", get_available_networks_html());
  });

  server.on("/format", delete_all_files);

  server.on("/saveNetwork", HTTP_POST, [](){
    if (server.hasArg("ssid") && server.hasArg("password")) { // check if value parameter is present
      // Save new credentials to SPIFFS
      save_new_wifi_config(server.arg("ssid"), server.arg("password"));

      server.send(200, "text/html", "restarting device");
      handle_restart();
    }
    else {
      server.send(200, "text/html", "Wrong parameters");
    }
  });

  server.onNotFound(redirect_to_root);
  server.begin();
  #if defined(DEBUG)
    Serial.println("Config server started");
  #endif    /* defined(DEBUG) */
}

void create_config_network() {
  #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "] Creating Access Point...");
  #endif    /* defined(DEBUG) */

  WiFi.softAP(config_network_ssid);
  WiFi.softAPConfig(config_network_ip, config_network_ip, IPAddress(255, 255, 255, 0));

  #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "] Access Point created with IP: " + WiFi.softAPIP().toString());
  #endif    /* defined(DEBUG) */
  
  start_network_settings_server();
}

void initialize_mdns() {
  if (!MDNS.begin(mdns_host_name.c_str())) {
    #if defined(DEBUG)
      Serial.println("[" + String(__func__) + "] ERROR setting up MDNS responder!");
    #endif    /* defined(DEBUG) */

    return;
  }
  MDNS.addService("http", "tcp", 80);

  #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "] mDNS responder started at: " + mdns_host_name + ".local");
  #endif    /* defined(DEBUG) */
}

String get_available_networks_html() {
  return networkChoiseSiteHead + String(list_visible_networks()) + networkChoiseSiteFooter;
}

String network_page_html() {
  return
    "<h3>Network Settings</h3><br>"
    "<b>WiFi network:</b> " + WiFi.SSID() + "<br>"
    "<b>WiFi RSSI:</b> " + String(WiFi.RSSI()) + "<br>"
    "<b>Current IP:</b> " + WiFi.localIP().toString() + "<br>"
    "<b>mDNS domain:</b> " + mdns_host_name + ".local<br>"
    "<hr>" + get_available_networks_html();
}

String networkChoiseSiteHead    = 
      "<b>Choose a WiFi network:</b>"
      "<form method='post' action='/saveNetwork'>"
        "<div class='mb-3'>"
          "<label for='ssid' class='form-label'>SSID</label>"
          "<input type='text' class='form-control' id='ssid' name='ssid'>"
        "</div>"
        "<div class='mb-3'>"
          "<label for='passVal' class='form-label'>Password</label>"
          "<input type='password' class='form-control' id='passVal' name='password'>"
        "</div>"
        "<button type='submit' class='btn btn-primary'>Connect</button>"
      "</form>"
      "<b>Available Networks</b>";

String networkChoiseSiteFooter  = 
      "<script>"
        "function copyText(element) {"
          "var textToCopy = element.textContent || element.innerText;"
          "document.getElementById('ssid').value = textToCopy;"
        "}"
      "</script>";