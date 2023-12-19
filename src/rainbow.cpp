#include "settings_things.h"
#include "rainbow.h"

Preferences     rainbow_params;

uint16_t        rainbow_master_delay;
uint16_t        rainbow_max_change;

color_ch_struct rainbow_change;

uint16_t        rainbow_curr_master_delay;

void rainbow_initialize() {
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


void rainbow_mode() {
  if (rainbow_curr_master_delay == 0) {
    // #if defined(DEBUG)
    //   Serial.println();
    //   Serial.print("R: ");
    //   if(rainbow_change.red_up){
    //     Serial.print("up");
    //   }
    //   else{
    //     Serial.print("down");
    //   }
    //   Serial.print("  G: ");
    //   if(rainbow_change.green_up){
    //     Serial.print("up");
    //   }
    //   else{
    //     Serial.print("down");
    //   }
    //   Serial.print("  B: ");
    //   if(rainbow_change.blue_up){
    //     Serial.print("up");
    //   }
    //   else{
    //     Serial.print("down");
    //   }
    //   Serial.println();
    // #endif    /* defined(DEBUG) */

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
    // #if defined(DEBUG)
    // Serial.println();
    // Serial.print("current delay");
    // Serial.print(rainbow_curr_master_delay);
    // Serial.println();
    // #endif    /* defined(DEBUG) */
  }
}