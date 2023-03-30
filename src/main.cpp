#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <Adafruit_NeoPixel.h>
//#include <AsyncTask.h>

#define PIN        2 // On Trinket or Gemma, suggest changing this to 1
#define NUMPIXELS 150 // Popular NeoPixel ring size
Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

const char* host = "obraz";
const char* ssid = "Siedziba_PIS";
const char* password = "niepowiemci";

uint8_t mode = 0;
bool changeMode = false;

uint16_t ajaxCurrPixel = 0;
uint8_t ajaxCurrR = 0;
uint8_t ajaxCurrG = 0;
uint8_t ajaxCurrB = 0;

int galaxyMasterDelay = 100;
int galaxyCurrMasterDelay = 0;
int galaxyDelayMin = 5000;
int galaxyDelayMax = 500000;
int galaxyLedWorkers = 15;
int* galaxyCurrDelay = new int[galaxyLedWorkers];

int galaxyDimMinus = 1;

struct pixels_struct{
  uint8_t R;
  uint8_t G;
  uint8_t B;
};
pixels_struct PIXELS[NUMPIXELS];


WebServer server(80);

//AsyncTask asyncGalaxy(galaxyMode);

/*
 * update Index Page
 */

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



String getModeName(uint8_t mode){
  switch (mode) {
  case 0:
    return "Off";
    break;
  case 1:
    return "Current mode: Galaxy<br>"
            "settings for galaxy"
            "<br><br>";
    break;
  case 2:
    return "Current mode: Selective<br>"
            "settings for selective"
            "<br><br>"
            "Pixel: <input type='number' id='currPixel' min='0' max='" + String(NUMPIXELS) + "' step='1' value='0'><br><br>"
            "<input type='color' id='colorpicker' value='#333333'><br><br>"
            "<input type='button' onclick='getColor()' value='get color'> <input type='button' onclick='setColor()' value='set color'>"
            "<script>"
              "function getColor() {"
                "var rval = 0;"
                "var gval = 0;"
                "var bval = 0;"
                "fetch('/ajax?getPixel')"
                  ".then(response => response.text())"
                  ".then(data => {"
                    "document.getElementById('currPixel').value = data;"
                  "});"
                "fetch('/ajax?getCurrR')"
                  ".then(response => response.text())"
                  ".then(data => {"
                    "rval = data;"
                  "});"
                "fetch('/ajax?getCurrG')"
                  ".then(response => response.text())"
                  ".then(data => {"
                    "gval = data;"
                  "});"
                "fetch('/ajax?getCurrB')"
                  ".then(response => response.text())"
                  ".then(data => {"
                    "bval = data;"
                  "});"
                "var RGBval = '#' + ((1 << 24) + (rval << 16) + (gval << 8) + bval).toString(16).slice(1);"
                "document.getElementById('currPixel').value = RGBval;"
              "}"
              "function setColor() {"
                "const newValue = document.getElementById('currPixel').value;"
                "fetch('/ajax?setPixel=' + newValue, { method: 'POST' })"
                  ".then(response => response.text());"
                "var RGB = document.getElementById('colorpicker').value;"
                "var r = parseInt(RGB.slice(1, 3), 16);"
                "var g = parseInt(RGB.slice(3, 5), 16);"
                "var b = parseInt(RGB.slice(5, 7), 16);"
                "fetch('/ajax?setCurrR=' + r, { method: 'POST' })"
                  ".then(response => response.text());"
                "fetch('/ajax?setCurrG=' + g, { method: 'POST' })"
                  ".then(response => response.text());"
                "fetch('/ajax?setCurrB=' + b, { method: 'POST' })"
                  ".then(response => response.text());"
                "fetch('/ajax?sendPixel', { method: 'POST' })"
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
      //Serial.println("-------- [" + String(i) + "] ---------");
      PIXELS[i].R = 0;
      PIXELS[i].G = 0;
      PIXELS[i].B = 0;
    }
    //pixels.clear(); 
    switch(mode){
      case 0: //off
        pixels.clear();
        break;
      case 1: //galaxy
        for(int i = 0; i < galaxyLedWorkers; i++){
          galaxyCurrDelay[i] = i*random(0, 50);
          //Serial.println("set delay[" + String(i) + "]=" + String(currDelay[i]));
          setLedColor(int(random(0, NUMPIXELS)), int(random(0, 255 + 1)), int(random(0, 255 + 1)), int(random(0, 255 + 1)));
        }
        break;
      default:
        break;
    }
    server.send(200, "text/html", "Mode changed to: " + String(getModeName(mode)) + "<br><br><a href='./'> Get back to main page</a>"); // send plain text response with new variable value
  }
}

void handleAjax() {
  if (server.method() == HTTP_GET) {
    String paramName = server.argName(0); // Get the name of the parameter
    String paramValue = server.arg(0); // Get the value of the parameter

      if(paramName == "getPixel") {
        server.send(200, "text/plain", String(ajaxCurrPixel));
      }
      else if(paramName == "setPixel"){
        ajaxCurrPixel = paramValue.toInt(); // Convert the parameter value to an integer
        // Set the variable value dynamically
        server.send(200, "text/plain", "OK");
        }
      else if(paramName == "getCurrR"){
        server.send(200, "text/plain", String(PIXELS[ajaxCurrPixel].R));
      }
      else if(paramName == "setCurrR"){
        PIXELS[ajaxCurrPixel].R = paramValue.toInt(); // Convert the parameter value to an integer
        // Set the variable value dynamically
        server.send(200, "text/plain", "OK");
      }
      else if(paramName == "getCurrG"){
        server.send(200, "text/plain", String(PIXELS[ajaxCurrPixel].G));
      }
      else if(paramName == "setCurrG"){
        PIXELS[ajaxCurrPixel].G = paramValue.toInt(); // Convert the parameter value to an integer
        // Set the variable value dynamically
        server.send(200, "text/plain", "OK");
      }
      else if(paramName == "getCurrB"){
        server.send(200, "text/plain", String(PIXELS[ajaxCurrPixel].B));
      }
      else if(paramName == "setCurrB"){
        PIXELS[ajaxCurrPixel].B = paramValue.toInt(); // Convert the parameter value to an integer
        // Set the variable value dynamically
        server.send(200, "text/plain", "OK");
    }
    else if(paramName == "sendPixel"){
      sendPixelColor(ajaxCurrPixel);
      server.send(200, "text/plain", "OK");
    }
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



/*
 * setup function
 */
void setup(void) {
  Serial.begin(115200);

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
            "<b>Inteligentny obraz<br>v0.0.1</b>"
            "<br>"
            "<a href='/setvalue?mode=0'>Off</a><br>"
            "<a href='/setvalue?mode=1'>Galaxy mode</a><br>"
            "<a href='/setvalue?mode=2'>Selective mode</a><br>"
            "<br>"
            + String(getModeName(mode)) +
            "<br>"
            "<a href='/updateIndex'>Aktualizacja</a>"
    "</font><br><br><br>"
    "Kompilacja " + String(__DATE__) + " " + String(__TIME__) +
    "</center>");
  });
  server.on("/ajax", handleAjax);
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


}

void loop(void) {
  server.handleClient();

  switch(mode){
    case 0:
    if(changeMode == true){
      pixels.clear(); 
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


