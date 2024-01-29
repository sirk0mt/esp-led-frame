#include "website.h"
#include "settings_things.h"

uint16_t    ajax_current_pixel  = 0;

String get_color_order_checked_state(uint8_t radio_id) {
  if (radio_id == color_order) {
    return "checked";
  } else {
    return "";
  }
}

String get_available_networks_html() {
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

String get_curr_mode_name() {
  switch (current_mode) {
    case 0:
      return "Off";
      break;
    case 1:
      return "Galaxy";
      break;
    case 2:
      return "Selective";
      break;
    case 3:
      return "Static";
      break;
    case 4:
      return "Color flow";
      break;
    case 5:
      return "Rainbow";
      break;
    default:
      return "Undefined";
      break;
  }
}

String rgb_to_hex(uint8_t red, uint8_t green, uint8_t blue) {
  #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "] Received color R: " + String(red) + " G: " + String(green) + " B: "+ String(blue));
  #endif    /* defined(DEBUG) */

  // check if values are between 0-255
  red = (red < 0) ? 0 : (red > 255) ? 255 : red;
  green = (green < 0) ? 0 : (green > 255) ? 255 : green;
  blue = (blue < 0) ? 0 : (blue > 255) ? 255 : blue;
  char hex_color[7];
  sprintf(hex_color, "#%02X%02X%02X", red, green, blue);

  #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "] Returning HEX: " + hex_color);
  #endif    /* defined(DEBUG) */

  return hex_color;
}

void hex_to_rgb(String hex_color, struct color_struct *current_color) {
  #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "] Got HEX: " + hex_color);
  #endif    /* defined(DEBUG) */
  const char *temp = hex_color.c_str();
  uint8_t temp_red, temp_green, temp_blue;
  sscanf(temp, "%02x%02x%02x", &temp_red, &temp_green, &temp_blue);

  current_color->red = temp_red;
  current_color->green = temp_green;
  current_color->blue = temp_blue;

  #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "] R: " + String(current_color->red) + " G: " + String(current_color->green) + " B: "+ String(current_color->blue));
  #endif    /* defined(DEBUG) */
}

void redirect_to_root() {
  server.sendHeader("Location", "http://" + server.client().localIP().toString());
  server.send(302, "text/plain", "");
}

void start_network_settings_server() {
  server.on("/", HTTP_GET, [](){
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", get_available_networks_html());
  });

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

void start_main_server() {
    server.on("/", HTTP_GET, []() {
      server.sendHeader("Connection", "close");
      server.send(200, "text/html", 
        "<html><head><title>LED PICTURE " + String(ver) + "</title>"
          "<link href='https://maxcdn.bootstrapcdn.com/bootstrap/4.5.2/css/bootstrap.min.css' rel='stylesheet'>"
          "<script src='https://code.jquery.com/jquery-3.5.1.min.js'></script>"
          "<script src='https://cdn.jsdelivr.net/npm/@popperjs/core@2.9.1/dist/umd/popper.min.js'></script>"
          "<script src='https://maxcdn.bootstrapcdn.com/bootstrap/4.5.2/js/bootstrap.min.js'></script>"
        "</head><body onload='onLoad()'>"
        "<div class='container mt-5'>"
          "<h1 class='text-center mb-4'>LED PICTURE " + String(ver) + "</h1>"
          "<ul class='nav nav-tabs' id='myTabs'>"
            "<li class='nav-item'><a class='nav-link active' id='modes-tab' data-toggle='tab' href='#modes'>Modes</a></li>"
            "<li class='nav-item'><a class='nav-link' id='network-tab' data-toggle='tab' href='#network'>Network</a></li>"
            "<li class='nav-item'><a class='nav-link' id='preferences-tab' data-toggle='tab' href='#preferences'>Preferences</a></li>"
            "<li class='nav-item'><a class='nav-link' id='update-tab' data-toggle='tab' href='#update'>Update</a></li>"
          "</ul>"
          "<div class='tab-content mt-3'>"
            "<div class='tab-pane fade show active' id='modes'>"
              "<h3>Modes</h3><hr>"
              "<b>Current mode:</b> <div class='d-inline' id='curr-name'>" + get_curr_mode_name() + "</div>"
              "<form class='text-center'>"
                "<label>Change mode:</label><br>"
                "<button type='button' class='btn btn-primary' id='mode-0' onclick='btnClick(this)'>Off</button>"
                "<button type='button' class='btn btn-primary' id='mode-1' onclick='btnClick(this)'>Galaxy</button>"
                "<button type='button' class='btn btn-primary' id='mode-2' onclick='btnClick(this)'>Selective</button>"
                "<button type='button' class='btn btn-primary' id='mode-3' onclick='btnClick(this)'>Static</button>"
                "<button type='button' class='btn btn-primary' id='mode-4' onclick='btnClick(this)'>Color flow</button>"
                "<button type='button' class='btn btn-primary' id='mode-5' onclick='btnClick(this)'>Rainbow</button>"
              "</form>"
              "<h3>Mode settings:</h3>"
              "<div class='text-center mb-3' id='mode-pref-content'></div>"
              "<button type='button' class='btn btn-primary' id='clear-btn' onclick='btnClick(this)'>Clear strip</button>"
            "</div>"
            "<div class='tab-pane fade' id='network'></div>"
            "<div class='tab-pane fade' id='preferences'>"
              "<h3>System Preferences</h3><br>"
              "<b>Pixels in row: </b>" + String(pixels_in_row) + "<br>"
              "<b>Pixels rows: </b>" + String(pixels_rows) + "<br>"
              "<b>mDNS domain:</b>"
              "<div class='input-group mb-3 col-md-4'>"
                "<input type='text' class='form-control' id='domName' aria-describedby='domain' value='" + mdns_host_name + "'>"
                "<div class='input-group-append'>"
                  "<span class='input-group-text' id='domain'>.local</span>"
                "</div>"
                "<button type='button' class='btn btn-primary' onclick='setDomain();'>save and reboot</button>"
              "</div>"
              "<br><br>"
              "<b>Choose LED color order: <br>"
              "<div class='btn-group btn-group-toggle' data-toggle='buttons'>"
                "<label class='btn btn-secondary'>"
                  "<input type='radio' name='colOr' id='order0' " + get_color_order_checked_state(0) + "> RGB"
                "</label>"
                "<label class='btn btn-secondary'>"
                  "<input type='radio' name='colOr' id='order1' " + get_color_order_checked_state(1) + "> RBG"
                "</label>"
                "<label class='btn btn-secondary'>"
                  "<input type='radio' name='colOr' id='order2' " + get_color_order_checked_state(2) + "> GRB"
                "</label>"
                "<label class='btn btn-secondary'>"
                  "<input type='radio' name='colOr' id='order3' " + get_color_order_checked_state(3) + "> GBR"
                "</label>"
                "<label class='btn btn-secondary'>"
                  "<input type='radio' name='colOr' id='order4' " + get_color_order_checked_state(4) + "> BRG"
                "</label>"
                "<label class='btn btn-secondary'>"
                  "<input type='radio' name='colOr' id='order5' " + get_color_order_checked_state(5) + "> BGR"
                "</label>"
              "</div>"
              "<br><br><button type='button' class='btn btn-primary' onclick='restart();'>Restart device</button>"
              "<script>"
                "function restart() {"
                  "fetch('/restart');"
                "};"
                "$(document).ready(function(){"
                    "$('input[type=\\'radio\\'][name=\\'colOr\\']').change(function() {"
                      "console.log('select changed to: ' + this.id);"
                    "});"
                "});"
                "function setDomain() {"
                  "var newName = document.getElementById('domName').value;"
                  "fetch('/setvalue?domname=' + newName, { method: 'POST' }).then(response => response.text());"
                  "restart();"
                "};"
              "</script>"
            "</div>"
            "<div class='tab-pane fade' id='update'>"
              "<h3>OTA Update</h3><br>"
              "<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
                "<div class='input-group mb-3'>"
                  "<input type='file' class='form-control' id='upFile' name='update' >"
                "</div>"
                "<input class='btn btn-info' type='submit' value='Update'>"
              "</form>"
              "<div id='prg'>progress: 0%</div>"
              "<script>"
                "$('form').submit(function(e){"
                  "e.preventDefault();"
                  "var form = $('#upload_form')[0];"
                  "var data = new FormData(form);"
                  "$.ajax({"
                    "url: '/update',"
                    "type: 'POST',"
                    "data: data,"
                    "contentType: false,"
                    "processData:false,"
                    "xhr: function() {"
                      "var xhr = new window.XMLHttpRequest();"
                      "xhr.upload.addEventListener('progress', function(evt) {"
                        "if (evt.lengthComputable) {"
                          "var per = evt.loaded / evt.total;"
                          "$('#prg').html('progress: ' + Math.round(per*100) + '%');"
                        "}"
                      "}, false);"
                      "return xhr;"
                    "},"
                    "success:function(d, s) {"
                      "console.log('success!')"
                    "},"
                    "error: function (a, b, c) {}"
                  "});"
                "});"
              "</script>"
            "</div>"
          "</div>"
          "<hr><div class='text-center mb-4'>"
            "Compilation " + String(__DATE__) + " " + String(__TIME__) + 
          "</div>"
        "</div>"
        "<script>"
          "function fetchDataAndPopulate(apiEndpoint, contentDivId) {"
            "$.get(apiEndpoint, function(data) {"
              "const content = JSON.stringify(data, null, 2);"
              "$(contentDivId).html(content);"
            "});"
          "}"
          "$('#myTabs a').on('shown.bs.tab', function (e) {"
            "const tabId = e.target.id;"
            "if (tabId == 'network-tab') {"
              "$('#network').html('Searching available networks...');"
              "fetchDataAndPopulate('/changeNetwork', '#network');"
            "}"
          "});"
          "function onLoad() {"
            "fetchDataAndPopulate('/getVal?v=currModePref', '#mode-pref-content');"
          "}"
          "function btnClick(clickedElement) {"
            "console.log('Clicked:', clickedElement.id);"
            "const modeSel = clickedElement.id.match(/^mode-(\\d+)$/);"
            "if (modeSel) {"
              "fetch('/setvalue?mode=' + parseInt(modeSel[1], 10), { method: 'POST' }).then(response => response.text());"
              "fetchDataAndPopulate('/getVal?v=currModeS', '#curr-name');"
              "fetchDataAndPopulate('/getVal?v=currModePref', '#mode-pref-content');"
            "} else {"
              "switch (clickedElement.id) {"
                "case 'clear-btn':"
                  "fetch('/clear', { method: 'POST' })"
                    ".then(response => response.text());"
                  "console.log('done clear');"
                  "break;"
                "case '2-btn':"
                  "fetchDataAndPopulate('/api/general', '#mode-pref-content');"
                  "console.log('done 2');"
                  "break;"
                "case '3-btn':"
                  "fetchDataAndPopulate('/api/general', '#mode-pref-content');"
                  "console.log('done 3');"
                  "break;"
                "default:"
                  "break;"
              "}"
            "}"
          "}</script></body></html>");
    });
    server.on("/changeNetwork", HTTP_GET, [](){
        server.sendHeader("Connection", "close");
        server.send(200, "text/html", network_page_html());
    });
    server.on("/saveNetwork", HTTP_POST, [](){
        if (server.hasArg("ssid") && server.hasArg("password")) { // check if value parameter is present
        String newSSID = server.arg("ssid");
        String newPassword = server.arg("password");

        // Save new credentials to SPIFFS
        save_new_wifi_config(newSSID, newPassword);

        // Connect to the new WiFi network
        WiFi.begin(newSSID.c_str(), newPassword.c_str());

        server.send(200, "text/html", "restarting device");
        handle_restart();
        }
        else {
        server.send(200, "text/html", "Wrong parameters");
        }
    });
    server.on("/getVal", HTTP_GET, [](){
      String paramName = server.arg("v");
      if(paramName == "currMode") {
        server.send(200, "text/plain", String(current_mode));
      }
      else if(paramName == "currModeS"){
        server.send(200, "text/html", get_curr_mode_name());
      }
      else if(paramName == "currModePref"){
        server.send(200, "text/html", get_html_settings_for_mode());
      }

    });
    server.on("/clear", handle_clear_strip); // clear strip
    server.on("/restart", handle_restart);
    // SELECTIVE MODE START
    server.on("/selectiveSet", HTTP_POST, [](){
        String paramName = server.argName(0); // Get the name of the parameter
        String paramValue = server.arg(0); // Get the value of the parameter
        if(paramName == "i"){
          ajax_current_pixel = paramValue.toInt(); 
          server.send(200, "text/plain", "OK");
        }
        else if(paramName == "r"){
          pixels[ajax_current_pixel].red = paramValue.toInt();
          server.send(200, "text/plain", "OK");
        }
        else if(paramName == "g"){
          pixels[ajax_current_pixel].green = paramValue.toInt();
          server.send(200, "text/plain", "OK");
        }
        else if(paramName == "b"){
          pixels[ajax_current_pixel].blue = paramValue.toInt();
          server.send(200, "text/plain", "OK");
        }
        else if(paramName == "send"){
          send_pixel(ajax_current_pixel);
          server.send(200, "text/plain", "OK");
        }
    });
    server.on("/selectiveGet", HTTP_GET, [](){
      String paramName = server.arg("v");
      if(paramName == "i") {
        server.send(200, "text/plain", String(ajax_current_pixel));
      }
      else if(paramName == "r"){
        server.send(200, "text/plain", String(pixels[ajax_current_pixel].red));
      }
      else if(paramName == "g"){
        server.send(200, "text/plain", String(pixels[ajax_current_pixel].green));
      }
      else if(paramName == "b"){
        server.send(200, "text/plain", String(pixels[ajax_current_pixel].blue));
      }
    });
    // SELECTIVE MODE END
  // GALAXY MODE START
  server.on("/galaxySet", HTTP_POST, [](){
    String paramName = server.argName(0); // Get the name of the parameter
    String paramValue = server.arg(0); // Get the value of the parameter
    if(paramName == "masterDel"){
      galaxy_master_delay = paramValue.toInt(); 
      galaxy_params.putUShort("MasterDel", galaxy_master_delay);
      server.send(200, "text/plain", "OK");
    }
    else if(paramName == "minDel"){
      galaxy_min_del= paramValue.toInt(); 
      galaxy_params.putUShort("MinDel", galaxy_min_del);
      server.send(200, "text/plain", "OK");
    }
    else if(paramName == "maxDel"){
      galaxy_max_del = paramValue.toInt(); 
      galaxy_params.putUShort("MaxDel", galaxy_max_del);
      server.send(200, "text/plain", "OK");
    }
    else if(paramName == "workers"){
      int newsize = paramValue.toInt(); 
      galaxy_curr_delay = resize_array(galaxy_curr_delay, galaxy_led_workers,newsize);
      galaxy_led_workers = newsize;
      galaxy_params.putUShort("LedWorkers", galaxy_led_workers);
      server.send(200, "text/plain", "OK");
    }
  });
    server.on("/galaxyGet", HTTP_GET, [](){
      String paramName = server.arg("v");
      if(paramName == "masterDel") {
        server.send(200, "text/plain", String(galaxy_master_delay));
      }
      else if(paramName == "minDel"){
        server.send(200, "text/plain", String(galaxy_min_del));
      }
      else if(paramName == "maxDel"){
        server.send(200, "text/plain", String(galaxy_max_del));
      }
      else if(paramName == "workers"){
        server.send(200, "text/plain", String(galaxy_led_workers));
      }
  });
  // GALAXY MODE END
  // STATIC MODE START
  server.on("/staticSet", HTTP_POST, [](){
    String paramName = server.argName(0); // Get the name of the parameter
    String paramValue = server.arg(0); // Get the value of the parameter
    if (paramName == "save") {
      static_color_save();
      server.send(200, "text/plain", "OK");
    }
    else if(paramName == "hex"){
      hex_to_rgb(paramValue, &current_static_color);
      static_color_changed = true;
      static_color_set();
      server.send(200, "text/plain", "OK");
    }
  });
  server.on("/staticGet", HTTP_GET, [](){
    String paramName = server.arg("v");
    if(paramName == "hex"){
      server.send(200, "text/plain", String(rgb_to_hex(current_static_color.red, current_static_color.green, current_static_color.blue)));
    }
  });
  // STATIC MODE END
  // RAINBOW MODE START
  server.on("/rainbowSet", HTTP_POST, [](){
    String paramName = server.argName(0); // Get the name of the parameter
    String paramValue = server.arg(0); // Get the value of the parameter
    if(paramName == "masterDel"){
      rainbow_master_delay = paramValue.toInt(); 
      rainbow_params.putUShort("rMasterDel", rainbow_master_delay);
      server.send(200, "text/plain", "OK");
    }
    else if(paramName == "maxChange"){
      rainbow_max_change = paramValue.toInt(); 
      rainbow_params.putUShort("rMaxChange", rainbow_max_change);
      server.send(200, "text/plain", "OK");
    }
  });
    server.on("/rainbowGet", HTTP_GET, [](){
      String paramName = server.arg("v");
      if(paramName == "masterDel") {
        server.send(200, "text/plain", String(rainbow_master_delay));
      }
      else if(paramName == "maxChange"){
        server.send(200, "text/plain", String(rainbow_max_change));
      }
  });
  // RAINBOW MODE END
  // RAINBOW FLOW MODE START
  server.on("/rainbowFlowSet", HTTP_GET, []() {
    String paramName = server.argName(0); // Get the name of the parameter
    String paramValue = server.arg(0); // Get the value of the parameter
    Serial.println("Change: " + paramName + " to: " + paramValue);
    if (paramValue != "") {
      if (paramName == "speed") {
        rainbow_flow_change_rate = paramValue.toInt();
        rainbow_flow_params.putUShort("rChRate", rainbow_flow_change_rate);
        Serial.println("Change rate changed to: " + String(rainbow_flow_change_rate));
      } else if (paramName == "degree") {
        rainbow_flow_change_degree = paramValue.toInt();
        rainbow_flow_params.putUShort("rChDeg", rainbow_flow_change_degree);
        Serial.println("Change degree changed to: " + String(rainbow_flow_change_degree));
      } else if (paramName == "grad") {
        rainbow_flow_gradient_density = paramValue.toInt();
        rainbow_flow_params.putUShort("rGradDen", rainbow_flow_gradient_density);
        Serial.println("Gradient density changed to: " + String(rainbow_flow_gradient_density));
      } else if (paramName == "del") {
        rainbow_flow_master_delay = paramValue.toInt();
        rainbow_flow_params.putUShort("rMDel", rainbow_flow_master_delay);
        Serial.println("Delay changed to: " + String(rainbow_flow_master_delay));
      } else if (paramName == "dir") {
        if (rainbow_flow_params.getBool("rDir")) {
          rainbow_flow_direction = false;
          rainbow_flow_params.putBool("rDir", false);
          Serial.println("Delay changed to: false");
        } else {
          rainbow_flow_direction = true;
          rainbow_flow_params.putBool("rDir", true);
          Serial.println("Delay changed to: true");
        }
      }
      server.send(200, "text/plain", "OK");
    } else {
      server.send(404, "text/plain", "Parameter not found");
    }
  });
  server.on("/rainbowFlowGet", HTTP_GET, [](){
    String paramName = server.arg("v");
    if(paramName == "speed") {
      server.send(200, "text/plain", String(rainbow_flow_change_rate));
    }
  });
  // RAINBOW FLOW MODE END
  server.on("/setvalue", handle_set_value);
  /*handling uploading firmware file */
  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      #if defined(DEBUG)
        Serial.printf("Update: %s\n", upload.filename.c_str());
      #endif    /* defined(DEBUG) */
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
        #if defined(DEBUG)
          Update.printError(Serial);
        #endif    /* defined(DEBUG) */
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      /* flashing firmware to ESP*/
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        #if defined(DEBUG)
          Update.printError(Serial);
        #endif    /* defined(DEBUG) */
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      #if defined(DEBUG)
        if (Update.end(true)) { //true to set the size to the current progress
          Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
        } else {
          Update.printError(Serial);
        }
      #endif    /* defined(DEBUG) */
    }
  });
    server.begin();
}

