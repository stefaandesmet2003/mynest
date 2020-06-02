#include "Arduino.h"
#include "keypad.h"
#include "ui.h"
#include "heatcontrol.h"
#include "myntp.h"
#include "TimeLib.h" // voor de functies day(), month(), year() etc

// display stuff
#include "SSD1306Wire.h"
//#include "SSD1306Brzo.h" // deze lib is sneller, maar werkt blijkbaar niet altijd met deze sketch (soms wordt het display niet geschreven)
#include "OLEDDisplayUi.h"
#include "Wire.h"
#include "NessieFonts.h"
#include "NessieImages.h"

#define I2C_DISPLAY_ADDRESS 0x3c
#define SDA_PIN             D2 //SDS
#define SDC_PIN             D1 //SDS

// weather underground
//#include "weather.h"

// user interface
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
// TODO : integrate wifi setup in UI
#include <DNSServer.h>
#include <ESP8266WebServer.h>

// Initialize the oled display for address 0x3c
SSD1306Wire     display(I2C_DISPLAY_ADDRESS, SDA_PIN, SDC_PIN);
//SSD1306Brzo     display(I2C_DISPLAY_ADDRESS, SDA_PIN, SDC_PIN);
OLEDDisplayUi   ui( &display );

//declaring prototypes
void drawProgress(OLEDDisplay *display, int percentage, String label);
void drawOtaProgress(unsigned int, unsigned int);

//void drawCurrentWeather(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
//void drawForecast(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawHome(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
void drawSetHeating(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y);
//void drawForecastDetails(OLEDDisplay *display, int x, int y, int dayIndex);

static bool isWifiQualityOK();
static void floatSplit(float fval,int &intPart, int &decPart);

// Add frames
// this array keeps function pointers to all frames
// frames are the single views that slide from right to left
/*
typedef enum {FRAME_HOME,FRAME_SETHEATING, FRAME_WEATHERCURRENT, FRAME_WEATHERFORECAST} frameIds_t;
FrameCallback frames[] = { drawHome, drawSetHeating, drawCurrentWeather, drawForecast };
int numberOfFrames = 4;
typedef enum { HOME_LOCKED, HOME_UNLOCKED, SETHEATING, WEATHER_CURRENT, WEATHER_FORECAST } uiState_t;
*/
typedef enum {FRAME_HOME,FRAME_SETHEATING } frameIds_t;
FrameCallback frames[] = { drawHome, drawSetHeating};
int numberOfFrames = 2;
typedef enum { HOME_LOCKED, HOME_UNLOCKED, SETHEATING} uiState_t;

uiState_t uiState = HOME_LOCKED;

#define DEFAULT_DISPLAY_ONTIME 20000 // 20s auto switch-off tegen inbranden
#define DEFAULT_KEYPAD_INACTIVE_TIME 10000 // after 10s auto-return to home menu
uint32_t lastKeyMillis;

uint32_t updateMillis;
int16_t nextUpdateTimeout=0;
bool uiUpdate = true;

