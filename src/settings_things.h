
#ifndef SETTINGS_H
/* If first use this header, define all things */
#define SETTINGS_H

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include "network_things.h"
#include "website.h"
#include "galaxy.h"
#include "rainbow.h"
#include "static.h"
#include "pixels.h"
#include "rainbow_flow.h"

/*  
 *  Include nvs_flash.h for format all settings process
 *  #include <nvs_flash.h>
 */


/* Version variable initialized in cpp file for that header */
extern const char*              ver;

#define LED_STRIP_PIN           2               /* GPIO pin for NeoPixel strip*/
 
#define DEBUG                                   /* To enable print serial debug information define / for disable comment that line */

extern Preferences              dev_settings;   /* Device settings class instance */
extern Preferences              main_settings;  /* Main settings class instance */

/* LED settings START */
extern uint16_t                 pixels_in_row;  /* Number of pixels in row */
extern uint16_t                 pixels_rows;    /* Number of rows */

extern uint16_t                 num_of_pixels;  /* Number of all pixels */

extern Adafruit_NeoPixel        strip;          /* LED strip class instance */
/* LED settings END */

extern uint16_t                 color_order;    /* NeoPixel strip color order */

extern uint16_t                 current_mode;   /* Variable to store current mode of light */

extern bool                     change_mode;    /* Variable to handle changid mode moment. ToDo is necessary? */


/**
 * \brief               Change strip type (color order)
 * \param[col_order]    col_order: 0 - NEO_RGB, 1 - NEO_RBG, 2 - NEO_GRB, 3 - NEO_GBR, 4 - NEO_BRG, 5 - NEO_BGR
*/
void change_strip_color_order(uint16_t col_order);

/**
 * \brief               Function that restart device.
*/
void handle_restart();

/**
 * \brief               Function saving new network coordinates.
 * \param[ssid]         ssid: SSID of network to save.
 * \param[password]     password: Password of new network to save.
*/
void save_new_wifi_config(const String& ssid, const String& password);

/**
 * \brief               Function that initialize settings from memory.
*/
void initialize_settings();


/**
 * \brief               Function that initialize start of last light mode.
*/
void initialize_starting_mode();


#endif      /* ifndef SETTINGS_H */