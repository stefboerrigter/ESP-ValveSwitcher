#include "valve.h"
#include <Wire.h>
#include <Arduino.h>
#include "utils.h"
#include <sstream>
#include "valveManager.h"

Valve::Valve(const valve_struct_t *pValve_type, int out_open, int out_close, 
            int in_open, int in_close, Adafruit_MCP23017 &mcp):
    m_type(pValve_type->valve_type), 
    m_pin_open(out_open), 
    m_pin_close(out_close),
    m_input_open(in_open), 
    m_input_close(in_close), 
    m_status(VALVE_INIT), 
    pIOExpander(mcp),
    m_name(pValve_type->valve_name)
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
        pIOExpander.digitalWrite(m_pin_open, HIGH);
        //pIOExpander.digitalWrite(m_pin_close, HIGH);
        pIOExpander.digitalWrite(m_pin_close, LOW);
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
        case VALVE_NOT_CONNECTED:
            retVal = "VALVE_NOT_CONNECTED";
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
    myDebug_P(PSTR("[Valve] Interrupt %s %d [ %d || %d]"), m_name.c_str(), pin, value, m_status);
    
    switch(m_status)
    {
        case VALVE_BUSY_OPENING:
        case VALVE_BUSY_CLOSING:
            if(pin == m_input_open && value == 0)
            {
                pIOExpander.digitalWrite(m_pin_open, LOW);
                myDebug_P(PSTR("[Valve] %s Open done"),m_name.c_str());
                m_status = VALVE_OPEN;
                fActionComplete(this);
            }
            else if(pin == m_input_close && value == 0)
            {
                pIOExpander.digitalWrite(m_pin_open, LOW);
                myDebug_P(PSTR("[Valve] %s Close done"),m_name.c_str());
                m_status = VALVE_CLOSED;
                fActionComplete(this);
            }
            break;
        default:
            myDebug_P(PSTR("[Valve] %s status %s ineterrupt! %d"), m_name.c_str(), valve_status_to_string(m_status).c_str(), pin);
            break;
    }
}

void Valve::setValveStatus(valve_status_t status)
{
    this->m_status = status;
}

char const *Valve::toString(void)
{
    std::stringstream retString;

    retString << m_name << ": " << valve_status_to_string(m_status);
    retString << " | Outputs: " << m_pin_open << "," << m_pin_close;
    retString << " | Inputs: "  << m_input_open << "," << m_input_close << " .";
    retString << "           ."; //TODO: find out why last characters are overridden.

    return retString.str().c_str();
}

void Valve::addOnCompleteHandler(std::function<int(Valve *pValve)> callback)
{
    this->fActionComplete = callback;
}

const char *Valve::getName()
{
    return m_name.c_str();
}