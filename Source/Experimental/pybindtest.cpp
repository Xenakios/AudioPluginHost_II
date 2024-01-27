#include <pybind11/pybind11.h>
#include <vector>
#include <optional>
#include <pybind11/stl.h>
#include "../Plugins/noise-plethora/plugins/NoisePlethoraPlugin.hpp"
#include "../Plugins/noise-plethora/plugins/Banks.hpp"
#include <iostream>
#include "audio/choc_AudioFileFormat_WAV.h"
#include "xapdsp.h"
#include "xap_utils.h"
#include "xaps/clap_xaudioprocessor.h"

int add(int i, int j) { return i + j; }

std::optional<double> avg(std::vector<double> &v)
{
    if (v.size() == 0)
        return {};
    double sum = 0.0;
    for (auto &e : v)
        sum += e;
    return sum / v.size();
}

void list_plugins()
{
    int k = 0;
    for (int i = 0; i < numBanks; ++i)
    {
        auto &bank = getBankForIndex(i);
        std::cout << "bank " << i << "\n";
        for (int j = 0; j < programsPerBank; ++j)
        {
            std::cout << "\t" << bank.getProgramName(j) << "\t\t" << k << "\n";
            // availablePlugins[k] = bank.getProgramName(j);
            ++k;
        }
    }
}

constexpr size_t ENVBLOCKSIZE = 64;

class NoisePlethoraEngine
{
  public:
    NoisePlethoraEngine(std::string plugintouse)
    {
        m_plug = MyFactory::Instance()->Create(plugintouse);
        if (!m_plug)
            throw std::runtime_error("Plugin could not be created");
    }
    double hipasscutoff = 12.0;
    void processToFile(std::string filename, double durinseconds, double p0, double p1)
    {
        m_p0_env.sortPoints();
        m_p1_env.sortPoints();
        m_filt_env.sortPoints();
        double sr = 44100.0;
        unsigned int numoutchans = 1;
        choc::audio::AudioFileProperties outfileprops;
        outfileprops.formatName = "WAV";
        outfileprops.bitDepth = choc::audio::BitDepth::float32;
        outfileprops.numChannels = numoutchans;
        outfileprops.sampleRate = sr;
        choc::audio::WAVAudioFileFormat<true> wavformat;
        auto writer = wavformat.createWriter(filename, outfileprops);
        if (!writer)
            throw std::runtime_error("Could not create output file");
        int outlen = durinseconds * sr;
        choc::buffer::ChannelArrayBuffer<float> buf{numoutchans, (unsigned int)outlen};
        buf.clear();
        m_plug->init();
        m_plug->m_sr = sr;
        auto chansdata = buf.getView().data.channels;
        StereoSimperSVF dcblocker;
        dcblocker.setCoeff(hipasscutoff, 0.01, 1.0 / sr);
        dcblocker.init();
        StereoSimperSVF filter;

        filter.init();
        int modcounter = 0;
        double mod_p0 = 0.0;
        double mod_p1 = 0.0;
        for (int i = 0; i < outlen; ++i)
        {
            if (modcounter == 0)
            {
                double seconds = i / sr;
                m_p0_env.processBlock(seconds, sr, 2);
                m_p1_env.processBlock(seconds, sr, 2);
                m_filt_env.processBlock(seconds, sr, 2);
                mod_p0 = m_p0_env.outputBlock[0];
                mod_p1 = m_p1_env.outputBlock[0];
                filter.setCoeff(m_filt_env.outputBlock[0], 0.01, 1.0 / sr);
            }
            ++modcounter;
            if (modcounter == ENVBLOCKSIZE)
                modcounter = 0;
            double finalp0 = std::clamp<double>(p0 + mod_p0, 0.0, 1.0);
            double finalp1 = std::clamp<double>(p1 + mod_p1, 0.0, 1.0);
            m_plug->process(finalp0, finalp1);
            float outL = m_plug->processGraph() * 0.5;
            float outR = outL;
            dcblocker.step<StereoSimperSVF::HP>(dcblocker, outL, outR);
            filter.step<StereoSimperSVF::LP>(filter, outL, outR);
            chansdata[0][i] = outL;
        }
        if (!writer->appendFrames(buf.getView()))
            throw std::runtime_error("Could not write output file");
    }
    void setEnvelope(size_t index, xenakios::Envelope<ENVBLOCKSIZE> env)
    {
        if (index == 0)
            m_p0_env = env;
        if (index == 1)
            m_p1_env = env;
        if (index == 2)
            m_filt_env = env;
    }

