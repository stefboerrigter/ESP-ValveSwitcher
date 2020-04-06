#include "ValveManagerStateError.h"

/////////////////////////////////////////////////////////////////////////////////////////////////
//  Error
//
//  Responsible for handling ERROR state of valves
//
/////////////////////////////////////////////////////////////////////////////////////////////////
void ValveManagerStateError::Process(ValveManager &manager){
    myDebug_P(PSTR("[ValveManagerStateError] Process not implemented"));
}

void ValveManagerStateError::HandleIsr(ValveManager &manager){
    myDebug_P(PSTR("[ValveManagerStateError] ISR not implemented"));
}

ValveManagerStateError::~ValveManagerStateError(){}