/*
 *  This sketch demonstrates how to scan WiFi networks. 
 *  The API is almost the same as with the WiFi Shield library, 
 *  the most obvious difference being the different file you need to include:
 */
#include "ESP8266WiFi.h"
#include <Arduino.h>
#include "tempsensor.h"
#include "MyESP.h"
#include "utils.h"
#include "version.h"
#include "valveManager.h"
#include "valve.h"
#include <CRC32.h>       // https://github.com/bakercp/CRC32
#include <ArduinoJson.h> // https://github.com/bblanchon/ArduinoJson

//DS18 temperature sensor config
#define DS18_GPIO     D5 //default pin
#define DS18_PARASITE false //default not parasite

#define TOPIC_VALVE_MAIN "valves"
#define TOPIC_VALVE_CMD  "valve_cmd_"
#define TOPIC_VALVE_DATA "valve_data"

static const command_t project_cmds[] PROGMEM = {
    {true, "led <on | off>", "toggle status LED on/off"},
    {true, "led_gpio <gpio>", "set the LED pin. Default is the onboard LED 2. For external D1 use 5"},
    {true, "valve [<valve_id> <valve_status>]", "Print all valves OR set valve ID to Status"},
    {false, "info", "show current captured on the devices"},

};
uint8_t _project_cmds_count = ArraySize(project_cmds);

typedef enum {
    MQTT_VALVE_NOT_CONNECTED = 0,
    MQTT_VALVE_OPEN,
    MQTT_VALVE_CLOSE,
    MQTT_VALVE_INVALID = 255,
} mqtt_valve_status;

typedef struct {
    mqtt_valve_status mqqt_status;
    valve_status_t    valve_status;
}valve_mqtt_translation;

valve_mqtt_translation valve_mqtt [] = {
    {MQTT_VALVE_NOT_CONNECTED,  VALVE_NOT_CONNECTED},
    {MQTT_VALVE_OPEN,           VALVE_OPEN},
    {MQTT_VALVE_CLOSE,          VALVE_CLOSED}
};

void publishValues(bool force);

typedef struct {
    uint32_t timestamp;      // for internal timings, via millis()
    uint8_t  ds18Sensors;    // count of dallas sensors
    DS18 ds18;               //ds18 object
    MyESP myESP;
    ValveManager valveManager;
} Admin;

//Local administration object of struct
Admin m_admin;

//// Local Function definitions:
bool LoadSaveCallback(MYESP_FSACTION action, JsonObject settings);
bool SetListCallback(MYESP_FSACTION action, uint8_t wc, const char * setting, const char * value);
void OTACallback_pre();
void OTACallback_post();
void TelnetCallback(uint8_t event);
void TelnetCommandCallback(uint8_t wc, const char * commandLine);
void MQTTCallback(unsigned int type, const char * topic, const char * message);
uint8_t _hasValvespecified(const char * key, const char * input);

mqtt_valve_status valveStatusToMqtt(valve_status_t status);


void setup() {
    // set up myESP for Wifi, MQTT, MDNS and Telnet callbacks
    m_admin.myESP.setTelnet(TelnetCommandCallback, TelnetCallback);      // set up Telnet commands
    //m_admin.myESP.setWIFI(WIFICallback);                                 // wifi callback
    m_admin.myESP.setMQTT(MQTTCallback, publishValues);                                 // MQTT ip, username and password taken from the SPIFFS settings
    m_admin.myESP.setSettings(LoadSaveCallback, SetListCallback, true); // default is Serial off
    m_admin.myESP.setOTA(OTACallback_pre, OTACallback_post);             // OTA callback which is called when OTA is starting and stopping
    m_admin.myESP.begin(APP_HOSTNAME, APP_NAME, APP_VERSION, APP_URL, APP_UPDATEURL);
  
    m_admin.ds18Sensors = m_admin.ds18.setup(DS18_GPIO, DS18_PARASITE); // returns #sensors
    m_admin.valveManager.Initialize(publishValues);
}

