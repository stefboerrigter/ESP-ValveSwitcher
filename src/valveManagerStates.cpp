#include "valveManagerStates.h"

#include <Wire.h>
#include <Arduino.h>
#include "utils.h"

#define I2C_ADDR 0 // -> Address 20; MCP23017 with all three address lines low.

// Interrupts from the MCP will be handled by this PIN
#define INTERRUPT_PIN D7 //GPIO13 - D7
//I2C -> D4 & D5?

// MCP23017 setup

typedef enum PORTS {
    SIG_OPEN_1 = 7,
    SIG_CLOSE_1 = 6,
    SIG_OPEN_2 = 5,
    SIG_CLOSE_2 = 4,
    SIG_OPEN_3 = 3,
    SIG_CLOSE_3 = 2,
    SIG_OPEN_4 = 1,
    SIG_CLOSE_4 = 0,
    SIG_INPUT_START = SIG_CLOSE_4,
    SIG_INPUT_MAX = SIG_OPEN_1,
} port_input_t;

typedef enum OUTPUT_PORTS {
    SIG_IN_CLOSE_1 = 8,
    SIG_IN_OPEN_1 = 9,
    SIG_IN_CLOSE_2 = 10,
    SIG_IN_OPEN_2 = 11,
    SIG_IN_CLOSE_3 = 12,
    SIG_IN_OPEN_3 = 13,
    SIG_IN_CLOSE_4 = 14,
    SIG_IN_OPEN_4 = 15,
    
    SIG_OUTPUT_START = SIG_IN_CLOSE_1,
    SIG_OUTUPT_MAX = SIG_IN_OPEN_4,
} port_output_t;


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
    int pin = 0;

    pinMode(INTERRUPT_PIN,INPUT); //pin for ESP
    //Configure I2C
    manager.mcp.begin(I2C_ADDR, 4, 5); //PIN4 & 4 is SD

    //initialize output pins
    for(pin = SIG_OUTPUT_START; pin <= SIG_OUTPUT_START; pin++)
    {
        manager.mcp.pinMode(pin, OUTPUT); 
        manager.mcp.digitalWrite(pin,LOW);
    }

    //initialize input pins
    for(pin = SIG_INPUT_START; pin <= SIG_INPUT_MAX; pin++)
    {
        manager.mcp.pinMode(pin,INPUT);             // Button i/p to GND
        manager.mcp.pullUp(pin,HIGH);               // Puled high ~100k
        manager.mcp.setupInterruptPin(pin,CHANGE);  // Generate interrupt on change
    }

    //Setup interrupts
    // On interrupt, polariy is set HIGH/LOW (last parameter).
    manager.mcp.setupInterrupts(MCP_INT_MIRROR, MCP_INT_ODR, LOW); // The mcp output interrupt pin.
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
        manager.mcp.digitalWrite(SIG_IN_CLOSE_1, HIGH);
        manager.mcp.digitalWrite(SIG_CLOSE_2, LOW);
        delay(1000);
    }
    else
    {
        manager.mcp.digitalWrite(SIG_IN_CLOSE_1, LOW);
        manager.mcp.digitalWrite(SIG_CLOSE_2, HIGH);
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
    if ( p==SIG_IN_CLOSE_1 && v == 1) 
    { //  Test for release - pin pulled high
        if ( ledState ) 
        {
            manager.mcp.digitalWrite(SIG_OPEN_1, LOW);
        } 
        else 
        {
            manager.mcp.digitalWrite(SIG_OPEN_1, HIGH);
        }

        ledState = ! ledState;
    }
    else{
        myDebug("[ValveMGR] Interrupt %d - %d", p, v);
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