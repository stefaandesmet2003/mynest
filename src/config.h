#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <Arduino.h>

extern String ota_name, ota_pass;
// Domain name for the mDNS responder & used as hostname;
// make this different from OTAName otherwise http://mdnsName/ doesn't work
extern String mdns_name; 
extern String www_user, www_pass;                                        

// add Wi-Fi networks you want to connect to
extern String wifi_ssid, wifi_pass;

// duckdns auto-update
extern String duck_domain;
extern String duck_token;

//logging
extern bool log_local;
extern bool log_cloud;
extern String log_cloudurl;
extern String cloud_user, cloud_pass;

extern uint8_t heat_default_day, heat_default_night, heat_default_fastheat;


// return 0 = OK, -1 = ERROR!
int config_load(void);

#endif // _CONFIG_H_

