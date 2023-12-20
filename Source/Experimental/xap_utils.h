#pragma once

#include <clap/helpers/event-list.hh>
#include "containers/choc_SingleReaderSingleWriterFIFO.h"
#include "clap/clap.h"
#include <algorithm>
// #include "JuceHeader.h"

#define XENAKIOS_CLAP_NAMESPACE 11111

#define XENAKIOS_EVENT_CHANGEFILE 103
struct xenakios_event_change_file
{
    clap_event_header header;
    int target = 0; // which file should be changed
    char filepath[256];
};

template <typename T> inline clap_id to_clap_id(T x) { return static_cast<clap_id>(x); }

namespace xenakios
{

/** Remaps a value from a source range to a target range. */
template <typename Type>
Type mapvalue(Type sourceValue, Type sourceRangeMin, Type sourceRangeMax, Type targetRangeMin,
              Type targetRangeMax)
{
    // jassert (! approximatelyEqual (sourceRangeMax, sourceRangeMin)); // mapping from a range of
    // zero will produce NaN!
    return targetRangeMin + ((targetRangeMax - targetRangeMin) * (sourceValue - sourceRangeMin)) /
                                (sourceRangeMax - sourceRangeMin);
}

struct CrossThreadMessage
{
    CrossThreadMessage() {}
    CrossThreadMessage(clap_id parId, int eType, double val)
        : paramId(parId), eventType(eType), value(val)
    {
    }
    template <typename T> CrossThreadMessage withParamId(T id)
    {
        auto result = *this;
        result.paramId = static_cast<clap_id>(id);
        return result;
    }
    CrossThreadMessage withType(int etype)
    {
        auto result = *this;
        result.eventType = etype;
        return result;
    }
    CrossThreadMessage withValue(double v)
    {
        auto result = *this;
        result.value = v;
        return result;
    }
    template <typename T> CrossThreadMessage asParamChange(T parid, double v)
    {
        auto result = *this;
        result.eventType = CLAP_EVENT_PARAM_VALUE;
        result.paramId = static_cast<clap_id>(parid);
        result.value = v;
        return result;
    }
    clap_id paramId = CLAP_INVALID_ID;
    int eventType = CLAP_EVENT_PARAM_VALUE;
    double value = 0.0;
};

// based on event list from free-audio clap-wrapper
//
class SortingEventList
{
  public:
    union clap_multi_event
    {
        clap_event_header_t header;
        clap_event_note_t note;
        clap_event_midi_t midi;
        clap_event_midi2_t midi2;
        clap_event_midi_sysex_t sysex;
        clap_event_param_value_t param;
        clap_event_param_mod_t parammod;
        clap_event_param_gesture_t paramgest;
        clap_event_note_expression_t noteexpression;
        clap_event_transport_t transport;
    };
    SortingEventList()
    {
        events.reserve(512);
        event_indices.reserve(512);
        input_events.ctx = this;
        input_events.size = [](const struct clap_input_events *list) {
            auto self = (SortingEventList *)list->ctx;
            return self->size();
        };
        input_events.get = [](const struct clap_input_events *list, uint32_t index) {
            auto self = (SortingEventList *)list->ctx;
            return self->get(index);
        };
    }
    clap_input_events input_events;
    void clear()
    {
        events.clear();
        event_indices.clear();
        sorted = false;
    }
    uint32_t size() const { return events.size(); }
    const clap_event_header_t *get(uint32_t index) const
    {
        if (events.size() > index)
        {
            assert(sorted);
            auto realindex = event_indices[index];
            return &events[realindex].header;
        }
        return nullptr;
    }
    template <typename EventType> void pushEvent(EventType *ev)
    {
        event_indices.push_back(events.size());
        events.push_back(*(clap_multi_event *)ev);
        sorted = false;
    }
    void sortEvents()
    {
        // just sorting the index
        // an item must be sorted to front of
        // if the timestamp if event[a] is earlier than
        // the timestamp of event[b].
        // if they have the same timestamp, the index must be preserved
        std::sort(event_indices.begin(), event_indices.end(),
                  [&](size_t const &a, size_t const &b) {
                      auto t1 = events[a].header.time;
                      auto t2 = events[b].header.time;
                      return (t1 == t2) ? (a < b) : (t1 < t2);
                  });
        sorted = true;
    }

