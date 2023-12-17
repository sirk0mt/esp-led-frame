

#ifndef GALAXY_H
#define GALAXY_H

extern Preferences galaxyParams; //parameters of galaxy mode

extern uint16_t galaxyMasterDelay;
extern uint16_t galaxyMinDel;
extern uint16_t galaxyMaxDel;
extern uint16_t galaxyLedWorkers;

extern uint16_t galaxyCurrMasterDelay;
extern uint16_t* galaxyCurrDelay;

extern uint16_t galaxyDimMinus;

void galaxyMode();

#endif