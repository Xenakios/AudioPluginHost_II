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

template <typename T> inline T degreesToRadians(T degrees) { return degrees * (M_PI / 180.0); }

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
    std::variant<sst::basic_blocks::dsp::EBApproxSin<>, sst::basic_blocks::dsp::EBApproxSemiSin<>,
                 sst::basic_blocks::dsp::EBTri<>, sst::basic_blocks::dsp::EBSaw<>,
                 sst::basic_blocks::dsp::EBPulse<>>
        theoscillator;

    alignas(16) std::array<sst::filtersplusplus::Filter, 2> filters;
    int phase = 0;
    int endphase = 0;
    double sr = 0.0;
    bool active = false;

    int prior_osc_type = -1;
    alignas(16) float ambcoeffs[4] = {};
    std::array<double, 2> cutoffs = {0.0, 0.0};
    std::array<double, 2> resons = {0.0, 0.0};
    float graingain = 0.0;
    float auxsend1 = 0.0;
    int envtype = 0;
    double envshape = 0.5;
    GranulatorVoice() {}
    void set_samplerate(double hz) { sr = hz; }
    void set_filter_type(size_t filtindex, const FilterInfo &finfo)
    {
        filters[filtindex].setFilterModel(finfo.model);
        filters[filtindex].setModelConfiguration(finfo.modelconfig);

        filters[filtindex].setSampleRateAndBlockSize(sr, granul_block_size);
        filters[filtindex].setMono();
        if (!filters[filtindex].prepareInstance())
        {
            std::print("could not prepare filter {}\n", filtindex);
        }
    }
    enum PARS
    {
        PAR_TPOS,
        PAR_DUR,
        PAR_FREQHZ,
        PAR_SYNCRATIO,
        PAR_TONETYPE,
        PAR_PULSEWIDTH,
        PAR_VOLUME,
        PAR_ENVTYPE,
        PAR_ENVSHAPE,
        PAR_HOR_ANGLE,
        PAR_VER_ANGLE,
        PAR_FILT1CUTOFF,
        PAR_FILT1RESON,
        PAR_FILT1EXT0,
        PAR_FILT2CUTOFF,
        PAR_FILT2RESON,
        PAR_FILT2EXT0,
        PAR_AUXSEND1,
        NUM_PARS
    };
    /* ambisonics panning code from ATK toolkit js code
        // W
  matrixNewDSP[0] =  kInvSqrt2;
  // X
  matrixNewDSP[1] =  mCosAzi * mCosEle;
  // Y
  matrixNewDSP[2] = -mSinAzi * mCosEle;
  // Z
  matrixNewDSP[3] =  mSinEle;
  */

    void start(std::vector<float> &evpars)
    {
        active = true;
        int newosctype = std::clamp<int>(evpars[PAR_TONETYPE], 0.0, 4.0);
        if (newosctype != prior_osc_type)
        {
            prior_osc_type = newosctype;
            if (newosctype == 0)
                theoscillator = sst::basic_blocks::dsp::EBApproxSin<>();
            else if (newosctype == 1)
                theoscillator = sst::basic_blocks::dsp::EBApproxSemiSin<>();
            else if (newosctype == 2)
                theoscillator = sst::basic_blocks::dsp::EBTri<>();
            else if (newosctype == 3)
                theoscillator = sst::basic_blocks::dsp::EBSaw<>();
            else
                theoscillator = sst::basic_blocks::dsp::EBPulse<>();
            std::visit([this](auto &q) { q.setSampleRate(sr); }, theoscillator);
        }
        auto hz = std::clamp(evpars[PAR_FREQHZ], 1.0f, 22050.0f);
        auto syncratio = std::clamp(evpars[PAR_SYNCRATIO], 1.0f, 16.0f);
        auto pw = evpars[PAR_PULSEWIDTH]; // osc implementation clamps itself to 0..1
        std::visit(
            [hz, syncratio, pw](auto &q) {
                q.reset();
                q.setFrequency(hz);
                q.setSyncRatio(syncratio);
                // setWidth is not available for all osc classes, so...
                if constexpr (std::is_same_v<decltype(q), sst::basic_blocks::dsp::EBPulse<> &>)
                {
                    q.setWidth(pw);
                }
            },
            theoscillator);
        float horz_angle = std::clamp(evpars[PAR_HOR_ANGLE], -180.0f, 180.0f);
        float vert_angle = std::clamp(evpars[PAR_VER_ANGLE], -180.0f, 180.0f);
        horz_angle = degreesToRadians(horz_angle);
        vert_angle = degreesToRadians(vert_angle);
        ambcoeffs[0] = 1.0 / std::sqrt(2.0);
        ambcoeffs[1] = std::cos(horz_angle) * std::cos(vert_angle);
        ambcoeffs[2] = -std::sin(horz_angle) * std::cos(vert_angle);
        ambcoeffs[3] = std::sin(vert_angle);

        phase = 0;
        endphase = sr * std::clamp(evpars[PAR_DUR], 0.001f, 1.0f);

        for (size_t i = 0; i < filters.size(); ++i)
        {
            cutoffs[i] = std::clamp(evpars[PAR_FILT1CUTOFF + 3 * i], -60.0f, 65.0f);
            resons[i] = std::clamp(evpars[PAR_FILT1RESON + 3 * i], 0.0f, 1.0f);
        }

        graingain = std::clamp(evpars[PAR_VOLUME], 0.0f, 1.0f);
        graingain = graingain * graingain * graingain;
        auxsend1 = std::clamp(evpars[PAR_AUXSEND1], 0.0f, 1.0f);

        envtype = std::clamp<int>(evpars[PAR_ENVTYPE], 0, 1);
        envshape = std::clamp(evpars[PAR_ENVSHAPE], 0.0f, 1.0f);
    }
    void process(float *outputs, int nframes)
    {
        for (size_t i = 0; i < filters.size(); ++i)
        {
            filters[i].makeCoefficients(0, cutoffs[i], resons[i]);
            filters[i].prepareBlock();
        }

        int envpeakpos = envshape * endphase;
        envpeakpos = std::clamp(envpeakpos, 16, endphase - 16);
        for (int i = 0; i < nframes; ++i)
        {
            float outsample = 0.0f;
            outsample = std::visit([](auto &q) { return q.step(); }, theoscillator);
            float envgain = 1.0f;
            if (envtype == 0)
            {
                if (phase < envpeakpos)
                {
                    envgain = xenakios::mapvalue<float>(phase, 0.0, envpeakpos, 0.0f, 1.0f);
                    envgain = 1.0f - envgain;
                    envgain = 1.0f - (envgain * envgain * envgain);
                }
                else if (phase >= envpeakpos)
                {
                    envgain = xenakios::mapvalue<float>(phase, envpeakpos, endphase, 1.0f, 0.0f);
                    envgain = envgain * envgain * envgain;
                }
            }
            else if (envtype == 1)
            {
                float envfreq = 1.0f + std::floor(15.0f * envshape);
                envgain =
                    0.5f + 0.5f * std::sin(M_PI * 2 / endphase * phase * envfreq + (1.5f * M_PI));
            }
            // ensure silence if we end up looping past the grain end
            envgain = envgain * (float)active;
            outsample *= envgain * graingain;
            outsample = filters[0].processMonoSample(outsample);
            outsample = filters[1].processMonoSample(outsample);
            float send1 = auxsend1 * outsample;
            outsample = (1.0f - auxsend1) * outsample;
            outputs[i * 5 + 0] = outsample * ambcoeffs[0];
            outputs[i * 5 + 1] = outsample * ambcoeffs[1];
            outputs[i * 5 + 2] = outsample * ambcoeffs[2];
            outputs[i * 5 + 3] = outsample * ambcoeffs[3];
            outputs[i * 5 + 4] = send1;
            ++phase;
            if (phase >= endphase)
            {
                active = false;
                // std::print("ended voice {}\n", (void *)this);
            }
        }
        for (auto &f : filters)
            f.concludeBlock();
    }
};

