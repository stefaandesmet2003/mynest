// TODO
/*
heatPercentage : checken dat het niet negatief wordt, want dat gaat fout met uint8_t
zomermodus : als de binnentemperatuur > 25°C -> uitschakelen
*/


#include "Arduino.h"
#include "heatcontrol.h"
#include "sigma_delta.h"
#include "myntp.h"
#include "TimeLib.h" // for the functions day(), month(), year() etc
#include <ESP8266WebServer.h>
#include <FS.h> // file functions, to use with SPIFFS object

// for the SI7021
#include <Wire.h>

#define si7021Addr                    0x40 // i2c address of the temperature/humidity sensor

#define SD_TARGET_FROST               0
#define SD_TARGET_MIN                 95 // changed 14/4/2018 because 0.78V is invalid -> weird behavior -- 75  // 0.78V measured output 
#define SD_TARGET_MAX                 215 // 1.014V measured output

// sensible defaults
#define HEAT_RANGE                    100 // heatPercentage : 0-100%
#define HEAT_DEFAULT_FROST            0  // frost protection mode in gas heater
#define HEAT_DEFAULT_DAY              70 // %, 
#define HEAT_DEFAULT_NIGHT            10 // %, 
#define HEAT_DEFAULT_FASTHEAT         80 // %, was 95

#define TEMP_SETPOINT_MIN             10.0
#define TEMP_SETPOINT_MAX             26.0
#define TEMP_SETPOINT_DEFAULT_DAY     23.5
#define TEMP_SETPOINT_DEFAULT_NIGHT   18.0
#define TEMP_SETPOINT_MAX_OFFSET      0.5 // max deviation from setpoint temperature

#define SDCONTROL_PIN                 2   // D4 = IO2
#define SENSOR_READ_INTERVAL          60000 // 1 minute
#define OUTDOOR_LOG_INTERVAL          600000 // 10 minutes - to match with the reading frequency by the weather-module
#define LOGGING_INTERVAL              300000 // 5 minutes

extern ESP8266WebServer server; // the app's webserver instance

// read the sensor data every SENSOR_READ_INTERVAL ms
uint32_t lastSensorMillis = -SENSOR_READ_INTERVAL;
uint32_t lastOutdoorDataMillis = -OUTDOOR_LOG_INTERVAL; // wunderground updates every 10 minutes
float indoorHum=0.0, indoorTemp=0.0, outdoorTemp=0.0; // actual sensor data being used

// logging
bool isLogging = false; // from config.txt, updated by main:initConfig()
uint32_t lastLogMillis = -LOGGING_INTERVAL;


#define NBR_INDOORTEMP_POINTS 16
float indoorTemps[NBR_INDOORTEMP_POINTS]; // avg over 15 SENSOR_READ_INTERVAL is used for the UI
uint8_t indoorTempsIdx = 0;
uint8_t indoorTempTrend = TEMP_TREND_STABLE;

float hc_TargetSetpoint = TEMP_SETPOINT_DEFAULT_DAY;
uint8_t hc_ThermostatMode = MODE_CLOCK;
// learning thermostat
// todo : adapt schedule to learning
// these defaults will be overwritten by config.txt
uint8_t heat_default_day = HEAT_DEFAULT_DAY;
uint8_t heat_default_night = HEAT_DEFAULT_NIGHT;
uint8_t heat_default_fastheat = HEAT_DEFAULT_FASTHEAT; 
uint8_t hc_HeatPercentageDay;
uint8_t hc_HeatPercentageNight;
uint8_t hc_HeatPercentageFastHeat;

static void readSi7021(float *rh, float *temp);
static void run7021();

float hc_GetIndoorTemp() { 
  return indoorTemp;
}
float hc_GetIndoorHum() { 
  return indoorHum;
}

void hc_SetTargetSetpoint (float targetSetpoint) {
  if ((targetSetpoint > TEMP_SETPOINT_MIN) && (targetSetpoint <= TEMP_SETPOINT_MAX)) {
    hc_TargetSetpoint = targetSetpoint;
  }
  // else don't touch the actual target
} // hc_SetTargetSetpoint

float hc_GetTargetSetpoint (void) {
  return hc_TargetSetpoint;
} // hc_GetTargetSetpoint

/*
#define TEMP_TREND_STABLE     0
#define TEMP_TREND_INCREASING 1
#define TEMP_TREND_DECREASING 2
*/
uint8_t hc_GetIndoorTempTrend() {
  return indoorTempTrend;
} // hc_GetIndoorTempTrend

