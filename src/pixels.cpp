#include <Arduino.h>
#include "settings_things.h"
#include "pixels.h"

pixels_struct* PIXELS = new pixels_struct[1]; //initialize array of pixel color structs

bool stripInitialize() {
  strip.updateLength(NUMPIXELS);
  #ifdef DEBUG
    Serial.println("Initialize LED strip with " + String(NUMPIXELS) + " pixels");
  #endif
  strip.begin(); // INITIALIZE NeoPixel strip object (REQUIRED)
  strip.clear();
  strip.show();
  return true;
}

bool resizePixelStruct(uint16_t newSize) {
  int newSize1 = 150;
  #ifdef DEBUG
    Serial.println("--- resizePixelStruct("+String(newSize1)+") START---");
    Serial.println("Change PIXEL array size from " + String(sizeof(PIXELS)));
  #endif

  // create a new array with the new size
  pixels_struct* newArray = new pixels_struct[newSize1];
  #ifdef DEBUG
    Serial.println("Generated new array with size: "+ String(sizeof(PIXELS)));
  #endif
  // delete the old array
  delete[] PIXELS;
  // update the global pointer and size variables
  PIXELS = newArray;

  #ifdef DEBUG
    Serial.println("Success changed to new size: "+ String(sizeof(PIXELS)));
    Serial.println("--- resizePixelStruct END---");
  #endif
  return true;
}

uint16_t* resizeArray(uint16_t* oldArray, uint16_t oldSize, uint16_t newSize) {
  #ifdef DEBUG
    Serial.println("--- resizeArray(oldArray, oldSize:"+String(oldSize)+", newSize: "+String(newSize)+") START---");
  #endif
  uint16_t* newArray = new uint16_t[newSize]; // Allocate a new array with the new size
  uint16_t minSize = (oldSize < newSize) ? oldSize : newSize; // Get the smaller size
  
  // Copy the elements from the old array to the new array
  for (int i = 0; i < minSize; i++) {
    newArray[i] = oldArray[i];
  }
  
  delete[] oldArray; // Free the memory of the old array
  return newArray; // Return the new array
}

void sendPixelColor(uint16_t led) {
  #ifdef DEBUG
    Serial.println("--- sendPixelColor("+String(led)+" START---");
  #endif
  strip.setPixelColor(led, strip.Color(PIXELS[led].G, PIXELS[led].R, PIXELS[led].B));
  strip.show();
}

void setLedColor(uint16_t led, uint8_t R, uint8_t G, uint8_t B) {
  #ifdef DEBUG
    Serial.println("--- setLedColor( LED: "+String(led)+" R: "+String(R)+" G: "+String(G)+" B: "+String(B)+") START---");
  #endif
  PIXELS[led].R = R;
  PIXELS[led].G = G;
  PIXELS[led].B = B;
  strip.setPixelColor(led, strip.Color(PIXELS[led].G, PIXELS[led].R, PIXELS[led].B));
  //Serial.println("set [" + String(led) + "] to -> R: " + String(PIXELS[led].R) + " G: " + String(PIXELS[led].G) + " B: " + String(PIXELS[led].B));
}

bool showTriangle() {
  for (int c = 0; c <= pixelsInRow; c++) {
    for (int r = 0; r <= pixelsRows-c-1; r++){
      strip.setPixelColor(r * pixelsInRow + c, strip.Color(0, 255, 0));
    }
  }
  strip.show();
  return true;
}