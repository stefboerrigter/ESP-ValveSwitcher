#include "valveManagerStates.h"

#define VALIDATION_LOOP_DURATION (30)

class ValveManagerStateValidate : public AbstractState {
    public:
        ValveManagerStateValidate(ValveManager &manager);
        virtual void Process(ValveManager &manager);
        virtual void HandleIsr(ValveManager &manager);
        virtual ~ValveManagerStateValidate();
        virtual void onValveActionComplete(ValveManager &manager, Valve *pValve);
    private: 
        Ticker m_processTimer;
        static void ICACHE_RAM_ATTR handleTimer( ValveManagerStateValidate *pManager);
        ValveManager *pManager;
};