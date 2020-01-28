#include "valveManager.h"
#include "utils.h"


//Own static instance pointer to handle ISR forwarding..
ValveManager *ValveManager::pInst = NULL;

/* Constructor */
ValveManager::ValveManager() :
    m_state(new ValveManagerInitialize())
{
    pInst = this;
    m_leds_enabled = false;
}

/* Destructor */
ValveManager::~ValveManager()
{
    delete m_state;
}

/* Process Loop -> pass through to active state*/
void ValveManager::Process()
{
    m_state->Process(*this);
}

/*Static Interrupt routine forwarder */
void ValveManager::handle_isr()
{
    if(pInst){
        pInst->m_state->HandleIsr(*pInst);
    }else{
        myDebug_P(PSTR("[ValveMGR] No PINST for handling ISR!"));
    }
}

/* Demo handling functions */
bool ValveManager::getLedsEnabled()
{
    return m_leds_enabled;
}

void ValveManager::toggleLedsEnabled()
{
    m_leds_enabled = !m_leds_enabled;
}