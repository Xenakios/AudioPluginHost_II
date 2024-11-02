#pragma once

#include "clap/clap.h"
#include "../Common/xap_utils.h"
#include "clap/events.h"
#include "containers/choc_Span.h"
#include "containers/choc_Value.h"
#include "text/choc_JSON.h"
#include <limits>
#include <stdexcept>

class ClapEventSequence
{

  public:
    union clap_multi_event
    {
        clap_event_header_t header;
        clap_event_note note;
        clap_event_midi_t midi;
        clap_event_midi2_t midi2;
        clap_event_param_value_t param;
        clap_event_param_mod_t parammod;
        clap_event_param_gesture_t paramgest;
        clap_event_note_expression_t noteexpression;
        clap_event_transport_t transport;
        clap_event_xen_string xstr;
        clap_event_xen_audiobuffer xabuf;
        clap_event_xen_audiorouting xrout;
    };
    struct Event
    {
        Event() {}
        template <typename EType>
        Event(double time, EType *e, int d0 = 0, int d1 = 0)
            : timestamp(time), event(*(clap_multi_event *)e), extdata0(d0), extdata1(d1)
        {
        }
        double timestamp = 0.0;
        int extdata0 = 0;
        int extdata1 = 0;
        clap_multi_event event;
        bool operator<(const Event &other) { return timestamp < other.timestamp; }
    };
    std::vector<Event> m_evlist;
    ClapEventSequence() { m_evlist.reserve(4096); }
    void sortEvents() { choc::sorting::stable_sort(m_evlist.begin(), m_evlist.end()); }
    void clearEvents() { m_evlist.clear(); }
    size_t getNumEvents() const { return m_evlist.size(); }
    // should be fairly accurate, despite the name of the method
    size_t getApproxSizeInBytes() const { return m_evlist.capacity() * sizeof(Event); }
    void addNoteOn(double time, int port, int channel, int key, double velo, int note_id)
    {
        auto ev =
            xenakios::make_event_note(0, CLAP_EVENT_NOTE_ON, port, channel, key, note_id, velo);
        m_evlist.push_back(Event(time, &ev));
    }
    void addNoteOff(double time, int port, int channel, int key, double velo, int note_id)
    {
        auto ev =
            xenakios::make_event_note(0, CLAP_EVENT_NOTE_OFF, port, channel, key, note_id, velo);
        m_evlist.push_back(Event(time, &ev));
    }
    void addNote(double time, double duration, int port, int channel, int key, int note_id,
                 double velo, double retune)
    {
        auto ev =
            xenakios::make_event_note(0, CLAP_EVENT_NOTE_ON, port, channel, key, note_id, velo);
        m_evlist.push_back(Event(time, &ev));
        if (std::abs(retune) >= 0.001)
        {
            auto exprev = xenakios::make_event_note_expression(0, CLAP_NOTE_EXPRESSION_TUNING, port,
                                                               channel, key, note_id, retune);
            m_evlist.push_back(Event(time, &exprev));
        }
        ev = xenakios::make_event_note(0, CLAP_EVENT_NOTE_OFF, port, channel, key, note_id, velo);
        m_evlist.push_back(Event(time + duration, &ev));
    }
    void addNoteF(double time, double duration, int port, int channel, double pitch, int note_id,
                  double velo)
    {
        int key = (int)pitch;
        double frac = pitch - key;
        auto ev =
            xenakios::make_event_note(0, CLAP_EVENT_NOTE_ON, port, channel, key, note_id, velo);
        m_evlist.push_back(Event(time, &ev));
        if (frac > 0.0)
        {
            auto exprev = xenakios::make_event_note_expression(0, CLAP_NOTE_EXPRESSION_TUNING, port,
                                                               channel, key, note_id, frac);
            m_evlist.push_back(Event(time, &exprev));
        }
        ev = xenakios::make_event_note(0, CLAP_EVENT_NOTE_OFF, port, channel, key, note_id, velo);
        m_evlist.push_back(Event(time + duration, &ev));
    }
    void addNoteExpression(double time, int port, int channel, int key, int note_id, int net,
                           double amt)
    {
        auto ev = xenakios::make_event_note_expression(0, net, port, channel, key, note_id, amt);
        m_evlist.push_back(Event(time, &ev));
    }
    void addParameterEvent(bool ismod, double time, int port, int channel, int key, int note_id,
                           uint32_t par_id, double value)
    {
        if (ismod)
        {
            auto ev = xenakios::make_event_param_mod(0, par_id, value, nullptr, port, channel, key,
                                                     note_id, 0);
            m_evlist.push_back(Event(time, &ev));
        }
        else
        {
            auto ev = xenakios::make_event_param_value(0, par_id, value, nullptr, port, channel,
                                                       key, note_id, 0);
            m_evlist.push_back(Event(time, &ev));
        }
    }
    void addTransportEvent(double time, double tempo);
    void addAudioBufferEvent(double time, int32_t target, double *buf, int32_t numchans,
                             int32_t numframes, int32_t samplerate)
    {
        clap_event_xen_audiobuffer ev;
        ev.header.flags = 0;
        ev.header.size = sizeof(clap_event_xen_audiobuffer);
        ev.header.space_id = 666;
        ev.header.time = 0;
        ev.header.type = XENAKIOS_AUDIOBUFFER_MSG;
        ev.target = target;
        ev.buffer = buf;
        ev.numchans = numchans;
        ev.numframes = numframes;
        ev.samplerate = samplerate;
        ev.target = target;
        m_evlist.push_back(Event(time, &ev));
    }
    void addAudioRoutingEvent(double time, int32_t target, int32_t opcode, int32_t src,
                              int32_t dest)
    {
        clap_event_xen_audiorouting ev;
        ev.header.flags = 0;
        ev.header.size = sizeof(clap_event_xen_audiorouting);
        ev.header.space_id = 666;
        ev.header.time = 0;
        ev.header.type = XENAKIOS_ROUTING_MSG;
        ev.target = target;
        ev.opcode = opcode;
        ev.src = src;
        ev.dest = dest;
        m_evlist.push_back(Event(time, &ev));
    }
    std::unordered_map<int, std::string> sequenceStrings;
    void addString(int id, std::string str) { sequenceStrings[id] = str; }
    void removeString(int id) { sequenceStrings.erase(id); }
    void addStringEvent(double time, int port, int channel, int key, int note_id, int str_id,
                        int32_t target)
    {
        auto it = sequenceStrings.find(str_id);
        if (it != sequenceStrings.end())
        {
            clap_event_xen_string ev;
            ev.header.flags = 0;
            ev.header.size = sizeof(clap_event_xen_string);
            ev.header.space_id = 666;
            ev.header.time = 0;
            ev.header.type = XENAKIOS_STRING_MSG;
            ev.target = target;
            ev.str = (char *)it->second.c_str();
            m_evlist.push_back(Event(time, &ev));
        }
    }
    void addProgramChange(double time, int port, int channel, int program)
    {
        clap_event_midi ev;
        ev.header.flags = 0;
        ev.header.size = sizeof(clap_event_midi);
        ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev.header.time = 0;
        ev.header.type = CLAP_EVENT_MIDI;
        ev.port_index = std::clamp<int>(port, 0, 65536);
        ev.data[0] = 0xc0 + (channel % 16);
        ev.data[1] = program & 0x7f;
        ev.data[2] = 0;
        m_evlist.push_back(Event(time, &ev));
    }
    void addMIDI1Message(double time, int port, uint8_t b0, uint8_t b1, uint8_t b2)
    {
        clap_event_midi ev;
        ev.header.flags = 0;
        ev.header.size = sizeof(clap_event_midi);
        ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev.header.time = 0;
        ev.header.type = CLAP_EVENT_MIDI;
        ev.port_index = port;
        ev.data[0] = b0;
        ev.data[1] = b1;
        ev.data[2] = b2;
        m_evlist.push_back(Event(time, &ev));
    }
    // doesn't require events to be pre-sorted, so needs to scan all events
    double getMaximumEventTime() const
    {
        if (m_evlist.empty())
            throw std::runtime_error("No events in sequence");
        double maxt = std::numeric_limits<double>::min();
        for (auto &e : m_evlist)
        {
            maxt = std::max(maxt, e.timestamp);
        }
        return maxt;
    }
    choc::value::Value toValueTree(std::string rootName)
    {
        sortEvents();
        auto root = choc::value::createObject(rootName);
        root.setMember("version", 0);
        auto evarr = choc::value::createEmptyArray();
        for (size_t i = 0; i < m_evlist.size(); ++i)
        {
            const Event &e = m_evlist[i];
            auto evob = choc::value::createObject("e");
            evob.setMember("time", e.timestamp);
            evob.setMember("type", (int64_t)e.event.header.type);

            if (e.extdata0 != 0)
                evob.setMember("ext0", e.extdata0);
            if (e.extdata1 != 0)
                evob.setMember("ext1", e.extdata1);
            if (e.event.header.space_id != CLAP_CORE_EVENT_SPACE_ID)
                evob.setMember("spid", (int64_t)e.event.header.space_id);
            if (e.event.header.type == CLAP_EVENT_NOTE_ON ||
                e.event.header.type == CLAP_EVENT_NOTE_OFF ||
                e.event.header.type == CLAP_EVENT_NOTE_CHOKE)
            {
                auto nev = (clap_event_note *)&e.event;
                evob.setMember("port", (int64_t)nev->port_index);
                evob.setMember("chan", (int64_t)nev->channel);
                evob.setMember("key", (int64_t)nev->key);
                if (nev->note_id != -1)
                    evob.setMember("nid", (int64_t)nev->note_id);
            }
            if (e.event.header.type == CLAP_EVENT_NOTE_EXPRESSION)
            {
                auto xev = (clap_event_note_expression *)&e.event;
                evob.setMember("port", (int64_t)xev->port_index);
                evob.setMember("chan", (int64_t)xev->channel);
                evob.setMember("key", (int64_t)xev->key);
                if (xev->note_id != -1)
                    evob.setMember("nid", (int64_t)xev->note_id);
                evob.setMember("exid", xev->expression_id);
                evob.setMember("val", xev->value);
            }
            if (e.event.header.type == CLAP_EVENT_PARAM_VALUE)
            {
                auto pev = (clap_event_param_value *)&e.event;
                evob.setMember("port", (int64_t)pev->port_index);
                evob.setMember("chan", (int64_t)pev->channel);
                evob.setMember("key", (int64_t)pev->key);
                if (pev->note_id != -1)
                    evob.setMember("nid", (int64_t)pev->note_id);
                evob.setMember("pid", (int64_t)pev->param_id);
                evob.setMember("val", (int64_t)pev->value);
            }
            if (e.event.header.type == CLAP_EVENT_PARAM_MOD)
            {
                auto pev = (clap_event_param_mod *)&e.event;
                evob.setMember("port", (int64_t)pev->port_index);
                evob.setMember("chan", (int64_t)pev->channel);
                evob.setMember("key", (int64_t)pev->key);
                if (pev->note_id != -1)
                    evob.setMember("nid", (int64_t)pev->note_id);
                evob.setMember("pid", (int64_t)pev->param_id);
                evob.setMember("val", (int64_t)pev->amount);
            }
            if (e.event.header.type == CLAP_EVENT_MIDI)
            {
                auto miev = (clap_event_midi *)&e.event;
                evob.setMember("port", (int64_t)miev->port_index);
                evob.setMember("b0", (int64_t)miev->data[0]);
                evob.setMember("b1", (int64_t)miev->data[1]);
                evob.setMember("b2", (int64_t)miev->data[2]);
            }
            if (e.event.header.type == CLAP_EVENT_MIDI2)
            {
                auto mi2ev = (clap_event_midi2 *)&e.event;
                evob.setMember("port", (int64_t)mi2ev->port_index);
                evob.setMember("i0", (int64_t)mi2ev->data[0]);
                evob.setMember("i1", (int64_t)mi2ev->data[1]);
                evob.setMember("i2", (int64_t)mi2ev->data[2]);
                evob.setMember("i3", (int64_t)mi2ev->data[3]);
            }
            evarr.addArrayElement(evob);
        }
        root.setMember("events", evarr);
        return root;
    }
    std::string toJSON()
    {
        auto root = toValueTree("e");
        return choc::json::toString(root, true);
    }
    struct Iterator
    {
        /// Creates an iterator positioned at the start of the sequence.
        Iterator(const ClapEventSequence &s) : owner(s) {}
        Iterator(const Iterator &) = default;
        Iterator(Iterator &&) = default;

