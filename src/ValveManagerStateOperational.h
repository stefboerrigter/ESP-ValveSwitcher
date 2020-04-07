#include "valveManagerStates.h"

class ValveManagerStateOperational : public AbstractState {
    public:
        ValveManagerStateOperational(ValveManager &manager);
        virtual void Process(ValveManager &manager);
        virtual void HandleIsr(ValveManager &manager);
        virtual ~ValveManagerStateOperational();
        virtual void onValveActionComplete(ValveManager &manager, Valve *pValve);
    private:
        Ticker m_processTimer;
        static void ICACHE_RAM_ATTR handleTimer( ValveManagerStateOperational *pManager);
        ValveManager *pManager;
};