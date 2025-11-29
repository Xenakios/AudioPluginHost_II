#pragma once
#include <vector>
#include "sst/basic-blocks/dsp/CorrelatedNoise.h"
#include "sst/basic-blocks/dsp/EllipticBlepOscillators.h"
#include "sst/basic-blocks/dsp/DPWSawPulseOscillator.h"
#include "sst/filters++.h"
#include <print>
#include "sst/basic-blocks/dsp/SmoothingStrategies.h"
#include "text/choc_StringUtilities.h"
#include <random>
#include <variant>
#include "../Common/xap_breakpoint_envelope.h"
#include "../Common/xen_ambisonics.h"

struct GrainEvent
{
    GrainEvent() = default;
    GrainEvent(double tpos, float dur, float hz, float vol)
        : time_position(tpos), duration(dur), frequency_hz(hz), volume(vol)
    {
    }
    double time_position = 0.0;
    float duration = 0.0f;
    float frequency_hz = 0.0f;
    int generator_type = 0;
    float volume = 0.0f;
    float auxsend = 0.0f;
    int envelope_type = 0;
    float envelope_shape = 0.5f;
    float azimuth = 0.0f;
    float elevation = 0.0f;
    float sync_ratio = 1.0f;
    float pulse_width = 0.5f;
    float fm_frequency_hz = 0.0f;
    float fm_amount = 0.0f;
    float fm_feedback = 0.0f;
    float noisecorr = 0.0f;
    float filter1params[3] = {100.0f, 0.0, 0.0f};
    float filter2params[3] = {100.0f, 0.0, 0.0f};
    float filterfeedback = 0.0f;
};

template <typename T> inline T degreesToRadians(T degrees) { return degrees * (M_PI / 180.0); }

const int granul_block_size = 8;

struct tone_info
{
    int index = -1;
    const char *name = nullptr;
};

tone_info osc_infos[7] = {{0, "EBSINE"},   {1, "EBSEMISINE"}, {2, "EBTRIANGLE"}, {3, "EBSAW"},
                          {4, "EBSQUARE"}, {5, "DPWSAW"},     {6, "DPWPULSE"}};

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
    g_filter_infos.reserve(256);
    auto models = sfpp::Filter::availableModels();
    std::string address;
    address.reserve(256);
    FilterInfo ninfo;
    ninfo.address = "none";
    ninfo.model = sst::filtersplusplus::FilterModel::None;
    ninfo.modelconfig = {};
    g_filter_infos.push_back(ninfo);
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

struct FMOsc
{
    using SmoothingStrategy = sst::basic_blocks::dsp::LagSmoothingStrategy;
    FMOsc()
    {
        SmoothingStrategy::setValueInstant(modIndex, 0.0);
        SmoothingStrategy::setValueInstant(carrierPhaseInc, 0.0);
        SmoothingStrategy::setValueInstant(modulatorPhaseInc, 0.0);
        SmoothingStrategy::setValueInstant(modulatorFeedbackAmount, 0.0);
    }

    void setSampleRate(double hz)
    {
        sampleRate = hz;
        // modulatorPhaseInc.setRateInMilliseconds(250.0, sampleRate, 1.0);
        // carrierPhaseInc.setRateInMilliseconds(500.0, sampleRate, 1.0);
        // carrierPhaseInc = calculatePhaseIncrement(carrierFreq);
        // modulatorPhaseInc = calculatePhaseIncrement(modulatorFreq);
    }
    float step()
    {
        double modulatorOutput = SmoothingStrategy::getValue(modIndex) *
                                 std::sin(modulatorPhase + modulatorFeedbackHistory);
        modulatorFeedbackHistory = modulatorOutput * modulatorFeedbackAmount.getValue();
        double carrierPhaseInput = carrierPhase + modulatorOutput;
        double output = std::sin(carrierPhaseInput);
        carrierPhase += carrierPhaseInc.getValue();
        modulatorPhase += modulatorPhaseInc.getValue();
        if (carrierPhase >= PI_2)
        {
            carrierPhase -= PI_2;
        }
        if (modulatorPhase >= PI_2)
        {
            modulatorPhase -= PI_2;
        }
        SmoothingStrategy::process(modIndex);
        SmoothingStrategy::process(carrierPhaseInc);
        SmoothingStrategy::process(modulatorPhaseInc);
        SmoothingStrategy::process(modulatorFeedbackAmount);
        return output;
    }

    void setFrequency(double freq)
    {
        if (freq > 0.0)
        {
            carrierFreq = freq;
            carrierPhaseInc.setTarget(calculatePhaseIncrement(freq));
        }
    }

