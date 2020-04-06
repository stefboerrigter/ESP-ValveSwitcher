#include "ValveManagerStateError.h"

/////////////////////////////////////////////////////////////////////////////////////////////////
//  Error
//
//  Responsible for handling ERROR state of valves
//
/////////////////////////////////////////////////////////////////////////////////////////////////
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
ValveManagerStateError::~ValveManagerStateError(){}