void loop() {
    m_admin.myESP.loop(); //Keep WiFI, MQTT and stuf active..
    /* Process sensors if any */
    if (m_admin.ds18Sensors) {
        m_admin.ds18.loop();
    }

    m_admin.valveManager.Process();
}



// callback for loading/saving settings to the file system (SPIFFS)
bool LoadSaveCallback(MYESP_FSACTION action, JsonObject settings) {
#ifdef EXAMPLE_SAVE_LOAD
    if (action == MYESP_FSACTION_LOAD) {
        // check for valid json
        if (settings.isNull()) {
            myDebug_P(PSTR("Error processing json settings"));
            return false;
        }

        // serializeJsonPretty(settings, Serial); // for debugging

        EMSESP_Settings.led             = settings["led"];
        EMSESP_Settings.led_gpio        = settings["led_gpio"] | EMSESP_LED_GPIO;
        EMSESP_Settings.dallas_gpio     = settings["dallas_gpio"] | EMSESP_DALLAS_GPIO;
        EMSESP_Settings.dallas_parasite = settings["dallas_parasite"] | EMSESP_DALLAS_PARASITE;
        EMSESP_Settings.shower_timer    = settings["shower_timer"];
        EMSESP_Settings.shower_alert    = settings["shower_alert"];
        EMSESP_Settings.publish_time    = settings["publish_time"] | DEFAULT_PUBLISHTIME;

        EMSESP_Settings.listen_mode = settings["listen_mode"];
        ems_setTxDisabled(EMSESP_Settings.listen_mode);

        EMSESP_Settings.tx_mode = settings["tx_mode"] | EMS_TXMODE_DEFAULT; // default to 1 (generic)
        ems_setTxMode(EMSESP_Settings.tx_mode);

        return true;
    }

    if (action == MYESP_FSACTION_SAVE) {
        settings["led"]             = EMSESP_Settings.led;
        settings["led_gpio"]        = EMSESP_Settings.led_gpio;
        settings["dallas_gpio"]     = EMSESP_Settings.dallas_gpio;
        settings["dallas_parasite"] = EMSESP_Settings.dallas_parasite;
        settings["listen_mode"]     = EMSESP_Settings.listen_mode;
        settings["shower_timer"]    = EMSESP_Settings.shower_timer;
        settings["shower_alert"]    = EMSESP_Settings.shower_alert;
        settings["publish_time"]    = EMSESP_Settings.publish_time;
        settings["tx_mode"]         = EMSESP_Settings.tx_mode;

        return true;
    }
#endif
    return false;
}

