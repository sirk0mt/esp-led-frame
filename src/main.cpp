#include <constAndDef.h>
#include <functs.h>

uint8_t mode = 0;
bool changeMode = false;

uint16_t ajaxCurrPixel = 0;
uint8_t ajaxCurrR = 0;
uint8_t ajaxCurrG = 0;
uint8_t ajaxCurrB = 0;

int galaxyMasterDelay = 10;
int galaxyCurrMasterDelay = 0;
int galaxyDelayMin = 50;
int galaxyDelayMax = 500;
int galaxyLedWorkers = 30;
int* galaxyCurrDelay = new int[galaxyLedWorkers];

int galaxyDimMinus = 1;


pixels_struct* PIXELS = new pixels_struct[1];

void resizePixelStruct(int newSize) {
    // create a new array with the new size
    pixels_struct* newArray = new pixels_struct[newSize];
    // delete the old array
    delete[] PIXELS;
    // update the global pointer and size variables
    PIXELS = newArray;
}

int* resizeArray(int* oldArray, int oldSize, int newSize) {
  int* newArray = new int[newSize]; // Allocate a new array with the new size
  int minSize = (oldSize < newSize) ? oldSize : newSize; // Get the smaller size
  
  // Copy the elements from the old array to the new array
  for (int i = 0; i < minSize; i++) {
    newArray[i] = oldArray[i];
  }
  
  delete[] oldArray; // Free the memory of the old array
  return newArray; // Return the new array
}


String getModeSettings(uint8_t mode){
  switch (mode) {
  case 0:
    return "Off";
    break;
  case 1:
    return "Current mode: Galaxy<br>"
            "settings for galaxy"
            "<br><br>"
            "Master delay: <input type='number' id='masterDel' min='0' step='1' value='" + String(galaxyMasterDelay) + "' style='display: inline-block;'> <input type='button' onclick='setMasterDel()' value='set'> <br>"
            "Minimum delay: <input type='number' id='minDel' min='0' step='1' value='" + String(galaxyDelayMin) + "' style='display: inline-block;'> <input type='button' onclick='setMinDel()' value='set'> <br>"
            "Maximum delay: <input type='number' id='maxDel' min='0' step='1' value='" + String(galaxyDelayMax) + "' style='display: inline-block;'> <input type='button' onclick='setMaxDel()' value='set'> <br>"
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
  default:
    return "Unknown";
    break;
  }
} 

 void sendPixelColor(uint16_t led){
  pixels.setPixelColor(led, pixels.Color(PIXELS[led].G, PIXELS[led].R, PIXELS[led].B));
  pixels.show();
}

void setLedColor(uint16_t led, uint8_t R, uint8_t G, uint8_t B){
        PIXELS[led].R = R;
        PIXELS[led].G = G;
        PIXELS[led].B = B;
        pixels.setPixelColor(led, pixels.Color(PIXELS[led].G, PIXELS[led].R, PIXELS[led].B));
        //Serial.println("set [" + String(led) + "] to -> R: " + String(PIXELS[led].R) + " G: " + String(PIXELS[led].G) + " B: " + String(PIXELS[led].B));
}

void handleSetValue() {
  if (server.hasArg("mode")) { // check if value parameter is present
    mode = server.arg("mode").toInt(); // update variable value
    changeMode = true;
    for (int i = 0; i < NUMPIXELS; i++) {
      PIXELS[i].R = 0;
      PIXELS[i].G = 0;
      PIXELS[i].B = 0;
    }
    pixels.clear();
    pixels.show();
    switch(mode){
      case 0: //off
        break;
      case 1: //galaxy
        for(int i = 0; i < galaxyLedWorkers; i++){
          galaxyCurrDelay[i] = i*random(0, 50);
          //Serial.println("set delay[" + String(i) + "]=" + String(currDelay[i]));
          setLedColor(int(random(0, NUMPIXELS)), int(random(0, 255 + 1)), int(random(0, 255 + 1)), int(random(0, 255 + 1)));
        }
        break;
      case 2: // selective
        break;
      default:
        break;
    }
    server.send(200, "text/html", "Mode changed <br><br><a href='./'> Get back to main page</a>" + String(autoBack)); // send plain text response with new variable value
  }
}


