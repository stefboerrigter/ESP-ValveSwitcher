#pragma once

#include "valveManager.h"
#include <stddef.h>
#include <Wire.h>
#include <Arduino.h>
#include "utils.h"

#define I2C_ADDR 0 // -> Address 20; MCP23017 with all three address lines low.

// Interrupts from the MCP will be handled by this PIN
#define INTERRUPT_PIN D7 //GPIO13 - D7
//I2C -> D4 & D5?

// MCP23017 setup

// Register bits
#define MCP_INT_MIRROR true  // Mirror inta to intb.
#define MCP_INT_ODR    false // Open drain.

class ValveManager;

class AbstractState {
    public:
        virtual void Process(ValveManager &manager) = 0;
        virtual void HandleIsr(ValveManager &manager) = 0;
        virtual ~AbstractState();
    protected:
        void setState(ValveManager &manager, AbstractState *state);
        
};