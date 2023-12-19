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
#include "concurrentqueue.h"
#include <chrono>
#include "../Plugins/noise-plethora/plugins/NoisePlethoraPlugin.hpp"
#include "../Plugins/noise-plethora/plugins/Banks.hpp"
#include "xapdsp.h"
#include "audio/choc_AudioFileFormat_WAV.h"

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
    using namespace xenakios;
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

void baz(auto a, auto b) { std::cout << a << " " << b << " " << a + b << "\n"; }

inline void test_auto() { baz(3, 2.5); }

using FifoType = moodycamel::ConcurrentQueue<uint8_t>;

template <typename EventType>
inline void push_clap_event_to_fifo(FifoType &fifo, const EventType *ev)
{
    const uint8_t *ev_as_bytes = reinterpret_cast<const uint8_t *>(ev);
    fifo.enqueue_bulk(ev_as_bytes, ev->header.size);
}

inline void test_moody()
{
    FifoType fifo{16384, 2, 1};
    for (int i = 0; i < 8; ++i)
    {
        auto ev = xenakios::make_event_note(0, CLAP_EVENT_NOTE_ON, 0, 0, 60 + i, 0, 0.5);
        push_clap_event_to_fifo(fifo, &ev);
    }
    std::cout << "fifo has " << fifo.size_approx() << " bytes\n";
}

struct GainOp
{
    float operator()(float input) { return input * gain; }
    std::array<float, 2> operator()(std::array<float, 2> input)
    {
        input[0] *= gain;
        input[1] *= gain;
        return input;
    }
    float gain = 1.0f;
};

template <typename DspOP> inline float operator|(float x, DspOP &op) { return op(x); }
template <typename DspOP> inline std::array<float, 2> operator|(std::array<float, 2> x, DspOP &op)
{
    return op(x);
}

inline void test_pipe()
{
    GainOp gain1;
    gain1.gain = 2.0f;
    GainOp gain2;
    gain2.gain = 2.0;
    auto output = std::array<float, 2>{1.0f, -1.0f} | gain1 | gain2;
    std::cout << output[0] << " " << output[1] << "\n";
}

void test_np_code()
{
    std::shared_ptr<NoisePlethoraPlugin> plug;
    std::string plugToCreate = "satanWorkout";
    std::unordered_map<int, std::string> availablePlugins;
    int k = 0;
    for (int i = 0; i < numBanks; ++i)
    {
        auto &bank = getBankForIndex(i);
        std::cout << "bank " << i << "\n";
        for (int j = 0; j < programsPerBank; ++j)
        {
            std::cout << "\t" << bank.getProgramName(j) << "\n";
            availablePlugins[k] = bank.getProgramName(j);
            ++k;
        }
    }
    plug = MyFactory::Instance()->Create(availablePlugins[0]);
    if (!plug)
    {
        std::cout << "could not create plugin\n";
        return;
    }

    double sr = 44100;
    StereoSimperSVF filter;
    filter.init();
    StereoSimperSVF dcblocker;
    dcblocker.setCoeff(0.0, 0.01, 1.0 / sr);
    dcblocker.init();
    int outlen = sr * 5;
    choc::audio::AudioFileProperties outfileprops;
    outfileprops.formatName = "WAV";
    outfileprops.bitDepth = choc::audio::BitDepth::float32;
    outfileprops.numChannels = 2;
    outfileprops.sampleRate = sr;
    choc::audio::WAVAudioFileFormat<true> wavformat;
    auto writer = wavformat.createWriter(
        R"(C:\develop\AudioPluginHost_mk2\audio\noise_plethora_out_03.wav)",
        outfileprops);
    choc::buffer::ChannelArrayBuffer<float> buf{2, (unsigned int)outlen};
    buf.clear();
    plug->init();
    plug->m_sr = sr;
    std::minstd_rand0 rng;
    std::uniform_real_distribution<float> whitenoise{-1.0f, 1.0f};
    for (int i = 0; i < outlen; ++i)
    {
        float p0 = 0.5 + 0.5 * std::sin(2 * 3.141592653 / sr * i * 0.3);
        float p1 = 0.5 + 0.5 * std::sin(2 * 3.141592653 / sr * i * 0.4);
        float fcutoff = 84.0 + 12.0 * std::sin(2 * 3.141592653 / sr * i * 4.0);
        filter.setCoeff(fcutoff, 0.7, 1.0 / sr);
        plug->process(p0, p1);
        float outL = plug->processGraph();
        float outR = outL;
        dcblocker.step<StereoSimperSVF::HP>(dcblocker, outL, outR);
        filter.step<StereoSimperSVF::LP>(filter, outL, outR);
        buf.getSample(0, i) = outL;
        buf.getSample(1, i) = outR;
        
    }
    writer->appendFrames(buf.getView());
}

int main()
{
    test_np_code();
    // test_pipe();
    // test_moody();
    // test_auto();
    //  test_alt_event_list();
    //  test_span();
    //  test_polym();
    return 0;
}
