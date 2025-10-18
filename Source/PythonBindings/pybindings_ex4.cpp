#include <pybind11/pybind11.h>
#include <pybind11/buffer_info.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include "../Common/xap_breakpoint_envelope.h"
#include "sst/basic-blocks/dsp/EllipticBlepOscillators.h"
#include "sst/basic-blocks/dsp/DPWSawPulseOscillator.h"
#include "sst/filters++.h"
#include <print>

namespace py = pybind11;

const int granul_block_size = 8;

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
namespace sfpp = sst::filtersplusplus;
struct FilterInfo
{
    std::string address;
    sfpp::FilterModel model;
    sfpp::FilterSubModel submodel;
    sfpp::ModelConfig modelconfig;
};

std::vector<FilterInfo> g_filter_infos;

void init_filter_infos()
{
    if (g_filter_infos.size() > 0)
        return;
    auto models = sfpp::Filter::availableModels();
    std::string address;
    address.reserve(256);
    for (auto &mod : models)
    {
        auto subm = sfpp::Filter::availableModelConfigurations(mod, true);
        for (auto s : subm)
        {
            address = sfpp::toString(mod);
            if (s == sfpp::ModelConfig())
            {
            }
            auto [pt, st, dt, smt] = s;
            if (pt != sfpp::Passband::UNSUPPORTED)
            {
                address += "/" + sfpp::toString(pt);
            }
            if (st != sfpp::Slope::UNSUPPORTED)
            {
                address += "/" + sfpp::toString(st);
            }
            if (dt != sfpp::DriveMode::UNSUPPORTED)
            {
                address += "/" + sfpp::toString(dt);
            }
            if (smt != sfpp::FilterSubModel::UNSUPPORTED)
            {
                address += "/" + sfpp::toString(smt);
            }
            address = choc::text::toLowerCase(address);
            address = choc::text::replace(address, " ", "_", "&", "and", ",", "");
            FilterInfo info;
            info.address = address;
            info.model = mod;
            info.modelconfig = s;
            g_filter_infos.push_back(info);
        }
    }
}