void hc_setup()
{
  pinMode(SDCONTROL_PIN,OUTPUT); // gebeurt eigenlijk ook door attachPin hieronder
  sigmaDeltaEnable();
  sigmaDeltaAttachPin(SDCONTROL_PIN);
  sigmaDeltaSetPrescaler(10);

  // assume config.txt has been read by now
  // maybe better do it here for these parameters?
  hc_HeatPercentageDay = heat_default_day;
  hc_HeatPercentageNight = heat_default_night;
  hc_HeatPercentageFastHeat = heat_default_fastheat;
  Serial.println("heat setup with parameters:");
  Serial.print("% Day : "); Serial.println(hc_HeatPercentageDay);
  Serial.print("% Night : "); Serial.println(hc_HeatPercentageNight);
  Serial.print("% FastHeat 6AM : "); Serial.println(hc_HeatPercentageFastHeat);
  
  hc_SetThermostatMode(MODE_CLOCK);
  hc_SetHeatPercentage(hc_HeatPercentageDay,false);

  // i2c needed for si7021
  Wire.begin();
  
} // hc_setup

void hc_loop()
{
  uint8_t curHeatPercentage = hc_GetHeatPercentage();
  uint8_t newHeatPercentage;
  uint32_t secsInDay = ntp_GetDateTime() % 86400;
  uint32_t secs6AM = 6*3600;
  uint32_t secs8AM = 8*3600;
  uint32_t secs10PM = 22*3600;
  
  switch (hc_ThermostatMode) {
    case MODE_NIGHT :
      newHeatPercentage = hc_HeatPercentageNight;
      // switch to clock mode in the morning
      if (secsInDay == secs6AM)
        hc_ThermostatMode = MODE_CLOCK;
      break;
    case MODE_CLOCK :
      // FASTHEAT tussen 6 en 8u 's ochtends
      if ((secsInDay >= secs6AM) && (secsInDay < secs8AM))
      { 
        newHeatPercentage = hc_HeatPercentageFastHeat;
      }
      else if ((secsInDay >= secs8AM) && (secsInDay < secs10PM))
      {
        newHeatPercentage = hc_HeatPercentageDay;
      }
      else if ((secsInDay >= secs10PM) || (secsInDay < secs6AM))
      {
        newHeatPercentage = hc_HeatPercentageNight;
      }
      break;
    case MODE_DAY : 
      newHeatPercentage = hc_HeatPercentageDay;
      break;
    case MODE_FROST : 
      newHeatPercentage = HEAT_DEFAULT_FROST;
      break;
  }
  // simple control mechanism for now
  // avoid overheating when the heat setting is accidentally set too high (for too long)
  if (indoorTemp > hc_TargetSetpoint)
    newHeatPercentage = _min(heat_default_day,newHeatPercentage);
  // avoid underheating when the heat setting is accidentally set too low (for too long)
  if (indoorTemp < TEMP_SETPOINT_MIN) // 10°C
    newHeatPercentage = _max(heat_default_night,newHeatPercentage);
    
  if (newHeatPercentage != curHeatPercentage)
    hc_SetHeatPercentage(newHeatPercentage,false);
  
  run7021(); // update temp/rh/outdoor data
  
} // hc_loop

void hc_SetThermostatMode(uint8_t thermostatMode)
{
  hc_ThermostatMode = thermostatMode;
  switch (thermostatMode) {
    case MODE_DAY :
      hc_SetHeatPercentage (hc_HeatPercentageDay,false);
      break;
    case MODE_NIGHT :
      hc_SetHeatPercentage (hc_HeatPercentageNight,false);
      break;
    case MODE_CLOCK :
      // the heat setting will be updated by the loop function according to the heating schedule
      break;
    case MODE_FROST :
      // TODO! implement real thermostat function
      hc_SetHeatPercentage (HEAT_DEFAULT_FROST); // use the frost protection function of the gas heater
      break;
  }
} // hc_SetThermostatMode

uint8_t hc_GetThermostatMode(void) 
{
  return hc_ThermostatMode;
} // hc_GetThermostatMode