        /// Seeks the iterator to the given time

        void setTime(double newTimeStamp)
        {
            auto eventData = owner.m_evlist.data();

            while (nextIndex != 0 && eventData[nextIndex - 1].timestamp >= newTimeStamp)
                --nextIndex;

            while (nextIndex < owner.m_evlist.size() &&
                   eventData[nextIndex].timestamp < newTimeStamp)
                ++nextIndex;

            currentTime = newTimeStamp;
        }

        /// Returns the current iterator time
        double getTime() const noexcept { return currentTime; }

        /// Returns a set of events which lie between the current time, up to (but not
        /// including) the given duration. This function then increments the iterator to
        /// set its current time to the end of this block.

        choc::span<const ClapEventSequence::Event> readNextEvents(double duration)
        {
            auto start = nextIndex;
            auto eventData = owner.m_evlist.data();
            auto end = start;
            auto total = owner.m_evlist.size();
            auto endTime = currentTime + duration;
            currentTime = endTime;

            while (end < total && eventData[end].timestamp < endTime)
                ++end;

            nextIndex = end;

            return {eventData + start, eventData + end};
        }

      private:
        const ClapEventSequence &owner;
        double currentTime = 0;
        size_t nextIndex = 0;
    };
    // to avoid issues with accumulating time counts going out of sync,
    // this iterator deals with time positions as samples and needs to be provided
    // the current playback sample rate
    struct IteratorSampleTime
    {
        /// Creates an iterator positioned at the start of the sequence.
        IteratorSampleTime(const ClapEventSequence &s, double sr) : owner(s), sampleRate(sr) {}
        IteratorSampleTime(const IteratorSampleTime &) = default;
        IteratorSampleTime(IteratorSampleTime &&) = default;