    void setModulatorFreq(double freq)
    {
        if (freq > 0.0)
        {
            modulatorFreq = freq;
            modulatorPhaseInc.setTarget(calculatePhaseIncrement(freq));
        }
    }

    void setModIndex(double index)
    {
        if (index >= 0.0)
        {
            SmoothingStrategy::setTarget(modIndex, index);
        }
    }
    void reset()
    {
        carrierPhase = 0.0;
        modulatorPhase = 0.0;
        modulatorFeedbackHistory = 0.0;
        SmoothingStrategy::resetFirstRun(carrierPhaseInc);
        SmoothingStrategy::resetFirstRun(modulatorPhaseInc);
        SmoothingStrategy::resetFirstRun(modIndex);
        SmoothingStrategy::resetFirstRun(modulatorFeedbackAmount);
    }
    void setSyncRatio(double) {}

    void setFeedbackAmount(double amt)
    {
        SmoothingStrategy::setTarget(modulatorFeedbackAmount, std::clamp(amt, -1.0, 1.0));
    }

    static constexpr double PI_2 = 2.0 * M_PI;
    double sampleRate = 0.0;

    double carrierFreq = 440.0;
    double modulatorFreq = 440.0;
    sst::basic_blocks::dsp::LagSmoothingStrategy::smoothValue_t modIndex;

    double carrierPhase = 0.0;
    double modulatorPhase = 0.0;

    sst::basic_blocks::dsp::LagSmoothingStrategy::smoothValue_t modulatorFeedbackAmount;
    double modulatorFeedbackHistory = 0.0;

    sst::basic_blocks::dsp::LagSmoothingStrategy::smoothValue_t carrierPhaseInc;
    sst::basic_blocks::dsp::LagSmoothingStrategy::smoothValue_t modulatorPhaseInc;

    double calculatePhaseIncrement(double freq) const { return (PI_2 * freq) / sampleRate; }
};

class NoiseGen
{
  public:
    using SmoothingStrategy = sst::basic_blocks::dsp::LagSmoothingStrategy;
    NoiseGen()
    {
        SmoothingStrategy::setValueInstant(phaseinc, 0.0);
        SmoothingStrategy::setValueInstant(correlation, 0.0);
    }
    float step()
    {
        phase += phaseinc.getValue();
        if (phase >= 1.0)
        {
            phase -= 1.0;
            heldvalue = sst::basic_blocks::dsp::correlated_noise_o2mk2_supplied_value(
                history[0], history[1], correlation.getValue(), bpdist(rng));
        }
        SmoothingStrategy::process(phaseinc);
        SmoothingStrategy::process(correlation);
        return heldvalue;
    }
    void reset()
    {
        history[0] = 0.0f;
        history[1] = 0.0f;
        phase = 0.0;
        heldvalue = 0.0;
        SmoothingStrategy::resetFirstRun(phaseinc);
        SmoothingStrategy::resetFirstRun(correlation);
    }
    void setFrequency(double freq)
    {
        frequency = freq;
        phaseinc.setTarget(1.0 / sr * freq);
    }
    void setCorrelation(double c) { correlation.setTarget(c); }
    void setSampleRate(double hz)
    {
        sr = hz;
        // correlation.setRateInMilliseconds(100.0, sr, 1.0);
        // phaseinc.setRateInMilliseconds(500.0, sr, 1.0);
    }
    void setRandSeed(unsigned int s) { rng.seed(s); }
    void setSyncRatio(double) {}
    alignas(16) std::minstd_rand0 rng;
    alignas(16) float history[2] = {0.0f, 0.0f};
    alignas(16) std::uniform_real_distribution<float> bpdist{-1.0, 1.0f};
    float heldvalue = 0.0;
    double sr = 0.0;
    double frequency = 1.0;
    alignas(16) double phase = 0.0;
    alignas(16) sst::basic_blocks::dsp::LagSmoothingStrategy::smoothValue_t phaseinc;
    alignas(16) sst::basic_blocks::dsp::LagSmoothingStrategy::smoothValue_t correlation;
};

class GranulatorVoice
{
  public:
    std::variant<NoiseGen, FMOsc, sst::basic_blocks::dsp::EBApproxSin<>,
                 sst::basic_blocks::dsp::EBApproxSemiSin<>, sst::basic_blocks::dsp::EBTri<>,
                 sst::basic_blocks::dsp::EBSaw<>, sst::basic_blocks::dsp::EBPulse<>>
        theoscillator;

