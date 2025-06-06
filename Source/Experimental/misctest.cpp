#include <cstdint>
#include <exception>
// #include <handleapi.h>
// #include <minwindef.h>

#include <filesystem>
#include <ostream>
#include <combaseapi.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>
#include <span>
#include <array>
#include "audio/choc_AudioFileFormat.h"
#include "audio/choc_SampleBuffers.h"
#include "platform/choc_Execute.h"
#include "clap/events.h"
#include <print>
#include "clap/clap.h"
#include <algorithm>
#include <cassert>
#include <random>
#include "clap/ext/params.h"
#include "clap/plugin.h"
#include "clap/stream.h"
#include "containers/choc_Value.h"
#include "libMTSClient.h"
#include "memory/choc_xxHash.h"
#include "testsinesynth.h"
#include "../Common/xap_utils.h"
#include "concurrentqueue.h"
#include <chrono>
#include "../Common/xapdsp.h"
#include "audio/choc_AudioFileFormat_WAV.h"
#include "containers/choc_NonAllocatingStableSort.h"
#include "../Common/xap_breakpoint_envelope.h"
#include "../Common/xen_modulators.h"
#include "sst/basic-blocks/modulators/SimpleLFO.h"
#include "sst/basic-blocks/dsp/LanczosResampler.h"
#include "sst/basic-blocks/dsp/FollowSlewAndSmooth.h"
#include "sst/basic-blocks/mod-matrix/ModMatrix.h"
#include "sst/basic-blocks/dsp/PanLaws.h"
#include "../Host/claphost.h"
#include "gui/choc_DesktopWindow.h"
#include "gui/choc_MessageLoop.h"
#include "gui/choc_WebView.h"
#include "text/choc_Files.h"
#include "RtAudio.h"
#include "../Xaps/clap_xaudioprocessor.h"
#include "text/choc_JSON.h"
#include "text/choc_StringUtilities.h"
#include "xaudiograph.h"
#include "../Common/dejavurandom.h"
#include "../Common/bluenoise.h"
#include <variant>
#include "mts_esp_wrapper.h"

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

    m.prepare(rt, 44100, 512);

    constexpr size_t blocklen = 64;
    constexpr double sr = 44100;
    std::array<SimpleLFO<blocklen>, 4> lfos{sr, sr, sr, sr};

    std::array<xenakios::Envelope, 4> envs;
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
        envs[0].processBlock(secs, sr, 2, 1);
        envs[1].processBlock(secs, sr, 2, 1);
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

std::mt19937 rng;

int MyRtAudioCallback(void *outputBuffer, void *inputBuffer, unsigned int nFrames,
                      double streamTime, RtAudioStreamStatus status, void *userData)
{
    float *fbuf = (float *)outputBuffer;
    std::uniform_real_distribution<float> dist(-0.1f, 0.1f);
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
    if (ra.startStream() != RTAUDIO_NO_ERROR)
    {
        std::cout << "error startng stream\n";
    }
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
    xenakios::BlueNoise bn{777};
    return;
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
        xenakios::BlueNoise bn;

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
        xenakios::Envelope env;
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
                      44100.0, 2, nullptr);
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
    mixenv.addPoint({16.0, 0.0});
    mixenv.sortPoints();
    double t = 0.0;
    while (t < 16.0)
    {
        double parval = mixenv.getValueAtPosition(t);
        eng.m_chain[1]->m_seq.addParameterEvent(false, t, 0, 0, 0, 0, 2944917344, parval);
        t += 0.1;
    }

    eng.processToFile(R"(C:\develop\AudioPluginHost_mk2\audio\chain_offline_02.wav)", 16.0, 44100.0,
                      2, nullptr);
}

inline void test_tempomap()
{
    double sr = 44100.0;
    choc::audio::WAVAudioFileFormat<true> format;
    choc::audio::AudioFileProperties props;
    props.bitDepth = choc::audio::BitDepth::float32;
    props.formatName = "WAV";
    props.numChannels = 1;
    props.sampleRate = sr;
    auto writer = format.createWriter("tempomap.wav", props);
    choc::buffer::ChannelArrayBuffer<float> buffer{1, (unsigned int)sr * 16};
    buffer.clear();
    TempoMap tm;
    // std::cout << tm.beatPosToSeconds(1.0) << "\n";
    tm.setStaticBPM(60.0);
    // std::cout << tm.beatPosToSeconds(1.0) << "\n";

    tm.m_bpm_envelope.addPoint({0.0, 120.0, xenakios::EnvelopePoint::Shape::Abrupt});
    tm.m_bpm_envelope.addPoint({4.0, 60.0, xenakios::EnvelopePoint::Shape::Abrupt});
    tm.m_bpm_envelope.addPoint({8.0, 480.0, xenakios::EnvelopePoint::Shape::Linear});
    tm.m_bpm_envelope.addPoint({40.0, 60.0, xenakios::EnvelopePoint::Shape::Abrupt});
    tm.updateMapping();
    double b = 0.0;
    while (b < 40.0)
    {
        double secpos = tm.beatPosToSeconds(b);
        int bufindex = secpos * sr;
        if (bufindex >= 0 && bufindex < buffer.getNumFrames())
        {
            buffer.getSample(0, bufindex) = 0.9f;
        }
        // std::cout << b << "\t" << tm.m_bpm_envelope.getValueAtPosition(b) << "\t"
        //          << secpos << "\n";
        b += 0.5;
    }
    writer->appendFrames(buffer.getView());
}

// helper type for the visitor #4
template <class... Ts> struct overloaded : Ts...
{
    using Ts::operator()...;
};

inline void test_clapvariant()
{
    std::vector<std::variant<clap_event_note, clap_event_param_value, clap_event_note_expression>>
        events;
    events.push_back(xenakios::make_event_note(0, CLAP_EVENT_NOTE_ON, 0, 0, 60, -1, 1.0));
    events.push_back(xenakios::make_event_param_value(0, 666, 0.5, nullptr, -1, -1, -1, -1));
    events.push_back(xenakios::make_event_note(100, CLAP_EVENT_NOTE_OFF, 0, 0, 60, -1, 1.0));
    events.push_back(xenakios::make_event_note_expression(200, 0, 0, 0, 60, -1, 0.333));
    for (auto &e : events)
    {
        std::visit(overloaded{[](const clap_event_note &nev) {
                                  std::cout << "note event with key " << nev.key << "\n";
                              },
                              [](const clap_event_param_value &pev) {
                                  std::cout << "param value event with param id " << pev.param_id
                                            << "\n";
                              },
                              [](const auto &ukn) { std::cout << "unhandled event\n"; }},
                   e);
    }
}

