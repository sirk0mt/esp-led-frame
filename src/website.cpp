#include "website.h"
#include "settings_things.h"

uint16_t ajaxCurrPixel = 0;
uint8_t ajaxCurrR = 0;
uint8_t ajaxCurrG = 0;
uint8_t ajaxCurrB = 0;

const char* autoBack = "<script>window.location.href = window.location.origin;</script>"; 

const char* updateIndex =
"<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
"<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
   "<input type='file' name='update'>"
        "<input type='submit' value='Update'>"
    "</form>"
 "<div id='prg'>progress: 0%</div>"
 "<br><br><br><br><a href='./'> Get back to main page</a>"
 "<script>"
  "$('form').submit(function(e){"
  "e.preventDefault();"
  "var form = $('#upload_form')[0];"
  "var data = new FormData(form);"
  " $.ajax({"
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
 "error: function (a, b, c) {"
 "}"
 "});"
 "});"
 "</script>";

String getAvailableNetworksSite() {
    String networkChoiseSiteHead = "<html><body>"
                            "<p>Choose a WiFi network:</p>"
                            "<form method='post' action='/saveNetwork'>"
                            "SSID: <input type='text' name='ssid' id='ssid'><br>"
                            "Password: <input type='password' name='password'><br>"
                            "<input type='submit' value='Connect'>"
                            "</form>"
                            "<h2>Available Networks</h2>";
    String networkChoiseSiteFooter = "<script>"
                                    "function copyText(element) {"
                                    "var textToCopy = element.textContent || element.innerText;"
                                    "document.getElementById('ssid').value = textToCopy;"
                                    "}"
                                    "</script></body></html>";

    return networkChoiseSiteHead + String(listVisibleNetworks()) + networkChoiseSiteFooter;
}


void handleRoot() {
  server.sendHeader("Location", "http://" + server.client().localIP().toString());
  server.send(302, "text/plain", "");
}

void startSettingsWebServer() {
  server.on("/", HTTP_GET, [](){
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", getAvailableNetworksSite());
  });

  server.on("/saveNetwork", HTTP_POST, [](){
    if (server.hasArg("ssid") && server.hasArg("password")) { // check if value parameter is present
      // Save new credentials to SPIFFS
      saveWiFiConfig(server.arg("ssid"), server.arg("password"));

      server.send(200, "text/html", "restarting device");
      handleRestart();
    }
    else {
      server.send(200, "text/html", "Wrong parameters");
    }
  });

  server.onNotFound(handleRoot);
  server.begin();
  #ifdef DEBUG
    Serial.println("Config server started");
  #endif
}