class GranulatorVoice
{
  public:
    sst::basic_blocks::dsp::EBApproxSin<> osc_sin;
    sst::basic_blocks::dsp::EBApproxSemiSin<> osc_semisin;
    sst::basic_blocks::dsp::EBTri<> osc_tri;
    sst::basic_blocks::dsp::EBSaw<> osc_saw;
    sst::basic_blocks::dsp::EBPulse<> osc_pulse;
    sst::filtersplusplus::Filter filter0;
    int phase = 0;
    int endphase = 0;
    double sr = 0.0;
    bool active = false;
    int osctype = 0;
    float ambcoeffs[4] = {};
    double cutoff0 = 0.0;
    double reson0 = 0.0;
    double graingain = 0.0;
    double envshape = 0.5;
    GranulatorVoice() {}
    void set_samplerate(double hz)
    {
        sr = hz;
        osc_sin.setSampleRate(hz);
        osc_semisin.setSampleRate(hz);
        osc_tri.setSampleRate(hz);
        osc_saw.setSampleRate(hz);
        osc_pulse.setSampleRate(hz);
    }
    void set_filter_type(std::string ft)
    {
        bool foundfilter = false;
        for (const auto &finfo : g_filter_infos)
        {
            if (finfo.address == ft)
            {
                filter0.setFilterModel(finfo.model);
                filter0.setModelConfiguration(finfo.modelconfig);
                std::print("processing with {}\n", finfo.address);
                foundfilter = true;
                // filsu.makeCoefficients(0, 440.0f, 0.5f);
                break;
            }
        }
        if (!foundfilter)
        {
            std::print("could not instantiate filter \"{}\", ensure the name is correct\n", ft);
        }
        filter0.setSampleRateAndBlockSize(sr, granul_block_size);
        filter0.setMono();
        if (!filter0.prepareInstance())
        {
            std::print("could not prepare filter\n");
        }
    }
    // t, dur, hz, 4, volume, horz_angle, vert_angle, cutoff, reson
    enum PARS
    {
        PAR_TPOS,
        PAR_DUR,
        PAR_FREQHZ,
        PAR_TONETYPE,
        PAR_VOLUME,
        PAR_ENVSHAPE,
        PAR_HOR_ANGLE,
        PAR_VER_ANGLE,
        PAR_FILT1CUTOFF,
        PAR_FILT1RESON,
        PAR_FILT1EXT0,
        PAR_FILT2CUTOFF,
        PAR_FILT2RESON,
        PAR_FILT2EXT0,
        NUM_PARS
    };
    void start(std::vector<float> &evpars)
    {
        /*
        if (evpars.size() < NUM_PARS)
            throw std::runtime_error(
                std::format("too few event parameters {}, need to have {} parameters",
                            evpars.size(), (int)NUM_PARS));
        */
        active = true;
        /* from ATK toolkit js code
        // W
  matrixNewDSP[0] =  kInvSqrt2;
  // X
  matrixNewDSP[1] =  mCosAzi * mCosEle;
  // Y
  matrixNewDSP[2] = -mSinAzi * mCosEle;
  // Z
  matrixNewDSP[3] =  mSinEle;
  */
        float horz_angle = evpars[PAR_HOR_ANGLE];
        float vert_angle = evpars[PAR_VER_ANGLE];
        ambcoeffs[0] = 1.0 / std::sqrt(2.0);
        ambcoeffs[1] = std::cos(horz_angle) * std::cos(vert_angle);
        ambcoeffs[2] = -std::sin(horz_angle) * std::cos(vert_angle);
        ambcoeffs[3] = std::sin(vert_angle);
        osctype = std::clamp<int>(evpars[PAR_TONETYPE], 0.0, 4.0);
        phase = 0;
        endphase = sr * std::clamp(evpars[PAR_DUR], 0.001f, 1.0f);
        auto hz = std::clamp(evpars[PAR_FREQHZ], 1.0f, 22050.0f);
        osc_sin.setFrequency(hz);
        osc_semisin.setFrequency(hz);
        osc_tri.setFrequency(hz);
        osc_saw.setFrequency(hz);
        osc_pulse.setFrequency(hz);

        cutoff0 = std::clamp(evpars[PAR_FILT1CUTOFF], -60.0f, 65.0f);
        reson0 = std::clamp(evpars[PAR_FILT1RESON], 0.0f, 1.0f);
        graingain = std::clamp(evpars[PAR_VOLUME], 0.0f, 1.0f);
        graingain = graingain * graingain * graingain;
        envshape = std::clamp(evpars[PAR_ENVSHAPE], 0.0f, 1.0f);
    }
    void process(float *outputs, int nframes)
    {
        filter0.makeCoefficients(0, cutoff0, reson0);
        filter0.prepareBlock();
        int envpeakpos = envshape * endphase;
        for (int i = 0; i < nframes; ++i)
        {
            float outsample = 0.0f;
            if (osctype == 0)
                outsample = osc_sin.step();
            else if (osctype == 1)
                outsample = osc_semisin.step();
            else if (osctype == 2)
                outsample = osc_tri.step();
            else if (osctype == 3)
                outsample = osc_saw.step();
            else if (osctype == 4)
                outsample = osc_pulse.step();
            float envgain = 1.0f;
            if (phase < envpeakpos)
                envgain = xenakios::mapvalue<float>(phase, 0.0, envpeakpos, 0.0f, 1.0f);
            else if (phase >= envpeakpos)
                envgain = xenakios::mapvalue<float>(phase, envpeakpos, endphase, 1.0f, 0.0f);
            envgain = 1.0f - envgain;
            envgain = 1.0f - (envgain * envgain * envgain);
            outsample *= envgain * graingain;
            outsample = filter0.processMonoSample(outsample);
            outputs[i * 4 + 0] = outsample * ambcoeffs[0];
            outputs[i * 4 + 1] = outsample * ambcoeffs[1];
            outputs[i * 4 + 2] = outsample * ambcoeffs[2];
            outputs[i * 4 + 3] = outsample * ambcoeffs[3];
            ++phase;
            if (phase >= endphase)
            {
                active = false;
                // std::print("ended voice {}\n", (void *)this);
            }
        }

        filter0.concludeBlock();
    }
};

