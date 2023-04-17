#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
// #include <nvs_flash.h> //to format settings

#define PIN        2 // pin connected to LED strip

// #define DEBUG

Preferences devSettings; //device settings
Preferences mainSettings; //main program settings
Preferences galaxyParams; //parameters of galaxy mode

//v0.0.0 - v(pelna wersja).(wstepnie gotowa).(skonczny dzien modow)
const char* ver = "v0.0.5";

uint16_t pixelsInRow;
uint16_t pixelsRows; 

uint16_t NUMPIXELS;
Adafruit_NeoPixel strip(1, PIN, NEO_GRB + NEO_KHZ800); //create instance for neopixel strip

String host;
String ssid;
String password;

uint16_t currentMode;

uint16_t galaxyMasterDelay;
uint16_t galaxyMinDel;
uint16_t galaxyMaxDel;
uint16_t galaxyLedWorkers=1;

WebServer server(80);

struct pixels_struct{ //create structure of pixel colors value
  uint8_t R;
  uint8_t G;
  uint8_t B;
};

const char* autoBack = "<script>window.location.href = window.location.origin;</script>"; //string to add in HTML to automatic redirect to previours page

// STATIC SITES START

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


// STATIC SITES END