    alignas(16) std::array<sst::filtersplusplus::Filter, 2> filters;
    int phase = 0;
    int endphase = 0;
    double sr = 0.0;
    bool active = false;
    float filters_tail = 0.0f;
    int prior_osc_type = -1;
    alignas(16) std::array<float, 16> ambcoeffs;
    enum FilterRouting
    {
        FR_SERIAL,
        FR_PARALLEL
    };
    FilterRouting filter_routing = FR_SERIAL;
    std::array<double, 2> cutoffs = {0.0, 0.0};
    std::array<double, 2> resons = {0.0, 0.0};
    std::array<double, 2> filtextpars = {0.0, 0.0};
    alignas(16) std::array<double, 2> feedbacksignals = {0.0, 0.0};
    float feedbackamt = 0.0f;
    float graingain = 0.0;
    float auxsend1 = 0.0;
    int envtype = 0;
    double envshape = 0.5;
    int grainid = 0;
    int ambisonic_order = 1;
    int num_outputchans = 0;
    std::vector<float> delaylinememory;
    GranulatorVoice()
    {
        delaylinememory.resize(16384);
        std::fill(ambcoeffs.begin(), ambcoeffs.end(), 0.0f);
    }
    void set_samplerate(double hz) { sr = hz; }
    void set_filter_type(size_t filtindex, const FilterInfo &finfo)
    {
        auto reqdelaysize =
            sst::filtersplusplus::Filter::requiredDelayLinesSizes(finfo.model, finfo.modelconfig);
        // std::print("filter {} requires {} samples of delay line\n", finfo.address, reqdelaysize);
        if (reqdelaysize > 0)
            filters_tail = 0.5f;
        if (reqdelaysize > delaylinememory.size())
            delaylinememory.resize(reqdelaysize);
        filters[filtindex].setFilterModel(finfo.model);
        filters[filtindex].setModelConfiguration(finfo.modelconfig);
        filters[filtindex].setSampleRateAndBlockSize(sr, granul_block_size);
        filters[filtindex].setMono();
        filters[filtindex].provideDelayLine(0, delaylinememory.data());
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
        PAR_FILTERFEEDBACKAMOUNT,
        PAR_FILT1CUTOFF,
        PAR_FILT1RESON,
        PAR_FILT1EXT0,
        PAR_FILT2CUTOFF,
        PAR_FILT2RESON,
        PAR_FILT2EXT0,
        PAR_AUXSEND1,
        PAR_FMFREQ,
        PAR_FMAMOUNT,
        PAR_FMFEEDBACK,
        PAR_NOISECORR,
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

    void start(GrainEvent &evpars)
    {
        active = true;
        int newosctype = std::clamp<int>(evpars.generator_type, 0.0, 6.0);
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
            else if (newosctype == 4)
                theoscillator = sst::basic_blocks::dsp::EBPulse<>();
            else if (newosctype == 5)
                theoscillator = FMOsc();
            else if (newosctype == 6)
                theoscillator = NoiseGen();
            std::visit([this](auto &q) { q.setSampleRate(sr); }, theoscillator);
        }
        auto hz = std::clamp(evpars.frequency_hz, 1.0f, 22050.0f);
        auto syncratio = std::clamp(evpars.sync_ratio, 1.0f, 16.0f);
        auto pw = evpars.pulse_width; // osc implementation clamps itself to 0..1
        auto fmhz = evpars.fm_frequency_hz;
        auto fmmodamount = std::clamp(evpars.fm_amount, 0.0f, 1.0f);
        fmmodamount = std::pow(fmmodamount, 3.0f) * 128.0f;
        auto fmfeedback = std::clamp(evpars.fm_feedback, -1.0f, 1.0f);
        auto noisecorr = std::clamp(evpars.noisecorr, -1.0f, 1.0f);
        std::visit(
            [hz, syncratio, pw, fmhz, fmfeedback, fmmodamount, noisecorr, this](auto &q) {
                q.reset();
                q.setFrequency(hz);
                q.setSyncRatio(syncratio);
                // handle extra parameters of osc types
                if constexpr (std::is_same_v<decltype(q), sst::basic_blocks::dsp::EBPulse<> &>)
                {
                    q.setWidth(pw);
                }
                if constexpr (std::is_same_v<decltype(q), FMOsc &>)
                {
                    q.setModulatorFreq(fmhz);
                    q.setModIndex(fmmodamount);
                    q.setFeedbackAmount(fmfeedback);
                }
                if constexpr (std::is_same_v<decltype(q), NoiseGen &>)
                {
                    q.setRandSeed(grainid);
                    q.setCorrelation(noisecorr);
                }
            },
            theoscillator);
        float azi = std::clamp(evpars.azimuth, -180.0f, 180.0f);
        float ele = std::clamp(evpars.elevation, -180.0f, 180.0f);
        azi = degreesToRadians(azi);
        ele = degreesToRadians(ele);

        float x = 0.0;
        float y = 0.0;
        float z = 0.0;
        sphericalToCartesian(azi, ele, x, y, z);
        if (ambisonic_order == 1)
            SHEval1(x, y, z, ambcoeffs.data());
        else if (ambisonic_order == 2)
            SHEval2(x, y, z, ambcoeffs.data());
        else if (ambisonic_order == 3)
            SHEval3(x, y, z, ambcoeffs.data());
        phase = 0;
        float actdur = std::clamp(evpars.duration, 0.001f, 1.0f);
        endphase = sr * actdur;
        float *filparams[2] = {evpars.filter1params, evpars.filter2params};
        for (size_t i = 0; i < filters.size(); ++i)
        {
            filters[i].reset();
            cutoffs[i] = std::clamp(filparams[i][0] - 69.0f, -69.0f, 60.0f);
            resons[i] = std::clamp(filparams[i][1], 0.0f, 1.0f);
            filtextpars[i] = std::clamp(filparams[i][2], -1.0f, 1.0f);
        }

        graingain = std::clamp(evpars.volume, 0.0f, 1.0f);
        graingain = graingain * graingain * graingain;
        auxsend1 = std::clamp(evpars.auxsend, 0.0f, 1.0f);

        envtype = std::clamp<int>(evpars.envelope_type, 0, 1);
        envshape = std::clamp(evpars.envelope_shape, 0.0f, 1.0f);

        feedbackamt = std::clamp(evpars.filterfeedback, -0.9999f, 0.9999f);
        feedbacksignals[0] = 0.0;
        feedbacksignals[1] = 0.0;
    }
    void process(float *outputs, int nframes)
    {
        for (size_t i = 0; i < filters.size(); ++i)
        {
            filters[i].makeCoefficients(0, cutoffs[i], resons[i], filtextpars[i]);
            filters[i].prepareBlock();
        }
        int endphasewithtail = endphase + filters_tail * sr;
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
            envgain = std::clamp(envgain, 0.0f, 1.0f);
            outsample *= envgain * graingain;
            if (filter_routing == FR_SERIAL)
            {
                outsample = filters[0].processMonoSample(outsample + feedbacksignals[0]);
                outsample = filters[1].processMonoSample(outsample);
                feedbacksignals[0] = outsample * feedbackamt;
            }
            else if (filter_routing == FR_PARALLEL)
            {
                float split = outsample;
                split = filters[0].processMonoSample(split + feedbacksignals[0]);
                feedbacksignals[0] = split * feedbackamt;
                outsample = filters[1].processMonoSample(outsample);
                outsample = split + outsample;
            }

            float send1 = auxsend1 * outsample;
            outsample = (1.0f - auxsend1) * outsample;
            for (int chan = 0; chan < num_outputchans; ++chan)
            {
                outputs[i * 16 + chan] = outsample * ambcoeffs[chan];
            }

            ++phase;
            if (phase >= endphasewithtail)
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

using events_t = std::vector<GrainEvent>;

class ToneGranulator
{
  public:
    const int numvoices = 32;
    double m_sr = 0.0;
    int graincount = 0;
    double maingain = 1.0;
    std::vector<std::unique_ptr<GranulatorVoice>> voices;
    events_t events;
    events_t events_to_switch;
    std::atomic<int> thread_op{0};
    alignas(16) float decodeToStereoMatrix[8];
    int evindex = 0;
    int playposframes = 0;
    alignas(16) sst::basic_blocks::dsp::OnePoleLag<float, true> gainlag;

    ToneGranulator(double sr, int filter_routing, std::string filtertype0, std::string filtertype1)
        : m_sr(sr)
    {
        generateDecodeStereoMatrix(decodeToStereoMatrix, 0.0, 0.5);
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

        for (int i = 0; i < numvoices; ++i)
        {
            auto v = std::make_unique<GranulatorVoice>();
            v->set_samplerate(sr);
            v->set_filter_type(0, *filter0info);
            v->set_filter_type(1, *filter1info);
            v->filter_routing = (GranulatorVoice::FilterRouting)filter_routing;

            voices.push_back(std::move(v));
        }
    }
    float m_stereoangle = 0.0f;
    float m_stereopattern = 0.5f;
    int num_out_chans = 0;
    void prepare(events_t evlist, int ambisonics_order)
    {
        if (thread_op == 1)
        {
            std::print("prepare called while audio thread should do state switch!\n");
        }
        events_to_switch = std::move(evlist);
        std::sort(
            events_to_switch.begin(), events_to_switch.end(),
            [](GrainEvent &lhs, GrainEvent &rhs) { return lhs.time_position < rhs.time_position; });
        std::erase_if(events_to_switch, [](GrainEvent &e) {
            return e.time_position < 0.0 || (e.time_position + e.duration) > 60.0;
        });
        std::map<int, int> aotonumchans{{1, 4}, {2, 9}, {3, 16}};
        for (auto &v : voices)
        {
            v->ambisonic_order = ambisonics_order;
            v->num_outputchans = aotonumchans[ambisonics_order];
        }
        num_out_chans = aotonumchans[ambisonics_order];
        thread_op = 1;
    }
    void process_block(float *outputbuffer, int nframes)
    {
        if (thread_op == 1)
        {
            std::swap(events_to_switch, events);

            float stereoangle = std::clamp(m_stereoangle, 0.0f, 180.0f);
            float stereopattern = std::clamp(m_stereopattern, 0.0f, 1.0f);
            generateDecodeStereoMatrix(decodeToStereoMatrix, degreesToRadians(stereoangle),
                                       stereopattern);
            evindex = 0;
            playposframes = 0;
            gainlag.setRateInMilliseconds(1000.0, m_sr, 1.0);
            gainlag.setTarget(0.0);
            thread_op = 0;
        }
        int bufframecount = 0;
        while (bufframecount < nframes)
        {
            GrainEvent *ev = nullptr;
            if (evindex < events.size())
                ev = &events[evindex];
            while (ev && std::floor(ev->time_position * m_sr) < playposframes + granul_block_size)
            {
                bool wasfound = false;
                for (int j = 0; j < voices.size(); ++j)
                {
                    if (!voices[j]->active)
                    {
                        // std::print("starting voice {} for event {}\n", j, evindex);
                        voices[j]->grainid = graincount;
                        voices[j]->start(*ev);
                        wasfound = true;
                        ++graincount;
                        break;
                    }
                }
                if (!wasfound)
                {
                    // ++missedgrains;
                }
                ++evindex;
                if (evindex >= events.size())
                    ev = nullptr;
                else
                    ev = &events[evindex];
            }
            alignas(16) double mixsum[16][granul_block_size];
            for (int i = 0; i < 16; ++i)
            {
                for (int j = 0; j < granul_block_size; ++j)
                {
                    mixsum[i][j] = 0.0f;
                }
            }

            int numactive = 0;

            for (int j = 0; j < voices.size(); ++j)
            {
                if (voices[j]->active)
                {
                    ++numactive;
                    alignas(16) float voiceout[16 * granul_block_size];
                    voices[j]->process(voiceout, granul_block_size);

                    for (int k = 0; k < granul_block_size; ++k)
                    {
                        for (int chan = 0; chan < num_out_chans; ++chan)
                        {
                            mixsum[chan][k] += voiceout[16 * k + chan];
                        }
                    }
                }
            }
            double compengain = 0.0;
            if (numactive > 0)
                compengain = 1.0 / std::sqrt(numactive);
            gainlag.setTarget(compengain);
            for (int k = 0; k < granul_block_size; ++k)
            {
                gainlag.process();
                float gain = gainlag.getValue() * maingain;
                for (int chan = 0; chan < num_out_chans; ++chan)
                {
                    outputbuffer[(bufframecount + k) * num_out_chans + chan] =
                        mixsum[chan][k] * gain;
                }
            }
            /*
            if (chans == 4)
            {
                for (int k = 0; k < granul_block_size; ++k)
                {
                    gainlag.process();
                    float gain = gainlag.getValue() * maingain;
                    outputbuffer[(bufframecount + k) * 4 + 0] = mixsum[0][k] * gain;
                    outputbuffer[(bufframecount + k) * 4 + 1] = mixsum[1][k] * gain;
                    outputbuffer[(bufframecount + k) * 4 + 2] = mixsum[2][k] * gain;
                    outputbuffer[(bufframecount + k) * 4 + 3] = mixsum[3][k] * gain;
                }
            }
            if (chans == 2 || chans == 3)
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
                    outputbuffer[(bufframecount + k) * 2 + 0] = spl0 * maingain;
                    outputbuffer[(bufframecount + k) * 2 + 1] = spl1 * maingain;

                    // if (chans == 3)
                    //     writebufs[2][framecount + k] = mixsum[4][k] * gain;
                }
            }
            */
            bufframecount += granul_block_size;
            playposframes += granul_block_size;
        }
    }
};
