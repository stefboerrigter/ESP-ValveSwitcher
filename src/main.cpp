/*
 *  This sketch demonstrates how to scan WiFi networks. 
 *  The API is almost the same as with the WiFi Shield library, 
 *  the most obvious difference being the different file you need to include:
 */
#include "ESP8266WiFi.h"
#include <Arduino.h>
#include "tempsensor.h"

#define WIFI_SSID "FamBoerrigter"
#define WIFI_PWD  "BoerrigterGijsbers147"

//DS18 temperature sensor config
#define DS18_GPIO     D5 //default pin
#define DS18_PARASITE false //default not parasite


typedef struct {
    uint32_t timestamp;      // for internal timings, via millis()
    uint8_t  ds18Sensors;    // count of dallas sensors
    DS18 ds18;               //ds18 object
} Admin;

//Local administration object of struct
Admin m_admin;

void setup() {
    m_admin.ds18Sensors = m_admin.ds18.setup(DS18_GPIO, DS18_PARASITE); // returns #sensors
}

void loop() {
    /* Process sensors if any */
    if (m_admin.ds18Sensors) {
        m_admin.ds18.loop();
    }
}
