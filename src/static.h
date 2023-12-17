

#ifndef STATIC_H
#define STATIC_H

extern Preferences staticParams; //params for static color mode

struct color{
  uint8_t R;
  uint8_t G;
  uint8_t B;
};

extern color currentStatic; //store last static color
extern boolean staticColorChanged;

void staticSetColor();


#endif