// callback for custom settings when showing Stored Settings with the 'set' command
// wc is number of arguments after the 'set' command
// returns true if the setting was recognized and changed and should be saved back to SPIFFs
bool SetListCallback(MYESP_FSACTION action, uint8_t wc, const char * setting, const char * value) {
    bool ok = false;
#ifdef EXAMPLE_SAVE_LOAD
    if (action == MYESP_FSACTION_SET) {
        // led
        if ((strcmp(setting, "led") == 0) && (wc == 2)) {
            if (strcmp(value, "on") == 0) {
                EMSESP_Settings.led = true;
                ok                  = true;
            } else if (strcmp(value, "off") == 0) {
                EMSESP_Settings.led = false;
                ok                  = true;
                // let's make sure LED is really off - For onboard high=off
                digitalWrite(EMSESP_Settings.led_gpio, (EMSESP_Settings.led_gpio == LED_BUILTIN) ? HIGH : LOW);
            } else {
                myDebug_P(PSTR("Error. Usage: set led <on | off>"));
            }
        }

        // test mode
        if ((strcmp(setting, "listen_mode") == 0) && (wc == 2)) {
            if (strcmp(value, "on") == 0) {
                EMSESP_Settings.listen_mode = true;
                ok                          = true;
                myDebug_P(PSTR("* in listen mode. All Tx is disabled."));
                ems_setTxDisabled(true);
            } else if (strcmp(value, "off") == 0) {
                EMSESP_Settings.listen_mode = false;
                ok                          = true;
                ems_setTxDisabled(false);
                myDebug_P(PSTR("* out of listen mode. Tx is now enabled."));
            } else {
                myDebug_P(PSTR("Error. Usage: set listen_mode <on | off>"));
            }
        }

        // led_gpio
        if ((strcmp(setting, "led_gpio") == 0) && (wc == 2)) {
            EMSESP_Settings.led_gpio = atoi(value);
            // reset pin
            pinMode(EMSESP_Settings.led_gpio, OUTPUT);
            digitalWrite(EMSESP_Settings.led_gpio, (EMSESP_Settings.led_gpio == LED_BUILTIN) ? HIGH : LOW); // light off. For onboard high=off
            ok = true;
        }

        // dallas_gpio
        if ((strcmp(setting, "dallas_gpio") == 0) && (wc == 2)) {
            EMSESP_Settings.dallas_gpio = atoi(value);
            ok                          = true;
        }

        // dallas_parasite
        if ((strcmp(setting, "dallas_parasite") == 0) && (wc == 2)) {
            if (strcmp(value, "on") == 0) {
                EMSESP_Settings.dallas_parasite = true;
                ok                              = true;
            } else if (strcmp(value, "off") == 0) {
                EMSESP_Settings.dallas_parasite = false;
                ok                              = true;
            } else {
                myDebug_P(PSTR("Error. Usage: set dallas_parasite <on | off>"));
            }
        }

        // shower timer
        if ((strcmp(setting, "shower_timer") == 0) && (wc == 2)) {
            if (strcmp(value, "on") == 0) {
                EMSESP_Settings.shower_timer = true;
                ok                           = do_publishShowerData();
            } else if (strcmp(value, "off") == 0) {
                EMSESP_Settings.shower_timer = false;
                ok                           = do_publishShowerData();
            } else {
                myDebug_P(PSTR("Error. Usage: set shower_timer <on | off>"));
            }
        }

        // shower alert
        if ((strcmp(setting, "shower_alert") == 0) && (wc == 2)) {
            if (strcmp(value, "on") == 0) {
                EMSESP_Settings.shower_alert = true;
                ok                           = do_publishShowerData();
            } else if (strcmp(value, "off") == 0) {
                EMSESP_Settings.shower_alert = false;
                ok                           = do_publishShowerData();
            } else {
                myDebug_P(PSTR("Error. Usage: set shower_alert <on | off>"));
            }
        }

        // publish_time
        if ((strcmp(setting, "publish_time") == 0) && (wc == 2)) {
            EMSESP_Settings.publish_time = atoi(value);
            ok                           = true;
        }

        // tx_mode
        if ((strcmp(setting, "tx_mode") == 0) && (wc == 2)) {
            uint8_t mode = atoi(value);
            if ((mode >= 1) && (mode <= 3)) { // see ems.h for definitions
                EMSESP_Settings.tx_mode = mode;
                ems_setTxMode(mode);
                ok = true;
            } else {
                myDebug_P(PSTR("Error. Usage: set tx_mode <1 | 2 | 3>"));
            }
        }
    }

    if (action == MYESP_FSACTION_LIST) {
        myDebug_P(PSTR("  led=%s"), EMSESP_Settings.led ? "on" : "off");
        myDebug_P(PSTR("  led_gpio=%d"), EMSESP_Settings.led_gpio);
        myDebug_P(PSTR("  dallas_gpio=%d"), EMSESP_Settings.dallas_gpio);
        myDebug_P(PSTR("  dallas_parasite=%s"), EMSESP_Settings.dallas_parasite ? "on" : "off");
        myDebug_P(PSTR("  tx_mode=%d"), EMSESP_Settings.tx_mode);
        myDebug_P(PSTR("  listen_mode=%s"), EMSESP_Settings.listen_mode ? "on" : "off");
        myDebug_P(PSTR("  shower_timer=%s"), EMSESP_Settings.shower_timer ? "on" : "off");
        myDebug_P(PSTR("  shower_alert=%s"), EMSESP_Settings.shower_alert ? "on" : "off");
        myDebug_P(PSTR("  publish_time=%d"), EMSESP_Settings.publish_time);
    }
#endif
    return ok;
}