inline void test_binary_clap_state()
{
    auto clap = std::make_unique<ClapPluginFormatProcessor>(
        R"(C:\Program Files\Common Files\CLAP\Surge Synth Team\Surge XT.clap)", 0);
    std::string filename = R"(C:\develop\AudioPluginHost_mk2\clapstatetests\sfxtestfile.bin)";
    if (true)
    {
        std::ofstream ofs(filename, std::ios::binary);
        if (ofs.is_open())
        {
            clap_plugin_descriptor desc;
            clap->getDescriptor(&desc);
            ofs.write("CLAPSTATE   ", 12);
            int version = 0;
            ofs.write((const char *)&version, sizeof(int));
            int len = strlen(desc.id);
            ofs.write((const char *)&len, sizeof(int));
            ofs.write(desc.id, len);
            clap_ostream costream;
            costream.ctx = &ofs;
            // returns the number of bytes written; -1 on write error
            // int64_t(CLAP_ABI *write)(const struct clap_ostream *stream, const void *buffer,
            // uint64_t size);
            costream.write = [](const struct clap_ostream *stream, const void *buffer,
                                uint64_t size) {
                std::cout << "plugin asked to write " << size << " bytes\n";
                auto &ctxofs = *((std::ofstream *)stream->ctx);
                ctxofs.write((const char *)buffer, size);
                return int64_t(size);
            };
            if (clap->stateSave(&costream))
            {
                std::cout << ofs.tellp() << " bytes written to file\n";
            }
        }
    }
    {
        std::ifstream ins(filename, std::ios::binary);
        if (ins.is_open())
        {
            char magic[13];
            memset(magic, 0, 13);
            ins.read(magic, 12);
            if (strcmp(magic, "CLAPSTATE   "))
            {
                std::cout << "invalid magic in file " << magic << "\n";
                return;
            }
            int i = -1;
            ins.read((char *)&i, sizeof(int));
            std::cout << "clap state version " << i << "\n";
            ins.read((char *)&i, sizeof(int));
            std::cout << "clap id len : " << i << "\n";
            std::string id;
            id.resize(i);
            ins.read(id.data(), i);
            std::cout << "clap id : " << id << "\n";
            clap_istream clapistream;
            clapistream.ctx = &ins;
            // returns the number of bytes read; 0 indicates end of file and -1 a read error
            // int64_t(CLAP_ABI *read)(const struct clap_istream *stream, void *buffer, uint64_t
            // size);
            clapistream.read = [](const struct clap_istream *stream, void *buffer, uint64_t size) {
                auto &ctxifstream = *(std::ifstream *)stream->ctx;
                ctxifstream.read((char *)buffer, size);
                std::cout << "stream gcount " << ctxifstream.gcount() << "\n";
                return int64_t(ctxifstream.gcount());
            };
            clap->stateLoad(&clapistream);
        }
    }
}

inline void test_sc()
{
    auto eng = std::make_unique<ClapProcessingEngine>();
    eng->addProcessorToChain(
        R"(C:\Program Files\Common Files\CLAP\Surge Synth Team\Shortcircuit XT.clap)", 0);
    eng->loadStateFromBinaryFile(0,
                                 R"(C:\develop\AudioPluginHost_mk2\clapstatetests\SCtestfile.bin)");
    // Sleep(1000);
    //  eng->openPluginGUIBlocking(0, false);
    //  eng->saveStateToBinaryFile(0,
    //                            R"(C:\develop\AudioPluginHost_mk2\clapstatetests\SCtestfile.bin)");
    auto &seq = eng->getSequence(0);
    seq.addNote(0.0, 4.0, 0, 0, 60, -1, 1.0, 0.0);
    seq.addNote(0.1, 4.0, 0, 0, 67, -1, 1.0, 0.0);
    seq.addNote(0.2, 4.0, 0, 0, 53, -1, 1.0, 0.0);
    seq.addNote(0.3, 4.0, 0, 0, 58, -1, 1.0, 0.0);
    seq.addNote(0.4, 4.0, 0, 0, 51, -1, 1.0, 0.0);
    seq.addNote(2.0, 4.0, 0, 0, 40, -1, 1.0, 0.0);
    eng->processToFile(R"(C:\develop\AudioPluginHost_mk2\clapstatetests\SCtestfile.wav)", 5.0,
                       44100.0, 2, nullptr);
}

class GrainDelay
{
  public:
    GrainDelay() {}
    std::array<float, 2> outputFrame;
    void prepareToPlay(double sampleRate, double maxDelayLenSeconds)
    {
        m_samplerate = sampleRate;
        m_buffer = {2, (unsigned int)(maxDelayLenSeconds * m_samplerate)};
        m_buffer.clear();
        m_writepos = m_buffer.getNumFrames() - 1;
    }
    void process(float inLeft, float inRight)
    {
        assert(m_samplerate > 1.0);
        if (!m_input_frozen)
        {
            m_buffer.getSample(0, m_writepos) = inLeft;
            m_buffer.getSample(1, m_writepos) = inRight;
            ++m_writepos;
            if (m_writepos == m_buffer.getNumFrames())
                m_writepos = 0;
        }

        if (m_grainphase == 0.0) // begin new grain
        {
            int mindelay = 0.1 * m_samplerate;
            int maxdelay = 1.0 * m_samplerate;
            int delaysamples = xenakios::mapvalue<float>(m_pos, 0.0f, 1.0f, mindelay, maxdelay);
            std::uniform_int_distribution<int> delaydist{mindelay, maxdelay};
            float panrange = xenakios::mapvalue(m_pan_width, 0.0f, 1.0f, 0.0f, 0.5f);
            const float pitches[4] = {0.0f, 4.0f, 7.0f, 12.0f};
            for (auto &ph : m_playheads)
            {
                if (!ph.isActive)
                {
                    ph.isActive = true;
                    float shapedsize = std::pow(m_size, 3.0f);
                    float lenseconds = xenakios::mapvalue(shapedsize, 0.0f, 1.0f, 0.01f, 1.0f);
                    ph.lensamples = lenseconds * m_samplerate;
                    ph.playpos = 0;
                    float pitch = m_rng.nextFloatInRange(0.0, 0.0);
                    // pitch = pitches[m_graincounter % 4];
                    pitch = m_center_pitch;
                    ph.rate = std::pow(2.0f, pitch / 12.0f);
                    ph.m_resampler.sri = m_samplerate;
                    ph.m_resampler.sro = m_samplerate / ph.rate;
                    ph.m_resampler.dPhaseO = ph.m_resampler.sri / ph.m_resampler.sro;
                    ph.pan = m_rng.nextFloatInRange(0.5f - panrange, 0.5f + panrange);
                    sst::basic_blocks::dsp::pan_laws::monoEqualPower(ph.pan, ph.panmatrix);
                    ph.outputbufferpos = 0;
                    int actualDelay = delaysamples; // delaydist(m_rng);
                    ph.bufferreadpos = m_writepos - actualDelay;
                    if (ph.bufferreadpos < 0)
                    {
                        ph.bufferreadpos = m_buffer.getNumFrames() - std::abs(ph.bufferreadpos);
                        assert(ph.bufferreadpos >= 0 && ph.bufferreadpos < m_buffer.getNumFrames());
                    }
                    int numplayheads = 0;
                    for (auto &p : m_playheads)
                    {
                        if (p.isActive)
                            ++numplayheads;
                    }
                    maxplayheads_used = std::max(maxplayheads_used, numplayheads);
                    break;
                }
            }
        }
        m_grainphase += 1.0 / m_samplerate * m_grainrate;
        if (m_grainphase >= 1.0)
        {
            m_grainphase = 0.0;
            ++m_graincounter;
        }
        outputFrame[0] = 0.0f;
        outputFrame[1] = 0.0f;
        for (auto &ph : m_playheads)
        {
            if (ph.isActive)
            {
                if (ph.outputbufferpos == 0)
                {

                    ph.m_resampler.dPhaseO = ph.m_resampler.sri / ph.m_resampler.sro;
                    int required = ph.m_resampler.inputsRequiredToGenerateOutputs(64);
                    for (int i = 0; i < required; ++i)
                    {
                        float leftGrainOut = m_buffer.getSample(0, ph.bufferreadpos);
                        float rightGrainOut = m_buffer.getSample(1, ph.bufferreadpos);
                        ph.m_resampler.push(leftGrainOut, rightGrainOut);
                        ++ph.bufferreadpos;
                        if (ph.bufferreadpos == m_buffer.getNumFrames())
                        {
                            ph.bufferreadpos = 0;
                        }
                    }

                    auto produced =
                        ph.m_resampler.populateNext(ph.outputbuffer[0], ph.outputbuffer[1], 64);
                }
                float resampledLeft = ph.outputbuffer[0][ph.outputbufferpos];
                float resampledRight = ph.outputbuffer[1][ph.outputbufferpos];
                ++ph.outputbufferpos;
                if (ph.outputbufferpos == 64)
                    ph.outputbufferpos = 0;
                float gain = 1.0f;
                if (ph.playpos < ph.lensamples / 2)
                    gain =
                        xenakios::mapvalue<float>(ph.playpos, 0, ph.lensamples / 2.0, 0.0f, 1.0f);
                if (ph.playpos >= ph.lensamples / 2)
                    gain = xenakios::mapvalue<float>(ph.playpos, ph.lensamples / 2.0, ph.lensamples,
                                                     1.0f, 0.0f);
                resampledLeft *= gain * 0.5f;
                resampledRight *= gain * 0.5f;
                outputFrame[0] += resampledLeft * ph.panmatrix[0];
                outputFrame[1] += resampledRight * ph.panmatrix[3];
                ++ph.playpos;
                if (ph.playpos == ph.lensamples)
                {
                    ph.isActive = false;
                }
            }
        }
    }
    void setInputFrozen(bool b) { m_input_frozen = b; }
    void setPanWidth(float w) { m_pan_width = w; }
    void setCenterPitch(float p) { m_center_pitch = std::clamp(p, -12.0f, 12.0f); }
    // -1 = 0.5Hz, 0 = 1Hz, 1 = 2Hz, 2 = 4Hz, 3 = 8Hz etc
    void setGrainRateOctaves(float r) { m_grainrate = std::pow(2.0, r); }
    // 0 closest to write head, 1 furthest from write head
    void setPosition(float p) { m_pos = std::clamp(p, 0.0f, 1.0f); }
    void setSize(float sz) { m_size = std::clamp(sz, 0.0f, 1.0f); }
    int maxplayheads_used = 0;

