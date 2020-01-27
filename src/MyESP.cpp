/*
 * MyESP - my ESP helper class to handle WiFi, MQTT, Telnet, Web and other utils
 * 
 * Paul Derbyshire - first revision: December 2018
 *
 * with ideas borrowed from Espurna https://github.com/xoseperez/espurna
 * and web from https://github.com/esprfid/esp-rfid
 */

#include "MyESP.h"

#ifdef CRASH
EEPROM_Rotate EEPROMr;
#endif

union system_rtcmem_t {
    struct {
        uint8_t stability_counter;
        uint8_t reset_reason;
        uint8_t boot_status;
        uint8_t dropout_counter;
    } parts;
    uint32_t value;
};

// nasty global variables that are called from internal ws functions
static char * _general_password = nullptr;
static bool   _shouldRestart    = false;

uint8_t RtcmemSize = (sizeof(RtcmemData) / 4u);
auto    Rtcmem     = reinterpret_cast<volatile RtcmemData *>(RTCMEM_ADDR);

// constructor
MyESP::MyESP() {
    _general_hostname = strdup("ESPValve");
    _app_name         = strdup("ESPValveApp");
    _app_version      = strdup(MYESP_VERSION);
    _app_url          = nullptr;
    _app_updateurl    = nullptr;

    // general
    _timerequest         = false;
    _formatreq           = false;
    _suspendOutput       = false;
    _ota_pre_callback_f  = nullptr;
    _ota_post_callback_f = nullptr;
    _load_average        = 100;  // calculated load average
    _general_serial      = true; // serial is set to on as default
    _general_log_events  = true; // all logs are written to an event log in SPIFFS
    _have_ntp_time       = false;

    // telnet
    _command[0]               = '\0';
    _telnetcommand_callback_f = nullptr;
    _telnet_callback_f        = nullptr;

    // fs
    _fs_loadsave_callback_f = nullptr;
    _fs_setlist_callback_f  = nullptr;

    // mqtt
    _mqtt_ip                   = nullptr;
    _mqtt_password             = nullptr;
    _mqtt_user                 = nullptr;
    _mqtt_port                 = MQTT_PORT;
    _mqtt_base                 = nullptr;
    _mqtt_reconnect_delay      = MQTT_RECONNECT_DELAY_MIN;
    _mqtt_last_connection      = 0;
    _mqtt_connecting           = false;
    _mqtt_enabled              = false;
    _mqtt_heartbeat            = false;
    _mqtt_keepalive            = MQTT_KEEPALIVE;
    _mqtt_qos                  = MQTT_QOS;
    _mqtt_retain               = MQTT_RETAIN;
    _mqtt_will_topic           = strdup(MQTT_WILL_TOPIC);
    _mqtt_will_online_payload  = strdup(MQTT_WILL_ONLINE_PAYLOAD);
    _mqtt_will_offline_payload = strdup(MQTT_WILL_OFFLINE_PAYLOAD);

    // network
    _network_password = nullptr;
    _network_ssid     = nullptr;
    _network_wmode    = 1; // default AP

    _wifi_callback_f = nullptr;
    _wifi_connected  = false;

    // system
    _rtcmem_status = false;
    _systemStable  = true;

    // ntp
    _ntp_server   = strdup(MYESP_NTP_SERVER);
    _ntp_interval = 60;
    _ntp_enabled  = false;

    // get the build time
    _buildTime = _getBuildTime();

    // MQTT log
    for (uint8_t i = 0; i < MYESP_MQTTLOG_MAX; i++) {
        MQTT_log[i].type      = 0;
        MQTT_log[i].timestamp = 0;
        MQTT_log[i].topic     = nullptr;
        MQTT_log[i].payload   = nullptr;
    }
}

MyESP::~MyESP() {
    end();
}

// end
void MyESP::end() {
    SPIFFS.end();
    SerialAndTelnet.end();
    jw.disconnect();
}

// general debug to the telnet or serial channels
void MyESP::myDebug(const char * format, ...) {
    if (_suspendOutput)
        return;

    va_list args;
    va_start(args, format);
    char test[1];

    int len = ets_vsnprintf(test, 1, format, args) + 1;

    char * buffer = new char[len];
    ets_vsnprintf(buffer, len, format, args);
    va_end(args);

    SerialAndTelnet.println(buffer);

    delete[] buffer;
}

// for flashmemory. Must use PSTR()
void MyESP::myDebug_P(PGM_P format_P, ...) {
    if (_suspendOutput)
        return;

    char format[strlen_P(format_P) + 1];
    memcpy_P(format, format_P, sizeof(format));

    va_list args;
    va_start(args, format_P);
    char test[1];
    int  len = ets_vsnprintf(test, 1, format, args) + 1;

    char * buffer = new char[len];
    ets_vsnprintf(buffer, len, format, args);

    va_end(args);

#ifdef MYESP_TIMESTAMP
    // capture & print timestamp
    char timestamp[10] = {0};
    snprintf_P(timestamp, sizeof(timestamp), PSTR("[%06lu] "), millis() % 1000000);
    SerialAndTelnet.print(timestamp);
#endif

    SerialAndTelnet.println(buffer);

    delete[] buffer;
}

// use Serial?
bool MyESP::getUseSerial() {
    return (_general_serial);
}

// heartbeat
bool MyESP::getHeartbeat() {
    return (_mqtt_heartbeat);
}

// init heap ram
uint32_t MyESP::_getInitialFreeHeap() {
    static uint32_t _heap = 0;

    if (0 == _heap) {
        _heap = ESP.getFreeHeap();
    }

    return _heap;
}

// called when WiFi is connected, and used to start OTA, MQTT
void MyESP::_wifiCallback(justwifi_messages_t code, char * parameter) {
    if (code == MESSAGE_CONNECTED) {
#if defined(ESP8266)
        WiFi.setSleepMode(WIFI_NONE_SLEEP); // added to possibly fix wifi dropouts in arduino core 2.5.0
#endif

        jw.enableAPFallback(false); // Disable AP mode after initial connect was successful - test for https://github.com/proddy/EMS-ESP/issues/187

        myDebug_P(PSTR("[WIFI] Connected to SSID %s (hostname: %s, IP: %s)"), WiFi.SSID().c_str(), _getESPhostname().c_str(), WiFi.localIP().toString().c_str());

        /*
        myDebug_P(PSTR("[WIFI] SSID  %s"), WiFi.SSID().c_str());
        myDebug_P(PSTR("[WIFI] CH    %d"), WiFi.channel());
        myDebug_P(PSTR("[WIFI] RSSI  %d"), WiFi.RSSI());
        myDebug_P(PSTR("[WIFI] IP    %s"), WiFi.localIP().toString().c_str());
        myDebug_P(PSTR("[WIFI] MAC   %s"), WiFi.macAddress().c_str());
        myDebug_P(PSTR("[WIFI] GW    %s"), WiFi.gatewayIP().toString().c_str());
        myDebug_P(PSTR("[WIFI] MASK  %s"), WiFi.subnetMask().toString().c_str());
        myDebug_P(PSTR("[WIFI] DNS   %s"), WiFi.dnsIP().toString().c_str());
        myDebug_P(PSTR("[WIFI] HOST  %s"), _getESPhostname().c_str());
        */

        // start OTA
        ArduinoOTA.begin(); // moved to support esp32
        myDebug_P(PSTR("[OTA] Listening to firmware updates on %s.local:%u"), ArduinoOTA.getHostname().c_str(), OTA_PORT);

        /*
        // show reason for the restart if any
        unsigned char reason = _getCustomResetReason();
        if (reason > 0) {
            char buffer[32];
            strcpy_P(buffer, custom_reset_string[reason - 1]);
            myDebug_P(PSTR("[SYSTEM] Last reset reason: %s (count %d)"), buffer, _getSystemStabilityCounter());
        } else {
            myDebug_P(PSTR("[SYSTEM] Last reset reason: %s (count %d)"), (char *)ESP.getResetReason().c_str(), _getSystemStabilityCounter());
            myDebug_P(PSTR("[SYSTEM] Last reset info: %s"), (char *)ESP.getResetInfo().c_str());
        }
        */

        // MQTT Setup
        _mqtt_setup();

        // NTP now that we have a WiFi connection
        if (_ntp_enabled) {
            NTP.Ntp(_ntp_server, _ntp_interval); // set up NTP server
            myDebug_P(PSTR("[NTP] NTP internet time enabled via server %s"), _ntp_server);
        }

        // call any final custom stuff
        if (_wifi_callback_f) {
            _wifi_callback_f();
        }

        _wifi_connected = true;
    }

    if (code == MESSAGE_ACCESSPOINT_CREATED) {
        myDebug_P(PSTR("[WIFI] MODE AP"));
        myDebug_P(PSTR("[WIFI] SSID  %s"), jw.getAPSSID().c_str());
        myDebug_P(PSTR("[WIFI] IP    %s"), WiFi.softAPIP().toString().c_str());
        myDebug_P(PSTR("[WIFI] MAC   %s"), WiFi.softAPmacAddress().c_str());

        _wifi_connected = true;

        // call any final custom stuff
        if (_wifi_callback_f) {
            _wifi_callback_f();
        }
    }

    if (code == MESSAGE_CONNECTING) {
        myDebug_P(PSTR("[WIFI] Connecting to %s..."), parameter);
        _wifi_connected = false;
    }

    if (code == MESSAGE_CONNECT_FAILED) {
        myDebug_P(PSTR("[WIFI] Could not connect to %s"), parameter);
        _wifi_connected = false;
    }

    if (code == MESSAGE_DISCONNECTED) {
        myDebug_P(PSTR("[WIFI] Disconnected"));
        _increaseSystemDropoutCounter(); // +1 to number of disconnects
        _wifi_connected = false;
    }

    if (code == MESSAGE_SCANNING) {
        myDebug_P(PSTR("[WIFI] Scanning"));
    }

    if (code == MESSAGE_SCAN_FAILED) {
        myDebug_P(PSTR("[WIFI] Scan failed"));
    }

    if (code == MESSAGE_NO_NETWORKS) {
        myDebug_P(PSTR("[WIFI] No networks found"));
    }

    if (code == MESSAGE_NO_KNOWN_NETWORKS) {
        myDebug_P(PSTR("[WIFI] No known networks found"));
    }

    if (code == MESSAGE_FOUND_NETWORK) {
        myDebug_P(PSTR("[WIFI] %s"), parameter);
    }

    if (code == MESSAGE_CONNECT_WAITING) {
        // too much noise
    }

    if (code == MESSAGE_ACCESSPOINT_CREATING) {
        myDebug_P(PSTR("[WIFI] Creating access point"));
        // for setting of wifi mode to AP, but don't save
        _network_wmode = 1;
        // (void)_fs_writeConfig();
    }

    if (code == MESSAGE_ACCESSPOINT_FAILED) {
        myDebug_P(PSTR("[WIFI] Could not create access point"));
    }
}

// return true if in WiFi AP mode
// does not work after wifi reset on ESP32 yet. See https://github.com/espressif/arduino-esp32/issues/1306
bool MyESP::isAPmode() {
    return (WiFi.getMode() & WIFI_AP);
}

