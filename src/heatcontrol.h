#ifndef _HEATCONTROL_H_
#define _HEATCONTROL_H_

// thermostat modes
#define MODE_CLOCK  0 // corresponds with alexa "AUTO"
#define MODE_DAY    1 // corresponds with alexa "HEAT"
#define MODE_NIGHT  2 // corresponds with alexa "ECO"
#define MODE_FROST  3 // corresponds with alexa "OFF"

void hc_setup();
void hc_loop();

void hc_SetHeatPercentage(uint8_t heat, bool manual=true); // 0..100
uint8_t hc_GetHeatPercentage (); // return 0..100

void hc_SetTargetSetpoint (float targetSetpoint);
float hc_GetTargetSetpoint (void);

void hc_SetThermostatMode(uint8_t thermostatMode);
uint8_t hc_GetThermostatMode(void);

float hc_GetIndoorTemp();
float hc_GetIndoorHum();
#define TEMP_TREND_STABLE     0
#define TEMP_TREND_INCREASING 1
#define TEMP_TREND_DECREASING 2
uint8_t hc_GetIndoorTempTrend(); // returns any of the 3 trend values

#endif // _HEATCONTROL_H_


