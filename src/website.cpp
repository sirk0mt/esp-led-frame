#include "website.h"
#include "settings_things.h"
#include "network_things.h"
#include "pixels.h"
#include "json.h"

#include "all_modes.h"

uint16_t    ajax_current_pixel  = 0;

String get_num_row_with_save_button(String label, String input_id,
                  int min, int value, String bt_id, String onlick, String bt_label) {
  return "<div class='form-row align-items-center mb-2'>"
            "<div class='col-auto'><label>" + label + "</label></div>"
            "<div class='col-auto'><input type='number' class='form-control' id='" + input_id + "' min='" + String(min) + "' step='1' value='" + String(value) + "'></div>"
            "<div class='col-auto'><button type='button' class='btn btn-primary' id='" + bt_id + "' onclick='" + onlick + "'>" + bt_label + "</button></div>"
          "</div>";
}

String get_centered_button(String id, String onclick, String label) {
  return "<div class='form-row align-items-center mb-2'>"
            "<div class='col-auto'><button type='button' class='btn btn-primary' id='" + id + "' onclick='" + onclick + "'>" + label + "</button></div>"
          "</div>";
}

String radio_btn_html(String name, String id_val, uint8_t radio_id, String value) {
  String check_state = "";
  if (radio_id == color_order) {
    check_state = "checked";
  } else {
    check_state = "";
  }
  return
    "<label class='btn btn-secondary'>"
      "<input type='radio' name='" + name + "' id='" + id_val + "' " + check_state + "> " + value + " "
    "</label>";
}

void handleFileList() {
    File root = LITTLEFS.open("/");
    String fileList = "<html><body><h1>File List:</h1><ul>";
    
    File file = root.openNextFile();
    while (file) {
        fileList += "<li>" + String(file.name()) + " - " + String(file.size()) + " bytes</li>";
        file = root.openNextFile();
    }
    fileList += "</ul></body></html>";
    server.send(200, "text/html", fileList);
}

void handleFileDelete() {
    String filePath = server.arg("file");
    if (LITTLEFS.remove(filePath)) {
        server.send(200, "text/plain", "File deleted successfully");
    } else {
        server.send(404, "text/plain", "Failed to delete file");
    }
}


void redirect_to_root() {
  server.sendHeader("Location", "http://" + server.client().localIP().toString());
  server.send(302, "text/plain", "");
}

/* ToDo maybe all subsites should be dynamic loaded like network settings */
void website_handle_root() {
  server.sendHeader("Connection", "close");
  server.send(200, "text/html", 
  "<html> "
    "<head>"
      "<title>LED PICTURE " + String(ver) + "</title>"
      "<link href='https://maxcdn.bootstrapcdn.com/bootstrap/4.5.2/css/bootstrap.min.css' rel='stylesheet'>"
      "<script src='https://code.jquery.com/jquery-3.5.1.min.js'></script>"
      "<script src='https://cdn.jsdelivr.net/npm/@popperjs/core@2.9.1/dist/umd/popper.min.js'></script>"
      "<script src='https://maxcdn.bootstrapcdn.com/bootstrap/4.5.2/js/bootstrap.min.js'></script>"
    "</head>"
  "<body onload='onLoad()'>"
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
            + get_centered_button("set_mdns", "setDomain();", "save and reboot") +
          "</div>"
          "<br><br>"
          "<b>Choose LED color order: <br>"
          "<div class='btn-group btn-group-toggle' data-toggle='buttons'>"
            + radio_btn_html("colOr", "order0", RGB_ORDER, "RGB") +
            radio_btn_html("colOr", "order1", RBG_ORDER, "RBG") +
            radio_btn_html("colOr", "order2", GRB_ORDER, "GRB") +
            radio_btn_html("colOr", "order3", GBR_ORDER, "GBR") +
            radio_btn_html("colOr", "order4", BRG_ORDER, "BRG") +
            radio_btn_html("colOr", "order5", BGR_ORDER, "BGR") +
          "</div>"
          "<br><br>" + get_centered_button("res_bt", "restart();", "Restart device") + 
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
      "<hr>"
      "<div class='text-center mb-4'>"
        "Compilation " + String(__DATE__) + " " + String(__TIME__) + 
      "</div>"
    "</div>"
    "<script>"
    "function onLoad() {"
      "fetchDataAndPopulate('/getVal?v=currModePref', '#mode-pref-content');"
    "}"
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
    "}"
  "</script>"
  "</body></html>"
  );
} /* ToDo rewrite endpoints and need some cleanup */


