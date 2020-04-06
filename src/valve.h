#pragma once
#include <string>
#include <Adafruit_MCP23017.h>


#include <stdlib.h>
#include <stdio.h>
#include <functional>
#if __cplusplus <= 199711L
  #error This file needs at least a C++11 compliant compiler, try using:
  #error    $ g++ -std=c++11 ..
#endif

typedef enum VALVE_TYPE{
    VALVE_LIVINGROOM = 0,
    VALVE_UPSTAIRS,
    VALVE_BATHROOM,
    VALVE_MAX,
    VALVE_TYPE_FIRST = VALVE_LIVINGROOM,
    VALVE_TYPE_LAST = VALVE_MAX,
} valve_t;

typedef struct VALVE_OBJECT{
    VALVE_TYPE valve_type;
    std::string valve_name;
} valve_struct_t;

typedef enum {
    VALVE_INIT,
    VALVE_OPEN,
    VALVE_CLOSED,
    VALVE_BUSY_OPENING,
    VALVE_BUSY_CLOSING,
    VALVE_ERROR,
    VALVE_NOT_CONNECTED,
} valve_status_t;

class ValveManager;

class Valve
{
    public:
        Valve(const valve_struct_t *pValve_type, int out_open, int out_close, 
            int in_open, int in_close, Adafruit_MCP23017 &pMcp);

        ~Valve();
        void openValve();
        void closeValve();

        valve_status_t getValveStatus();
        void setValveStatus(valve_status_t status);

        valve_t getType();
        bool hasInterruptPin(int interruptPin, int value);
        void interruptSignaled(int pin, int value);
        std::string valve_status_to_string(valve_status_t status);
        const char *toString();
        const char *getName();
        
        void addOnCompleteHandler(std::function<int(Valve *pValve)> callback);

    private:
        valve_t m_type;
        int m_pin_open;
        int m_pin_close;
        int m_input_open;
        int m_input_close;
        valve_status_t m_status;
        Adafruit_MCP23017 &pIOExpander;
        std::string m_name;
        std::function<int(Valve *pValve)> fActionComplete;
};