// received MQTT message
// we send this to the call back function. Important to parse are the event strings such as MQTT_MESSAGE_EVENT and MQTT_CONNECT_EVENT
void MyESP::_mqttOnMessage(char * topic, char * payload, size_t len) {
    if (len == 0)
        return;

    char message[len + 1];
    strlcpy(message, (char *)payload, len + 1);

    myDebug_P(PSTR("[MQTT] Received %s => %s"), topic, message); // enable for debugging

    // topics are in format MQTT_BASE/HOSTNAME/TOPIC
    char * topic_magnitude = strrchr(topic, '/'); // strip out everything until last /
    if (topic_magnitude != nullptr) {
        topic = topic_magnitude + 1;
    }

    // check for standard messages
    // Restart the device
    if (strcmp(topic, MQTT_TOPIC_RESTART) == 0) {
        myDebug_P(PSTR("[MQTT] Received restart command"), message);
        resetESP();
        return;
    }

    // Send message event to custom service
    (_mqtt_callback_f)(MQTT_MESSAGE_EVENT, topic, message);
}

// MQTT subscribe
// returns false if failed
bool MyESP::mqttSubscribe(const char * topic) {
    if (mqttClient.connected() && (strlen(topic) > 0)) {
        char * topic_s = _mqttTopic(topic);

        uint16_t packet_id = mqttClient.subscribe(topic_s, _mqtt_qos);
        // myDebug_P(PSTR("[MQTT] Subscribing to %s"), topic_s);

        if (packet_id) {
            // add to mqtt log
            _addMQTTLog(topic_s, "", 2); // type of 2 means Subscribe. Has an empty payload for now
            return true;
        } else {
            myDebug_P(PSTR("[MQTT] Error subscribing to %s, error %d"), _mqttTopic(topic), packet_id);
        }
    }

    return false; // didn't work
}

// MQTT unsubscribe
void MyESP::mqttUnsubscribe(const char * topic) {
    if (mqttClient.connected() && (strlen(topic) > 0)) {
        (void)mqttClient.unsubscribe(_mqttTopic(topic));
        myDebug_P(PSTR("[MQTT] Unsubscribing to %s"), _mqttTopic(topic));
    }
}

// Publish using the user's custom retain flag
bool MyESP::mqttPublish(const char * topic, const char * payload) {
    // use the custom MQTT retain flag
    return mqttPublish(topic, payload, _mqtt_retain);
}

// MQTT Publish
// returns true if all good
bool MyESP::mqttPublish(const char * topic, const char * payload, bool retain) {
    if (mqttClient.connected() && (strlen(topic) > 0)) {
        //myDebug_P(PSTR("[MQTT] Sending publish to %s with payload %s"), _mqttTopic(topic), payload); // for debugging
        uint16_t packet_id = mqttClient.publish(_mqttTopic(topic), _mqtt_qos, retain, payload);

        if (packet_id) {
            _addMQTTLog(topic, payload, 1); // add to the log, using type of 1 for Publish
            return true;
        } else {
            myDebug_P(PSTR("[MQTT] Error publishing to %s with payload %s [error %d]"), _mqttTopic(topic), payload, packet_id);
        }
    }

    return false; // failed
}

// MQTT onConnect - when a connect is established
void MyESP::_mqttOnConnect() {
    myDebug_P(PSTR("[MQTT] MQTT connected established"));
    _mqtt_reconnect_delay = MQTT_RECONNECT_DELAY_MIN;

    _mqtt_last_connection = millis();

    // say we're alive to the Last Will topic
    mqttPublish(_mqtt_will_topic, _mqtt_will_online_payload, true); // force retain on

    // subscribe to general subs
    mqttSubscribe(MQTT_TOPIC_RESTART);

    // subscribe to a start message and send the first publish
    // forcing retain to off since we only want to send this once
    mqttSubscribe(MQTT_TOPIC_START);
    mqttPublish(MQTT_TOPIC_START, MQTT_TOPIC_START_PAYLOAD, false);

    // send heartbeat if enabled
    _heartbeatCheck(true);

    // call custom function to handle mqtt receives
    (_mqtt_callback_f)(MQTT_CONNECT_EVENT, nullptr, nullptr);
}

// MQTT setup
void MyESP::_mqtt_setup() {
    if (!_mqtt_enabled) {
        myDebug_P(PSTR("[MQTT] is disabled"));
    }

    mqttClient.onConnect([this](bool sessionPresent) { _mqttOnConnect(); });

    mqttClient.onDisconnect([this](AsyncMqttClientDisconnectReason reason) {
        if (reason == AsyncMqttClientDisconnectReason::TCP_DISCONNECTED) {
            myDebug_P(PSTR("[MQTT] TCP Disconnected"));
            _increaseSystemDropoutCounter();                             // +1 to number of disconnects
            (_mqtt_callback_f)(MQTT_DISCONNECT_EVENT, nullptr, nullptr); // call callback with disconnect
        }
        if (reason == AsyncMqttClientDisconnectReason::MQTT_IDENTIFIER_REJECTED) {
            myDebug_P(PSTR("[MQTT] Identifier Rejected"));
        }
        if (reason == AsyncMqttClientDisconnectReason::MQTT_SERVER_UNAVAILABLE) {
            myDebug_P(PSTR("[MQTT] Server unavailable"));
        }
        if (reason == AsyncMqttClientDisconnectReason::MQTT_MALFORMED_CREDENTIALS) {
            myDebug_P(PSTR("[MQTT] Malformed credentials"));
        }
        if (reason == AsyncMqttClientDisconnectReason::MQTT_NOT_AUTHORIZED) {
            myDebug_P(PSTR("[MQTT] Not authorized"));
        }

        // Reset reconnection delay
        _mqtt_last_connection = millis();
        _mqtt_connecting      = false;
    });

    //mqttClient.onSubscribe([this](uint16_t packetId, uint8_t qos) { myDebug_P(PSTR("[MQTT] Subscribe ACK for PID %d"), packetId); });
    //mqttClient.onPublish([this](uint16_t packetId) { myDebug_P(PSTR("[MQTT] Publish ACK for PID %d"), packetId); });

    mqttClient.onMessage([this](char * topic, char * payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
        _mqttOnMessage(topic, payload, len);
    });
}

// WiFI setup
void MyESP::_wifi_setup() {
    jw.setHostname(_general_hostname); // Set WIFI hostname
    jw.subscribe([this](justwifi_messages_t code, char * parameter) { _wifiCallback(code, parameter); });
    jw.setConnectTimeout(MYESP_WIFI_CONNECT_TIMEOUT);
    jw.setReconnectTimeout(MYESP_WIFI_RECONNECT_INTERVAL);

    /// wmode 1 is AP, 0 is client
    if (_network_wmode == 1) {
        jw.enableAP(true);
    } else {
        jw.enableAP(false);
    }

    jw.enableAPFallback(true);                       // AP mode only as fallback
    jw.enableSTA(true);                              // Enable STA mode (connecting to a router)
    jw.enableScan(false);                            // Configure it to not scan available networks and connect in order of dBm
    jw.cleanNetworks();                              // Clean existing network configuration
    jw.addNetwork(_network_ssid, _network_password); // Add a network
}

// set the callback function for the OTA onstart
void MyESP::setOTA(ota_callback_f OTACallback_pre, ota_callback_f OTACallback_post) {
    _ota_pre_callback_f  = OTACallback_pre;
    _ota_post_callback_f = OTACallback_post;
}

// OTA callback when the upload process starts
void MyESP::_OTACallback() {
    myDebug_P(PSTR("[OTA] Start"));

#ifdef CRASH
    // If we are not specifically reserving the sectors we are using as
    // EEPROM in the memory layout then any OTA upgrade will overwrite
    // all but the last one.
    // Calling rotate(false) disables rotation so all writes will be done
    // to the last sector. It also sets the dirty flag to true so the next commit()
    // will actually persist current configuration to that last sector.
    // Calling rotate(false) will also prevent any other EEPROM write
    // to overwrite the OTA image.
    // In case the OTA process fails, reenable rotation.
    // See onError callback below.
    EEPROMr.rotate(false);
    EEPROMr.commit();
#endif

    if (_ota_pre_callback_f) {
        (_ota_pre_callback_f)(); // call custom function
    }
}

// OTA Setup
void MyESP::_ota_setup() {
    if (!_network_ssid) {
        return;
    }

    ArduinoOTA.setPort(OTA_PORT);
    ArduinoOTA.setHostname(_general_hostname);

    ArduinoOTA.onStart([this]() { _OTACallback(); });
    ArduinoOTA.onEnd([this]() {
        myDebug_P(PSTR("[OTA] Done, restarting..."));
        _deferredReset(500, CUSTOM_RESET_OTA);
    });

    ArduinoOTA.onProgress([this](unsigned int progress, unsigned int total) {
        static unsigned int _progOld;
        unsigned int        _prog = (progress / (total / 100));
        if (_prog != _progOld) {
            myDebug_P(PSTR("[OTA] Progress: %u%%"), _prog);
            _progOld = _prog;
        }
    });

    ArduinoOTA.onError([this](ota_error_t error) {
        if (error == OTA_AUTH_ERROR)
            myDebug_P(PSTR("[OTA] Auth Failed"));
        else if (error == OTA_BEGIN_ERROR)
            myDebug_P(PSTR("[OTA] Begin Failed"));
        else if (error == OTA_CONNECT_ERROR)
            myDebug_P(PSTR("[OTA] Connect Failed"));
        else if (error == OTA_RECEIVE_ERROR)
            myDebug_P(PSTR("[OTA] Receive Failed"));
        else if (error == OTA_END_ERROR)
            myDebug_P(PSTR("[OTA] End Failed"));

#ifdef CRASH
        // There's been an error, reenable eeprom rotation
        EEPROMr.rotate(true);
#endif
    });
}

// eeprom
void MyESP::_eeprom_setup() {
#ifdef CRASH
    EEPROMr.size(4);
    EEPROMr.begin(SPI_FLASH_SEC_SIZE);
#endif
}

// Set callback of sketch function to process project messages
void MyESP::setTelnet(telnetcommand_callback_f callback_cmd, telnet_callback_f callback) {
    _telnetcommand_callback_f = callback_cmd; // external function to handle commands
    _telnet_callback_f        = callback;
}

void MyESP::_telnetConnected() {
    myDebug_P(PSTR("[TELNET] Connected to %s version %s. Type ? for commands."), _app_name, _app_version);
    //_consoleShowHelp(); // Show the initial message

    // call callback
    if (_telnet_callback_f) {
        (_telnet_callback_f)(TELNET_EVENT_CONNECT);
    }
}

void MyESP::_telnetDisconnected() {
    // myDebug_P(PSTR("[TELNET] Telnet connection closed"));
    if (_telnet_callback_f) {
        (_telnet_callback_f)(TELNET_EVENT_DISCONNECT); // call callback
    }
}

// Initialize the telnet server
void MyESP::_telnet_setup() {
    SerialAndTelnet.setWelcomeMsg("");
    SerialAndTelnet.setCallbackOnConnect([this]() { _telnetConnected(); });
    SerialAndTelnet.setCallbackOnDisconnect([this]() { _telnetDisconnected(); });
    SerialAndTelnet.setDebugOutput(false);
    SerialAndTelnet.setPingTime(0);            // default is 1500ms (1.5 seconds)
    SerialAndTelnet.begin(TELNET_SERIAL_BAUD); // default baud is 115200

    // init command buffer for console commands
    memset(_command, 0, TELNET_MAX_COMMAND_LENGTH);
}