  private:
    alignas(32) choc::buffer::ChannelArrayBuffer<float> m_buffer;
    alignas(32) int m_writepos = 0;
    bool m_input_frozen = false;
    float m_pan_width = 0.0f;
    float m_center_pitch = 0.0f;
    float m_samplerate = 1.0;
    float m_pos = 0.0;
    float m_size = 0.5;
    struct Playhead
    {
        Playhead()
        {
            for (int i = 0; i < 2; ++i)
                for (int j = 0; j < 64; ++j)
                    outputbuffer[i][j] = 0.0f;
            for (int i = 0; i < 4; ++i)
                panmatrix[i] = 0.0f;
        }
        bool isActive = false;
        float rate = 1.0f;
        int bufferreadpos = 0;
        int playpos = 0;
        int lensamples = 0;
        float pan = 0.5f;
        alignas(32) float outputbuffer[2][64];
        int outputbufferpos = 0;
        alignas(32) sst::basic_blocks::dsp::LanczosResampler<64> m_resampler{44100.0f, 44100.0f};
        alignas(32) sst::basic_blocks::dsp::pan_laws::panmatrix_t panmatrix;
    };
    alignas(32) std::array<Playhead, 32> m_playheads;
    double m_grainphase = 0.0;
    double m_grainrate = 1.0;
    xenakios::Xoroshiro128Plus m_rng;
    int m_graincounter = 0;
};

inline void test_grain_delay()
{
    choc::audio::AudioFileFormatList flist;
    flist.addFormat(std::make_unique<choc::audio::WAVAudioFileFormat<false>>());

    // auto reader = flist.createReader(
    //     R"(C:\MusicAudio\sourcesamples\ilkka virran romu pieno\trashpiano01a.wav)");
    auto reader = flist.createReader(R"(C:\MusicAudio\sourcesamples\sheila.wav)");
    // auto reader =
    //    flist.createReader(R"(C:\MusicAudio\sourcesamples\test_signals\440hz_sine_0db.wav)");
    if (reader)
    {
        auto inProps = reader->getProperties();
        unsigned int numInChans = inProps.numChannels;
        choc::buffer::ChannelArrayBuffer<float> sourceBuffer{inProps.numChannels,
                                                             (unsigned int)inProps.numFrames};
        reader->readFrames(0, sourceBuffer.getView());
        choc::audio::AudioFileProperties outprops;
        outprops.bitDepth = choc::audio::BitDepth::float32;
        outprops.formatName = "WAV";
        outprops.numChannels = 2;
        outprops.sampleRate = 44100;
        choc::audio::WAVAudioFileFormat<true> wav;
        auto writer = wav.createWriter(
            R"(C:\develop\AudioPluginHost_mk2\clapstatetests\graindelayout01.wav)", outprops);
        unsigned int outlen = 20 * 44100;
        choc::buffer::ChannelArrayBuffer<float> outputBuffer(2, outlen);
        outputBuffer.clear();
        auto proc = std::make_unique<GrainDelay>();
        proc->prepareToPlay(44100.0, 10.0);
        int inplaypos = 0;
        xenakios::Envelope panenvelope;
        panenvelope.addPoint({0.0, 0.0});
        panenvelope.addPoint({5.0, 1.0});
        panenvelope.addPoint({10.0, 0.0});
        xenakios::Envelope freeze_env;
        freeze_env.addPoint({0.0f, 0.0f});
        freeze_env.addPoint({2.0f, 0.0f});
        freeze_env.addPoint({2.1f, 1.0f});
        freeze_env.addPoint({4.0f, 1.0f});
        freeze_env.addPoint({4.1f, 0.0f});
        freeze_env.addPoint({6.6f, 0.0f});
        freeze_env.addPoint({6.7f, 1.0f});
        freeze_env.addPoint({7.7f, 1.0f});
        freeze_env.addPoint({7.8f, 0.0f});
        xenakios::Envelope pitch_env;
        pitch_env.addPoint({0.0, 0.0});
        pitch_env.addPoint({2.0, 0.0});
        pitch_env.addPoint({2.0, 0.0});
        pitch_env.addPoint({10.0, 12.0});

        pitch_env.addPoint({16.0, -12.0});
        pitch_env.addPoint({17.5, -11.0});
        pitch_env.addPoint({19.0, 0.0});
        xenakios::Envelope rate_env;
        rate_env.addPoint({0.0, 3.0});
        rate_env.addPoint({2.0, 3.0});
        rate_env.addPoint({10.0, 7.0});
        rate_env.addPoint({15.0, 8.0});
        rate_env.addPoint({20.0, -1.0});
        xenakios::Envelope size_env;
        size_env.addPoint({0.0, 0.0});
        size_env.addPoint({10.0, 0.6});
        size_env.addPoint({20.0, 0.0});
        bool frozen = true;
        float drywet = 1.0f;
        for (int i = 0; i < outlen; ++i)
        {
            float insampleLeft = sourceBuffer.getSample(0, inplaypos);
            float insampleRight = insampleLeft;
            if (inProps.numChannels == 2)
                insampleRight = sourceBuffer.getSample(1, inplaypos);
            ++inplaypos;
            if (inplaypos >= inProps.numFrames)
                inplaypos = 0;
            float secondspos = i / 44100.0f;
            float panwidth = panenvelope.getValueAtPosition(secondspos);
            // proc->setPanWidth(panwidth);

            if (i % 22050 == 0)
                frozen = !frozen;
            if (frozen)
                proc->setPanWidth(0.0f);
            else
                proc->setPanWidth(0.0);
            // proc->setInputFrozen(frozen);
            float pitch = pitch_env.getValueAtPosition(secondspos);
            proc->setCenterPitch(-3.0);
            float rate = rate_env.getValueAtPosition(secondspos);
            proc->setGrainRateOctaves(6.0);
            float sz = size_env.getValueAtPosition(secondspos);
            proc->setSize(0.4);
            float pos = 0.4 + 0.2 * std::sin(2 * 3.141592653 / 44100 * i * 1.25);
            proc->setPosition(pos);
            proc->process(insampleLeft, insampleRight);
            float outLeft = drywet * proc->outputFrame[0] + insampleLeft * (1.0f - drywet);
            float outRight = drywet * proc->outputFrame[1] + insampleRight * (1.0f - drywet);
            outputBuffer.getSample(0, i) = outLeft;
            outputBuffer.getSample(1, i) = outRight;
        }
        writer->appendFrames(outputBuffer.getView());
        std::cout << proc->maxplayheads_used << " playheads were activated at most\n";
    }
    else
        std::cout << "could not open file\n";
}