void startWebServer() {
    server.on("/", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", "<center><font size=16>"
            "<b>Inteligentny obraz<br>" + String(ver) + "</b>"
            "<br>"
            "<a href='/setvalue?mode=0'>Off</a><br>"
            "<a href='/setvalue?mode=1'>Galaxy mode</a><br>"
            "<a href='/setvalue?mode=2'>Selective mode</a><br>"
            "<a href='/setvalue?mode=3'>Static mode</a><br>"
            "<a href='/setvalue?mode=4'>Rainbow mode</a><br>"
            "<br>"
            + String(getModeSettings(currentMode)) +
            "<br>"
            "<a href='/clear'>Clear strip</a><br>"
            "<br>"
            "<a href='/updateIndex'>Aktualizacja</a>"
            "<br>"
            "<a href='/changeNetwork'>Change network</a>"
            "<br>"
            "<a href='/restart'>Restart</a>"
    "</font><br><br><br>"
    "<b>WiFi network:</b> " + WiFi.SSID() + "<br><br>"
    "<b>WiFi RSSI:</b> " + String(WiFi.RSSI())+"<br><br>"
    "Kompilacja " + String(__DATE__) + " " + String(__TIME__) +
    "</center>");
    });
    server.on("/changeNetwork", HTTP_GET, [](){
        server.sendHeader("Connection", "close");
        server.send(200, "text/html", getAvailableNetworksSite());
    });
    server.on("/saveNetwork", HTTP_POST, [](){
        if (server.hasArg("ssid") && server.hasArg("password")) { // check if value parameter is present
        String newSSID = server.arg("ssid");
        String newPassword = server.arg("password");

        // Save new credentials to SPIFFS
        saveWiFiConfig(newSSID, newPassword);

        // Connect to the new WiFi network
        WiFi.begin(newSSID.c_str(), newPassword.c_str());

        server.send(200, "text/html", "restarting device");
        handleRestart();
        }
        else {
        server.send(200, "text/html", "Wrong parameters");
        }
    });
    server.on("/clear", handleClear); // clear strip
    server.on("/restart", handleRestart);
    // SELECTIVE MODE START
    server.on("/selectiveSet", HTTP_POST, [](){
        String paramName = server.argName(0); // Get the name of the parameter
        String paramValue = server.arg(0); // Get the value of the parameter
        if(paramName == "i"){
        ajaxCurrPixel = paramValue.toInt(); 
        server.send(200, "text/plain", "OK");
        }
        else if(paramName == "r"){
        PIXELS[ajaxCurrPixel].R = paramValue.toInt();
        server.send(200, "text/plain", "OK");
        }
        else if(paramName == "g"){
        PIXELS[ajaxCurrPixel].G = paramValue.toInt();
        server.send(200, "text/plain", "OK");
        }
        else if(paramName == "b"){
        PIXELS[ajaxCurrPixel].B = paramValue.toInt();
        server.send(200, "text/plain", "OK");
        }
        else if(paramName == "send"){
        sendPixelColor(ajaxCurrPixel);
        server.send(200, "text/plain", "OK");
        }
    });
    server.on("/selectiveGet", HTTP_GET, [](){
      String paramName = server.arg("v");
      if(paramName == "i") {
        server.send(200, "text/plain", String(ajaxCurrPixel));
      }
      else if(paramName == "r"){
        server.send(200, "text/plain", String(PIXELS[ajaxCurrPixel].R));
      }
      else if(paramName == "g"){
        server.send(200, "text/plain", String(PIXELS[ajaxCurrPixel].G));
      }
      else if(paramName == "b"){
        server.send(200, "text/plain", String(PIXELS[ajaxCurrPixel].B));
      }
    });
    // SELECTIVE MODE END
  // GALAXY MODE START
  server.on("/galaxySet", HTTP_POST, [](){
    String paramName = server.argName(0); // Get the name of the parameter
    String paramValue = server.arg(0); // Get the value of the parameter
    if(paramName == "masterDel"){
      galaxyMasterDelay = paramValue.toInt(); 
      galaxyParams.putUShort("MasterDel", galaxyMasterDelay);
      server.send(200, "text/plain", "OK");
    }
    else if(paramName == "minDel"){
      galaxyMinDel= paramValue.toInt(); 
      galaxyParams.putUShort("MinDel", galaxyMinDel);
      server.send(200, "text/plain", "OK");
    }
    else if(paramName == "maxDel"){
      galaxyMaxDel = paramValue.toInt(); 
      galaxyParams.putUShort("MaxDel", galaxyMaxDel);
      server.send(200, "text/plain", "OK");
    }
    else if(paramName == "workers"){
      int newsize = paramValue.toInt(); 
      galaxyCurrDelay = resizeArray(galaxyCurrDelay,galaxyLedWorkers,newsize);
      galaxyLedWorkers = newsize;
      galaxyParams.putUShort("LedWorkers", galaxyLedWorkers);
      server.send(200, "text/plain", "OK");
    }
  });
    server.on("/galaxyGet", HTTP_GET, [](){
      String paramName = server.arg("v");
      if(paramName == "masterDel") {
        server.send(200, "text/plain", String(galaxyMasterDelay));
      }
      else if(paramName == "minDel"){
        server.send(200, "text/plain", String(galaxyMinDel));
      }
      else if(paramName == "maxDel"){
        server.send(200, "text/plain", String(galaxyMaxDel));
      }
      else if(paramName == "workers"){
        server.send(200, "text/plain", String(galaxyLedWorkers));
      }
  });
  // GALAXY MODE END
  // STATIC MODE START
  server.on("/staticSet", HTTP_POST, [](){
    String paramName = server.argName(0); // Get the name of the parameter
    String paramValue = server.arg(0); // Get the value of the parameter
    if(paramName == "r"){
      currentStatic.R = paramValue.toInt();
      server.send(200, "text/plain", "OK");
    }
    else if(paramName == "g"){
      currentStatic.G = paramValue.toInt();
      server.send(200, "text/plain", "OK");
    }
    else if(paramName == "b"){
      currentStatic.B = paramValue.toInt();
      server.send(200, "text/plain", "OK");
    }
    else if(paramName == "send"){
      staticColorChanged = true;
      staticSetColor();
      server.send(200, "text/plain", "OK");
    }
  });
    server.on("/staticGet", HTTP_GET, [](){
      String paramName = server.arg("v");
      if(paramName == "r"){
        server.send(200, "text/plain", String(currentStatic.R));
      }
      else if(paramName == "g"){
        server.send(200, "text/plain", String(currentStatic.G));
      }
      else if(paramName == "b"){
        server.send(200, "text/plain", String(currentStatic.B));
      }
  });
  // STATIC MODE END
  // RAINBOW MODE START
  server.on("/rainbowSet", HTTP_POST, [](){
    String paramName = server.argName(0); // Get the name of the parameter
    String paramValue = server.arg(0); // Get the value of the parameter
    if(paramName == "masterDel"){
      rainbowMasterDelay = paramValue.toInt(); 
      rainbowParams.putUShort("rMasterDel", rainbowMasterDelay);
      server.send(200, "text/plain", "OK");
    }
    else if(paramName == "maxChange"){
      rainbowMaxChange = paramValue.toInt(); 
      rainbowParams.putUShort("rMaxChange", rainbowMaxChange);
      server.send(200, "text/plain", "OK");
    }
  });
    server.on("/rainbowGet", HTTP_GET, [](){
      String paramName = server.arg("v");
      if(paramName == "masterDel") {
        server.send(200, "text/plain", String(rainbowMasterDelay));
      }
      else if(paramName == "maxChange"){
        server.send(200, "text/plain", String(rainbowMaxChange));
      }
  });
  // RAINBOW MODE END
  server.on("/setvalue", handleSetValue);
  server.on("/updateIndex", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", updateIndex);
  });
  /*handling uploading firmware file */
  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      #ifdef DEBUG
        Serial.printf("Update: %s\n", upload.filename.c_str());
      #endif
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
        #ifdef DEBUG
          Update.printError(Serial);
        #endif
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      /* flashing firmware to ESP*/
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        #ifdef DEBUG
          Update.printError(Serial);
        #endif
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      #ifdef DEBUG
        if (Update.end(true)) { //true to set the size to the current progress
          Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
        } else {
          Update.printError(Serial);
        }
      #endif
    }
  });
    server.begin();
}

