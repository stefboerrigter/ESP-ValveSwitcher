#include "valveManagerStates.h"

class ValveManagerStateValidate : public AbstractState {
    public:
        ValveManagerStateValidate();
        virtual void Process(ValveManager &manager);
        virtual void HandleIsr(ValveManager &manager);
        virtual ~ValveManagerStateValidate();
    private: 
        Ticker m_processTimer;
        static void ICACHE_RAM_ATTR handleTimer( ValveManagerStateValidate *pManager);
        bool m_timerInitialized;
        int m_timerInterrupts;
        ValveManager *pManager;
};