inline void test_xoroshirorandom()
{
    xenakios::Xoroshiro128Plus xr;
    for (int i = 0; i < 10; ++i)
        std::cout << xr.nextHypCos(0.0, 0.1) << "\n";
    return;
    for (double z = 0.0; z < 1.1; z += 0.1)
    {
        double z1 = z;
        // z1 = std::clamp(z1, std::numeric_limits<double>::epsilon(),
        //                 1.0 - std::numeric_limits<double>::epsilon());
        double y = std::tan(M_PI * (0.0 - 0.5));
        // double y = 2.0 / M_PI * std::log(std::tan(M_PI / 2.0 * z1));
        std::cout << z1 << "\t" << y << "\n";
    }

    return;

    xenakios::DejaVuRandom dj{2};
    dj.setDejaVu(0.4);
    for (int i = 0; i < 20; ++i)
        std::cout << dj.nextFloat() << "\n";
    return;
    xenakios::Xoroshiro128Plus rng{65537, 32771};
    // xenakios::Xoroshiro128Plus rng;
    int iters = 10000000;
    double accum = 0.0;
    for (int i = 0; i < iters; ++i)
        accum += rng.nextFloat();
    // std::cout << rng.nextFloat() << "\n";
    std::cout << accum / iters << "\n";
}

inline void test_testsinesyn()
{
    auto synth = std::make_unique<TestSineSynth>();
    synth->prepare(44100, 512);
    // float L alignas(16)[sfx::core::ConcreteConfig::blockSize],
    //         R alignas(16)[sfx::core::ConcreteConfig::blockSize];
    for (int i = 0; i < 1024; ++i)
    {
        float L = 0.0f;
        float R = 0.0f;
        synth->process(L, R);
    }
}

inline void test_clapengineRT(int iter)
{
    choc::messageloop::initialise();
    auto eng = std::make_unique<ClapProcessingEngine>();
    eng->addProcessorToChain(R"(C:\Program Files\Common Files\CLAP\Surge Synth Team\Surge XT.clap)",
                             0);
    std::string plugname = R"(C:\Program Files\Common Files\CLAP\Airwindows Consolidated.clap)";
    // std::string plugname = R"(C:\Program Files\Common Files\CLAP\Surge Synth Team\Surge XT
    // Effects.clap)";
    eng->addProcessorToChain(plugname, 0);
    // std::this_thread::sleep_for(1000ms);
    auto info = eng->getParameters(1);

    /*0
    for (auto &e : info)
    {
        // std::cout << e.first << " " << e.second << "\n";
    }
    */
    auto &seq = eng->getSequence(0);
    seq.addNote(0.1, 1.0, 0, 0, 60, -1, 0.5, 0.0);
    seq.addNote(0.5, 1.0, 0, 0, 67, -1, 0.5, 0.0);
    seq.addNote(1.0, 0.2, 0, 0, 74, -1, 0.5, 0.0);

    // auto &seq2 = eng->getSequence(1);
    // seq2.addParameterEvent(false, 0.1, 0, 0, 0, 0, 3034571532, 0.02);
    eng->processToFile(std::format(R"(C:\MusicAudio\clap_out\out{}.wav)", iter), 5, 44100.0, 2,
                       nullptr);
    return;

    eng->startStreaming(132, 44100.0, 256, false);
    choc::messageloop::Timer timer{5000, [&eng]() {
                                       std::cout << "engine stop timer callback\n";
                                       eng->stopStreaming();
                                       choc::messageloop::stop();
                                       return false;
                                   }};
    choc::messageloop::run();
}

inline void test_numrange()
{
    NumericRange<int> range;
    assert(range.isEmpty());
    assert(range.getLength() == 0);
    range = NumericRange<int>(8, 13);
}

