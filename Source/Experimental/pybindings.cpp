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
#include "offlineclaphost.h"
#include "dejavurandom.h"

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
    SeqEvent asNote(double timestamp, int etype, int port, int channel, int key, double velo,
                    int note_id = -1)
    {
        SeqEvent result;
        result.timestamp = timestamp;
        auto ev = (clap_event_note *)result.data;
        ev->header.flags = 0;
        ev->header.size = sizeof(clap_event_note);
        ev->header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev->header.time = 0;
        ev->header.type = etype;
        ev->port_index = port;
        ev->channel = channel;
        ev->key = key;
        ev->velocity = velo;
        ev->note_id = note_id;
        struct vec
        {
            float x;
            float y;
        };
        return result;
    }

    double timestamp = 0.0;
    std::byte data[128];
};

namespace py = pybind11;

PYBIND11_MODULE(xenakios, m)
{
    m.doc() = "pybind11 xenakios plugin"; // optional module docstring

    m.def("list_plugins", &list_plugins, "print noise plethora plugins");

    py::class_<ClapEventSequence>(m, "ClapSequence")
        .def(py::init<>())
        .def("getNumEvents", &ClapEventSequence::getNumEvents)
        .def("addNoteOn", &ClapEventSequence::addNoteOn)
        .def("addNoteOff", &ClapEventSequence::addNoteOff)
        .def("addParameterEvent", &ClapEventSequence::addParameterEvent)
        .def("addProgramChange", &ClapEventSequence::addProgramChange)
        .def("addNoteExpression", &ClapEventSequence::addNoteExpression);

    py::module m_const = m.def_submodule("constants", "Constants");

#define C(x) m_const.attr(#x) = py::int_((int)(x));
    C(CLAP_NOTE_EXPRESSION_TUNING);
    C(CLAP_NOTE_EXPRESSION_PAN);
    C(CLAP_NOTE_EXPRESSION_VOLUME);
    C(CLAP_NOTE_EXPRESSION_VIBRATO);
    C(CLAP_NOTE_EXPRESSION_BRIGHTNESS);
    C(CLAP_NOTE_EXPRESSION_PRESSURE);
    C(CLAP_NOTE_EXPRESSION_EXPRESSION);

    py::class_<DejaVuRandom>(m, "DejaVuRandom")
        .def(py::init<unsigned int>())
        .def("setLoopLength", &DejaVuRandom::setLoopLength)
        .def("setDejaVu", &DejaVuRandom::setDejaVu)
        .def("nextFloat", &DejaVuRandom::nextFloatInRange)
        .def("nextInt", &DejaVuRandom::nextIntInRange);

    py::class_<ClapProcessingEngine>(m, "ClapEngine")
        .def(py::init<const std::string &, int>())
        .def("setSequence", &ClapProcessingEngine::setSequencer)
        .def("getParameters", &ClapProcessingEngine::getParameters)
        .def("showGUIBlocking", &ClapProcessingEngine::openPluginGUIBlocking)
        .def("openWindow", &ClapProcessingEngine::openPersistentWindow)
        .def("processToFile", &ClapProcessingEngine::processToFile);

    py::class_<NoisePlethoraEngine>(m, "NoisePlethoraEngine")
        .def(py::init<const std::string &>())
        .def("processToFile", &NoisePlethoraEngine::processToFile)
        .def_readwrite("highpass", &NoisePlethoraEngine::hipasscutoff,
                       "high pass filter cutoff, in semitones")
        .def("setEnvelope", &NoisePlethoraEngine::setEnvelope);
    py::class_<xenakios::EnvelopePoint>(m, "EnvelopePoint")
        .def(py::init<double, double>())
        .def("getX", &xenakios::EnvelopePoint::getX)
        .def("getY", &xenakios::EnvelopePoint::getY);
    py::class_<xenakios::Envelope<ENVBLOCKSIZE>>(m, "Envelope")
        .def(py::init<>())
        .def(py::init<std::vector<xenakios::EnvelopePoint>>())
        .def("numPoints", &xenakios::Envelope<ENVBLOCKSIZE>::getNumPoints)
        .def("addPoint", &xenakios::Envelope<ENVBLOCKSIZE>::addPoint)
        .def("getPoint",&xenakios::Envelope<ENVBLOCKSIZE>::getPointSafe)
        .def("getValueAtPosition", &xenakios::Envelope<ENVBLOCKSIZE>::getValueAtPosition);
    
    m.def("generateNoteExpressionsFromEnvelope", &generateNoteExpressionsFromEnvelope, "",
          py::arg("targetSequence"), py::arg("sourceEnvelope"), py::arg("eventsStartTime"),
          py::arg("duration"), py::arg("granularity"), py::arg("noteExpressionType"),
          py::arg("port"), py::arg("channel"), py::arg("key"), py::arg("note_id"));
    m.def("generateParameterEventsFromEnvelope", &generateParameterEventsFromEnvelope);
    m.def("generateEnvelopeFromLFO", &generateEnvelopeFromLFO);
}
