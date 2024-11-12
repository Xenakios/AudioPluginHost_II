#include <pybind11/buffer_info.h>
#include <pybind11/pybind11.h>
#include <stdexcept>
#include <vector>
// #include <optional>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>
#include <pybind11/numpy.h>
#include <pybind11/functional.h>
#include <iostream>
#include "audio/choc_AudioFileFormat_WAV.h"
#include "../Common/xapdsp.h"
#include "../Common/xap_utils.h"
#include "../Host/claphost.h"
#include "../Common/dejavurandom.h"
#include "../Common/xen_modulators.h"
#include "clap/ext/audio-ports.h"
#include "clap/ext/params.h"
#include "gui/choc_MessageLoop.h"
#include "../Common/bluenoise.h"

#if !NOJUCE
#include "juce_audio_processors/juce_audio_processors.h"
#include "juce_core/juce_core.h"
#include "juce_events/juce_events.h"
#include "juce_audio_utils/juce_audio_utils.h"
#endif

namespace py = pybind11;

void init_py1(py::module_ &);
void init_py2(py::module_ &, py::module_ &);

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

static void addChainWithPlugins(ClapProcessingEngine &eng,
                                std::vector<std::pair<std::string, int>> plugins)
{
    for (auto &e : plugins)
    {
        auto &chain = eng.addChain();
        chain.addProcessor(e.first, e.second);
    }
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

inline py::array_t<float> processChain(ProcessorChain &chain, const py::array_t<float> &input_audio)
{
    // for convenience we should probably allow ndim 1, and ndim 3 could work for
    // passing in sidechain audio?
    if (input_audio.ndim() != 2)
        throw std::runtime_error(std::format(
            "Input audio ndim {} not compatible, only ndim 2 allowed", input_audio.ndim()));
    int num_inchans = input_audio.shape(0);
    if (num_inchans > 64)
        throw std::runtime_error(
            std::format("Input audio channel count {} too high, max allowed is 64", num_inchans));
    int numinsamples = input_audio.shape(1);
    float *chanpointers[64];
    for (int i = 0; i < 64; ++i)
    {
        if (i < num_inchans)
        {
            chanpointers[i] = (float *)input_audio.data(i);
        }
        else
        {
            chanpointers[i] = nullptr;
        }
    }
    choc::buffer::SeparateChannelLayout<float> layout{chanpointers};
    choc::buffer::ChannelArrayView<float> view{
        layout, {(unsigned int)num_inchans, (unsigned int)numinsamples}};
    auto fut = chain.thpool.submit_task([&]() { return chain.processAudio(view, {}); });
    auto err = fut.get();
    if (err == -1)
        throw std::runtime_error("Chain was not activated when processing called");
    py::buffer_info binfo(
        chain.audioOutputData.data(),           /* Pointer to buffer */
        sizeof(float),                          /* Size of one scalar */
        py::format_descriptor<float>::format(), /* Python struct-style format descriptor */
        2,                                      /* Number of dimensions */
        {num_inchans, numinsamples},            /* Buffer dimensions */
        {sizeof(float) * numinsamples,          /* Strides (in bytes) for each index */
         sizeof(float)});
    py::array_t<float> output_audio{binfo};
    return output_audio;
}

double getParamAttribute(ProcessorEntry &proc, const std::string &name)
{
    auto it = proc.stringToIdMap.find(name);
    if (it != proc.stringToIdMap.end())
    {
        double val = 0.0;
        if (proc.m_proc->paramsValue(it->second, &val))
        {
            return val;
        }
        throw std::runtime_error("Could not get current value of " + name);
    }
    throw std::runtime_error(name + " is not a parameter attribute of " + proc.name);
}

void setParamAttribute(ProcessorEntry &proc, const std::string &name, double value)
{
    auto it = proc.stringToIdMap.find(name);
    if (it != proc.stringToIdMap.end())
    {
        std::cout << "would post param change for " << name << " " << it->second << " to value "
                  << value << "\n";
        proc.from_ui_fifo.push({ProcessorEntry::Msg::Opcode::SetParam, it->second, value});
        return;
    }
    throw std::runtime_error(name + " is not a parameter attribute of " + proc.name);
}

inline py::array_t<float> getArrayWithBlueNoise(xenakios::BlueNoise &noise, double gain, int chans,
                                                int frames)
{
    py::buffer_info binfo(
        nullptr,                                /* Pointer to buffer */
        sizeof(float),                          /* Size of one scalar */
        py::format_descriptor<float>::format(), /* Python struct-style format descriptor */
        2,                                      /* Number of dimensions */
        {chans, frames},                        /* Buffer dimensions */
        {sizeof(float) * frames,                /* Strides (in bytes) for each index */
         sizeof(float)});
    py::array_t<float> output_audio{binfo};
    for (int i = 0; i < chans; ++i)
    {
        float *writebuf = output_audio.mutable_data(i);
        for (int j = 0; j < frames; ++j)
        {
            writebuf[j] = (-1.0 + 2.0 * noise()) * gain;
        }
    }
    return output_audio;
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
    init_py2(m, m_const);

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

    py::class_<xenakios::DejaVuRandom>(m, "DejaVuRandom")
        .def(py::init<unsigned int>(), "seed"_a = 0)
        .def("setLoopLength", &xenakios::DejaVuRandom::setLoopLength, "count"_a)
        .def("setDejaVu", &xenakios::DejaVuRandom::setDejaVu, "amount"_a,
             "0.0 fully random, 0.5 freeze loop, >0.5 pick randomly from loop")
        .def("nextFloat", &xenakios::DejaVuRandom::nextFloatInRange)
        .def("nextInt", &xenakios::DejaVuRandom::nextIntInRange);
    // .def("setDepth", &xenakios::BlueNoise::setDepth, "depth"_a = 4)
    py::class_<xenakios::BlueNoise>(m, "BlueNoise")
        .def(py::init<unsigned int>(), "seed"_a = 0)
        .def("get_array", &getArrayWithBlueNoise)
        .def_property("depth", &xenakios::BlueNoise::getDepth, &xenakios::BlueNoise::setDepth)
        .def("__call__", &xenakios::BlueNoise::operator());

    py::class_<clap_audio_port_info>(m, "clap_audio_port_info")
        .def_property_readonly("channel_count",
                               [](const clap_audio_port_info &info) { return info.channel_count; })
        .def_property_readonly("name", [](const clap_audio_port_info &info) { return info.name; });

    py::class_<ProcessorEntry>(m, "Processor")
        .def("__getattribute__", &getParamAttribute)
        .def("__setattr__", &setParamAttribute);

    py::class_<ProcessorChain>(m, "ClapChain")
        .def(py::init<std::vector<std::pair<std::string, int>>>())
        .def("getSequence", &ProcessorChain::getSequence)
        .def("audio_port_count", &ProcessorChain::getNumAudioPorts)
        .def("audio_port_info", &ProcessorChain::getAudioPortInfo)
        .def("get_params_json", &ProcessorChain::getParametersAsJSON)
        .def("get_processor", &ProcessorChain::getProcessor, py::return_value_policy::reference)
        .def("activate", &ProcessorChain::activate)
        .def("stop_processing", &ProcessorChain::stopProcessing)
        .def("process", &processChain);

    py::class_<ClapProcessingEngine>(m, "ClapEngine")
        .def(py::init<>())
        .def("addPlugin", &ClapProcessingEngine::addProcessorToChain)
        .def("addChain", &addChainWithPlugins)
        .def("getChain", &ClapProcessingEngine::getChain, py::return_value_policy::reference)
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
        .def("postParameterMessage", &ClapProcessingEngine::postParameterMessage)
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
