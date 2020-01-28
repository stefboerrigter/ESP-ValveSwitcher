#include "valve.h"

Valve::Valve(const valve_struct_t *pValve_type, int out_open, int out_close, int in_open, int in_close):
    m_type(pValve_type->valve_type), m_name(pValve_type->valve_name), m_pin_open(out_open), m_pin_close(out_close),
    m_input_open(in_open), m_input_close(in_close)
{

}

Valve::~Valve()
{

}