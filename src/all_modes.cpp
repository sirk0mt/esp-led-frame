
#include "all_modes.h"
#include "settings_things.h"

String get_curr_mode_name() {
  switch (current_mode) {
    case MODE_OFF:
      return "Off";
      break;
    case MODE_GALAXY:
      return "Galaxy";
      break;
    case MODE_PIXELS:
      return "Selective";
      break;
    case MODE_STATIC:
      return "Static";
      break;
    case MODE_RAINBOW_FLOW:
      return "Color flow";
      break;
    case MODE_RAINBOW:
      return "Rainbow";
      break;
    default:
      return "Undefined";
      break;
  }
}

void init_all_modes_endpoints() {
    galaxy_init_endpoints();
    
}