#include "valves.h"
#include <Wire.h>
#include <Arduino.h>

//https://www.best-microcontroller-projects.com/mcp23017.html

#define I2C_ADDR 0

// Interrupts from the MCP will be handled by this PIN
#define INTERRUPT_PIN 3

// MCP23017 setup
#define MCP_LED1 7
#define MCP_INPUTPIN 8
#define MCP_INPUTPIN2 10
#define MCP_LEDTOG1 11
#define MCP_LEDTOG2 4
// Register bits
#define MCP_INT_MIRROR true  // Mirror inta to intb.
#define MCP_INT_ODR    false // Open drain.

#include <Adafruit_MCP23017.h>
Adafruit_MCP23017 mcp;

void isr();

valves::valves()
{

}

valves::~valves()
{

}

//////////////////////////////////////////////
void valves::initialize()
{
    pinMode(INTERRUPT_PIN,INPUT); //pin 3?
    mcp.begin(I2C_ADDR);

    mcp.pinMode(MCP_LEDTOG1, OUTPUT);  // Toggle LED 1
    mcp.pinMode(MCP_LEDTOG2, OUTPUT);  // Toggle LED 2
    
    mcp.pinMode(MCP_LED1, OUTPUT);     // LED output
    mcp.digitalWrite(MCP_LED1,LOW);
    
    // This Input pin provides the interrupt source.
    mcp.pinMode(MCP_INPUTPIN,INPUT);   // Button i/p to GND
    mcp.pullUp(MCP_INPUTPIN,HIGH);     // Puled high ~100k

    // Show a different value for interrupt capture data register = debug.
    mcp.pinMode(MCP_INPUTPIN2,INPUT);   // Button i/p to GND
    mcp.pullUp(MCP_INPUTPIN2,HIGH);     // Puled high ~100k

    // On interrupt, polariy is set HIGH/LOW (last parameter).
    mcp.setupInterrupts(MCP_INT_MIRROR, MCP_INT_ODR, LOW); // The mcp output interrupt pin.
    mcp.setupInterruptPin(MCP_INPUTPIN,CHANGE); // The mcp  action that causes an interrupt.

    mcp.setupInterruptPin(MCP_INPUTPIN2,CHANGE); // No button connected, see effect on code=none.

    mcp.digitalWrite(MCP_LED1,LOW);

    mcp.readGPIOAB(); // Initialise for interrupts.

    attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN),isr,FALLING); // Enable Arduino interrupt control.
}
//////////////////////////////////////////////
void valves::Process(){
  
  delay(300);

  mcp.digitalWrite(MCP_LEDTOG1, HIGH);
  mcp.digitalWrite(MCP_LEDTOG2, LOW);
 
  delay(300);

  mcp.digitalWrite(MCP_LEDTOG1, LOW);
  mcp.digitalWrite(MCP_LEDTOG2, HIGH);
  
}


//////////////////////////////////////////////
// The interrupt routine handles LED1
// This is the button press since this is the only active interrupt.
void isr()
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

    p = mcp.getLastInterruptPin();
    // This one resets the interrupt state as it reads from reg INTCAPA(B).
    v = mcp.getLastInterruptPinValue();

    // Here either the button has been pushed or released.
    if ( p==MCP_INPUTPIN && v == 1) 
    { //  Test for release - pin pulled high
        if ( ledState ) 
        {
            mcp.digitalWrite(MCP_LED1, LOW);
        } 
        else 
        {
            mcp.digitalWrite(MCP_LED1, HIGH);
        }

        ledState = ! ledState;
    }

    attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN),isr,FALLING); // Reinstate interrupts from external pin.
}