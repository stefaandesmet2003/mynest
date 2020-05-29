#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h> // handelt de /update url af

#include <FS.h>

ESP8266WebServer server(80);            // create a web server on port 80
ESP8266HTTPUpdateServer httpUpdater;
File fsUploadFile;                      // a File variable to temporarily store the received file
ESP8266WiFiMulti wifiMulti;             // Create an instance of the ESP8266WiFiMulti class, called 'wifiMulti'
                                       
String ota_name, ota_pass;
// Domain name for the mDNS responder & used as hostname;
// make this different from OTAName otherwise http://mdnsName/ doesn't work
String mdns_name; 
String www_user, www_pass;                                        

// add Wi-Fi networks you want to connect to
String wifi_ssid, wifi_pass;

// duckdns auto-update
extern String duck_domain;
extern String duck_token;

extern bool isLogging;
extern uint8_t heat_default_day, heat_default_night, heat_default_fastheat;
bool rebootRequest = false;
uint32_t rebootMillis;

/**************************************************************/
/*   NESSIE APP STUFF */
/**************************************************************/
#include "myntp.h"
#include "keypad.h"
#include "heatcontrol.h"
#include "ui.h" // display stuff
#include "duckupdate.h"

extern void handleNTPRequest();
extern void handleNestRequest();

/**************************************************************/
/*   HELPER FUNCTIONS                                         */
/**************************************************************/

String formatBytes(size_t bytes) { // convert sizes in bytes to KB and MB
  if (bytes < 1024) {
    return String(bytes) + "B";
  } else if (bytes < (1024 * 1024)) {
    return String(bytes / 1024.0) + "KB";
  }
  else
    return (String(bytes / 1024.0 / 1024.0) + "MB");

} // formatBytes

String getContentType(String filename){ // determine the filetype of a given filename, based on the extension
  if(server.hasArg("download")) return "application/octet-stream";
  else if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
} // getContentType

/**************************************************************/
/*   SERVER HANDLERS                                          */
/**************************************************************/