String getModeSettings(int mode) {
  #ifdef DEBUG
    Serial.println("--- getModeSettings("+String(mode)+") START---");
  #endif
  switch (mode) {
  case 0:
    return "Off";
    break;
  case 1:
    return "Current mode: Galaxy<br>"
            "settings for galaxy"
            "<br><br>"
            "Master delay: <input type='number' id='masterDel' min='0' step='1' value='" + String(galaxyMasterDelay) + "' style='display: inline-block;'> <input type='button' onclick='setMasterDel()' value='set'> <br>"
            "Minimum delay: <input type='number' id='minDel' min='0' step='1' value='" + String(galaxyMinDel) + "' style='display: inline-block;'> <input type='button' onclick='setMinDel()' value='set'> <br>"
            "Maximum delay: <input type='number' id='maxDel' min='0' step='1' value='" + String(galaxyMaxDel) + "' style='display: inline-block;'> <input type='button' onclick='setMaxDel()' value='set'> <br>"
            "LED workers: <input type='number' id='workers' min='0' max='150' step='1' value='" + String(galaxyLedWorkers) + "' style='display: inline-block;'> <input type='button' onclick='setWorkers()' value='set'> <br>"
            "<script>"
              "function setMasterDel() {"
                "var newval = parseInt(document.getElementById('masterDel').value);"
                "fetch('/galaxySet?masterDel=' + newval, { method: 'POST' })"
                  ".then(response => response.text());"
              "}"
	            "function setMinDel() {"
                "var newval = parseInt(document.getElementById('minDel').value);"
                "fetch('/galaxySet?minDel=' + newval, { method: 'POST' })"
                ".then(response => response.text());"
              "}"
	            "function setMaxDel() {"
                "var newval = parseInt(document.getElementById('maxDel').value);"
                "fetch('/galaxySet?maxDel=' + newval, { method: 'POST' })"
                  ".then(response => response.text());"
              "}"
	            "function setWorkers() {"
                "var newval = parseInt(document.getElementById('workers').value);"
                "fetch('/galaxySet?workers=' + newval, { method: 'POST' })"
                  ".then(response => response.text());"
              "}"
            "</script>";
    break;
  case 2:
    return "Current mode: Selective<br>"
            "<br><br>"
            "Pixel: <input type='number' id='currPixel' min='0' max='" + String(NUMPIXELS) + "' step='1' value='0' style='display: inline-block;'><br><br>"
            "<input type='color' id='colorpicker' value='#000000'><br>"
			"R: <div id='Red' style='display: inline-block;'>0</div><br>"
			"G: <div id='Green' style='display: inline-block;'>0</div><br>"
			"B: <div id='Blue' style='display: inline-block;'>0</div><br>"
			"<br>"
            "<input type='button' onclick='getColor()' value='get color'>"
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
                "fetch('/selectiveSet?i=' + currPixel, { method: 'POST' })"
                  ".then(response => response.text());"
                "fetch('/selectiveGet?v=r')"
                  ".then(response => response.text())"
                  ".then(data => {"
                    "rval = parseInt(data);"
					"rDiv.textContent = parseInt(data);"
                  "});"
                "fetch('/selectiveGet?v=g')"
                  ".then(response => response.text())"
                  ".then(data => {"
                    "gval = parseInt(data);"
					"gDiv.textContent = parseInt(data);"
                  "});"
                "fetch('/selectiveGet?v=b')"
                  ".then(response => response.text())"
                  ".then(data => {"
                    "bval = parseInt(data);"
					"bDiv.textContent = parseInt(data);"
                  "});"
                "var RGBval = '#' + ((1 << 24) + (rval << 16) + (gval << 8) + bval).toString(16).slice(1);"
                "document.getElementById('colorpicker').value = RGBval;"
              "}"
              "function setColor() {"
                "var currPixel = document.getElementById('currPixel').value;"
                "fetch('/selectiveSet?i=' + currPixel, { method: 'POST' })"
                  ".then(response => response.text());"
                "var RGB = document.getElementById('colorpicker').value;"
                "var r = parseInt(RGB.slice(1, 3), 16);"
                "var g = parseInt(RGB.slice(3, 5), 16);"
                "var b = parseInt(RGB.slice(5, 7), 16);"
                "fetch('/selectiveSet?r=' + r, { method: 'POST' })"
                  ".then(response => response.text());"
                "fetch('/selectiveSet?g=' + g, { method: 'POST' })"
                  ".then(response => response.text());"
                "fetch('/selectiveSet?b=' + b, { method: 'POST' })"
                  ".then(response => response.text());"
                "fetch('/selectiveSet?send=0', { method: 'POST' })"
                  ".then(response => response.text());"
              "}"
            "</script>";
    break;
  case 3:
    return "Current mode: Static<br>"
            "<br><br>"
            "<input type='color' id='colorpicker' value='#000000'><br>"
			"R: <div id='Red' style='display: inline-block;'>0</div><br>"
			"G: <div id='Green' style='display: inline-block;'>0</div><br>"
			"B: <div id='Blue' style='display: inline-block;'>0</div><br>"
			"<br>"
            "<input type='button' onclick='getStaticColor()' value='get color'>"
			"<input type='button' onclick='setStaticColor()' value='set color'>"
            "<script>"
              "function getStaticColor() {"
				
				"var rDiv = document.getElementById('Red');"
				"var gDiv = document.getElementById('Green');"
				"var bDiv = document.getElementById('Blue');"
				"var rval = 0;"
				"var gval = 0;"
				"var bval = 0;"
                "fetch('/staticGet?v=r')"
                  ".then(response => response.text())"
                  ".then(data => {"
                    "rval = parseInt(data);"
					"rDiv.textContent = parseInt(data);"
                  "});"
                "fetch('/staticGet?v=g')"
                  ".then(response => response.text())"
                  ".then(data => {"
                    "gval = parseInt(data);"
					"gDiv.textContent = parseInt(data);"
                  "});"
                "fetch('/staticGet?v=b')"
                  ".then(response => response.text())"
                  ".then(data => {"
                    "bval = parseInt(data);"
					"bDiv.textContent = parseInt(data);"
                  "});"
                "var RGBval = '#' + ((1 << 24) + (rval << 16) + (gval << 8) + bval).toString(16).slice(1);"
                "document.getElementById('colorpicker').value = RGBval;"
              "}"
              "function setStaticColor() {"
                "var RGB = document.getElementById('colorpicker').value;"
                "var r = parseInt(RGB.slice(1, 3), 16);"
                "var g = parseInt(RGB.slice(3, 5), 16);"
                "var b = parseInt(RGB.slice(5, 7), 16);"
                "fetch('/staticSet?r=' + r, { method: 'POST' })"
                  ".then(response => response.text());"
                "fetch('/staticSet?g=' + g, { method: 'POST' })"
                  ".then(response => response.text());"
                "fetch('/staticSet?b=' + b, { method: 'POST' })"
                  ".then(response => response.text());"
                "fetch('/staticSet?send=0', { method: 'POST' })"
                  ".then(response => response.text());"
              "}"
            "</script>";
    break;
  case 4:
    return "Current mode: Rainbow<br>"
            "settings for rainbow"
            "<br><br>"
            "Master delay: <input type='number' id='masterDel' min='0' step='1' value='" + String(rainbowMasterDelay) + "' style='display: inline-block;'> <input type='button' onclick='setMasterDel()' value='set'> <br>"
            "Max change value: <input type='number' id='maxChange' min='0' step='1' value='" + String(rainbowMaxChange) + "' style='display: inline-block;'> <input type='button' onclick='setMaxChange()' value='set'> <br>"
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
  default:
    return "Unknown";
    break;
  }
} 

