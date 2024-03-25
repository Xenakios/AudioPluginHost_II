#pragma once

#include <clap/helpers/event-list.hh>
#include "containers/choc_SingleReaderSingleWriterFIFO.h"
#include "clap/clap.h"
#include <algorithm>
#include <optional>
#include "containers/choc_NonAllocatingStableSort.h"

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

template <typename Type>
static Type decibelsToGain(Type decibels, Type minusInfinityDb = Type(-100))
{
    return decibels > minusInfinityDb ? std::pow(Type(10.0), decibels * Type(0.05)) : Type();
}

class EnvelopePoint
{
  public:
    enum class Shape
    {
        Linear,
        Hold,
        Last
    };
    EnvelopePoint() {}
    EnvelopePoint(double x, double y, Shape s = Shape::Linear, double p0 = 0.0, double p1 = 0.0)
        : m_x(x), m_y(y), m_shape(s), m_p0(p0), m_p1(p1)
    {
    }
    double getX() const { return m_x; }
    double getY() const { return m_y; }
    Shape getShape() const { return m_shape; }

  private:
    double m_x = 0.0;
    double m_y = 0.0;
    double m_p0 = 0.0;
    double m_p1 = 0.0;
    Shape m_shape = Shape::Linear;
};

// Simple breakpoint envelope class, modelled after the SST LFO.
// Output is always calculated into the outputBlock array.
// This aims to be as simple as possible, to allow composing
// more complicated things elsewhere.
template <size_t BLOCK_SIZE = 64> class Envelope
{
  public:
    float outputBlock[BLOCK_SIZE];
    void clearOutputBlock()
    {
        for (int i = 0; i < BLOCK_SIZE; ++i)
            outputBlock[i] = 0.0f;
    }
    Envelope(std::optional<double> defaultPointValue = {})
    {
        m_points.reserve(16);
        if (defaultPointValue)
            addPoint({0.0, *defaultPointValue});
        clearOutputBlock();
    }
    Envelope(std::vector<EnvelopePoint> points) : m_points(std::move(points))
    {
        sortPoints();
        clearOutputBlock();
    }
    void addPoint(EnvelopePoint pt)
    {
        m_points.push_back(pt);
        m_sorted = false;
    }
    /*
    void addPoint(double x, double y)
    {
        m_points.emplace_back(x, y);
        m_sorted = false;
    }
    */
    void removeEnvelopePointAtIndex(size_t index) { m_points.erase(m_points.begin() + index); }
    // use carefully, only when you are going to add at least one point right after this
    void clearAllPoints()
    {
        m_points.clear();
        m_sorted = false;
    }
    // value = normalized 0..1 position in the envelope segment
    static double getShapedValue(double value, EnvelopePoint::Shape shape, double p0, double p1)
    {
        if (shape == EnvelopePoint::Shape::Linear)
            return value;
        // holds the value for 99% the segment length, then ramps to the next value
        // literal sudden jump is almost never useful, but we might want to support that too...
        if (shape == EnvelopePoint::Shape::Hold)
        {
            // we might want to somehow make this work based on samples/time,
            // but percentage will have to work for now
            if (value < 0.99)
                return 0.0;
            return xenakios::mapvalue(value, 0.99, 1.0, 0.0, 1.0);
        }
        return value;
    }
    size_t getNumPoints() const { return m_points.size(); }
    // int because we want to allow negative index...
    const EnvelopePoint &getPointSafe(int index) const
    {
        if (index < 0)
            return m_points.front();
        if (index >= m_points.size())
            return m_points.back();
        return m_points[index];
    }
    void sortPoints()
    {
        choc::sorting::stable_sort(
            m_points.begin(), m_points.end(),
            [](const EnvelopePoint &a, const EnvelopePoint &b) { return a.getX() < b.getX(); });
        m_sorted = true;
    }
    int currentPointIndex = -1;
    void updateCurrentPointIndex(double t)
    {
        int newIndex = currentPointIndex;
        if (t < m_points.front().getX())
            newIndex = 0;
        else if (t > m_points.back().getX())
            newIndex = m_points.size() - 1;
        else
        {
            for (int i = 0; i < m_points.size(); ++i)
            {
                if (t >= m_points[i].getX())
                {
                    newIndex = i;
                }
            }
        }

        if (newIndex != currentPointIndex)
        {
            currentPointIndex = newIndex;
            // std::cout << "update current point index to " << currentPointIndex << " at tpos " <<
            // t
            //           << "\n";
        }
    }
    double getValueAtPosition(double pos, double sr)
    {
        if (!m_sorted)
            sortPoints();
        processBlock(pos, sr, 2);
        return outputBlock[0];
    }
    // interpolate_mode :
    // 0 : sample accurately interpolates into the outputBlock
    // 1 : fills the output block with the same sampled value from the envelope at the timepos
    // 2 : sets only the first outputBlock element into the sampled value from the envelope at the
    // timepos, useful if you really know you are never going to care about about the other array
    // elements
    void processBlock(double timepos, double samplerate, int interpolate_mode)
    {
        // behavior would be undefined if the envelope points are not sorted or if no points
        assert(m_sorted && m_points.size() > 0);
        if (currentPointIndex == -1 || timepos < m_points[currentPointIndex].getX() ||
            timepos >= getPointSafe(currentPointIndex + 1).getX())
        {
            updateCurrentPointIndex(timepos);
        }

        int index0 = currentPointIndex;
        assert(index0 >= 0);
        auto &pt0 = getPointSafe(index0);
        auto &pt1 = getPointSafe(index0 + 1);
        double x0 = pt0.getX();
        double x1 = pt1.getX();
        double y0 = pt0.getY();
        double y1 = pt1.getY();
        if (interpolate_mode > 0)
        {
            double outvalue = x0;
            double xdiff = x1 - x0;
            if (xdiff < 0.00001)
                outvalue = y1;
            else
            {
                double ydiff = y1 - y0;
                outvalue = y0 + ydiff * ((1.0 / xdiff * (timepos - x0)));
            }
            if (interpolate_mode == 1)
            {
                for (int i = 0; i < BLOCK_SIZE; ++i)
                {
                    outputBlock[i] = outvalue;
                }
            }
            else
                outputBlock[0] = outvalue;

            return;
        }
        assert(samplerate > 0.0);
        const double invsr = 1.0 / samplerate;
        auto shape = pt0.getShape();
        for (int i = 0; i < BLOCK_SIZE; ++i)
        {
            double outvalue = x0;
            double xdiff = x1 - x0;
            if (xdiff < 0.00001)
                outvalue = y1;
            else
            {
                double ydiff = y1 - y0;
                double normpos = ((1.0 / xdiff * (timepos - x0)));
                normpos = Envelope::getShapedValue(normpos, shape, 0.0, 0.0);
                outvalue = y0 + ydiff * normpos;
            }
            outputBlock[i] = outvalue;
            timepos += invsr;
            // we may get to the next envelope point within the block, so
            // advance and update as needed
            if (timepos >= x1)
            {
                ++index0;
                auto &tpt0 = getPointSafe(index0);
                auto &tpt1 = getPointSafe(index0 + 1);
                x0 = tpt0.getX();
                x1 = tpt1.getX();
                y0 = tpt0.getY();
                y1 = tpt1.getY();
                shape = tpt0.getShape();
            }
        }
    }

  private:
    std::vector<EnvelopePoint> m_points;
    bool m_sorted = false;
};

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
    result.header.size = sizeof(clap_event_param_mod);
    result.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    result.header.type = CLAP_EVENT_PARAM_MOD;
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
    assert(channel >= -1 && channel < 16);
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

// note that limits can't be the same and the loop may run for long!
// looks like this is broken...need to explore further
template <typename T> inline T wrap_value(const T minval, const T val, const T maxval)
{
    T temp = val;
    while (temp < minval || temp > maxval)
    {
        if (temp < minval)
            temp = maxval - (minval - temp);
        if (temp > maxval)
            temp = minval - (maxval - temp);
    }
    return temp;
}

// note that limits can't be the same and the loop may run for long!
template <typename T> inline T reflect_value(const T minval, const T val, const T maxval)
{
    T temp = val;
    while (temp < minval || temp > maxval)
    {
        if (temp < minval)
            temp = minval + (minval - temp);
        if (temp > maxval)
            temp = maxval + (maxval - temp);
    }
    return temp;
}

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

#define XENAKIOS_STRING_MSG 60000

struct clap_event_xen_string
{
    clap_event_header header;
    // which string property to change
    int32_t target;
    // owned by host, do not free, do not cache, do not mutate
    // only immediately use or copy the contents
    char* str;
};