bool handleFileRead(String path){
  if ((0 != strcmp(path.c_str(),"/privacy.html")) && (!server.authenticate((const char*) www_user.c_str(), (const char*) www_pass.c_str()))) {
    server.requestAuthentication(); 
    return false;
  }
  // authentication OK --> continue
  
  Serial.println("handleFileRead: " + path);
  if(path.endsWith("/")) path += "index.html";
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)){
    if(SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
} // handleFileRead

void handleFileUpload() {
  if(!server.authenticate((const char*) www_user.c_str(), (const char*) www_pass.c_str()))
    return server.requestAuthentication();  
  // authentication OK --> continue
  
  if(server.uri() != "/edit") return;
  HTTPUpload& upload = server.upload();
  if(upload.status == UPLOAD_FILE_START){
    String filename = upload.filename;
    if(!filename.startsWith("/")) filename = "/"+filename;
    Serial.print("handleFileUpload Name: "); Serial.println(filename);
    fsUploadFile = SPIFFS.open(filename, "w");
    filename = String();
  } else if(upload.status == UPLOAD_FILE_WRITE) {
    //Serial.print("handleFileUpload Data: "); Serial.println(upload.currentSize);
    if(fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize);
  } else if(upload.status == UPLOAD_FILE_END) {
      if(fsUploadFile)
        fsUploadFile.close();
      Serial.print("handleFileUpload Size: "); Serial.println(upload.totalSize);
  }
} // handleFileUpload

void handleFileDelete() {
  if(!server.authenticate((const char*) www_user.c_str(), (const char*) www_pass.c_str()))
    return server.requestAuthentication();  
  // authentication OK --> continue
  
  if(server.args() == 0) return server.send(500, "text/plain", "BAD ARGS");
  String path = server.arg(0);
  Serial.println("handleFileDelete: " + path);
  if(path == "/")
    return server.send(500, "text/plain", "BAD PATH");
  if(!SPIFFS.exists(path))
    return server.send(404, "text/plain", "FileNotFound");
  SPIFFS.remove(path);
  server.send(200, "text/plain", "");
  path = String();
} // handleFileDelete

void handleFileCreate(){
  if(!server.authenticate((const char*) www_user.c_str(), (const char*) www_pass.c_str()))
    return server.requestAuthentication();  
  // authentication OK --> continue
  
  if(server.args() == 0)
    return server.send(500, "text/plain", "BAD ARGS");
  String path = server.arg(0);
  Serial.println("handleFileCreate: " + path);
  if(path == "/")
    return server.send(500, "text/plain", "BAD PATH");
  if(SPIFFS.exists(path))
    return server.send(500, "text/plain", "FILE EXISTS");
  File file = SPIFFS.open(path, "w");
  if(file)
    file.close();
  else
    return server.send(500, "text/plain", "CREATE FAILED");
  server.send(200, "text/plain", "");
  path = String();
} // handleFileCreate

// this is linked to the ace.js code in data/edit.html
void handleFileList() {
  if(!server.authenticate((const char*) www_user.c_str(), (const char*) www_pass.c_str()))
    return server.requestAuthentication();  
  // authentication OK --> continue

  if(!server.hasArg("dir")) {server.send(500, "text/plain", "BAD ARGS"); return;}
  
  String path = server.arg("dir");
  Serial.println("handleFileList: " + path);
  Dir dir = SPIFFS.openDir(path);
  path = String();

  String output = "[";
  while(dir.next()){
    File entry = dir.openFile("r");
    if (output != "[") output += ',';
    bool isDir = false;
    output += "{\"type\":\"";
    output += (isDir)?"dir":"file";
    output += "\",\"name\":\"";
    output += String(entry.name()).substring(1);
    output += "\"}";
    entry.close();
  }
  output += "]";
  server.send(200, "text/json", output);
} // handleFileList

void handleNotFound() { 
  if(!server.authenticate((const char*) www_user.c_str(), (const char*) www_pass.c_str()))
    return server.requestAuthentication();  
  // authentication OK --> continue

  // check if the file exists in the flash memory (SPIFFS), if so, send it
  // if the requested file or page doesn't exist, return a 404 not found error
  if (!handleFileRead(server.uri())) {        
    server.send(404, "text/plain", "404: File Not Found");
  }
} // handleNotFound

// return 0 = OK, -1 = ERROR!
int initConfig() {
  File configFile;
  String line;
  String key, val;
  int sepAt;  
  configFile = SPIFFS.open("/config.txt","r");

  if (!configFile) {
    Serial.println("ERROR - config.txt missing");
    return (-1);
  }
  while (1) {
    line = configFile.readStringUntil('\n');
    if (line == "") {
      break;
    }

    sepAt = line.indexOf(':');
    if (sepAt == -1) continue;

    key = line.substring(0, sepAt);
    val = line.substring(sepAt + 1);
    Serial.println(key + " = " + val);

    if (key == "wifi_ssid") {
      wifi_ssid = val;
    }
    else if (key == "wifi_pass") {
      wifi_pass = val;
    }
    else if (key == "ota_name") {
      ota_name = val;
    }
    else if (key == "ota_pass") {
      ota_pass = val;
    }
    else if (key == "www_user") {
      www_user = val;
    }
    else if (key == "www_pass") {
      www_pass = val;
    }
    else if (key == "mdns_name") {
      mdns_name = val;
    }
    else if (key == "duck_domain") {
      duck_domain = val;
    }
    else if (key == "duck_token") {
      duck_token = val;
    }
    else if (key == "logging") {
      isLogging = (val.toInt()!=0)?true:false;
    }
    else if (key == "heat_day") {
      heat_default_day = (uint8_t) val.toInt();
    }
    else if (key == "heat_night") {
      heat_default_night = (uint8_t) val.toInt();
    }
    else if (key == "heat_fastheat") {
      heat_default_fastheat = (uint8_t) val.toInt();
    }

/*
heat_day:70
heat_night:0
heat_frost:0
heat_fastheat:80
*/    
  }
  return 0;
} // initConfig

/*****************************************************************************************************************************
  WIFI STUFF 
******************************************************************************************************************************/
void startWiFi() {
  if (WiFi.getMode() != WIFI_STA)
  {
      WiFi.mode(WIFI_STA); // we want this permanently in flash
  }
  WiFi.hostname(mdns_name); // --> http://mdnsName/ (als de router mee wil) met mdns ook : http://mdnsName.local/ (na startMDNS)
  WiFi.persistent(false);
  // aangezien de WiFi.persistent lijn later is toegevoegd staan de credentials hieronder al in de flash config
  // geen probleem; zelfs met lege flash config zorgt code hieronder voor een connectie

  wifiMulti.addAP((const char*) wifi_ssid.c_str(),(const char*) wifi_pass.c_str());
  //wifiMulti.addAP(ssid2, wifipwd2);

  Serial.println("Connecting");
  while (wifiMulti.run() != WL_CONNECTED) {  // Wait for the Wi-Fi to connect
      delay(250);
      Serial.print('.');
  }
  Serial.println("\r\n");
  Serial.print("Connected to ");
  Serial.println(WiFi.SSID());             // Tell us what network we're connected to
  Serial.print("IP address:\t");
  Serial.print(WiFi.localIP());            // Send the IP address of the ESP8266 to the computer
  Serial.println("\r\n");  
      
  // WiFiManager alternative from weatherStationDemo, to consider!
  // configModeCallback links with UI
  /*
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
  // Uncomment for testing wifi manager
  //wifiManager.resetSettings();
  wifiManager.setAPCallback(configModeCallback);

  //or use this for auto generated name ESP + ChipID
  wifiManager.autoConnect();
  */  

} // startWiFi

void startOTA() { // Start the OTA service
  ArduinoOTA.setHostname((const char*) ota_name.c_str());
  ArduinoOTA.setPassword((const char*) ota_pass.c_str());
  
  ArduinoOTA.onProgress(ui_drawOtaProgress);
  // no need to route other OTA callbacks to UI
  
  ArduinoOTA.onStart([]() {
    Serial.println("OTA Start");
    rebootRequest = false;
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("OTA End");
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
} // startOTA

// Start the SPIFFS and list all contents
void startSPIFFS() {
  // Start the SPI Flash File System (SPIFFS)
  SPIFFS.begin();                             
  Serial.println("SPIFFS started. Contents:");
  {
    Dir dir = SPIFFS.openDir("/");
    // List the file system contents
    while (dir.next()) {                      
      String fileName = dir.fileName();
      size_t fileSize = dir.fileSize();
      Serial.printf("\tFS File: %s, size: %s\r\n", fileName.c_str(), formatBytes(fileSize).c_str());
    }
    Serial.printf("\n");
  }
} // startSPIFFS

// Start the mDNS responder
void startMDNS() { 
  // start the multicast domain name server
  MDNS.begin((const String) mdns_name);                        

  //stond hier niet, maar wel bij de webupdater example. Wat doet dit ????
  MDNS.addService("http", "tcp", 80);
  
  Serial.println("mDNS responder started: http://" + mdns_name + ".local");
} // startMDNS

// Start a HTTP server with a couple of services, and basic authentication protection
void startServer() { 

  httpUpdater.setup(&server);
  
  //list directory
  server.on("/list", HTTP_GET, handleFileList);
  //load editor
  server.on("/edit", HTTP_GET, [](){
    if(!server.authenticate((const char*) www_user.c_str(), (const char*) www_pass.c_str()))
      return server.requestAuthentication();  
    // authentication OK --> continue
    if(!handleFileRead("/edit.html")) server.send(404, "text/plain", "FileNotFound");
  });
  //create file
  server.on("/edit", HTTP_PUT, handleFileCreate);
  //delete file
  server.on("/edit", HTTP_DELETE, handleFileDelete);
  //first callback is called after the request has ended with all parsed arguments
  //second callback handles file uploads at that location
  server.on("/edit", HTTP_POST, [](){ server.send(200, "text/plain", ""); }, handleFileUpload);

  //get heap status, analog input value and all GPIO statuses in one json call
  // gebruikt in de index.html (zie FSBrowser example)
  server.on("/all", HTTP_GET, [](){
    if(!server.authenticate((const char*) www_user.c_str(), (const char*) www_pass.c_str()))
      return server.requestAuthentication();  
    // authentication OK --> continue
    String json = "{";
    json += "\"heap\":"+String(ESP.getFreeHeap());
    json += ", \"analog\":"+String(analogRead(A0));
    json += ", \"gpio\":"+String((uint32_t)(((GPI | GPO) & 0xFFFF) | ((GP16I & 0x01) << 16)));
    json += "}";
    server.send(200, "text/json", json);
    json = String();
  });
  
  server.on("/reboot", HTTP_GET, [](){
    if(!server.authenticate((const char*) www_user.c_str(), (const char*) www_pass.c_str()))
      return server.requestAuthentication();  
    // authentication OK --> continue
    server.send(200,"text/plain", "rebooting now!");
    // need a sync here before restarting, otherwise the response never gets sent
    //ESP.restart();
    rebootRequest = true;
    rebootMillis = millis() + 3000; // reboot delay
  });

  server.on("/nest", HTTP_GET, [](){
    if(!server.authenticate((const char*) www_user.c_str(), (const char*) www_pass.c_str()))
      return server.requestAuthentication();  
    // authentication OK --> continue
    handleNestRequest();
  });

  server.on("/ntp", HTTP_GET, [](){
    if(!server.authenticate((const char*) www_user.c_str(), (const char*) www_pass.c_str()))
      return server.requestAuthentication();  
    // authentication OK --> continue
    handleNTPRequest();
  });
  
  //called when the url is not defined here
  //use it to load content from SPIFFS
  server.onNotFound(handleNotFound);
  // and check if the file exists

  // start the HTTP server
  server.begin();                             
  Serial.println("HTTP server started.");
} // startServer

void setup() 
{
  int initCode;
  pinMode(LED_BUILTIN, OUTPUT);
  
  Serial.begin(115200);        // Start the Serial communication to send messages to the computer
  startSPIFFS();               // Start the SPIFFS and list all contents
  initCode = initConfig();
  hc_setup();
  keypad_setup();
  ui_setup();    

  if (initCode == 0) {
    startWiFi();                 // Start a Wi-Fi access point, and try to connect to some given access points. Then wait for either an AP or STA connection
    startOTA();                  // Start the OTA service
    startMDNS();                 // Start the mDNS responder
    startServer();               // Start a HTTP server with a file read handler and an upload handler
    startNTP();                  // Start listening for UDP messages to port 123
  }
  else {
    // TODO : fallback to a access point to allow manual config over wifi
  }

} // setup

void loop() 
{
  server.handleClient();  // run the server
  ArduinoOTA.handle();    // listen for OTA events
  runNTP();               // periodically update the time
  
  keypad_loop();          // scan touch keys
  ui_loop();              // display user interface
  hc_loop();              // heating control
  runDuck();              // duckDNS update

  if (rebootRequest) {
    if (millis() > rebootMillis) {
      ESP.restart();
    }
  }
 
} // loop