#include <algorithm>
#include <pybind11/buffer_info.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include "libMTSMaster.h"
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include "../Common/clap_eventsequence.h"
#include "signalsmith-stretch.h"

namespace py = pybind11;

class TimeStretch
{
  public:
    TimeStretch(int preset, int iomode)
    {
        m_iomode = iomode;
        m_stretcher = std::make_unique<signalsmith::stretch::SignalsmithStretch<float>>();
        outdata.reserve(65536);
        stretchPreset = std::clamp(preset, 0, 4);
        presetConfigs[0] = {0.04f, 0.02f};
        presetConfigs[1] = {0.1f, 0.04f};  // signalsmith cheap
        presetConfigs[2] = {0.12f, 0.03f}; // signalsmith default
        presetConfigs[3] = {0.2f, 0.02f};
        presetConfigs[4] = {0.4f, 0.02f};
    }
    void set_stretch(float st)
    {
        st = std::clamp(st, 0.1f, 64.0f);
        m_stretchfactor = st;
    }
    void set_pitch(float semitones)
    {
        semitones = std::clamp(semitones, -48.0f, 48.0f);
        m_pitchfactor = std::pow(2.0, semitones / 12.0);
    }
    int get_latency() { return m_stretcher->outputLatency(); }
    int samples_required(int numoutsamples) { return numoutsamples / m_stretchfactor; }
    py::array_t<float> process(const py::array_t<float> &input_audio, float insamplerate,
                               int numoutsampleswanted)
    {
        int num_inchans = input_audio.shape(0);
        if (num_inchans > 64)
            throw std::runtime_error(
                std::format("Channel count {} too high, max allowed is 64", num_inchans));

        if (samplerate != insamplerate)
        {
            auto conf = presetConfigs[stretchPreset];
            m_stretcher->configure(num_inchans, insamplerate * conf.first,
                                   insamplerate * conf.second);
            samplerate = insamplerate;
        }
        m_stretcher->setTransposeFactor(m_pitchfactor);
        if (m_iomode == 0)
        {
            int numinsamples = input_audio.shape(1);
            int numoutsamples = numinsamples * m_stretchfactor;
            if (numoutsamples < 16 ||
                (numoutsamples * num_inchans * sizeof(float)) > (1024 * 1024 * 1024))
                throw std::runtime_error(std::format(
                    "Resulting output buffer size {} samples outside limits", numoutsamples));
            outdata.resize(numoutsamples * num_inchans);
            float const *buftostretch[64];
            float *buf_from_stretch[64];
            for (int i = 0; i < 64; ++i)
            {
                if (i < num_inchans)
                {
                    buftostretch[i] = input_audio.data(i);
                    buf_from_stretch[i] = &outdata[numoutsamples * i];
                }
                else
                {
                    buftostretch[i] = nullptr;
                    buf_from_stretch[i] = nullptr;
                }
            }
            m_stretcher->process(buftostretch, numinsamples, buf_from_stretch, numoutsamples);
            py::buffer_info binfo(
                outdata.data(),                         /* Pointer to buffer */
                sizeof(float),                          /* Size of one scalar */
                py::format_descriptor<float>::format(), /* Python struct-style format descriptor */
                2,                                      /* Number of dimensions */
                {2, numoutsamples},                     /* Buffer dimensions */
                {sizeof(float) * numoutsamples,         /* Strides (in bytes) for each index */
                 sizeof(float)});
            py::array_t<float> output_audio{binfo};
            return output_audio;
        }
        else
        {
            if (numoutsampleswanted < 16)
                throw std::runtime_error("Too small output buffer wanted");
            int numinsamples = input_audio.shape(1);
            if (numinsamples != samples_required(numoutsampleswanted))
                throw std::runtime_error(std::format("Expected {} input samples, got {}",
                                                     samples_required(numoutsampleswanted),
                                                     numinsamples));
            int numoutsamples = numoutsampleswanted;
            outdata.resize(numoutsamples * num_inchans);
            float const *buftostretch[64];
            float *buf_from_stretch[64];
            for (int i = 0; i < 64; ++i)
            {
                if (i < num_inchans)
                {
                    buftostretch[i] = input_audio.data(i);
                    buf_from_stretch[i] = &outdata[numoutsamples * i];
                }
                else
                {
                    buftostretch[i] = nullptr;
                    buf_from_stretch[i] = nullptr;
                }
            }
            m_stretcher->process(buftostretch, numinsamples, buf_from_stretch, numoutsamples);
            py::buffer_info binfo(
                outdata.data(),                         /* Pointer to buffer */
                sizeof(float),                          /* Size of one scalar */
                py::format_descriptor<float>::format(), /* Python struct-style format descriptor */
                2,                                      /* Number of dimensions */
                {2, numoutsamples},                     /* Buffer dimensions */
                {sizeof(float) * numoutsamples,         /* Strides (in bytes) for each index */
                 sizeof(float)});
            py::array_t<float> output_audio{binfo};
            // std::cout << output_audio.shape(0) << " " << output_audio.shape(1) << "\n";
            return output_audio;
        }
    }