// Show help of commands
void MyESP::_consoleShowHelp() {
    myDebug_P(PSTR(""));
    myDebug_P(PSTR("* %s version %s"), _app_name, _app_version);

    if (isAPmode()) {
        myDebug_P(PSTR("* Device is in AP mode with SSID %s"), jw.getAPSSID().c_str());
    } else {
        myDebug_P(PSTR("* Hostname: %s (%s)"), _getESPhostname().c_str(), WiFi.localIP().toString().c_str());
        myDebug_P(PSTR("* WiFi SSID: %s (signal %d%%)"), WiFi.SSID().c_str(), getWifiQuality());
        if (isMQTTConnected()) {
            myDebug_P(PSTR("* MQTT connected (heartbeat %s)"), getHeartbeat() ? "enabled" : "disabled");
        } else {
            myDebug_P(PSTR("* MQTT disconnected"));
        }
    }

    myDebug_P(PSTR("*"));
    myDebug_P(PSTR("* Commands:"));
    myDebug_P(PSTR("*  ?/help=show commands, CTRL-D/quit=close telnet session"));
    myDebug_P(PSTR("*  set, system, restart, mqttlog"));

#ifdef CRASH
    myDebug_P(PSTR("*  crash <dump | clear | test [n]>"));
    // show crash dump if just restarted after a fatal crash
    uint32_t crash_time;
    EEPROMr.get(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_CRASH_TIME, crash_time);
    if ((crash_time) && (crash_time != 0xFFFFFFFF)) {
        myDebug_P(PSTR("[SYSTEM] There is stack data available from the last system crash. Use 'crash dump' to view and 'crash clear' to reset"));
    }
#endif

    // call callback function
    if (_telnet_callback_f) {
        (_telnet_callback_f)(TELNET_EVENT_SHOWCMD);
    }

    myDebug_P(PSTR("")); // newline
}

// see if a char * string is empty. It could not be initialized yet.
// return true if there is a value
bool MyESP::_hasValue(const char * s) {
    if ((s == nullptr) || (strlen(s) == 0)) {
        return false;
    }
    return (s[0] != '\0');
}

// print all set commands and current values
void MyESP::_printSetCommands() {
    myDebug_P(PSTR("\nset commands:\n"));
    myDebug_P(PSTR("  set erase"));
    myDebug_P(PSTR("  set <wifi_mode <ap | client>"));
    myDebug_P(PSTR("  set <wifi_ssid | wifi_password> [value]"));
    myDebug_P(PSTR("  set mqtt_enabled <on | off>"));
    myDebug_P(PSTR("  set <mqtt_ip | mqtt_username | mqtt_password> [value]"));
    myDebug_P(PSTR("  set mqtt_heartbeat <on | off>"));
    myDebug_P(PSTR("  set mqtt_base [string]"));
    myDebug_P(PSTR("  set mqtt_port [number]"));
    myDebug_P(PSTR("  set mqtt_qos [0,1,2,3]"));
    myDebug_P(PSTR("  set mqtt_keepalive [seconds]"));
    myDebug_P(PSTR("  set mqtt_retain [on | off]"));
    myDebug_P(PSTR("  set ntp_enabled <on | off>"));
    myDebug_P(PSTR("  set serial <on | off>"));
    myDebug_P(PSTR("  set log_events <on | off>"));

    // call callback function
    if (_telnet_callback_f) {
        (_telnet_callback_f)(TELNET_EVENT_SHOWSET);
    }

    myDebug_P(PSTR("\nCurrent settings:\n"));

    if (_network_wmode == 0) {
        myDebug_P(PSTR("  wifi_mode=client"));
    } else {
        myDebug_P(PSTR("  wifi_mode=ap"));
    }

    if (_hasValue(_network_ssid)) {
        myDebug_P(PSTR("  wifi_ssid=%s"), _network_ssid);
    } else {
        myDebug_P(PSTR("  wifi_ssid="));
    }
    SerialAndTelnet.print(FPSTR("  wifi_password="));
    if (_hasValue(_network_password)) {
        for (uint8_t i = 0; i < strlen(_network_password); i++) {
            SerialAndTelnet.print(FPSTR("*"));
        }
    }
    myDebug_P(PSTR(""));
    myDebug_P(PSTR("  mqtt_enabled=%s"), (_mqtt_enabled) ? "on" : "off");
    if (_hasValue(_mqtt_ip)) {
        myDebug_P(PSTR("  mqtt_ip=%s"), _mqtt_ip);
    } else {
        myDebug_P(PSTR("  mqtt_ip="));
    }
    if (_hasValue(_mqtt_user)) {
        myDebug_P(PSTR("  mqtt_username=%s"), _mqtt_user);
    } else {
        myDebug_P(PSTR("  mqtt_username="));
    }
    SerialAndTelnet.print(FPSTR("  mqtt_password="));
    if (_hasValue(_mqtt_password)) {
        for (uint8_t i = 0; i < strlen(_mqtt_password); i++) {
            SerialAndTelnet.print(FPSTR("*"));
        }
    }
    myDebug_P(PSTR(""));
    if (_hasValue(_mqtt_base)) {
        myDebug_P(PSTR("  mqtt_base=%s"), _mqtt_base);
    } else {
        myDebug_P(PSTR("  mqtt_base="));
    }
    myDebug_P(PSTR("  mqtt_port=%d"), _mqtt_port);
    myDebug_P(PSTR("  mqtt_keepalive=%d"), _mqtt_keepalive);
    myDebug_P(PSTR("  mqtt_retain=%s"), (_mqtt_retain) ? "on" : "off");
    myDebug_P(PSTR("  mqtt_qos=%d"), _mqtt_qos);
    myDebug_P(PSTR("  mqtt_heartbeat=%s"), (_mqtt_heartbeat) ? "on" : "off");

#ifdef FORCE_SERIAL
    myDebug_P(PSTR("  serial=%s (this is always when compiled with -DFORCE_SERIAL)"), (_general_serial) ? "on" : "off");
#else
    myDebug_P(PSTR("  serial=%s"), (_general_serial) ? "on" : "off");
#endif

    myDebug_P(PSTR("  ntp_enabled=%s"), (_ntp_enabled) ? "on" : "off");
    myDebug_P(PSTR("  log_events=%s"), (_general_log_events) ? "on" : "off");

    // print any custom settings
    if (_fs_setlist_callback_f) {
        (_fs_setlist_callback_f)(MYESP_FSACTION_LIST, 0, nullptr, nullptr);
    }

    myDebug_P(PSTR("")); // newline
}

// reset / restart
void MyESP::resetESP() {
    myDebug_P(PSTR("* Restart ESP..."));
    _deferredReset(500, CUSTOM_RESET_TERMINAL);
    end();
#if defined(ARDUINO_ARCH_ESP32)
    ESP.restart();
#else
    ESP.restart();
#endif
}

// read next word from string buffer
// if parameter true then a word is only terminated by a newline
char * MyESP::_telnet_readWord(bool allow_all_chars) {
    if (allow_all_chars) {
        return (strtok(nullptr, "\n")); // allow only newline
    } else {
        return (strtok(nullptr, ", \n")); // allow space and comma
    }
}

// change settings - always as strings
// messy code but effective since we don't have too many settings
// wc is word count, number of parameters after the 'set' command
bool MyESP::_changeSetting(uint8_t wc, const char * setting, const char * value) {
    bool save_config        = false;
    bool save_custom_config = false;
    bool restart            = false;

    // check for our internal commands first
    if (strcmp(setting, "erase") == 0) {
        _fs_eraseConfig();
        return true;
    } else if (strcmp(setting, "wifi_ssid") == 0) {
        save_config = fs_setSettingValue(&_network_ssid, value, "");
        restart     = save_config;
        //jw.enableSTA(false);
    } else if (strcmp(setting, "wifi_password") == 0) {
        save_config = fs_setSettingValue(&_network_password, value, "");
        restart     = save_config;
        //jw.enableSTA(false);
    } else if (strcmp(setting, "wifi_mode") == 0) {
        if (value) {
            if (strcmp(value, "ap") == 0) {
                _network_wmode = 1;
                save_config = restart = true;
            } else if (strcmp(value, "client") == 0) {
                _network_wmode = 0;
                save_config = restart = true;
            } else {
                save_config = false;
            }
        }
    } else if (strcmp(setting, "mqtt_ip") == 0) {
        save_config = fs_setSettingValue(&_mqtt_ip, value, "");
    } else if (strcmp(setting, "mqtt_username") == 0) {
        save_config = fs_setSettingValue(&_mqtt_user, value, "");
    } else if (strcmp(setting, "mqtt_password") == 0) {
        save_config = fs_setSettingValue(&_mqtt_password, value, "");
    } else if (strcmp(setting, "mqtt_base") == 0) {
        save_config = fs_setSettingValue(&_mqtt_base, value, MQTT_BASE_DEFAULT);
    } else if (strcmp(setting, "mqtt_port") == 0) {
        save_config = fs_setSettingValue(&_mqtt_port, value, MQTT_PORT);
    } else if (strcmp(setting, "mqtt_keepalive") == 0) {
        save_config = fs_setSettingValue(&_mqtt_keepalive, value, MQTT_KEEPALIVE);
    } else if (strcmp(setting, "mqtt_qos") == 0) {
        save_config = fs_setSettingValue(&_mqtt_qos, value, MQTT_QOS);
    } else if (strcmp(setting, "mqtt_enabled") == 0) {
        save_config = fs_setSettingValue(&_mqtt_enabled, value, false);
    } else if (strcmp(setting, "mqtt_retain") == 0) {
        save_config = fs_setSettingValue(&_mqtt_retain, value, MQTT_RETAIN);
    } else if (strcmp(setting, "serial") == 0) {
        save_config = fs_setSettingValue(&_general_serial, value, false);
        restart     = save_config;
    } else if (strcmp(setting, "mqtt_heartbeat") == 0) {
        save_config = fs_setSettingValue(&_mqtt_heartbeat, value, false);
    } else if (strcmp(setting, "ntp_enabled") == 0) {
        save_config = fs_setSettingValue(&_ntp_enabled, value, false);
    } else if (strcmp(setting, "log_events") == 0) {
        save_config = fs_setSettingValue(&_general_log_events, value, false);
    } else {
        // finally check for any custom commands
        if (_fs_setlist_callback_f) {
            save_custom_config = (_fs_setlist_callback_f)(MYESP_FSACTION_SET, wc, setting, value);
        }
    }

    bool ok = false;

    // if we were able to recognize the set command, continue
    if ((save_config || save_custom_config)) {
        // check for 2 params
        if (value == nullptr) {
            myDebug_P(PSTR("%s has been reset to its default value."), setting);
        } else {
            // must be 3 params
            myDebug_P(PSTR("%s changed."), setting);
        }
    }

    // now do the saving for system config if something has changed
    if (save_config) {
        ok = _fs_writeConfig();
    }

    // and see if we need to also save for custom config
    if (save_custom_config) {
        ok = _fs_createCustomConfig();
    }

    if (restart) {
        myDebug_P(PSTR("Please 'restart' to apply new settings."));
    }

    return ok;
}

// force the serial on/off
void MyESP::setUseSerial(bool b) {
    _general_serial = b;
    SerialAndTelnet.setSerial(b ? &Serial : nullptr);
}

