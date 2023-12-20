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
#include "containers/choc_NonAllocatingStableSort.h"
#include "sst/basic-blocks/modulators/SimpleLFO.h"

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
            std::cout << "update current point index to " << currentPointIndex << " at tpos " << t
                      << "\n";
        }
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

inline void test_envelope()
{
    Envelope<64> env;
    env.addPoint({0.0, 0.1});
    env.addPoint({10.0, 1.0});
    env.addPoint({20.0, 0.25});
    env.sortPoints();
}

template <size_t BLOCK_SIZE> struct SRProvider
{
    static constexpr size_t BLOCK_SIZE_OS = BLOCK_SIZE * 2;
    double samplerate = 44100;
    alignas(32) float table_envrate_linear[512];
    void initTables()
    {
        double dsamplerate_os = samplerate * 2;
        for (int i = 0; i < 512; ++i)
        {
            double k =
                dsamplerate_os * pow(2.0, (((double)i - 256.0) / 16.0)) / (double)BLOCK_SIZE_OS;
            table_envrate_linear[i] = (float)(1.f / k);
        }
    }
    float envelope_rate_linear_nowrap(float x)
    {
        x *= 16.f;
        x += 256.f;
        int e = std::clamp<int>((int)x, 0, 0x1ff - 1);

        float a = x - (float)e;

        return (1 - a) * table_envrate_linear[e & 0x1ff] +
               a * table_envrate_linear[(e + 1) & 0x1ff];
    }
};

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
            std::cout << "\t" << bank.getProgramName(j) << "\t\t" << k << "\n";
            availablePlugins[k] = bank.getProgramName(j);
            ++k;
        }
    }
    int plugIndex = 4;
    // std::cout << "which to creare?\n";
    // std::cin >> plugIndex;
    plug = MyFactory::Instance()->Create(availablePlugins[plugIndex]);
    if (!plug)
    {
        std::cout << "could not create plugin\n";
        return;
    }
    std::cout << "created " << availablePlugins[plugIndex] << "\n";
    constexpr int BLOCK_SIZE = 64;
    SRProvider<BLOCK_SIZE> srprovider;
    srprovider.samplerate = 44100;
    srprovider.initTables();
    using LFOType = sst::basic_blocks::modulators::SimpleLFO<SRProvider<BLOCK_SIZE>, BLOCK_SIZE>;
    LFOType lfo1(&srprovider, 0);
    LFOType lfo2(&srprovider, 1);
    LFOType lfo3(&srprovider, 2);

    Envelope<BLOCK_SIZE> filtenv{{{0.0, 0.0}, {2.0, 48.0}, {3.0, 90.0}, {8.0, 80.0}, {10.0, 48.0}}};
    /*
    filtenv.addPoint({0.0, 0.0});
    filtenv.addPoint({2.0, 0.0});
    filtenv.addPoint({3.0, 90.0});
    filtenv.addPoint({10.0, 48.0});
    filtenv.sortPoints();
    */

    Envelope<BLOCK_SIZE> volenv;
    volenv.addPoint({0.0, -100.0});
    volenv.addPoint({1.0, 0});
    volenv.addPoint({5.0, -3, EnvelopePoint::Shape::Hold});
    volenv.addPoint({6.000, -100, EnvelopePoint::Shape::Linear});
    // volenv.addPoint({5.005, 1.0});
    volenv.addPoint({9.0, 0.0});
    volenv.addPoint({10.0, -100.0});
    volenv.sortPoints();

    StereoSimperSVF filter;
    filter.init();
    StereoSimperSVF dcblocker;
    dcblocker.setCoeff(12.0, 0.01, 1.0 / srprovider.samplerate);
    dcblocker.init();
    int outlen = srprovider.samplerate * 30;
    unsigned int numoutchans = 6;
    choc::audio::AudioFileProperties outfileprops;
    outfileprops.formatName = "WAV";
    outfileprops.bitDepth = choc::audio::BitDepth::float32;
    outfileprops.numChannels = numoutchans;
    outfileprops.sampleRate = srprovider.samplerate;
    choc::audio::WAVAudioFileFormat<true> wavformat;
    auto writer = wavformat.createWriter(
        R"(C:\develop\AudioPluginHost_mk2\audio\noise_plethora_out_03.wav)", outfileprops);
    choc::buffer::ChannelArrayBuffer<float> buf{numoutchans, (unsigned int)outlen};
    buf.clear();
    plug->init();
    plug->m_sr = srprovider.samplerate;
    int lfocounter = 0;
    // unsafe raw access but we know what we are doing ;-)
    auto chansdata = buf.getView().data.channels;

    for (int i = 0; i < outlen; ++i)
    {
        double pos_in_secs = i / outfileprops.sampleRate;
        pos_in_secs = std::fmod(pos_in_secs, 10.0);
        if (lfocounter == 0)
        {
            lfo1.process_block(0.5f, 0.0f, LFOType::Shape::SINE, false);
            lfo2.process_block(2.6f, 0.8f, LFOType::Shape::SH_NOISE, false);
            lfo3.process_block(-0.43f, 0.0f, LFOType::Shape::SINE, false);
            // float fcutoff = 84.0 + 9.0 * lfo3.outputBlock[0];
            filtenv.processBlock(pos_in_secs, outfileprops.sampleRate, 2);

            float fcutoff = filtenv.outputBlock[0];
            filter.setCoeff(fcutoff, 0.7, 1.0 / srprovider.samplerate);
            volenv.processBlock(pos_in_secs, outfileprops.sampleRate, 0);
        }
        float gain = xenakios::decibelsToGain(volenv.outputBlock[lfocounter]);
        ++lfocounter;
        if (lfocounter == BLOCK_SIZE)
            lfocounter = 0;
        float p0 = 0.5 + 0.01 * lfo1.outputBlock[0];
        float p1 = 0.5 + 0.05 * lfo2.outputBlock[0];
        // float fcutoff =
        plug->process(p0, p1);
        float outL = plug->processGraph();
        float outR = outL;
        dcblocker.step<StereoSimperSVF::HP>(dcblocker, outL, outR);
        filter.step<StereoSimperSVF::LP>(filter, outL, outR);
        chansdata[0][i] = outL * gain;
        chansdata[1][i] = outR * gain;
        chansdata[2][i] = p0;
        chansdata[3][i] = p1;
        chansdata[4][i] = xenakios::mapvalue(filtenv.outputBlock[0], 0.0f, 100.0f, 0.0f, 1.0f);
        chansdata[5][i] = gain;
    }
    writer->appendFrames(buf.getView());
}

int main()
{
    // test_envelope();
    test_np_code();
    // test_pipe();
    // test_moody();
    // test_auto();
    //  test_alt_event_list();
    //  test_span();
    //  test_polym();
    return 0;
}