  private:
    std::unique_ptr<signalsmith::stretch::SignalsmithStretch<float>> m_stretcher;
    std::vector<float> outdata;
    // 0 : stretcher decides output buffer size
    // 1 : client decides stretch output buffer size, needs to change input buffer size as needed
    int m_iomode = 0;
    double m_stretchfactor = 2.0;
    double m_pitchfactor = 1.0;
    float samplerate = -1.0;
    int stretchPreset = 0;
    std::unordered_map<int, std::pair<float, float>> presetConfigs;
};

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

using namespace pybind11::literals;

void init_py1(py::module_ &m)
{
    py::class_<MTSESPSource>(m, "MTS_Source")
        .def(py::init<>())
        .def("setNoteTuningMap", &MTSESPSource::setNoteTuningMap)
        .def("setNoteTuning", &MTSESPSource::setNoteTuning);

    py::class_<TimeStretch>(m, "TimeStretch")
        .def(py::init<int, int>(), "stretch_preset"_a = 2, "io_mode"_a = 0)
        .def("set_stretch", &TimeStretch::set_stretch)
        .def("set_pitch", &TimeStretch::set_pitch)
        .def("get_latency", &TimeStretch::get_latency)
        .def("samples_required", &TimeStretch::samples_required)
        .def("process", &TimeStretch::process, "input_audio"_a, "samplerate"_a = 44100.0f,
             "outputwanted"_a = 0);

    py::class_<ClapEventSequence>(m, "ClapSequence")
        .def(py::init<>())
        .def("getNumEvents", &ClapEventSequence::getNumEvents)
        .def("clearEvents", &ClapEventSequence::clearEvents)
        .def("getSizeInBytes", &ClapEventSequence::getApproxSizeInBytes)
        .def("getMaximumEventTime", &ClapEventSequence::getMaximumEventTime)
        .def("addString", &ClapEventSequence::addString)
        .def("addStringEvent", &ClapEventSequence::addStringEvent)
        .def("addAudioBufferEvent", addAudioBufferEvent)
        .def("addAudioRoutingEvent", &ClapEventSequence::addAudioRoutingEvent)
        .def("addNoteOn", &ClapEventSequence::addNoteOn)
        .def("addNoteOff", &ClapEventSequence::addNoteOff)
        .def("addNote", &ClapEventSequence::addNote, "time"_a = 0.0, "dur"_a = 0.05, "port"_a = 0,
             "ch"_a = 0, "key"_a, "nid"_a = -1, "velo"_a = 1.0, "retune"_a = 0.0)
        .def("addNoteFloatPitch", &ClapEventSequence::addNoteF, "time"_a = 0.0, "dur"_a = 0.05,
             "port"_a = 0, "ch"_a = 0, "pitch"_a, "nid"_a = -1, "velo"_a = 1.0)
        .def("addParameterEvent", &ClapEventSequence::addParameterEvent, "ismod"_a = false,
             "time"_a = 0.0, "port"_a = -1, "ch"_a = -1, "key"_a = -1, "nid"_a = -1, "parid"_a,
             "val"_a)
        .def("addProgramChange", &ClapEventSequence::addProgramChange, "time"_a = 0.0,
             "port"_a = -1, "ch"_a = -1, "program"_a)
        .def("addTransportEvent", &ClapEventSequence::addTransportEvent)
        .def("addNoteExpression", &ClapEventSequence::addNoteExpression);
}
