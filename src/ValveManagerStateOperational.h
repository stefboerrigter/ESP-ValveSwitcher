#include "valveManagerStates.h"

class ValveManagerStateOperational : public AbstractState {
    public:
        ValveManagerStateOperational();
        virtual void Process(ValveManager &manager);
        virtual void HandleIsr(ValveManager &manager);
        virtual ~ValveManagerStateOperational();
        virtual void onValveActionComplete(ValveManager &manager, Valve *pValve);
};