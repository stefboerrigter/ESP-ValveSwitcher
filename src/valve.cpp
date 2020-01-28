#include "valve.h"
#include <Wire.h>
#include <Arduino.h>
#include "utils.h"

Valve::Valve(const valve_struct_t *pValve_type, int out_open, int out_close, int in_open, int in_close, Adafruit_MCP23017 *pMcp):
    m_type(pValve_type->valve_type), m_name(pValve_type->valve_name), m_pin_open(out_open), m_pin_close(out_close),
    m_input_open(in_open), m_input_close(in_close), m_status(VALVE_INIT), pIOExpander(pMcp)
{
    
}

Valve::~Valve()
{

}

void Valve::openValve()
{
    myDebug_P(PSTR("[Valve] Open %s %d %d"), m_name.c_str(), m_pin_open, m_pin_close);
    pIOExpander->digitalWrite(m_pin_open, LOW);
    pIOExpander->digitalWrite(m_pin_close, HIGH);
}

void Valve::closeValve()
{
    myDebug_P(PSTR("[Valve] Close %s %d %d"), m_name.c_str(), m_pin_open, m_pin_close);
    pIOExpander->digitalWrite(m_pin_open, HIGH);
    pIOExpander->digitalWrite(m_pin_close, LOW);
}

valve_status_t Valve::getValveStatus()
{
    return m_status;
}

valve_t Valve::getType()
{
    return m_type;
}

bool Valve::hasInterruptPin(int interruptPin, int value)
{
    return (interruptPin == m_input_close || interruptPin == m_input_open);
}

void Valve::interruptSignaled(int pin)
{

    pIOExpander->digitalWrite(m_pin_open, LOW);
    pIOExpander->digitalWrite(m_pin_close, LOW);
    m_status = VALVE_INIT;
    myDebug_P(PSTR("[Valve] Interrupt %s %d [ %d ]"), m_name.c_str(), pin, m_status);
}