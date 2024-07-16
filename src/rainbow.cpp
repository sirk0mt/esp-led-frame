
#include "settings_things.h"
#include "network_things.h"
#include "json.h"
#include "all_modes.h"
#include "pixels.h"

const char* rainbow_prefs_file = "/rainbow.json";

uint16_t        rainbow_master_delay;
uint16_t        rainbow_max_change;

color_ch_struct rainbow_change;

uint16_t        rainbow_curr_master_delay;

String rainbow_html() {
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
}

void rainbow_init_defaults() {
  rainbow_master_delay          = 10;
  mode_json_doc["master_del"]   = rainbow_master_delay;

  rainbow_max_change            = 10;
  mode_json_doc["max_change"]      = rainbow_max_change;
}

void rainbow_init_endpoints() {
  server.on("/rainbowSet", HTTP_POST, [](){
    if (current_mode == MODE_RAINBOW) {
      String paramName = server.argName(0); // Get the name of the parameter
      String paramValue = server.arg(0); // Get the value of the parameter
      if(paramName == "masterDel"){
        rainbow_master_delay = paramValue.toInt(); 
        mode_json_doc["master_del"] = rainbow_master_delay;
        server.send(200, "text/plain", "OK");
      }
      else if(paramName == "maxChange"){
        rainbow_max_change = paramValue.toInt(); 
        mode_json_doc["max_change"] = rainbow_max_change;
        server.send(200, "text/plain", "OK");
      }
    }
    else {
      server.send(200, "text/plain", "You're not in rainbow mode!");
    }
  });
    server.on("/rainbowGet", HTTP_GET, [](){
      if (current_mode == MODE_RAINBOW) {
        String paramName = server.arg("v");
        if(paramName == "masterDel") {
          server.send(200, "text/plain", String(rainbow_master_delay));
        }
        else if(paramName == "maxChange"){
          server.send(200, "text/plain", String(rainbow_max_change));
        }
      }
      else {
        server.send(200, "text/plain", "You're not in rainbow mode!");
      }
  });
}

void rainbow_start() {
  rainbow_change.red_change     = random(0, rainbow_max_change + 1);
  rainbow_change.red_current    = random(0, 255);

  rainbow_change.green_change   = random(1, rainbow_max_change + 1);
  rainbow_change.green_current  = random(0, 255);

  rainbow_change.blue_change    = random(1, rainbow_max_change + 1);
  rainbow_change.blue_current   = random(0, 255);

  /* all count up */
  rainbow_change.red_up   = true;
  rainbow_change.green_up = true;
  rainbow_change.blue_up  = true;

  #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "] Current changes -" +
                                              " R: " + rainbow_change.red_change +
                                              " G: " + rainbow_change.green_change +
                                              " B: " + rainbow_change.blue_change);
  #endif    /* defined(DEBUG) */
}

void rainbow_stop() {
  
}

void rainbow_mode() {
  if (rainbow_curr_master_delay == 0) {
    if (rainbow_change.red_up) {
        if (255 < (rainbow_change.red_current + rainbow_change.red_change)) {
          rainbow_change.red_current  = 255;
          rainbow_change.red_change   = random(1, rainbow_max_change);
          rainbow_change.red_up       = false;

          #if defined(DEBUG)
            Serial.println("[" + String(__func__) + "] R:255  next change: " + rainbow_change.red_change);
          #endif    /* defined(DEBUG) */

        } else {
          rainbow_change.red_current  = rainbow_change.red_current + rainbow_change.red_change;
        }
    } else {
        if (rainbow_change.red_current < rainbow_change.red_change) {
          rainbow_change.red_current  = 0;
          rainbow_change.red_change   = random(1, rainbow_max_change + 1);
          rainbow_change.red_up       = true;

          #if defined(DEBUG)
            Serial.println("[" + String(__func__) + "] R:0  next change: " + rainbow_change.red_change);
          #endif    /* defined(DEBUG) */

        } else {
          rainbow_change.red_current  = rainbow_change.red_current - rainbow_change.red_change;
        }
    }

    if(rainbow_change.green_up){
        if (255 < (rainbow_change.green_current + rainbow_change.green_change)) {
          rainbow_change.green_current  = 255;
          rainbow_change.green_change   = random(1, rainbow_max_change);
          rainbow_change.green_up       = false;

          #if defined(DEBUG)
            Serial.println("[" + String(__func__) + "] G:255  next change: " + rainbow_change.green_change);
          #endif    /* defined(DEBUG) */

        } else {
          rainbow_change.green_current  = rainbow_change.green_current + rainbow_change.green_change;
        }
    } else {
        if (rainbow_change.green_current < rainbow_change.green_change) {
          rainbow_change.green_current  = 0;
          rainbow_change.green_change   = random(1, rainbow_max_change+1);
          rainbow_change.green_up       = true;

          #if defined(DEBUG)
            Serial.println("[" + String(__func__) + "] G:0  next change: " + rainbow_change.green_change);
          #endif    /* defined(DEBUG) */

        } else {
          rainbow_change.green_current  = rainbow_change.green_current - rainbow_change.green_change;
        }
    }

    if (rainbow_change.blue_up) {
      if (255 < (rainbow_change.blue_current + rainbow_change.blue_change)) {
        rainbow_change.blue_current   = 255;
        rainbow_change.blue_change    = random(1, rainbow_max_change);
        rainbow_change.blue_up        = false;

        #if defined(DEBUG)
          Serial.println("[" + String(__func__) + "] B:255  next change: " + rainbow_change.blue_change);
        #endif    /* defined(DEBUG) */

      } else {
        rainbow_change.blue_current   = rainbow_change.blue_current + rainbow_change.blue_change;
      }
    } else {
      if (rainbow_change.blue_current < rainbow_change.blue_change) {
        rainbow_change.blue_current   = 0;
        rainbow_change.blue_change    = random(1, rainbow_max_change+1);
        rainbow_change.blue_up        = true;

        #if defined(DEBUG)
          Serial.println("[" + String(__func__) + "] B:0  next change: " + rainbow_change.blue_change);
        #endif    /* defined(DEBUG) */

      } else {
        rainbow_change.blue_current   = rainbow_change.blue_current - rainbow_change.blue_change;
      }
    }

    for (uint16_t i = 0; i < num_of_pixels; ++i) {
      pixels[i].red   = rainbow_change.red_current;
      pixels[i].green = rainbow_change.green_current;
      pixels[i].blue  = rainbow_change.blue_current;

      strip.setPixelColor(i, strip.Color(pixels[i].green, pixels[i].red, pixels[i].blue));
    }

    strip.show();
    rainbow_curr_master_delay = rainbow_master_delay;
  } else {
    --rainbow_curr_master_delay;
  }
}