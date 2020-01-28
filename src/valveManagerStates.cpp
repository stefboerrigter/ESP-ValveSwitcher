#include "valveManagerStates.h"

#include <Wire.h>
#include <Arduino.h>
#include "utils.h"

#define I2C_ADDR 0 // -> Address 20; MCP23017 with all three address lines low.

// Interrupts from the MCP will be handled by this PIN
#define INTERRUPT_PIN D7 //GPIO13 - D7
//I2C -> D4 & D5?

// MCP23017 setup
#define MCP_LED1 7
#define MCP_INPUTPIN 8
#define MCP_INPUTPIN2 10
#define MCP_LEDTOG1 11
#define MCP_LEDTOG2 4
// Register bits
#define MCP_INT_MIRROR true  // Mirror inta to intb.
#define MCP_INT_ODR    false // Open drain.

void ICACHE_RAM_ATTR isr();

AbstractState::~AbstractState(){

}

void AbstractState::setState(ValveManager &manager, AbstractState *state)
{
    AbstractState *aux = manager.m_state;
    manager.m_state = state;
    delete aux;
}

/////////////////////////////////////////////////////////////////////////////////////////////////
//  Initialize
//
//  Responsible for initializing the MCP, open Communication and set to a default initialize state
//
/////////////////////////////////////////////////////////////////////////////////////////////////
void ValveManagerInitialize::Process(ValveManager &manager)
{
    pinMode(INTERRUPT_PIN,INPUT); //pin 3?
    manager.mcp.begin(I2C_ADDR, 4, 5); //PIN4 & 4 is SD

    manager.mcp.pinMode(MCP_LEDTOG1, OUTPUT);  // Toggle LED 1
    manager.mcp.pinMode(MCP_LEDTOG2, OUTPUT);  // Toggle LED 2
    
    manager.mcp.pinMode(MCP_LED1, OUTPUT);     // LED output
    manager.mcp.digitalWrite(MCP_LED1,LOW);
    
    // This Input pin provides the interrupt source.
    manager.mcp.pinMode(MCP_INPUTPIN,INPUT);   // Button i/p to GND
    manager.mcp.pullUp(MCP_INPUTPIN,HIGH);     // Puled high ~100k

    // Show a different value for interrupt capture data register = debug.
    manager.mcp.pinMode(MCP_INPUTPIN2,INPUT);   // Button i/p to GND
    manager.mcp.pullUp(MCP_INPUTPIN2,HIGH);     // Puled high ~100k

    // On interrupt, polariy is set HIGH/LOW (last parameter).
    manager.mcp.setupInterrupts(MCP_INT_MIRROR, MCP_INT_ODR, LOW); // The mcp output interrupt pin.
    manager.mcp.setupInterruptPin(MCP_INPUTPIN,CHANGE); // The mcp  action that causes an interrupt.

    manager.mcp.setupInterruptPin(MCP_INPUTPIN2,CHANGE); // No button connected, see effect on code=none.

    manager.mcp.digitalWrite(MCP_LED1,LOW);

    manager.mcp.readGPIOAB(); // Initialise for interrupts.

    attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN), &ValveManager::handle_isr, FALLING); // Enable Arduino interrupt control.
    myDebug_P(PSTR("[ValveMGR] Initialized"));
    //go to operational state..
    this->setState(manager, new ValveManagerOperational());
}

void ValveManagerInitialize::HandleIsr(ValveManager &manager){

}

ValveManagerInitialize::~ValveManagerInitialize(){}

/////////////////////////////////////////////////////////////////////////////////////////////////
//  Operational
//
//  Responsible for operating the valves as designed..
//
/////////////////////////////////////////////////////////////////////////////////////////////////
void ValveManagerOperational::Process(ValveManager &manager){
    if(manager.getLedsEnabled())
    {
        manager.mcp.digitalWrite(MCP_LEDTOG1, HIGH);
        manager.mcp.digitalWrite(MCP_LEDTOG2, LOW);
        delay(1000);
    }
    else
    {
        manager.mcp.digitalWrite(MCP_LEDTOG1, LOW);
        manager.mcp.digitalWrite(MCP_LEDTOG2, HIGH);
        delay(1000);
    }
    manager.toggleLedsEnabled();
}

void ValveManagerOperational::HandleIsr(ValveManager &manager)
{
    uint8_t p,v;
    static uint16_t ledState=0;

    noInterrupts();

    // Debounce. Slow I2C: extra debounce between interrupts anyway.
    // Can not use delay() in interrupt code.
    delayMicroseconds(1000); 

    // Stop interrupts from external pin.
    detachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN));

    interrupts(); // re-start interrupts for mcp
    p = manager.mcp.getLastInterruptPin();
    // This one resets the interrupt state as it reads from reg INTCAPA(B).
    v = manager.mcp.getLastInterruptPinValue();

    // Here either the button has been pushed or released.
    if ( p==MCP_INPUTPIN && v == 1) 
    { //  Test for release - pin pulled high
        if ( ledState ) 
        {
            manager.mcp.digitalWrite(MCP_LED1, LOW);
        } 
        else 
        {
            manager.mcp.digitalWrite(MCP_LED1, HIGH);
        }

        ledState = ! ledState;
    }
    else{
        myDebug("[ValveMGR] Interrupt %d", p);
    }

    attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN), &ValveManager::handle_isr ,FALLING); // Reinstate interrupts from external pin.
}

ValveManagerOperational::~ValveManagerOperational(){}

/////////////////////////////////////////////////////////////////////////////////////////////////
//  Validate
//
//  Responsible for validating if the valves still function as expected 
//  Start every valve, rotate it and verify it signals Open & Close state..
//
/////////////////////////////////////////////////////////////////////////////////////////////////
void ValveManagerValidate::Process(ValveManager &manager){

}

void ValveManagerValidate::HandleIsr(ValveManager &manager){

}

ValveManagerValidate::~ValveManagerValidate(){}

/////////////////////////////////////////////////////////////////////////////////////////////////
//  Error
//
//  Responsible for handling ERROR state of valves
//
/////////////////////////////////////////////////////////////////////////////////////////////////
void ValveManagerError::Process(ValveManager &manager){

}

void ValveManagerError::HandleIsr(ValveManager &manager){

}

ValveManagerError::~ValveManagerError(){}