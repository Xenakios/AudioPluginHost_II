#pragma once
#include <vector>
#include "sst/basic-blocks/dsp/CorrelatedNoise.h"
#include "sst/basic-blocks/dsp/EllipticBlepOscillators.h"
#include <print>
#include "sst/basic-blocks/dsp/SmoothingStrategies.h"
#include "text/choc_StringUtilities.h"
#include <random>
#include <variant>
#include "../Common/xap_breakpoint_envelope.h"
#include "../Common/xen_ambisonics.h"
#include "grainfx.h"

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
    float filterparams[2][3] = {{100.0f, 0.0, 0.0f}, {100.0f, 0.0, 0.0f}};
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

struct SimpleEnvelope
{
    double curvalue = 0.0;
    double increment = 0.0;
    void start(double startvalue, double endvalue, int dursamples)
    {
        curvalue = startvalue;
        increment = (endvalue - startvalue) / dursamples;
    }
    double step()
    {
        double result = curvalue;
        curvalue += increment;
        return result;
    }
};

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
    int grain_end_phase = 0;
    double sr = 0.0;
    bool active = false;
    float tail_len = 0.005;
    float tail_fade_len = 0.005;
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
    alignas(16) SimpleEnvelope aux_envelope;
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
        // actdur = std::pow(actdur, 2.0);
        // actdur = 0.001 + 0.999 * actdur;
        grain_end_phase = sr * actdur;
        aux_envelope.start(1.0, -1.0, grain_end_phase);

        for (size_t i = 0; i < filters.size(); ++i)
        {
            filters[i].reset();
            cutoffs[i] = std::clamp(evpars.filterparams[i][0] - 69.0f, -69.0f, 60.0f);
            resons[i] = std::clamp(evpars.filterparams[i][1], 0.0f, 1.0f);
            filtextpars[i] = std::clamp(evpars.filterparams[i][2], -1.0f, 1.0f);
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
        float aux_env_value = aux_envelope.step();
        for (int i = 1; i < nframes; ++i)
            aux_envelope.step();
        for (size_t i = 0; i < filters.size(); ++i)
        {
            float cutoffmod = 0.0f;
            if (i == 0)
                cutoffmod = 12.0 * aux_env_value;
            filters[i].makeCoefficients(0, cutoffs[i] + cutoffmod, resons[i], filtextpars[i]);
            filters[i].prepareBlock();
        }
        // int endphasewithtail = endphase + filters_tail * sr;
        int envpeakpos = envshape * grain_end_phase;
        envpeakpos = std::clamp(envpeakpos, 16, grain_end_phase - 16);

        int tail_len_samples = tail_len * sr;
        int tail_fade_samples = tail_fade_len * sr;
        int tail_fade_start = grain_end_phase + tail_len_samples - tail_fade_samples;
        int tail_fade_end = grain_end_phase + tail_len_samples;
        for (int i = 0; i < nframes; ++i)
        {
            float outsample = 0.0f;
            if (phase < grain_end_phase)
            {
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
                        envgain = xenakios::mapvalue<float>(phase, envpeakpos, grain_end_phase,
                                                            1.0f, 0.0f);
                        envgain = envgain * envgain * envgain;
                    }
                }
                else if (envtype == 1)
                {
                    float envfreq = 1.0f + std::floor(15.0f * envshape);
                    envgain = 0.5f + 0.5f * std::sin(M_PI * 2 / grain_end_phase * phase * envfreq +
                                                     (1.5f * M_PI));
                }
                envgain = std::clamp(envgain, 0.0f, 1.0f);
                outsample *= envgain * graingain;
            }
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

            ++phase;
            float fadegain = 1.0f;
            if (phase >= grain_end_phase)
            {
                if (phase >= tail_fade_end)
                {
                    active = false;
                    fadegain = 0.0f;
                }
                else if (phase >= tail_fade_start)
                {
                    fadegain = xenakios::mapvalue<float>(phase, tail_fade_start, tail_fade_end,
                                                         1.0f, 0.0f);
                    if (fadegain < 0.0f)
                        fadegain = 0.0f;
                }
            }
            for (int chan = 0; chan < num_outputchans; ++chan)
            {
                outputs[i * 16 + chan] = outsample * ambcoeffs[chan] * fadegain;
            }
        }
        for (auto &f : filters)
            f.concludeBlock();
    }
};

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

    int evindex = 0;
    int playposframes = 0;
    alignas(16) sst::basic_blocks::dsp::OnePoleLag<float, true> gainlag;

    ToneGranulator(double sr, int filter_routing, std::string filtertype0, std::string filtertype1,
                   float tail_len, float tail_fade_len)
        : m_sr(sr)
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

        for (int i = 0; i < numvoices; ++i)
        {
            auto v = std::make_unique<GranulatorVoice>();
            v->set_samplerate(sr);
            v->set_filter_type(0, *filter0info);
            v->set_filter_type(1, *filter1info);
            v->filter_routing = (GranulatorVoice::FilterRouting)filter_routing;
            v->tail_len = std::clamp(tail_len, 0.0f, 5.0f);
            v->tail_fade_len = std::clamp(tail_fade_len, 0.0f, v->tail_len);
            voices.push_back(std::move(v));
        }
    }

    int num_out_chans = 0;
    int missedgrains = 0;
    void prepare(events_t evlist, int ambisonics_order)
    {
        if (thread_op == 1)
        {
            std::print("prepare called while audio thread should do state switch!\n");
        }
        missedgrains = 0;
        events_to_switch = std::move(evlist);
        std::sort(
            events_to_switch.begin(), events_to_switch.end(),
            [](GrainEvent &lhs, GrainEvent &rhs) { return lhs.time_position < rhs.time_position; });
        std::erase_if(events_to_switch, [](GrainEvent &e) {
            return e.time_position < 0.0 || (e.time_position + e.duration) > 120.0;
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
                    ++missedgrains;
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
            bufframecount += granul_block_size;
            playposframes += granul_block_size;
        }
    }
};
