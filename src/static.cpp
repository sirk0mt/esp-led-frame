#include "settings_things.h"
#include "static.h"

Preferences staticParams; //params for static color mode

color currentStatic; //store last static color
boolean staticColorChanged = false;


void staticSetColor() {
  
  for (uint16_t i=0; i < NUMPIXELS;i++){
    PIXELS[i].R = currentStatic.R;
    PIXELS[i].G = currentStatic.G;
    PIXELS[i].B = currentStatic.B;
    strip.setPixelColor(i, strip.Color(PIXELS[i].G, PIXELS[i].R, PIXELS[i].B));
  }
  strip.show();

  //if (currentMode == 3){ // if current mode is static, save color to memory
    if (staticColorChanged){
      staticParams.putUShort("R", ushort(currentStatic.R));
      staticParams.putUShort("G", ushort(currentStatic.G));
      staticParams.putUShort("B", ushort(currentStatic.B));
      staticColorChanged = false;
      #ifdef DEBUG
        Serial.println("Static color saved to memory");
      #endif
    } 
  //}
  
}