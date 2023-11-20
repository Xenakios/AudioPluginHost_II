#include <iostream>
#include <memory>
#include <vector>
#include <span>
#include <array>
#include "clap/clap.h"
#include <algorithm>
#include <cassert>
#include <random>
#include "xap_utils.h"

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


inline void test_alt_event_list()
{
    SortingEventList list;
    auto mev = make_event_param_mod(45, 666, 0.42, nullptr);
    list.pushEvent(&mev);
    auto nev = make_event_note(0, CLAP_EVENT_NOTE_ON, 0, 0, 60, 10000, 0.888);
    list.pushEvent(&nev);
    std::mt19937 rng;
    std::uniform_int_distribution<int> timedist(0, 512);
    std::uniform_real_distribution<double> valdist(0.0, 1.0);
    for (int i = 0; i < 100; ++i)
    {
        auto time = timedist(rng);
        auto val = valdist(rng);
        auto exev = make_event_note_expression(time, CLAP_NOTE_EXPRESSION_PAN, -1, -1, 60, -1, val);
        list.pushEvent(&exev);
    }
    list.sortEvents();
    for (int i = 0; i < list.size(); ++i)
    {
        auto ev = list.get(i);
        std::cout << i << "\t" << ev->time << "\t" << ev->type << "\n";
    }
}

int main()
{
    test_alt_event_list();
    // test_span();
    // test_polym();
    return 0;
}