void handleSetValue() {
  #ifdef DEBUG
    Serial.println("--- handleSetValue START---");
  #endif
  if (server.hasArg("mode")) { // check if value parameter is present
    currentMode = server.arg("mode").toInt(); // update variable value
    #ifdef DEBUG
    Serial.println("Change mode to: "+String(currentMode));
    #endif
    changeMode = true;
    for (int i = 0; i < NUMPIXELS; i++) {
      PIXELS[i].R = 0;
      PIXELS[i].G = 0;
      PIXELS[i].B = 0;
    }
    #ifdef DEBUG
      Serial.println("PIXELS array is cleared");
    #endif
    strip.clear();
    strip.show(); 
    #ifdef DEBUG
      Serial.println("LED strip cleared");
    #endif

    switch(currentMode){
      case 0: //off
        mainSettings.putUShort("currentMode", 0);
        #ifdef DEBUG
          Serial.println("Updated currentMode value: "+ String(mainSettings.getUShort("currentMode")));
          Serial.println("--- handleSetValue END - set to 0 - OFF ---");
        #endif
        break;
      case 1: //galaxy
        mainSettings.putUShort("currentMode", 1);
        for(int i = 0; i < galaxyLedWorkers; i++){
          galaxyCurrDelay[i] = i*random(0, 50);
          //Serial.println("set delay[" + String(i) + "]=" + String(currDelay[i]));
          setLedColor(int(random(0, NUMPIXELS)), int(random(0, 255 + 1)), int(random(0, 255 + 1)), int(random(0, 255 + 1)));
        }
        #ifdef DEBUG
          Serial.println("--- handleSetValue END - set to 1 - GALAXY ---");
        #endif
        break;
      case 2: // selective
        mainSettings.putUShort("currentMode", 2);
        #ifdef DEBUG
          Serial.println("--- handleSetValue END - set to 2 - SELECTIVE ---");
        #endif
        break;
      case 3: //static
        staticSetColor();
        mainSettings.putUShort("currentMode", 3);
        #ifdef DEBUG
          Serial.println("--- handleSetValue END - set to 3 - STATIC ---");
        #endif
        break;
      case 4: //rainbow
        rainbowChange.Rch = random(0, rainbowMaxChange+1);
        rainbowChange.R = random(0, 255);
        #ifdef DEBUG
          Serial.println("Current changes:");
          Serial.println("R: " + rainbowChange.Rch);
        #endif
        rainbowChange.Gch = random(1, rainbowMaxChange+1);
        rainbowChange.G = random(0, 255);
        #ifdef DEBUG
          Serial.println("G: "+ rainbowChange.Gch);
        #endif
        rainbowChange.Bch = random(1, rainbowMaxChange+1);
        rainbowChange.B = random(0, 255);
        #ifdef DEBUG
          Serial.println("B: "+ rainbowChange.Bch);
        #endif
        //all count up
        rainbowChange.Ru = true;
        rainbowChange.Gu = true;
        rainbowChange.Bu = true;
        #ifdef DEBUG
          Serial.println("Rainbow mode initialized");
        #endif

        rainbowMode();
        mainSettings.putUShort("currentMode", 4);
        #ifdef DEBUG
          Serial.println("--- handleSetValue END - set to 4 - RAINBOW ---");
        #endif
        break;
      default:
        break;
    }
    server.send(200, "text/html", "Mode changed <br><br><a href='./'> Get back to main page</a>" + String(autoBack)); // send plain text response with new variable value
  }
}

void handleClear() {
  strip.clear();
  strip.show();
  server.send(200, "text/html", "Cleared<br><br><a href='./'> Get back to main page</a>" + String(autoBack)); // send plain text response with new variable value
}
