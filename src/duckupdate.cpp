// updates the duckdns ddns record, because skynet modem looses the setting all the time. AArgh
// for the update to work, no need to find your own external IP, duckdns does this automatically when accessedµ
// the find your own IP call is only done to trigger update to duckdns

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

#include "config.h"

#define ONE_HOUR    3600000UL
#define ONE_DAY     24*ONE_HOUR
#define DUCK_UPDATE_INTERVAL ONE_DAY

WiFiClient client;

// duckdns auto-update
String new_ip;
String old_ip = "0.0.0.0";

uint32_t duckUpdateMillis = -DUCK_UPDATE_INTERVAL; // force check at startup

// check duckDNS every day (DUCK_UPDATE_INTERVAL)
void duck_loop (void) {

  if ((millis() - duckUpdateMillis) > DUCK_UPDATE_INTERVAL) {

    duckUpdateMillis = millis(); // we'll come back in 24h

    if (WiFi.status() != WL_CONNECTED) {
      //can't do much without wifi connection
      Serial.println("OOPS, no Wifi connection!");
      return;
    }

    if (duck_token == "" || duck_domain == "") {
      // can't do much without valid duck config parameters
      Serial.println("ERROR: duckdns config incorrect");
      Serial.println("domain=" + duck_domain);
      Serial.println("token=" + duck_token);
      return;
    }

    HTTPClient http;
    // ######## GET PUBLIC IP ######## //
    http.begin(client, "http://ipv4bot.whatismyipaddress.com/");
    int httpCode = http.GET();
    if (httpCode > 0) {
      if(httpCode == HTTP_CODE_OK) {
        new_ip = http.getString();
        Serial.printf ("current IP = %s \n", new_ip.c_str());
        // TODO : misschien hier nog checken op valid ip ?
      }
    }
    else {
      Serial.printf("http get failed"); // TODO uitwerken!
      http.end();
      return;
    }
    http.end();
  
    // ######## CHECK & UPDATE ######### //
    if(old_ip != new_ip){
  
      String duck_url = "http://www.duckdns.org/update?domains="+ duck_domain + "&token=" + duck_token;
      Serial.printf ("old IP was = %s \n", old_ip.c_str());
      Serial.println("updating duckDNS now : " + duck_url);
      if (http.begin(client, duck_url)) 
      {    
        // start connection and send HTTP header
        int httpCode = http.GET();
    
        // httpCode will be negative on error
        if (httpCode > 0) {
          // HTTP header has been send and Server response header has been handled
          if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
            String payload = http.getString();
            Serial.printf("http response : %s \n", payload.c_str());
            // TODO : if playload.c_str() != "OK" -> retry, want dan is de update KO
            old_ip = new_ip;
            Serial.println("duck update OK");
          }
        } 
        else {
          Serial.printf("duck update KO, error: %s\n", http.errorToString(httpCode).c_str());
        }
    
        http.end();
      } 
      else 
      {
        Serial.printf("duckdns KO : unable to connect\n");
      }
    }
    else {
      Serial.println("IP unchanged, no update needed");
    }
  }

  else {
    // too early, nothing to do
  }
  
} // duck_loop
