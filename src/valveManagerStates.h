#pragma once

#include "valveManager.h"
#include <stddef.h>

class ValveManager;

class AbstractState {
    public:
        virtual void Process(ValveManager &manager) = 0;
        virtual void HandleIsr(ValveManager &manager) = 0;
        virtual ~AbstractState();
    protected:
        void setState(ValveManager &manager, AbstractState *state);
        
};

/* State machine Classes */
class ValveManagerInitialize : public AbstractState {
    public:
        virtual void Process(ValveManager &manager);
        virtual void HandleIsr(ValveManager &manager);
        virtual ~ValveManagerInitialize();
};

class ValveManagerOperational : public AbstractState {
    public:
        virtual void Process(ValveManager &manager);
        virtual void HandleIsr(ValveManager &manager);
        virtual ~ValveManagerOperational();
};

class ValveManagerValidate : public AbstractState {
    public:
        virtual void Process(ValveManager &manager);
        virtual void HandleIsr(ValveManager &manager);
        virtual ~ValveManagerValidate();
};

class ValveManagerError : public AbstractState {
    public:
        virtual void Process(ValveManager &manager);
        virtual void HandleIsr(ValveManager &manager);
        virtual ~ValveManagerError();
};