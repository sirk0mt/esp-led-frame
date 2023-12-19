#ifndef NETWORK_THINGS_H
/* If first use this header, define all things */
#define NETWORK_THINGS_H

#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>


extern String       config_network_ssid;        /* SSID of created configuration network */
extern IPAddress    config_network_ip;          /* IP of device in created config network */

extern String       mdns_host_name;             /* Host name for mDNS service (before .local). For egzample: host.local*/
extern String       saved_ssid;                 /* Saved network SSID */
extern String       saved_password;             /* Saved network password */

extern WebServer    server;                     /* WebServer class instace */

extern uint8_t      mdns_del;                    /* ToDo check if necessary */
extern uint8_t      mdns_del_curr;               /* ToDo check if necessary */


/**
 * \brief               Function that tries to connect to saved WiFi network.
 * \return              True if connected or False if connection failed.
*/
bool connect_saved_network();

/**
 * \brief               Function for get visible networks in HTML list.
 * \return              String with HTML list of visible WiFi networks.
*/
String list_visible_networks();

/**
 * \brief               Function to create WiFi network for configure connection to local WiFi.
*/
void create_config_network();

/**
 * \brief               Initialize mDNS server.
*/
void initialize_mdns();

#endif      /* ifndef NETWORK_THINGS_H */