  private:
    std::vector<clap_multi_event> events;
    std::vector<size_t> event_indices;
    bool sorted = false;
};

inline clap_event_param_gesture make_event_param_gesture(uint32_t time, clap_id paramid,
                                                         bool is_begin)
{
    clap_event_param_gesture result;
    result.header.flags = 0;
    result.header.size = sizeof(clap_event_param_gesture);
    result.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    if (is_begin)
        result.header.type = CLAP_EVENT_PARAM_GESTURE_BEGIN;
    else
        result.header.type = CLAP_EVENT_PARAM_GESTURE_END;
    result.header.time = time;
    result.param_id = paramid;
    return result;
}

inline clap_event_param_value make_event_param_value(uint32_t time, clap_id paramid, double value,
                                                     void *cookie, int16_t port = -1,
                                                     int16_t channel = -1, int16_t key = -1,
                                                     int32_t noteid = -1, uint32_t flags = 0)
{
    clap_event_param_value result;
    result.header.flags = flags;
    result.header.size = sizeof(clap_event_param_value);
    result.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    result.header.type = CLAP_EVENT_PARAM_VALUE;
    result.header.time = time;
    result.param_id = paramid;
    result.value = value;
    result.cookie = cookie;
    result.port_index = port;
    result.channel = channel;
    result.key = key;
    result.note_id = noteid;
    return result;
}

inline clap_event_param_mod make_event_param_mod(uint32_t time, clap_id paramid, double amount,
                                                 void *cookie, int16_t port = -1,
                                                 int16_t channel = -1, int16_t key = -1,
                                                 int32_t noteid = -1, uint32_t flags = 0)
{
    clap_event_param_mod result;
    result.header.flags = flags;
    result.header.size = sizeof(clap_event_param_value);
    result.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    result.header.type = CLAP_EVENT_PARAM_VALUE;
    result.header.time = time;
    result.param_id = paramid;
    result.amount = amount;
    result.cookie = cookie;
    result.port_index = port;
    result.channel = channel;
    result.key = key;
    result.note_id = noteid;
    return result;
}

inline clap_event_note make_event_note(uint32_t time, uint16_t evtype, int16_t port,
                                       int16_t channel, int16_t key, int32_t note_id,
                                       double velocity, uint32_t flags = 0)
{
    clap_event_note result;
    result.header.flags = flags;
    result.header.size = sizeof(clap_event_note);
    result.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    result.header.type = evtype;
    result.header.time = time;
    result.port_index = port;
    result.channel = channel;
    result.key = key;
    result.note_id = note_id;
    result.velocity = velocity;
    return result;
}

inline clap_event_note_expression
make_event_note_expression(uint32_t time, clap_note_expression net, int16_t port, int16_t channel,
                           int16_t key, int32_t note_id, double value, uint32_t flags = 0)
{
    clap_event_note_expression result;
    result.header.flags = flags;
    result.header.size = sizeof(clap_event_note_expression);
    result.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    result.header.type = CLAP_EVENT_NOTE_EXPRESSION;
    result.header.time = time;
    result.port_index = port;
    result.channel = channel;
    result.key = key;
    result.note_id = note_id;
    result.expression_id = net;
    result.value = value;
    return result;
}

inline void pushParamEvent(clap::helpers::EventList &elist, bool is_mod, uint32_t timeStamp,
                           clap_id paramId, double value)
{
    if (!is_mod)
    {
        clap_event_param_value pv;
        pv.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        pv.header.size = sizeof(clap_event_param_value);
        pv.header.flags = 0;
        pv.header.time = timeStamp;
        pv.header.type = CLAP_EVENT_PARAM_VALUE;
        pv.cookie = nullptr;
        pv.param_id = paramId;
        pv.value = value;
        pv.channel = -1;
        pv.key = -1;
        pv.note_id = -1;
        pv.port_index = -1;
        elist.push(reinterpret_cast<const clap_event_header *>(&pv));
    }
    else
    {
        clap_event_param_mod pv;
        pv.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        pv.header.size = sizeof(clap_event_param_mod);
        pv.header.flags = 0;
        pv.header.time = timeStamp;
        pv.header.type = CLAP_EVENT_PARAM_MOD;
        pv.cookie = nullptr;
        pv.param_id = paramId;
        pv.amount = value;
        pv.channel = -1;
        pv.key = -1;
        pv.note_id = -1;
        pv.port_index = -1;
        elist.push(reinterpret_cast<const clap_event_header *>(&pv));
    }
}

} // namespace xenakios