// heatPercentage : 0..100, manual : updates the default day/night heatPercentage setting (basic learning)
void hc_SetHeatPercentage (uint8_t heatPercentage, bool manual)
{
  // this should be better !
  // for now : copying the fixed schedule from hc_loop to determine which heatPercentage to adapt
  uint32_t secsInDay = ntp_GetDateTime() % 86400;
  uint32_t secs6AM = 6*3600;
  uint32_t secs8AM = 8*3600;
  uint32_t secs10PM = 22*3600;
  uint32_t sdTarget;
  
  if (heatPercentage > HEAT_RANGE) heatPercentage = HEAT_RANGE;
  // 14.4.2018 - make heatPercentage == 0 special mode with sdTarget = SD_TARGET_FROST (outside normal day/night target range)
  if (heatPercentage == HEAT_DEFAULT_FROST)  // 0
    sdTarget = SD_TARGET_FROST; // 0
  else
    sdTarget = SD_TARGET_MIN + (SD_TARGET_MAX - SD_TARGET_MIN)*heatPercentage / HEAT_RANGE;
  sigmaDeltaWrite (0,(uint8_t)sdTarget);
  if (manual) {
    // do the learning
    // for now : keep it simple : take the requested heatPercentage setting for the new default
    if (hc_ThermostatMode == MODE_DAY) hc_HeatPercentageDay = heatPercentage;
    else if (hc_ThermostatMode == MODE_NIGHT) hc_HeatPercentageNight = heatPercentage;
    else if (hc_ThermostatMode == MODE_CLOCK) {
      // FASTHEAT tussen 6 en 8u 's ochtends
      if ((secsInDay >= secs6AM) && (secsInDay < secs8AM))
      { 
        hc_HeatPercentageFastHeat = heatPercentage;
      }
      else if ((secsInDay >= secs8AM) && (secsInDay < secs10PM))
      {
        hc_HeatPercentageDay = heatPercentage;
      }
      else if ((secsInDay >= secs10PM) || (secsInDay < secs6AM))
      {
        hc_HeatPercentageNight = heatPercentage;
      }
    }
  }
} // hc_SetHeatPercentage

uint8_t hc_GetHeatPercentage ()
{
  uint32_t sdTarget = (uint32_t) sigmaDeltaRead ();
  uint8_t heatPercentage;
  if (sdTarget < SD_TARGET_MIN) heatPercentage = 0;
  else heatPercentage = (uint8_t)((sdTarget-SD_TARGET_MIN)*HEAT_RANGE/(SD_TARGET_MAX-SD_TARGET_MIN));
  return (heatPercentage);
  
} // hc_GetHeatPercentage

// web interface
void handleNestRequest()
{
  uint32_t heatPercentage = heat_default_day;
  float targetSetpoint = TEMP_SETPOINT_DEFAULT_DAY;
  int volume = 5;
  int i;
  String message;
  uint32_t dt = ntp_GetUnixDateTime(); // browser does local time correction
  float a0volt; // voltage at A0 pin (== control voltage VOUT to gas boiler)

  // http://mdnsname.local/nest
  if (server.args() == 0) {
    // go straight to the end
  }
  
  // http://mdnsname.local/nest?heat=85
  // http://mdnsname.local/nest?temp=23.5
  else {
    for (i=0;i<server.args();i++)
    {
      if (server.argName(i) == "heat")
      {
        heatPercentage = server.arg(i).toInt();
        if (heatPercentage > HEAT_RANGE) {
          //message = "heat setting out of range [0..100%]";
        }
        else {
          hc_SetHeatPercentage(heatPercentage);
          //message = "setting heatPercentage to " + String(heatPercentage) + "%";
        }
      }
      else if (server.argName(i) == "temp")
      {
        targetSetpoint = server.arg(i).toFloat();
        if ((targetSetpoint >= TEMP_SETPOINT_MIN) && (targetSetpoint <= TEMP_SETPOINT_MAX)) {
          hc_SetTargetSetpoint(targetSetpoint);
          //message = "setting targetSetpoint to " + String(targetSetpoint) + "oC";
        }
        else {
          //message = "temperature setting out of range [" + String(TEMP_SETPOINT_MIN) + ".." + String(TEMP_SETPOINT_MAX) + "degC]";
        }
      }
      else if (server.argName(i) == "mode") {
        if ((server.arg(i) == "auto") || (server.arg(i) == "clock")) {
          hc_SetThermostatMode(MODE_CLOCK);
        }
        else if ((server.arg(i) == "heat") || (server.arg(i) == "day")) {
          hc_SetThermostatMode(MODE_DAY);
        }
        else if ((server.arg(i) == "eco") || (server.arg(i) == "night")) {
          hc_SetThermostatMode(MODE_NIGHT);
        }
        else if ((server.arg(i) == "off") || (server.arg(i) == "frost")) {
          hc_SetThermostatMode(MODE_FROST);
        }
      }
    }
  }
  // 14.4.2018 : added A0 measurement
  // if necessary add automatic adaptation of sdTarget range in case of drift
  a0volt = analogRead(A0)*3.17 / 1023.0; // 3.17 should be 3.2 (220K/100K voltage divider), in reality 3.17 is closer to real measured voltage
  
  message = "{\"time\": " + String(dt) + 
              ",\"targetSetpoint\":" + String(hc_TargetSetpoint) +
              ",\"heatPercentage\":" + String(hc_GetHeatPercentage()) +
              ",\"indoorTemperature\":" + String(indoorTemp) +
              ",\"indoorHumidity\":" + String(indoorHum) +
              ",\"thermostatMode\":" + String(hc_ThermostatMode) +
              ",\"A0VOLT\":" + String(a0volt) +
              ",\"freeHeap\":" + String(ESP.getFreeHeap()) +
              ",\"wifiSSID\": \"" + WiFi.SSID() +
              "\",\"wifiRSSI\":" + String(WiFi.RSSI()) + "}";             
  
  Serial.println(message);
  server.send(200, "text/plain", message);
    
} // handleNestRequest

