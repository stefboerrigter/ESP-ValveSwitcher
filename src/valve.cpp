#include "valve.h"
#include <Wire.h>
#include <Arduino.h>
#include "utils.h"

Valve::Valve(const valve_struct_t *pValve_type, int out_open, int out_close, int in_open, int in_close, Adafruit_MCP23017 &mcp):
    m_type(pValve_type->valve_type), 
    m_name(pValve_type->valve_name), 
    m_pin_open(out_open), 
    m_pin_close(out_close),
    m_input_open(in_open), 
    m_input_close(in_close), 
    m_status(VALVE_INIT), 
    pIOExpander(mcp)
{
}

Valve::~Valve()
{

}

void Valve::openValve()
{
    if(m_status == VALVE_OPEN || m_status == VALVE_BUSY_OPENING)
    {
        //done already
        return;
    }
    else
    {
        //no guarding for 'busy_closing'? just override:    
        myDebug_P(PSTR("[Valve] Open %s %d %d"), m_name.c_str(), m_pin_open, m_pin_close);
        pIOExpander.digitalWrite(m_pin_open, HIGH);
        pIOExpander.digitalWrite(m_pin_close, LOW);
        m_status = VALVE_BUSY_OPENING; 
    }
}

void Valve::closeValve()
{
    if(m_status == VALVE_CLOSED || m_status == VALVE_BUSY_CLOSING)
    {
        //done already
        return;
    }
    else
    {
        myDebug_P(PSTR("[Valve] Close %s %d %d"), m_name.c_str(), m_pin_open, m_pin_close);
        pIOExpander.digitalWrite(m_pin_open, LOW);
        pIOExpander.digitalWrite(m_pin_close, HIGH);
        m_status = VALVE_BUSY_CLOSING;
    }
}

valve_status_t Valve::getValveStatus()
{
    return m_status;
}

std::string Valve::valve_status_to_string(valve_status_t status)
{
    std::string retVal;
    switch(status)
    {
        case VALVE_CLOSED:
            retVal = "VALVE_CLOSED";
            break;
        case VALVE_OPEN:
            retVal = "VALVE_OPEN";
            break;
        case VALVE_INIT:
            retVal = "VALVE_INIT";
            break;
        case VALVE_ERROR:
            retVal = "VALVE_ERROR";
            break;
        case VALVE_BUSY_CLOSING:
            retVal = "VALVE_BUSY_CLOSING";
            break;
        case VALVE_BUSY_OPENING:
            retVal = "VALVE_BUSY_OPENING";
            break;        
    }
    return retVal;
}
valve_t Valve::getType()
{
    return m_type;
}

bool Valve::hasInterruptPin(int interruptPin, int value)
{
    return (interruptPin == m_input_close || interruptPin == m_input_open);
}

void Valve::interruptSignaled(int pin, int value)
{
    //m_status = VALVE_INIT;
    //myDebug_P(PSTR("[Valve] Interrupt %s %d [ %d || %d]"), m_name.c_str(), pin, value, m_status);
    
    if (pin == m_input_open)
    {
        if(m_status == VALVE_BUSY_OPENING && value == 0)
        {
            myDebug_P(PSTR("[Valve] %s Open done"),m_name.c_str());
            m_status = VALVE_OPEN;
            pIOExpander.digitalWrite(m_pin_open, LOW);
        }
        else
        {
            if(m_status == VALVE_OPEN)
            {
                myDebug_P(PSTR("[Valve] %s Open done -> Not Done. Status not updated"), m_name.c_str());
            }
        }
    }
    else if(pin == m_input_close)
    {
        if(m_status == VALVE_BUSY_CLOSING && value == 0)
        {
            myDebug_P(PSTR("[Valve] %s Close done"),m_name.c_str());
            m_status = VALVE_CLOSED;
            pIOExpander.digitalWrite(m_pin_close, LOW);
        }
        else
        {
            if(m_status == VALVE_CLOSED)
            {
                myDebug_P(PSTR("[Valve] %s Close done -> Not Done. Status not updated!"), m_name.c_str());
            }
        }
    }
    else
    {
        myDebug_P(PSTR("[Valve] %s Interrupt to wrong valve! %d"), m_name.c_str(), pin);
    }
}