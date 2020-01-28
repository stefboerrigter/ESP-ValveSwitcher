#pragma once

#include "valveManagerStates.h"
#include <stddef.h>
#include <list>
#include <Adafruit_MCP23017.h>
#include "valve.h"

typedef enum OUTPUT_PORTS {
    SIG_OUT_OPEN_1 = 7,
    SIG_OUT_CLOSE_1 = 6,
    SIG_OUT_OPEN_2 = 5,
    SIG_OUT_CLOSE_2 = 4,
    SIG_OUT_OPEN_3 = 3,
    SIG_OUT_CLOSE_3 = 2,
    SIG_OUT_OPEN_4 = 1,
    SIG_OUT_CLOSE_4 = 0,
    SIG_OUTPUT_START = SIG_OUT_CLOSE_4,
    SIG_OUTPUT_MAX = SIG_OUT_OPEN_1,
} port_output_t;

typedef enum INPUT_PORTS {
    SIG_IN_CLOSE_1 = 8,
    SIG_IN_OPEN_1 = 9,
    SIG_IN_CLOSE_2 = 10,
    SIG_IN_OPEN_2 = 11,
    SIG_IN_CLOSE_3 = 12,
    SIG_IN_OPEN_3 = 13,
    SIG_IN_CLOSE_4 = 14,
    SIG_IN_OPEN_4 = 15,
    
    SIG_INPUT_START = SIG_IN_CLOSE_1,
    SIG_INPUT_MAX = SIG_IN_OPEN_4,
} port_input_t;

class AbstractState;

class ValveManager {   
    friend class AbstractState;
    public:
        ValveManager();
        ~ValveManager();
        void Process();
        void Initialize();
        
        bool getLedsEnabled();
        void toggleLedsEnabled();
        Valve * getValve(VALVE_TYPE type);
        Valve * getValve(int interrupt_pin, int value);

        //semi-private... TBD
        static void ICACHE_RAM_ATTR handle_isr();
        static ValveManager *pInst;
        Adafruit_MCP23017 mcp;


    private:
        std::list<Valve *> m_valves;
        bool m_leds_enabled;
        AbstractState* m_state;
};