/*
from atk js code
********************************************************************
Matrix: Generate 2x4 matrix for stereo decoding
********************************************************************
*/
void generateDecodeStereoMatrix(float *aMatrix, float anAngle, float aPattern)
{
    // calculate lG0, lG1, lG2 (scaled by pattern)
    float lG0 = (1.0 - aPattern) * std::sqrt(2.0);
    float lG1 = aPattern * cos(anAngle);
    float lG2 = aPattern * sin(anAngle);

    // Left
    aMatrix[0] = lG0;
    aMatrix[1] = lG1;
    aMatrix[2] = lG2;
    aMatrix[3] = 0.0;

    // Right
    aMatrix[4] = lG0;
    aMatrix[5] = lG1;
    aMatrix[6] = -lG2;
    aMatrix[7] = 0.0;
}

using events_t = std::vector<std::vector<float>>;

class ToneGranulator
{
  public:
    const int numvoices = 32;
    double playpos = 0.0;
    double m_sr = 44100.0;
    std::vector<std::unique_ptr<GranulatorVoice>> voices;

    events_t events;

    ToneGranulator(double sr, events_t evts, std::string filtertype0, std::string filtertype1)
        : events{evts}
    {
        init_filter_infos();
        const FilterInfo *filter0info = nullptr;
        const FilterInfo *filter1info = nullptr;
        for (const auto &finfo : g_filter_infos)
        {
            if (finfo.address == filtertype0)
            {
                filter0info = &finfo;
                std::print("filter 0 is {}\n", finfo.address);
            }
            if (finfo.address == filtertype1)
            {
                filter1info = &finfo;
                std::print("filter 1 is {}\n", finfo.address);
            }
        }
        if (!filter0info)
        {
            throw std::runtime_error(std::format(
                "could not find filter 0 \"{}\", ensure the name is correct", filtertype0));
        }
        if (!filter1info)
        {
            throw std::runtime_error(std::format(
                "could not find filter 1 \"{}\", ensure the name is correct", filtertype1));
        }
        std::sort(events.begin(), events.end(), [](auto &lhs, auto &rhs) {
            return lhs[GranulatorVoice::PAR_TPOS] < rhs[GranulatorVoice::PAR_TPOS];
        });
        for (int i = 0; i < numvoices; ++i)
        {
            auto v = std::make_unique<GranulatorVoice>();
            v->set_samplerate(sr);
            v->set_filter_type(0, *filter0info);
            v->set_filter_type(1, *filter1info);
            voices.push_back(std::move(v));
        }
    }
    inline py::array_t<float> generate(std::string outputmode, double stereoangle,
                                       double stereopattern)
    {
        int chans = 0;
        if (outputmode == "mono")
            chans = 1;
        if (outputmode == "stereo")
            chans = 2;
        if (outputmode == "stereo+aux")
            chans = 3;
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
        alignas(16) float decodeToStereoMatrix[8];
        stereoangle = std::clamp(stereoangle, 0.0, 180.0);
        stereopattern = std::clamp(stereopattern, 0.0, 1.0);
        generateDecodeStereoMatrix(decodeToStereoMatrix, degreesToRadians(stereoangle),
                                   stereopattern);
        int evindex = 0;
        int framecount = 0;
        using clock = std::chrono::system_clock;
        using ms = std::chrono::duration<double, std::milli>;
        const auto start_time = clock::now();
        sst::basic_blocks::dsp::OnePoleLag<float, true> gainlag;
        gainlag.setRateInMilliseconds(1000.0, m_sr, 1.0 / granul_block_size);
        gainlag.setTarget(0.0);
        while (framecount < frames - granul_block_size)
        {
            std::vector<float> *ev = nullptr;
            if (evindex < events.size())
                ev = &events[evindex];
            while (ev && std::floor((*ev)[0] * m_sr) < framecount + granul_block_size)
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
            alignas(16) double mixsum[5][granul_block_size];
            for (int i = 0; i < 5; ++i)
                for (int j = 0; j < granul_block_size; ++j)
                    mixsum[i][j] = 0.0f;
            int numactive = 0;

            for (int j = 0; j < voices.size(); ++j)
            {
                if (voices[j]->active)
                {
                    ++numactive;
                    alignas(16) float voiceout[5 * granul_block_size];
                    float aux0 = 0.0f;
                    voices[j]->process(voiceout, granul_block_size);
                    for (int k = 0; k < granul_block_size; ++k)
                    {
                        mixsum[0][k] += voiceout[5 * k + 0];
                        mixsum[1][k] += voiceout[5 * k + 1];
                        mixsum[2][k] += voiceout[5 * k + 2];
                        mixsum[3][k] += voiceout[5 * k + 3];
                        mixsum[4][k] += voiceout[5 * k + 4];
                    }
                }
            }
            double compengain = 0.0;
            if (numactive > 0)
                compengain = 1.0 / std::sqrt(numactive);
            gainlag.setTarget(compengain);
            if (chans == 4)
            {
                for (int k = 0; k < granul_block_size; ++k)
                {
                    gainlag.process();
                    float gain = gainlag.getValue();
                    for (int j = 0; j < 4; ++j)
                    {
                        writebufs[j][framecount + k] = mixsum[j][k] * gain;
                    }
                }
            }
            else if (chans == 1)
            {
                for (int k = 0; k < granul_block_size; ++k)
                    writebufs[0][framecount + k] = mixsum[0][k] * compengain;
            }
            else if (chans == 2 || chans == 3)
            {
                for (int k = 0; k < granul_block_size; ++k)
                {
                    gainlag.process();
                    float gain = gainlag.getValue();
                    float wIn = mixsum[0][k] * gain;
                    float xIn = mixsum[1][k] * gain;
                    float yIn = mixsum[2][k] * gain;
                    float zIn = mixsum[3][k] * gain;

                    float spl0 = wIn * decodeToStereoMatrix[0] + xIn * decodeToStereoMatrix[1] +
                                 yIn * decodeToStereoMatrix[2] + zIn * decodeToStereoMatrix[3];
                    float spl1 = wIn * decodeToStereoMatrix[4] + xIn * decodeToStereoMatrix[5] +
                                 yIn * decodeToStereoMatrix[6] + zIn * decodeToStereoMatrix[7];
                    writebufs[0][framecount + k] = spl0;
                    writebufs[1][framecount + k] = spl1;
                    if (chans == 3)
                        writebufs[2][framecount + k] = mixsum[4][k] * gain;
                }
            }
            framecount += granul_block_size;
        }
        const ms render_duration = clock::now() - start_time;
        double rtfactor = (frames / m_sr * 1000.0) / render_duration.count();
        std::print("render took {} milliseconds, {:.2f}x realtime\n", render_duration.count(),
                   rtfactor);
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
        .def(py::init<double, events_t, std::string, std::string>())
        .def("generate", &ToneGranulator::generate, "omode"_a, "stereoangle"_a = 30.0,
             "stereopattern"_a = 0.5);
}