void galaxyMode(){
  if(galaxyCurrMasterDelay == 0){
    for (int i = 0; i < NUMPIXELS; i++) {
      //Serial.println("-------- [" + String(i) + "] ---------");
      if(PIXELS[i].R >= galaxyDimMinus){
        PIXELS[i].R = PIXELS[i].R - galaxyDimMinus;
      }
      else{
        PIXELS[i].R = 0;
      }    
      
      if(PIXELS[i].G >= galaxyDimMinus){
        PIXELS[i].G = PIXELS[i].G - galaxyDimMinus;
      }
      else{
        PIXELS[i].G = 0;
      }

      if(PIXELS[i].B >= galaxyDimMinus){
        PIXELS[i].B = PIXELS[i].B - galaxyDimMinus;
      }
      else{
        PIXELS[i].B = 0;
      }
      //Serial.println("dimmer to -> R: " + String(PIXELS[i].R) + " G: " + String(PIXELS[i].G) + " B: " + String(PIXELS[i].B));
      pixels.setPixelColor(i, pixels.Color(PIXELS[i].G, PIXELS[i].R, PIXELS[i].B));
    }

    for(int i = 0; i < galaxyLedWorkers; i++){
      galaxyCurrDelay[i]--;
      if(galaxyCurrDelay[i] <= 0){
        galaxyCurrDelay[i] = random(galaxyDelayMin, galaxyDelayMax + 1);
        //Serial.println("new delay[" + String(i) + "]=" + String(currDelay[i]));
        setLedColor(uint16_t(random(0, NUMPIXELS)), uint8_t(random(0, 255 + 1)), uint8_t(random(0, 255 + 1)), uint8_t(random(0, 255 + 1)));
      }
    }
    
    //Serial.println("-------- WAIT " + String(currDelay) + " iterations ---------");

    pixels.show();
    galaxyCurrMasterDelay = galaxyMasterDelay;
  }
  else{
    galaxyCurrMasterDelay--;
  }

}

void handleClear(){
  pixels.clear();
  pixels.show();
   server.send(200, "text/html", "Cleared<br><br><a href='./'> Get back to main page</a>" + String(autoBack)); // send plain text response with new variable value
  
}


/*
 * setup function
 */
void setup(void) {
  Serial.begin(115200);

  resizePixelStruct(NUMPIXELS);

  // Connect to WiFi network
  WiFi.begin(ssid, password);
  Serial.println("");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  /*use mdns for host name resolution*/
  if (!MDNS.begin(host)) { //http://obraz.local
    Serial.println("Error setting up MDNS responder!");
    while (1) {
      delay(1000);
    }
  }
  Serial.println("mDNS responder started");
  /*return index page which is stored in serverIndex */
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    //v0.0.0 - v(pelna wersja).(wstepnie gotowa).(skonczny dzien modow)
    server.send(200, "text/html", "<center><font size=16>"
            "<b>Inteligentny obraz<br>v0.0.3</b>"
            "<br>"
            "<a href='/setvalue?mode=0'>Off</a><br>"
            "<a href='/setvalue?mode=1'>Galaxy mode</a><br>"
            "<a href='/setvalue?mode=2'>Selective mode</a><br>"
            "<br>"
            + String(getModeSettings(mode)) +
            "<br>"
            "<a href='/clear'>Clear strip</a><br>"
            "<br>"
            "<a href='/updateIndex'>Aktualizacja</a>"
    "</font><br><br><br>"
    "Kompilacja " + String(__DATE__) + " " + String(__TIME__) +
    "</center>");
  });
  server.on("/clear", handleClear); // clear strip
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
      server.send(200, "text/plain", "OK");
    }
    else if(paramName == "minDel"){
      galaxyDelayMin= paramValue.toInt(); 
      server.send(200, "text/plain", "OK");
    }
    else if(paramName == "maxDel"){
      galaxyDelayMax = paramValue.toInt(); 
      server.send(200, "text/plain", "OK");
    }
    else if(paramName == "workers"){
      int newsize = paramValue.toInt(); 
      galaxyCurrDelay = resizeArray(galaxyCurrDelay,galaxyLedWorkers,newsize);
      galaxyLedWorkers = newsize;
      server.send(200, "text/plain", "OK");
    }
  });
    server.on("/galaxyGet", HTTP_GET, [](){
      String paramName = server.arg("v");
      if(paramName == "masterDel") {
        server.send(200, "text/plain", String(galaxyMasterDelay));
      }
      else if(paramName == "minDel"){
        server.send(200, "text/plain", String(galaxyDelayMin));
      }
      else if(paramName == "maxDel"){
        server.send(200, "text/plain", String(galaxyDelayMax));
      }
      else if(paramName == "workers"){
        server.send(200, "text/plain", String(galaxyLedWorkers));
      }
  });
  // GALAXY MODE END
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
      Serial.printf("Update: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      /* flashing firmware to ESP*/
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });
  server.begin();

  pixels.begin(); // INITIALIZE NeoPixel strip object (REQUIRED)
  randomSeed(analogRead(0));
  pixels.clear();
  pixels.show();

}

void loop(void) {
  server.handleClient();

  switch(mode){
    case 0:
    if(changeMode == true){
      // do something
      changeMode = false;
    }
      break;
    case 1:
      galaxyMode();
      break;
    default:
      break;
  }
  delay(1);
}


