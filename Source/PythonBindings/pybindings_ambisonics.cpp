#include <pybind11/pybind11.h>
#include <pybind11/buffer_info.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include "../Common/xen_ambisonics.h"
#include "../Common/xap_breakpoint_envelope.h"
// #include "../Common/clap_eventsequence.h"
#include "../Common/automation_sequence.h"
#include "../Common/xapdsp.h"
#include "plugins/Galactic3Ambisonic.h"
#include <print>

namespace py = pybind11;

inline py::array_t<float> decode_ambisonics_to_stereo(const py::array_t<float> &input_audio)
{
    if (input_audio.ndim() != 2)
        throw std::runtime_error(
            std::format("array ndim {} incompatible, must be 2", input_audio.ndim()));
    uint32_t numInChans = input_audio.shape(0);
    if (!(numInChans == 4 || numInChans == 9 || numInChans == 16))
        throw std::runtime_error(
            std::format("invalid input channel count {}, must be 4/9/16", numInChans));
    int frames = input_audio.size() / numInChans;
    auto numOutChans = 2;
    py::buffer_info binfo(
        nullptr,                                /* Pointer to buffer */
        sizeof(float),                          /* Size of one scalar */
        py::format_descriptor<float>::format(), /* Python struct-style format descriptor */
        2,                                      /* Number of dimensions */
        {numOutChans, frames},                  /* Buffer dimensions */
        {sizeof(float) * frames,                /* Strides (in bytes) for each index */
         sizeof(float)});
    py::array_t<float> output_audio{binfo};
    alignas(32) float *writebufs[2];
    for (int i = 0; i < numOutChans; ++i)
        writebufs[i] = output_audio.mutable_data(i);
    alignas(32) float *readbufs[2];
    for (int i = 0; i < 2; ++i)
        readbufs[i] = (float *)input_audio.data(i);
    const float midGain = 1.414f;
    for (int i = 0; i < frames; ++i)
    {
        float m = readbufs[0][i] * midGain;
        float s = readbufs[1][i];
        writebufs[0][i] = (m + s) * 0.5f;
        writebufs[1][i] = (m - s) * 0.5f;
    }
    return output_audio;
}

inline py::array_t<float> encode_to_ambisonics(const py::array_t<float> &input_audio,
                                               double sample_rate, int amb_order,
                                               xenakios::AutomationSequence &automation)
{
    if (input_audio.ndim() != 2)
        throw std::runtime_error(
            std::format("array ndim {} incompatible, must be 2", input_audio.ndim()));
    uint32_t numInChans = input_audio.shape(0);
    if (numInChans != 1)
        throw std::runtime_error(
            std::format("input audio must be mono, has {} channels", numInChans));
    int frames = input_audio.size();
    auto numOutChans = 0;
    if (amb_order == 1)
        numOutChans = 4;
    if (amb_order == 2)
        numOutChans = 9;
    if (amb_order == 3)
        numOutChans = 16;
    if (numOutChans == 0)
        throw std::runtime_error("invalid ambisonics order");
    py::buffer_info binfo(
        nullptr,                                /* Pointer to buffer */
        sizeof(float),                          /* Size of one scalar */
        py::format_descriptor<float>::format(), /* Python struct-style format descriptor */
        2,                                      /* Number of dimensions */
        {numOutChans, frames},                  /* Buffer dimensions */
        {sizeof(float) * frames,                /* Strides (in bytes) for each index */
         sizeof(float)});
    py::array_t<float> output_audio{binfo};
    alignas(32) float *writebufs[16];
    for (int i = 0; i < numOutChans; ++i)
        writebufs[i] = output_audio.mutable_data(i);
    const float *readbuf = input_audio.data(0);
    alignas(32) float SH0[64];
    alignas(32) float SH1[64];
    memset(SH0, 0, sizeof(float) * 64);
    memset(SH1, 0, sizeof(float) * 64);
    auto allpassbank = std::make_unique<AllPassBank<16>>();
    allpassbank->prepare(sample_rate);

    const int basedelays[] = {401,  443,  521,  631,  761,  887,  1031, 1153,
                              1297, 1321, 1399, 1453, 1489, 1523, 1559, 1597};
    for (size_t i = 0; i < 16; ++i)
    {
        allpassbank->basedelaytimes[i] = basedelays[i];
    }

    // for (size_t i = 0; i < 9; ++i)
    //     allpassbank->filters[i].delayLength = 10000;
    StereoSimperSVF allpasses[16];
    for (size_t i = 0; i < 16; ++i)
    {
        allpasses[i].init();
        allpasses[i].setCoeff(36.0 + i * 2.13, 0.9f, 1.0 / sample_rate);
    }
    automation.sort_events();
    xenakios::AutomationSequence::Iterator automiter(automation, sample_rate);
    int outcounter = 0;
    const int blocksize = 32;
    float aziRads = 0.0f;
    float eleRads = 0.0f;
    float spreadmix = 0.0f;
    float spreadfeedback = 0.0f;
    while (outcounter < frames)
    {
        double tpos = outcounter / sample_rate;
        auto automevts = automiter.readNextEvents(blocksize);
        for (auto &ev : automevts)
        {
            if (ev.id == 0)
                aziRads = degreesToRadians(ev.value);
            else if (ev.id == 1)
                eleRads = degreesToRadians(ev.value);
            else if (ev.id == 2)
                allpassbank->smoothed_mix.setTarget(ev.value);
            else if (ev.id == 3)
                spreadfeedback = ev.value;
        }
        float x = 0.0;
        float y = 0.0;
        float z = 0.0;
        sphericalToCartesian(aziRads, eleRads, x, y, z);
        float spread = std::clamp(spreadmix, 0.0f, 1.0f);
        if (amb_order == 1)
            SHEval1(x, y, z, SH1);
        else if (amb_order == 2)
        {
            SHEval2(x, y, z, SH1);
        }
        else if (amb_order == 3)
        {
            SHEval3(x, y, z, SH1);
            // focus_coeffs3rd_order(SH1, focusval);
        }

        for (int i = 0; i < numOutChans; ++i)
            SH1[i] *= n3d2sn3d[i];
        int framestoprocess = std::min(blocksize, frames - outcounter);
        allpassbank->mix = allpassbank->smoothed_mix.getValue();
        allpassbank->smoothed_mix.process();
        for (int j = 0; j < numOutChans; ++j)
        {
            allpassbank->filters[j].g = spreadfeedback;
            double gainstep = (SH1[j] - SH0[j]) / blocksize;
            for (int i = 0; i < framestoprocess; ++i)
            {
                float gain = SH0[j] + gainstep * i;
                float dummy = 0.0;
                float dry = readbuf[outcounter + i] * gain;
                writebufs[j][outcounter + i] = allpassbank->process(j, dry);
                // float wet = dry;
                // if (j > 0)
                // wet = allpassbank->filters[j].process(dry);
                // StereoSimperSVF::step<StereoSimperSVF::ALL>(allpasses[j], wet, dummy);
                // writebufs[j][outcounter + i] = dry; // (1.0 - focusval) * dry + wet *
                // focusval;
            }
        }

        for (int j = 0; j < numOutChans; ++j)
        {
            SH0[j] = SH1[j];
        }
        outcounter += blocksize;
    }
    return output_audio;
}

