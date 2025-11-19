#include <pybind11/pybind11.h>
#include <pybind11/buffer_info.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include <pybind11/functional.h>
#include "../Common/xap_breakpoint_envelope.h"
#include "sst/basic-blocks/dsp/CorrelatedNoise.h"
#include "sst/basic-blocks/dsp/EllipticBlepOscillators.h"
#include "sst/basic-blocks/dsp/DPWSawPulseOscillator.h"
#include "sst/filters++.h"
#include <print>
#include "sst/basic-blocks/dsp/SmoothingStrategies.h"
#include <random>
#include "../Common/granularsynth.h"

namespace py = pybind11;

inline xenakios::Envelope envelope_from_pyob(py::object ob)
{
    if (py::isinstance<py::float_>(ob) || py::isinstance<py::int_>(ob))
    {
        double value = ob.cast<double>();
        // std::print("got constant with value {}\n", value);
        xenakios::Envelope result;
        result.addPoint({0.0, value});
        result.sortPoints();
        result.clearOutputBlock();
        return result;
    }
    if (py::isinstance<py::list>(ob))
    {
        auto l = ob.cast<py::list>();
        std::print("got list with {} elements\n", l.size());
        xenakios::Envelope result;
        for (auto &p : l)
        {
            if (py::isinstance<py::tuple>(p))
            {
                auto tup = p.cast<py::tuple>();
                std::print("{} {}\n", tup[0].cast<double>(), tup[1].cast<double>());
                result.addPoint({tup[0].cast<double>(), tup[1].cast<double>()});
            }
        }
        return result;
    }
    if (py::isinstance<py::function>(ob))
    {
        auto f = ob.cast<py::function>();
        std::print("got a callable\n");
        xenakios::Envelope result;
        for (double x = 0.0; x < 5.0; x += 0.05)
        {
            double y = f(x).cast<double>();
            // std::print("{} {}\n", x, y);
            result.addPoint({x, y});
        }
        result.sortPoints();
        result.clearOutputBlock();
        return result;
    }
    try
    {
        // this isn't super efficient, we make a copy of the envelope that already exists
        // but i guess this will have to do for now...
        auto &foo = ob.cast<xenakios::Envelope &>();
        // std::print("got envelope with {} points\n", foo.getNumPoints());
        foo.sortPoints();
        foo.clearOutputBlock();
        return foo;
    }
    catch (const py::cast_error &e)
    {
        throw std::runtime_error("invalid argument type");
    }
    return {};
}

