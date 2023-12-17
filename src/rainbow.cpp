#include "settings_things.h"
#include "rainbow.h"

Preferences rainbowParams; //params for rainbow mode

uint16_t rainbowMasterDelay;
uint16_t rainbowMaxChange;

colorChange rainbowChange; // store current random color change for rainbow mode

uint16_t rainbowCurrMasterDelay;

void rainbowInitialize() {
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
}


void rainbowMode() {
  if(rainbowCurrMasterDelay == 0){
    // #ifdef DEBUG
    //   Serial.println();
    //   Serial.print("R: ");
    //   if(rainbowChange.Ru){
    //     Serial.print("up");
    //   }
    //   else{
    //     Serial.print("down");
    //   }
    //   Serial.print("  G: ");
    //   if(rainbowChange.Gu){
    //     Serial.print("up");
    //   }
    //   else{
    //     Serial.print("down");
    //   }
    //   Serial.print("  B: ");
    //   if(rainbowChange.Bu){
    //     Serial.print("up");
    //   }
    //   else{
    //     Serial.print("down");
    //   }
    //   Serial.println();
    // #endif

    if(rainbowChange.Ru){
        if(255<(rainbowChange.R + rainbowChange.Rch)){
          #ifdef DEBUG
            Serial.println();
            Serial.print("Rainbow - R:255  next change: ");
            Serial.print(rainbowChange.Rch);
          #endif
          rainbowChange.R = 255;
          rainbowChange.Rch = random(1, rainbowMaxChange);
          rainbowChange.Ru = false;
        }
        else{
          rainbowChange.R = rainbowChange.R + rainbowChange.Rch;
        }
    }
    else{
        if(rainbowChange.R<rainbowChange.Rch){
          rainbowChange.R = 0;
          rainbowChange.Rch = random(1, rainbowMaxChange+1);
          rainbowChange.Ru = true;
          #ifdef DEBUG
            Serial.println();
            Serial.print("Rainbow - R:0  next change: ");
            Serial.print(rainbowChange.Rch);
          #endif
        }
        else{
          rainbowChange.R = rainbowChange.R - rainbowChange.Rch;
        }
    }


    if(rainbowChange.Gu){
        if(255<(rainbowChange.G + rainbowChange.Gch)){
          #ifdef DEBUG
            Serial.println();
            Serial.print("Rainbow - G:255  next change: ");
            Serial.print(rainbowChange.Gch);
          #endif
          rainbowChange.G = 255;
          rainbowChange.Gch = random(1, rainbowMaxChange);
          rainbowChange.Gu = false;
        }
        else{
          rainbowChange.G = rainbowChange.G + rainbowChange.Gch;
        }
    }
    else{
        if(rainbowChange.G<rainbowChange.Gch){
          rainbowChange.G = 0;
          rainbowChange.Gch = random(1, rainbowMaxChange+1);
          rainbowChange.Gu = true;
          #ifdef DEBUG
            Serial.println();
            Serial.print("Rainbow - G:0  next change: ");
            Serial.print(rainbowChange.Gch);
          #endif
        }
        else{
          rainbowChange.G = rainbowChange.G - rainbowChange.Gch;
        }
    }

        if(rainbowChange.Bu){
        if(255<(rainbowChange.B + rainbowChange.Bch)){
          #ifdef DEBUG
            Serial.println();
            Serial.print("Rainbow - B:255  next change: ");
            Serial.print(rainbowChange.Bch);
          #endif
          rainbowChange.B = 255;
          rainbowChange.Bch = random(1, rainbowMaxChange);
          rainbowChange.Bu = false;
        }
        else{
          rainbowChange.B = rainbowChange.B + rainbowChange.Bch;
        }
    }
    else{
        if(rainbowChange.B<rainbowChange.Bch){
          rainbowChange.B = 0;
          rainbowChange.Bch = random(1, rainbowMaxChange+1);
          rainbowChange.Bu = true;
          #ifdef DEBUG
            Serial.println();
            Serial.print("Rainbow - B:0  next change: ");
            Serial.print(rainbowChange.Bch);
          #endif
        }
        else{
          rainbowChange.B = rainbowChange.B - rainbowChange.Bch;
        }
    }


    for (uint16_t i=0; i < NUMPIXELS;i++){
      PIXELS[i].R = rainbowChange.R;
      PIXELS[i].G = rainbowChange.G;
      PIXELS[i].B = rainbowChange.B;

      strip.setPixelColor(i, strip.Color(PIXELS[i].G, PIXELS[i].R, PIXELS[i].B));
    }
    strip.show();
    rainbowCurrMasterDelay = rainbowMasterDelay;
  }
  else{
    rainbowCurrMasterDelay--;
    // #ifdef DEBUG
    // Serial.println();
    // Serial.print("current delay");
    // Serial.print(rainbowCurrMasterDelay);
    // Serial.println();
    // #endif
  }
}