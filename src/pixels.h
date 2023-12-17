

#ifndef PIXELS_H
#define PIXELS_H

struct pixels_struct{ //create structure of pixel colors value
  uint8_t R;
  uint8_t G;
  uint8_t B;
};

extern pixels_struct* PIXELS;

bool stripInitialize();

bool resizePixelStruct(uint16_t newSize);

uint16_t* resizeArray(uint16_t* oldArray, uint16_t oldSize, uint16_t newSize);

void sendPixelColor(uint16_t led);

void setLedColor(uint16_t led, uint8_t R, uint8_t G, uint8_t B);

bool showTriangle();

#endif