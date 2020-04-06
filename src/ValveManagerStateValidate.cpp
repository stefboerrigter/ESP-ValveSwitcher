#include "ValveManagerStateValidate.h"
#include "ValveManagerStateError.h"
#include "ValveManagerStateOperational.h"

/////////////////////////////////////////////////////////////////////////////////////////////////
//  Validate
//
//  Responsible for validating if the valves still function as expected 
//  Start every valve, rotate it and verify it signals Open & Close state..
//
/////////////////////////////////////////////////////////////////////////////////////////////////

ValveManagerStateValidate::ValveManagerStateValidate() :
    m_timerInitialized(false),
    pManager(NULL)

{
    myDebug_P(PSTR("[ValveMGRVal] Validation thread constructor"));
}

//---------------------------------------------------------------------------------
ValveManagerStateValidate::~ValveManagerStateValidate(){
    myDebug_P(PSTR("[ValveMGRVal] Validation thread destructor"));
    m_processTimer.detach();
}

//---------------------------------------------------------------------------------
void ValveManagerStateValidate::Process(ValveManager &manager){
    //keep track of active valves during validation..
    int iterator = 0;
    VALVE_TYPE type = VALVE_LIVINGROOM;

    if(!m_timerInitialized)
    {
        m_timerInitialized = true;
        this->pManager = &manager;
        m_processTimer.attach(30, &ValveManagerStateValidate::handleTimer, this); //Timer fires after X seconds
    }

    for(type = VALVE_TYPE_FIRST; type < VALVE_TYPE_LAST; )
    {
        Valve *pValve = manager.getValve(type);
        valve_status_t status = pValve->getValveStatus();
        switch(status)
        {
            case VALVE_INIT:
                //myDebug("[ValveMGRVal] Initialize Valve %s", pValve->toString());
                pValve->closeValve();//start close on initialization
                break;
            case VALVE_ERROR:
                //signal report to MQTT & Telnet
                myDebug("[ValveMGRVal] Valve Error %d", type);
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

//---------------------------------------------------------------------------------
void ValveManagerStateValidate::HandleIsr(ValveManager &manager){
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
        myDebug("[ValveMGRVal] No pInsInterrupt %d - %d", p, v);
    }
    //Re-configure interrupt
    attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN), &ValveManager::handle_isr ,FALLING); // Reinstate interrupts from external pin.
}

//---------------------------------------------------------------------------------
void ValveManagerStateValidate::onValveActionComplete(ValveManager &manager, Valve *pValve)
{
    switch(pValve->getValveStatus())
    {
        case VALVE_OPEN:
            //valve is done initializing.
            myDebug("[ValveMGRVal] Valve %s is open and ready", pValve->getName());
            break;
        case VALVE_CLOSED:
            pValve->openValve();
            break;
        default: //do nothing.
            break;
    }
}

//---------------------------------------------------------------------------------
void ValveManagerStateValidate::handleTimer(ValveManagerStateValidate *pMgr)
{
    VALVE_TYPE type = VALVE_LIVINGROOM;
    int iterator = 0;

    for(type = VALVE_TYPE_FIRST; type < VALVE_TYPE_LAST; )
    {
        Valve *pValve = pMgr->pManager->getValve(type);
        valve_status_t status = pValve->getValveStatus();
        switch(status)
        {
            case VALVE_OPEN:
                //complete; leave allValvesReady on True
                break;
            default:
                pValve->setValveStatus(VALVE_NOT_CONNECTED);
                break;
        }
        
        iterator = static_cast<int>(type);
        type = static_cast<VALVE_TYPE>(++iterator);
    }
    pMgr->setState(*(pMgr->pManager), new ValveManagerStateOperational());
}