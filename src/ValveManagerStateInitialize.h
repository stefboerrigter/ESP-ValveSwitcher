#include "valveManagerStates.h"

class ValveManagerStateInitialize : public AbstractState {
    public:
        ValveManagerStateInitialize(ValveManager &manager);
        virtual ~ValveManagerStateInitialize();
        virtual void Process(ValveManager &manager);
        virtual void HandleIsr(ValveManager &manager);
        virtual void onValveActionComplete(ValveManager &manager, Valve *pValve);
};