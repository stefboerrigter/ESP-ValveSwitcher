#pragma once

#include "valveManagerStates.h"
#include <stddef.h>

#include <Adafruit_MCP23017.h>

class AbstractState;

class ValveManager {   
    friend class AbstractState;
    public:
        ValveManager();
        ~ValveManager();
        void Process();
        
        bool getLedsEnabled();
        void toggleLedsEnabled();

        //semi-private... TBD
        static void handle_isr();
        static ValveManager *pInst;
        Adafruit_MCP23017 mcp;

    private:
        bool m_leds_enabled;
        AbstractState* m_state;
};

