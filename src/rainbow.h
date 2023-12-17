

#ifndef RAINBOW_H
#define RAINBOW_H

extern Preferences rainbowParams; //params for rainbow mode

extern uint16_t rainbowMasterDelay;
extern uint16_t rainbowMaxChange;
struct colorChange{
  uint8_t Rch; //change
  bool Ru; //R up
  uint8_t R; //current color
  uint8_t Gch; //change
  bool Gu; // G up
  uint8_t G; //current color
  uint8_t Bch; //change
  bool Bu; // B up
  uint8_t B; //current color
};
extern colorChange rainbowChange; // store current random color change for rainbow mode

extern uint16_t rainbowCurrMasterDelay;

void rainbowInitialize();

void rainbowMode();


#endif