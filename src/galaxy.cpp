#include "all_modes.h"

#include "json.h"
#include "settings_things.h"
#include "network_things.h"
#include "pixels.h"
#include "website.h"

const char* galaxy_prefs_file = "/galaxy.json"; 

// ToDo - może przenieść divy do ogólnego htmla
String galaxy_html() { 
  return  
      "<div class='row justify-content-center'>"
        "<div class='col-auto text-center'>"
          + get_num_row_with_save_button("Master delay:", "master_del", 0, galaxy_master_delay, "bt-masterDel", "galaxySetVal(this)", "Set") +
          get_num_row_with_save_button("Minimum delay:", "min_del", 0, galaxy_min_del, "bt-minDel", "galaxySetVal(this)", "Set") +
          get_num_row_with_save_button("Maximum delay:", "max_del", 0, galaxy_max_del, "bt-maxDel", "galaxySetVal(this)", "Set") +
          get_num_row_with_save_button("LED workers:", "led_workers", 0, galaxy_led_workers, "bt-workers", "galaxySetVal(this)", "Set") +
          get_centered_button("bt-save", "galaxySave()", "SAVE") + 
        "</div>"
      "</div>"
      "<script>"
        "function galaxySetVal(clickedElement) {"
          "switch (clickedElement.id) {"
            "case 'bt-masterDel':"
              "var paramId = 'master_del';"
              "break;"
            "case 'bt-minDel':"
              "var paramId = 'min_del';"
              "break;"
            "case 'bt-maxDel':"
              "var paramId = 'max_del';"
              "break;"
            "case 'bt-workers':"
              "var paramId = 'led_workers';"
              "break;"
            "default:"
              "var paramId = '';"
          "}"
          "var newval = parseInt(document.getElementById(paramId).value);"
          "fetch('/galaxy/set?' + paramId + '=' + newval, { method: 'GET' }).then(response => response.text());"
        "}"
        "function galaxySave() {"
          "fetch('/galaxy/save', { method: 'GET' }).then(response => response.text());"
        "}"
      "</script>";
}

uint16_t    galaxy_master_delay;
uint16_t    galaxy_min_del;
uint16_t    galaxy_max_del;
uint16_t    galaxy_led_workers        = 1;

uint16_t    galaxy_curr_master_delay  = 0;
uint16_t*   galaxy_curr_delay         = new uint16_t[galaxy_led_workers];

uint16_t    galaxy_dim_minus          = 1;

void galaxy_init_defaults() {
  #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "]");
  #endif    /* defined(DEBUG) */
  galaxy_master_delay           = 30;
  mode_json_doc["master_del"]   = galaxy_master_delay;

  galaxy_min_del                = 50;
  mode_json_doc["min_del"]      = galaxy_min_del;

  galaxy_max_del                = 2000;
  mode_json_doc["max_del"]      = galaxy_max_del;

  galaxy_led_workers            = 600;
  mode_json_doc["led_workers"]  = galaxy_led_workers;
}

void galaxy_handle_set() {
  if (current_mode == MODE_GALAXY) {
    String paramName = server.argName(0); // Get the name of the parameter
    String paramValue = server.arg(0); // Get the value of the parameter
    if(paramName == "master_del"){
      galaxy_master_delay = paramValue.toInt(); 
      mode_json_doc["master_del"] = galaxy_master_delay;
      server.send(200, "text/plain", "OK");
    }
    else if(paramName == "min_del"){
      galaxy_min_del = paramValue.toInt(); 
      mode_json_doc["min_del"] = galaxy_min_del;
      server.send(200, "text/plain", "OK");
    }
    else if(paramName == "max_del"){
      galaxy_max_del = paramValue.toInt(); 
      mode_json_doc["max_del"] = galaxy_max_del;
      server.send(200, "text/plain", "OK");
    }
    else if(paramName == "led_workers"){
      int newsize = paramValue.toInt(); 
      galaxy_curr_delay = resize_array(galaxy_curr_delay, galaxy_led_workers,newsize);
      galaxy_led_workers = newsize;
      mode_json_doc["led_workers"] = galaxy_led_workers;
      server.send(200, "text/plain", "OK");
    }
  }
}

void galaxy_init_endpoints() {

  server.on("/galaxy/set", galaxy_handle_set);

  server.on("/galaxy/save", [](){
    if (current_mode == MODE_GALAXY) {
      json_save_prefs_file(galaxy_prefs_file);
      server.send(200, "text/plain", "saved");
    }
  });

  #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "] init galaxy get endpoint");
  
    server.on("/galaxy/get", HTTP_GET, [](){
      if (current_mode == MODE_GALAXY) {
        String paramName = server.arg("v");
        if(paramName == "master_del") {
          server.send(200, "text/plain", String(galaxy_master_delay));
        }
        else if(paramName == "min_del"){
          server.send(200, "text/plain", String(galaxy_min_del));
        }
        else if(paramName == "max_del"){
          server.send(200, "text/plain", String(galaxy_max_del));
        }
        else if(paramName == "led_workers"){
          server.send(200, "text/plain", String(galaxy_led_workers));
        }
      }
    });
  #endif    /* defined(DEBUG) */

  #if defined(DEBUG)
    Serial.println("[" + String(__func__) + "] endpoints init done");
  #endif    /* defined(DEBUG) */
}

void galaxy_start() {
  mode_json_doc.clear();
  json_load_prefs_file(galaxy_prefs_file);

  galaxy_master_delay = json_load_uint16(mode_json_doc, "master_del");
  galaxy_min_del      = json_load_uint16(mode_json_doc, "min_del");
  galaxy_max_del      = json_load_uint16(mode_json_doc, "max_del");
  galaxy_led_workers  = json_load_uint16(mode_json_doc, "led_workers");
  galaxy_curr_delay   = resize_array(galaxy_curr_delay,1,galaxy_led_workers);

}

void galaxy_mode() {
  if (galaxy_curr_master_delay == 0) {
    for (int current_pixel = 0; current_pixel < num_of_pixels; ++current_pixel) {
      if (pixels[current_pixel].red >= galaxy_dim_minus) {
        pixels[current_pixel].red = pixels[current_pixel].red - galaxy_dim_minus;
      } else {
        pixels[current_pixel].red = 0;
      }    
      
      if (pixels[current_pixel].green >= galaxy_dim_minus) {
        pixels[current_pixel].green = pixels[current_pixel].green - galaxy_dim_minus;
      } else {
        pixels[current_pixel].green = 0;
      }

      if (pixels[current_pixel].blue >= galaxy_dim_minus) {
        pixels[current_pixel].blue = pixels[current_pixel].blue - galaxy_dim_minus;
      } else {
        pixels[current_pixel].blue = 0;
      }

      strip.setPixelColor(current_pixel, strip.Color(pixels[current_pixel].green, pixels[current_pixel].red, pixels[current_pixel].blue));
    }

    for (int current_worker = 0; current_worker < galaxy_led_workers; ++current_worker) {
      --galaxy_curr_delay[current_worker];
      if (galaxy_curr_delay[current_worker] <= 0) {
        galaxy_curr_delay[current_worker] = random(galaxy_min_del, galaxy_max_del + 1);
        set_pixel_color(uint16_t(random(0, num_of_pixels)), uint8_t(random(0, 255 + 1)), uint8_t(random(0, 255 + 1)), uint8_t(random(0, 255 + 1)));
      }
    }

    strip.show();
    galaxy_curr_master_delay = galaxy_master_delay;
  } else {
    --galaxy_curr_master_delay;
  }

}