inline py::array_t<float> generate_tone(std::string tone_name, py::object pitch_param,
                                        py::object volume_param, double sr, double duration)
{
    auto tone_type = osc_name_to_index(tone_name);
    if (tone_type < 0 || tone_type > 6)
        throw std::runtime_error("Invalid tone type");
    auto pitch_env = envelope_from_pyob(pitch_param);
    auto vol_env = envelope_from_pyob(volume_param);
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
    std::variant<sst::basic_blocks::dsp::EBApproxSin<>, sst::basic_blocks::dsp::EBApproxSemiSin<>,
                 sst::basic_blocks::dsp::EBTri<>, sst::basic_blocks::dsp::EBSaw<>,
                 sst::basic_blocks::dsp::EBPulse<>, sst::basic_blocks::dsp::DPWSawOscillator<>,
                 sst::basic_blocks::dsp::DPWPulseOscillator<>>
        voscillator;
    if (tone_type == 0)
        voscillator = sst::basic_blocks::dsp::EBApproxSin<>();
    else if (tone_type == 1)
        voscillator = sst::basic_blocks::dsp::EBApproxSemiSin<>();
    else if (tone_type == 2)
        voscillator = sst::basic_blocks::dsp::EBTri<>();
    else if (tone_type == 3)
        voscillator = sst::basic_blocks::dsp::EBSaw<>();
    else if (tone_type == 4)
        voscillator = sst::basic_blocks::dsp::EBPulse<>();
    else if (tone_type == 5)
        voscillator = sst::basic_blocks::dsp::DPWSawOscillator<>();
    else if (tone_type == 6)
        voscillator = sst::basic_blocks::dsp::DPWPulseOscillator<>();
    std::visit(
        [sr](auto &osc) {
            if constexpr (!(std::is_same_v<decltype(osc),
                                           sst::basic_blocks::dsp::DPWSawOscillator<> &> ||
                            std::is_same_v<decltype(osc),
                                           sst::basic_blocks::dsp::DPWPulseOscillator<> &>))
            {
                osc.setSampleRate(sr);
            }
        },
        voscillator);
    const size_t blocksize = 8;
    int framecount = 0;
    float *writebuf = output_audio.mutable_data(0);
    while (framecount < frames)
    {
        size_t framestoprocess = std::min<int>(blocksize, frames - framecount);
        double tpos = framecount / sr;
        double pitch = pitch_env.getValueAtPosition(tpos);
        pitch = std::clamp(pitch, 0.0, 136.0);
        double hz = 440.0 * std::pow(2.0, 1.0 / 12 * (pitch - 69.0));
        vol_env.processBlock(tpos, sr, 0, blocksize);

        std::visit(
            [&](auto &osc) {
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
                    double gain = vol_env.outputBlock[i];
                    gain = std::clamp(gain, 0.0, 1.0);
                    gain = gain * gain * gain;
                    writebuf[framecount + i] = sample * gain * 0.8;
                }
            },
            voscillator);

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

inline py::array_t<float> generate_corrnoise(xenakios::Envelope &freqenv,
                                             xenakios::Envelope &correnv, double sr,
                                             double duration)
{
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
    NoiseGen gen;
    gen.setSampleRate(sr);
    gen.reset();
    const size_t blocksize = 8;
    int framecount = 0;
    float *writebuf = output_audio.mutable_data(0);
    while (framecount < frames)
    {
        size_t framestoprocess = std::min<int>(blocksize, frames - framecount);
        double tpos = framecount / sr;
        gen.setFrequency(freqenv.getValueAtPosition(tpos));
        gen.setCorrelation(correnv.getValueAtPosition(tpos));
        for (int i = 0; i < framestoprocess; ++i)
        {
            float sample = gen.step();
            writebuf[framecount + i] = sample * 0.8;
        }

        framecount += blocksize;
    }
    return output_audio;
}

inline py::array_t<float> generate_fm(xenakios::Envelope &carrierpitchenv,
                                      xenakios::Envelope &modulatorpitchenv,
                                      xenakios::Envelope &modamtenv, xenakios::Envelope &fbenv,
                                      double sr, double duration)
{
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

    const size_t blocksize = 8;
    int framecount = 0;
    float *writebuf = output_audio.mutable_data(0);
    FMOsc fmosc;
    fmosc.setSampleRate(sr);

    double gain = 0.5;
    while (framecount < frames)
    {
        size_t framestoprocess = std::min<int>(blocksize, frames - framecount);
        double tpos = framecount / sr;
        double carrierfreq =
            440.0 * std::pow(2.0, (1.0 / 12) * (carrierpitchenv.getValueAtPosition(tpos) - 69.0));
        fmosc.setFrequency(carrierfreq);
        double modulationfreq =
            440.0 * std::pow(2.0, (1.0 / 12) * (modulatorpitchenv.getValueAtPosition(tpos) - 69.0));
        fmosc.setModulatorFreq(modulationfreq);
        double modindex = modamtenv.getValueAtPosition(tpos);
        fmosc.setModIndex(modindex);
        double feedback = fbenv.getValueAtPosition(tpos);
        fmosc.setFeedbackAmount(feedback);
        for (int i = 0; i < framestoprocess; ++i)
        {
            float sample = fmosc.step();

            writebuf[framecount + i] = sample * gain * 0.8;
        }

        framecount += blocksize;
    }
    return output_audio;
}

inline py::array_t<float> render_granulator(ToneGranulator &gran, events_t evlist,
                                            std::string outputmode, double stereoangle,
                                            double stereopattern)
{
    gran.prepare(std::move(evlist), stereoangle, stereopattern);
    if (gran.events_to_switch.empty())
        throw std::runtime_error("grain event list empty after events were erased");
    int frames = (gran.events_to_switch.back()[GranulatorVoice::PAR_TPOS] +
                  gran.events_to_switch.back()[GranulatorVoice::PAR_DUR]) *
                 gran.m_sr;
    int chans = 0;
    if (outputmode == "stereo")
        chans = 2;
    if (outputmode == "ambisonics")
        chans = 4;
    if (chans == 0)
        throw std::runtime_error("audio output mode must be stereo or ambisonics");
    py::buffer_info binfo(
        nullptr,                                /* Pointer to buffer */
        sizeof(float),                          /* Size of one scalar */
        py::format_descriptor<float>::format(), /* Python struct-style format descriptor */
        2,                                      /* Number of dimensions */
        {chans, frames},                        /* Buffer dimensions */
        {sizeof(float) * frames,                /* Strides (in bytes) for each index */
         sizeof(float)});
    py::array_t<float> output_audio{binfo};
    float *writebufs[4];
    for (int i = 0; i < chans; ++i)
        writebufs[i] = output_audio.mutable_data(i);
    using clock = std::chrono::system_clock;
    using ms = std::chrono::duration<double, std::milli>;
    const auto start_time = clock::now();
    int framecount = 0;
    float procbuf[4 * granul_block_size];
    for (int i = 0; i < 4 * granul_block_size; ++i)
        procbuf[i] = 0.0f;
    while (framecount < frames - granul_block_size)
    {
        gran.process_block(procbuf, granul_block_size, chans);
        for (int i = 0; i < granul_block_size; ++i)
        {
            int pos = framecount + i;
            for (int j = 0; j < chans; ++j)
            {
                writebufs[j][pos] = procbuf[i * chans + j];
            }
        }
        framecount += granul_block_size;
    }
    const ms render_duration = clock::now() - start_time;
    double rtfactor = (frames / gran.m_sr * 1000.0) / render_duration.count();
    std::print("render took {} milliseconds, {:.2f}x realtime\n", render_duration.count(),
               rtfactor);
    return output_audio;
}

void init_py4(py::module_ &m, py::module_ &m_const)
{

    using namespace pybind11::literals;

    m.def("generate_tone", &generate_tone, "tone_type"_a, "pitch"_a, "volume"_a, "samplerate"_a,
          "duration"_a);
    m.def("generate_fmtone", &generate_fm, "carrierfreq"_a, "modulatorfreq"_a, "modulationamont"_a,
          "feedback"_a, "samplerate"_a, "duration"_a);
    m.def("generate_corrnoise", &generate_corrnoise);
    m.def("tone_types", &osc_types);
    py::class_<ToneGranulator>(m, "ToneGranulator")
        .def(py::init<double, int, std::string, std::string>())
        .def("render", render_granulator);
}
