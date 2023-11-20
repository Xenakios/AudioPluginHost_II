#include <iostream>
#include <memory>
#include <vector>
#include <span>
#include <array>
#include "clap/clap.h"
#include <algorithm>
#include <cassert>

class object_t
{
  public:
    template <typename T> object_t(T x) : self_(std::make_shared<model<T>>(std::move(x))) {}
    friend void draw(const object_t &x, std::ostream &out, size_t position)
    {
        x.self_->draw_(out, position);
    }

  private:
    struct concept_t
    {
        virtual ~concept_t() = default;
        virtual void draw_(std::ostream &, size_t) const = 0;
    };
    template <typename T> struct model : public concept_t
    {
        model(T x) : data_(std::move(x)) {}
        void draw_(std::ostream &out, size_t position) const override
        {
            draw(data_, out, position);
        }
        T data_;
    };
    std::shared_ptr<const concept_t> self_;
};

template <typename T> inline std::ostream &operator<<(std::ostream &out, const std::vector<T> &vec)
{
    out << "vector[";
    for (size_t i = 0; i < vec.size(); ++i)
    {
        out << vec[i] << " @ " << &vec[i] << " ";
        if (i < vec.size() - 1)
            out << ",";
    }
    out << "]\n";
    return out;
}

template <typename T> inline void draw(const T &data, std::ostream &out, size_t pos)
{
    out << std::string(pos, ' ') << data << " @ " << &data << "\n";
}

using doc_t = std::vector<object_t>;

inline void draw(const doc_t &data, std::ostream &out, size_t pos)
{
    out << std::string(pos, ' ') << "<doc>\n";
    for (const auto &e : data)
        draw(e, out, pos + 2);
    out << std::string(pos, ' ') << "</doc>\n";
}

class MyDocument
{
  public:
    MyDocument() = default;
    object_t m_name{std::string("Olio")};
    object_t m_vec0{std::vector<double>{}};
    object_t m_vec1{std::vector<int>{}};
};

inline void draw(const MyDocument &data, std::ostream &out, size_t pos)
{
    out << std::string(pos, ' ') << "<MyDocument>\n";
    std::cout << "name : ";
    draw(data.m_name, out, pos + 2);
    std::cout << "vector 0 : ";
    draw(data.m_vec0, out, pos + 2);
    std::cout << "vector 1 : ";
    draw(data.m_vec1, out, pos + 2);
    out << std::string(pos, ' ') << "</MyDocument>\n";
}

using history_t = std::vector<MyDocument>;

void test_polym()
{
    history_t history;
    MyDocument doc;
    history.emplace_back(doc);
    while (true)
    {
        char line[4096];
        std::cin.getline(line, 4096);
        std::string str(line);
        if (str == "quit")
            break;
        doc.m_name = str;
        history.emplace_back(doc);
    }
    for (auto &e : history)
    {
        std::cout << "------ Doc state ------\n";
        draw(e, std::cout, 0);
        std::cout << "-----------------------\n";
    }

    /*
    history_t history;
    doc_t doc;
    history.emplace_back(doc);
    doc.emplace_back(666);
    object_t vecob{std::vector<double>{0.42, 0.777, -99.99}};
    doc.emplace_back(vecob);
    doc.emplace_back(std::string("pippelji"));

    history.emplace_back(doc);
    doc.back() = std::string("pipuli");
    history.emplace_back(doc);
    doc.erase(doc.begin() + 0);
    history.emplace_back(doc);
    for (const auto &state : history)
    {
        std::cout << "------ Doc state ------\n";
        draw(state, std::cout, 0);
        std::cout << "-----------------------\n";
    }
    */
}

inline void f(std::span<float, 3> s)
{
    std::cout << "span has " << s.size() << " elements\n";
    s[0] = 666.0f;
    for (auto &e : s)
        std::cout << e << " ";
    std::cout << "\n";
}

inline void test_span()
{
    float arr[3] = {0.1f, 0.2f, 0.3f};
    f(arr);
    std::array<float, 3> stdarr{0.5f, 0.6f, 0.7f};
    f(stdarr);
    float arrerr[2] = {0.2f, 0.3f};
    // f(arrerr);
}

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

inline void test_alt_event_list()
{
    SortingEventList list;
    clap_event_param_mod mev;
    mev.header.time = 0;
    mev.param_id = 666;
    mev.amount = 0.42;
    list.pushEvent(&mev);
    clap_event_note nev;
    nev.header.time = 0;
    nev.channel = 0;
    nev.note_id = 10000;
    nev.key = 60;
    nev.velocity = 0.7;
    nev.port_index = 6;
    list.pushEvent(&nev);
}

int main()
{
    test_alt_event_list();
    // test_span();
    // test_polym();
    return 0;
}
