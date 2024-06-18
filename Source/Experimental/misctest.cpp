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
#include "xapdsp.h"
#include "audio/choc_AudioFileFormat_WAV.h"
#include "containers/choc_NonAllocatingStableSort.h"
#include "sst/basic-blocks/modulators/SimpleLFO.h"
#include "sst/basic-blocks/dsp/LanczosResampler.h"
#include "sst/basic-blocks/dsp/FollowSlewAndSmooth.h"
#include "sst/basic-blocks/mod-matrix/ModMatrix.h"
#include "offlineclaphost.h"
#include "gui/choc_DesktopWindow.h"
#include "gui/choc_MessageLoop.h"
#include "gui/choc_WebView.h"
#include "text/choc_Files.h"
#include "RtAudio.h"
#include "xaudiograph.h"
#include "dejavurandom.h"

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

inline void test_envelope()
{
    xenakios::Envelope<64> env;
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
#ifdef NPLETHORABUILT
void test_np_code()
{
    std::unique_ptr<NoisePlethoraPlugin> plug;
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

    xenakios::Envelope<BLOCK_SIZE> filtenv{
        {{0.0, 0.0}, {2.0, 48.0}, {3.0, 90.0}, {8.0, 80.0}, {10.0, 48.0}}};
    /*
    filtenv.addPoint({0.0, 0.0});
    filtenv.addPoint({2.0, 0.0});
    filtenv.addPoint({3.0, 90.0});
    filtenv.addPoint({10.0, 48.0});
    filtenv.sortPoints();
    */

    xenakios::Envelope<BLOCK_SIZE> volenv;
    volenv.addPoint({0.0, -100.0});
    volenv.addPoint({1.0, 0});
    volenv.addPoint({5.0, -3, xenakios::EnvelopePoint::Shape::Hold});
    volenv.addPoint({6.000, -100, xenakios::EnvelopePoint::Shape::Linear});
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
#endif

inline void test_lanczos()
{
    const size_t BLOCK_SIZE = 32;

    choc::audio::AudioFileFormatList aflist;
    aflist.addFormat(std::make_unique<choc::audio::WAVAudioFileFormat<false>>());
    std::string infilename = R"(C:\MusicAudio\sourcesamples\count_96000.wav)";
    // std::string infilename = R"(C:\MusicAudio\sourcesamples\test_signals\440hz_sine_0db.wav)";
    auto reader = aflist.createReader(infilename);

    if (reader)
    {
        auto inprops = reader->getProperties();
        double outsr = 44100.0;
        SRProvider<BLOCK_SIZE> srprovider;
        srprovider.samplerate = outsr;
        srprovider.initTables();
        using LFOType =
            sst::basic_blocks::modulators::SimpleLFO<SRProvider<BLOCK_SIZE>, BLOCK_SIZE>;
        LFOType lfo1(&srprovider, 1);
        xenakios::Envelope<BLOCK_SIZE> rate_env{
            {{0.0, 36.0}, {2.5, -12.0}, {5.0, 0.0}, {5.1, 6.0}, {8.0, 6.0}, {10.0, -7.0}}};
        xenakios::Envelope<BLOCK_SIZE> deform_env{
            {{0.0, -1.0}, {10.0, -1.0}, {12.0, 0.0}, {20.0, 0.0}, {22.0, 1.0}, {30.0, 1.0}}};
        // std::cout << inprops.getDescription() << "\n";
        sst::basic_blocks::dsp::LanczosResampler<BLOCK_SIZE> rs(inprops.sampleRate,
                                                                inprops.sampleRate);

        unsigned int startsample = inprops.sampleRate * 0.2;
        unsigned int endsample = inprops.sampleRate * 0.25;
        unsigned int incounter = startsample;
        unsigned int outcounter = 0;
        choc::buffer::ChannelArrayBuffer<float> readbuf{inprops.numChannels,
                                                        (unsigned int)inprops.numFrames};
        readbuf.clear();
        unsigned int outlenframes = outsr * 30;
        choc::buffer::ChannelArrayBuffer<float> outputbuf{inprops.numChannels,
                                                          (unsigned int)outlenframes + 64};
        outputbuf.clear();
        reader->readFrames(0, readbuf.getView());
        float rs_buf_0[BLOCK_SIZE];
        float rs_buf_1[BLOCK_SIZE];
        double srcompratio = outsr / inprops.sampleRate;
        while (outcounter < outlenframes)
        {
            double opossecs = outcounter / outsr;
            rate_env.processBlock(opossecs, outsr, 2);
            deform_env.processBlock(opossecs, outsr, 2);

            lfo1.process_block(4.0, deform_env.outputBlock[0], LFOType::Shape::SH_NOISE, false);
            double semitones = 12.0 * lfo1.outputBlock[0]; // rate_env.outputBlock[0];
            semitones = std::clamp(semitones, -12.0, 12.0);
            double rate = std::pow(2.0, semitones / 12.0);
            // rate = 1.0;
            rs.sri = inprops.sampleRate;
            rs.sro = inprops.sampleRate / rate * srcompratio;
            rs.dPhaseO = rs.sri / rs.sro;
            auto wanted = rs.inputsRequiredToGenerateOutputs(BLOCK_SIZE);
            for (size_t i = 0; i < wanted; ++i)
            {
                if (inprops.numChannels == 1)
                    rs.push(readbuf.getSample(0, incounter), readbuf.getSample(0, incounter));
                else
                    rs.push(readbuf.getSample(0, incounter), readbuf.getSample(1, incounter));
                ++incounter;
                if (incounter >= endsample)
                    incounter = startsample;
            }
            rs.populateNext(rs_buf_0, rs_buf_1, BLOCK_SIZE);

            for (int i = 0; i < BLOCK_SIZE; ++i)
            {
                outputbuf.getSample(0, outcounter + i) = 0.9 * rs_buf_0[i];
                if (inprops.numChannels == 2)
                    outputbuf.getSample(1, outcounter + i) = 0.9 * rs_buf_1[i];
            }
            outcounter += BLOCK_SIZE;
        }
        choc::audio::AudioFileProperties outfileprops;
        outfileprops.formatName = "WAV";
        outfileprops.bitDepth = choc::audio::BitDepth::float32;
        outfileprops.numChannels = inprops.numChannels;
        outfileprops.sampleRate = outsr;
        choc::audio::WAVAudioFileFormat<true> wavformat;
        auto writer = wavformat.createWriter(
            R"(C:\MusicAudio\sourcesamples\test_signals\lanczos\lanczos1.wav)", outfileprops);
        if (writer)
        {
            writer->appendFrames(outputbuf.getView());
        }
    }
    else
        std::cout << "Could not create reader\n";
}

template <size_t Size> class LookUpTable
{
  public:
    LookUpTable(std::array<float, Size> tab)
    {
        for (size_t i = 0; i < Size; ++i)
            m_table[i] = tab[i];
        m_minv = -1.0f;
        m_maxv = 1.0f;
        m_table[Size] = m_table[Size - 1];
    }
    LookUpTable(std::function<float(float)> func, float minv, float maxv)
        : m_minv(minv), m_maxv(maxv)
    {
        for (size_t i = 0; i < Size; ++i)
            m_table[i] = func(xenakios::mapvalue<float>(i, 0, Size - 1, minv, maxv));
        m_table[Size] = m_table[Size - 1];
    }
    float operator()(float x)
    {
        if (x < m_minv)
            return m_table[0];
        else if (x > m_maxv)
            return m_table.back();
        size_t index0 = xenakios::mapvalue<float>(x, m_minv, m_maxv, 0, Size - 1);
        size_t index1 = index0 + 1;
        float y0 = m_table[index0];
        float y1 = m_table[index1];
        float frac = x - std::round(x);
        return y0 + (y1 - y0) * frac;
    }
    std::array<float, Size + 1> m_table;

  private:
    float m_minv = 0.0f;
    float m_maxv = 1.0f;
};

inline void test_fb_osc()
{
    double outsr = 44100;
    choc::audio::AudioFileProperties outfileprops;
    outfileprops.formatName = "WAV";
    outfileprops.bitDepth = choc::audio::BitDepth::float32;
    outfileprops.numChannels = 1;
    outfileprops.sampleRate = outsr;
    choc::audio::WAVAudioFileFormat<true> wavformat;
    auto writer = wavformat.createWriter(
        R"(C:\MusicAudio\sourcesamples\test_signals\lanczos\fb_osc01.wav)", outfileprops);
    int outlenframes = outsr * 5.0;
    choc::buffer::ChannelArrayBuffer<float> outputbuf{outfileprops.numChannels,
                                                      (unsigned int)outlenframes + 64};
    outputbuf.clear();

    LookUpTable<128> tab{[](float x) { return -x; }, -1.0f, 1.0f};
    std::mt19937 rng(9);
    std::uniform_int_distribution<int> dist(0, 127);
    std::uniform_real_distribution<float> distf(-1.0f, 1.0f);
    for (int i = 0; i < 32; ++i)
        tab.m_table[dist(rng)] = distf(rng);
    float s0 = 0.065f;
    const size_t delaylen = 256;
    SimpleRingBuffer<float, delaylen> delay;
    for (size_t i = 0; i < delaylen; ++i)
        delay.push(0.00f);
    float fb = 0.0f;
    for (int i = 0; i < outlenframes; ++i)
    {

        float in = delay.pop() + fb;
        in = std::clamp(in, -1.0f, 1.0f);
        float s1 = tab(in);
        // assert(s1 >= -1.0f && s1 < 1.0f);
        // std::cout << s0 << "\n";
        outputbuf.getSample(0, i) = s1 * 0.5;
        delay.push(s1);
        fb = s0 * -0.3;
        s0 = s1;
    }
    if (writer)
    {
        writer->appendFrames(outputbuf.getView());
    }
}

inline void test_offline_clap() {}

inline void test_clap_gui_choc()
{
    // ClapPluginFormatProcessor::mainthread_id() = std::this_thread::get_id();
    // auto plug = std::make_unique<ClapPluginFormatProcessor>(
    //     R"(C:\Program Files\Common Files\CLAP\Conduit.clap)", 0);
    auto plug = std::make_unique<ClapPluginFormatProcessor>(
        R"(C:\Program Files\Common Files\CLAP\airwin-to-clap.clap)", 1);
    clap::helpers::EventList flushOutList;
    clap::helpers::EventList flushInList;

    choc::ui::setWindowsDPIAwareness(); // For Windows, we need to tell the OS we're high-DPI-aware
    plug->guiCreate("win32", false);
    uint32_t pw = 0;
    uint32_t ph = 0;
    plug->guiGetSize(&pw, &ph);
    choc::ui::DesktopWindow window({100, 100, (int)pw, (int)ph});

    clap_plugin_descriptor desc;
    plug->getDescriptor(&desc);
    window.setWindowTitle(desc.name);
    window.setResizable(true);
    window.setMinimumSize(200, 100);
    window.setMaximumSize(1920, 1080);
    window.windowClosed = [&plug] {
        plug->guiDestroy();
        choc::messageloop::stop();
    };
#ifdef ENABLE_WEBVIEW_JUNK
    choc::ui::WebView webview;
    webview.navigate(R"(C:\develop\AudioPluginHost_mk2\htmltest.html)");
    window.setContent(webview.getViewHandle());

    webview.bind("onSliderMoved", [](const choc::value::ValueView &args) -> choc::value::Value {
        // note that things could get messed up here because the choc functions can throw
        // exceptions, so we should maybe have a try catch block here...but we should
        // just know this will work, really.
        auto parid = args[0]["id"].get<int>();
        auto value = args[0]["value"].get<double>();
        std::cout << "par " << parid << " changed to " << value << std::endl;
        // choc::messageloop::postMessage(
        //    [bpm] { std::cout << bpm << std::endl; });
        return choc::value::Value{};
    });
#endif
    choc::messageloop::Timer flushTimer{
        1000, [&plug, &flushOutList, &flushInList]() {
            plug->paramsFlush(flushInList.clapInputEvents(), flushOutList.clapOutputEvents());
            for (size_t i = 0; i < flushOutList.size(); ++i)
            {
                auto ev = flushOutList.get(i);
                if (ev->type == CLAP_EVENT_PARAM_VALUE)
                {
                    auto pev = (clap_event_param_value *)ev;
                    std::cout << "param " << pev->param_id << " changed to " << pev->value
                              << std::endl;
                }
            }
            flushOutList.clear();
            return true;
        }};

    clap_window clapwin;
    clapwin.api = "win32";
    if (plug->m_webview_ed)
        clapwin.ptr = &window;
    else
        clapwin.win32 = window.getWindowHandle();
    plug->guiSetParent(&clapwin);
    plug->guiShow();
    window.toFront();
    choc::messageloop::run();
}

inline void test_seq_event_chase()
{
    ClapEventSequence seq;
    for (int i = 0; i < 8; ++i)
    {
        seq.addNoteOn(i * 0.1, 0, 0, 60 + i, 1.0, -1);
        seq.addNoteOff(5.0 + i * 0.1, 0, 0, 60 + i, 1.0, -1);
    }
    seq.addNoteOn(0.0, 0, 0, 48, 1.0, -1);
    seq.addNoteOff(10.0, 0, 0, 48, 1.0, -1);
    seq.addNoteOn(6.0, 0, 0, 41, 1.0, -1);
    seq.addNoteOff(7.0, 0, 0, 41, 1.0, -1);
    seq.sortEvents();

    double seekpos = 6.0;
    ClapEventSequence::Iterator it(seq);
    it.setTime(0.0);
    auto evtsspan = it.readNextEvents(seekpos);
    choc::span<ClapEventSequence::Event> allspan{seq.m_evlist};
    for (auto i = evtsspan.begin(); i != evtsspan.end(); ++i)
    {
        if (i->event.header.type == CLAP_EVENT_NOTE_ON)
        {
            auto noteonev = (const clap_event_note *)&i->event;
            for (auto j = i + 1; j != allspan.end(); ++j)
            {
                if (j->event.header.type == CLAP_EVENT_NOTE_OFF ||
                    j->event.header.type == CLAP_EVENT_NOTE_CHOKE)
                {
                    auto noteoffev = (const clap_event_note *)&j->event;
                    if (j->timestamp >= seekpos && (noteoffev->port_index == noteonev->port_index &&
                                                    noteoffev->channel == noteonev->channel &&
                                                    noteoffev->key == noteonev->key &&
                                                    noteoffev->note_id == noteonev->note_id))
                    {
                        std::cout << "note off for " << noteonev->key
                                  << " is past seek position at " << j->timestamp << "\n";
                    }
                }
            }
        }
    }
}

inline void test_mod_matrix()
{
    FixedMatrix<Config> m;
    FixedMatrix<Config>::RoutingTable rt;
    std::array<Config::SourceIdentifier, 8> sources;
    sources[0] = {Config::SourceIdentifier::SI::LFO1};
    sources[1] = {Config::SourceIdentifier::SI::LFO2};
    sources[2] = {Config::SourceIdentifier::SI::LFO3};
    sources[3] = {Config::SourceIdentifier::SI::LFO4};
    sources[4] = {Config::SourceIdentifier::SI::BKENV1};
    sources[5] = {Config::SourceIdentifier::SI::BKENV2};
    sources[6] = {Config::SourceIdentifier::SI::BKENV3};
    sources[7] = {Config::SourceIdentifier::SI::BKENV4};

    std::array<Config::TargetIdentifier, 8> targets;
    for (size_t i = 0; i < targets.size(); ++i)
    {
        targets[i] = Config::TargetIdentifier{(int)i};
    }
    targets[5] = Config::TargetIdentifier{5, 0, 0};
    targets[6] = Config::TargetIdentifier{6, 0, 1};

    std::array<float, 8> sourceValues;
    std::fill(sourceValues.begin(), sourceValues.end(), 0.0f);
    for (size_t i = 0; i < sourceValues.size(); ++i)
    {
        m.bindSourceValue(sources[i], sourceValues[i]);
    }

    std::array<float, 8> targetValues;
    std::fill(targetValues.begin(), targetValues.end(), 0.0f);
    for (size_t i = 0; i < targetValues.size(); ++i)
    {
        m.bindTargetBaseValue(targets[i], targetValues[i]);
    }

    // rt.updateRoutingAt(0, sources[0], targets[0], 0.1);
    // rt.updateRoutingAt(1, sources[1], target0, 0.1);
    rt.updateRoutingAt(0, sources[0], sources[5], {}, targets[0], 0.8);
    rt.updateRoutingAt(1, sources[1], sources[4], {}, targets[0], 0.2);
    rt.updateRoutingAt(2, sources[0], targets[1], 1.0);
    rt.updateRoutingAt(3, sources[1], targets[2], 1.0);
    rt.updateRoutingAt(4, sources[2], targets[3], 0.6);
    rt.updateRoutingAt(5, sources[1], targets[3], 0.3);
    // rt.updateRoutingAt(5, source3, target3, 0.3);
    // rt.updateRoutingAt(6, source1, target3, 0.4);
    // rt.updateRoutingAt(7, source4, target4, 2.0);
    // rt.updateRoutingAt(8, source2, target4, 0.5);

    // rt.updateRoutingAt(5, source4, target5, 1.0);

    m.prepare(rt);

    constexpr size_t blocklen = 64;
    constexpr double sr = 44100;
    std::array<SimpleLFO<blocklen>, 4> lfos{sr, sr, sr, sr};

    std::array<xenakios::Envelope<blocklen>, 4> envs;
    envs[0].addPoint({0.0, 0.0});
    envs[0].addPoint({0.25, 1.0});
    envs[0].addPoint({0.5, 0.0});
    envs[0].addPoint({1.0, 1.0});
    envs[0].addPoint({1.5, 0.0});
    envs[0].addPoint({2.0, 0.0});
    envs[0].sortPoints();

    envs[1].addPoint({0.0, 1.0});
    envs[1].addPoint({1.0, -1.0});
    envs[1].addPoint({2.0, 1.0});
    envs[1].sortPoints();

    int outcounter = 0;
    int outlen = sr * 2.0;

    choc::audio::AudioFileProperties outfileprops;
    outfileprops.formatName = "WAV";
    outfileprops.bitDepth = choc::audio::BitDepth::float32;
    outfileprops.numChannels = 4;
    outfileprops.sampleRate = sr;
    choc::audio::WAVAudioFileFormat<true> wavformat;
    auto writer = wavformat.createWriter(
        R"(C:\MusicAudio\sourcesamples\test_signals\lanczos\sst_modmatrix01.wav)", outfileprops);

    choc::buffer::ChannelArrayBuffer<float> outputbuf{outfileprops.numChannels,
                                                      (unsigned int)outlen + 64};
    outputbuf.clear();
    auto chansdata = outputbuf.getView().data.channels;
    while (outcounter < outlen)
    {
        double secs = outcounter / sr;
        envs[0].processBlock(secs, sr, 2);
        envs[1].processBlock(secs, sr, 2);
        lfos[0].m_lfo.process_block(1.0, 0.0, 0, false);
        lfos[1].m_lfo.process_block(4.75, 0.0, 0, false);
        lfos[2].m_lfo.process_block(2.5, 0.0, 4, false);
        lfos[3].m_lfo.process_block(3.1, 0.0, 4, false);
        sourceValues[0] = lfos[0].m_lfo.outputBlock[0];
        sourceValues[1] = lfos[1].m_lfo.outputBlock[0];
        sourceValues[2] = lfos[2].m_lfo.outputBlock[0];
        sourceValues[3] = lfos[3].m_lfo.outputBlock[0];
        sourceValues[4] = envs[0].outputBlock[0];
        sourceValues[5] = envs[1].outputBlock[0];
        m.process();
        for (int i = 0; i < blocklen; ++i)
        {
            chansdata[0][outcounter + i] = m.getTargetValue(targets[0]);
            chansdata[1][outcounter + i] = m.getTargetValue(targets[1]);
            chansdata[2][outcounter + i] = m.getTargetValue(targets[2]);
            chansdata[3][outcounter + i] = m.getTargetValue(targets[3]);
            // chansdata[4][outcounter + i] = m.getTargetValue(target4);
        }
        outcounter += blocklen;
    }
    writer->appendFrames(outputbuf.getView());
}

inline void test_file_player_clap()
{
    /*
    ClapProcessingEngine engine{R"(C:\Program Files\Common Files\CLAP\FilePlayerPlugin.clap)", 0};
    ClapEventSequence seq;
    seq.addParameterEvent(false, 0.0, -1, -1, -1, -1, 2022, 1.0);
    // seq.addParameterEvent(false,0.0,-1,-1,-1,-1,8888,0.1);
    // seq.addParameterEvent(false,0.0,-1,-1,-1,-1,1001,1.0);
    engine.setSequence(0, seq);
    engine.processToFile(R"(C:\develop\AudioPluginHost_mk2\audio\noise_plethora_out_03.wav)", 10.0,
                         44100.0);
    */
}

inline void test_thread_rand()
{
    auto f = []() { std::cout << rand() << " \n"; };
    using namespace std::chrono_literals;

    std::thread th0(f);
    std::this_thread::sleep_for(1000ms);
    std::thread th1(f);
    std::this_thread::sleep_for(1000ms);
    std::thread th2(f);
    std::this_thread::sleep_for(1000ms);
    std::thread th3(f);
    std::this_thread::sleep_for(1000ms);
    th0.join();
    th1.join();
    th2.join();
    th3.join();
}

std::mt19937 rng;

int MyRtAudioCallback(void *outputBuffer, void *inputBuffer, unsigned int nFrames,
                      double streamTime, RtAudioStreamStatus status, void *userData)
{
    float *fbuf = (float *)outputBuffer;
    std::uniform_real_distribution<float> dist(-0.05f, 0.05f);
    for (int i = 0; i < nFrames; ++i)
    {
        float osample = dist(rng);
        for (int j = 0; j < 2; ++j)
        {
            fbuf[i * 2 + j] = osample;
        }
    }
    return 0;
}

inline void test_rtaudio()
{
    RtAudio ra;
    auto defdev = ra.getDefaultOutputDevice();
    std::cout << ra.getDeviceInfo(defdev).name << "\n";
    RtAudio::StreamParameters outpars;
    outpars.deviceId = defdev;
    outpars.firstChannel = 0;
    outpars.nChannels = 2;
    unsigned int bframes = 512;
    auto err = ra.openStream(&outpars, nullptr, RTAUDIO_FLOAT32, 44100, &bframes, MyRtAudioCallback,
                             nullptr, nullptr);
    if (err != RTAUDIO_NO_ERROR)
    {
        std::cout << "error opening stream\n";
        return;
    }
    std::cout << "stream opened with size " << bframes << "\n";
    ra.startStream();
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(1000ms);
    return;
    auto devices = ra.getDeviceIds();
    for (auto &d : devices)
    {
        std::cout << ra.getDeviceInfo(d).name << "\n";
    }
}

template <typename T> inline void writeBinaryToStream(std::ostream &os, T v)
{
    os.write((const char *)&v, sizeof(T));
}

inline void test_clap_riff()
{
    std::ofstream os("koe.clappreset", std::ios::binary);
    os << "RIFF";
    uint32_t sz = 1000;
    writeBinaryToStream(os, sz);
    os << "CLAP";
    os << "hder";
    auto proc = std::make_unique<ClapPluginFormatProcessor>(
        R"(C:\Program Files\Common Files\CLAP\Conduit.clap)", 0);
    clap_plugin_descriptor desc;
    if (proc->getDescriptor(&desc))
    {
        sz = 4 + strlen(desc.id) + 1;
        writeBinaryToStream(os, sz);
        os << desc.id;
        writeBinaryToStream(os, (unsigned char)0);
        writeBinaryToStream(os, (unsigned char)255);
    }
}

inline void test_env2()
{
    xenakios::Envelope env;
    env.addPoint(xenakios::EnvelopePoint(0.0, 1.0));
    env.addPoint(xenakios::EnvelopePoint(1.0, 0.0));
    env.addPoint(xenakios::EnvelopePoint(2.0, 0.5));
    env.addPoint(xenakios::EnvelopePoint(3.5, 0.99));
    env.addPoint(xenakios::EnvelopePoint(4.0, 1.00));
    env.removeEnvelopePoints(
        [](xenakios::EnvelopePoint pt) { return pt.getX() >= 1.0 && pt.getX() < 3.5; });
    for (size_t i = 0; i < env.getNumPoints(); ++i)
        std::cout << env.getPointSafe(i).getX() << " " << env.getPointSafe(i).getY() << "\n";
}

inline void test_no_juce_agraph()
{
    ClapEventSequence seq;
    seq.addNote(0.0, 2.0, 0, 0, 60, 0, 1.0, 0.0);
    seq.addNote(1.0, 2.0, 0, 0, 64, 0, 1.0, 0.0);
    seq.addNote(2.0, 2.0, 0, 0, 67, 0, 1.0, 0.0);
    seq.addNote(3.0, 2.0, 0, 0, 72, 0, 1.0, 0.0);
    auto g = std::make_unique<XAPGraph>();
    g->addProcessorAsNode(
        std::make_unique<ClapPluginFormatProcessor>(
            R"(C:\Program Files\Common Files\CLAP\Surge Synth Team\Surge XT.clap)", 0),
        "Surge XT", 0);
    g->outputNodeId = "Surge XT";
    g->activate(44100.0, 512, 512);
}

class BlueNoise
{
  public:
    BlueNoise() { m_previous = m_dist(m_rng); }
    float operator()()
    {
        float maxdist = 0.0f;
        float z0 = 0.0f;
        for (int i = 0; i < m_depth; ++i)
        {
            float z1 = m_dist(m_rng);
            float dist = std::abs(z1 - m_previous);
            if (dist > maxdist)
            {
                maxdist = dist;
                z0 = z1;
            }
        }
        m_previous = z0;
        return m_previous;
    }
    void setDepth(int d) { m_depth = std::clamp(d, 1, 32); }

  private:
    std::minstd_rand m_rng;
    std::uniform_real_distribution<float> m_dist{0.0f, 1.0f};
    float m_previous = 0.0f;
    int m_depth = 4;
};

class GoldenRatioNoise
{
  public:
    GoldenRatioNoise() { m_x0 = m_dist(m_rng); }
    double operator()()
    {
        double x1 = std::fmod(m_x0 + 0.618033988749, 1.0);
        m_x0 = x1;
        return m_x0;
    }

  private:
    double m_x0 = 0.0;
    std::minstd_rand m_rng;
    std::uniform_real_distribution<float> m_dist{0.0f, 1.0f};
};

using namespace sst::basic_blocks;

class CorrelatedNoise
{
  public:
    CorrelatedNoise()
    {
        randstate[0] = m_dist(m_rng);
        randstate[1] = m_dist(m_rng);
    }
    float operator()()
    {
        return dsp::correlated_noise_o2mk2_supplied_value(randstate[0], randstate[1], m_corr,
                                                          m_dist(m_rng));
    }
    void setCorrelation(float c) { m_corr = std::clamp(c, -1.0f, 1.0f); }

  private:
    float randstate[2] = {0.0f, 0.0f};
    float m_corr = 0.0;
    std::minstd_rand m_rng;
    std::uniform_real_distribution<float> m_dist{-1.0f, 1.0f};
};

inline void test_bluenoise()
{
    choc::audio::AudioFileProperties outfileprops;
    double outsr = 44100;
    outfileprops.formatName = "WAV";
    outfileprops.bitDepth = choc::audio::BitDepth::float32;
    outfileprops.numChannels = 1;
    outfileprops.sampleRate = outsr;
    choc::audio::WAVAudioFileFormat<true> wavformat;
    auto writer = wavformat.createWriter(
        R"(C:\MusicAudio\sourcesamples\pedalboard\bluenoise01.wav)", outfileprops);
    if (writer)
    {

        int outlen = 10 * outsr;
        choc::buffer::ChannelArrayBuffer<float> buf(1, outlen);
        BlueNoise bn;
        GoldenRatioNoise grn;
        xenakios::DejaVuRandom dvrand(6);
        CorrelatedNoise corrnoise;
        int depth = 1;
        ClapEventSequence seq;
        double t = 0.0;
        std::minstd_rand rng;
        std::uniform_real_distribution<float> dist{0.0f, 1.0f};
        while (t < 10.0)
        {
            // seq.addParameterEvent(false, t, 0, 0, 0, -1, 1, dist(rng));
            t += 0.5;
        }
        seq.addParameterEvent(false, 0.0, 0, 0, 0, -1, 1, 1.0);
        seq.addParameterEvent(false, 1.0, 0, 0, 0, -1, 1, 1.0);
        seq.addParameterEvent(false, 2.00, 0, 0, 0, -1, 1, 0.0);
        seq.addParameterEvent(false, 3.00, 0, 0, 0, -1, 1, 0.0);
        seq.addParameterEvent(false, 4.00, 0, 0, 0, -1, 1, 0.5);
        seq.addParameterEvent(false, 5.00, 0, 0, 0, -1, 1, 0.0);
        seq.addParameterEvent(false, 6.00, 0, 0, 0, -1, 1, 0.25);
        seq.addParameterEvent(false, 7.00, 0, 0, 0, -1, 1, 0.0);
        sst::basic_blocks::dsp::SlewLimiter slew;
        slew.setParams(250.0, 1.0, outsr);

        ClapEventSequence::Iterator seqiter(seq);
        xenakios::Envelope<64> env;
        env.addPoint({0.0, -1.0});
        env.addPoint({5.0, 1.0});
        env.addPoint({10.0, -1.0});

        env.sortPoints();
        dvrand.setLoopLength(256);
        dvrand.setDejaVu(0.4);
        float corr = 0.0;
        float volume = -96.0;
        corrnoise.setCorrelation(0.0);

        for (int i = 0; i < outlen; ++i)
        {
            if (i % 64 == 0)
            {
                // env.processBlock(i / outsr, outsr, 2);
                // bn.setDepth(env.outputBlock[0]);
                // dvrand.setDejaVu(env.outputBlock[0]);
                //  corrnoise.setCorrelation(env.outputBlock[0]);
                auto evts = seqiter.readNextEvents(64 / outsr);
                for (auto &ev : evts)
                {
                    if (ev.event.header.type == CLAP_EVENT_PARAM_VALUE)
                    {
                        auto pev = (clap_event_param_value *)&ev.event;
                        if (pev->param_id == 0)
                            corr = pev->value;
                        if (pev->param_id == 1)
                        {
                            volume = pev->value;
                        }
                    }
                }
            }
            float slew_volume = slew.step(volume);
            // slew_volume = xenakios::decibelsToGain(slew_volume);
            // corrnoise.setCorrelation(slewcorr);
            // buf.getSample(0, i) = -0.5 + 1.0 * dvrand.nextFloat();
            buf.getSample(0, i) = corrnoise() * slew_volume;
        }
        writer->appendFrames(buf.getView());
    }
}

using ms = std::chrono::duration<double, std::milli>;
using namespace std::chrono_literals;

inline void generateSequenceFromScore(const choc::value::ValueView &score,
                                      ClapEventSequence &sequence,
                                      const choc::value::ValueView &renderInfo)
{
    using clock = std::chrono::system_clock;
    const auto start_time = clock::now();
    sequence.m_evlist.clear();
    int noteid = 0;
    double curvestart = 0.0;
    double curve_end = 0.0;
    double scoreWidth = renderInfo["scoreWidth"].get<double>();
    double scoreHeigth = renderInfo["scoreHeigth"].get<double>();
    double minPitch = 36.0;
    double maxPitch = 84.0;
    for (const auto &curve : score)
    {
        if (curve.size() < 2)
            continue;
        double startNote = 0;
        double timepos =
            xenakios::mapvalue<double>(curve[0]["x"].get<double>(), 0.0, scoreWidth, 0.0, 30.0);
        curvestart = timepos;
        double key = xenakios::mapvalue<double>(curve[0]["y"].get<double>(), scoreHeigth, 0.0,
                                                minPitch, maxPitch);
        startNote = key;
        sequence.addNoteOn(timepos, 0, 0, key, 1.0, noteid);
        double endpos = curve[curve.size() - 1]["x"].get<double>();
        endpos = xenakios::mapvalue<double>(endpos, 0.0, 1800.0, 0.0, 30.0);
        sequence.addNoteOff(endpos, 0, 0, key, 1.0, noteid);
        curve_end = endpos;

        double tpos = curvestart;
        int i = 0;
        while (tpos < curve_end)
        {
            double x0 =
                xenakios::mapvalue<double>(curve[i]["x"].get<double>(), 0.0, scoreWidth, 0.0, 30.0);
            double y0 = xenakios::mapvalue<double>(curve[(int)i]["y"].get<double>(), scoreHeigth,
                                                   0.0, minPitch, maxPitch);
            int nextIndex = i + 1;
            if (nextIndex >= curve.size())
                --nextIndex;
            double x1 = xenakios::mapvalue<double>(curve[nextIndex]["x"].get<double>(), 0.0,
                                                   scoreWidth, 0.0, 30.0);
            double y1 = xenakios::mapvalue<double>(curve[(int)nextIndex]["y"].get<double>(),
                                                   scoreHeigth, 0.0, minPitch, maxPitch);

            double outvalue = y0;
            double xdiff = x1 - x0;
            if (xdiff < 0.00001)
                outvalue = y1;
            else
            {
                double ydiff = y1 - y0;
                outvalue = y0 + ydiff * ((1.0 / xdiff * (tpos - x0)));
            }
            double pitchDiff = outvalue - startNote;
            sequence.addNoteExpression(tpos, 0, 0, startNote, noteid, CLAP_NOTE_EXPRESSION_TUNING,
                                       pitchDiff);
            tpos += 0.05;
            if (tpos >= x1)
                ++i;
        }

        ++noteid;
    }
    const ms duration = clock::now() - start_time;
    std::cout << "finished generating sequence from score in " << duration.count() / 1000.0
              << " seconds, " << sequence.getNumEvents() << " events" << std::endl;
}

inline void testWebviewCurveEditor()
{
    choc::ui::setWindowsDPIAwareness(); // For Windows, we need to tell the OS we're high-DPI-aware
    choc::ui::DesktopWindow window({50, 50, (int)1800, (int)600});
    window.setResizable(true);
    window.setMinimumSize(200, 100);
    window.setMaximumSize(1920, 1080);
    window.windowClosed = [] { choc::messageloop::stop(); };
    choc::ui::WebView::Options opts;
    opts.enableDebugMode = true;
    choc::ui::WebView webview{opts};
    webview.navigate(R"(C:\develop\AudioPluginHost_mk2\html\canvastest.html)");
    ClapEventSequence sequence;
    webview.bind("onScoreChanged",
                 [&sequence](const choc::value::ValueView &args) -> choc::value::Value {
                     generateSequenceFromScore(args[0], sequence, args[1]);
                     return choc::value::Value{};
                 });
    window.setContent(webview.getViewHandle());
    choc::messageloop::run();
    if (sequence.getNumEvents() == 0)
        return;
    sequence.sortEvents();

    ClapProcessingEngine eng;
    eng.addProcessorToChain(R"(C:\Program Files\Common Files\CLAP\Surge Synth Team\Surge XT.clap)",
                            0);
    eng.setSequence(0, sequence);
    eng.processToFile(R"(C:\develop\AudioPluginHost_mk2\audio\webview_xupic_offline02.wav)", 30.0,
                      44100.0);
}

inline void test_chained_offline()
{
    ClapProcessingEngine eng;
    eng.addProcessorToChain(R"(C:\Program Files\Common Files\CLAP\Surge Synth Team\Surge XT.clap)",
                            0);
    eng.addProcessorToChain(R"(C:\Program Files\Common Files\CLAP\Airwindows Consolidated.clap)",
                            0);
    auto pars = eng.getParameters(1);
    for (auto &p : pars)
        std::cout << p.first << " " << p.second << "\n";
    eng.m_chain[0]->m_seq.addNote(0.0, 2.0, 0, 0, 60, 0, 1.0, 0.0);
    eng.m_chain[0]->m_seq.addNote(0.1, 2.0, 0, 0, 65, 0, 1.0, 0.0);
    eng.m_chain[0]->m_seq.addNote(2.0, 2.0, 0, 0, 67, 0, 1.0, 0.0);
    eng.m_chain[0]->m_seq.addNote(2.1, 2.0, 0, 0, 72, 0, 1.0, 0.0);
    eng.m_chain[0]->m_seq.addNote(4.0, 2.0, 0, 0, 74, 0, 1.0, 0.0);
    eng.m_chain[0]->m_seq.addNote(4.1, 5.0, 0, 0, 79, 0, 1.0, 0.0);
    xenakios::Envelope mixenv;
    mixenv.addPoint({0.0, 0.0});
    mixenv.addPoint({5.0, 1.0});
    mixenv.addPoint({10.0, 0.0});
    mixenv.sortPoints();
    double t = 0.0;
    while (t < 16.0)
    {
        double parval = mixenv.getValueAtPosition(t);
        eng.m_chain[1]->m_seq.addParameterEvent(false, t, 0, 0, 0, 0, 2944917344, parval);
        t += 0.1;
    }

    eng.processToFile(R"(C:\develop\AudioPluginHost_mk2\audio\chain_offline_02.wav)", 16.0,
                      44100.0);
}

int main()
{
    test_chained_offline();
    // testWebviewCurveEditor();
    // test_bluenoise();
    // test_no_juce_agraph();
    // test_env2();
    // test_clap_riff();
    // test_rtaudio();
    // test_thread_rand();
    // test_file_player_clap();
    // test_plethora_synth();
    // test_mod_matrix_pyt();
    // test_mod_matrix();
    // test_seq_event_chase();
    // test_clap_gui_choc();
    // test_offline_clap();
    // test_fb_osc();
    // test_lanczos();
    // test_envelope();
    // test_np_code();
    // test_pipe();
    // test_moody();
    // test_auto();
    //  test_alt_event_list();
    //  test_span();
    //  test_polym();
    return 0;
}