using events_t = std::vector<std::vector<float>>;

class ToneGranulator
{
  public:
    const int numvoices = 32;
    double playpos = 0.0;
    double m_sr = 44100.0;
    std::vector<std::unique_ptr<GranulatorVoice>> voices;

    events_t events;

    ToneGranulator(double sr, events_t evts, std::string filtertype) : events{evts}
    {
        init_filter_infos();
        std::sort(events.begin(), events.end(),
                  [](auto &lhs, auto &rhs) { return lhs[0] < rhs[0]; });
        for (int i = 0; i < numvoices; ++i)
        {
            auto v = std::make_unique<GranulatorVoice>();
            v->set_samplerate(sr);
            v->set_filter_type(filtertype);
            voices.push_back(std::move(v));
        }
    }
    inline py::array_t<float> generate(std::string outputmode)
    {
        int chans = 0;
        if (outputmode == "mono")
            chans = 1;
        if (outputmode == "stereo")
            chans = 2;
        if (outputmode == "ambisonics")
            chans = 4;
        if (chans == 0)
            throw std::runtime_error("output mode must be mono, stereo or ambisonics");
        int frames = (events.back()[0] + events.back()[1]) * m_sr;
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

        int evindex = 0;
        int framecount = 0;
        while (framecount < frames - granul_block_size)
        {
            std::vector<float> *ev = nullptr;
            if (evindex < events.size())
                ev = &events[evindex];
            while (ev && std::floor((*ev)[0] * m_sr) >= framecount &&
                   std::floor((*ev)[0] * m_sr) < framecount + granul_block_size)
            {
                bool wasfound = false;
                for (int j = 0; j < voices.size(); ++j)
                {
                    if (!voices[j]->active)
                    {
                        // std::print("starting voice {} for event {}\n", j, evindex);
                        voices[j]->start(*ev);
                        wasfound = true;
                        break;
                    }
                }
                if (!wasfound)
                {
                    std::print("Could not find voice for event {}\n", evindex);
                }
                ++evindex;
                if (evindex >= events.size())
                    ev = nullptr;
                else
                    ev = &events[evindex];
            }
            double mixsum[4][granul_block_size];
            for (int i = 0; i < 4; ++i)
                for (int j = 0; j < granul_block_size; ++j)
                    mixsum[i][j] = 0.0f;
            for (int j = 0; j < voices.size(); ++j)
            {
                if (voices[j]->active)
                {
                    float voiceout[4 * granul_block_size];
                    voices[j]->process(voiceout, granul_block_size);
                    for (int k = 0; k < granul_block_size; ++k)
                    {
                        mixsum[0][k] += voiceout[4 * k + 0];
                        mixsum[1][k] += voiceout[4 * k + 1];
                        mixsum[2][k] += voiceout[4 * k + 2];
                        mixsum[3][k] += voiceout[4 * k + 3];
                    }
                }
            }
            double compengain = 8.0 / numvoices;
            if (chans == 4)
            {
                for (int j = 0; j < 4; ++j)
                {
                    for (int k = 0; k < granul_block_size; ++k)
                        writebufs[j][framecount + k] = mixsum[j][k] * compengain;
                }
            }
            else if (chans == 1)
            {
                for (int k = 0; k < granul_block_size; ++k)
                    writebufs[0][framecount + k] = mixsum[0][k] * compengain;
            }
            else if (chans == 2)
            {
                for (int k = 0; k < granul_block_size; ++k)
                {
                    float mid = mixsum[0][k] * compengain;
                    float side = mixsum[1][k] * compengain;
                    writebufs[0][framecount + k] = 0.5 * (mid + side);
                    writebufs[1][framecount + k] = 0.5 * (mid - side);
                }
            }
            framecount += granul_block_size;
        }
        return output_audio;
    }
};

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
    py::class_<ToneGranulator>(m, "ToneGranulator")
        .def(py::init<double, events_t, std::string>())
        .def("generate", &ToneGranulator::generate);
}
