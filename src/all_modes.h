#ifndef ALL_MODES_H
/* If first use this header, define all things */
#define ALL_MODES_H

#define MODE_OFF 0

#include "galaxy.h"
#define MODE_GALAXY 1

#include "static.h"
#define MODE_STATIC 2

#include "rainbow.h"
#define MODE_RAINBOW 3

#include "pixels.h"
#define MODE_PIXELS 4

#include "rainbow_flow.h"
#define MODE_RAINBOW_FLOW 5


String get_curr_mode_name();

void init_all_modes_endpoints();

#endif      /* ifndef ALL_MODES_H */