  private:
    std::unique_ptr<NoisePlethoraPlugin> m_plug;
    using ENVTYPE = xenakios::Envelope<ENVBLOCKSIZE>;
    xenakios::Envelope<ENVBLOCKSIZE> m_p0_env{0.0};
    xenakios::Envelope<ENVBLOCKSIZE> m_p1_env{0.0};
    xenakios::Envelope<ENVBLOCKSIZE> m_filt_env{120.0};
    std::array<ENVTYPE, 3> m_envs{ENVTYPE{0.0}, ENVTYPE{0.0}, ENVTYPE{120.0}};
};

struct SeqEvent
{
    SeqEvent() {}
};

class ClapEventSequencer
{

  public:
    union clap_multi_event
    {
        clap_event_header_t header;
        clap_event_note note;
        clap_event_midi_t midi;
        clap_event_midi2_t midi2;
        clap_event_param_value_t param;
        clap_event_param_mod_t parammod;
        clap_event_param_gesture_t paramgest;
        clap_event_note_expression_t noteexpression;
        clap_event_transport_t transport;
    };
    struct Event
    {
        Event() {}
        template<typename EType>
        Event(double time, EType* e) : timestamp(time), event(*(clap_multi_event*)e) {}
        double timestamp = 0.0;
        clap_multi_event event;
    };
    std::vector<Event> m_evlist;
    ClapEventSequencer() {}
    void addNoteOn(double time, int port, int channel, int key, double velo, int note_id)
    {
        auto ev =
            xenakios::make_event_note(0, CLAP_EVENT_NOTE_ON, port, channel, key, note_id, velo);
        m_evlist.push_back(Event(time, &ev));
    }
    void addNoteOff(double time, int port, int channel, int key, double velo, int note_id)
    {
        auto ev =
            xenakios::make_event_note(0, CLAP_EVENT_NOTE_OFF, port, channel, key, note_id, velo);
        m_evlist.push_back(Event(time, &ev));
    }
};

class ClapProcessingEngine
{
    std::unique_ptr<ClapPluginFormatProcessor> m_plug;