inline void test_clap_engine_multichain()
{
    try
    {
        size_t bufferSize = 512;
        auto eng = std::make_unique<ClapProcessingEngine>();
        eng->addChain();
        auto &chain0 = eng->getChain(0);
        chain0.addProcessor(R"(C:\Program Files\Common Files\CLAP\Surge Synth Team\Surge XT.clap)",
                            0);

        chain0.addProcessor(
            R"(C:\Program Files\Common Files\CLAP\Airwindows (with BaconPaul and members of the Surge Synth Team)\Airwindows Consolidated.clap)",
            0);

        auto &seq0 = chain0.getSequence(0);
        seq0.addNote(0.0, 1.0, 0, 1, 62, -1, 1.0, 0.0);
        seq0.addNote(1.0, 1.0, 0, 1, 69, -1, 1.0, 0.0);
        seq0.addNote(2.0, 1.0, 0, 1, 51, -1, 1.0, 0.0);
        chain0.chainSequence.addParameterEvent(false, 0.0, 0, 0, 0, 0, 0, 12.0);
        chain0.chainSequence.addParameterEvent(false, 2.5, 0, 0, 0, 0, 0, 0.0);
        chain0.chainSequence.addParameterEvent(false, 3.5, 0, 0, 0, 0, 0, 36);
        chain0.chainSequence.addParameterEvent(false, 3.6, 0, 0, 0, 0, 0, 0);

        eng->addChain();
        auto &chain1 = eng->getChain(1);
        chain1.addProcessor(R"(C:\Program Files\Common Files\CLAP\Surge Synth Team\Surge XT.clap)",
                            0);

        auto &seq1 = chain1.getSequence(0);
        seq1.addNote(0.5, 1.0, 0, 2, 60, -1, 1.0, 0.0);
        seq1.addNote(1.5, 1.0, 0, 2, 67, -1, 1.0, 0.0);
        seq1.addNote(2.5, 1.0, 0, 2, 53, -1, 1.0, 0.5);
        seq1.addNote(3.0, 1.0, 0, 2, 53, -1, 1.0, 0.0);
        eng->processToFile2(R"(C:\MusicAudio\clap_out\chain_out1.wav)", 5.0, 44100.0, 2);
    }
    catch (std::exception &ex)
    {
        std::cout << ex.what() << "\n";
    }
}

inline void test_param_origin()
{
    auto plug = std::make_unique<ClapPluginFormatProcessor>(
        R"(C:\Program Files\Common Files\CLAP\FilePlayerPlugin.clap)", 0);
    char display[256];
    for (float x = -12.0f; x <= 12.0; x += 1.0f)
    {
        if (plug->paramsValueToText(44, x, display, 256))
        {
            std::cout << x << "\t" << display << "\n";
        }
        else
            std::cout << x << " was not formatted\n";
    }
    return;
    for (int i = 0; i < plug->paramsCount(); ++i)
    {
        clap_param_info pinfo;
        if (plug->paramsInfo(i, &pinfo))
        {
            double origin = 0.0;
            if (plug->paramsOrigin(pinfo.id, &origin))
                std::cout << pinfo.name << " has origin value " << origin << "\n";
            else
                std::cout << pinfo.name << " has no origin value\n";
        }
    }
}

inline void test_seq_to_json()
{
    {
        ClapEventSequence seq;
        seq.addNote(0.0, 1.0, 0, 0, 60, -1, 0.67, 0.0);
        seq.addNote(0.0, 1.0, 0, 0, 67, -1, 0.67, 0.5);
        seq.addParameterEvent(false, 0.1, -1, -1, -1, -1, 666, 0.42);
        seq.addMIDI1Message(15.0, 0, 176, 21, 63);
        seq.addProgramChange(16.1, 0, 0, 99);
        std::ofstream ofs(R"(C:\develop\AudioPluginHost_mk2\sequence_test.json)");
        auto json = seq.toJSON();
        ofs << json;
    }
    return;
    {
        auto json =
            choc::file::loadFileAsString(R"(C:\develop\AudioPluginHost_mk2\sequence_test.json)");
        auto root = choc::json::parse(json);
        auto seq = ClapEventSequence::fromValueTree(root);
    }
}

void run_tests();

inline void test_mts_esp_wrapper()
{
    mtsesp_wrapper::MIDIKeyTuner tuner;

    if (tuner.MTS_ESP_available())
    {
        std::cout << "MTS ESP available\n";
        std::cout << "Scale name " << MTS_GetScaleName(tuner.getClient()) << "\n";
    }
    else
        std::cout << "MTS ESP not available\n";
    std::cout << "With MTS ESP enabled\n";
    std::cout << tuner.noteToFrequency(69) << "\n";
    std::cout << tuner.noteToFrequency(60) << "\n";
    std::cout << "With MTS ESP disabled\n";
    tuner.setMTSESP_Enabled(false);
    std::cout << tuner.noteToFrequency(69) << "\n";
    std::cout << tuner.noteToFrequency(60) << "\n";
}

// begin VCV Rack SDK
template <typename T = float> struct TExponentialFilter
{
    T out = 0.f;
    T lambda = 0.f;

    void reset() { out = 0.f; }

    void setLambda(T lambda) { this->lambda = lambda; }

    /** Sets \f$ \lambda = 1 / \tau \f$. */
    void setTau(T tau) { this->lambda = 1 / tau; }

    T process(T deltaTime, T in)
    {
        T y = out + (in - out) * lambda * deltaTime;
        // If no change was made between the old and new output, assume T granularity is too small
        // and snap output to input out =  simd::ifelse(out == y, in, y);
        if (out == y)
            out = in;
        else
            out = y;
        return out;
    }
};
// end VCV Rack SDK

