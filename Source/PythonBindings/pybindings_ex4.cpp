#include <pybind11/pybind11.h>
#include <pybind11/buffer_info.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include <pybind11/functional.h>
#include "../Common/xap_breakpoint_envelope.h"
#include "sst/basic-blocks/dsp/CorrelatedNoise.h"
#include "sst/basic-blocks/dsp/EllipticBlepOscillators.h"
#include "sst/basic-blocks/dsp/DPWSawPulseOscillator.h"
#include "sst/basic-blocks/dsp/Interpolators.h"
#include "sst/filters++.h"
#include <print>
#include "sst/basic-blocks/dsp/SmoothingStrategies.h"
#include <random>
#include "../granularsynth/granularsynth.h"
#include "../Common/xen_ambisonics.h"
#include "gendyn.h"
#include "../cli/xcli_utils.h"
#include "../Common/clap_eventsequence.h"
#include "../Common/xapdsp.h"

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
                                        py::object sync_param, py::object volume_param, double sr,
                                        double duration)
{
    auto tone_type = osc_name_to_index(tone_name);
    if (tone_type < 0 || tone_type > 6)
        throw std::runtime_error("Invalid tone type");
    auto pitch_env = envelope_from_pyob(pitch_param);
    auto vol_env = envelope_from_pyob(volume_param);
    auto sync_env = envelope_from_pyob(sync_param);
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
        pitch = std::clamp(pitch, -24.0, 136.0);
        double hz = 440.0 * std::pow(2.0, 1.0 / 12 * (pitch - 69.0));
        double sync = sync_env.getValueAtPosition(tpos);
        sync = std::clamp(sync, 0.0, 48.0);
        double syncratio = std::pow(2.0, 1.0 / 12.0 * sync);
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
                    osc.setSyncRatio(syncratio);
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

inline std::vector<std::string> get_sst_filter_types()
{
    init_filter_infos();
    std::vector<std::string> result;
    result.reserve(g_filter_infos.size());
    for (auto &i : g_filter_infos)
    {
        result.push_back(i.address);
    }
    return result;
}

inline void granulator_print_params(ToneGranulator &g)
{
    for (const auto &p : g.parmetadatas)
    {
        std::print("{:12} {}/{} {}\n", p.id, p.groupName, p.name, p.defaultVal);
    }
}

inline void granulator_set_param(ToneGranulator &gran, int64_t parid, float v)
{
    auto it = gran.idtoparvalptr.find(parid);
    if (it != gran.idtoparvalptr.end())
    {
        *(it->second) = v;
    }
    else
        throw std::runtime_error(std::format("parameter id {} does not exist", parid));
}

inline void granulator_set_modulation(ToneGranulator &g, int modslot, int modsource, int modvia,
                                      double depth, int modcurve, int modtarget)
{
    if (modslot >= 0 && modslot < GranulatorModConfig::FixedMatrixSize)
    {
        g.modmatrix.rt.updateActiveAt(modslot, true);
        g.modmatrix.rt.updateRoutingAt(modslot,
                                       GranulatorModConfig::SourceIdentifier{(uint32_t)modsource},
                                       GranulatorModConfig::SourceIdentifier{(uint32_t)modvia},
                                       GranulatorModConfig::CurveIdentifier{modcurve},
                                       GranulatorModConfig::TargetIdentifier{modtarget}, depth);
        if (modvia == 0)
        {
            g.modmatrix.rt.routes[modslot].sourceVia = std::nullopt;
        }
    }
}

inline py::array_t<float> render_granulator(ToneGranulator &gran, double samplerate,
                                            events_t evlist, std::string outputmode,
                                            double outputduration)
{
    if (evlist.empty() && (outputduration <= 0.0 || outputduration > 600.0))
        throw std::runtime_error(std::format(
            "output duration {} invalid (should be larger than 0 and less than 600 seconds)",
            outputduration));
    int chans = 0;
    int ambiorder = 0;
    if (outputmode == "ambisonics_1st_order")
    {
        chans = 4;
        ambiorder = 1;
    }
    if (outputmode == "ambisonics_2nd_order")
    {
        chans = 9;
        ambiorder = 2;
    }
    if (outputmode == "ambisonics_3rd_order")
    {
        chans = 16;
        ambiorder = 3;
    }
    if (chans == 0)
        throw std::runtime_error("invalid audio output mode");
    gran.prepare(samplerate, std::move(evlist), ambiorder, 0, 0.002, 0.002);
    if (gran.events_to_switch.empty() && outputduration == 0.0)
        throw std::runtime_error("grain event list empty after events were erased");
    // we can't know the exact tail amount needed until processing...
    // 1 second is hopefully enough to not cut off the render too abruptly in most cases, but
    // maybe should make the tail length a user settable thing
    int frames = 0;
    if (outputduration > 0.0 && gran.events_to_switch.empty())
    {
        frames = gran.m_sr * outputduration;
    }
    else
    {
        frames = (gran.events_to_switch.back().time_position +
                  gran.events_to_switch.back().duration + 1.0) *
                 gran.m_sr;
    }
    py::buffer_info binfo(
        nullptr,                                /* Pointer to buffer */
        sizeof(float),                          /* Size of one scalar */
        py::format_descriptor<float>::format(), /* Python struct-style format descriptor */
        2,                                      /* Number of dimensions */
        {chans, frames},                        /* Buffer dimensions */
        {sizeof(float) * frames,                /* Strides (in bytes) for each index */
         sizeof(float)});
    py::array_t<float> output_audio{binfo};
    alignas(32) float *writebufs[16];
    for (int i = 0; i < chans; ++i)
        writebufs[i] = output_audio.mutable_data(i);
    using clock = std::chrono::system_clock;
    using ms = std::chrono::duration<double, std::milli>;
    const auto start_time = clock::now();
    int outframecount = 0;
    float procbuf[16 * granul_block_size];
    for (int i = 0; i < 16 * granul_block_size; ++i)
        procbuf[i] = 0.0f;
    while (outframecount < frames)
    {
        int framestoprocess = std::min(granul_block_size, frames - outframecount);
        gran.process_block(procbuf, granul_block_size);
        for (int i = 0; i < framestoprocess; ++i)
        {
            int pos = outframecount + i;
            for (int j = 0; j < chans; ++j)
            {
                writebufs[j][pos] = procbuf[i * chans + j];
            }
        }
        outframecount += granul_block_size;
    }
    const ms render_duration = clock::now() - start_time;
    double rtfactor = (frames / gran.m_sr * 1000.0) / render_duration.count();
    std::print("missed playing {} grains\n", gran.missedgrains);
    std::print("render took {} milliseconds, {:.2f}x realtime\n", render_duration.count(),
               rtfactor);
    return output_audio;
}

void process_airwindows(int index)
{
    audioMasterCallback amc = 0;
    std::unique_ptr<AirwinConsolidatedBase> plugin;
    size_t numParams = 0;
    // we will want to do this smarter if we expect to end up allowing lots of
    // AirWindows plugins
    if (index == 0)
    {
        plugin = make_aw_safe<airwinconsolidated::BezEQ::BezEQ>(amc);
        numParams = airwinconsolidated::BezEQ::kNumParameters;
    }
    if (index == 1)
    {
        plugin = make_aw_safe<airwinconsolidated::HipCrush::HipCrush>(amc);
        numParams = airwinconsolidated::HipCrush::kNumParameters;
    }

    if (!plugin)
        throw std::runtime_error("invalid plugin index for creation");
    char textbuffer[512];
    memset(textbuffer, 0, 512);
    if (plugin->getEffectName(textbuffer))
    {
        std::print("created AirWindows/{}\n", textbuffer);
        std::print("{:3} parameters\n", numParams);
        for (size_t i = 0; i < numParams; ++i)
        {
            plugin->getParameterName(i, textbuffer);
            std::print("{:3} {}\n", i, textbuffer);
        }
    }
    else
    {
        std::print("could not get effect name\n");
    }
}

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

inline float fast_db_to_gain(float db)
{
    // 0.05 is 1/20. ln(10) is ~2.302585
    // Total multiplier = 0.05 * 2.302585 = 0.115129
    float x = db * 0.1151292546497022847f;

    // Fast approx for e^x
    // This uses the IEEE 754 floating point representation trick
    union
    {
        float f;
        int32_t i;
    } u;
    u.i = (int32_t)(12102203 * x + 1064866805);
    return u.f;
}

inline void apply_volume_envelope(py::array_t<float> input_audio, int volume_shaping,
                                  double sample_rate, xenakios::Envelope &vol_env)
{
    if (input_audio.ndim() != 2)
        throw std::runtime_error(
            std::format("array ndim {} incompatible, must be 2", input_audio.ndim()));
    uint32_t numInChans = input_audio.shape(0);
    int frames = input_audio.size();
    alignas(32) float *writebufs[16];
    for (int i = 0; i < numInChans; ++i)
        writebufs[i] = input_audio.mutable_data(i);
    int outcounter = 0;
    const int blocksize = 8;
    vol_env.sortPoints();
    while (outcounter < frames)
    {
        double tpos = outcounter / sample_rate;
        vol_env.processBlock(tpos, sample_rate, 0, blocksize);
        int framestoprocess = std::min(blocksize, frames - outcounter);
        for (int i = 0; i < framestoprocess; ++i)
        {
            float gain = vol_env.outputBlock[i];
            // cubic volume response
            if (volume_shaping == 1)
                gain = gain * gain * gain;
            // decibels
            else if (volume_shaping == 2)
            {
                gain = std::clamp(gain, -120.0f, 12.0f);
                gain = fast_db_to_gain(gain);
            }
            for (int j = 0; j < numInChans; ++j)
            {
                writebufs[j][outcounter + i] *= gain;
            }
        }
        outcounter += blocksize;
    }
}

inline py::array_t<float> encode_to_ambisonics(const py::array_t<float> &input_audio,
                                               double sample_rate, int amb_order,
                                               xenakios::Envelope &azimuth,
                                               xenakios::Envelope &elevation)
{
    if (input_audio.ndim() != 2)
        throw std::runtime_error(
            std::format("array ndim {} incompatible, must be 2", input_audio.ndim()));
    uint32_t numInChans = input_audio.shape(0);
    if (numInChans != 1)
        throw std::runtime_error("input audio must be mono");
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

    int outcounter = 0;
    const int blocksize = 32;
    while (outcounter < frames)
    {
        double tpos = outcounter / sample_rate;
        float aziRads = degreesToRadians(azimuth.getValueAtPosition(tpos));
        float eleRads = degreesToRadians(elevation.getValueAtPosition(tpos));
        float x = 0.0;
        float y = 0.0;
        float z = 0.0;
        sphericalToCartesian(aziRads, eleRads, x, y, z);
        if (amb_order == 1)
            SHEval1(x, y, z, SH1);
        else if (amb_order == 2)
            SHEval2(x, y, z, SH1);
        else if (amb_order == 3)
            SHEval3(x, y, z, SH1);
        for (int i = 0; i < numOutChans; ++i)
            SH1[i] *= n3d2sn3d[i];
        int framestoprocess = std::min(blocksize, frames - outcounter);
        for (int j = 0; j < numOutChans; ++j)
        {
            double gainstep = (SH1[j] - SH0[j]) / blocksize;
            for (int i = 0; i < framestoprocess; ++i)
            {
                float gain = SH0[j] + gainstep * i;
                writebufs[j][outcounter + i] = readbuf[outcounter + i] * gain;
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

void set_mod_amt(GrainEvent &ev, int dest, double amt)
{
    if (dest < 0 || dest >= GrainEvent::MD_NUMDESTS)
        throw std::runtime_error(
            std::format("mod destination {} index invalid, it has to be between 0 and {}", dest,
                        GrainEvent::MD_NUMDESTS - 1));
    ev.modamounts[dest] = amt;
}

struct testsrprovider
{
    double samplerate = 0.0;
    alignas(32) float table_envrate_linear[512];
    void initTables()
    {
        double dsamplerate_os = samplerate * 2;
        for (int i = 0; i < 512; ++i)
        {
            double k =
                dsamplerate_os * pow(2.0, (((double)i - 256.0) / 16.0)) / (double)BLOCK_SIZE_OS;
            table_envrate_linear[i] = (float)(1.f / k);
        }
    }
    float envelope_rate_linear_nowrap(float x)
    {
        x *= 16.f;
        x += 256.f;
        int e = std::clamp<int>((int)x, 0, 0x1ff - 1);

        float a = x - (float)e;

        return (1 - a) * table_envrate_linear[e & 0x1ff] +
               a * table_envrate_linear[(e + 1) & 0x1ff];
    }
    static constexpr size_t BLOCKSIZE = 64;
    static constexpr size_t BLOCK_SIZE_OS = BLOCKSIZE * 2;
};

inline std::vector<double> test_simple_lfo(int sh, xenakios::Envelope &rate_env,
                                           xenakios::Envelope &deform_env,
                                           xenakios::Envelope &phasewarp_env)
{
    testsrprovider srprovider;
    srprovider.samplerate = 44100.0;
    srprovider.initTables();
    using lfo_t = sst::basic_blocks::modulators::SimpleLFO<testsrprovider, 64>;
    lfo_t lfo{&srprovider};
    lfo.attack(lfo_t::SH_MORPHRND);
    int outcounter = 0;
    int blocksize = 512;
    int outlen = 44100 * 10;
    std::vector<double> result;
    while (outcounter < outlen)
    {
        double tpos = outcounter / 44100.0;
        float rate = rate_env.getValueAtPosition(tpos);
        float deform = deform_env.getValueAtPosition(tpos);
        float pwarp = phasewarp_env.getValueAtPosition(tpos);
        lfo.process_block(rate, deform, sh, false, 1.0, pwarp);
        result.push_back(lfo.outputBlock[0]);
        outcounter += blocksize;
    }
    return result;
}

inline std::vector<double> test_morphing_random() { return {}; }

inline void gendyn_print_params(Gendyn2026 &g)
{
    for (const auto &p : g.paramMetaDatas)
    {
        std::print("{:12} {}/{} {}\n", p.id, p.groupName, p.name, p.defaultVal);
    }
}

py::array_t<float> gendyn_render(Gendyn2026 &gendyn, double sr, double outdur,
                                 ClapEventSequence &events)
{
    int numOutChans = 1;
    int frames = outdur * sr;
    py::buffer_info binfo(
        nullptr,                                /* Pointer to buffer */
        sizeof(float),                          /* Size of one scalar */
        py::format_descriptor<float>::format(), /* Python struct-style format descriptor */
        2,                                      /* Number of dimensions */
        {numOutChans, frames},                  /* Buffer dimensions */
        {sizeof(float) * frames,                /* Strides (in bytes) for each index */
         sizeof(float)});
    py::array_t<float> output_audio{binfo};
    float *outdata = output_audio.mutable_data(0);
    int outcounter = 0;
    const size_t blocksize = 64;
    constexpr size_t osfactor = 2;
    dsp::Decimator<osfactor, 16> decimator;
    decimator.reset();
    gendyn.prepare(sr * osfactor);
    float osbuffer[osfactor];
    events.sortEvents();
    ClapEventSequence::IteratorSampleTime eviter{events, sr};
    StereoSimperSVF highpass;
    highpass.init();
    highpass.setCoeff(12.0, 0.0f, 1.0 / sr);
    StereoSimperSVF lowpass;
    lowpass.init();
    float lpreso = 0.0f;
    float lpcutoff = 135.0f; // ~19khz
    lowpass.setCoeff(lpcutoff, lpreso, 1.0 / sr);

    while (outcounter < frames)
    {
        size_t torender = std::min(blocksize, (size_t)frames - outcounter);
        auto blockevents = eviter.readNextEvents(blocksize);
        for (auto &ev : blockevents)
        {
            if (ev.event.header.type == CLAP_EVENT_PARAM_VALUE)
            {
                float val = ev.event.param.value;
                auto it = gendyn.parIdToValuePtr.find(ev.event.param.param_id);
                if (it != gendyn.parIdToValuePtr.end())
                {
                    float minv = gendyn.parIdToMetaDataPtr[ev.event.param.param_id]->minVal;
                    float maxv = gendyn.parIdToMetaDataPtr[ev.event.param.param_id]->maxVal;
                    val = std::clamp(val, minv, maxv);
                    *gendyn.parIdToValuePtr[ev.event.param.param_id] = val;
                    if (ev.event.param.param_id == Gendyn2026::PAR_RANDSEED)
                        gendyn.rng.seed(val, 13);
                    else if (ev.event.param.param_id == Gendyn2026::PAR_TRIGRESET)
                        gendyn.reset();
                    else if (ev.event.param.param_id == Gendyn2026::PAR_INTERPOLATIONMODE)
                        gendyn.setInterpolationMode(val);
                }
                else
                {
                    if (ev.event.param.param_id == 100)
                        highpass.setCoeff(val, 0.0f, 1.0 / sr);
                    else if (ev.event.param.param_id == 101)
                        lpcutoff = val;
                    else if (ev.event.param.param_id == 102)
                        lpreso = val;
                }
            }
        }
        lowpass.setCoeff(lpcutoff, lpreso, 1.0 / sr);
        for (int i = 0; i < torender; ++i)
        {
            for (int j = 0; j < osfactor; ++j)
            {
                osbuffer[j] = gendyn.step();
            }
            float decimsample = decimator.process(osbuffer);
            float dummysample = 0.0f;
            StereoSimperSVF::step<StereoSimperSVF::HP>(highpass, decimsample, dummysample);
            StereoSimperSVF::step<StereoSimperSVF::LP>(lowpass, decimsample, dummysample);
            outdata[outcounter + i] = decimsample;
        }
        outcounter += blocksize;
    }

    return output_audio;
}

void init_py4(py::module_ &m, py::module_ &m_const)
{
    using namespace pybind11::literals;
    py::class_<Gendyn2026>(m, "gendyn")
        .def(py::init<>())
        .def("print_params", &gendyn_print_params)
        .def("prepare", &Gendyn2026::prepare)
        .def("render", &gendyn_render, "samplerate"_a = 44100.0, "duration"_a = 1.0, "events"_a);
    m.def("test_simple_lfo", &test_simple_lfo);
    m.def("encode_to_ambisonics", &encode_to_ambisonics, "input_audio"_a, "sample_rate"_a,
          "ambisonics_order"_a, "azimuth"_a, "elevation"_a);
    m.def("decode_ambisonics_to_stereo", &decode_ambisonics_to_stereo, "input_audio"_a);
    m.def("apply_volume_envelope", &apply_volume_envelope, "input_audio"_a, "shaping"_a,
          "sample_rate"_a, "volume_envelope"_a);
    m.def("airwindows_test", &process_airwindows);
    m.def("generate_tone", &generate_tone, "tone_type"_a, "pitch"_a, "sync"_a, "volume"_a,
          "samplerate"_a, "duration"_a);
    m.def("generate_fmtone", &generate_fm, "carrierfreq"_a, "modulatorfreq"_a, "modulationamont"_a,
          "feedback"_a, "samplerate"_a, "duration"_a);
    m.def("generate_corrnoise", &generate_corrnoise);
    m.def("tone_types", &osc_types);
    m.def("get_sst_filter_types", &get_sst_filter_types);

    py::class_<GrainEvent>(m, "GrainEvent")
        .def(py::init<double, float, float, float>(), "time_position"_a, "duration"_a,
             "pitch_semitones"_a, "volume"_a)
        .def("set_mod_amount", &set_mod_amt)
        .def(
            "set_filter_param",
            [](GrainEvent &ev, int which, double frequency, double resonance, double extpar0) {
                if (which >= 0 && which < 2)
                {
                    // ev.filterparams[which][0] = frequency;
                    // ev.filterparams[which][1] = resonance;
                    // ev.filterparams[which][2] = extpar0;
                }
                else
                    throw std::runtime_error("Filter index out of range");
            },
            "whichfilter"_a, "frequency"_a, "resonance"_a, "expar0"_a = 0.0)
        .def_readwrite("sync_ratio", &GrainEvent::sync_ratio)
        .def_readwrite("azimuth", &GrainEvent::azimuth)
        .def_readwrite("elevation", &GrainEvent::elevation)
        .def_readwrite("envshape", &GrainEvent::envelope_shape)
        .def_readwrite("wavetype", &GrainEvent::generator_type)
        .def_readwrite("duration", &GrainEvent::duration)
        .def_readwrite("timepos", &GrainEvent::time_position)
        .def_readwrite("pitch_semitones", &GrainEvent::pitch_semitones)
        .def_readwrite("pulse_width", &GrainEvent::pulse_width)
        .def_readwrite("fm_frequency", &GrainEvent::fm_frequency_hz)
        .def_readwrite("fm_depth", &GrainEvent::fm_amount)
        .def_readwrite("fm_feedback", &GrainEvent::fm_feedback)
        .def_readwrite("moise_corr", &GrainEvent::noisecorr)
        .def_readwrite("volume", &GrainEvent::volume);

    py::class_<ToneGranulator>(m, "ToneGranulator")
        .def(py::init<>())
        .def("set_voice_aux_envelope", &ToneGranulator::set_voice_aux_envelope)
        .def("set_voice_gain_envelope", &ToneGranulator::set_voice_gain_envelope)
        .def("print_parameters", granulator_print_params)
        .def("set_parameter", granulator_set_param)
        .def("set_modulation", granulator_set_modulation, "slot"_a, "src"_a, "via"_a, "depth"_a,
             "curve"_a, "target"_a)
        .def("render", render_granulator, "samplerate"_a, "event_list"_a, "outputmode"_a,
             "outputduration"_a = 0.0);
}
