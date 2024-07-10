
#ifndef WEBSITE_H
#define WEBSITE_H

#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>

extern uint16_t     ajax_current_pixel;             /* Current pixel for ajax front end*/

extern const char*  auto_back_html;                 /* string to add in HTML to automatic redirect to previours page */

String get_num_row_with_save_button(String label, String input_id,
                  int min, int value, String bt_id, String onlick, String bt_label);

String get_centered_button(String id, String onclick, String label);

/**
 * \brief               Function that send redirect to root of device to TCP client
*/
void redirect_to_root();

/**
 * \brief               Start server with only saved network changing site
*/
void start_network_settings_server(); 

/**
 * \brief               Start main web server of all devices
*/
void start_main_server();

/**
 * \brief               Get string with HTML setting for selected mode
*/
String get_html_settings_for_mode();

/**
 * \brief               API for set values
*/
void handle_set_value();

/**
 * \brief               Function for clear whole LED strip
*/
void handle_clear_strip();

#endif      /* ifndef WEBSITE_H*/