void MyESP::_telnetCommand(char * commandLine) {
    char * str   = commandLine;
    bool   state = false;

    if (strlen(commandLine) == 0)
        return;

    // count the number of arguments
    unsigned wc = 0;
    while (*str) {
        if (*str == ' ' || *str == '\n' || *str == '\t') {
            state = false;
        } else if (state == false) {
            state = true;
            ++wc;
        }
        ++str;
    }

    // check first for reserved commands
    char * temp             = strdup(commandLine);         // because strotok kills original string buffer
    char * ptrToCommandName = strtok((char *)temp, " \n"); // space and newline

    // set command
    if (strcmp(ptrToCommandName, "set") == 0) {
        bool ok = false;
        if (wc == 1) {
            _printSetCommands();
            ok = true;
        } else if (wc == 2) { // set <something>
            char * setting = _telnet_readWord(false);
            ok             = _changeSetting(wc - 1, setting, nullptr);
        } else { // set <something> <values...>
            char * setting = _telnet_readWord(false);
            char * value   = _telnet_readWord(true); // allow strange characters
            ok             = _changeSetting(wc - 1, setting, value);
        }

        if (!ok) {
            myDebug_P(PSTR("")); // newline
            myDebug_P(PSTR("Unknown set command or wrong number of arguments."));
        }

        return;
    }

    // help command
    if ((strcmp(ptrToCommandName, "help") == 0) && (wc == 1)) {
        _consoleShowHelp();
        return;
    }

    // restart command
    if (((strcmp(ptrToCommandName, "restart") == 0) || (strcmp(ptrToCommandName, "reboot") == 0)) && (wc == 1)) {
        resetESP();
        return;
    }

    // print mqtt log command
    if ((strcmp(ptrToCommandName, "mqttlog") == 0) && (wc == 1)) {
        _printMQTTLog();
        return;
    }

    // show system stats
    if ((strcmp(ptrToCommandName, "system") == 0) && (wc == 1)) {
        showSystemStats();
        return;
    }

    // show system stats
    if ((strcmp(ptrToCommandName, "quit") == 0) && (wc == 1)) {
        myDebug_P(PSTR("[TELNET] exiting telnet session"));
        SerialAndTelnet.disconnectClient();
        return;
    }

#ifdef CRASH
    // crash command
    if ((strcmp(ptrToCommandName, "crash") == 0) && (wc >= 2)) {
        char * cmd = _telnet_readWord(false);
        if (strcmp(cmd, "dump") == 0) {
            crashDump();
        } else if (strcmp(cmd, "clear") == 0) {
            crashClear();
        } else {
            myDebug_P(PSTR("Error. Usage: crash <dump | clear | test [n]>"));
        }
        return; // don't call custom command line callback
    }
#endif

    // call callback function
    if (_telnetcommand_callback_f) {
        (_telnetcommand_callback_f)(wc, commandLine);
    }
}

// returns WiFi hostname as a String object
String MyESP::_getESPhostname() {
    String hostname;

#if defined(ARDUINO_ARCH_ESP32)
    hostname = String(WiFi.getHostname());
#else
    hostname = WiFi.hostname();
#endif

    return (hostname);
}

// takes the time from the gcc during compilation
char * MyESP::_getBuildTime() {
    const char time_now[] = __TIME__; // hh:mm:ss
    uint8_t    hour       = atoi(&time_now[0]);
    uint8_t    minute     = atoi(&time_now[3]);
    uint8_t    second     = atoi(&time_now[6]);

    const char   date_now[] = __DATE__; // Mmm dd yyyy
    const char * months[]   = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    uint8_t      month      = 0;
    for (int i = 0; i < 12; i++) {
        if (strncmp(date_now, months[i], 3) == 0) {
            month = i + 1;
            break;
        }
    }
    uint8_t  day  = atoi(&date_now[3]);
    uint16_t year = atoi(&date_now[7]);

    char buffer[30];
    snprintf_P(buffer, sizeof(buffer), PSTR("%04d-%02d-%02d %02d:%02d:%02d"), year, month, day, hour, minute, second);

    return (strdup(buffer));
}

// returns system uptime in seconds
unsigned long MyESP::_getUptime() {
    static uint32_t last_uptime      = 0;
    static uint8_t  uptime_overflows = 0;

    if (millis() < last_uptime) {
        ++uptime_overflows;
    }
    last_uptime             = millis();
    uint32_t uptime_seconds = uptime_overflows * (MYESP_UPTIME_OVERFLOW / 1000) + (last_uptime / 1000);

    return uptime_seconds;
}

// init RTC mem
void MyESP::_rtcmemInit() {
    memset((uint32_t *)RTCMEM_ADDR, 0, sizeof(uint32_t) * RTCMEM_BLOCKS);
    Rtcmem->magic = RTCMEM_MAGIC;
}

uint8_t MyESP::getSystemBootStatus() {
    system_rtcmem_t data;
    data.value = Rtcmem->sys;
    return data.parts.boot_status;
}

void MyESP::_setSystemBootStatus(uint8_t status) {
    system_rtcmem_t data;
    data.value             = Rtcmem->sys;
    data.parts.boot_status = status;
    Rtcmem->sys            = data.value;
    // myDebug("*** setting boot status to %d", data.parts.boot_status);
}

uint8_t MyESP::_getSystemStabilityCounter() {
    system_rtcmem_t data;
    data.value = Rtcmem->sys;
    return data.parts.stability_counter;
}

uint8_t MyESP::_getSystemDropoutCounter() {
    system_rtcmem_t data;
    data.value = Rtcmem->sys;
    return data.parts.dropout_counter;
}

void MyESP::_setSystemStabilityCounter(uint8_t counter) {
    system_rtcmem_t data;
    data.value                   = Rtcmem->sys;
    data.parts.stability_counter = counter;
    Rtcmem->sys                  = data.value;
}

void MyESP::_setSystemDropoutCounter(uint8_t counter) {
    system_rtcmem_t data;
    data.value                 = Rtcmem->sys;
    data.parts.dropout_counter = counter;
    Rtcmem->sys                = data.value;
}

void MyESP::_increaseSystemDropoutCounter() {
    system_rtcmem_t data;
    data.value = Rtcmem->sys;
    data.parts.dropout_counter++;
    Rtcmem->sys = data.value;
}

uint8_t MyESP::_getSystemResetReason() {
    system_rtcmem_t data;
    data.value = Rtcmem->sys;
    return data.parts.reset_reason;
}

void MyESP::_setSystemResetReason(uint8_t reason) {
    system_rtcmem_t data;
    data.value              = Rtcmem->sys;
    data.parts.reset_reason = reason;
    Rtcmem->sys             = data.value;
}

// system_get_rst_info() result is cached by the Core init for internal use
uint32_t MyESP::getSystemResetReason() {
    return resetInfo.reason;
}

void MyESP::_rtcmemSetup() {
    _rtcmem_status = _rtcmemStatus();
    if (!_rtcmem_status) {
        _rtcmemInit();
    }
}

void MyESP::_setCustomResetReason(uint8_t reason) {
    _setSystemResetReason(reason);
}

// returns false if not set and needs to be intialized, causing all rtcmem data to be wiped
bool MyESP::_rtcmemStatus() {
    bool readable;

    uint32_t reason = getSystemResetReason();

    // the last reset could have been caused by manually pressing the reset button
    // so before wiping, capture the boot sequence
    if (reason == REASON_EXT_SYS_RST) { // external system reset
        if (getSystemBootStatus() == MYESP_BOOTSTATUS_BOOTING) {
            _setSystemBootStatus(MYESP_BOOTSTATUS_RESETNEEDED);
             //_formatreq = true; // do a wipe next in the loop() - commented out for now because we use the web
        } else {
            _setSystemBootStatus(MYESP_BOOTSTATUS_POWERON);
        }
    }

    switch (reason) {
    //case REASON_EXT_SYS_RST:  // external system reset
    case REASON_WDT_RST:      // hardware watch dog reset
    case REASON_DEFAULT_RST:  // normal startup by power on
    case REASON_SOFT_WDT_RST: // Software watchdog
        readable = false;
        break;
    default:
        readable = true;
    }

    readable = readable and (RTCMEM_MAGIC == Rtcmem->magic);

    return readable;
}

bool MyESP::_getRtcmemStatus() {
    return _rtcmem_status;
}

uint8_t MyESP::_getCustomResetReason() {
    static uint8_t status = 255;
    if (status == 255) {
        if (_rtcmemStatus())
            status = _getSystemResetReason();
        if (status > 0)
            _setCustomResetReason(0);
        if (status > CUSTOM_RESET_MAX)
            status = 0;
    }
    return status;
}

void MyESP::_deferredReset(unsigned long delaytime, uint8_t reason) {
    _setSystemBootStatus(MYESP_BOOTSTATUS_POWERON);
    _setCustomResetReason(reason);
    delay(delaytime);
}

// Call this method on boot with stable=true to reset the crash counter
// Each call increments the counter
// If the counter reaches MYESP_SYSTEM_CHECK_MAX then the system is flagged as unstable
void MyESP::_setSystemCheck(bool stable) {
    uint8_t value = 0;

    if (stable) {
        value = 0; // system is ok
    } else {
        if (!_getRtcmemStatus()) {
            _setSystemStabilityCounter(1);
            return;
        }

        value = _getSystemStabilityCounter();

        if (++value > MYESP_SYSTEM_CHECK_MAX) {
            _systemStable = false;
            value         = 0; // system is unstable
            myDebug_P(PSTR("[SYSTEM] Warning, system UNSTABLE. Serial mode is enabled."));

            // enable Serial again
            if (!_general_serial) {
                SerialAndTelnet.setSerial(&Serial);
                _general_serial = true;
            }
        }
    }

    _setSystemStabilityCounter(value);
}

// return if system is stable (false=bad)
bool MyESP::_getSystemCheck() {
    return _systemStable;
}

// periodically check if system is stable
void MyESP::_systemCheckLoop() {
    static bool checked = false;
    if (!checked && (millis() > MYESP_SYSTEM_CHECK_TIME)) {
        _setSystemCheck(true); // Flag system as stable
        checked = true;
    }
}

