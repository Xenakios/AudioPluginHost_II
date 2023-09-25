#include "xap_utils.h"
#ifdef FOOX
#include <format>
#include <iostream>


void printClapEvents(clap::helpers::EventList &elist)
{
    std::string line;
    std::cout << std::format("{:5}|{:20}|{:10}|{}\n", "Time", "Event type", "Param ID",
                             "Value/Amt");
    for (int i = 0; i < elist.size(); ++i)
    {
        auto ev = elist.get(i);
        line = std::format("{} UNKNOWN CLAP EVENT", ev->time);
        if (ev->type == CLAP_EVENT_NOTE_ON || ev->type == CLAP_EVENT_NOTE_OFF)
        {
            auto nev = reinterpret_cast<const clap_event_note *>(ev);
            if (ev->type == CLAP_EVENT_NOTE_ON)
                line = std::format("{:5} CLAP NOTE ON {}", nev->header.time, nev->key);
            else
                line = std::format("{:5} CLAP NOTE OFF {}", nev->header.time, nev->key);
        }
        if (ev->type == CLAP_EVENT_PARAM_VALUE)
        {
            auto pev = reinterpret_cast<const clap_event_param_value *>(ev);
            line = std::format("{:5}|{:20}|{:10}|{}", pev->header.time, "CLAP PARAM VALUE",
                               pev->param_id, pev->value);
        }
        if (ev->type == CLAP_EVENT_PARAM_MOD)
        {
            auto pev = reinterpret_cast<const clap_event_param_mod *>(ev);
            line = std::format("{:5}|{:20}|{:10}|{}", pev->header.time, "CLAP PARAM MOD",
                               pev->param_id, pev->amount);
        }
        std::cout << line << "\n";
    }
}
#else
void printClapEvents(clap::helpers::EventList &elist)
{

}
#endif