        /// Seeks the iterator to the given time

        void setTime(int64_t newTimeStamp)
        {
            auto eventData = owner.m_evlist.data();

            while (nextIndex != 0 &&
                   eventData[nextIndex - 1].timestamp * sampleRate >= newTimeStamp)
                --nextIndex;

            while (nextIndex < owner.m_evlist.size() &&
                   eventData[nextIndex].timestamp * sampleRate < newTimeStamp)
                ++nextIndex;

            currentTime = newTimeStamp;
        }

        /// Returns the current iterator time
        int64_t getTime() const noexcept { return currentTime; }

        /// Returns a set of events which lie between the current time, up to (but not
        /// including) the given duration. This function then increments the iterator to
        /// set its current time to the end of this block.

        choc::span<const ClapEventSequence::Event> readNextEvents(int duration)
        {
            auto start = nextIndex;
            auto eventData = owner.m_evlist.data();
            auto end = start;
            auto total = owner.m_evlist.size();
            auto endTime = currentTime + duration;
            currentTime = endTime;

            while (end < total && eventData[end].timestamp * sampleRate < endTime)
                ++end;

            nextIndex = end;

            return {eventData + start, eventData + end};
        }

      private:
        const ClapEventSequence &owner;
        int64_t currentTime = 0;
        size_t nextIndex = 0;
        double sampleRate = 0.0;
    };
};
