#include "valveManagerStates.h"

AbstractState::~AbstractState(){

}

void AbstractState::setState(ValveManager &manager, AbstractState *state)
{
    AbstractState *aux = manager.m_state;
    manager.m_state = state;
    delete aux;
}