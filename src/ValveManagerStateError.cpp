#include "ValveManagerStateError.h"

/////////////////////////////////////////////////////////////////////////////////////////////////
//  Error
//
//  Responsible for handling ERROR state of valves
//
/////////////////////////////////////////////////////////////////////////////////////////////////
ValveManagerStateError::ValveManagerStateError(ValveManager &manager)
{
   myDebug("[ValveMGRError] Entered Error state");
}

//---------------------------------------------------------------------------------
void ValveManagerStateError::Process(ValveManager &manager){
    myDebug_P(PSTR("[ValveMGRError] Process not implemented"));
}

//---------------------------------------------------------------------------------
void ValveManagerStateError::HandleIsr(ValveManager &manager){
    myDebug_P(PSTR("[ValveMGRError] ISR not implemented"));
}

//---------------------------------------------------------------------------------
void ValveManagerStateError::onValveActionComplete(ValveManager &manager, Valve *pValve)
{
    myDebug("[ValveMGRError] No onValveActionComplete implemented");
}

//---------------------------------------------------------------------------------
ValveManagerStateError::~ValveManagerStateError()
{
    //myDebug("[ValveMGRError] Leave Error state");
}