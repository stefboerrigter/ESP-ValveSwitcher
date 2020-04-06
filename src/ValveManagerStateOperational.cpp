#include "ValveManagerStateOperational.h"
#include "ValveManagerStateError.h"

/////////////////////////////////////////////////////////////////////////////////////////////////
//  Operational
//
//  Responsible for operating the valves as designed..
//
/////////////////////////////////////////////////////////////////////////////////////////////////
ValveManagerStateOperational::ValveManagerStateOperational()
{
    myDebug("[ValveMGROp] Entered Operational State");
}

//---------------------------------------------------------------------------------
void ValveManagerStateOperational::Process(ValveManager &manager)
{
    //keep track of active valves...
    int iterator;
    VALVE_TYPE type = VALVE_LIVINGROOM;
    for(type = VALVE_TYPE_FIRST; type < VALVE_TYPE_LAST; )
    {
        
        Valve *pValve = manager.getValve(type);
        valve_status_t status = pValve->getValveStatus();
        switch(status)
        {
            case VALVE_INIT:
                pValve->closeValve();//close on initialization
                break;
            case VALVE_ERROR:
                //signal report to MQTT & Telnet
                myDebug("[ValveMGROp] Valve Error %d", type);
                setState(manager, new ValveManagerStateError());
                break;
            default:
                //do nothing here
                break;
        }
        iterator = static_cast<int>(type);
        type = static_cast<VALVE_TYPE>(++iterator);
    }
}

void ValveManagerStateOperational::HandleIsr(ValveManager &manager)
{
    int p,v;

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
    
    Valve *pValve = manager.getValve(p, v);
    //myDebug("[ValveMGROp] No pInsInterrupt %d - %d", p, v);
    if(pValve != NULL)
    {
        pValve->interruptSignaled(p, v);
    }
    else
    {
        myDebug("[ValveMGROp] No pInsInterrupt %d - %d", p, v);
    }
    //Re-configure interrupt
    attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN), &ValveManager::handle_isr ,FALLING); // Reinstate interrupts from external pin.
}

ValveManagerStateOperational::~ValveManagerStateOperational(){}
