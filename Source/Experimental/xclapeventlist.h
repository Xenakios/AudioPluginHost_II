#pragma once

#include <clap/clap.h>
#include <vector>
#include "containers/choc_NonAllocatingStableSort.h"
#include <algorithm>
#include <cassert>

namespace xenakios
{
// An attempt to implement an easier to use Clap event list
// for host purposes. This wouldn't work with MIDI SYSEX and similar dynamically
// sized messages, but maybe we will just live with that...
class ClapEventList
{
  public:
    static constexpr size_t maxEventSize = std::max(
        {sizeof(clap_event_note), sizeof(clap_event_note_expression),
         sizeof(clap_event_param_value), sizeof(clap_event_param_mod), sizeof(clap_event_trigger),
         sizeof(clap_event_midi), sizeof(clap_event_transport), sizeof(clap_event_midi2)});
    ClapEventList()
    {
        m_data.resize(m_max_events * maxEventSize);
        m_entries.resize(m_max_events);
        m_clap_in_events.ctx = this;
        m_clap_in_events.get = inEventsGetFunc;
        m_clap_in_events.size = inEventsSizeFunc;
        m_clap_out_events.ctx = this;
        m_clap_out_events.try_push = outEventsPushFunc;
    }
    static bool outEventsPushFunc(const struct clap_output_events *list,
                                  const clap_event_header_t *event)
    {
        return static_cast<ClapEventList *>(list->ctx)->tryPush(event);
    }
    static const clap_event_header_t *inEventsGetFunc(const struct clap_input_events *list,
                                                      uint32_t index)
    {
        return static_cast<ClapEventList *>(list->ctx)->get(index);
    }
    static uint32_t inEventsSizeFunc(const struct clap_input_events *list)
    {
        return static_cast<ClapEventList *>(list->ctx)->size();
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
        m_is_sorted = false;
        return true;
    }
    template <typename EventType> bool tryPushAs(const EventType *evin)
    {
        return tryPush(reinterpret_cast<const clap_event_header *>(evin));
    }
    uint32_t size() const { return m_write_index; }
    clap_event_header *get(size_t index) const
    {
        assert(m_is_sorted);
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
        m_is_sorted = false;
    }
    void sort()
    {
        choc::sorting::stable_sort(
            m_entries.begin(), m_entries.begin() + m_write_index,
            [](const Entry &a, const Entry &b) { return a.eventTime < b.eventTime; });
        m_is_sorted = true;
    }
    clap_input_events m_clap_in_events;
    clap_output_events m_clap_out_events;

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
    bool m_is_sorted = false;
};
} // namespace xenakios
