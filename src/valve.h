#pragma once
#include <string>

typedef enum VALVE_TYPE{
    VALVE_LIVINGROOM = 0,
    VALVE_UPSTAIRS,
    VALVE_BATHROOM,
    VALVES_MAX
} valve_t;

typedef struct VALVE_OBJECT{
    VALVE_TYPE valve_type;
    std::string valve_name;
} valve_struct_t;

class Valve{
    public:
        Valve(const valve_struct_t *pValve_type, int out_open, int out_close, int in_open, int in_close);
        ~Valve();



    private:
        valve_t m_type;
        std::string m_name;
        int m_pin_open;
        int m_pin_close;
        int m_input_open;
        int m_input_close;
};