/**********************************************************************************************/
/*   LOCAL FUNCTIONS                                                                          */
/**********************************************************************************************/

static void run7021()
{
  uint32_t currentMillis = millis();
  
  if (currentMillis - lastSensorMillis > SENSOR_READ_INTERVAL) {  // Every minute, request the temperature/humidity
    float rh7021 = 0.0,temp7021 = 0.0;
    readSi7021( &rh7021,&temp7021);
    lastSensorMillis = currentMillis;
    if (temp7021 < 100.0) {
      indoorTemp = temp7021;
    }
    if (rh7021 > 0.0){ // 0.0 used as 'error reading'; could be better ..
      indoorHum = rh7021;
    }

    // update window for the temperature history (used by the UI)
    // trend? (used by the UI)
    uint8_t idx2min = (indoorTempsIdx + NBR_INDOORTEMP_POINTS - 2) % NBR_INDOORTEMP_POINTS;
    uint8_t idx5min = (indoorTempsIdx + NBR_INDOORTEMP_POINTS - 5) % NBR_INDOORTEMP_POINTS;

    // 08.2018 : added basic sanity check for the thermostat algorithm
    if (indoorTemps[idx2min] != 0.0)
    {
      // current temperature cannot deviate more than 0.5°C from temperature 2 mins ago
      indoorTemp = _max (indoorTemp, indoorTemps[idx2min]-0.5);
      indoorTemp = _min (indoorTemp, indoorTemps[idx2min]+0.5);
    }

    indoorTemps[indoorTempsIdx] = indoorTemp;

    if ((indoorTemp > indoorTemps[idx2min]) && (indoorTemp > indoorTemps[idx5min])) {
      indoorTempTrend = TEMP_TREND_INCREASING;
    }
    else if ((indoorTemp < indoorTemps[idx2min]) && (indoorTemp < indoorTemps[idx5min])) {
      indoorTempTrend = TEMP_TREND_DECREASING;
    }
    else indoorTempTrend = TEMP_TREND_STABLE;

    indoorTempsIdx++;
    if (indoorTempsIdx == NBR_INDOORTEMP_POINTS) indoorTempsIdx = 0;
  }
  
  // logging every LOGGING_INTERVAL
  if ((isLogging) && (currentMillis - lastLogMillis > LOGGING_INTERVAL)) {
    // only log to file when we have a valid NTP time
    uint32_t actualTime = ntp_GetUnixDateTime();
    lastLogMillis = currentMillis;
    if (actualTime) 
    {
      File tempLog = SPIFFS.open("/temp.csv", "a"); // Write the time and the temperature to the csv file
      tempLog.print(actualTime);
      tempLog.print(',');
      tempLog.print(indoorTemp);
      tempLog.print(',');
      tempLog.print(indoorHum);
      tempLog.print(',');
      // log outdoor weather data from wunderground.com
      /*
      if (currentMillis - lastOutdoorDataMillis > OUTDOOR_LOG_INTERVAL) 
      {
        lastOutdoorDataMillis = currentMillis;
        double newOutdoorTemp = atof(wunderground.getCurrentTemp().c_str());
        // todo : nog een sanity check op de waarde die van wunderground komt
        outdoorTemp = (float) newOutdoorTemp;
        tempLog.print(newOutdoorTemp); 
      }
      */
      tempLog.print(',');
      tempLog.print(hc_GetHeatPercentage()); // log the heatPercentage to monitor the algorithm
      tempLog.println();
      tempLog.close();
    }
  }
} // run7021

