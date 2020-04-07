#include "ValveManagerStateInitialize.h"
#include "ValveManagerStateValidate.h"

/////////////////////////////////////////////////////////////////////////////////////////////////
//  Initialize
//
//  Responsible for initializing the MCP, open Communication and set to a default initialize state
//
/////////////////////////////////////////////////////////////////////////////////////////////////
ValveManagerStateInitialize::ValveManagerStateInitialize(ValveManager &manager)
{
    //myDebug("[ValveMGRInit] Entered Initialization state");
}

//---------------------------------------------------------------------------------
void ValveManagerStateInitialize::Process(ValveManager &manager)
{
    int pin = 0;

    pinMode(INTERRUPT_PIN,INPUT); //pin for ESP
    //Configure I2C
    manager.mcp.begin(I2C_ADDR, 4, 5); //PIN4 & 4 is SD

    //initialize output pins
    for(pin = SIG_OUTPUT_START; pin <= SIG_OUTPUT_MAX; pin++)
    {
        //myDebug_P(PSTR("[ValveMGR] Pin %d Output"), pin);    
        manager.mcp.pinMode(pin, OUTPUT); 
        manager.mcp.digitalWrite(pin,LOW);
    }

    //initialize input pins
    for(pin = SIG_INPUT_START; pin <= SIG_INPUT_MAX; pin++)
    {
        //myDebug_P(PSTR("[ValveMGR] Pin %d Input"), pin);    
        manager.mcp.pinMode(pin,INPUT);             // Button i/p to GND
        manager.mcp.pullUp(pin,HIGH);               // Puled high ~100k
        manager.mcp.setupInterruptPin(pin,CHANGE);  // Generate interrupt on change
    }
    //https://www.best-microcontroller-projects.com/mcp23017-interrupt.html
    
    //Setup interrupts,// On interrupt, polariy is set HIGH/LOW (last parameter).
    manager.mcp.setupInterrupts(MCP_INT_MIRROR, MCP_INT_ODR, LOW); // The mcp output interrupt pin.
    manager.mcp.readGPIOAB(); // Initialise for interrupts.
    
    attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN), &ValveManager::handle_isr, FALLING); // Enable Arduino interrupt control.
    myDebug_P(PSTR("[ValveMGRInit] Initialized"));
    //go to operational state..
    setState(manager, new ValveManagerStateValidate(manager));
}

//---------------------------------------------------------------------------------
void ValveManagerStateInitialize::HandleIsr(ValveManager &manager)
{
    //myDebug_P(PSTR("[ValveMGRInit] ISR not implemented"));
}

//---------------------------------------------------------------------------------
void ValveManagerStateInitialize::onValveActionComplete(ValveManager &manager, Valve *pValve)
{
    //myDebug("[ValveMGRInit] No onValveActionComplete implemented");
}

//---------------------------------------------------------------------------------
ValveManagerStateInitialize::~ValveManagerStateInitialize()
{
    myDebug("[ValveMGRInit] Leave Initialization state");
}

