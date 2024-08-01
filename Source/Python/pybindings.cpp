#include <pybind11/pybind11.h>
#include <vector>
#include <optional>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>
#include <pybind11/numpy.h>
#include <pybind11/functional.h>
#include <iostream>
#include "audio/choc_AudioFileFormat_WAV.h"
#include "../Experimental/xapdsp.h"
#include "../Experimental/xap_utils.h"
#include "../Experimental/offlineclaphost.h"
#include "../Experimental/dejavurandom.h"
#include "libMTSMaster.h"
#include "Tunings.h"
#include "../Experimental/bluenoise.h"
// #include "juce_core/juce_core.h"
#include "juce_audio_utils/juce_audio_utils.h"

namespace py = pybind11;

inline void juceTest()
{
    juce::AudioPluginFormatManager mana;
    mana.addDefaultFormats();
    for(auto& e : mana.getFormats())
        std::cout << e->getName() << "\n";
}

inline void writeArrayToFile(const py::array_t<double> &arr, double samplerate,
                             std::filesystem::path path)
{
    uint32_t numChans = arr.shape(0);
    choc::audio::AudioFileProperties outfileprops;
    outfileprops.formatName = "WAV";
    outfileprops.bitDepth = choc::audio::BitDepth::float32;
    outfileprops.numChannels = numChans;
    outfileprops.sampleRate = samplerate;
    choc::audio::WAVAudioFileFormat<true> wavformat;
    auto writer = wavformat.createWriter(path.string(), outfileprops);
    if (writer)
    {
        py::buffer_info buf1 = arr.request();

        double *ptr1 = static_cast<double *>(buf1.ptr);
        std::vector<double *> ptrs;
        uint32_t numFrames = buf1.size / numChans;
        for (size_t i = 0; i < numChans; ++i)
            ptrs.push_back(ptr1 + i * numFrames);
        choc::buffer::SeparateChannelLayout<double> layout(ptrs.data(), 0);
        choc::buffer::ChannelArrayView<double> bufview(layout, {numChans, numFrames});
        if (!writer->appendFrames(bufview))
            throw std::runtime_error("Could not write to audio file");
        writer->flush();
    }
    else
        throw std::runtime_error("Could not create audio file writer");
}

class MTSESPSource
{
  public:
    MTSESPSource()
    {
        if (MTS_CanRegisterMaster())
        {
            MTS_RegisterMaster();
            std::cout << "registered MTS Source\n";
            double freqs[128];
            for (int i = 0; i < 128; ++i)
            {
                freqs[i] = 440.0 * std::pow(2.0, (69 - i) / 12.0);
            }
            MTS_SetNoteTunings(freqs);
            MTS_SetScaleName("12tet 440Hz");
        }
    }
    ~MTSESPSource()
    {
        MTS_DeregisterMaster();
        std::cout << "deregistered MTS Source\n";
    }
    void setNoteTuning(int midikey, double hz)
    {
        if (midikey >= 0 && midikey < 128)
        {
            MTS_SetNoteTuning(hz, midikey);
        }
    }
    void setNoteTuningMap(std::unordered_map<int, double> tunmap)
    {
        for (const auto &e : tunmap)
        {
            if (e.first >= 0 && e.first < 128)
            {
                if (e.second >= 0.0 && e.second < 20000.0)
                {
                    MTS_SetNoteTuning(e.second, e.first);
                }
            }
        }
    }

  private:
};

constexpr size_t ENVBLOCKSIZE = 64;

inline void addAudioBufferEvent(ClapEventSequence &seq, double time, int32_t target,
                                const py::array_t<double> &arr, int32_t samplerate)
{
    uint32_t numChans = arr.shape(0);
    if (arr.ndim() == 1)
        numChans = 1;
    if (numChans > 256)
        throw std::runtime_error("Numpy array has an unreasonable number of channels " +
                                 std::to_string(numChans) + ", max supported is 256 channels");
    py::buffer_info buf1 = arr.request();
    double *ptr1 = static_cast<double *>(buf1.ptr);
    uint32_t numframes = buf1.size / numChans;
    if (arr.ndim() == 1)
        numframes = arr.shape(0);
    seq.addAudioBufferEvent(time, target, ptr1, numChans, numframes, samplerate);
}