// print out ESP system stats
// for battery power is ESP.getVcc()
void MyESP::showSystemStats() {
#if defined(ESP8266)
    myDebug_P(PSTR("%sESP8266 System stats:%s"), COLOR_BOLD_ON, COLOR_BOLD_OFF);
#else
    myDebug_P(PSTR("ESP32 System stats:"));
#endif
    myDebug_P(PSTR(""));

    myDebug_P(PSTR(" [APP] %s version: %s"), _app_name, _app_version);
    myDebug_P(PSTR(" [APP] MyESP version: %s"), MYESP_VERSION);
    myDebug_P(PSTR(" [APP] Build timestamp: %s"), _buildTime);

    // uptime
    uint32_t t = _getUptime(); // seconds

    uint32_t d   = t / 86400L;
    uint32_t h   = ((t % 86400L) / 3600L) % 60;
    uint32_t rem = t % 3600L;
    uint8_t  m   = rem / 60;
    uint8_t  s   = rem % 60;
    myDebug_P(PSTR(" [APP] Uptime: %d day%s %d hour%s %d minute%s %d second%s"),
              d,
              (d == 1) ? "" : "s",
              h,
              (h == 1) ? "" : "s",
              m,
              (m == 1) ? "" : "s",
              s,
              (s == 1) ? "" : "s");

    myDebug_P(PSTR(" [APP] System Load: %d%%"), getSystemLoadAverage());

    if (!_getSystemCheck()) {
        myDebug_P(PSTR(" [SYSTEM] Device is in SAFE MODE"));
    }

    if (isAPmode()) {
        myDebug_P(PSTR(" [WIFI] Device is in AP mode with SSID %s"), jw.getAPSSID().c_str());
    } else {
        myDebug_P(PSTR(" [WIFI] WiFi Hostname: %s"), _getESPhostname().c_str());
        myDebug_P(PSTR(" [WIFI] WiFi IP: %s"), WiFi.localIP().toString().c_str());
        myDebug_P(PSTR(" [WIFI] WiFi signal strength: %d%%"), getWifiQuality());
    }

    myDebug_P(PSTR(" [WIFI] WiFi MAC: %s"), WiFi.macAddress().c_str());

    if (isMQTTConnected()) {
        myDebug_P(PSTR(" [MQTT] is connected (heartbeat %s)"), getHeartbeat() ? "enabled" : "disabled");
    } else {
        myDebug_P(PSTR(" [MQTT] is disconnected"));
    }

    if (_ntp_enabled) {
        myDebug_P(PSTR(" [NTP] Time in UTC is %02d:%02d:%02d"), to_hour(now()), to_minute(now()), to_second(now()));
    }

#ifdef CRASH
    char output_str[80] = {0};
    char buffer[16]     = {0};
    myDebug_P(PSTR(" [EEPROM] EEPROM size: %u"), EEPROMr.reserved() * SPI_FLASH_SEC_SIZE);
    strlcpy(output_str, " [EEPROM] EEPROM Sector pool size is ", sizeof(output_str));
    strlcat(output_str, itoa(EEPROMr.size(), buffer, 10), sizeof(output_str));
    strlcat(output_str, ", and in use are: ", sizeof(output_str));
    for (uint32_t i = 0; i < EEPROMr.size(); i++) {
        strlcat(output_str, itoa(EEPROMr.base() - i, buffer, 10), sizeof(output_str));
        strlcat(output_str, " ", sizeof(output_str));
    }
    myDebug(output_str);
#endif

    myDebug_P(PSTR(" [SYSTEM] System is %s"), _getSystemCheck() ? "Stable" : "Unstable!");

#ifdef ARDUINO_BOARD
    myDebug_P(PSTR(" [SYSTEM] Board: %s"), ARDUINO_BOARD);
#endif

    myDebug_P(PSTR(" [SYSTEM] CPU frequency: %u MHz"), ESP.getCpuFreqMHz());
    myDebug_P(PSTR(" [SYSTEM] SDK version: %s"), ESP.getSdkVersion());

#if defined(ESP8266)
    myDebug_P(PSTR(" [SYSTEM] CPU chip ID: 0x%06X"), ESP.getChipId());
    myDebug_P(PSTR(" [SYSTEM] Core version: %s"), ESP.getCoreVersion().c_str());
    myDebug_P(PSTR(" [SYSTEM] Boot version: %d"), ESP.getBootVersion());
    myDebug_P(PSTR(" [SYSTEM] Boot mode: %d"), ESP.getBootMode());
    unsigned char reason = _getCustomResetReason();
    if (reason > 0) {
        char buffer[32];
        strcpy_P(buffer, custom_reset_string[reason - 1]);
        myDebug_P(PSTR(" [SYSTEM] Last reset reason: %s"), buffer);
    } else {
        myDebug_P(PSTR(" [SYSTEM] Last reset reason: %s"), (char *)ESP.getResetReason().c_str());
        myDebug_P(PSTR(" [SYSTEM] Last reset info: %s"), (char *)ESP.getResetInfo().c_str());
    }
    myDebug_P(PSTR(" [SYSTEM] Restart count: %d"), _getSystemStabilityCounter());
    myDebug_P(PSTR(" [SYSTEM] # TCP disconnects: %d"), _getSystemDropoutCounter());

    myDebug_P(PSTR(" [SYSTEM] rtcmem status: blocks:%u addr:0x%p"), RtcmemSize, Rtcmem);
    for (uint8_t block = 0; block < RtcmemSize; ++block) {
        myDebug_P(PSTR(" [SYSTEM] rtcmem %02u: %u"), block, reinterpret_cast<volatile uint32_t *>(RTCMEM_ADDR)[block]);
    }
#endif

    FlashMode_t mode = ESP.getFlashChipMode();
#if defined(ESP8266)
    myDebug_P(PSTR(" [FLASH] Flash chip ID: 0x%06X"), ESP.getFlashChipId());
#endif
    myDebug_P(PSTR(" [FLASH] Flash speed: %u Hz"), ESP.getFlashChipSpeed());
    myDebug_P(PSTR(" [FLASH] Flash mode: %s"), mode == FM_QIO ? "QIO" : mode == FM_QOUT ? "QOUT" : mode == FM_DIO ? "DIO" : mode == FM_DOUT ? "DOUT" : "UNKNOWN");
#if defined(ESP8266)
    myDebug_P(PSTR(" [FLASH] Flash size (CHIP): %d"), ESP.getFlashChipRealSize());
#endif
    myDebug_P(PSTR(" [FLASH] Flash size (SDK): %d"), ESP.getFlashChipSize());
    myDebug_P(PSTR(" [FLASH] Flash Reserved: %d"), 1 * SPI_FLASH_SEC_SIZE);
    myDebug_P(PSTR(" [MEM] Firmware size: %d"), ESP.getSketchSize());
    myDebug_P(PSTR(" [MEM] Max OTA size: %d"), (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000);
    myDebug_P(PSTR(" [MEM] OTA Reserved: %d"), 4 * SPI_FLASH_SEC_SIZE);

    uint32_t total_memory = _getInitialFreeHeap();
    uint32_t free_memory  = ESP.getFreeHeap();

    myDebug(" [MEM] Free Heap: %d bytes initially | %d bytes used (%2u%%) | %d bytes free (%2u%%)",
            total_memory,
            total_memory - free_memory,
            100 * (total_memory - free_memory) / total_memory,
            free_memory,
            100 * free_memory / total_memory);

    myDebug_P(PSTR(""));
}

/*
 * Send heartbeat via MQTT with all system data
 */
void MyESP::_heartbeatCheck(bool force = false) {
    static uint32_t last_heartbeat = 0;

    if ((millis() - last_heartbeat > MYESP_HEARTBEAT_INTERVAL) || force) {
        last_heartbeat = millis();

        // _printHeap("Heartbeat"); // for heartbeat debugging

        if (!isMQTTConnected() || !(_mqtt_heartbeat)) {
            return;
        }

        uint32_t total_memory  = _getInitialFreeHeap();
        uint32_t free_memory   = ESP.getFreeHeap();
        uint8_t  mem_available = 100 * free_memory / total_memory; // as a %

        StaticJsonDocument<200> doc;
        JsonObject              rootHeartbeat = doc.to<JsonObject>();

        rootHeartbeat["version"]         = _app_version;
        rootHeartbeat["IP"]              = WiFi.localIP().toString();
        rootHeartbeat["rssid"]           = getWifiQuality();
        rootHeartbeat["load"]            = getSystemLoadAverage();
        rootHeartbeat["uptime"]          = _getUptime();
        rootHeartbeat["freemem"]         = mem_available;
        rootHeartbeat["MQTTdisconnects"] = _getSystemDropoutCounter();

        char data[300] = {0};
        serializeJson(doc, data, sizeof(data));

        // myDebugLog("Publishing hearbeat via MQTT");

        (void)mqttPublish(MQTT_TOPIC_HEARTBEAT, data, false); // send to MQTT with retain off
    }
}

// handler for Telnet
void MyESP::_telnetHandle() {
    SerialAndTelnet.handle();

    static uint8_t charsRead = 0;
    // read asynchronously until full command input
    while (SerialAndTelnet.available()) {
        char c = SerialAndTelnet.read();

        if (c == 0)
            return;

        SerialAndTelnet.serialPrint(c); // echo to Serial (if connected)

        switch (c) {
        case '\r': // likely have full command in buffer now, commands are terminated by CR and/or LF
        case '\n':
            _command[charsRead] = '\0'; // null terminate our command char array

            if (charsRead > 0) {
                charsRead      = 0; // is static, so have to reset
                _suspendOutput = false;
                /*
                if (_general_serial) {
                    SerialAndTelnet.serialPrint('\n'); // force newline if in Serial
                }
                */
                SerialAndTelnet.write('\n'); // force NL

                _telnetCommand(_command);
            }
            break;
        case '\b': // (^H)
        case 0x7F: // (^?)
            if (charsRead > 0) {
                _command[--charsRead] = '\0';

                SerialAndTelnet.write(' ');
                SerialAndTelnet.write('\b');
            }
            break;
        case '?':
            if (!_suspendOutput) {
                _consoleShowHelp();
            } else {
                _command[charsRead++] = c; // add it to buffer as its part of the string entered
            }
            break;
        case 0x04: // EOT, CTRL-D
            myDebug_P(PSTR("[TELNET] exiting telnet session"));
            SerialAndTelnet.disconnectClient();
            break;
        default:
            _suspendOutput = true;
            if (charsRead < TELNET_MAX_COMMAND_LENGTH) {
                _command[charsRead++] = c;
            }
            _command[charsRead] = '\0'; // just in case
            break;
        }
    }
}

// make sure we have a connection to MQTT broker and the MQTT IP is set
void MyESP::_mqttConnect() {
    if ((!_mqtt_enabled) || (!_hasValue(_mqtt_ip))) {
        return; // MQTT not enabled
    }

    // Do not connect if already connected or still trying to connect
    if (mqttClient.connected() || _mqtt_connecting || (WiFi.status() != WL_CONNECTED)) {
        return;
    }

    // Check reconnect interval
    if (millis() - _mqtt_last_connection < _mqtt_reconnect_delay) {
        return;
    }

    _mqtt_connecting = true; // we're doing a connection

    // Increase the reconnect delay
    _mqtt_reconnect_delay += MQTT_RECONNECT_DELAY_STEP;
    if (_mqtt_reconnect_delay > MQTT_RECONNECT_DELAY_MAX) {
        _mqtt_reconnect_delay = MQTT_RECONNECT_DELAY_MAX;
    }

    mqttClient.setServer(_mqtt_ip, _mqtt_port);
    mqttClient.setClientId(_general_hostname);
    mqttClient.setKeepAlive(_mqtt_keepalive);
    mqttClient.setCleanSession(false);

    // last will
    if (_hasValue(_mqtt_will_topic)) {
        //myDebug_P(PSTR("[MQTT] Setting last will topic %s"), _mqttTopic(_mqtt_will_topic));
        mqttClient.setWill(_mqttTopic(_mqtt_will_topic), 1, true,
                           _mqtt_will_offline_payload); // retain always true
    }

    if (_hasValue(_mqtt_user)) {
        myDebug_P(PSTR("[MQTT] Connecting to MQTT using user %s..."), _mqtt_user);
        mqttClient.setCredentials(_mqtt_user, _mqtt_password);
    } else {
        myDebug_P(PSTR("[MQTT] Connecting to MQTT..."));
    }

    // Connect to the MQTT broker
    mqttClient.connect();
}

// Setup everything we need
void MyESP::setWIFI(wifi_callback_f callback) {
    // callback
    _wifi_callback_f = callback;
}

// init MQTT settings
void MyESP::setMQTT(mqtt_callback_f callback) {
    _mqtt_callback_f = callback; // callback
}

// builds up a topic by prefixing the base and hostname
char * MyESP::_mqttTopic(const char * topic) {
    static char buffer[MQTT_MAX_TOPIC_SIZE] = {0};

    if (_hasValue(_mqtt_base)) {
        strlcpy(buffer, _mqtt_base, sizeof(buffer));
        strlcat(buffer, "/", sizeof(buffer));
        strlcat(buffer, _general_hostname, sizeof(buffer));
    } else {
        strlcpy(buffer, _general_hostname, sizeof(buffer));
    }

    strlcat(buffer, "/", sizeof(buffer));
    strlcat(buffer, topic, sizeof(buffer));

    return buffer;
}

// validates a file in SPIFFS, loads it into the json buffer and returns true if ok
size_t MyESP::_fs_validateConfigFile(const char * filename, size_t maxsize, JsonDocument & doc) {
    // see if we can open it
    File file = SPIFFS.open(filename, "r");
    if (!file) {
        myDebug_P(PSTR("[FS] File %s not found"), filename);
        return 0;
    }

    // check size
    size_t size = file.size();

    // myDebug_P(PSTR("[FS] Checking file %s (%d bytes)"), filename, size); // remove for debugging

    if (size > maxsize) {
        file.close();
        myDebug_P(PSTR("[FS] Error. File %s size %d is too large (max %d)"), filename, size, maxsize);
        return 0;
    } else if (size == 0) {
        file.close();
        myDebug_P(PSTR("[FS] Error. Corrupted file %s"), filename);
        return 0;
    }

    // check integrity by reading file from SPIFFS into the char array
    char * buffer = new char[size + 2]; // reserve some memory to read in the file

    size_t real_size = file.readBytes(buffer, size);
    if (real_size != size) {
        file.close();
        myDebug_P(PSTR("[FS] Error. File %s sizes don't match (%d/%d), looks corrupted"), filename, real_size, size);
        delete[] buffer;
        return 0;
    }

    // now read into the given json
    DeserializationError error = deserializeJson(doc, buffer);
    if (error) {
        myDebug_P(PSTR("[FS] Error. Failed to deserialize json, error %s"), error.c_str());
        delete[] buffer;
        return 0;
    }

    // serializeJsonPretty(doc, Serial); // enable for debugging

    file.close();
    delete[] buffer;
    return size;
}

// validates the event log file in SPIFFS
// returns true if all OK
size_t MyESP::_fs_validateLogFile(const char * filename) {
    // exit if we have disabled logging
    if (!_general_log_events) {
        return 0;
    }

    // see if we can open it
    File eventlog = SPIFFS.open(filename, "r");
    if (!eventlog) {
        myDebug_P(PSTR("[FS] File %s not found"), filename);
        return 0;
    }

    // check sizes
    size_t size    = eventlog.size();
    size_t maxsize = ESP.getFreeHeap() - 2000; // reserve some buffer
    // myDebug_P(PSTR("[FS] Checking file %s (%d/%d bytes)"), filename, size, maxsize); // remove for debugging
    if (size > maxsize) {
        eventlog.close();
        myDebug_P(PSTR("[FS] File %s size %d is too large"), filename, size);
        return 0;
    } else if (size == 0) {
        eventlog.close();
        myDebug_P(PSTR("[FS] Corrupted file %s"), filename);
        return 0;
    }

    /*
    // check integrity by reading file from SPIFFS into the char array
    char * buffer = new char[size + 2]; // reserve some memory to read in the file
    size_t real_size = file.readBytes(buffer, size);
    if (real_size != size) {
        file.close();
        myDebug_P(PSTR("[FS] Error, file %s sizes don't match (%d/%d), looks corrupted"), filename, real_size, size);
        delete[] buffer;
        return false;
    }
    file.close();
    delete[] buffer;
    */

    /*
    File configFile = SPIFFS.open(filename, "r");
    myDebug_P(PSTR("[FS] File: "));
    while (configFile.available()) {
        SerialAndTelnet.print((char)configFile.read());
    }
    myDebug_P(PSTR("[FS] end")); // newline
    configFile.close();
    */

    // parse it to check JSON validity
    // its slow but the only reliable way to check integrity of the file
    uint16_t                                   char_count = 0;
    bool                                       abort      = false;
    char                                       char_buffer[MYESP_JSON_LOG_MAXSIZE];
    StaticJsonDocument<MYESP_JSON_LOG_MAXSIZE> doc;

    // eventlog.seek(0);
    while (eventlog.available() && !abort) {
        char c = eventlog.read(); // read a char

        // see if we have reached the end of the string
        if (c == '\0' || c == '\n') {
            char_buffer[char_count] = '\0'; // terminate and add it to the list
            // Serial.printf("Got line: %s\n", char_buffer); // for debugging
            // validate it by looking at JSON structure
            DeserializationError error = deserializeJson(doc, char_buffer);
            if (error) {
                myDebug_P(PSTR("[FS] Event log has a corrupted entry (error %s)"), error.c_str());
                abort = true;
            }
            char_count = 0; // start new record
        } else {
            // add the char to the buffer if recording, checking for overrun
            if (char_count < MYESP_JSON_LOG_MAXSIZE) {
                char_buffer[char_count++] = c;
            } else {
                abort = true; // reached limit of our line buffer
            }
        }
    }

    eventlog.close();

    if (abort) {
        return 0;
    }

    return size;
}

// format File System
void MyESP::_fs_eraseConfig() {
    myDebug_P(PSTR("[FS] Performing a factory reset..."));
    _formatreq = true;
}

void MyESP::setSettings(fs_loadsave_callback_f loadsave, fs_setlist_callback_f setlist, bool useSerial) {
    _fs_loadsave_callback_f = loadsave;
    _fs_setlist_callback_f  = setlist;
    _general_serial         = useSerial;
}

// load system config from SPIFFS
// returns false on error or the file needs to be recreated
bool MyESP::_fs_loadConfig() {
    // see if old file exists and delete it
    if (SPIFFS.exists("/config.json")) {
        SPIFFS.remove("/config.json");
        myDebug_P(PSTR("[FS] Removed old config version"));
    }

    StaticJsonDocument<MYESP_SPIFFS_MAXSIZE_CONFIG> doc;

    // set to true to print out contents of file
    size_t size = _fs_validateConfigFile(MYESP_CONFIG_FILE, MYESP_SPIFFS_MAXSIZE_CONFIG, doc);
    if (!size) {
        myDebug_P(PSTR("[FS] Error. Failed to open system config"));
        return false;
    }

    JsonObject network = doc["network"];
    _network_ssid      = strdup(network["ssid"] | "");
    _network_password  = strdup(network["password"] | "");
    _network_wmode     = network["wmode"]; // 0 is client, 1 is AP
    
    JsonObject general = doc["general"];
    _general_hostname   = strdup(general["hostname"]);
    _general_log_events = general["log_events"];

    // serial is only on when booting
#ifdef FORCE_SERIAL
    myDebug_P(PSTR("[FS] Serial is forced"));
    _general_serial = true;
#else
    _general_serial = general["serial"];
#endif

    JsonObject mqtt = doc["mqtt"];
    _mqtt_enabled   = mqtt["enabled"];
    _mqtt_heartbeat = mqtt["heartbeat"];
    _mqtt_ip        = strdup(mqtt["ip"] | "");
    _mqtt_user      = strdup(mqtt["user"] | "");
    _mqtt_port      = mqtt["port"] | MQTT_PORT;
    _mqtt_keepalive = mqtt["keepalive"] | MQTT_KEEPALIVE;
    _mqtt_retain    = mqtt["retain"];
    _mqtt_qos       = mqtt["qos"] | MQTT_QOS;
    _mqtt_password  = strdup(mqtt["password"] | "");
    _mqtt_base      = strdup(mqtt["base"] | MQTT_BASE_DEFAULT);

    JsonObject ntp = doc["ntp"];
    _ntp_server    = strdup(ntp["server"] | "");
    _ntp_interval  = ntp["interval"] | 60;
    if (_ntp_interval < 2)
        _ntp_interval = 60;
    _ntp_enabled = ntp["enabled"];

    myDebug_P(PSTR("[FS] System config loaded (%d bytes)"), size);

    return true;
}

// saves a string into a config setting, using default value if non set
// returns true if successful
bool MyESP::fs_setSettingValue(char ** setting, const char * value, const char * value_default) {
    if (*setting == nullptr) {
        free(*setting); // first free any allocated memory
    }

    if (_hasValue(value)) {
        *setting = strdup(value);
    } else {
        *setting = strdup(value_default); // use the default value
    }

    return true;
}

// saves a 2-byte short integer into a config setting, using default value if non set
// returns true if successful
bool MyESP::fs_setSettingValue(uint16_t * setting, const char * value, uint16_t value_default) {
    if (_hasValue(value)) {
        *setting = (uint16_t)atoi(value);
    } else {
        *setting = value_default; // use the default value
    }

    return true;
}

// saves an 8-bit integer into a config setting, using default value if non set
// returns true if successful
bool MyESP::fs_setSettingValue(uint8_t * setting, const char * value, uint8_t value_default) {
    if (_hasValue(value)) {
        *setting = (uint8_t)atoi(value);
    } else {
        *setting = value_default; // use the default value
    }

    return true;
}

// saves a bool into a config setting, using default value if non set
// returns true if successful
bool MyESP::fs_setSettingValue(bool * setting, const char * value, bool value_default) {
    if (_hasValue(value)) {
        if ((strcmp(value, "on") == 0) || (strcmp(value, "yes") == 0) || (strcmp(value, "1") == 0) || (strcmp(value, "true") == 0)) {
            *setting = true;
        } else if ((strcmp(value, "off") == 0) || (strcmp(value, "no") == 0) || (strcmp(value, "0") == 0) || (strcmp(value, "false") == 0)) {
            *setting = false;
        } else {
            return false; // invalid setting value
        }
    } else {
        *setting = value_default; // use the default value
    }

    return true;
}

// load custom settings
bool MyESP::_fs_loadCustomConfig() {
    StaticJsonDocument<MYESP_SPIFFS_MAXSIZE_CONFIG> doc;

    size_t size = _fs_validateConfigFile(MYESP_CUSTOMCONFIG_FILE, MYESP_SPIFFS_MAXSIZE_CONFIG, doc);
    if (!size) {
        myDebug_P(PSTR("[FS] Error. Failed to open custom config"));
        return false;
    }

    if (_fs_loadsave_callback_f) {
        const JsonObject & json = doc["settings"];
        if (!(_fs_loadsave_callback_f)(MYESP_FSACTION_LOAD, json)) {
            myDebug_P(PSTR("[FS] Error reading custom config"));
            return false;
        } else {
            myDebug_P(PSTR("[FS] Custom config loaded (%d bytes)"), size);
        }
    }

    return true;
}

// save custom config to spiffs
bool MyESP::fs_saveCustomConfig(JsonObject root) {
    bool ok = false;

    // call any custom functions before handling SPIFFS
    if (_ota_pre_callback_f) {
        (_ota_pre_callback_f)();
    }

    // open for writing
    File configFile = SPIFFS.open(MYESP_CUSTOMCONFIG_FILE, "w");
    if (!configFile) {
        myDebug_P(PSTR("[FS] Failed to open custom config for writing"));
        ok = false;
    } else {
        // Serialize JSON to file
        size_t n = serializeJson(root, configFile);
        configFile.close();

        if (n) {
            /*
        // reload the settings, not sure why?
        if (_fs_loadsave_callback_f) {
            if (!(_fs_loadsave_callback_f)(MYESP_FSACTION_LOAD, root)) {
                myDebug_P(PSTR("[FS] Error parsing custom config json"));
            }
        }
        */

            if (_general_log_events) {
                _writeEvent("INFO", "system", "Custom config stored in the SPIFFS", "");
            }

            // myDebug_P(PSTR("[FS] custom config saved"));
            ok = true;
        }
    }

    if (_ota_post_callback_f) {
        (_ota_post_callback_f)();
    }

    return ok;
}

// save system config to spiffs
bool MyESP::fs_saveConfig(JsonObject root) {
    bool ok = false;

    // call any custom functions before handling SPIFFS
    if (_ota_pre_callback_f) {
        (_ota_pre_callback_f)();
    }

    // open for writing
    File configFile = SPIFFS.open(MYESP_CONFIG_FILE, "w");
    if (!configFile) {
        myDebug_P(PSTR("[FS] Failed to open system config for writing"));
        ok = false;
    } else {
        // Serialize JSON to file
        size_t n = serializeJson(root, configFile);
        configFile.close();

        if (n) {
            if (_general_log_events) {
                _writeEvent("INFO", "system", "System config stored in the SPIFFS", "");
            }
            // myDebug_P(PSTR("[FS] system config saved"));
            ok = true;
        }

        // serializeJsonPretty(root, Serial); // for debugging
    }

    if (_ota_post_callback_f) {
        (_ota_post_callback_f)();
    }

    return ok;
}

// create an initial system config file using default settings
bool MyESP::_fs_writeConfig() {
    StaticJsonDocument<MYESP_SPIFFS_MAXSIZE_CONFIG> doc;
    JsonObject                                      root = doc.to<JsonObject>();

    root["command"] = "configfile"; // header, important!

    JsonObject network  = doc.createNestedObject("network");
    network["ssid"]     = _network_ssid;
    network["password"] = _network_password;
    network["wmode"]    = _network_wmode;

    JsonObject general    = doc.createNestedObject("general");
    general["password"]   = _general_password;
    general["serial"]     = _general_serial;
    general["hostname"]   = _general_hostname;
    general["log_events"] = _general_log_events;

    JsonObject mqtt   = doc.createNestedObject("mqtt");
    mqtt["enabled"]   = _mqtt_enabled;
    mqtt["heartbeat"] = _mqtt_heartbeat;
    mqtt["ip"]        = _mqtt_ip;
    mqtt["user"]      = _mqtt_user;
    mqtt["port"]      = _mqtt_port;
    mqtt["qos"]       = _mqtt_qos;
    mqtt["keepalive"] = _mqtt_keepalive;
    mqtt["retain"]    = _mqtt_retain;

    mqtt["password"] = _mqtt_password;
    mqtt["base"]     = _mqtt_base;

    JsonObject ntp  = doc.createNestedObject("ntp");
    ntp["server"]   = _ntp_server;
    ntp["interval"] = _ntp_interval;
    ntp["enabled"]  = _ntp_enabled;

    bool ok = fs_saveConfig(root); // save it

    return ok;
}

// create an empty json doc for the custom config and call callback to populate it
bool MyESP::_fs_createCustomConfig() {
    StaticJsonDocument<MYESP_SPIFFS_MAXSIZE_CONFIG> doc;
    JsonObject                                      root = doc.to<JsonObject>();

    root["command"] = "custom_configfile"; // header, important!

    if (_fs_loadsave_callback_f) {
        JsonObject settings = root.createNestedObject("settings");
        if (!(_fs_loadsave_callback_f)(MYESP_FSACTION_SAVE, settings)) {
            myDebug_P(PSTR("[FS] Error building custom config json"));
        }
    } else {
        myDebug_P(PSTR("[FS] Created custom config"));
    }

    bool ok = fs_saveCustomConfig(root);

    return ok;
}


// init the SPIFF file system and load the config
// if it doesn't exist try and create it
void MyESP::_fs_setup() {
    if (_ota_pre_callback_f) {
        (_ota_pre_callback_f)(); // call custom function
    }

    if (!SPIFFS.begin()) {
        myDebug_P(PSTR("[FS] Formatting filesystem..."));
        if (SPIFFS.format()) {
            if (_general_log_events) {
                _writeEvent("WARN", "system", "File system formatted", "");
            }
        } else {
            myDebug_P(PSTR("[FS] Failed to format file system"));
        }
    }

    // load the main system config file if we can. Otherwise create it and expect user to configure in web interface
    if (!_fs_loadConfig()) {
        myDebug_P(PSTR("[FS] Creating a new system config"));
        _fs_writeConfig(); // create the initial config file
    }

    // load system and custom config
    if (!_fs_loadCustomConfig()) {
        _fs_createCustomConfig(); // create the initial config file
    }

    /*
    // fill event log with tests
    SPIFFS.remove(MYESP_EVENTLOG_FILE);
    File fs = SPIFFS.open(MYESP_EVENTLOG_FILE, "w");
    fs.close();
    char logs[100];
    for (uint8_t i = 1; i < 143; i++) {
        sprintf(logs, "Record #%d", i);
        _writeEvent("WARN", "system", "test data", logs);
    }
    */

    // validate the event log. Sometimes it can can corrupted.
    size_t size = _fs_validateLogFile(MYESP_EVENTLOG_FILE);
    if (size) {
        myDebug_P(PSTR("[FS] Event log loaded (%d bytes)"), size);
    } else {
        myDebug_P(PSTR("[FS] Resetting event log"));
        SPIFFS.remove(MYESP_EVENTLOG_FILE);
        if (_general_log_events) {
            _writeEvent("WARN", "system", "Event Log", "Log was erased due to probable file corruption");
        }
    }

    if (_ota_post_callback_f) {
        (_ota_post_callback_f)(); // call custom function
    }
}

// returns load average as a %
uint32_t MyESP::getSystemLoadAverage() {
    return _load_average;
}

// calculate load average
void MyESP::_calculateLoad() {
    static uint32_t last_loadcheck    = 0;
    static uint32_t load_counter_temp = 0;
    load_counter_temp++;

    if (millis() - last_loadcheck > MYESP_LOADAVG_INTERVAL) {
        static uint32_t load_counter     = 0;
        static uint32_t load_counter_max = 1;

        load_counter      = load_counter_temp;
        load_counter_temp = 0;
        if (load_counter > load_counter_max) {
            load_counter_max = load_counter;
        }
        _load_average  = 100 - (100 * load_counter / load_counter_max);
        last_loadcheck = millis();
    }
}

// returns true is MQTT is alive
bool MyESP::isMQTTConnected() {
    return mqttClient.connected();
}

// return true if wifi is connected (client or AP mode)
bool MyESP::isWifiConnected() {
    return (_wifi_connected);
}

/*
   Return the quality (Received Signal Strength Indicator)
   of the WiFi network.
   Returns a number between 0 and 100 if WiFi is connected.
   Returns -1 if WiFi is disconnected.

   High quality: 90% ~= -55dBm
   Medium quality: 50% ~= -75dBm
   Low quality: 30% ~= -85dBm
   Unusable quality: 8% ~= -96dBm
*/
int MyESP::getWifiQuality() {
    if (WiFi.status() != WL_CONNECTED)
        return -1;
    int dBm = WiFi.RSSI();
    if (dBm <= -100)
        return 0;
    if (dBm >= -50)
        return 100;
    return 2 * (dBm + 100);
}

#ifdef CRASH
/**
 * Save crash information in EEPROM
 * This function is called automatically if ESP8266 suffers an exception
 * It should be kept quick / consise to be able to execute before hardware wdt may kick in
 */
extern "C" void custom_crash_callback(struct rst_info * rst_info, uint32_t stack_start, uint32_t stack_end) {
    // write crash time to EEPROM
    uint32_t crash_time = millis();
    EEPROMr.put(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_CRASH_TIME, crash_time);

    // write reset info to EEPROM
    EEPROMr.write(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_RESTART_REASON, rst_info->reason);
    EEPROMr.write(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_EXCEPTION_CAUSE, rst_info->exccause);

    // write epc1, epc2, epc3, excvaddr and depc to EEPROM
    EEPROMr.put(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_EPC1, rst_info->epc1);
    EEPROMr.put(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_EPC2, rst_info->epc2);
    EEPROMr.put(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_EPC3, rst_info->epc3);
    EEPROMr.put(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_EXCVADDR, rst_info->excvaddr);
    EEPROMr.put(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_DEPC, rst_info->depc);

    // write stack start and end address to EEPROM
    EEPROMr.put(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_STACK_START, stack_start);
    EEPROMr.put(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_STACK_END, stack_end);

    // write stack trace to EEPROM and avoid overwriting settings
    int16_t current_address = SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_STACK_TRACE;
    for (uint32_t i = stack_start; i < stack_end; i++) {
        byte * byteValue = (byte *)i;
        EEPROMr.write(current_address++, *byteValue);
    }

    EEPROMr.commit();
}

/**
 * Clears crash info
 */
void MyESP::crashClear() {
    myDebug_P(PSTR("[CRASH] Clearing crash dump"));
    uint32_t crash_time = 0xFFFFFFFF;
    EEPROMr.put(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_CRASH_TIME, crash_time);
    EEPROMr.commit();
}

/**
 * Print out crash information that has been previously saved in EEPROM
 * Copied from https://github.com/krzychb/EspSaveCrash
 */
void MyESP::crashDump() {
    uint32_t crash_time;
    EEPROMr.get(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_CRASH_TIME, crash_time);
    if ((crash_time == 0) || (crash_time == 0xFFFFFFFF)) {
        myDebug_P(PSTR("[CRASH] No crash data captured."));
        return;
    }

    uint32_t t   = crash_time / 1000; // convert to seconds
    uint32_t d   = t / 86400L;
    uint32_t h   = (t / 3600L) % 60;
    uint32_t rem = t % 3600L;
    uint8_t  m   = rem / 60;
    uint8_t  s   = rem % 60;
    myDebug_P(PSTR("[CRASH] Last crash was %d days %d hours %d minutes %d seconds since boot time"), d, h, m, s);

    // get reason and exception
    // https://www.espressif.com/sites/default/files/documentation/esp8266_reset_causes_and_common_fatal_exception_causes_en.pdf
    char buffer[80] = {0};
    char ss[16]     = {0};
    strlcpy(buffer, "[CRASH] Reason of restart: ", sizeof(buffer));

    uint8_t reason = EEPROMr.read(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_RESTART_REASON);
    switch (reason) {
    case REASON_WDT_RST:
        strlcat(buffer, "1 - Hardware WDT reset", sizeof(buffer));
        break;
    case REASON_EXCEPTION_RST:
        strlcat(buffer, "2 - Fatal exception", sizeof(buffer));
        break;
    case REASON_SOFT_WDT_RST:
        strlcat(buffer, "3 - Software watchdog reset", sizeof(buffer));
        break;
    case REASON_EXT_SYS_RST:
        strlcat(buffer, "6 - Hardware reset", sizeof(buffer));
        break;
    case REASON_SOFT_RESTART:
        strlcat(buffer, "4 - Software reset", sizeof(buffer));
        break;
    default:
        strlcat(buffer, itoa(reason, ss, 10), sizeof(buffer));
    }
    myDebug(buffer);

    // check for exception
    // see https://github.com/esp8266/Arduino/blob/master/doc/exception_causes.rst
    if (reason == REASON_EXCEPTION_RST) {
        // get exception cause
        uint8_t cause = EEPROMr.read(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_EXCEPTION_CAUSE);
        strlcpy(buffer, "[CRASH] Exception cause: ", sizeof(buffer));
        if (cause == 0) {
            strlcat(buffer, "0 - IllegalInstructionCause", sizeof(buffer));
        } else if (cause == 3) {
            strlcat(buffer, "3 - LoadStoreErrorCause", sizeof(buffer));
        } else if (cause == 6) {
            strlcat(buffer, "6 - IntegerDivideByZeroCause", sizeof(buffer));
        } else if (cause == 9) {
            strlcat(buffer, "9 - LoadStoreAlignmentCause", sizeof(buffer));
        } else {
            strlcat(buffer, itoa(cause, ss, 10), sizeof(buffer));
        }
    }
    myDebug(buffer);

    uint32_t epc1, epc2, epc3, excvaddr, depc;
    EEPROMr.get(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_EPC1, epc1);
    EEPROMr.get(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_EPC2, epc2);
    EEPROMr.get(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_EPC3, epc3);
    EEPROMr.get(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_EXCVADDR, excvaddr);
    EEPROMr.get(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_DEPC, depc);

    myDebug_P(PSTR("[CRASH] epc1=0x%08x epc2=0x%08x epc3=0x%08x"), epc1, epc2, epc3);
    myDebug_P(PSTR("[CRASH] excvaddr=0x%08x depc=0x%08x"), excvaddr, depc);

    uint32_t stack_start, stack_end;
    EEPROMr.get(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_STACK_START, stack_start);
    EEPROMr.get(SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_STACK_END, stack_end);

    myDebug_P(PSTR("[CRASH] sp=0x%08x end=0x%08x"), stack_start, stack_end);

    int16_t current_address = SAVE_CRASH_EEPROM_OFFSET + SAVE_CRASH_STACK_TRACE;
    int16_t stack_len       = stack_end - stack_start;

    uint32_t stack_trace;

    myDebug_P(PSTR(">>>stack>>>"));

    for (int16_t i = 0; i < stack_len; i += 0x10) {
        SerialAndTelnet.printf("%08x: ", stack_start + i);
        for (byte j = 0; j < 4; j++) {
            EEPROMr.get(current_address, stack_trace);
            SerialAndTelnet.printf("%08x ", stack_trace);
            current_address += 4;
        }
        SerialAndTelnet.println();
    }
    myDebug_P(PSTR("<<<stack<<<"));
    myDebug_P(PSTR("\nTo clean this dump use the command: %scrash clear%s\n"), COLOR_BOLD_ON, COLOR_BOLD_OFF);
}

#else
void MyESP::crashClear() {
}
void MyESP::crashDump() {
}
void MyESP::crashInfo() {
}
#endif

// write a log entry to SPIFFS
// assumes we have "log_events" on
void MyESP::_writeEvent(const char * type, const char * src, const char * desc, const char * data) {
    // this will also create the file if its doesn't exist
    File eventlog = SPIFFS.open(MYESP_EVENTLOG_FILE, "a");
    if (!eventlog) {
        //Serial.println("[SYSTEM] Error opening event log for writing"); // for debugging
        eventlog.close();
        return;
    }

    StaticJsonDocument<MYESP_JSON_LOG_MAXSIZE> root;
    root["type"] = type;
    root["src"]  = src;
    root["desc"] = desc;
    root["data"] = data;
    root["time"] = now(); // is relative if we're not using NTP

    // Serialize JSON to file
    size_t n = serializeJson(root, eventlog);
    eventlog.print("\n"); // this indicates end of the entry

    if (!n) {
        //Serial.println("[SYSTEM] Error writing to event log"); // for debugging
    }

    eventlog.close();
}

// read both system config and the custom config and send as json to web socket
bool MyESP::_fs_sendConfig() {
    File   configFile;
    size_t size;
    char   json[MYESP_SPIFFS_MAXSIZE_CONFIG] = {0};

    configFile = SPIFFS.open(MYESP_CONFIG_FILE, "r");
    if (!configFile) {
        myDebug_P(PSTR("[FS] No system config found to load"));
        return false;
    }
    size = configFile.size();

    // read file from SPIFFS into a char array
    if (configFile.readBytes(json, size) != size) {
        configFile.close();
        return false;
    }
    configFile.close();

    configFile = SPIFFS.open(MYESP_CUSTOMCONFIG_FILE, "r");
    if (!configFile) {
        myDebug_P(PSTR("[FS] No custom config found to load"));
        return false;
    }
    size = configFile.size();

    // read file from SPIFFS into the same char array
    memset(json, 0, MYESP_SPIFFS_MAXSIZE_CONFIG);
    if (configFile.readBytes(json, size) != size) {
        configFile.close();
        return false;
    }
    configFile.close();

    return true;
}

// print memory
void MyESP::_printHeap(const char * s) {
    uint32_t total_memory = _getInitialFreeHeap();
    uint32_t free_memory  = ESP.getFreeHeap();

    myDebug(" [%s] Free Heap: %d bytes initially | %d bytes used (%2u%%) | %d bytes free (%2u%%)",
            s,
            total_memory,
            total_memory - free_memory,
            100 * (total_memory - free_memory) / total_memory,
            free_memory,
            100 * free_memory / total_memory);
}

// print MQTT log - everything that was published last per topic
void MyESP::_printMQTTLog() {
    myDebug_P(PSTR("MQTT publish log:"));
    uint8_t i;

    for (i = 0; i < MYESP_MQTTLOG_MAX; i++) {
        if ((MQTT_log[i].topic != nullptr) && (MQTT_log[i].type == 1)) {
            myDebug_P(PSTR("  Topic:%s Payload:%s"), MQTT_log[i].topic, MQTT_log[i].payload);
        }
    }

    myDebug_P(PSTR("")); // newline
    myDebug_P(PSTR("MQTT subscriptions:"));

    for (i = 0; i < MYESP_MQTTLOG_MAX; i++) {
        if ((MQTT_log[i].topic != nullptr) && (MQTT_log[i].type == 2)) {
            if (_hasValue(MQTT_log[i].payload)) {
                myDebug_P(PSTR("  Topic:%s Last Payload:%s"), MQTT_log[i].topic, MQTT_log[i].payload);
            } else {
                myDebug_P(PSTR("  Topic:%s"), MQTT_log[i].topic);
            }
        }
    }

    myDebug_P(PSTR("")); // newline
}

// add an MQTT log entry to our buffer
// type 0=none, 1=publish, 2=subscribe
void MyESP::_addMQTTLog(const char * topic, const char * payload, const uint8_t type) {
    static uint8_t logCount   = 0;
    uint8_t        logPointer = 0;
    bool           found      = false;

    // myDebug("_addMQTTLog [#%d] %s (%d) [%s] (%d)", logCount, topic, strlen(topic), payload, strlen(payload)); // for debugging

    // find the topic
    // topics must be unique for either publish or subscribe
    while ((logPointer < MYESP_MQTTLOG_MAX) && (_hasValue(MQTT_log[logPointer].topic))) {
        if ((strcmp(MQTT_log[logPointer].topic, topic) == 0) && (MQTT_log[logPointer].type == type)) {
            found = true;
            break;
        }
        logPointer++;
    }

    // if not found add it and increment next free space pointer
    if (!found) {
        logPointer = logCount;
        if (++logCount == MYESP_MQTTLOG_MAX) {
            logCount = 0; // rotate
        }
    }

    // delete old record
    if (MQTT_log[logPointer].topic) {
        free(MQTT_log[logPointer].topic);
    }
    if (MQTT_log[logPointer].payload) {
        free(MQTT_log[logPointer].payload);
    }

    // and add new record
    MQTT_log[logPointer].type      = type; // 0=none, 1=publish, 2=subscribe
    MQTT_log[logPointer].topic     = strdup(topic);
    MQTT_log[logPointer].payload   = strdup(payload);
    MQTT_log[logPointer].timestamp = now();
}

// bootup sequence
// quickly flash LED until we get a Wifi connection, or AP established
void MyESP::_bootupSequence() {
    uint8_t boot_status = getSystemBootStatus();

    // check if its booted
    if (boot_status == MYESP_BOOTSTATUS_BOOTED) {
        if ((_ntp_enabled) && (now() > 10000) && !_have_ntp_time) {
            _have_ntp_time = true;
            if (_general_log_events) {
                _writeEvent("INFO", "system", "System booted", "");
            }
        }
        return;
    }

    // still starting up
    if (millis() <= MYESP_BOOTUP_DELAY) {
        return;
    }

    // only kick in after a few seconds
    if (boot_status == MYESP_BOOTSTATUS_POWERON) {
        _setSystemBootStatus(MYESP_BOOTSTATUS_BOOTING);
    }

    static uint32_t last_bootupflash = 0;

    // flash LED quickly
    if ((millis() - last_bootupflash > MYESP_BOOTUP_FLASHDELAY)) {
        last_bootupflash = millis();
        int state        = digitalRead(LED_BUILTIN);
        digitalWrite(LED_BUILTIN, !state);
    }

    if (isWifiConnected()) {
        _setSystemBootStatus(MYESP_BOOTSTATUS_BOOTED); // completed, reset flag
        digitalWrite(LED_BUILTIN, HIGH);               // turn off LED, 1=OFF with LED_BULLETIN

        // write a log message if we're not using NTP, otherwise wait for the internet time to arrive
        if (!_ntp_enabled) {
            if (_general_log_events) {
                _writeEvent("INFO", "system", "System booted", "");
            }
        }
    }
}

// setup MyESP
void MyESP::begin(const char * app_hostname, const char * app_name, const char * app_version, const char * app_url, const char * app_updateurl) {
    _general_hostname = strdup(app_hostname);
    _app_name         = strdup(app_name);
    _app_version      = strdup(app_version);
    _app_url          = strdup(app_url);
    _app_updateurl    = strdup(app_updateurl);

    _telnet_setup(); // Telnet setup, called first to set Serial

    // print a welcome message
    myDebug_P(PSTR("\n\n* %s version %s"), _app_name, _app_version);

    // set up onboard LED
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);

    _getInitialFreeHeap(); // get initial free mem
    _rtcmemSetup();        // rtc internal mem setup
    _eeprom_setup();       // set up EEPROM for storing crash data, if compiled with -DCRASH
    _fs_setup();           // SPIFFS setup, do this first to get values
    _wifi_setup();         // WIFI setup
    _ota_setup();          // init OTA

    _setSystemCheck(false); // reset system check
    _heartbeatCheck(true);  // force heartbeat

    _setSystemDropoutCounter(0); // reset # TCP dropouts

    SerialAndTelnet.flush();
}

/*
 * Loop. This is called as often as possible and it handles wifi, telnet, mqtt etc
 */
void MyESP::loop() {
    _calculateLoad();
    _systemCheckLoop();
    _heartbeatCheck();
    _bootupSequence(); // see if a reset was pressed during bootup

    jw.loop();           // WiFi
    ArduinoOTA.handle(); // OTA

    ESP.wdtFeed();   // feed the watchdog...
    _telnetHandle(); // telnet
    ESP.wdtFeed();   // feed the watchdog...

    _mqttConnect(); // MQTT

    if (_formatreq) {
        myDebug("[SYSTEM] Factory reset initiated. Please wait. System will automatically restart when complete...");
        SPIFFS.end();
        SPIFFS.format();
        _deferredReset(500, CUSTOM_RESET_FACTORY);
        ESP.restart();
    }

    if (_shouldRestart) {
        if (_general_log_events) {
            _writeEvent("INFO", "system", "System is restarting", "");
        }
        myDebug("[SYSTEM] Restarting...");
        _deferredReset(500, CUSTOM_RESET_TERMINAL);
        ESP.restart();
    }

    yield(); // ... and breath.
}

MyESP myESP;