void ui_setup()
{
  // initialize display
  display.init();
  display.flipScreenVertically();
  display.clear();

  display.setFont(Lato_Heavy_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(64, 11, "Starting up ..");
  display.setContrast(255);
  display.display(); // bootscreen

  // initialize ui
  ui.setTargetFPS(30);
  
  //ui.setActiveSymbol(activeSymbol);
  //ui.setInactiveSymbol(inactiveSymbol);
  ui.disableAllIndicators(); // no display space available for these

  // You can change the transition that is used
  // SLIDE_LEFT, SLIDE_RIGHT, SLIDE_TOP, SLIDE_DOWN
  ui.setFrameAnimation(SLIDE_LEFT);
  ui.setFrames(frames, numberOfFrames);

  ui.disableAutoTransition();
  
  lastKeyMillis = millis();
  uiState = HOME_LOCKED;
  
} // ui_setup

// loopfunction for display update
void ui_loop()
{
  // for testing
  byte b;

  if(Serial.available())
  {
    b = Serial.read();
    switch(b)
    {
      case 'l' :
        notifyKeyEvent(KEY_1);
        break;
      case 'm' :
        notifyKeyEvent(KEY_2);
        break;
      case 'r' :
        notifyKeyEvent(KEY_3);
        break;
      case 's' :
        notifyKeyEvent(SWIPE_RIGHT);
        break;
      case 'z' :
        notifyKeyEvent(SWIPE_LEFT);
        break;
      }
  }
  
  // auto-home
  if (((millis() - lastKeyMillis) > DEFAULT_KEYPAD_INACTIVE_TIME) && (uiState != HOME_LOCKED)) {
    uiState = HOME_LOCKED;
    ui.transitionToFrame(FRAME_HOME);
    uiUpdate = true;
  }
  
  // auto display off
  if ((uiUpdate) && (millis()-lastKeyMillis > DEFAULT_DISPLAY_ONTIME)) {
      
    display.clear();
    display.display();
    //display.displayOff(); // ? wat doet dit??
    uiState = HOME_LOCKED;
    // stop display updates @30fps
    uiUpdate = false;
    return;
  }
  if (uiUpdate) {
    nextUpdateTimeout = (int16_t) ui.update();
    // test : avoid 30fps updates on a fixed frame
    if (ui.getUiState()->frameState == FIXED) {
      nextUpdateTimeout = 500; // 2 fps when not in transition
    }
    //if (nextUpdateTimeout < 0) Serial.println(nextUpdateTimeout); // running out of time
    // the next call to ui.update should only happen after remainingTimeBudget ms
    
    updateMillis = millis();
    uiUpdate = false;
  }
  else {
    if ((nextUpdateTimeout < 0) || (millis() - updateMillis) > (uint32_t) nextUpdateTimeout) {
      uiUpdate = true;
    }
  }
} // ui_loop

// keypad handler
void notifyKeyEvent(uint8_t keyEvent )
{
  lastKeyMillis = millis(); // reset the inactivity counter bij elke key event

  if (keyEvent == SWIPE_RIGHT) ui.setFrameAnimation(SLIDE_RIGHT);
  else if(keyEvent == SWIPE_LEFT) ui.setFrameAnimation(SLIDE_LEFT);
  uiUpdate = true; // force UI update after a key event
  
  switch (uiState)
  {
    case HOME_LOCKED :
      if ((keyEvent == SWIPE_RIGHT) || (keyEvent == SWIPE_LEFT)){
        uiState = HOME_UNLOCKED;
      }
      break;
    case HOME_UNLOCKED :
      if ((keyEvent == SWIPE_RIGHT) || (keyEvent == SWIPE_LEFT)){
        uiState = SETHEATING;
        ui.transitionToFrame(FRAME_SETHEATING);
      }
      else if (keyEvent == KEY_1) { // L --> targetSetpoint -0.1°C
        hc_SetTargetSetpoint(hc_GetTargetSetpoint()-0.1);
      }
      else if (keyEvent == KEY_2) { // M  --> targetSetpoint +0.1°C
        hc_SetTargetSetpoint(hc_GetTargetSetpoint()+0.1);
      }
      else if (keyEvent == (KEY_1 | KEY_2 | KEY_3)) {
        uiState = HOME_LOCKED;
        ui.transitionToFrame(FRAME_HOME);
      }
      break;
    case SETHEATING :
      uint8_t heatPercentage = hc_GetHeatPercentage();
      if ((keyEvent == SWIPE_RIGHT) || (keyEvent == SWIPE_LEFT)){
        uiState = HOME_UNLOCKED;
        ui.transitionToFrame(FRAME_HOME);
      }
      else if ((keyEvent == KEY_3) || (keyEvent == (KEY_3 | KEY_2))) { // mode wijzigen
        uint8_t thermostatMode = hc_GetThermostatMode();
        thermostatMode = (thermostatMode + 1) % 4; // magic number : 4 thermostatModes at the moment
        hc_SetThermostatMode(thermostatMode);
      }
      else if (keyEvent == KEY_1) { // L --> minus
        if (heatPercentage >= 5)
          hc_SetHeatPercentage(heatPercentage-5);
      }
      else if (keyEvent == KEY_2) { // M  --> plus
        if (heatPercentage <= 95)
          hc_SetHeatPercentage(heatPercentage+5);
      }
      else if (keyEvent == (KEY_1 | KEY_2 | KEY_3)) {
        uiState = HOME_LOCKED;
        ui.transitionToFrame(FRAME_HOME);
      }
      break;
  }
  ui.setAutoTransitionForwards(); // lib verandert dit achter de rug bij transitionFrame
  
} // notifyKeyEvent

void drawHome(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y)
{
  int textWidth;
  display->setTextAlignment(TEXT_ALIGN_LEFT);
  display->setFont(Lato_Heavy_10);

  // date time
  tmElements_t tm;
  breakTime(ntp_GetDateTime(), tm);
  char line[25];
  snprintf(line,25,"%02d.%02d.%d  %02d:%02d:%02d",tm.Day,tm.Month,tm.Year+1970,tm.Hour,tm.Minute,tm.Second);
  display->drawString(0 + x, 0 + y, line);

  // wifi icon
  if (isWifiQualityOK())
    display->drawXbm(128-wifi_signal_normal_width+x, 0+y, wifi_signal_normal_width, wifi_signal_normal_height, wifi_signal_normal_bits);
  else
    display->drawXbm(128-wifi_signal_low_width+x, 0+y, wifi_signal_low_width, wifi_signal_low_height, wifi_signal_low_bits);

  // current temperature & humidity
  display->drawXbm(0+x, 15+y, thermometer_width, thermometer_height, thermometer_bits);
  float indoorTemp, indoorHum;
  int intPart,decPart;
  indoorTemp = hc_GetIndoorTemp();
  indoorHum = hc_GetIndoorHum();
  floatSplit (indoorTemp, intPart, decPart);
  display->setFont(Lato_Heavy_28);
  display->drawString(thermometer_width + x, 15 + y, String(intPart));
  textWidth = display->getStringWidth(String(intPart));
  display->setFont(ArialMT_Plain_16_compact);
  display->drawString(thermometer_width+textWidth + x, 12 + y, String(decPart));

  if (uiState == HOME_LOCKED) {
    // show if indoor temperature increasing / decreasing
    if (hc_GetIndoorTempTrend() == TEMP_TREND_INCREASING) {
      // indoor temperature is increasing
      display->drawXbm(60+x, 15+28-1-2*arrow_up_sharp_height+y, arrow_up_sharp_width, arrow_up_sharp_height, arrow_up_sharp_bits);
    }
    else if (hc_GetIndoorTempTrend() == TEMP_TREND_DECREASING) {
      // indoor temperature is decreasing
      display->drawXbm(60+x, 15+28-arrow_down_sharp_height+y, arrow_down_sharp_width, arrow_down_sharp_height, arrow_down_sharp_bits);
    }
    // show current humidity
    display->drawXbm(80+x, 44-humidity_height+y, humidity_width, humidity_height, humidity_bits);
    display->setFont(ArialMT_Plain_16_compact);
    floatSplit (indoorHum, intPart, decPart);
    display->drawString(80+humidity_width+3+ x, 24 + y, String(intPart)+"%");

    // show locked symbol
    display->drawXbm(128-locked_width+x, 64-locked_height+y, locked_width, locked_height, locked_bits);
  }
  else if (uiState == HOME_UNLOCKED) {
    // show target temperature setting
    floatSplit (hc_GetTargetSetpoint(), intPart, decPart);
    snprintf(line,25,">> %02d.%1d°C",intPart,decPart);
    display->drawString(60+x, 24+y, line);

    // up/down arrows for heating control (targetSetpoint)
    display->drawXbm(0+x, 64-arrow_down_height+y, arrow_down_width, arrow_down_height, arrow_down_bits);
    display->drawXbm(60+x, 64-arrow_up_height+y, arrow_up_width, arrow_up_height, arrow_up_bits);
      
    // show unlocked symbol
    display->drawXbm(128-unlocked_width+x, 64-unlocked_height+y, unlocked_width, unlocked_height, unlocked_bits);
  }   
} // drawHome

void drawSetHeating(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y)
{
  char line[20];
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(ArialMT_Plain_16_compact);
  float indoorTemp;
  int intPart,decPart;
  indoorTemp = hc_GetIndoorTemp();
  floatSplit (indoorTemp, intPart, decPart);
  snprintf(line,20,"%02d.%1d°C",intPart,decPart);
  display->drawString(64 + x, 0 + y, line);
  
  // thermostatMode icon
  uint8_t thermostatMode = hc_GetThermostatMode();
  switch (thermostatMode) {
    case MODE_DAY : 
      display->drawXbm(128-sun_width+x, 64-sun_height+y, sun_width, sun_height, sun_bits);
      break;
    case MODE_NIGHT : 
      display->drawXbm(128-moon_width+x, 64-moon_height+y, moon_width, moon_height, moon_bits);    
      break;
    case MODE_CLOCK : 
      display->drawXbm(128-clock_width+x, 64-clock_height+y, clock_width, clock_height, clock_bits);
      break;
    case MODE_FROST : 
      display->drawXbm(128-snowflake_width+x, 64-snowflake_height+y, snowflake_width, snowflake_height, snowflake_bits);
      break;
  }

  // up/down arrows for heating control
  display->drawXbm(0+x, 64-arrow_down_height+y, arrow_down_width, arrow_down_height, arrow_down_bits);
  display->drawXbm(60+x, 64-arrow_up_height+y, arrow_up_width, arrow_up_height, arrow_up_bits);
  
  // heat setting
  display->setFont(ArialMT_Plain_16_compact);
  uint8_t heatPercentage = hc_GetHeatPercentage (); 
  String heatPercentageStr = String(heatPercentage) + "%";
  display->drawString(40 + x, 30 + y, heatPercentageStr);
  
  display->setTextAlignment(TEXT_ALIGN_LEFT);
} // drawSetHeating

void drawProgress(OLEDDisplay *display, int percentage, String label) {
  display->clear();
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(Lato_Heavy_10);
  display->drawString(64, 10, label);
  display->drawProgressBar(2, 28, 124, 10, percentage);
  display->display();
}

void ui_drawOtaProgress(unsigned int progress, unsigned int total) {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(Lato_Heavy_10);
  display.drawString(64, 10, "MyNest Update");
  display.drawProgressBar(2, 28, 124, 10, progress / (total / 100));
  display.display();
}

// voor de temperatuur & humidity
static void floatSplit(float fval,int &intPart, int &decPart)
{
  intPart = (int) fval;
  decPart = (int)((fval - intPart)*10.0);
}

static bool isWifiQualityOK()
{
  // -50 is 100%, -100 is 0%
  int32_t dbm = WiFi.RSSI();
  if(dbm <= -75) {
      return false;
  } else 
      return true;
}