class CV_Sequencer
{
  public:
    struct Event
    {
        double time = 0.0;
        int outport = 0;
        int outchan = 0;
        double voltage = 0.0;
        bool operator<(const Event &other) { return time < other.time; }
    };
    std::vector<Event> events;
    CV_Sequencer()
    {
        events.reserve(4096);
        for (int i = 0; i < numOutports; ++i)
        {
            for (int j = 0; j < maxOutChannels; ++j)
            {
                // it's not completely clear what the SST slew limiter does...
                slews[i][j].setParams(5.0f, 1.0f, sampleRate);
                // so experimenting with an exponential filter too
                expFilters[i][j].setTau(1.0 / 100.0);
                outportvalues[i][j] = 0.0f;
                activatedOutports[i][j] = false;
                smoothedValues[i][j] = 0.0f;
            }
        }
    }
    void process()
    {
        if (events.size() > 0 && curEventIndex < events.size())
        {
            int eventIndex = curEventIndex;
            Event *ptr = &events[curEventIndex];
            float invRate = 1.0 / playRate;
            while ((int)(ptr->time * sampleRate) == (int)(samplePos * invRate))
            {
                int op = ptr->outport;
                int oc = ptr->outchan;
                // std::cout << std::format("{}\t{}\t{}\t{}\t{}\n", outputSamplesProduced,
                // samplePos,
                //                          op, oc, ptr->voltage);
                // assert(ptr->outport >= 0 && ptr->outport < numOutports);
                // assert(ptr->outchan >= 0 && ptr->outchan < maxOutChannels);
                outportvalues[op][oc] = ptr->voltage;
                // once a port/channel has activated, it stays active but
                // we can get at least some cpu savings by not running all the smoothers always
                activatedOutports[op][oc] = true;
                ++curEventIndex;
                if (curEventIndex == events.size())
                    break;
                ptr = &events[curEventIndex];
            }
        }
        for (int i = 0; i < numOutports; ++i)
        {
            for (int j = 0; j < maxOutChannels; ++j)
            {
                if (activatedOutports[i][j])
                {
                    float o = outportvalues[i][j];
                    // smoothedValues[i][j] =
                    //     std::clamp(slews[i][j].step(outportvalues[i][j]), -10.0f, 10.0f);
                    o = expFilters[i][j].process(1.0 / sampleRate, o);
                    o = std::clamp(o, -10.0f, 10.0f);
                    smoothedValues[i][j] = o;
                }
            }
        }
        samplePos += playRate;
        if (samplePos >= loopLen)
        {
            samplePos = 0;
            curEventIndex = 0;
            if (zeroVoltagesAtLoop)
            {
                for (int i = 0; i < numOutports; ++i)
                {
                    for (int j = 0; j < maxOutChannels; ++j)
                    {
                        outportvalues[i][j] = 0.0f;
                    }
                }
            }
        }
        ++outputSamplesProduced;
    }
    void sortAndScanEvents()
    {
        choc::sorting::stable_sort(events.begin(), events.end());
        int highestPort = 0;
        for (auto &e : events)
        {
            e.outport = std::clamp(e.outport, 0, 15);
            e.outchan = std::clamp(e.outchan, 0, 15);
            highestPort = std::max(highestPort, e.outport);
        }
        highestPortNumber = highestPort;
    }
    void seek(double timepos)
    {
        int foundIndex = -1;
        for (size_t i = 0; i < events.size(); ++i)
        {
            if (events[i].time >= timepos)
            {
                foundIndex = i;
                break;
            }
        }
        if (foundIndex >= 0)
        {
            curEventIndex = foundIndex;
        }
    }
    void writeToTextFile(std::string filename)
    {
        std::ofstream os(filename);
        sortAndScanEvents();
        for (auto &e : events)
        {
            os << e.time << " " << e.outport << " " << e.outchan << " " << e.voltage << "\n";
        }
    }
    void loadFromTextFile(std::string filename)
    {
        auto txt = choc::file::loadFileAsString(filename);
        auto lines = choc::text::splitIntoLines(txt, false);
        if (lines.size() > 0)
        {
            events.clear();
            for (auto &line : lines)
            {
                auto tokens = choc::text::splitString(txt, ' ', false);
                if (tokens.size() >= 4)
                {
                    Event ev;
                    ev.time = std::stod(tokens[0]);
                    ev.outport = std::stoi(tokens[1]);
                    ev.outchan = std::stoi(tokens[2]);
                    ev.voltage = std::stod(tokens[3]);
                    events.push_back(ev);
                }
            }
            sortAndScanEvents();
        }
    }
    static constexpr size_t numOutports = 16;
    static constexpr size_t maxOutChannels = 16;
    float outportvalues[numOutports][maxOutChannels];
    bool activatedOutports[numOutports][maxOutChannels];
    float smoothedValues[numOutports][maxOutChannels];
    int highestPortNumber = 0;
    sst::basic_blocks::dsp::SlewLimiter slews[numOutports][maxOutChannels];
    TExponentialFilter<float> expFilters[numOutports][maxOutChannels];
    int curEventIndex = 0;
    double samplePos = 0.0;
    double playRate = 1.0;
    int loopLen = 5 * 44100;
    bool zeroVoltagesAtLoop = true;
    double sampleRate = 44100.0;
    int outputSamplesProduced = 0;
};

inline void test_cv_seq()
{
    auto seq = std::make_unique<CV_Sequencer>();
    seq->events.emplace_back(0.0, 0, 0, 0.5);
    seq->events.emplace_back(2.3, 0, 0, -0.5);
    seq->events.emplace_back(0.0, 1, 0, -9.99);
    seq->events.emplace_back(1.0, 14, 0, 8.888);
    seq->events.emplace_back(1.2, 14, 0, -8.888);
    seq->events.emplace_back(2.1, 14, 0, 8.888);
    std::mt19937 gen;
    std::uniform_real_distribution<double> dist{-5.0, 5.0};
    std::uniform_real_distribution<double> dist2{-10.0, 10.0};
    for (int i = 0; i < 10; ++i)
    {
        seq->events.emplace_back(0.5 + i * 0.1, 8, 0, dist(gen));
    }
    for (int i = 0; i < 40; ++i)
    {
        seq->events.emplace_back(1.4 + i * 0.05, 5, 0, dist2(gen));
    }
    for (int i = 0; i < 400; ++i)
    {
        seq->events.emplace_back(1.4 + i * 0.005, 11, 0, dist(gen));
    }
    double t = 0.0;
    while (t < 5.0)
    {
        seq->events.emplace_back(t, 13, 0, 6.0 * std::sin(2 * 3.141592 * t));
        t += 0.01;
    }
    t = 1.5;
    while (t < 5.0)
    {
        seq->events.emplace_back(t, 15, 0, 6.0 * std::sin(2 * 3.141592 * t * 9.7));
        t += 0.01;
    }
    seq->sortAndScanEvents();
    /*

    seq->seek(0.0);
    std::cout << seq->curEventIndex << "\n";
    seq->seek(1.5);
    std::cout << seq->curEventIndex << "\n";
    return;
    seq->writeToTextFile(R"(C:\develop\AudioPluginHost_mk2\cvsequence.txt)");
    */
    seq->playRate = 1.0;
    choc::audio::WAVAudioFileFormat<true> format;
    choc::audio::AudioFileProperties props;

    props.bitDepth = choc::audio::BitDepth::float32;
    props.numChannels = seq->highestPortNumber + 1;
    props.sampleRate = 44100;
    auto writer = format.createWriter(R"(C:\MusicAudio\clap_out\cvseq\seqout.wav)", props);

    int outlen = 10 * 44100;
    int bufsize = 512;
    int outcounter = 0;

    choc::buffer::ChannelArrayBuffer<float> buffer{(unsigned int)(seq->highestPortNumber + 1),
                                                   (unsigned int)bufsize};

    while (outcounter < outlen)
    {
        for (int i = 0; i < bufsize; ++i)
        {
            seq->process();
            for (int j = 0; j < buffer.getNumChannels(); ++j)
            {
                buffer.getSample(j, i) = 0.1 * seq->smoothedValues[j][0];
            }
        }
        writer->appendFrames(buffer.getView());
        outcounter += bufsize;
    }
}

inline void test_smallvectorenv()
{
    xenakios::Envelope env;
    env.addPoint({0.0, -1.0});
    env.addPoint({0.5, 1.0});
    env.addPoint({0.75, -1.0});
    env.sortPoints();
    choc::audio::WAVAudioFileFormat<true> format;
    choc::audio::AudioFileProperties props;

    props.bitDepth = choc::audio::BitDepth::float32;
    props.numChannels = 1;
    props.sampleRate = 44100;
    auto writer = format.createWriter(R"(C:\MusicAudio\clap_out\envelope01.wav)", props);
    unsigned int outlen = 1 * 44100;
    choc::buffer::ChannelArrayBuffer<float> buf{1, outlen};
    env.processBlock(0.0, 44100, 0, outlen);
    for (int i = 0; i < outlen; ++i)
        buf.getSample(0, i) = env.outputBlock[i];
    writer->appendFrames(buf.getView());
}

