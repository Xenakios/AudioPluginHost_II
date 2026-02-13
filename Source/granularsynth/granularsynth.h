#pragma once
#include <vector>
#include "sst/basic-blocks/dsp/CorrelatedNoise.h"
#include "sst/basic-blocks/dsp/EllipticBlepOscillators.h"
#include <print>
#include "sst/basic-blocks/dsp/SmoothingStrategies.h"
#include "text/choc_StringUtilities.h"
#include "text/choc_Files.h"
#include <random>
#include <variant>
#include "../Common/xap_breakpoint_envelope.h"
#include "../Common/xen_ambisonics.h"
#include "grainfx.h"
#include "sst/basic-blocks/mod-matrix/ModMatrix.h"
#include "sst/basic-blocks/modulators/SimpleLFO.h"
#include "sst/basic-blocks/params/ParamMetadata.h"
#include "containers/choc_SingleReaderSingleWriterFIFO.h"

using namespace sst::basic_blocks::mod_matrix;

const int granul_block_size = 8;

struct GranulatorModConfig
{
    struct SourceIdentifier
    {
        uint32_t src{0};
        bool operator==(const SourceIdentifier &other) const { return src == other.src; }
    };

    struct TargetIdentifier
    {
        int baz{0};
        // uint32_t nm{};
        int16_t depthPosition{-1};

        bool operator==(const TargetIdentifier &other) const
        {
            return baz == other.baz && depthPosition == other.depthPosition;
            // return baz == other.baz && nm == other.nm && depthPosition == other.depthPosition;
        }
    };
    static bool isTargetModMatrixDepth(const TargetIdentifier &t) { return t.depthPosition >= 0; }
    static bool supportsLag(const SourceIdentifier &s) { return true; }
    static size_t getTargetModMatrixElement(const TargetIdentifier &t)
    {
        assert(isTargetModMatrixDepth(t));
        return (size_t)t.depthPosition;
    }

    using RoutingExtraPayload = int;
    struct MyCurve
    {
        int id = 0;
        float par0 = 0.0f;
        bool operator==(const MyCurve &other) { return id == other.id; }
    };
    using CurveIdentifier = MyCurve;
    enum CurveTypes
    {
        CURVE_LINEAR = 1,
        CURVE_SQUARE,
        CURVE_CUBE,
        CURVE_STEPS4,
        CURVE_STEPS5,
        CURVE_STEPS6,
        CURVE_STEPS7,
        CURVE_EXPSIN1,
        CURVE_EXPSIN2,
        CURVE_XOR1,
        CURVE_XOR2,
        CURVE_XOR3,
        CURVE_XOR4,
        CURVE_XOR5,
        CURVE_XOR6,
        CURVE_XOR7,
        CURVE_XOR8,
        CURVE_BITMIRROR
    };
    static float xor_curve(float x, uint16_t a)
    {
        x = std::clamp(x, -1.0f, 1.0f);
        x = (x + 1.0f) * 0.5f;
        uint16_t ival = static_cast<uint16_t>(x * 65535.0f);
        ival = ival ^ a;
        x = ival / 65535.0f;
        return -1.0f + 2.0f * x;
    }
    static uint16_t reverse_bits_16(uint16_t n)
    {
        n = ((n >> 1) & 0x5555) | ((n << 1) & 0xAAAA); // Swap adjacent bits
        n = ((n >> 2) & 0x3333) | ((n << 2) & 0xCCCC); // Swap 2-bit groups
        n = ((n >> 4) & 0x0F0F) | ((n << 4) & 0xF0F0); // Swap nibbles
        n = ((n >> 8) & 0x00FF) | ((n << 8) & 0xFF00); // Swap bytes
        return n;
    }

    static float bit_reversal_curve(float x)
    {
        x = std::clamp(x, -1.0f, 1.0f);
        x = (x + 1.0f) * 0.5f;

        uint16_t ival = static_cast<uint16_t>(x * 65535.0f);
        ival = reverse_bits_16(ival); // Mirror the bits

        x = ival / 65535.0f;
        return -1.0f + 2.0f * x;
    }
    static float expsin(float x, int ampmode, float frequency)
    {
        float norm = (x + 1.0f) * 0.5f;
        float amplitude = 1.0f;
        if (ampmode == 1)
            amplitude = norm * norm;
        else if (ampmode == 2)
            amplitude = norm * norm * norm;
        return amplitude * std::sin(2 * M_PI * norm * frequency);
    }
    static std::function<float(float)> getCurveOperator(CurveIdentifier id)
    {
        switch (id.id)
        {
        case CURVE_LINEAR:
            return [](auto x) { return x; };
        case CURVE_SQUARE:
            return [](auto x) { return x * x; };
        case CURVE_CUBE:
            return [](auto x) { return x * x * x; };
        case CURVE_STEPS4:
            return [id](auto x) { return std::round(x * 4) / 4; };
        case CURVE_STEPS5:
            return [](auto x) { return std::round(x * 5) / 5; };
        case CURVE_STEPS6:
            return [](auto x) { return std::round(x * 6) / 6; };
        case CURVE_STEPS7:
            return [](auto x) { return std::round(x * 7) / 7; };
        case CURVE_EXPSIN1:
            return [](auto x) { return expsin(x, 1, 8.0f); };
        case CURVE_EXPSIN2:
            return [](auto x) { return expsin(x, 2, 12.0f); };
        case CURVE_XOR1:
            return [](auto x) { return xor_curve(x, 13107); };
        case CURVE_XOR2:
            return [](auto x) { return xor_curve(x, 43690); };
        case CURVE_XOR3:
            return [](auto x) { return xor_curve(x, 25027); };
        case CURVE_XOR4:
            return [](auto x) { return xor_curve(x, 255); };
        case CURVE_BITMIRROR:
            return [](auto x) { return bit_reversal_curve(x); };
        }

        return [](auto x) { return x; };
    }

    static constexpr bool IsFixedMatrix{true};
    static constexpr size_t FixedMatrixSize{16};
    static constexpr bool ProvidesNonZeroTargetBases{true};
};

template <> struct std::hash<GranulatorModConfig::MyCurve>
{
    std::size_t operator()(const GranulatorModConfig::MyCurve &c) const noexcept
    {
        auto h1 = std::hash<int>{}((int)c.id);
        return h1;
    }
};

