#include <pybind11/pybind11.h>
#include <vector>
// #include <optional>
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
#include "../Experimental/xen_modulators.h"
#include "clap/ext/params.h"
#include "gui/choc_MessageLoop.h"
#include "Tunings.h"
#if !NOJUCE
#include "juce_audio_processors/juce_audio_processors.h"
#include "juce_core/juce_core.h"
#include "juce_events/juce_events.h"
#include "juce_audio_utils/juce_audio_utils.h"
#endif
#include "../Experimental/bluenoise.h"

namespace py = pybind11;

void init_py1(py::module_ &);

#if !NOJUCE
inline void juceTest(std::string plugfilename)
{
    juce::ScopedJuceInitialiser_GUI gui_init;
    juce::AudioPluginFormatManager mana;
    mana.addDefaultFormats();
    juce::OwnedArray<juce::PluginDescription> descarr;
    juce::VST3PluginFormat vst3;
    vst3.findAllTypesForFile(descarr, plugfilename);
    for (auto &e : descarr)
    {
        std::cout << e->name << "\t" << e->fileOrIdentifier << "\n";
    }
}
#endif
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

constexpr size_t ENVBLOCKSIZE = 64;

int g_inputHookCount = 0;

// When Python is run in interactive interpreter mode and idle waiting for input, this callback
// is called ~10 times a second, letting us handle the Clap plugin on main thread requests
static int XInputHook()
{
    ++g_inputHookCount;
    for (auto &e : ClapProcessingEngine::getEngines())
    {
        e->runMainThreadTasks();
    }
    return 0;
}

static void startStreaming(ClapProcessingEngine &eng, std::optional<unsigned int> deviceId,
                           double sampleRate, int preferredBufferSize, int blockExecution)
{
    eng.startStreaming(deviceId, sampleRate, preferredBufferSize, false);
    if (blockExecution > 0)
    {
        choc::messageloop::initialise();
        bool errSet = false;
        choc::messageloop::Timer timer(50, [&errSet, &eng, blockExecution]() {
            eng.runMainThreadTasks();
            if (PyErr_CheckSignals() != 0)
            {
                std::cout << "got python signal, stopping event loop!\n";
                choc::messageloop::stop();
                errSet = true;
                if (blockExecution == 2)
                {
                    eng.stopStreaming();
                }
                return false;
            }
            return true;
        });
        choc::messageloop::run();
        std::cout << "finished choc loop\n";
        if (errSet)
        {
            throw py::error_already_set();
        }
    }
}

PYBIND11_MODULE(xenakios, m)
{
    using namespace pybind11::literals;
    m.doc() = "pybind11 xenakios plugin"; // optional module docstring
    PyOS_InputHook = XInputHook;

    m.def("writeArrayToFile", &writeArrayToFile);
    // m.def("chocLoop", &runChocLoop);
    m.def("numInputHookCallbacks", []() { return g_inputHookCount; });

    init_py1(m);

    py::module m_const = m.def_submodule("constants", "Constants");

#define C(x) m_const.attr(#x) = py::int_((int)(x));
    C(CLAP_NOTE_EXPRESSION_TUNING);
    C(CLAP_NOTE_EXPRESSION_PAN);
    C(CLAP_NOTE_EXPRESSION_VOLUME);
    C(CLAP_NOTE_EXPRESSION_VIBRATO);
    C(CLAP_NOTE_EXPRESSION_BRIGHTNESS);
    C(CLAP_NOTE_EXPRESSION_PRESSURE);
    C(CLAP_NOTE_EXPRESSION_EXPRESSION);

    C(CLAP_PARAM_IS_AUTOMATABLE);
    C(CLAP_PARAM_IS_AUTOMATABLE_PER_CHANNEL);
    C(CLAP_PARAM_IS_AUTOMATABLE_PER_PORT);
    C(CLAP_PARAM_IS_AUTOMATABLE_PER_KEY);
    C(CLAP_PARAM_IS_AUTOMATABLE_PER_NOTE_ID);
    C(CLAP_PARAM_IS_MODULATABLE);
    C(CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID);
    C(CLAP_PARAM_IS_MODULATABLE_PER_KEY);
    C(CLAP_PARAM_IS_MODULATABLE_PER_PORT);
    C(CLAP_PARAM_IS_MODULATABLE_PER_CHANNEL);
    C(CLAP_PARAM_IS_BYPASS);
    C(CLAP_PARAM_IS_ENUM);
    C(CLAP_PARAM_IS_HIDDEN);
    C(CLAP_PARAM_IS_PERIODIC);
    C(CLAP_PARAM_IS_READONLY);
    C(CLAP_PARAM_IS_STEPPED);

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
        .def("getDepth", &xenakios::BlueNoise::getDepth)
        .def("nextFloat", &xenakios::BlueNoise::operator());

    py::class_<ClapProcessingEngine>(m, "ClapEngine")
        .def(py::init<>())
        .def("addPlugin", &ClapProcessingEngine::addProcessorToChain)
        .def("getSequence", &ClapProcessingEngine::getSequence, py::return_value_policy::reference)
        .def_static("scanPluginFile", &ClapProcessingEngine::scanPluginFile,
                    "Returns plugin info as JSON formatted string")
        .def_static("scanPluginDirs", &ClapProcessingEngine::scanPluginDirectories)
        .def("setSequence", &ClapProcessingEngine::setSequence)
        .def("getParametersJSON", &ClapProcessingEngine::getParametersAsJSON)
        .def("getParameterValueAsText", &ClapProcessingEngine::getParameterValueAsText)
        .def("showGUIBlocking", &ClapProcessingEngine::openPluginGUIBlocking)
        .def("openWindow", &ClapProcessingEngine::openPersistentWindow)
        .def("getDeviceNames", &ClapProcessingEngine::getDeviceNames)
        .def("startStreaming", &startStreaming, "deviceID"_a = std::optional<unsigned int>(),
             "sampleRate"_a = 44100, "bufferSize"_a = 512, "blockExecution"_a = 0)
        .def("wait", &ClapProcessingEngine::wait)
        .def("postNoteMessage", &ClapProcessingEngine::postNoteMessage)
        .def("panic", &ClapProcessingEngine::allNotesOff,
             "Send all note offs to all plugins in chain")
        .def("setMainVolume", &ClapProcessingEngine::setMainVolume,
             "Set engine main volume in decibels")
        .def("stopStreaming", &ClapProcessingEngine::stopStreaming)
        .def("saveStateToBinaryFile", &ClapProcessingEngine::saveStateToBinaryFile)
        .def("loadStateFromBinaryFile", &ClapProcessingEngine::loadStateFromBinaryFile)
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