String get_html_settings_for_mode() {
  #if defined(DEBUG)
    Serial.println("--- get_html_settings_for_mode("+String(current_mode)+") START---");
  #endif    /* defined(DEBUG) */
  switch (current_mode) {
  case 0:
    return "Off";
    break;
  case 1: /* galaxy */
    return 
      "<div class='row justify-content-center'>"
        "<div class='col-auto text-center'>"
          "<div class='form-row align-items-center mb-2'>"
            "<div class='col-auto'><label for='exampleNumber'>Master delay:</label></div>"
            "<div class='col-auto'><input type='number' class='form-control' id='masterDel' min='0' step='1' value='" + String(galaxy_master_delay) + "'></div>"
            "<div class='col-auto'><button type='button' class='btn btn-primary' id='bt-masterDel' onclick='galaxySetVal(this)'>Set</button></div>"
          "</div>"
          "<div class='form-row align-items-center mb-2'>"
            "<div class='col-auto'><label for='exampleNumber'>Minimum delay:</label></div>"
            "<div class='col-auto'><input type='number' class='form-control' id='minDel' min='0' step='1' value='" + String(galaxy_min_del) + "'></div>"
            "<div class='col-auto'><button type='button' class='btn btn-primary' id='bt-minDel' onclick='galaxySetVal(this)'>Set</button></div>"
          "</div>"
          "<div class='form-row align-items-center mb-2'>"
            "<div class='col-auto'><label for='exampleNumber'>Maximum delay:</label></div>"
            "<div class='col-auto'><input type='number' class='form-control' id='maxDel' min='0' step='1' value='" + String(galaxy_max_del) + "'></div>"
            "<div class='col-auto'><button type='button' class='btn btn-primary' id='bt-maxDel' onclick='galaxySetVal(this)'>Set</button></div>"
          "</div>"
          "<div class='form-row align-items-center mb-2'>"
            "<div class='col-auto'><label for='exampleNumber'>LED workers:</label></div>"
            "<div class='col-auto'><input type='number' class='form-control' id='workers' min='0' step='1' value='" + String(galaxy_led_workers) + "'></div>"
            "<div class='col-auto'><button type='button' class='btn btn-primary' id='bt-workers' onclick='galaxySetVal(this)'>Set</button></div>"
          "</div>"
        "</div>"
      "</div>"
      "<script>"
        "function galaxySetVal(clickedElement) {"
          "switch (clickedElement.id) {"
            "case 'bt-masterDel':"
              "var paramId = 'masterDel';"
              "break;"
            "case 'bt-minDel':"
              "var paramId = 'minDel';"
              "break;"
            "case 'bt-maxDel':"
              "var paramId = 'maxDel';"
              "break;"
            "case 'bt-workers':"
              "var paramId = 'workers';"
              "break;"
            "default:"
              "var paramId = '';"
          "}"
          "var newval = parseInt(document.getElementById(paramId).value);"
          "fetch('/galaxySet?' + paramId + '=' + newval, { method: 'POST' }).then(response => response.text());"
        "}"
      "</script>";
    break;
  case 2: /* selective */
    return 
      "Pixel: <input type='number' id='currPixel' min='0' max='" + String(num_of_pixels) + "' step='1' value='0' style='display: inline-block;'><br><br>"
      "<input type='color' id='colorpicker' value='#000000'><br>"
			"R: <div id='Red' style='display: inline-block;'>0</div><br>"
			"G: <div id='Green' style='display: inline-block;'>0</div><br>"
			"B: <div id='Blue' style='display: inline-block;'>0</div><br>"
			"<br><input type='button' onclick='getColor()' value='get color'>"
			"<input type='button' onclick='setColor()' value='set color'>"
      "<script>"
        "function getColor() {"
          "var currPixel = document.getElementById('currPixel').value;"
				  "var rDiv = document.getElementById('Red');"
				  "var gDiv = document.getElementById('Green');"
				  "var bDiv = document.getElementById('Blue');"
				  "var rval = 0;"
				  "var gval = 0;"
				  "var bval = 0;"
          "fetch('/selectiveSet?i=' + currPixel, { method: 'POST' }).then(response => response.text());"
          "fetch('/selectiveGet?v=r').then(response => response.text()).then(data => {"
            "rval = parseInt(data);"
					  "rDiv.textContent = parseInt(data);"
          "});"
          "fetch('/selectiveGet?v=g').then(response => response.text()).then(data => {"
            "gval = parseInt(data);"
					  "gDiv.textContent = parseInt(data);"
          "});"
          "fetch('/selectiveGet?v=b').then(response => response.text()).then(data => {"
            "bval = parseInt(data);"
					  "bDiv.textContent = parseInt(data);"
          "});"
          "var RGBval = '#' + ((1 << 24) + (rval << 16) + (gval << 8) + bval).toString(16).slice(1);"
          "document.getElementById('colorpicker').value = RGBval;"
        "}"
        "function setColor() {"
          "var currPixel = document.getElementById('currPixel').value;"
          "fetch('/selectiveSet?i=' + currPixel, { method: 'POST' }).then(response => response.text());"
          "var RGB = document.getElementById('colorpicker').value;"
          "var r = parseInt(RGB.slice(1, 3), 16);"
          "var g = parseInt(RGB.slice(3, 5), 16);"
          "var b = parseInt(RGB.slice(5, 7), 16);"
          "fetch('/selectiveSet?r=' + r, { method: 'POST' }).then(response => response.text());"
          "fetch('/selectiveSet?g=' + g, { method: 'POST' }).then(response => response.text());"
          "fetch('/selectiveSet?b=' + b, { method: 'POST' }).then(response => response.text());"
          "fetch('/selectiveSet?send=0', { method: 'POST' }).then(response => response.text());"
        "}"
      "</script>";
    break;
  case 3: /* static */
    return 
      "<input type='color' id='colorpicker' value='" + rgb_to_hex(current_static_color.red, current_static_color.green, current_static_color.blue) + "'><br>"
      "HEX: <div id='StaHEX' style='display: inline-block;'>" + rgb_to_hex(current_static_color.red, current_static_color.green, current_static_color.blue) + "</div><br>"
			"R: <div id='Red' style='display: inline-block;'>" + current_static_color.red + "</div><br>"
			"G: <div id='Green' style='display: inline-block;'>" + current_static_color.green + "</div><br>"
			"B: <div id='Blue' style='display: inline-block;'>" + current_static_color.blue + "</div><br>"
			"<button type='button' class='btn btn-primary' id='mode-5' onclick='setStaticColor()'>Save color</button>"
      "<script>"
        "var colorpicker = document.getElementById('colorpicker');"
        "colorpicker.addEventListener('input', function(event) {"
            "var selected_color = event.target.value;"
            "var hexval = document.getElementById('colorpicker').value;"
            "var hexint = parseInt(hexval.slice(1, 7), 16);"
            "document.getElementById('Red').textContent = (hexint >> 16) & 255;"
            "document.getElementById('Green').textContent = (hexint >> 8) & 255;"
            "document.getElementById('Blue').textContent = hexint & 255;"
            "fetch('/staticSet?hex=' + hexval.slice(1, 7), { method: 'POST' }).then(response => response.text());"
        "});"
        "function setStaticColor() {"
          "fetch('/staticSet?save=0', { method: 'POST' }).then(response => response.text());"
        "}"
      "</script>";
    break;
  case 4: /* color flow */
    return 
      "<div class='row justify-content-center'>"
        "<div class='col-auto text-center'>"
          "<div class='form-row align-items-center mb-2'>"
            "<div class='col-auto'><label for='exampleNumber'>Master delay:</label></div>"
            "<div class='col-auto'><input type='number' class='form-control' id='masterDel' min='0' step='1' value='" + String(rainbow_master_delay) + "'></div>"
            "<div class='col-auto'><button type='button' class='btn btn-primary' id='bt-masterDel' onclick='setMasterDel()'>Set</button></div>"
          "</div>"
          "<div class='form-row align-items-center mb-2'>"
            "<div class='col-auto'><label for='exampleNumber'>Max change value:</label></div>"
            "<div class='col-auto'><input type='number' class='form-control' id='maxChange' min='0' step='1' value='" + String(rainbow_max_change) + "'></div>"
            "<div class='col-auto'><button type='button' class='btn btn-primary' id='bt-minDel' onclick='setMaxChange()'>Set</button></div>"
          "</div>"
        "</div>"
      "</div>"
      "<script>"
        "function setMasterDel() {"
          "var newval = parseInt(document.getElementById('masterDel').value);"
          "fetch('/rainbowSet?masterDel=' + newval, { method: 'POST' })"
            ".then(response => response.text());"
        "}"
        "function setMaxChange() {"
          "var newval = parseInt(document.getElementById('maxChange').value);"
          "fetch('/rainbowSet?maxChange=' + newval, { method: 'POST' })"
          ".then(response => response.text());"
        "}"
      "</script>";
    break;
  case 5: /* rainbow flow */
    return 
      "<div class='row justify-content-center'>"
        "<div class='col-auto text-center'>"
          "<div class='form-row align-items-center mb-2'>"
            "<div class='col-auto'><label for='exampleNumber'>Speed:</label></div>"
            "<div class='col-auto'><input type='number' class='form-control' id='masterSpeed' min='0' step='1' value='" + String(rainbow_flow_change_rate) + "'></div>"
            "<div class='col-auto'><button type='button' class='btn btn-primary' id='bt-masterSpeed' onclick='setVal(this)'>Set</button></div>"
          "</div>"
          "<div class='form-row align-items-center mb-2'>"
            "<div class='col-auto'><label for='exampleNumber'>Degree of animation:</label></div>"
            "<div class='col-auto'><input type='number' class='form-control' id='degree' min='0' step='1' value='" + String(rainbow_flow_change_degree) + "'></div>"
            "<div class='col-auto'><button type='button' class='btn btn-primary' id='bt-degree' onclick='setVal(this)'>Set</button></div>"
          "</div>"
          "<div class='form-row align-items-center mb-2'>"
            "<div class='col-auto'><label for='exampleNumber'>Gradient density:</label></div>"
            "<div class='col-auto'><input type='number' class='form-control' id='grad' min='0' step='1' value='" + String(rainbow_flow_gradient_density) + "'></div>"
            "<div class='col-auto'><button type='button' class='btn btn-primary' id='bt-grad' onclick='setVal(this)'>Set</button></div>"
          "</div>"
          "<div class='form-row align-items-center mb-2'>"
            "<div class='col-auto'><label for='exampleNumber'>Master delay:</label></div>"
            "<div class='col-auto'><input type='number' class='form-control' id='del' min='0' step='1' value='" + String(rainbow_flow_master_delay) + "'></div>"
            "<div class='col-auto'><button type='button' class='btn btn-primary' id='bt-del' onclick='setVal(this)'>Set</button></div>"
          "</div>"
          "<div class='form-row align-items-center mb-2'>"
            "<div class='col-auto'><button type='button' class='btn btn-primary' id='bt-dir' onclick='setVal(this)'>Change direction</button></div>"
          "</div>"
        "</div>"
      "</div>"
      "<script>"
        "function setVal(clickedElement) {"
          "switch(clickedElement.id) {"
            "case 'bt-masterSpeed':"
              "var newval = parseInt(document.getElementById('masterSpeed').value);"
              "fetch('/rainbowFlowSet?speed=' + newval, { method: 'GET' })"
                ".then(response => response.text());"
              "break;"
            "case 'bt-degree':"
              "var newval = parseInt(document.getElementById('degree').value);"
              "fetch('/rainbowFlowSet?degree=' + newval, { method: 'GET' })"
                ".then(response => response.text());"
              "break;"
            "case 'bt-grad':"
              "var newval = parseInt(document.getElementById('grad').value);"
              "fetch('/rainbowFlowSet?grad=' + newval, { method: 'GET' })"
                ".then(response => response.text());"
              "break;"
            "case 'bt-del':"
              "var newval = parseInt(document.getElementById('del').value);"
              "fetch('/rainbowFlowSet?del=' + newval, { method: 'GET' })"
                ".then(response => response.text());"
              "break;"
            "case 'bt-dir':"
              "fetch('/rainbowFlowSet?dir=1', { method: 'GET' })"
                ".then(response => response.text());"
              "break;"
            "default:"
              "break;"
          "}"
        "}"
      "</script>";
    break;
  default:
    return "Unknown";
    break;
  }
} 