// synchronous version for now --> includes 2x 150ms delay for the sensor data to be ready
// acceptable for now, reading only 1/minute
// TODO : make asynchronous!
// for reference : TemperatureLoggerInOut.ino : asynchronous reading of DS12B20 sensor
// intf : requestTemperature & requestHumidity en then wait for a fixed delay before reading data
static void readSi7021(float *rh, float *temp)
{
  unsigned int data[2] = {0,0};
 
  Wire.beginTransmission(si7021Addr);
  //Send humidity measurement command
  Wire.write(0xF5);
  Wire.endTransmission();
  delay(150);
 
  // Request 2 bytes of data
  Wire.requestFrom(si7021Addr, 2);
  // Read 2 bytes of data to get humidity
  if(Wire.available() == 2)
  {
    data[0] = Wire.read();
    data[1] = Wire.read();
    // Convert the data
    float humidity  = ((data[0] * 256.0) + data[1]);
    humidity = ((125 * humidity) / 65536.0) - 6;
    *rh = humidity;
  }
  else *rh = 0.0; // reading error (in case the sensor returns odd values - due to i2c comms?
 
  Wire.beginTransmission(si7021Addr);
  // Send temperature measurement command
  Wire.write(0xF3);
  Wire.endTransmission();
  delay(150);
 
  // Request 2 bytes of data
  Wire.requestFrom(si7021Addr, 2);
 
  // Read 2 bytes of data for temperature
  if(Wire.available() == 2)
  {
    data[0] = Wire.read();
    data[1] = Wire.read();
    // Convert the data
    float celsTemp  = ((data[0] * 256.0) + data[1]);
    celsTemp = ((175.72 * celsTemp) / 65536.0) - 46.85;
    *temp = celsTemp;
  }
  else {
    *temp = 100.0; // error
  }
} // readSi7021


/****************************************************************************************************/
/* EXPERIMENTAL NOT IN USE                                                                          */
/****************************************************************************************************/

// state
uint32_t lastHeatSettingChangeMillis;

#define CONTROLMODE_RESET   0  // at startup & after a setpoint change
#define CONTROLMODE_COARSE  1  // abs(deltaTemp) > 1.0
#define CONTROLMODE_FINE    2  // abs(deltaTemp) < 1.0
uint8_t heatControlMode = CONTROLMODE_RESET; // reset, coarse, fine

//uint8_t hc_HeatPercentageDay = HEAT_DEFAULT_DAY;
uint8_t hc_HeatPercentage; // current heatPercentage
#define HEATPERCENTAGE_AUTOCHANGE_DELAY 15*1000*60 // 15 minutes
bool heatPercentageRangeLimitingOn = false;
uint8_t heatPercentageRangeCenter;


