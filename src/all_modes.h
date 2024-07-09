#ifndef ALL_MODES_H
/* If first use this header, define all things */
#define ALL_MODES_H

#include <Arduino.h>

#define MODE_OFF 0
#define MODE_GALAXY 1
#include "galaxy.h"

#define MODE_STATIC 2
#include "static.h"

#define MODE_RAINBOW 3
#include "rainbow.h"

#define MODE_PIXELS 4

#define MODE_RAINBOW_FLOW 5
#include "rainbow_flow.h"


String get_curr_mode_name();

void init_all_modes_endpoints();

#endif      /* ifndef ALL_MODES_H */