inline void test_envelope_iterator()
{
    xenakios::Envelope env;
    env.addPoint({0.0, 0.0});
    env.addPoint({1.0, 1.0});
    env.addPoint({2.0, 0.0});
    xenakios::Envelope::Iterator enviter{env};

    choc::audio::WAVAudioFileFormat<true> format;
    choc::audio::AudioFileProperties props;

    props.bitDepth = choc::audio::BitDepth::float32;
    props.numChannels = 1;
    props.sampleRate = 44100;
    auto writer = format.createWriter(R"(C:\MusicAudio\clap_out\envelope_iter01.wav)", props);
    unsigned int outlen = 2.5 * 44100;
    choc::buffer::ChannelArrayBuffer<float> buf{1, outlen};
    buf.clear();
    for (int i = 0; i < outlen; ++i)
    {
        double tpos = i / props.sampleRate;
        double y = enviter.evaluateAtPosition(tpos);
        buf.getSample(0, i) = y;
    }
}

class GritNoise
{
  public:
    GritNoise() {}
    void setSampleRate(float s) { m_sr = s; }
    void setGrit(float g)
    {
        g = std::clamp(g, 0.0f, 1.0f);
        m_grit = std::pow(g, 3.0f);
    }
    float singleSamplePulses()
    {
        float result = 0.0f;
        float z = m_rng.nextFloatInRange(0.0f, 1.0f);
        if (z < m_grit)
        {
            result = m_rng.nextFloatInRange(-1.0f, 1.0f);
        }
        return result;
    }
    float getNext()
    {
        float result = m_cur_value;
        float z = m_rng.nextFloatInRange(0.0f, 1.0f);
        if (z < m_grit)
        {
            result = m_rng.nextFloatInRange(-1.0f, 1.0f);
            m_cur_value = result;
        }
        m_cur_value *= m_decay_rate;
        return result;
    }

  private:
    xenakios::Xoroshiro128Plus m_rng;
    float m_grit = 1.0f;
    float m_sr = 44100.0f;
    float m_cur_value = 0.0f;
    float m_decay_rate = 0.75f;
};

