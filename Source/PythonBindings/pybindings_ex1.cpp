#include <algorithm>
#include <pybind11/pybind11.h>
#include <pybind11/buffer_info.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include "audio/choc_AudioFileFormat.h"
#include "audio/choc_AudioFileFormat_WAV.h"
// #include "audio/choc_AudioFileFormat_FLAC.h"
// #include "audio/choc_AudioFileFormat_Ogg.h"
// #include "audio/choc_AudioFileFormat_MP3.h"
#include "audio/choc_SampleBuffers.h"

#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include "../Common/clap_eventsequence.h"
#include "signalsmith-stretch.h"
#include "../Common/xen_modulators.h"

namespace py = pybind11;

class XAudioFileReader
{
  public:
    XAudioFileReader(std::string path)
    {
        choc::audio::AudioFileFormatList flist;
        flist.addFormat(std::make_unique<choc::audio::WAVAudioFileFormat<false>>());
        // flist.addFormat(std::make_unique<choc::audio::FLACAudioFileFormat<false>>());
        // flist.addFormat(std::make_unique<choc::audio::OggAudioFileFormat<false>>());
        // flist.addFormat(std::make_unique<choc::audio::MP3AudioFileFormat>());

        m_reader = flist.createReader(path);
        if (!m_reader)
            throw std::runtime_error("Can't open " + path);
        m_loopEnd = m_reader->getProperties().numFrames;
    }
    ~XAudioFileReader() { /*std::cout << "XAudioFileReader dtor\n";*/ }
    void seek(uint64_t pos) { m_currentPos = pos; }
    void set_loop_points(uint64_t start, uint64_t end)
    {
        m_loopStart = start;
        m_loopEnd = end;
    }
    void set_looping(bool b) { m_looping = b; }
    py::array_t<float> read(int num_samples, int force_output_channels)
    {
        if (force_output_channels == 666)
            throw std::runtime_error("exception test");
        auto filechans = m_reader->getProperties().numChannels;
        int outchans_to_use = filechans;

        if (filechans == 1 && force_output_channels > 1)
            outchans_to_use = force_output_channels;
        if (m_readVec.size() < num_samples * outchans_to_use)
        {
            m_readVec.resize(outchans_to_use * num_samples);
        }
        float *chanpointers[64];
        for (int i = 0; i < 64; ++i)
        {
            if (i < outchans_to_use)
                chanpointers[i] = &m_readVec[i * num_samples];
            else
                chanpointers[i] = nullptr;
        }
        choc::buffer::SeparateChannelLayout<float> layout{chanpointers};
        choc::buffer::ChannelArrayView<float> view{layout, {filechans, (unsigned int)num_samples}};
        if (m_looping && m_currentPos + num_samples >= m_loopEnd)
        {
            m_currentPos = m_loopStart;
            m_reader->readFrames(m_currentPos, view);
        }
        else
        {
            m_reader->readFrames(m_currentPos, view);
        }

        if (filechans == 1 && force_output_channels > 1)
        {
            for (int i = 0; i < num_samples; ++i)
            {
                chanpointers[1][i] = chanpointers[0][i];
            }
        }
        py::buffer_info binfo(
            m_readVec.data(),                       /* Pointer to buffer */
            sizeof(float),                          /* Size of one scalar */
            py::format_descriptor<float>::format(), /* Python struct-style format descriptor */
            2,                                      /* Number of dimensions */
            {outchans_to_use, num_samples},         /* Buffer dimensions */
            {sizeof(float) * num_samples,           /* Strides (in bytes) for each index */
             sizeof(float)});
        py::array_t<float> output_audio{binfo};
        m_currentPos += num_samples;
        return output_audio;
    }
    int sample_rate() { return m_reader->getProperties().sampleRate; }
    uint64_t tell() { return m_currentPos; }
    uint64_t get_num_frames() { return m_reader->getProperties().numFrames; }

  private:
    choc::audio::AudioFileProperties m_props;
    std::unique_ptr<choc::audio::AudioFileReader> m_reader;
    uint64_t m_currentPos = 0;
    uint64_t m_loopStart = 0;
    uint64_t m_loopEnd = 0;
    bool m_looping = false;
    std::vector<float> m_readVec;
};

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
            /*
            // Satisfying this check is tricky when using PedalBoard AudioFile
            if (numinsamples != samples_required(numoutsampleswanted))
                throw std::runtime_error(std::format("Expected {} input samples, got {}",
                                                     samples_required(numoutsampleswanted),
                                                   numinsamples));
            */
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
    py::class_<TimeStretch>(m, "TimeStretch")
        .def(py::init<int, int>(), "stretch_preset"_a = 2, "io_mode"_a = 0)
        .def("set_stretch", &TimeStretch::set_stretch)
        .def("set_pitch", &TimeStretch::set_pitch)
        .def("get_latency", &TimeStretch::get_latency)
        .def("samples_required", &TimeStretch::samples_required)
        .def("process", &TimeStretch::process, "input_audio"_a, "samplerate"_a,
             "outputwanted"_a = 0);

    py::class_<XAudioFileReader>(m, "AudioFile")
        .def(py::init<std::string>(), "filename"_a)
        .def("seek", &XAudioFileReader::seek)
        .def("tell", &XAudioFileReader::tell)
        .def_property_readonly("sample_rate", &XAudioFileReader::sample_rate)
        .def_property_readonly("frames", &XAudioFileReader::get_num_frames)
        .def("set_looping", &XAudioFileReader::set_looping)
        .def("set_loop_points", &XAudioFileReader::set_loop_points)
        .def("read", &XAudioFileReader::read);

    py::class_<AltMultiModulator>(m, "MultiLFO")
        .def(py::init<double>())
        .def("set_mod_amt", &AltMultiModulator::set_modulation_amount)
        .def("set_shape", &AltMultiModulator::set_lfo_shape)
        .def("set_rate", &AltMultiModulator::set_lfo_rate)
        .def("set_deform", &AltMultiModulator::set_lfo_deform)
        .def("set_shift", &AltMultiModulator::set_lfo_shift)
        .def("set_rseed", &AltMultiModulator::set_lfo_randseed)
        .def("to_list", &AltMultiModulator::get_as_vector, "which_output"_a, "duration"_a,
             "shift"_a = 0.0, "scale"_a = 1.0, "skip"_a = 2);

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