static void runHeatControl()
{
  uint8_t idx5min = (indoorTempsIdx + NBR_INDOORTEMP_POINTS - 5) % NBR_INDOORTEMP_POINTS;
  uint8_t idx15min = (indoorTempsIdx + NBR_INDOORTEMP_POINTS - 15) % NBR_INDOORTEMP_POINTS;
  // indoorTemp = latest temperature measurement = indoorTemps[indoorTempsIdx]
  float delta5 = indoorTemp - indoorTemps[idx5min]; // temp difference over last 5 minutes
  float delta15 = indoorTemp - indoorTemps[idx15min];// temp difference over last 15 minutes

  float deltaTemp = indoorTemp - hc_TargetSetpoint;
  float timeToSetpointTemperature; // estimated duration until target setpoint is reached
  bool heatPercentageChangeAllowed = false;

  if (((millis() - lastHeatSettingChangeMillis) > HEATPERCENTAGE_AUTOCHANGE_DELAY) &&
      (delta15 * delta5 > 0.0)) // delta5 & delta15 move in the same direction
  {
    heatPercentageChangeAllowed = true;
  }

  timeToSetpointTemperature = - deltaTemp / delta15 * 15.0; // in minutes; if negative, target setpoint is not reachable

  switch (heatControlMode)
  {
    case CONTROLMODE_RESET :
      // after a reset or change in target setpoint
      heatControlMode = CONTROLMODE_COARSE;
      if (deltaTemp > 2.0) hc_HeatPercentage = 0;
      else if (deltaTemp > 1.0) hc_HeatPercentage = 50;
      else if (deltaTemp < -1.0) hc_HeatPercentage = 75;
      else if (deltaTemp < -2.0) hc_HeatPercentage = 100;
      else 
      {
        heatControlMode = CONTROLMODE_FINE;
        // and keep current hc_HeatPercentage
      }
      break;
      heatPercentageChangeAllowed = true;

    case CONTROLMODE_COARSE : // abs(deltaTemp) > 1.0
      if (!heatPercentageChangeAllowed)
        break;

      // ready for range limiting? (temperature moves in the right direction)
      if (deltaTemp * delta15 < 0.0) { 
        if (!heatPercentageRangeLimitingOn) {
          heatPercentageRangeLimitingOn = true;
          heatPercentageRangeCenter = hc_HeatPercentage;
        }
      }
      else
        heatPercentageRangeLimitingOn = false;

      // too hot :
      if ((deltaTemp > 1.0) && (delta15 > 0.0)) hc_HeatPercentage -= 15;
      else if ((deltaTemp > 0.0) && (timeToSetpointTemperature > 60.0)) hc_HeatPercentage -= 5;
      // too cold :
      else if ((deltaTemp < -1.0) && (delta15 < 0.0)) hc_HeatPercentage += 15;
      else if ((deltaTemp < 0.0) && (timeToSetpointTemperature > 60.0)) hc_HeatPercentage += 5;

      // adjust hc_HeatPercentage to within range limits
      /* voorlopig geen range limiting in coarse control
      if (heatPercentageRangeLimitingOn) {
        hc_HeatPercentage = _max (heatPercentageRangeCenter-15, hc_HeatPercentage);
        hc_HeatPercentage = _min (heatPercentageRangeCenter+15, hc_HeatPercentage);
      }
      */

      // update heatControlMode ?
      if ((deltaTemp < 1.0) && (deltaTemp >-1.0)) {
        heatControlMode = CONTROLMODE_FINE;
        heatPercentageRangeLimitingOn = false;
      }
      break;

    case CONTROLMODE_FINE : // abs(deltaTemp) < 1.0
      if (!heatPercentageChangeAllowed)
        break;

      // ready for range limiting? (abs(deltaTemp) < 0.5)
      if ((deltaTemp < 0.5) && (deltaTemp > -0.5)) {
        if (!heatPercentageRangeLimitingOn) {
          heatPercentageRangeLimitingOn = true;
          heatPercentageRangeCenter = hc_HeatPercentage;
        }
      }
      else
        heatPercentageRangeLimitingOn = false;

      // too hot :
      if (deltaTemp > 0.5) {
        if (delta15 > 0.0) hc_HeatPercentage -= 5;
        else if ((delta15 < 0.0) && (timeToSetpointTemperature < 35.0)) hc_HeatPercentage += 5; // slow down the temp drop to avoid overshoot
      }
      else if ((deltaTemp > 0.0) && (deltaTemp < 0.5)) {
        if (delta15 > 0.0) hc_HeatPercentage -= 2;
      }
      // too cold : 
      else if (deltaTemp < -0.5){
        if (delta15 < 0.0) hc_HeatPercentage += 5;
        else if ((delta15 > 0.0) && (timeToSetpointTemperature < 35.0)) hc_HeatPercentage -= 5; // slow down the temp raise to avoid overshoot

      }
      else if ((deltaTemp < 0.0) && (deltaTemp > -0.5)) {
        if (delta15 < 0.0) hc_HeatPercentage += 2;
      }

      // adjust hc_HeatPercentage to within range limits
      if (heatPercentageRangeLimitingOn) {
        hc_HeatPercentage = _max (heatPercentageRangeCenter-10, hc_HeatPercentage);
        hc_HeatPercentage = _min (heatPercentageRangeCenter+10, hc_HeatPercentage);
      }

      // update heatControlMode ? (well this shouldn't happen if the algorithm works correctly)
      if ((deltaTemp > 1.0) || (deltaTemp < -1.0)) {
        heatControlMode = CONTROLMODE_COARSE;
        heatPercentageRangeLimitingOn = false;
      } 
      break;

  } // switch

  if (heatPercentageChangeAllowed)
  {
    hc_SetHeatPercentage (hc_HeatPercentage);
    lastHeatSettingChangeMillis = millis();
  }


} // runHeatControl

