#include "settings_things.h"
#include "galaxy.h"


uint16_t    galaxy_master_delay;
uint16_t    galaxy_min_del;
uint16_t    galaxy_max_del;
uint16_t    galaxy_led_workers        = 1;

uint16_t    galaxy_curr_master_delay  = 0;
uint16_t*   galaxy_curr_delay         = new uint16_t[galaxy_led_workers];

uint16_t    galaxy_dim_minus          = 1;

void galaxy_init_defaults() {
  galaxy_master_delay           = 30;
  mode_json_doc["master_del"]   = galaxy_master_delay;

  galaxy_min_del                = 50;
  mode_json_doc["min_del"]      = galaxy_min_del;

  galaxy_max_del                = 2000;
  mode_json_doc["max_del"]      = galaxy_max_del;

  galaxy_led_workers            = 600;
  mode_json_doc["led_workers"]  = galaxy_led_workers;
}

void galaxy_init_endpoints() {
  server.on("/galaxySet", HTTP_POST, [](){
    if (current_mode == MODE_GALAXY) {
      String paramName = server.argName(0); // Get the name of the parameter
      String paramValue = server.arg(0); // Get the value of the parameter
      if(paramName == "masterDel"){
        galaxy_master_delay = paramValue.toInt(); 
        mode_json_doc["master_del"] = galaxy_master_delay;
        server.send(200, "text/plain", "OK");
      }
      else if(paramName == "minDel"){
        galaxy_min_del = paramValue.toInt(); 
        mode_json_doc["min_del"] = galaxy_min_del;
        server.send(200, "text/plain", "OK");
      }
      else if(paramName == "maxDel"){
        galaxy_max_del = paramValue.toInt(); 
        mode_json_doc["max_del"] = galaxy_max_del;
        server.send(200, "text/plain", "OK");
      }
      else if(paramName == "workers"){
        int newsize = paramValue.toInt(); 
        galaxy_curr_delay = resize_array(galaxy_curr_delay, galaxy_led_workers,newsize);
        galaxy_led_workers = newsize;
        mode_json_doc["led_workers"] = galaxy_led_workers;
        server.send(200, "text/plain", "OK");
      }
    }
    else {
      server.send(200, "text/plain", "You're not in galaxy mode!");
    }
  });
    server.on("/galaxyGet", HTTP_GET, [](){
      if (current_mode == MODE_GALAXY) {
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
      }
      else {
        server.send(200, "text/plain", "You're not in galaxy mode!");
      }
  });
}

void galaxy_start() {
  // malloc dla każdej zmiennej
}

void galaxy_stop() {
  // free dla każdej zmiennej
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