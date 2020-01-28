#pragma once
#include <string>
#include <Adafruit_MCP23017.h>

typedef enum VALVE_TYPE{
    VALVE_LIVINGROOM = 0,
    VALVE_UPSTAIRS,
    VALVE_BATHROOM,
    VALVES_MAX
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
} valve_status_t;


class Valve{
    public:
        Valve(const valve_struct_t *pValve_type, int out_open, int out_close, int in_open, int in_close, Adafruit_MCP23017 *pMcp);
        ~Valve();
        void openValve(Adafruit_MCP23017 *pIOExpander);
        void closeValve(Adafruit_MCP23017 *pIOExpander);

        valve_status_t getValveStatus();
        valve_t getType();
        bool hasInterruptPin(int interruptPin, int value);
        void interruptSignaled(int pin);
    private:
        valve_t m_type;
        std::string m_name;
        int m_pin_open;
        int m_pin_close;
        int m_input_open;
        int m_input_close;
        valve_status_t m_status;
        Adafruit_MCP23017 *pIOExpander;
};