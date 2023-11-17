#pragma once

#include <clap/helpers/event-list.hh>
#include "containers/choc_SingleReaderSingleWriterFIFO.h"

#define XENAKIOS_CLAP_NAMESPACE 11111

#define XENAKIOS_EVENT_CHANGEFILE 103
struct xenakios_event_change_file
{
    clap_event_header header;
    int target = 0; // which file should be changed
    char filepath[256];
};

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
        jassert(m_available > 0);
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