// Show command - display stats on an 's' command
void showInfo() {
    myDebug_P(PSTR("%sESP-Valve system stats:%s"), COLOR_BOLD_ON, COLOR_BOLD_OFF);
    myDebug_P(PSTR(" Valve [<Valve ID> <Valve Action>] to control valves"));
}

// print settings
void _showCommands(uint8_t event) {
    bool      mode = (event == TELNET_EVENT_SHOWSET); // show set commands or normal commands
    command_t cmd;

    // find the longest key length so we can right-align the text
    uint8_t max_len = 0;
    uint8_t i;
    for (i = 0; i < _project_cmds_count; i++) {
        memcpy_P(&cmd, &project_cmds[i], sizeof(cmd));
        if ((strlen(cmd.key) > max_len) && (cmd.set == mode)) {
            max_len = strlen(cmd.key);
        }
    }

    char line[200] = {0};
    for (i = 0; i < _project_cmds_count; i++) {
        memcpy_P(&cmd, &project_cmds[i], sizeof(cmd));
        if (cmd.set == mode) {
            if (event == TELNET_EVENT_SHOWSET) {
                strlcpy(line, "  set ", sizeof(line));
            } else {
                strlcpy(line, "*  ", sizeof(line));
            }
            strlcat(line, cmd.key, sizeof(line));
            for (uint8_t j = 0; j < ((max_len + 5) - strlen(cmd.key)); j++) { // account for longest string length
                strlcat(line, " ", sizeof(line));                             // padding
            }
            strlcat(line, cmd.description, sizeof(line));
            myDebug(line); // print the line
        }
    }
}

// call back when a telnet client connects or disconnects
// we set the logging here
void TelnetCallback(uint8_t event) {
    if (event == TELNET_EVENT_CONNECT) {
        return;
    } else if (event == TELNET_EVENT_DISCONNECT) {
        return;
    } else if ((event == TELNET_EVENT_SHOWCMD) || (event == TELNET_EVENT_SHOWSET)) {
        _showCommands(event);
    }
}

// used to read the next string from an input buffer and convert to an 8 bit int
uint8_t _readIntNumber() {
    char * numTextPtr = strtok(nullptr, ", \n");
    if (numTextPtr == nullptr) {
        return 0;
    }
    return atoi(numTextPtr);
}

// extra commands options for telnet debug window
// wc is the word count, i.e. number of arguments. Everything is in lower case.
void TelnetCommandCallback(uint8_t wc, const char * commandLine) {
    bool ok = false;
    // get first command argument
    char * first_cmd = strtok((char *)commandLine, ", \n");

    if (strcmp(first_cmd, "info") == 0) 
    {
        showInfo();
        ok = true;
    }

    if (strcmp(first_cmd, "valve") == 0) 
    {
        if(wc == 3)
        {
            //Specific valve given with open/close command 
            int valve = _readIntNumber();
            int setting = _readIntNumber();
            
            Valve *pValve = m_admin.valveManager.getValve((VALVE_TYPE)valve);
            if(pValve)
            {
                if(setting)
                {
                    pValve->openValve();
                }
                else
                {
                    pValve->closeValve();
                }
            }
            else
            {
                myDebug_P(PSTR("Valve %d not valid"), valve);
            }
        }
        else
        {
            //Print all valve information:
            VALVE_TYPE type = VALVE_LIVINGROOM;
            int iterator;
            myDebug_P(PSTR("Print valve info, use valve <VALVEID> <SET(0|1)> to control valve"));
            for(type = VALVE_TYPE_FIRST; type < VALVE_TYPE_LAST; )
            {
                Valve *pValve = m_admin.valveManager.getValve(type);
                myDebug("%d | %s", type, pValve->toString());
                iterator = static_cast<int>(type);
                type = static_cast<VALVE_TYPE>(++iterator);
            }
        }
        ok = true;
    }

    // check for invalid command
    if (!ok) {
        myDebug_P(PSTR("Unknown command or wrong number of arguments. Use ? for help."));
    }
}