template <> struct std::hash<GranulatorModConfig::SourceIdentifier>
{
    std::size_t operator()(const GranulatorModConfig::SourceIdentifier &s) const noexcept
    {
        auto h1 = std::hash<int>{}((int)s.src);
        return h1;
        // auto h2 = std::hash<int>{}((int)s.index0);
        // auto h3 = std::hash<int>{}((int)s.index1);
        // return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

template <> struct std::hash<GranulatorModConfig::TargetIdentifier>
{
    std::size_t operator()(const GranulatorModConfig::TargetIdentifier &s) const noexcept
    {
        auto h1 = std::hash<int>{}((int)s.baz);
        return h1;
        // auto h2 = std::hash<uint32_t>{}((int)s.nm);

        // return h1 ^ (h2 << 1);
    }
};

class GranulatorModMatrix
{
  public:
    FixedMatrix<GranulatorModConfig> m;
    FixedMatrix<GranulatorModConfig>::RoutingTable rt;
    // std::array<GranulatorModConfig::SourceIdentifier, 32> sourceIds;

    double samplerate = 0.0;

    static constexpr size_t BLOCKSIZE = granul_block_size;
    static constexpr size_t BLOCK_SIZE_OS = BLOCKSIZE * 2;
    static constexpr size_t numLfos = 8;
    using lfo_t = sst::basic_blocks::modulators::SimpleLFO<GranulatorModMatrix, BLOCKSIZE>;
    alignas(32) std::array<std::unique_ptr<lfo_t>, numLfos> m_lfos;
    alignas(32) float table_envrate_linear[512];
    alignas(32) std::array<sst::basic_blocks::dsp::RNG, numLfos> m_rngs;
    GranulatorModMatrix(double sr) : samplerate(sr)
    {
        initTables();
        // sourceIds[0] =
        //     GranulatorModConfig::SourceIdentifier{GranulatorModConfig::SourceIdentifier::NOSOURCE};
        for (size_t i = 0; i < numLfos; ++i)
        {
            auto lfo = std::make_unique<lfo_t>(this);
            m_lfos[i] = std::move(lfo);
            m_lfos[i]->attack(0);
        }
    }
    void init_from_json_file(std::string path)
    {
        try
        {
            auto jsontxt = choc::file::loadFileAsString(path);
            init_from_json(jsontxt);
        }
        catch (std::exception &ex)
        {
            std::print("{}\n", ex.what());
        }
    }
    void init_from_json(std::string json)
    {
        try
        {
            auto ob = choc::json::parseValue(json);
            auto routingarr = ob["routings"];
            if (routingarr.isArray())
            {
                for (int i = 0; i < routingarr.size(); ++i)
                {
                    auto entryob = routingarr[i];
                    int slot = entryob["slot"].get<int>();
                    if (slot >= 0 && slot < GranulatorModConfig::FixedMatrixSize)
                    {
                        int src = entryob["src"].get<int>();
                        int dest = entryob["dest"].get<int>();
                        float d = entryob["depth"].get<float>();
                        // rt.updateRoutingAt(slot, sourceIds[src], targetIds[dest], d);
                    }
                }
            }
            auto lfosarray = ob["lfos"];
            if (lfosarray.isArray())
            {
                for (int i = 0; i < lfosarray.size(); ++i)
                {
                    auto entryob = lfosarray[i];
                    int lfoindex = entryob["index"].get<int>();
                    if (lfoindex >= 0 && lfoindex < numLfos)
                    {
                        float rate = entryob["rate"].getWithDefault(0.0f);
                        // lfo_rates[lfoindex] = rate;
                        float deform = entryob["deform"].getWithDefault(0.0f);
                        // lfo_deforms[lfoindex] = deform;
                        float shift = entryob["deform"].getWithDefault(0.0f);
                        // lfo_shifts[lfoindex] = entryob["shift"].getWithDefault(0.5f);
                        int shape = entryob["shape"].getWithDefault(0);
                        // lfo_shapes[lfoindex] = shape;
                    }
                }
            }
        }
        catch (std::exception &ex)
        {
            std::print("error loading mod matrix json : {}\n", ex.what());
        }
    }
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
};

struct GrainEvent
{
    enum ModDest
    {
        MD_FIL0FREQ,
        MD_FIL0RESO,
        MD_PITCH,
        MD_AZI,
        MD_ELE,
        MD_NUMDESTS
    };
    GrainEvent() = default;
    GrainEvent(double tpos, float dur, float pitch, float vol)
        : time_position(tpos), duration(dur), pitch_semitones(pitch), volume(vol)
    {
        for (int i = 0; i < MD_NUMDESTS; ++i)
            modamounts[i] = 0.0f;
    }
    double time_position = 0.0;
    float duration = 0.0f;
    float pitch_semitones = 0.0f;
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
    float modamounts[MD_NUMDESTS];
};

template <typename T> inline T degreesToRadians(T degrees) { return degrees * (M_PI / 180.0); }

struct tone_info
{
    int index = -1;
    const char *name = nullptr;
};

static tone_info osc_infos[7] = {{0, "EBSINE"},  {1, "EBSEMISINE"}, {2, "EBTRIANGLE"},
                                 {3, "EBSAW"},   {4, "EBSQUARE"},   {5, "DPWSAW"},
                                 {6, "DPWPULSE"}};

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

template <bool TaperEnabled> struct SimpleEnvelope
{
    static constexpr int maxnumsteps = 5;
    std::array<float, maxnumsteps> steps;
    alignas(16) int curstep = 0;
    alignas(16) double steplen = 0.0;
    alignas(16) double phase = 0.0;
    alignas(16) int taper_phase = 0;
    alignas(16) int taper_len = 0;
    SimpleEnvelope()
    {
        steps[0] = 1.0f;
        steps[1] = 0.75f;
        steps[2] = 0.5f;
        steps[3] = 0.25f;
        steps[4] = 0.25;
    }
    void start(int dursamples)
    {
        taper_phase = 0;
        taper_len = dursamples;
        curstep = 0;
        phase = 0.0;
        steplen = (double)dursamples / (maxnumsteps - 1);
    }
    double step()
    {
        double y0 = steps[curstep];
        double y1 = 0.0;
        if (curstep + 1 < maxnumsteps - 1)
        {
            y1 = steps[curstep + 1];
        }
        else
        {
            y1 = steps[maxnumsteps - 1];
        }
        double y2 = y0 + (y1 - y0) / steplen * phase;
        phase += 1.0;
        if (phase >= steplen)
        {
            phase = 0.0;
            ++curstep;
            if (curstep >= maxnumsteps)
                curstep = maxnumsteps - 1;
        }
        if constexpr (TaperEnabled)
        {
            double tapergain = 1.0;
            const int taperfadelen = 32;
            if (taper_phase < taperfadelen)
                tapergain = xenakios::mapvalue<double>(taper_phase, 0, taperfadelen, 0.0, 1.0);
            else if (taper_phase >= taper_len - taperfadelen)
                tapergain = xenakios::mapvalue<double>(taper_phase, taper_len - taperfadelen,
                                                       taper_len, 1.0, 0.0);
            ++taper_phase;
            return y2 * tapergain;
        }
        return y2;
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
    alignas(16) SimpleEnvelope<true> gain_envelope;
    alignas(16) SimpleEnvelope<false> aux_envelope;
    alignas(16) float modamounts[GrainEvent::MD_NUMDESTS];
    float pitch_base = 0.0f;
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
        for (int i = 0; i < GrainEvent::MD_NUMDESTS; ++i)
            modamounts[i] = 0.0f;
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
        pitch_base = std::clamp(evpars.pitch_semitones, -48.0f, 64.0f);
        auto syncratio = std::clamp(evpars.sync_ratio, 1.0f, 16.0f);
        auto pw = evpars.pulse_width; // osc implementation clamps itself to 0..1
        auto fmhz = evpars.fm_frequency_hz;
        auto fmmodamount = std::clamp(evpars.fm_amount, 0.0f, 1.0f);
        fmmodamount = std::pow(fmmodamount, 3.0f) * 128.0f;
        auto fmfeedback = std::clamp(evpars.fm_feedback, -1.0f, 1.0f);
        auto noisecorr = std::clamp(evpars.noisecorr, -1.0f, 1.0f);
        std::visit(
            [syncratio, pw, fmhz, fmfeedback, fmmodamount, noisecorr, this](auto &q) {
                q.reset();
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

        float azi = std::clamp(evpars.azimuth, -360.0f, 360.0f);
        float ele = std::clamp(evpars.elevation, -360.0f, 360.0f);
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
        float actdur = std::clamp(evpars.duration, 0.0f, 1.0f);
        actdur = actdur * actdur * actdur;
        actdur = 0.002f + 0.498f * actdur;
        grain_end_phase = sr * actdur;
        gain_envelope.start(grain_end_phase);
        aux_envelope.start(grain_end_phase);

        for (size_t i = 0; i < filters.size(); ++i)
        {
            filters[i].reset();
            cutoffs[i] = std::clamp(evpars.filterparams[i][0] - 9.0f, -48.0f, 64.0f);
            resons[i] = std::clamp(evpars.filterparams[i][1], 0.0f, 1.0f);
            filtextpars[i] = std::clamp(evpars.filterparams[i][2], -1.0f, 1.0f);
        }
        for (int i = 0; i < GrainEvent::MD_NUMDESTS; ++i)
            modamounts[i] = evpars.modamounts[i];

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
        {
            aux_envelope.step();
        }
        std::visit(
            [this, aux_env_value](auto &q) {
                double finalpitch = pitch_base + aux_env_value * modamounts[GrainEvent::MD_PITCH];
                double hz = 440.0 * std::pow(2.0, 1.0 / 12.0 * (finalpitch - 9.0));
                q.setFrequency(hz);
            },
            theoscillator);
        for (size_t i = 0; i < filters.size(); ++i)
        {
            float cutoffmod = 0.0f;
            if (i == 0)
                cutoffmod = modamounts[GrainEvent::MD_FIL0FREQ] * aux_env_value;
            filters[i].makeCoefficients(0, cutoffs[i] + cutoffmod, resons[i], filtextpars[i]);
            filters[i].prepareBlock();
        }
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
                float envgain = 0.0f;
                if (envtype == 3)
                {
                    envgain = gain_envelope.step();
                }
                else if (envtype == 0 || envtype == 1)
                {
                    if (phase < envpeakpos)
                    {
                        envgain = xenakios::mapvalue<float>(phase, 0.0, envpeakpos, 0.0f, 1.0f);
                        if (envtype == 1)
                        {
                            envgain = 1.0f - envgain;
                            envgain = 1.0f - (envgain * envgain * envgain);
                        }
                    }
                    else if (phase >= envpeakpos)
                    {
                        envgain = xenakios::mapvalue<float>(phase, envpeakpos, grain_end_phase,
                                                            1.0f, 0.0f);
                        if (envtype == 1)
                            envgain = envgain * envgain * envgain;
                    }
                }
                else if (envtype == 2)
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

class StepModSource
{
  public:
    static constexpr size_t maxSteps = 4096;
    int curstep = 0;
    int looppos = 0;
    std::atomic<int> curstepforgui;
    int numactivesteps = 0;
    int loopstartstep = 0;
    int looplen = 1;
    int loopoffset = 0;
    std::atomic<bool> unipolar{false};
    std::vector<float> steps;
    struct Message
    {
        enum Opcode
        {
            OP_NOOP,
            OP_NUMSTEPS,
            OP_LOOPSTART,
            OP_LOOPLEN,
            OP_SETSTEP,
            OP_UNIPOLAR,
            OP_OFFSET
        };
        Opcode opcode = OP_NOOP;
        uint32_t dest = 0;
        float fval0 = 0.0f;
        int ival0 = 0;
    };

    StepModSource() { steps.resize(maxSteps); }

    float next()
    {
        if (steps.empty())
            return 0.0f;
        curstep = loopstartstep + ((looppos + loopoffset) % looplen);
        float result = steps[curstep];
        curstepforgui = curstep;
        looppos = (looppos + 1) % looplen;
        if (unipolar.load())
            result = (result + 1.0f) * 0.5f;
        return result;
    }
};

std::vector<float> generate_from_js(std::string jscode);

inline double calculate_inverse(double y, double a, double b, double d)
{
    // Ensure we don't divide by zero
    if (b == 0)
        return 0;

    double val = (y - a) / b;

    // Clamp to [0, 1] because we know x must be in that
    // range
    if (val < 0)
        val = 0;
    if (val > 1)
        val = 1;

    return pow(val, 1.0 / d);
}

class ToneGranulator
{
  public:
    const int numvoices = 32;
    double m_sr = 0.0;
    int graincount = 0;

    std::vector<std::unique_ptr<GranulatorVoice>> voices;
    events_t events;
    events_t events_to_switch;
    std::atomic<int> thread_op{0};

    int evindex = 0;
    int playposframes = 0;
    int num_out_chans = 0;
    int missedgrains = 0;
    alignas(16) double graingen_phase = 0.0;
    alignas(16) double graingen_phase_prior = 2.0;
    alignas(16) sst::basic_blocks::dsp::OnePoleLag<float, true> gainlag;
    alignas(16) xenakios::Xoroshiro128Plus rng;
    alignas(32) GranulatorModMatrix modmatrix;
    using pmd = sst::basic_blocks::params::ParamMetaData;
    std::vector<pmd> parmetadatas;
    std::vector<float> paramvalues;
    std::unordered_map<uint32_t, float *> idtoparvalptr;
    std::unordered_map<uint32_t, pmd *> idtoparmetadata;
    std::unordered_map<uint32_t, float> modRanges;
    std::unordered_map<int, int> shapeParToActualShape;
    float grain_pitch_mod = 0.0;
    float pulse_width = 0.5;

    choc::fifo::SingleReaderSingleWriterFIFO<StepModSource::Message> fifo;
    int osc_type = 4;
    enum PARAMS
    {
        PAR_MAINVOLUME = 100,
        PAR_AMBORDER = 200,
        PAR_OSCTYPE = 300,
        PAR_DENSITY = 400,
        PAR_PITCH = 500,
        PAR_AZIMUTH = 600,
        PAR_ELEVATION = 700,
        PAR_DURATION = 800,
        PAR_F0CO = 900,
        PAR_F0RE = 1000,
        PAR_F0EX = 1100,
        PAR_F1CO = 1200,
        PAR_F1RE = 1300,
        PAR_F1EX = 1400,
        PAR_FMPITCH = 1500,
        PAR_FMDEPTH = 1600,
        PAR_FMFEEDBACK = 1700,
        PAR_OSC_SYNC = 1800,
        PAR_ENVMORPH = 1900,
        PAR_GRAINVOLUME = 2000,
        PAR_NOISECORRELATION = 2100,
        PAR_AMBIDIFFUSION = 2200,
        PAR_TEST = 2300,
        PAR_LFORATES = 100000,
        PAR_LFODEFORMS = 100100,
        PAR_LFOSHIFTS = 100200,
        PAR_LFOWARPS = 100300,
        PAR_LFOSHAPES = 100400,
        PAR_LFOUNIPOLARS = 100500
    };
    enum SI
    {
        NOSOURCE,
        LFO0,
        LFO1,
        LFO2,
        LFO3,
        LFO4,
        LFO5,
        LFO6,
        LFO7,
        STEPS0,
        STEPS1,
        STEPS2,
        STEPS3,
        STEPS4,
        STEPS5,
        STEPS6,
        STEPS7,
        MIDICCSTART,
        MIDICCEND = MIDICCSTART + 64
    };
    float dummyTargetValue = 0.0f;

    alignas(32) std::array<float, 8> stepModValues;
    alignas(32) std::array<StepModSource, 8> stepModSources;
    struct ModSourceInfo
    {
        std::string name;
        std::string groupname;
        GranulatorModConfig::SourceIdentifier id;
        float val = 0.0f;
    };
    std::vector<ModSourceInfo> modSources;
    std::array<float, 128> modSourceValues;
    std::unordered_map<int, int> midiCCMap;
    alignas(16) std::atomic<int> numVoicesUsed;
    void handleStepSequencerMessages()
    {
        StepModSource::Message msg;
        while (fifo.pop(msg))
        {
            if (msg.dest < stepModSources.size())
            {
                auto &ms = stepModSources[msg.dest];
                if (msg.opcode == StepModSource::Message::OP_UNIPOLAR)
                {
                    ms.unipolar = msg.ival0;
                }
                else if (msg.opcode == StepModSource::Message::OP_OFFSET)
                {
                    ms.loopoffset = msg.ival0;
                }
                else if (msg.opcode == StepModSource::Message::OP_NUMSTEPS)
                    ms.numactivesteps = msg.ival0;
                else if (msg.opcode == StepModSource::Message::OP_LOOPSTART)
                    ms.loopstartstep = msg.ival0;
                else if (msg.opcode == StepModSource::Message::OP_LOOPLEN)
                    ms.looplen = msg.ival0;
                else if (msg.opcode == StepModSource::Message::OP_SETSTEP)
                {
                    if (msg.ival0 >= 0 && msg.ival0 < StepModSource::maxSteps)
                    {
                        ms.steps[msg.ival0] = msg.fval0;
                    }
                }
                if (ms.curstep < ms.loopstartstep || ms.curstep > ms.loopstartstep + ms.looplen)
                {
                    ms.curstep = ms.loopstartstep;
                    ms.curstepforgui = ms.curstep;
                }
            }
        }
    }
    void setStepSequenceStepsA(uint32_t seqIndex, std::vector<float> newsteps, int loopstart,
                               int looplen)
    {
        /*
        if (newsteps.size() > StepModSource::maxSteps)
            return;
        StepModSource::Message msg;
        msg.dest = seqIndex;
        if (newsteps.size() == 0)
        {
            msg.opcode = StepModSource::Message::OP_SETPARAMS;
            msg.numsteps = newsteps.size();
            msg.loopstart = loopstart;
            msg.looplen = looplen;
        }
        if (newsteps.size() > 0)
        {
            msg.opcode = StepModSource::Message::OP_SETSTEPS;
            msg.numsteps = newsteps.size();
            msg.loopstart = loopstart;
            msg.looplen = looplen;
            std::copy(newsteps.begin(), newsteps.end(), msg.steps.begin());
        }
        fifo.push(msg);
        */
    }
    /*
        stepModSources[0].setSteps(generate_from_js(R"(
        function generate_steps()
        {
            arr = [];
            for (var i=0;i<100;++i)
            {
              if (i % 5 == 0 || i % 11 == 0)
                arr.push(1.0);
              else arr.push(-1.0);
            }
            return arr;
        }

    )"));
        */
    ToneGranulator() : m_sr(44100.0), modmatrix(44100.0)
    {
        shapeParToActualShape[0] = GranulatorModMatrix::lfo_t::SINE;
        shapeParToActualShape[1] = GranulatorModMatrix::lfo_t::PULSE;
        shapeParToActualShape[2] = GranulatorModMatrix::lfo_t::SAW_TRI_RAMP;
        shapeParToActualShape[3] = GranulatorModMatrix::lfo_t::SMOOTH_NOISE;
        shapeParToActualShape[4] = GranulatorModMatrix::lfo_t::SH_NOISE;
        for (int i = 0; i < 8; ++i)
        {
            midiCCMap[21 + i] = MIDICCSTART + i;
            midiCCMap[41 + i] = MIDICCSTART + 8 + i;
        }
        fifo.reset(1024);
        for (auto &v : stepModValues)
            v = 0.0f;
        auto sendfunc = [this](uint32_t destss, std::vector<float> values) {
            int i = 0;
            for (auto v : values)
            {
                fifo.push({StepModSource::Message::OP_SETSTEP, destss, v, i});
                ++i;
            }
            fifo.push({StepModSource::Message::OP_NUMSTEPS, destss, 0.0f, StepModSource::maxSteps});
            fifo.push({StepModSource::Message::OP_LOOPSTART, destss, 0.0f, 0});
            fifo.push({StepModSource::Message::OP_LOOPLEN, destss, 0.0f, (int)values.size()});
        };
        sendfunc(0, {-1.0f, 1.0f});
        sendfunc(1, {-1.0f, 0.0f, 1.0f});
        sendfunc(2, {-1.0f, -0.333f, 0.333f, 1.0f});
        sendfunc(3, {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f});

        for (size_t i = 0; i < 4; ++i)
        {
            stepModSources[4 + i].steps.resize(StepModSource::maxSteps);
            stepModSources[4 + i].numactivesteps = StepModSource::maxSteps;
            stepModSources[4 + i].loopstartstep = 0;
            stepModSources[4 + i].looplen = StepModSource::maxSteps;
        }
        for (size_t i = 0; i < StepModSource::maxSteps; ++i)
        {
            stepModSources[4].steps[i] = rng.nextFloatInRange(-1.0f, 1.0f);
            stepModSources[5].steps[i] = rng.nextFloatInRange(-1.0f, 1.0f);
            float z = rng.nextFloat();
            if (z < 0.5f)
                stepModSources[6].steps[i] = -1.0f;
            else
                stepModSources[6].steps[i] = 1.0f;
            z = rng.nextFloat();
            if (z < 0.5f)
                stepModSources[7].steps[i] = -1.0f;
            else
                stepModSources[7].steps[i] = 1.0f;
        }
        parmetadatas.reserve(128);
        parmetadatas.push_back(pmd()
                                   .withRange(-24.0, 0.0)
                                   .withDefault(-6.0)
                                   .withLinearScaleFormatting("dB")
                                   .withName("Main volume")
                                   .withFlags(CLAP_PARAM_IS_MODULATABLE)
                                   .withGroupName("Main output")
                                   .withID(PAR_MAINVOLUME));
        parmetadatas.push_back(pmd()
                                   .withUnorderedMapFormatting({{0, "Stereo"},
                                                                {1, "Ambisonic 1ST Order"},
                                                                {2, "Ambisonic 2ND Order"},
                                                                {3, "Ambisonic 3RD Order"}},
                                                               true)
                                   .withDefault(2)
                                   .withName("Spatialization mode")
                                   .withGroupName("Spatialization")
                                   .withID(PAR_AMBORDER));
        parmetadatas.push_back(pmd()
                                   .withUnorderedMapFormatting({{0, "SINE"},
                                                                {1, "SEMISINE"},
                                                                {2, "TRIANGLE"},
                                                                {3, "SAW"},
                                                                {4, "SQUARE"},
                                                                {5, "FM"},
                                                                {6, "NOISE"}},
                                                               true)
                                   .withDefault(0)
                                   .withName("Oscillator type")
                                   .withGroupName("Oscillator")
                                   .withID(PAR_OSCTYPE));
        parmetadatas.push_back(pmd()
                                   .withRange(0.0f, 7.0f)
                                   .withDefault(4.0)
                                   .withATwoToTheBFormatting(1.0f, 1.0, "Hz")
                                   .withName("Density")
                                   .withGroupName("Time")
                                   .withID(PAR_DENSITY)
                                   .withFlags(CLAP_PARAM_IS_MODULATABLE));
        parmetadatas.push_back(pmd()
                                   .withRange(0.0f, 1.0f)
                                   .withDefault(0.75f)
                                   .asCubicDecibelAttenuation()
                                   .withName("Grain volume")
                                   .withGroupName("Volume")
                                   .withID(PAR_GRAINVOLUME)
                                   .withFlags(CLAP_PARAM_IS_MODULATABLE));
        parmetadatas.push_back(pmd()
                                   .withRange(0.0f, 1.0f)
                                   .withDefault(0.5f)
                                   .withOffsetPowerFormatting("ms", 0.002f, 0.498f, 3.0f, 1000.0f)
                                   .withDecimalPlaces(0)
                                   .withName("Duration")
                                   .withGroupName("Time")
                                   .withID(PAR_DURATION)
                                   .withFlags(CLAP_PARAM_IS_MODULATABLE));
        parmetadatas.push_back(pmd()
                                   .withRange(0.0f, 1.0f)
                                   .withDefault(0.5f)
                                   .withLinearScaleFormatting("%", 100.0f)
                                   .withName("Env Morph")
                                   .withGroupName("Volume")
                                   .withID(PAR_ENVMORPH)
                                   .withFlags(CLAP_PARAM_IS_MODULATABLE));
        parmetadatas.push_back(pmd()
                                   .withRange(-48.0, 48.0)
                                   .withDefault(0.0)
                                   .withLinearScaleFormatting("ST")
                                   .withName("Pitch")
                                   .withGroupName("Oscillator")
                                   .withID(PAR_PITCH)
                                   .withFlags(CLAP_PARAM_IS_MODULATABLE));
        parmetadatas.push_back(pmd()
                                   .withRange(0.0, 4.0)
                                   .withDefault(0.0)
                                   .withLinearScaleFormatting("ST", 12.0f)
                                   .withName("OSC Sync")
                                   .withID(PAR_OSC_SYNC)
                                   .withGroupName("Oscillator")
                                   .withFlags(CLAP_PARAM_IS_MODULATABLE));
        parmetadatas.push_back(pmd()
                                   .withRange(-48.0, 48.0)
                                   .withDefault(0.0)
                                   .withLinearScaleFormatting("ST")
                                   .withName("FM Pitch")
                                   .withID(PAR_FMPITCH)
                                   .withGroupName("Oscillator")
                                   .withFlags(CLAP_PARAM_IS_MODULATABLE));
        parmetadatas.push_back(pmd()
                                   .withRange(0.0, 1.0)
                                   .withDefault(0.0)
                                   .withLinearScaleFormatting("%", 100.0f)
                                   .withName("FM Depth")
                                   .withGroupName("Oscillator")
                                   .withID(PAR_FMDEPTH)
                                   .withFlags(CLAP_PARAM_IS_MODULATABLE));
        parmetadatas.push_back(pmd()
                                   .withRange(-1.0, 1.0)
                                   .withDefault(0.0)
                                   .withLinearScaleFormatting("%", 100.0f)
                                   .withName("FM Feedback")
                                   .withGroupName("Oscillator")
                                   .withID(PAR_FMFEEDBACK)
                                   .withFlags(CLAP_PARAM_IS_MODULATABLE));
        parmetadatas.push_back(pmd()
                                   .withRange(-1.0, 1.0)
                                   .withDefault(0.0)
                                   .withLinearScaleFormatting("%", 100.0f)
                                   .withName("Noise Correlation")
                                   .withGroupName("Oscillator")
                                   .withID(PAR_NOISECORRELATION)
                                   .withFlags(CLAP_PARAM_IS_MODULATABLE));
        parmetadatas.push_back(pmd()
                                   .withRange(-48.0, 64.0)
                                   .withDefault(0.0)
                                   .withLinearScaleFormatting("ST")
                                   .withName("Filter 1 Frequency")
                                   .withID(PAR_F0CO)
                                   .withGroupName("Filter 1")
                                   .withFlags(CLAP_PARAM_IS_MODULATABLE));
        parmetadatas.push_back(pmd()
                                   .withRange(0.0, 1.0)
                                   .withDefault(0.0)
                                   .withLinearScaleFormatting("%", 100.0f)
                                   .withName("Filter 1 Resonance")
                                   .withID(PAR_F0RE)
                                   .withGroupName("Filter 1")
                                   .withFlags(CLAP_PARAM_IS_MODULATABLE));
        parmetadatas.push_back(pmd()
                                   .withRange(-48.0, 64.0)
                                   .withDefault(0.0)
                                   .withLinearScaleFormatting("ST")
                                   .withName("Filter 2 Frequency")
                                   .withID(PAR_F1CO)
                                   .withGroupName("Filter 2")
                                   .withFlags(CLAP_PARAM_IS_MODULATABLE));
        parmetadatas.push_back(pmd()
                                   .withRange(0.0, 1.0)
                                   .withDefault(0.0)
                                   .withLinearScaleFormatting("%", 100.0f)
                                   .withName("Filter 2 Resonance")
                                   .withID(PAR_F1RE)
                                   .withGroupName("Filter 2")
                                   .withFlags(CLAP_PARAM_IS_MODULATABLE));
        parmetadatas.push_back(pmd()
                                   .withRange(-180.0f, 180.0f)
                                   .withDefault(0.0)
                                   .withLinearScaleFormatting("°")
                                   .withName("Azimuth")
                                   .withGroupName("Spatialization")
                                   .withID(PAR_AZIMUTH)
                                   .withFlags(CLAP_PARAM_IS_MODULATABLE));
        parmetadatas.push_back(pmd()
                                   .withRange(-180.0f, 180.0f)
                                   .withDefault(0.0)
                                   .withLinearScaleFormatting("°")
                                   .withName("Elevation")
                                   .withGroupName("Spatialization")
                                   .withID(PAR_ELEVATION)
                                   .withFlags(CLAP_PARAM_IS_MODULATABLE));
        parmetadatas.push_back(pmd()
                                   .withRange(0.0f, 1.0f)
                                   .withDefault(0.0)
                                   .withLinearScaleFormatting("%", 100.0f)
                                   .withName("Ambisonic Diffusion")
                                   .withGroupName("Spatialization")
                                   .withID(PAR_AMBIDIFFUSION)
                                   .withFlags(CLAP_PARAM_IS_MODULATABLE));
        parmetadatas.push_back(pmd()
                                   .withRange(0.0f, 1.0f)
                                   .withDefault(0.0)
                                   .withLinearScaleFormatting("%", 100.0f)
                                   .withName("Error test")
                                   .withGroupName("Spatialization")
                                   .withID(PAR_TEST)
                                   .withFlags(CLAP_PARAM_IS_MODULATABLE));
        for (int i = 0; i < GranulatorModMatrix::numLfos; ++i)
        {
            parmetadatas.push_back(pmd()
                                       .asOnOffBool()
                                       .withID(PAR_LFOUNIPOLARS + i)
                                       .withName(std::format("LFO {} UNIPOLAR", i + 1))
                                       .withGroupName(std::format("LFO {}", i + 1)));
            parmetadatas.push_back(pmd()
                                       .withUnorderedMapFormatting({{0, "SIN"},
                                                                    {1, "SIN->SQR->TRI"},
                                                                    {2, "DOWN->TRI->UP"},
                                                                    {3, "SMOOTH NOISE"},
                                                                    {4, "S&H NOISE"}},
                                                                   true)
                                       .withName(std::format("LFO {} SHAPE", i + 1))
                                       .withGroupName(std::format("LFO {}", i + 1))
                                       .withID(PAR_LFOSHAPES + i));
            parmetadatas.push_back(pmd()
                                       .withRange(-6.0, 5.0)
                                       .withDefault(0.0)
                                       .withDecimalPlaces(3)
                                       .withATwoToTheBFormatting(1.0f, 1.0f, "Hz")
                                       .withName(std::format("LFO {} RATE", i + 1))
                                       .withGroupName(std::format("LFO {}", i + 1))
                                       .withID(PAR_LFORATES + i)
                                       .withFlags(CLAP_PARAM_IS_MODULATABLE));
            parmetadatas.push_back(pmd()
                                       .withRange(-1.0, 1.0)
                                       .withDefault(0.0)
                                       .withLinearScaleFormatting("%", 100.0f)
                                       .withName(std::format("LFO {} DEFORM", i + 1))
                                       .withGroupName(std::format("LFO {}", i + 1))
                                       .withID(PAR_LFODEFORMS + i)
                                       .withFlags(CLAP_PARAM_IS_MODULATABLE));
            parmetadatas.push_back(pmd()
                                       .withRange(-1.0, 1.0)
                                       .withDefault(0.0)
                                       .withLinearScaleFormatting("%", 100.0f)
                                       .withName(std::format("LFO {} SHIFT", i + 1))
                                       .withGroupName(std::format("LFO {}", i + 1))
                                       .withID(PAR_LFOSHIFTS + i)
                                       .withFlags(CLAP_PARAM_IS_MODULATABLE));
            parmetadatas.push_back(pmd()
                                       .withRange(-1.0, 1.0)
                                       .withDefault(0.0)
                                       .withLinearScaleFormatting("%", 100.0f)
                                       .withName(std::format("LFO {} WARP", i + 1))
                                       .withGroupName(std::format("LFO {}", i + 1))
                                       .withID(PAR_LFOWARPS + i)
                                       .withFlags(CLAP_PARAM_IS_MODULATABLE));
        }

        paramvalues.resize(parmetadatas.size());
        for (int i = 0; i < parmetadatas.size(); ++i)
        {
            idtoparmetadata[parmetadatas[i].id] = &parmetadatas[i];
            paramvalues[i] = parmetadatas[i].defaultVal;
            idtoparvalptr[parmetadatas[i].id] = &paramvalues[i];
            if (parmetadatas[i].flags & CLAP_PARAM_IS_MODULATABLE)
            {
                // we might want to have custom ranges too, but these autogenerated ones
                // shuffice for now
                float range = (parmetadatas[i].maxVal - parmetadatas[i].minVal);
                modRanges[parmetadatas[i].id] = range;
            }
        }
        // we want these for stereo testing now...
        // modRanges[PAR_AZIMUTH] = 30.0f;
        // modRanges[PAR_ELEVATION] = 30.0f;
        for (int i = 0; i < numvoices; ++i)
        {
            auto v = std::make_unique<GranulatorVoice>();
            voices.push_back(std::move(v));
        }
        for (size_t i = 0; i < parmetadatas.size(); ++i)
        {
            const auto &md = parmetadatas[i];
            if (md.flags & CLAP_PARAM_IS_MODULATABLE)
            {
                modmatrix.m.bindTargetBaseValue(GranulatorModConfig::TargetIdentifier{(int)md.id},
                                                *idtoparvalptr[md.id]);
            }
        }
        modmatrix.m.bindTargetBaseValue(GranulatorModConfig::TargetIdentifier{(int)1},
                                        dummyTargetValue);

        modSources.emplace_back("Off", "", GranulatorModConfig::SourceIdentifier{0});
        for (uint32_t i = 0; i < GranulatorModMatrix::numLfos; ++i)
        {
            modSources.emplace_back(std::format("LFO {}", i + 1), "LFO",
                                    GranulatorModConfig::SourceIdentifier{i + 1});
        }
        for (uint32_t i = 0; i < 8; ++i)
        {
            modSources.emplace_back(std::format("StepSeq {}", i + 1), "Step Sequencer",
                                    GranulatorModConfig::SourceIdentifier{STEPS0 + i});
        }

        for (uint32_t i = 0; i < 8; ++i)
        {
            modSources.emplace_back(std::format("MIDI CC {}", i + 21), "MIDI",
                                    GranulatorModConfig::SourceIdentifier{i + MIDICCSTART});
        }
        for (uint32_t i = 0; i < 8; ++i)
        {
            modSources.emplace_back(std::format("MIDI CC {}", i + 41), "MIDI",
                                    GranulatorModConfig::SourceIdentifier{i + 8 + MIDICCSTART});
        }
        for (auto &v : modSourceValues)
            v = 0.0f;
        for (uint32_t i = 0; i < modSources.size(); ++i)
        {
            modmatrix.m.bindSourceValue(modSources[i].id, modSourceValues[i]);
        }
        init_filter_infos();
    }
    std::array<sfpp::FilterModel, 2> filtersModels{sfpp::FilterModel(), sfpp::FilterModel()};
    std::array<sfpp::ModelConfig, 2> filtersConfigs{sfpp::ModelConfig(), sfpp::ModelConfig()};
    void set_filter(int which, sfpp::FilterModel mo, sfpp::ModelConfig conf)
    {
        FilterInfo finfo;
        finfo.model = mo;
        finfo.modelconfig = conf;
        filtersModels[which] = mo;
        filtersConfigs[which] = conf;
        for (int i = 0; i < numvoices; ++i)
        {
            auto &v = voices[i];
            // v->set_samplerate(sr);
            v->set_filter_type(which, finfo);
        }
    }
    void initFilterri(double sr, int filter_routing, std::string filttype0, std::string filttype1,
                      float taillen, float tailfadelen)
    {
        const FilterInfo *filter0info = nullptr;
        const FilterInfo *filter1info = nullptr;
        for (const auto &finfo : g_filter_infos)
        {
            if (finfo.address == filttype0)
            {
                filter0info = &finfo;
                std::print("filter 0 is {}\n", finfo.address);
            }
            if (finfo.address == filttype1)
            {
                filter1info = &finfo;
                std::print("filter 1 is {}\n", finfo.address);
            }
        }
        if (!filter0info)
        {
            throw std::runtime_error(std::format(
                "could not find filter 0 \"{}\", ensure the name is correct", filttype0));
        }
        if (!filter1info)
        {
            throw std::runtime_error(std::format(
                "could not find filter 1 \"{}\", ensure the name is correct", filttype1));
        }

        for (int i = 0; i < numvoices; ++i)
        {
            auto &v = voices[i];
            v->set_samplerate(sr);
            v->set_filter_type(0, *filter0info);
            v->set_filter_type(1, *filter1info);
            v->filter_routing = (GranulatorVoice::FilterRouting)filter_routing;
            v->tail_len = std::clamp(taillen, 0.0f, 5.0f);
            v->tail_fade_len = std::clamp(tailfadelen, 0.0f, v->tail_len);
        }
    }
    void set_voice_aux_envelope(std::array<float, SimpleEnvelope<false>::maxnumsteps> env)
    {
        for (auto &v : voices)
        {
            v->aux_envelope.steps = env;
        }
    }
    void set_voice_gain_envelope(std::array<float, SimpleEnvelope<false>::maxnumsteps> env)
    {
        for (auto &v : voices)
        {
            v->gain_envelope.steps = env;
        }
    }
    float next_samplerate = 0.0f;
    int next_filter_routing = -1;
    int next_ambisonics_order = -1;
    std::string next_filt0type;
    std::string next_filt1type;
    float next_tail_len = 0.0f;
    float next_tail_fade_len = 0.0f;
    void prepare(float samplerate, events_t evlist, int ambisonics_order, int filter_routing,
                 float tail_len, float tail_fade_len)
    {
        if (thread_op == 1)
        {
            std::print("prepare called while audio thread should do state switch!\n");
        }
        missedgrains = 0;
        if (evlist.size() > 0)
        {
            events_to_switch = std::move(evlist);
            std::sort(events_to_switch.begin(), events_to_switch.end(),
                      [](GrainEvent &lhs, GrainEvent &rhs) {
                          return lhs.time_position < rhs.time_position;
                      });
            std::erase_if(events_to_switch, [](GrainEvent &e) {
                return e.time_position < 0.0 || (e.time_position + e.duration) > 120.0;
            });
        }
        FilterInfo finfo;
        finfo.model = sfpp::FilterModel::None;
        finfo.modelconfig = sfpp::ModelConfig{};
        for (int i = 0; i < numvoices; ++i)
        {
            auto &v = voices[i];
            v->set_samplerate(samplerate);
            v->filter_routing = (GranulatorVoice::FilterRouting)filter_routing;
            v->tail_len = tail_len;
            v->tail_fade_len = tail_fade_len;
            v->set_filter_type(0, finfo);
            v->set_filter_type(1, finfo);
        }
        next_samplerate = samplerate;
        next_ambisonics_order = ambisonics_order;
        // next_filt0type = filt0type;
        // next_filt1type = filt1type;
        // next_filter_routing = filter_routing;
        // next_tail_len = tail_len;
        // next_tail_fade_len = tail_fade_len;
        //  set_ambisonics_order(ambisonics_order);
        modmatrix.m.prepare(modmatrix.rt, samplerate, granul_block_size);
        thread_op = 1;
    }
    std::map<int, int> aotonumchans{{1, 4}, {2, 9}, {3, 16}};
    std::atomic<bool> is_prepared{false};
    void set_ambisonics_order(int order)
    {
        for (auto &v : voices)
        {
            v->ambisonic_order = order;
            v->num_outputchans = aotonumchans[order];
        }
        num_out_chans = aotonumchans[order];
    }
    void generate_grain()
    {
        double actgrate =
            modmatrix.m.getTargetValue(GranulatorModConfig::TargetIdentifier{PAR_DENSITY});
        actgrate = std::clamp(actgrate, -1.0, 8.0);
        double grate = 1.0 / std::pow(2.0, actgrate);
        for (int i = 0; i < granul_block_size; ++i)
        {
            if (graingen_phase_prior > graingen_phase)
            {
                float pitch =
                    modmatrix.m.getTargetValue(GranulatorModConfig::TargetIdentifier{PAR_PITCH});
                float gdur =
                    modmatrix.m.getTargetValue(GranulatorModConfig::TargetIdentifier{PAR_DURATION});
                float gvol = modmatrix.m.getTargetValue(
                    GranulatorModConfig::TargetIdentifier{PAR_GRAINVOLUME});
                GrainEvent genev{0.0, gdur, pitch, gvol};
                genev.envelope_shape =
                    modmatrix.m.getTargetValue(GranulatorModConfig::TargetIdentifier{PAR_ENVMORPH});
                genev.azimuth =
                    modmatrix.m.getTargetValue(GranulatorModConfig::TargetIdentifier{PAR_AZIMUTH});
                genev.elevation = modmatrix.m.getTargetValue(
                    GranulatorModConfig::TargetIdentifier{PAR_ELEVATION});
                genev.generator_type = osc_type;
                float fm_pitch =
                    modmatrix.m.getTargetValue(GranulatorModConfig::TargetIdentifier{PAR_FMPITCH});
                genev.fm_frequency_hz = 440.0 * std::pow(2.0, 1.0 / 12 * (fm_pitch - 9.0));
                genev.fm_amount =
                    modmatrix.m.getTargetValue(GranulatorModConfig::TargetIdentifier{PAR_FMDEPTH});
                genev.fm_feedback = modmatrix.m.getTargetValue(
                    GranulatorModConfig::TargetIdentifier{PAR_FMFEEDBACK});
                genev.pulse_width = pulse_width;
                genev.noisecorr = modmatrix.m.getTargetValue(
                    GranulatorModConfig::TargetIdentifier{PAR_NOISECORRELATION});
                genev.sync_ratio =
                    std::pow(2.0, modmatrix.m.getTargetValue(
                                      GranulatorModConfig::TargetIdentifier{PAR_OSC_SYNC}));
                genev.filterparams[0][0] =
                    modmatrix.m.getTargetValue(GranulatorModConfig::TargetIdentifier{PAR_F0CO});
                genev.filterparams[0][1] =
                    modmatrix.m.getTargetValue(GranulatorModConfig::TargetIdentifier{PAR_F0RE});
                genev.filterparams[1][0] =
                    modmatrix.m.getTargetValue(GranulatorModConfig::TargetIdentifier{PAR_F1CO});
                genev.filterparams[1][1] =
                    modmatrix.m.getTargetValue(GranulatorModConfig::TargetIdentifier{PAR_F1RE});
                genev.modamounts[GrainEvent::MD_PITCH] = grain_pitch_mod;
                bool wasfound = false;
                for (int j = 0; j < voices.size(); ++j)
                {
                    if (!voices[j]->active)
                    {
                        // std::print("starting voice {} alternating value {}\n", j,
                        // alternatingValue);
                        for (size_t sm = 0; sm < stepModSources.size(); ++sm)
                            stepModValues[sm] = stepModSources[sm].next();
                        voices[j]->grainid = graincount;
                        voices[j]->start(genev);
                        float ambdif = *idtoparvalptr[PAR_AMBIDIFFUSION];
                        if (ambdif > 0.0f)
                        {
                            ambdif *= 0.1f;
                            for (size_t coeff = 4; coeff < 16; ++coeff)
                            {
                                float diffamount = rng.nextHypCos(0.0f, ambdif);
                                voices[j]->ambcoeffs[coeff] += diffamount;
                            }
                        }
                        wasfound = true;
                        ++graincount;
                        break;
                    }
                }
                if (!wasfound)
                {
                    ++missedgrains;
                }
            }
            graingen_phase_prior = graingen_phase;
            graingen_phase += 1.0 / m_sr / grate;
            if (graingen_phase >= 1.0)
                graingen_phase -= 1.0;
        }
    }
    void process_block(float *outputbuffer, int nframes)
    {
        if (thread_op == 1)
        {
            std::swap(events_to_switch, events);
            evindex = 0;
            playposframes = 0;
            m_sr = next_samplerate;
            set_ambisonics_order(next_ambisonics_order);
            // initFilter(m_sr, next_filter_routing, next_filt0type, next_filt1type,
            // next_tail_len,
            //            next_tail_fade_len);
            gainlag.setRateInMilliseconds(1000.0, m_sr, 1.0);
            gainlag.setTarget(0.0);
            graingen_phase = 0.0;
            graingen_phase_prior = 2.0;
            thread_op = 0;
        }
        handleStepSequencerMessages();
        bool self_generate = false;
        if (events.size() == 0)
            self_generate = true;
        int bufframecount = 0;
        while (bufframecount < nframes)
        {
            for (uint32_t i = 0; i < modmatrix.numLfos; ++i)
            {
                float shift = modmatrix.m.getTargetValue(
                    GranulatorModConfig::TargetIdentifier{(int)(PAR_LFOSHIFTS + i)});
                modmatrix.m_lfos[i]->applyPhaseOffset(shift);
                float rate = modmatrix.m.getTargetValue(
                    GranulatorModConfig::TargetIdentifier{(int)(PAR_LFORATES + i)});
                float deform = modmatrix.m.getTargetValue(
                    GranulatorModConfig::TargetIdentifier{(int)(PAR_LFODEFORMS + i)});
                float warp = modmatrix.m.getTargetValue(
                    GranulatorModConfig::TargetIdentifier{(int)(PAR_LFOWARPS + i)});
                int shape = *idtoparvalptr[PAR_LFOSHAPES + i];
                shape = shapeParToActualShape[shape];
                modmatrix.m_lfos[i]->process_block(rate, deform, shape, false, 1.0f, warp);
                bool unipolar = (*idtoparvalptr[PAR_LFOUNIPOLARS + i]) > 0.5f;
                if (!unipolar)
                    modSourceValues[LFO0 + i] = modmatrix.m_lfos[i]->outputBlock[0];
                else
                    modSourceValues[LFO0 + i] = (modmatrix.m_lfos[i]->outputBlock[0] + 1.0f) * 0.5f;
            }
            for (size_t i = 0; i < stepModSources.size(); ++i)
                modSourceValues[STEPS0 + i] = stepModValues[i];
            modmatrix.m.process();
            if (!self_generate)
            {
                GrainEvent *ev = nullptr;
                if (evindex < events.size())
                    ev = &events[evindex];
                while (ev &&
                       std::floor(ev->time_position * m_sr) < playposframes + granul_block_size)
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
            }
            else
            {
                generate_grain();
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
            float maingain =
                modmatrix.m.getTargetValue(GranulatorModConfig::TargetIdentifier{PAR_MAINVOLUME});
            maingain = std::clamp(maingain, -96.0f, 0.0f);
            maingain = xenakios::decibelsToGain(maingain);
            gainlag.setTarget(compengain * maingain);
            for (int k = 0; k < granul_block_size; ++k)
            {
                gainlag.process();
                float gain = gainlag.getValue();
                for (int chan = 0; chan < num_out_chans; ++chan)
                {
                    outputbuffer[(bufframecount + k) * num_out_chans + chan] =
                        mixsum[chan][k] * gain;
                }
            }
            bufframecount += granul_block_size;
            playposframes += granul_block_size;
        }
        int voicesused = 0;
        for (auto &v : voices)
        {
            if (v->active)
                ++voicesused;
        }
        numVoicesUsed = voicesused;
    }
};
