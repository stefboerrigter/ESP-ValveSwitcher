#pragma once


class valves {
    public:
        valves();
        ~valves();
        void Process();
        void initialize();
    private:
        bool m_leds_enabled;
};