inline clap_event_param_value makeClapParameterValueEvent(int time, clap_id paramId, double value,
                                                          void *cookie = nullptr, int port = -1,
                                                          int channel = -1, int key = -1,
                                                          int noteid = -1)
{
    clap_event_param_value pv;
    pv.header.time = time;
    pv.header.size = sizeof(clap_event_param_value);
    pv.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    pv.header.flags = 0;
    pv.header.type = CLAP_EVENT_PARAM_VALUE;
    pv.channel = channel;
    pv.cookie = cookie;
    pv.key = key;
    pv.note_id = noteid;
    pv.param_id = paramId;
    pv.port_index = port;
    pv.value = value;
    return pv;
}

inline clap_event_param_mod makeClapParameterModEvent(int time, clap_id paramId, double value,
                                                      void *cookie = nullptr, int port = -1,
                                                      int channel = -1, int key = -1,
                                                      int noteid = -1)
{
    clap_event_param_mod pv;
    pv.header.time = time;
    pv.header.size = sizeof(clap_event_param_mod);
    pv.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    pv.header.flags = 0;
    pv.header.type = CLAP_EVENT_PARAM_MOD;
    pv.channel = channel;
    pv.cookie = cookie;
    pv.key = key;
    pv.note_id = noteid;
    pv.param_id = paramId;
    pv.port_index = port;
    pv.amount = value;
    return pv;
}

template <typename T, typename... Args> inline bool equalsToAny(const T &a, Args &&...args)
{
    return ((a == args) || ...);
}

void printClapEvents(clap::helpers::EventList &elist);

template <typename T>
class SingleReaderSingleWriterFifoHelper : public choc::fifo::SingleReaderSingleWriterFIFO<T>
{
  public:
    SingleReaderSingleWriterFifoHelper(size_t initial_size = 2048)
    {
        choc::fifo::SingleReaderSingleWriterFIFO<T>::reset(initial_size);
    }
};

template <typename T, size_t Size> class SimpleRingBuffer
{
  public:
    SimpleRingBuffer() { std::fill(m_buffer.begin(), m_buffer.end(), T{}); }
    void push(T val)
    {
        m_buffer[m_write_index] = val;
        ++m_available;
        ++m_write_index;
        if (m_write_index == Size)
        {
            m_write_index = 0;
        }
    }
    T pop()
    {
        assert(m_available > 0);
        T result = m_buffer[m_read_index];
        ++m_read_index;
        --m_available;
        if (m_read_index == Size)
        {
            m_read_index = 0;
        }
        return result;
    }
    int available() const { return m_available; }
    int size() const { return Size; }

  private:
    std::array<T, Size> m_buffer;
    int m_write_index = 0;
    int m_read_index = 0;
    int m_available = 0;
};

template <typename Key, typename Value> class KeyValueTable
{
  public:
    KeyValueTable() { entries.reserve(64); }
    Value &operator[](const Key &k)
    {
        for (auto &e : entries)
        {
            if (e.key == k)
                return e.value;
        }
        Entry entry;
        entry.key = k;
        entry.value = Value{};
        entries.push_back(entry);
        return entries.back().value;
    }
    struct Entry
    {
        Key key;
        Value value;
    };
    auto begin() { return entries.begin(); }
    auto end() { return entries.end(); }

  private:
    std::vector<Entry> entries;
};

inline clap_param_info makeParamInfo(clap_id paramId, std::string name, double minval,
                                     double maxval, double defaultVal, clap_param_info_flags flags,
                                     void *cookie = nullptr)
{
    clap_param_info result;
    result.cookie = cookie;
    result.default_value = defaultVal;
    result.min_value = minval;
    result.max_value = maxval;
    result.id = paramId;
    result.flags = flags;
    auto ptr = name.c_str();
    strcpy_s(result.name, ptr);
    result.module[0] = 0;
    return result;
}

class VecToStreamAdapter
{
    std::vector<unsigned char> &m_src;
    size_t readpos = 0;

  public:
    VecToStreamAdapter(std::vector<unsigned char> &src) : m_src(src) {}
    size_t read(void *dest, size_t sz)
    {
        auto o = (unsigned char *)dest;
        if (readpos >= m_src.size())
            return 0;
        for (size_t i = 0; i < sz; ++i)
        {
            o[i] = m_src[readpos];
            ++readpos;
            if (readpos >= m_src.size())
                return i + 1;
        }
        return sz;
    }
};