  public:
    ClapEventSequencer m_seq;
    void setSequencer(ClapEventSequencer seq)
    {
        m_seq = seq;
        // m_seq.m_evlist.sortEvents();
    }
    ClapProcessingEngine(std::string plugfilename, int plugindex)
    {
        ClapPluginFormatProcessor::mainthread_id() = std::this_thread::get_id();
        m_plug = std::make_unique<ClapPluginFormatProcessor>(plugfilename, plugindex);
        if (m_plug)
        {

            clap_plugin_descriptor desc;
            if (m_plug->getDescriptor(&desc))
            {
                std::cout << "created : " << desc.name << "\n";
            }
        }
    }
    void processToFile(std::string filename, double duration, double samplerate)
    {
        int procblocksize = 512;
        std::atomic<bool> renderloopfinished{false};
        m_plug->activate(samplerate, procblocksize, procblocksize);
        std::thread th([&] {
            clap_process cp;
            memset(&cp, 0, sizeof(clap_process));
            cp.frames_count = procblocksize;
            cp.audio_inputs_count = 1;
            choc::buffer::ChannelArrayBuffer<float> ibuf{2, (unsigned int)procblocksize};
            ibuf.clear();
            clap_audio_buffer inbufs[1];
            inbufs[0].channel_count = 2;
            inbufs[0].constant_mask = 0;
            inbufs[0].latency = 0;
            auto ichansdata = ibuf.getView().data.channels;
            inbufs[0].data32 = (float **)ichansdata;
            cp.audio_inputs = inbufs;

            cp.audio_outputs_count = 1;
            choc::buffer::ChannelArrayBuffer<float> buf{2, (unsigned int)procblocksize};
            buf.clear();
            clap_audio_buffer outbufs[1];
            outbufs[0].channel_count = 2;
            outbufs[0].constant_mask = 0;
            outbufs[0].latency = 0;
            auto chansdata = buf.getView().data.channels;
            outbufs[0].data32 = (float **)chansdata;
            cp.audio_outputs = outbufs;

            clap_event_transport transport;
            memset(&transport, 0, sizeof(clap_event_transport));
            cp.transport = &transport;
            transport.tempo = 120;
            transport.header.flags = 0;
            transport.header.size = sizeof(clap_event_transport);
            transport.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            transport.header.time = 0;
            transport.header.type = CLAP_EVENT_TRANSPORT;
            transport.flags = CLAP_TRANSPORT_IS_PLAYING;

            clap::helpers::EventList list_in;
            clap::helpers::EventList list_out;
            cp.in_events = list_in.clapInputEvents();
            cp.out_events = list_out.clapOutputEvents();

            std::cout << "plug activated\n";
            m_plug->startProcessing();
            int outcounter = 0;
            int outlensamples = duration * samplerate;
            choc::audio::AudioFileProperties outfileprops;
            outfileprops.formatName = "WAV";
            outfileprops.bitDepth = choc::audio::BitDepth::float32;
            outfileprops.numChannels = 2;
            outfileprops.sampleRate = samplerate;
            choc::audio::WAVAudioFileFormat<true> wavformat;
            auto writer = wavformat.createWriter(filename, outfileprops);

            // choc::buffer::ChannelArrayView<float> bufferview(
            //     choc::buffer::SeparateChannelLayout<float>(cp.audio_outputs->data32));
            // bufferview.clear();
            bool offsent = false;
            while (outcounter < outlensamples)
            {
                if (outcounter == 0)
                {
                    auto ev = xenakios::make_event_note(0, CLAP_EVENT_NOTE_ON, 0, 0, 60, -1, 1.0);
                    list_in.push((const clap_event_header *)&ev);
                }
                if (outcounter >= outlensamples / 2 && offsent == false)
                {
                    auto ev = xenakios::make_event_note(0, CLAP_EVENT_NOTE_OFF, 0, 0, 60, -1, 1.0);
                    list_in.push((const clap_event_header *)&ev);
                    offsent = true;
                }
                m_plug->process(&cp);
                list_out.clear();
                list_in.clear();
                writer->appendFrames(buf.getView());
                std::cout << outcounter << " ";
                cp.steady_time = outcounter;

                outcounter += procblocksize;
            }
            m_plug->stopProcessing();
            writer->flush();
            std::cout << "\nfinished\n";
            renderloopfinished = true;
        });
        using namespace std::chrono_literals;
        // fake event loop to flush the on main thread requests from the plugin
        while (!renderloopfinished)
        {
            m_plug->runMainThreadTasks();
            // might be needlessly short sleep
            std::this_thread::sleep_for(1ms);
        }
        th.join();
    }
};

namespace py = pybind11;

PYBIND11_MODULE(xenakios, m)
{
    m.doc() = "pybind11 xenakios plugin"; // optional module docstring

    m.def("add", &add, "A function that adds two numbers");
    m.def("avg", &avg, "average of list");
    m.def("list_plugins", &list_plugins, "print noise plethora plugins");

    py::class_<ClapEventSequencer>(m, "ClapSequence")
        .def(py::init<>())
        .def("addNoteOn", &ClapEventSequencer::addNoteOn)
        .def("addNoteOff", &ClapEventSequencer::addNoteOff);

    py::class_<ClapProcessingEngine>(m, "ClapEngine")
        .def(py::init<const std::string &, int>())
        .def("setSequence", &ClapProcessingEngine::setSequencer)
        .def("processToFile", &ClapProcessingEngine::processToFile);

    py::class_<NoisePlethoraEngine>(m, "NoisePlethoraEngine")
        .def(py::init<const std::string &>())
        .def("processToFile", &NoisePlethoraEngine::processToFile)
        .def_readwrite("highpass", &NoisePlethoraEngine::hipasscutoff,
                       "high pass filter cutoff, in semitones")
        .def("setEnvelope", &NoisePlethoraEngine::setEnvelope);
    py::class_<xenakios::EnvelopePoint>(m, "EnvelopePoint").def(py::init<double, double>());
    py::class_<xenakios::Envelope<ENVBLOCKSIZE>>(m, "Envelope")
        .def(py::init<>())
        .def(py::init<std::vector<xenakios::EnvelopePoint>>())
        .def("numPoints", &xenakios::Envelope<ENVBLOCKSIZE>::getNumPoints)
        .def("addPoint", &xenakios::Envelope<ENVBLOCKSIZE>::addPoint);
}
