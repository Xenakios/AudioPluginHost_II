#pragma once

#include <clap/clap.h>
#include <vector>
#include "containers/choc_NonAllocatingStableSort.h"
#include <algorithm>

namespace xenakios
{
// An attempt to implement an easier to use Clap event list
// for host purposes. This wouldn't work with MIDI SYSEX and similar dynamically
// sized messages, but maybe we will just live with that...
class ClapEventList
{
  public:
    static constexpr size_t maxEventSize =
        std::max({sizeof(clap_event_note), sizeof(clap_event_note_expression),
                  sizeof(clap_event_param_value), sizeof(clap_event_param_mod),
                  sizeof(clap_event_midi), sizeof(clap_event_transport)});
    ClapEventList()
    {
        m_data.resize(m_max_events * maxEventSize);
        m_entries.resize(m_max_events);
    }
    bool tryPush(const clap_event_header *evin)
    {
        if (evin == nullptr || evin->type == CLAP_EVENT_MIDI_SYSEX || evin->size > maxEventSize ||
            m_write_index >= m_max_events)
            return false;
        auto ptr = &m_data[m_write_index * maxEventSize];
        memcpy(ptr, evin, maxEventSize);
        m_entries[m_write_index].dataPtr = ptr;
        m_entries[m_write_index].eventTime = evin->time;
        ++m_write_index;
        return true;
    }
    template <typename EventType> bool tryPushAs(const EventType *evin)
    {
        return tryPush(reinterpret_cast<const clap_event_header *>(evin));
    }
    size_t size() const { return m_write_index; }
    clap_event_header *get(size_t index) const
    {
        return reinterpret_cast<clap_event_header *>(m_entries[index].dataPtr);
    }
    template <typename EventType> EventType *getAs(size_t index)
    {
        return reinterpret_cast<EventType *>(m_entries[index].dataPtr);
    }
    template <typename EventType> EventType *getLastAs()
    {
        if (m_write_index > 0)
            return getAs<EventType>(m_write_index - 1);
        return nullptr;
    }
    void clear(bool deep = false)
    {
        if (deep)
        {
            for (auto &e : m_entries)
            {
                e.dataPtr = nullptr;
                e.eventTime = 0;
            }
            for (auto &e : m_data)
                e = 0;
        }
        m_write_index = 0;
    }
    void sort()
    {
        choc::sorting::stable_sort(
            m_entries.begin(), m_entries.begin() + m_write_index,
            [](const Entry &a, const Entry &b) { return a.eventTime < b.eventTime; });
    }

  private:
    struct Entry
    {
        unsigned char *dataPtr = nullptr;
        uint32_t eventTime = 0;
    };
    std::vector<unsigned char> m_data;
    std::vector<Entry> m_entries;
    size_t m_max_events = 512;
    size_t m_write_index = 0;
};
} // namespace xenakios
