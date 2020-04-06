#include "valveManagerStates.h"

class ValveManagerStateError : public AbstractState {
    public:
        virtual ~ValveManagerStateError();
        virtual void Process(ValveManager &manager);
        virtual void HandleIsr(ValveManager &manager);
};