PYBIND11_MODULE(xenakios, m)
{
    using namespace pybind11::literals;
    m.doc() = "pybind11 xenakios plugin"; // optional module docstring

    py::class_<MTSESPSource>(m, "MTS_Source")
        .def(py::init<>())
        .def("setNoteTuningMap", &MTSESPSource::setNoteTuningMap)
        .def("setNoteTuning", &MTSESPSource::setNoteTuning);

    py::class_<ClapEventSequence>(m, "ClapSequence")
        .def(py::init<>())
        .def("getNumEvents", &ClapEventSequence::getNumEvents)
        .def("getSizeInBytes", &ClapEventSequence::getApproxSizeInBytes)
        .def("addString", &ClapEventSequence::addString)
        .def("addStringEvent", &ClapEventSequence::addStringEvent)
        .def("addAudioBufferEvent", addAudioBufferEvent)
        .def("addAudioRoutingEvent", &ClapEventSequence::addAudioRoutingEvent)
        .def("addNoteOn", &ClapEventSequence::addNoteOn)
        .def("addNoteOff", &ClapEventSequence::addNoteOff)
        .def("addNote", &ClapEventSequence::addNote, "time"_a = 0.0, "dur"_a = 0.05, "port"_a = 0,
             "ch"_a = 0, "key"_a, "nid"_a = -1, "velo"_a = 1.0, "retune"_a = 0.0)
        .def("addNoteF", &ClapEventSequence::addNoteF, "time"_a = 0.0, "dur"_a = 0.05, "port"_a = 0,
             "ch"_a = 0, "pitch"_a, "nid"_a = -1, "velo"_a = 1.0)
        .def("addParameterEvent", &ClapEventSequence::addParameterEvent, "ismod"_a = false,
             "time"_a = 0.0, "port"_a = -1, "ch"_a = -1, "key"_a = -1, "nid"_a = -1, "parid"_a,
             "val"_a)
        .def("addProgramChange", &ClapEventSequence::addProgramChange)
        .def("addTransportEvent", &ClapEventSequence::addTransportEvent)
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

    m_const.attr("MIDI_0_FREQ") = Tunings::MIDI_0_FREQ;

    py::class_<xenakios::DejaVuRandom>(m, "DejaVuRandom")
        .def(py::init<unsigned int>())
        .def("setLoopLength", &xenakios::DejaVuRandom::setLoopLength)
        .def("setDejaVu", &xenakios::DejaVuRandom::setDejaVu)
        .def("nextFloat", &xenakios::DejaVuRandom::nextFloatInRange)
        .def("nextInt", &xenakios::DejaVuRandom::nextIntInRange);

    py::class_<xenakios::BlueNoise>(m, "BlueNoise")
        .def(py::init<unsigned int>())
        .def("setDepth", &xenakios::BlueNoise::setDepth)
        .def("nextFloat", &xenakios::BlueNoise::operator());

    py::class_<ClapProcessingEngine>(m, "ClapEngine")
        .def(py::init<>())
        .def("addPlugin", &ClapProcessingEngine::addProcessorToChain)
        .def("getSequence", &ClapProcessingEngine::getSequence, py::return_value_policy::reference)
        .def_static("scanPluginFile", &ClapProcessingEngine::scanPluginFile)
        .def_static("scanPluginDirs", &ClapProcessingEngine::scanPluginDirectories)
        .def("setSequence", &ClapProcessingEngine::setSequence)
        .def("getParameters", &ClapProcessingEngine::getParameters)
        .def("getNumParameters", &ClapProcessingEngine::getNumParameters)
        .def("getParameterInfoString", &ClapProcessingEngine::getParameterInfoString)
        .def("showGUIBlocking", &ClapProcessingEngine::openPluginGUIBlocking)
        .def("openWindow", &ClapProcessingEngine::openPersistentWindow)
        .def("saveStateToFile", &ClapProcessingEngine::saveStateToFile)
        .def("loadStateFromFile", &ClapProcessingEngine::loadStateFromFile)
        .def("processToFile", &ClapProcessingEngine::processToFile, "filename"_a, "duration"_a,
             "samplerate"_a, "numoutchannels"_a = 2);

    py::class_<xenakios::EnvelopePoint>(m, "EnvelopePoint")
        .def(py::init<double, double>())
        .def("__repr__",
             [](const xenakios::EnvelopePoint &a) {
                 return std::format("EnvelopePoint x={} y={}", a.getX(), a.getY());
             })
        .def("getX", &xenakios::EnvelopePoint::getX)
        .def("getY", &xenakios::EnvelopePoint::getY);

    py::class_<xenakios::Envelope<ENVBLOCKSIZE>>(m, "Envelope")
        .def(py::init<>())
        .def(py::init<std::vector<xenakios::EnvelopePoint>>())
        .def("numPoints", &xenakios::Envelope<ENVBLOCKSIZE>::getNumPoints)
        .def("addPoint", &xenakios::Envelope<ENVBLOCKSIZE>::addPoint)
        .def("removePoint", &xenakios::Envelope<ENVBLOCKSIZE>::removeEnvelopePointAtIndex)
        .def("removePoints", &xenakios::Envelope<ENVBLOCKSIZE>::removeEnvelopePoints)
        .def("__iter__",
             [](xenakios::Envelope<ENVBLOCKSIZE> &v) {
                 return py::make_iterator(v.begin(), v.end());
             })

        .def("getPoint", &xenakios::Envelope<ENVBLOCKSIZE>::getPointSafe)
        .def("setPoint", &xenakios::Envelope<ENVBLOCKSIZE>::setPoint)
        .def("getValueAtPosition", &xenakios::Envelope<ENVBLOCKSIZE>::getValueAtPosition);

    py::class_<MultiModulator>(m, "MultiModulator")
        .def(py::init<double>())
        .def("applyToSequence", &MultiModulator::applyToSequence)
        .def("setOutputAsParameter", &MultiModulator::setOutputAsParameter)
        .def("setOutputAsParameterModulation", &MultiModulator::setOutputAsParameterModulation)
        .def("setConnection", &MultiModulator::setConnection)
        .def("setLFOProps", &MultiModulator::setLFOProps);

    m.def("writeArrayToFile", &writeArrayToFile);
    m.def("juceTest", &juceTest);

    m.def("generateNoteExpressionsFromEnvelope", &generateNoteExpressionsFromEnvelope, "",
          py::arg("targetSequence"), py::arg("sourceEnvelope"), py::arg("eventsStartTime"),
          py::arg("duration"), py::arg("granularity"), py::arg("noteExpressionType"),
          py::arg("port"), py::arg("channel"), py::arg("key"), py::arg("note_id"));

    m.def("generateParameterEventsFromEnvelope", &generateParameterEventsFromEnvelope,
          "ismod"_a = false, "targetseq"_a, "env"_a, "start_time"_a = 0.0, "duration"_a,
          "granularity"_a = 0.05, "parid"_a, "port"_a = -1, "chan"_a = -1, "key"_a = -1,
          "nid"_a = -1);

    m.def("generateEnvelopeFromLFO", &generateEnvelopeFromLFO);
}