// OTA callback when the OTA process starts
void OTACallback_pre() {
    
}

// OTA callback when the OTA process finishes
void OTACallback_post() {
    
}


// MQTT Callback to handle incoming/outgoing changes
void MQTTCallback(unsigned int type, const char * topic, const char * message) {
    // we're connected. lets subscribe to some topics
    if (type == MQTT_CONNECT_EVENT) {
        // subscribe to the 4 heating circuits for receiving setpoint temperature and modes
        char topic_s[50];
        char buffer[4];
        VALVE_TYPE type = VALVE_LIVINGROOM;
        int iterator;
        for(type = VALVE_TYPE_FIRST; type < VALVE_TYPE_LAST; )
        {
            Valve *pValve = m_admin.valveManager.getValve(type);
            if(pValve)
            {
                strlcpy(topic_s, TOPIC_VALVE_CMD, sizeof(topic_s));
                strlcat(topic_s, itoa(type, buffer, 10), sizeof(topic_s));
                if(m_admin.myESP.mqttSubscribe(topic_s))
                {
                   myDebug_P(PSTR("[MQTT] Subscribed to topic %s."), topic_s); 
                }
                else
                {
                    myDebug_P(PSTR("[MQTT] Subscription to topic %s failed"), topic_s);
                }
                
            }
            iterator = static_cast<int>(type);
            type = static_cast<VALVE_TYPE>(++iterator);
        }
        // generic incoming MQTT command for valve ports.
        m_admin.myESP.mqttSubscribe(TOPIC_VALVE_MAIN);

        return;
    }

    // handle incoming MQTT publish events
    if (type != MQTT_MESSAGE_EVENT) {
        return;
    }
    // check first for generic commands
    if (strcmp(topic, TOPIC_VALVE_MAIN) == 0) {
        // convert JSON and get the command
        StaticJsonDocument<100> doc;
        DeserializationError    error = deserializeJson(doc, message); // Deserialize the JSON document
        if (error) {
            myDebug_P(PSTR("[MQTT] Invalid command from topic %s, payload %s, error %s"), topic, message, error.c_str());
            return;
        }
        //const char * command = doc["cmd"];
        myDebug_P(PSTR("[MQTT] Received topic %s, payload %s"), topic, message);
        
        return; // no match for generic commands
    }

#if 0
    if (strcmp(topic, TOPIC_VALVE_CMD) == 0) {
        //uint8_t t = atoi((char *)message);
        //Perform Action
        //publishValues(true);
        myDebug_P(PSTR("[MQTT] Received topic %s, payload %s"), topic, message);
        return;
    }
#endif
    uint8_t valve;
    // thermostat temp changes
    valve = _hasValvespecified(TOPIC_VALVE_CMD, topic);
    if (valve != 255) {
        char *endptr;
        int valveValue = strtol(message, &endptr, 10);
        Valve *pValve = m_admin.valveManager.getValve((VALVE_TYPE)valve);
        if(pValve)
        {
            switch(valveValue)
            {
                case MQTT_VALVE_NOT_CONNECTED:
                    myDebug("Trying to NOT connect valve? ");
                    break;
                case MQTT_VALVE_CLOSE:
                    if(pValve->getValveStatus() != VALVE_NOT_CONNECTED)
                    {
                        pValve->closeValve();
                    }
                    else
                    {
                        myDebug("Trying to close valve which isn't connected");
                    }
                    
                    break;
                case MQTT_VALVE_OPEN:
                    if(pValve->getValveStatus() != VALVE_NOT_CONNECTED)
                    {
                        pValve->openValve();
                    }
                    else
                    {
                        myDebug("Trying to open valve which isn't connected");
                    }
                    break;
                default:
                    break;
            }
        }
        //myDebug_P(PSTR("[MQTT]: Valve Received new data %d [%d]"), valve, valveValue);
        //publishValues(false); // publish back immediately
        return;
    }

    myDebug_P(PSTR("[MQTT] Received topic %s, payload %s not handled"), topic, message);
}