void start_main_server() {
  galaxy_init_endpoints();
  server.on("/", website_handle_root);



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
  case MODE_OFF:
    return "Off";
    break;
  case MODE_GALAXY:
    return galaxy_html();
    break;
  case MODE_PIXELS:
    return "Pixel: <input type='number' id='currPixel' min='0' max='" + String(num_of_pixels) + "' step='1' value='0' style='display: inline-block;'><br><br>"
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
  case MODE_STATIC:
    return static_html();
    break;
  case MODE_RAINBOW:
    return rainbow_html();
    break;
  case MODE_RAINBOW_FLOW:
    return rainbow_flow_html();      
    break;
  default:
    return "Unknown";
    break;
  }
} 

void handle_set_value() {  // ToDo - rewrite endpoints to settings things
  #if defined(DEBUG)
    Serial.println("--- handle_set_value START---");
  #endif    /* defined(DEBUG) */
  if (server.hasArg("mode")) { // check if value parameter is present
    current_mode = server.arg("mode").toInt(); // update variable value
    #if defined(DEBUG)
      Serial.println("Change mode to: "+String(current_mode));
    #endif    /* defined(DEBUG) */
    change_mode = true;
    for (int i = 0; i < num_of_pixels; i++) { /* clear whole pixels array */
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

    main_json_doc["current_mode"] = current_mode;
    json_save_prefs_file(main_prefs_file);
    #if defined(DEBUG)
      Serial.println("Updated current_mode value: "+ String(current_mode));
    #endif    /* defined(DEBUG) */

    switch(current_mode) {
      case MODE_OFF:
        #if defined(DEBUG)
          Serial.println("--- handle start MODE_OFF---");
        #endif    /* defined(DEBUG) */
        break;
      case MODE_GALAXY:
        galaxy_start();
        #if defined(DEBUG)
          Serial.println("--- handle start MODE_GALAXY---");
        #endif    /* defined(DEBUG) */
        break;
      case MODE_STATIC:
        static_start();
        #if defined(DEBUG)
          Serial.println("--- handle start MODE_STATIC---");
        #endif    /* defined(DEBUG) */
        break;
      case MODE_RAINBOW:
        rainbow_start();
        #if defined(DEBUG)
          Serial.println("--- handle start MODE_RAINBOW---");
        #endif    /* defined(DEBUG) */
        break;
      case MODE_RAINBOW_FLOW:
        rainbow_flow_start();
        #if defined(DEBUG)
          Serial.println("--- handle start MODE_RAINBOW_FLOW---");
        #endif    /* defined(DEBUG) */
        break;
      case 99:
        #if defined(DEBUG)
          Serial.println("--- handle_set_value END - set to 99 - SELECTIVE ---");
        #endif    /* defined(DEBUG) */
        break;
      default:
        break;
    }
    server.send(200, "text/html", "Mode changed <br><br><a href='./'> Get back to main page</a>"); // send plain text response with new variable value
  } else if (server.hasArg("domname")) {
    mdns_host_name = server.arg("domname");
    main_json_doc["host"] = mdns_host_name;
    #if defined(DEBUG)
      Serial.println("--- handle_set_value END - mDNS changed to: " + mdns_host_name + ".local");
    #endif    /* defined(DEBUG) */
  } else if (server.hasArg("size")) {
    int in_row, rows;

    if (server.hasArg("x")) {
      pixels_in_row = server.arg("x").toInt();
      main_json_doc["led_in_row"] = pixels_in_row ;
    } else {
      #if defined(DEBUG)
        Serial.println("--- handle_set_value no x param");
      #endif    /* defined(DEBUG) */
      return;
    }

    if (server.hasArg("y")) {
      pixels_rows = server.arg("y").toInt();
      main_json_doc["led_rows"] = pixels_rows;
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
