#include "valveManager.h"
#include "utils.h"
#include <string>
#include "ValveManagerStateInitialize.h"

//Own static instance pointer to handle ISR forwarding..
ValveManager *ValveManager::pInst = NULL;

static const valve_struct_t valve_types[VALVE_MAX] = 
{
    {VALVE_LIVINGROOM, "Livingroom"},
    {VALVE_UPSTAIRS, "Upstairs"},
    {VALVE_BATHROOM, "Bathroom"},
};

/* Constructor */
ValveManager::ValveManager() :
    m_state(new ValveManagerStateInitialize(*this))
{
    using namespace std::placeholders;
    pInst = this;
    m_leds_enabled = false;
    //Clear list and create valves:
    m_valves.clear();
    m_valves.push_back(new Valve(&valve_types[VALVE_LIVINGROOM], SIG_OUT_OPEN_1, SIG_OUT_CLOSE_1, SIG_IN_OPEN_1, SIG_IN_CLOSE_1, mcp)); 
    m_valves.push_back(new Valve(&valve_types[VALVE_UPSTAIRS],   SIG_OUT_OPEN_2, SIG_OUT_CLOSE_2, SIG_IN_OPEN_2, SIG_IN_CLOSE_2, mcp));
    m_valves.push_back(new Valve(&valve_types[VALVE_BATHROOM],   SIG_OUT_OPEN_3, SIG_OUT_CLOSE_3, SIG_IN_OPEN_3, SIG_IN_CLOSE_3, mcp));

    //Add callback handler for all valves:
    for(Valve *pValve : m_valves)
    {
        pValve->addOnCompleteHandler(std::bind(&ValveManager::valveCallback, this, _1));
    }
}

/* Destructor */
ValveManager::~ValveManager()
{
    delete m_state;
}

//TODO; remove?
void ValveManager::Initialize(valve_update_callback callback)
{
    f_UpdateCallback = callback;
}

/* Process Loop -> pass through to active state*/
void ValveManager::Process()
{
    m_state->Process(*this);
}

Valve * ValveManager::getValve(VALVE_TYPE type)
{
    Valve *pValve = NULL;
    for(auto it = m_valves.cbegin(); it != m_valves.cend(); it++)
    {
        if((*it)->getType() == type)
        {
            pValve = *it;
        }
    }
    return pValve;
}

Valve * ValveManager::getValve(int interrupt_pin, int value)
{
    Valve *pValve = NULL;
    for(auto it = m_valves.cbegin(); it != m_valves.cend(); it++)
    {
        if((*it)->hasInterruptPin(interrupt_pin, value))
        {
            pValve = *it;
        }
    }
    return pValve;
}

/*Static Interrupt routine forwarder */
void ValveManager::handle_isr()
{
    if(pInst){
        myDebug_P(PSTR("[ValveMGR] Interrupt!"));
        pInst->m_state->HandleIsr(*pInst);
    }else{
        myDebug_P(PSTR("[ValveMGR] No pInstance for handling ISR!"));
    }
}

int ValveManager::valveCallback (Valve *pValve)
{
    myDebug_P(PSTR("[ValveMGR] Callback! %d = %d"), pValve->getType(), pValve->getValveStatus());
    m_state->onValveActionComplete(*this, pValve);
    f_UpdateCallback(false);
    return 0;
}