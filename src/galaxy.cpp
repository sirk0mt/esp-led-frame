#include "settings_things.h"
#include "galaxy.h"

Preferences galaxyParams; //parameters of galaxy mode

uint16_t galaxyMasterDelay;
uint16_t galaxyMinDel;
uint16_t galaxyMaxDel;
uint16_t galaxyLedWorkers=1;

uint16_t galaxyCurrMasterDelay = 0;
uint16_t* galaxyCurrDelay = new uint16_t[galaxyLedWorkers];

uint16_t galaxyDimMinus = 1;


void galaxyMode() {
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
      strip.setPixelColor(i, strip.Color(PIXELS[i].G, PIXELS[i].R, PIXELS[i].B));
    }

    for(int i = 0; i < galaxyLedWorkers; i++){
      galaxyCurrDelay[i]--;
      if(galaxyCurrDelay[i] <= 0){
        galaxyCurrDelay[i] = random(galaxyMinDel, galaxyMaxDel + 1);
        //Serial.println("new delay[" + String(i) + "]=" + String(currDelay[i]));
        setLedColor(uint16_t(random(0, NUMPIXELS)), uint8_t(random(0, 255 + 1)), uint8_t(random(0, 255 + 1)), uint8_t(random(0, 255 + 1)));
      }
    }

    strip.show();
    galaxyCurrMasterDelay = galaxyMasterDelay;
  }
  else{
    galaxyCurrMasterDelay--;
  }

}