// Init callback, which is used to set functions and call methods after a wifi connection has been established
void WIFICallback() {
    // This is where we enable the UART service to scan the incoming serial Tx/Rx bus signals
    // This is done after we have a WiFi signal to avoid any resource conflicts
    // system_uart_swap();
}

// see's if a topic string is appended with an interger value
// used to identify a heating circuit
// returns valve number 0 - N
// or the default (255) is no suffix can be found
uint8_t _hasValvespecified(const char * key, const char * input) {
    int orig_len = strlen(key); // original length of the topic we're comparing too

    // check if the strings match ignoring any suffix
    if (strncmp(input, key, orig_len) == 0) {
        // see if we have additional chars at the end, we want none or 1
        uint8_t diff = (strlen(input) - orig_len);
        if (diff > 1) {
            return 255; // invalid
        }

        if (diff == 0) {
            return 255; // identical, use default which is 1
        }

        // return the value of the last char, 0-9
        return input[orig_len] - '0';
    }

    return 255; // invalid
}


// send values via MQTT
// a json object is created for the boiler and one for the thermostat
// CRC check is done to see if there are changes in the values since the last send to avoid too much wifi traffic
// a check is done against the previous values and if there are changes only then they are published. Unless force=true
void publishValues(bool force) {
    // don't send if MQTT is not connected
    if (!m_admin.myESP.isMQTTConnected()) {
        return;
    }

    //char                                      s[20] = {0}; // for formatting strings
    StaticJsonDocument<MQTT_MAX_PAYLOAD_SIZE> doc;
    char                                      data[MQTT_MAX_PAYLOAD_SIZE] = {0};
    CRC32                                     crc;
    uint32_t                                  fchecksum;
    uint8_t                                   jsonSize;

    VALVE_TYPE                                type = VALVE_LIVINGROOM;
    int                                       iterator;

    static uint32_t                           previousValvePublishCRC = 0;

    JsonObject rootObject = doc.to<JsonObject>();

    //rootObject["Valves"] = "testings";
    
    for(type = VALVE_TYPE_FIRST; type < VALVE_TYPE_LAST; )
    {
        Valve *pValve = m_admin.valveManager.getValve(type);
        JsonObject valveData = rootObject.createNestedObject(pValve->getName());
        valveData["status"] = (uint8_t)valveStatusToMqtt(pValve->getValveStatus());
        iterator = static_cast<int>(type);
        type = static_cast<VALVE_TYPE>(++iterator);
    }

    serializeJson(doc, data, sizeof(data));
    // check for empty json
    jsonSize = measureJson(doc);
    if (jsonSize > 2) {
        // calculate hash and send values if something has changed, to save unnecessary wifi traffic
        for (uint8_t i = 0; i < (jsonSize - 1); i++) {
            crc.update(data[i]);
        }
        fchecksum = crc.finalize();
        if ((previousValvePublishCRC != fchecksum) || force) {
            previousValvePublishCRC = fchecksum;
            myDebug("Publishing valve data via MQTT [%s]", data);

            // send values via MQTT
            m_admin.myESP.mqttPublish(TOPIC_VALVE_DATA, data);
        }
        else
        {
            myDebug("Publishing valve data via MQTT skipped");
        }
        
    }
}


mqtt_valve_status valveStatusToMqtt(valve_status_t status)
{
    uint8_t i = 0;
    for(i = 0; i < (sizeof(valve_mqtt) / sizeof(valve_mqtt[0])); i++)
    {
        if(valve_mqtt[i].valve_status == status)
        {
            return valve_mqtt[i].mqqt_status;
        }
    }
    return MQTT_VALVE_INVALID;
}