inline void test_better_grit_noise()
{
    auto mts = MTS_RegisterClient();
    assert(mts);
    double sr = 44100.0;
    unsigned int outlen = sr * 30;
    std::array<StereoSimperSVF, 8> filters;
    for (auto &f : filters)
        f.init();
    GritNoise gnoise;
    // gnoise.setGrit(0.1f);

    xenakios::Envelope grit_env;
    grit_env.addPoint({0.0, 0.01});
    grit_env.addPoint({15.0, 0.50});
    grit_env.addPoint({30.0, 0.1});
    choc::audio::AudioFileProperties props;
    props.numChannels = 2;
    props.sampleRate = sr;
    props.bitDepth = choc::audio::BitDepth::float32;
    choc::audio::WAVAudioFileFormat<true> outformat;
    auto writer = outformat.createWriter(
        R"(C:\MusicAudio\sourcesamples\test_signals\bkenv\grit1.wav)", props);
    if (writer)
    {

        choc::buffer::ChannelArrayBuffer<float> buffer{2, outlen};
        int paramupdatecounter = 0;
        for (int i = 0; i < outlen; ++i)
        {
            if (paramupdatecounter == 0)
            {
                double secondspos = i / sr;
                float grit = grit_env.getValueAtPosition(secondspos);
                // float grit = 0.10f + 0.05 * std::sin(2 * M_PI * 0.2 * secondspos);
                // grit = 0.1;
                gnoise.setGrit(grit);
                for (int j = 0; j < filters.size(); ++j)
                {
                    float lfo_octave = -3.0 + 0.5 * j;
                    float lfohz = std::pow(2.0, lfo_octave);
                    float cutoff = 60.0 + 10.0 * std::sin(2 * M_PI * lfohz * secondspos);
                    float tuningoffset = MTS_RetuningInSemitones(mts, (char)cutoff, 0);
                    filters[j].setCoeff((int)cutoff + tuningoffset, 0.99f, 1.0 / sr);
                }
            }
            float noiseL = gnoise.getNext() * 0.125;
            float noiseR = noiseL;
            float mix[2] = {0.0f, 0.0f};
            for (int j = 0; j < 2; ++j)
            {
                float dummy = 0.0f;
                float filtered = noiseL;
                filters[j].step<StereoSimperSVF::LP>(filters[j], filtered, dummy);
                float pancoeffs[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                float pan = 1.0 / 1 * j;
                sst::basic_blocks::dsp::pan_laws::monoEqualPower(pan, pancoeffs);
                mix[0] += filtered * pancoeffs[0];
                mix[1] += filtered * pancoeffs[3];
            }
            buffer.getSample(0, i) = mix[0];
            buffer.getSample(1, i) = mix[1];
            ++paramupdatecounter;
            if (paramupdatecounter == 32)
                paramupdatecounter = 0;
        }
        writer->appendFrames(buffer.getView());
        std::cout << "finished\n";
    }
    MTS_DeregisterClient(mts);
}
extern "C"
{
    struct Counter
    {
        int c;
        int d;
    };
    int cube(int x);
    float llvm_clamp(float, float, float);
    void counter_init(Counter *, int, int);
    void counter_tick(Counter *);
}

inline void test_llvm_ir()
{
    // std::print("{}\n", llvm_clamp(0.0f, -1.0f, 1.0f));
    // std::print("{}\n", llvm_clamp(-1.1f, -1.0f, 1.0f));
    // std::print("{}\n", llvm_clamp(2.3f, -1.0f, 1.0f));
    // return;
    Counter counter;
    counter_init(&counter, 666, -10);
    for (int i = 0; i < 10; ++i)
    {
        std::print("{}\n", counter.c);
        counter_tick(&counter);
    }
}

class PipeMessageBuilder
{
  public:
    unsigned char *destBuffer = nullptr;
    size_t bufSize = 0;
    size_t pos = 0;
    PipeMessageBuilder(unsigned char *buf, size_t sz) : destBuffer(buf), bufSize(sz)
    {
        memset(destBuffer, 0, bufSize);
    }
    template <typename T> void write(T &&value)
    {
        assert(pos + sizeof(T) <= bufSize);
        memcpy(destBuffer + pos, &value, sizeof(T));
        pos += sizeof(T);
    }
    void write(clap_event_header *hdr)
    {
        assert(pos + hdr->size <= bufSize);
        memcpy(destBuffer + pos, hdr, hdr->size);
        pos += hdr->size;
    }
};

class PipeMessageReader
{
  public:
    unsigned char *sourceBuffer = nullptr;
    size_t bufSize = 0;
    size_t pos = 0;
    PipeMessageReader(unsigned char *src, size_t sz) : sourceBuffer(src), bufSize(sz) {}
    template <typename T> void read(T *value)
    {
        memcpy(value, sourceBuffer + pos, sizeof(T));
        pos += sizeof(T);
    }
    void readClapEvent(clap_event_header *ev)
    {
        // this is pretty sus
        memcpy(ev, sourceBuffer + pos, sizeof(clap_event_header));
        memcpy(ev, sourceBuffer + pos, ev->size);
        pos += ev->size;
    }
};

inline void test_messagebuilder()
{
    unsigned char buf[512];
    {
        PipeMessageBuilder builder(buf, 512);
        uint64_t magic = 666;
        double timestamp = 0.5;
        auto ev = xenakios::make_event_note(0, CLAP_EVENT_NOTE_ON, 0, 0, 60, -1, 0.888);
        builder.write(magic);
        builder.write(timestamp);
        builder.write((clap_event_header *)&ev);
        std::print("built message size is {}\n", builder.pos);
    }
    {
        PipeMessageReader reader(buf, 512);
        uint64_t magic = 0;
        double timestamp = -1.0;
        ClapEventSequence::clap_multi_event nev;
        memset(&nev, 0, sizeof(ClapEventSequence::clap_multi_event));
        reader.read(&magic);
        reader.read(&timestamp);
        reader.readClapEvent((clap_event_header *)&nev.header);

        std::print("read message size is {} {}\n", reader.pos, nev.note.key);
    }
}

using event_target_t = std::tuple<int16_t, int16_t, int16_t, int32_t>;
auto etsz = sizeof(event_target_t);

struct hash_event_target
{
    size_t operator()(const event_target_t &x) const
    {
        choc::hash::xxHash64 h;
        h.addInput(&std::get<0>(x), sizeof(int16_t));
        h.addInput(&std::get<1>(x), sizeof(int16_t));
        h.addInput(&std::get<2>(x), sizeof(int16_t));
        h.addInput(&std::get<3>(x), sizeof(int32_t));
        return h.getHash();
    }
};

template <typename F>
inline void chaseNotes(ClapEventSequence &seq, double chaseToTime, F &&callback)
{
    ClapEventSequence::Iterator iter(seq);
    auto events = iter.readNextEvents(chaseToTime);
    clap_event_note nev;
    std::unordered_set<event_target_t, hash_event_target> activeNotes;
    for (auto &e : events)
    {
        if (e.event.header.type == CLAP_EVENT_NOTE_ON)
        {
            auto nev = &e.event.note;
            activeNotes.insert({nev->port_index, nev->channel, nev->key, nev->note_id});
        }
        if (e.event.header.type == CLAP_EVENT_NOTE_OFF ||
            e.event.header.type == CLAP_EVENT_NOTE_CHOKE)
        {
            auto nev = &e.event.note;
            activeNotes.erase({nev->port_index, nev->channel, nev->key, nev->note_id});
        }
    }
    for (auto &e : activeNotes)
    {
        callback(std::get<2>(e));
    }
}

inline void test_eventchase()
{
    std::mt19937 rng{1000};
    std::uniform_real_distribution<double> valdist{0.0, 1.0};
    std::uniform_int_distribution<unsigned int> parid_dist{666, 670};
    ClapEventSequence seq;
    seq.addNoteOn(0.0, 0, 0, 60, 0.5, -1);
    seq.addNoteOff(1.0, 0, 0, 60, 0.5, -1);
    seq.addNoteOn(0.5, 0, 0, 67, 0.5, -1);
    seq.addNoteOn(0.5, 0, 0, 68, 0.5, -1);
    seq.addNoteOn(2.0, 0, 0, 72, 0.5, -1);
    for (int i = 0; i < 100; ++i)
    {
        double val = valdist(rng);
        clap_id id = parid_dist(rng);
        seq.addParameterEvent(false, i * 0.1, -1, -1, -1, -1, id, val);
    }
    seq.sortEvents();
    chaseNotes(seq, 2.0, [](int key) { std::print("should start note {}\n", key); });
}

inline void test_webview_score()
{
    auto mts = MTS_RegisterClient();
    choc::ui::DesktopWindow dw{{50, 50, 1200, 850}};
    dw.setWindowTitle("Graphic Score");
    choc::ui::WebView::Options opts;
    opts.enableDebugMode = true;
    choc::ui::WebView webview{opts};
    // score pitch to Hz
    // Hz to note number
    // note number to score pitch
    webview.bind("getTuningOffsetTable",
                 [&](const choc::value::ValueView &args) -> choc::value::Value {
                     auto arr = choc::value::createEmptyArray();
                     for (int i = 0; i < 128; ++i)
                     {
                         auto offset = MTS_RetuningInSemitones(mts, i, 0);
                         arr.addArrayElement(offset);
                     }
                     return arr;
                 });
    webview.navigate(R"(C:\develop\AudioPluginHost_mk2\html\canvastest.html)");
    dw.setResizable(true);
    dw.setContent(webview.getViewHandle());

    dw.windowClosed = []() { choc::messageloop::stop(); };
    choc::messageloop::run();
    MTS_DeregisterClient(mts);
}

void test_weierstrass();
void test_airwin_registry();
void test_alt_multilfo();
void print_mandelbrot();

inline int test_choc_subprocess(int mode)
{
    if (mode == 0)
    {
        using namespace std::chrono_literals;
        std::cout << "\e[0;33mstarting sleep...\n";
        std::this_thread::sleep_for(30000ms);
        std::cout << "ended sleep\e[0m\n";
        return 0;
    }
    if (mode == 1)
    {
        std::cout << "starting external process...\n";
        auto r = choc::execute(R"(C:\develop\AudioPluginHost_mk2\build\TestingProgram.exe 0)");
        if (r.statusCode == 0)
        {
            std::cout << r.output << "\n";
        }
        else
        {
            std::cout << "external process exited with error " << r.statusCode << "\n";
            std::cout << r.output << "\n";
        }
        return 0;
    }
    if (mode == 2)
    {
        std::cout << "\e[0;31mcould not calculate\e[0m\n";
        return 42;
    }
    return 0;
}

inline void test_choc_execute()
{
    std::string_view infile = R"(C:\MusicAudio\sourcesamples\sheila.wav)";

    for (int i = 0; i < 12; ++i)
    {
        double semitones = -6.0 + i;
        auto outfile =
            std::format(R"(C:\develop\AudioPluginHost_mk2\python\cdp8\sheila_test_{:02d}.wav)", i);
        std::filesystem::remove(outfile);
        auto cmd = std::format(R"(C:\CDPR8\_cdp\_cdprogs\modify.exe speed 2 {} {} {})", infile,
                               outfile, semitones);
        auto r = choc::execute(cmd);
    }

    // std::cout << r.output << "\n";
}

int main(int argc, char **argv)
{
    test_choc_execute();
    // if (argc >= 2)
    //     return test_choc_subprocess(atoi(argv[1]));
    //  print_mandelbrot();
    //  test_alt_multilfo();
    //  test_airwin_registry();
    //  test_weierstrass();
    //  test_webview_score();
    //  test_eventchase();
    //   test_messagebuilder();
    //    test_llvm_ir();

    // test_better_grit_noise();
    // test_pipe(argc, argv);
    // test_no_ctor();
    // test_envelope_iterator();
    // test_smallvectorenv();
    // test_cv_seq();
    // test_clap_engine_multichain();
    // test_vec();
    // test_mts_esp_wrapper();
    // run_tests();
    // test_seq_to_json();
    // test_param_origin();

    //  test_numrange();
    //  inplace_test();
    //  return 0;
    //   test_osc_receive();

    // test_clapengineRT(1);

    // test_testsinesyn();
    // test_xoroshirorandom();
    // test_grain_delay();
    // test_sc();
    // test_binary_clap_state();
    // test_clapvariant();
    // test_tempomap();
    // test_chained_offline();
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
