#include "valveManagerStates.h"

class ValveManagerStateInitialize : public AbstractState {
    public:
        virtual void Process(ValveManager &manager);
        virtual void HandleIsr(ValveManager &manager);
        virtual ~ValveManagerStateInitialize();
};