inline py::array_t<float> render_galactic3ambisonics(py::array_t<float> input_audio,
                                                     double samplerate,
                                                     xenakios::AutomationSequence &automation)
{
    if (input_audio.ndim() != 2)
        throw std::runtime_error(
            std::format("array ndim {} incompatible, must be 2", input_audio.ndim()));
    uint32_t numInChans = input_audio.shape(0);
    if (numInChans != 1)
        throw std::runtime_error(
            std::format("input audio must be mono, has {} channels", numInChans));
    int frames = input_audio.size();
    int outlen = frames + 5.0 * samplerate;
    auto numOutChans = 16;
    py::buffer_info binfo(
        nullptr,                                /* Pointer to buffer */
        sizeof(float),                          /* Size of one scalar */
        py::format_descriptor<float>::format(), /* Python struct-style format descriptor */
        2,                                      /* Number of dimensions */
        {numOutChans, outlen},                  /* Buffer dimensions */
        {sizeof(float) * outlen,                /* Strides (in bytes) for each index */
         sizeof(float)});
    py::array_t<float> output_audio{binfo};
    alignas(32) float *writebufs[16];
    for (int i = 0; i < numOutChans; ++i)
        writebufs[i] = output_audio.mutable_data(i);
    const float *readbuf = input_audio.data(0);
    auto plug = std::make_unique<airwinconsolidated::Galactic3::Galactic3>(0);
    plug->setSampleRate(samplerate);
    automation.sort_events();
    xenakios::AutomationSequence::Iterator automiter{automation, samplerate};
    const size_t blocksize = 32;
    choc::buffer::ChannelArrayBuffer<float> inbuf{2, blocksize, true};
    choc::buffer::ChannelArrayBuffer<float> outbuf{16, blocksize, true};
    size_t outcounter = 0;

    while (outcounter < outlen)
    {
        auto toprocess = std::min(blocksize, outlen - outcounter);
        for (int i = 0; i < toprocess; ++i)
        {
            if (outcounter + i < frames)
            {
                inbuf.getSample(0, i) = readbuf[outcounter + i];
                inbuf.getSample(1, i) = readbuf[outcounter + i];
            }
            else
            {
                inbuf.getSample(0, i) = 0.0f;
                inbuf.getSample(1, i) = 0.0f;
            }
        }
        auto aevts = automiter.readNextEvents(blocksize);
        for (auto &ev : aevts)
        {

            auto pid = ev.id;
            if (pid >= 0 && pid < airwinconsolidated::Galactic3::kNumParameters)
            {
                std::print("{} {} {}\n", outcounter / samplerate, pid, ev.value);
                plug->setParameter(pid, ev.value);
            }
        }
        plug->processReplacing((float **)inbuf.getView().data.channels,
                               (float **)outbuf.getView().data.channels, toprocess);
        for (int i = 0; i < toprocess; ++i)
        {
            for (int j = 0; j < numOutChans; ++j)
            {
                writebufs[j][outcounter + i] = outbuf.getSample(j, i);
            }
        }
        outcounter += blocksize;
    }
    return output_audio;
}

void init_py_ambisonics(py::module_ &m, py::module_ &m_const)
{
    using namespace pybind11::literals;
    m.def("encode_to_ambisonics", &encode_to_ambisonics, "input_audio"_a, "sample_rate"_a,
          "ambisonics_order"_a, "automation"_a);
    m.def("decode_ambisonics_to_stereo", &decode_ambisonics_to_stereo, "input_audio"_a);
    m.def("render_galactic3ambisonics", &render_galactic3ambisonics, "input_audio"_a,
          "samplerate"_a, "automation"_a);
}
