#include <pybind11/pybind11.h>
#include <pybind11/buffer_info.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include "../Common/xap_breakpoint_envelope.h"
#include "sst/basic-blocks/dsp/EllipticBlepOscillators.h"
#include "sst/basic-blocks/dsp/DPWSawPulseOscillator.h"
#include <print>

namespace py = pybind11;

struct tone_info
{
    int index = -1;
    const char *name = nullptr;
};

tone_info osc_infos[7] = {{0, "EBSINE"},   {1, "EBSEMISINE"}, {2, "EBTRIANGLE"}, {3, "EBSAW"},
                          {4, "EBSQUARE"}, {5, "DPWSAW"},     {6, "DPWSQUARE"}};

inline std::vector<std::string> osc_types()
{
    std::vector<std::string> result;
    for (int i = 0; i < 7; ++i)
        result.push_back(osc_infos[i].name);
    return result;
}

inline int osc_name_to_index(std::string name)
{
    for (int i = 0; i < 7; ++i)
        if (name == osc_infos[i].name)
            return osc_infos[i].index;
    return -1;
}

inline py::array_t<float> generate_tone(std::string tone_name, xenakios::Envelope &pitch_env,
                                        xenakios::Envelope &volume_env, double sr, double duration)
{
    auto tone_type = osc_name_to_index(tone_name);
    if (tone_type < 0 || tone_type > 6)
        throw std::runtime_error("Invalid tone type");
    pitch_env.sortPoints();
    volume_env.sortPoints();
    int chans = 1;
    int frames = duration * sr;
    py::buffer_info binfo(
        nullptr,                                /* Pointer to buffer */
        sizeof(float),                          /* Size of one scalar */
        py::format_descriptor<float>::format(), /* Python struct-style format descriptor */
        2,                                      /* Number of dimensions */
        {chans, frames},                        /* Buffer dimensions */
        {sizeof(float) * frames,                /* Strides (in bytes) for each index */
         sizeof(float)});
    py::array_t<float> output_audio{binfo};
    sst::basic_blocks::dsp::EBApproxSin<> osc_sin;
    sst::basic_blocks::dsp::EBApproxSemiSin<> osc_semisin;
    sst::basic_blocks::dsp::EBTri<> osc_tri;
    sst::basic_blocks::dsp::EBSaw<> osc_saw;
    sst::basic_blocks::dsp::EBPulse<> osc_pulse;
    sst::basic_blocks::dsp::DPWSawOscillator<> osc_saw_dpw;
    sst::basic_blocks::dsp::DPWPulseOscillator<> osc_pulse_dpw;
    osc_sin.setSampleRate(sr);
    osc_semisin.setSampleRate(sr);
    osc_tri.setSampleRate(sr);
    osc_saw.setSampleRate(sr);
    osc_pulse.setSampleRate(sr);
    const size_t blocksize = 8;
    int framecount = 0;
    float *writebuf = output_audio.mutable_data(0);
    volume_env.clearOutputBlock();
    while (framecount < frames)
    {
        size_t framestoprocess = std::min<int>(blocksize, frames - framecount);
        double tpos = framecount / sr;
        double pitch = pitch_env.getValueAtPosition(tpos);
        pitch = std::clamp(pitch, 0.0, 136.0);
        double hz = 440.0 * std::pow(2.0, 1.0 / 12 * (pitch - 69.0));
        volume_env.processBlock(tpos, sr, 0, blocksize);
        auto renderfunc = [&](auto &osc) {
            if constexpr (std::is_same_v<decltype(osc),
                                         sst::basic_blocks::dsp::DPWSawOscillator<> &> ||
                          std::is_same_v<decltype(osc),
                                         sst::basic_blocks::dsp::DPWPulseOscillator<> &>)
            {
                osc.setFrequency(hz, 1.0 / sr);
            }
            else
            {
                osc.setFrequency(hz);
            }

            for (int i = 0; i < framestoprocess; ++i)
            {
                float sample = osc.step();
                double gain = volume_env.outputBlock[i];
                gain = std::clamp(gain, 0.0, 1.0);
                gain = gain * gain * gain;
                writebuf[framecount + i] = sample * gain * 0.8;
            }
        };
        if (tone_type == 0)
            renderfunc(osc_sin);
        else if (tone_type == 1)
            renderfunc(osc_semisin);
        else if (tone_type == 2)
            renderfunc(osc_tri);
        else if (tone_type == 3)
            renderfunc(osc_saw);
        else if (tone_type == 4)
            renderfunc(osc_pulse);
        else if (tone_type == 5)
            renderfunc(osc_saw_dpw);
        else if (tone_type == 6)
            renderfunc(osc_pulse_dpw);
        framecount += blocksize;
    }
    return output_audio;
}

using EnvOrDouble = std::variant<double, xenakios::Envelope *>;
template <class... Ts> struct overloaded : Ts...
{
    using Ts::operator()...;
};
void vartest(EnvOrDouble x)
{
    std::visit(overloaded{[](double val) { std::print("got double {}\n", val); },
                          [](xenakios::Envelope *env) {
                              std::print("got envelope object ptr {}\n", (void *)env);
                          }},
               x);
}

void init_py4(py::module_ &m, py::module_ &m_const)
{

    using namespace pybind11::literals;
    // m.def("vartest", &vartest);
    m.def("generate_tone", &generate_tone, "tone_type"_a, "pitch"_a, "volume"_a, "samplerate"_a,
          "duration"_a);
    m.def("tone_types", &osc_types);
}
