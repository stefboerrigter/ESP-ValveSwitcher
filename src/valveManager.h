#pragma once


class ValveManager {
    public:
        ValveManager();
        ~ValveManager();
        void Process();
        void initialize();
        
        enum ValveStates{
            VALVES_INIT,
            VALVES_OPERATIONAL,
            VALVES_VALIDATE,
            VALVES_ERROR,
        };

    private:
        bool m_leds_enabled;
};