void handle_set_value() {
  #if defined(DEBUG)
    Serial.println("--- handle_set_value START---");
  #endif    /* defined(DEBUG) */
  if (server.hasArg("mode")) { // check if value parameter is present
    current_mode = server.arg("mode").toInt(); // update variable value
    #if defined(DEBUG)
    Serial.println("Change mode to: "+String(current_mode));
    #endif    /* defined(DEBUG) */
    change_mode = true;
    for (int i = 0; i < num_of_pixels; i++) {
      pixels[i].red = 0;
      pixels[i].green = 0;
      pixels[i].blue = 0;
    }
    #if defined(DEBUG)
      Serial.println("pixels array is cleared");
    #endif    /* defined(DEBUG) */
    strip.clear();
    strip.show(); 
    #if defined(DEBUG)
      Serial.println("LED strip cleared");
    #endif    /* defined(DEBUG) */

    switch(current_mode){
      case 0: //off
        main_settings.putUShort("currentMode", 0);
        #if defined(DEBUG)
          Serial.println("Updated current_mode value: "+ String(main_settings.getUShort("currentMode")));
          Serial.println("--- handle_set_value END - set to 0 - OFF ---");
        #endif    /* defined(DEBUG) */
        break;
      case 1: //galaxy
        main_settings.putUShort("currentMode", 1);
        // for(int i = 0; i < galaxy_led_workers; ++i){
        //   galaxy_curr_delay[i] = i*random(0, 50);
        //   //Serial.println("set delay[" + String(i) + "]=" + String(currDelay[i]));
        //   set_pixel_color(int(random(0, num_of_pixels)), int(random(0, 255 + 1)), int(random(0, 255 + 1)), int(random(0, 255 + 1)));
        // }
        #if defined(DEBUG)
          Serial.println("--- handle_set_value END - set to 1 - GALAXY ---");
        #endif    /* defined(DEBUG) */
        break;
      case 2: // selective
        main_settings.putUShort("currentMode", 2);
        #if defined(DEBUG)
          Serial.println("--- handle_set_value END - set to 2 - SELECTIVE ---");
        #endif    /* defined(DEBUG) */
        break;
      case 3: //static
        static_color_set();
        main_settings.putUShort("currentMode", 3);
        #if defined(DEBUG)
          Serial.println("--- handle_set_value END - set to 3 - STATIC ---");
        #endif    /* defined(DEBUG) */
        break;
      case 4: //rainbow
        rainbow_change.red_change = random(0, rainbow_max_change + 1);
        rainbow_change.red_current = random(0, 255);
        #if defined(DEBUG)
          Serial.println("Current changes:");
          Serial.println("R: " + rainbow_change.red_change);
        #endif    /* defined(DEBUG) */
        rainbow_change.green_change = random(1, rainbow_max_change + 1);
        rainbow_change.green_current = random(0, 255);
        #if defined(DEBUG)
          Serial.println("G: "+ rainbow_change.green_change);
        #endif    /* defined(DEBUG) */
        rainbow_change.blue_change = random(1, rainbow_max_change + 1);
        rainbow_change.blue_current = random(0, 255);
        #if defined(DEBUG)
          Serial.println("B: "+ rainbow_change.blue_change);
        #endif    /* defined(DEBUG) */
        //all count up
        rainbow_change.red_up = true;
        rainbow_change.green_up = true;
        rainbow_change.blue_up = true;
        #if defined(DEBUG)
          Serial.println("Rainbow mode initialized");
        #endif    /* defined(DEBUG) */

        rainbow_mode();
        main_settings.putUShort("currentMode", 4);
        #if defined(DEBUG)
          Serial.println("--- handle_set_value END - set to 4 - RAINBOW ---");
        #endif    /* defined(DEBUG) */
        break;
      case 5: // rainbow flow
        main_settings.putUShort("currentMode", 5);
        #if defined(DEBUG)
          Serial.println("--- handle_set_value END - set to 5 - RAINBOW FLOW ---");
        #endif    /* defined(DEBUG) */
        break;
      default:
        break;
    }
    server.send(200, "text/html", "Mode changed <br><br><a href='./'> Get back to main page</a>"); // send plain text response with new variable value
  } else if (server.hasArg("domname")) {
    mdns_host_name = server.arg("domname");
    dev_settings.putString("host", mdns_host_name);
    #if defined(DEBUG)
      Serial.println("--- handle_set_value END - mDNS changed to: " + mdns_host_name + ".local");
    #endif    /* defined(DEBUG) */
  } else if (server.hasArg("size")) {
    int in_row, rows;

    if (server.hasArg("x")) {
      pixels_in_row = server.arg("x").toInt();
      dev_settings.putUShort("pixelsInRow", pixels_in_row);
    } else {
      #if defined(DEBUG)
        Serial.println("--- handle_set_value no x param");
      #endif    /* defined(DEBUG) */
      return;
    }

    if (server.hasArg("y")) {
      pixels_rows = server.arg("y").toInt();
      dev_settings.putUShort("pixelsRows", pixels_rows);
    } else {
      #if defined(DEBUG)
        Serial.println("--- handle_set_value no y param");
      #endif    /* defined(DEBUG) */
      return;
    }
    
    #if defined(DEBUG)
      Serial.println("--- handle_set_value new matrix size - x: " + String(pixels_in_row) + " y: " + String(pixels_rows));
    #endif    /* defined(DEBUG) */
  }
}

void handle_clear_strip() {
  strip.clear();
  strip.show();
  server.send(200, "text/html", "Cleared<br><br><a href='./'> Get back to main page</a>"); // send plain text response with new variable value
}
