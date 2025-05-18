#pragma once

#include "sst/basic-blocks/modulators/SimpleLFO.h"
#include "sst/basic-blocks/mod-matrix/ModMatrix.h"
#include "xap_utils.h"
#include "clap_eventsequence.h"
#include "xap_breakpoint_envelope.h"
#include <stdexcept>
#include "text/choc_Files.h"
#include "text/choc_JSON.h"

template <size_t BLOCK_SIZE> class SimpleLFO
{
  public:
    static constexpr size_t BLOCK_SIZE_OS = BLOCK_SIZE * 2;
    double samplerate = 44100;
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
    sst::basic_blocks::modulators::SimpleLFO<SimpleLFO, BLOCK_SIZE> m_lfo;
    double rate = 0.0;
    double deform = 0.0;
    int shape = 0;
    SimpleLFO(double sr) : samplerate(sr), m_lfo(this) { initTables(); }
};

inline xenakios::Envelope generateEnvelopeFromLFO(double rate, double deform, int shape,
                                                  double envlen, double envgranul)
{
    double sr = 44100.0;
    constexpr size_t blocklen = 64;
    xenakios::Envelope result;
    SimpleLFO<blocklen> lfo{sr};
    int outpos = 0;
    int outlen = sr * envlen;
    int granlen = envgranul * sr;
    int granpos = 0;
    while (outpos < outlen)
    {
        lfo.m_lfo.process_block(rate, deform, shape, false);
        granpos += blocklen;
        if (granpos >= granlen)
        {
            granpos = 0;
            result.addPoint({outpos / sr, lfo.m_lfo.outputBlock[0]});
        }
        outpos += blocklen;
    }
    return result;
}

using namespace sst::basic_blocks::mod_matrix;

struct Config
{
    struct SourceIdentifier
    {
        enum SI
        {
            LFO1,
            LFO2,
            LFO3,
            LFO4,
            BKENV1,
            BKENV2,
            BKENV3,
            BKENV4
        } src{LFO1};
        int index0{0};
        int index1{0};

        bool operator==(const SourceIdentifier &other) const
        {
            return src == other.src && index0 == other.index0 && index1 == other.index1;
        }
    };

    struct TargetIdentifier
    {
        int baz{0};
        uint32_t nm{};
        int16_t depthPosition{-1};

        bool operator==(const TargetIdentifier &other) const
        {
            return baz == other.baz && nm == other.nm && depthPosition == other.depthPosition;
        }
    };

    using CurveIdentifier = int;

    static bool isTargetModMatrixDepth(const TargetIdentifier &t) { return t.depthPosition >= 0; }
    static size_t getTargetModMatrixElement(const TargetIdentifier &t)
    {
        assert(isTargetModMatrixDepth(t));
        return (size_t)t.depthPosition;
    }

    using RoutingExtraPayload = int;

    static constexpr bool IsFixedMatrix{true};
    static constexpr size_t FixedMatrixSize{16};
};

template <> struct std::hash<Config::SourceIdentifier>
{
    std::size_t operator()(const Config::SourceIdentifier &s) const noexcept
    {
        auto h1 = std::hash<int>{}((int)s.src);
        auto h2 = std::hash<int>{}((int)s.index0);
        auto h3 = std::hash<int>{}((int)s.index1);
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

template <> struct std::hash<Config::TargetIdentifier>
{
    std::size_t operator()(const Config::TargetIdentifier &s) const noexcept
    {
        auto h1 = std::hash<int>{}((int)s.baz);
        auto h2 = std::hash<uint32_t>{}((int)s.nm);

        return h1 ^ (h2 << 1);
    }
};

class MultiModulator
{
  public:
    MultiModulator(double sampleRate) : sr(sampleRate)
    {
        sources[0] = {Config::SourceIdentifier::SI::LFO1};
        sources[1] = {Config::SourceIdentifier::SI::LFO2};
        sources[2] = {Config::SourceIdentifier::SI::LFO3};
        sources[3] = {Config::SourceIdentifier::SI::LFO4};
        sources[4] = {Config::SourceIdentifier::SI::BKENV1};
        sources[5] = {Config::SourceIdentifier::SI::BKENV2};
        sources[6] = {Config::SourceIdentifier::SI::BKENV3};
        sources[7] = {Config::SourceIdentifier::SI::BKENV4};
        for (size_t i = 0; i < targets.size(); ++i)
        {
            targets[i] = Config::TargetIdentifier{(int)i};
        }
        targets[5] = Config::TargetIdentifier{5, 0, 0};
        targets[6] = Config::TargetIdentifier{6, 0, 1};
        std::fill(sourceValues.begin(), sourceValues.end(), 0.0f);
        for (size_t i = 0; i < sourceValues.size(); ++i)
        {
            m.bindSourceValue(sources[i], sourceValues[i]);
        }
        std::fill(targetValues.begin(), targetValues.end(), 0.0f);
        for (size_t i = 0; i < targetValues.size(); ++i)
        {
            m.bindTargetBaseValue(targets[i], targetValues[i]);
        }
        for (auto &l : lfos)
        {
            l.samplerate = sampleRate;
            l.initTables();
        }
        for (auto &e : outputprops)
            e = OutputProps();
    }

    void applyToSequence(ClapEventSequence &destSeq, double startTime, double duration)
    {
        m.prepare(rt, sr, blocklen);
        double tpos = startTime;
        double gran = (blocklen / sr);
        while (tpos < startTime + duration)
        {
            size_t i = 0;
            for (auto &lfo : lfos)
            {
                lfo.m_lfo.process_block(lfo.rate, lfo.deform, lfo.shape, false);
                sourceValues[i] = lfo.m_lfo.outputBlock[0];
                ++i;
            }
            for (auto &env : envs)
            {
                if (env.getNumPoints() > 0)
                {
                    env.processBlock(tpos, sr, 2, 1);
                    sourceValues[i] = env.outputBlock[0];
                }
                ++i;
            }
            m.process();
            for (i = 0; i < outputprops.size(); ++i)
            {
                double dv = m.getTargetValue(targets[i]);
                const auto &oprop = outputprops[i];
                if (oprop.type == CLAP_EVENT_PARAM_VALUE)
                {
                    std::cout << "should not be\n";
                    dv = xenakios::mapvalue<double>(dv, -1.0, 1.0, oprop.minval, oprop.maxval);
                    dv = std::clamp<double>(dv, oprop.minval, oprop.maxval);
                    destSeq.addParameterEvent(false, tpos, -1, -1, -1, -1, oprop.paramid, dv);
                }
                if (oprop.type == CLAP_EVENT_PARAM_MOD)
                {
                    dv *= oprop.mod_depth;
                    destSeq.addParameterEvent(true, tpos, -1, -1, -1, -1, oprop.paramid, dv);
                }
                if (oprop.type == CLAP_EVENT_NOTE_EXPRESSION)
                {
                    destSeq.addNoteExpression(tpos, oprop.port, oprop.channel, oprop.key,
                                              oprop.note_id, oprop.expid, dv);
                }
            }
            tpos += gran;
        }
    }
    void setOutputAsParameter(size_t index, clap_id parid, double minval, double maxval)
    {
        outputprops[index].paramid = parid;
        outputprops[index].type = CLAP_EVENT_PARAM_VALUE;
        outputprops[index].minval = minval;
        outputprops[index].maxval = maxval;
    }
    void setOutputAsParameterModulation(size_t index, clap_id parid, double depth)
    {
        outputprops[index].paramid = parid;
        outputprops[index].type = CLAP_EVENT_PARAM_MOD;
        outputprops[index].mod_depth = depth;
    }
    void setOutputAsNoteExpression(size_t index, int net, int port, int channel, int key,
                                   int note_id)
    {
        outputprops[index].expid = net;
        outputprops[index].port = port;
        outputprops[index].channel = channel;
        outputprops[index].key = key;
        outputprops[index].note_id = note_id;
        outputprops[index].type = CLAP_EVENT_NOTE_EXPRESSION;
    }
    void setConnection(size_t slotIndex, size_t sourceIndex, size_t targetIndex, double depth)
    {
        rt.updateRoutingAt(slotIndex, sources[sourceIndex], targets[targetIndex], depth);
    }
    void setLFOProps(size_t index, double rate, double deform, int shape)
    {
        if (index >= lfos.size())
            throw std::runtime_error("LFO index must be 0..3");
        lfos[index].rate = rate;
        lfos[index].deform = deform;
        lfos[index].shape = shape;
    }
    double sr = 44100.0;
    FixedMatrix<Config> m;
    FixedMatrix<Config>::RoutingTable rt;
    std::array<Config::SourceIdentifier, 8> sources;
    std::array<Config::TargetIdentifier, 16> targets;
    std::array<float, 8> sourceValues;
    std::array<float, 16> targetValues;
    static constexpr size_t blocklen = 64;
    std::array<SimpleLFO<blocklen>, 4> lfos{44100, 44100, 44100, 44100};
    std::array<xenakios::Envelope, 4> envs;
    struct OutputProps
    {
        int type = -1;
        clap_id paramid = CLAP_INVALID_ID;
        clap_note_expression expid = -1;
        int port = -1;
        int channel = -1;
        int key = -1;
        int note_id = -1;
        double minval = 0.0;
        double maxval = 1.0;
        double mod_depth = 0.0;
    };
    std::array<OutputProps, 8> outputprops;
};

class AltMultiModulator
{
  public:
    enum ModSource
    {
        MS_LFO0,
        MS_LFO1,
        MS_LFO2,
        MS_LFO3
    };
    enum ModDest
    {
        MD_Output0,
        MD_Output1,
        MD_Output2,
        MD_Output3,
        MD_LFO0Rate,
        MD_LFO1Rate,
        MD_LFO2Rate,
        MD_LFO3Rate,
        MD_LFO0Amount,
        MD_LFO1Amount,
        MD_LFO2Amount,
        MD_LFO3Amount
    };

    static constexpr size_t BLOCKSIZE = 64;
    static constexpr size_t BLOCK_SIZE_OS = BLOCKSIZE * 2;
    using lfo_t = sst::basic_blocks::modulators::SimpleLFO<AltMultiModulator, BLOCKSIZE>;
    static constexpr size_t numLfos = 4;
    static constexpr size_t numModulationSources = 4;
    static constexpr size_t numModulationDestinations = 12;
    static constexpr size_t numOutputs = 4;
    alignas(32) float mod_matrix[numModulationSources][numModulationDestinations];
    alignas(32) std::array<double, numOutputs> output_values;
    std::array<int, numLfos> lfo_shapes;
    std::array<double, numLfos> lfo_amounts;
    std::array<double, numLfos> lfo_amt_mods;
    std::array<double, numLfos> lfo_rates;
    std::array<double, numLfos> lfo_rate_mods;
    std::array<double, numLfos> lfo_deforms;
    std::array<double, numLfos> lfo_shifts;
    std::array<double, numLfos> lfo_unipolars;
    std::array<uint32_t, numLfos> lfo_randseeds;
    AltMultiModulator(double sr) : samplerate(sr)
    {
        initTables();
        for (size_t i = 0; i < numLfos; ++i)
        {
            lfo_randseeds[i] = 10 + 107 * i;
            m_rngs[i].reseed(lfo_randseeds[i]);
            m_lfos[i] = std::make_unique<lfo_t>(this, m_rngs[i]);
            lfo_amounts[i] = 1.0;
            lfo_amt_mods[i] = 0.0;
            lfo_shapes[i] = lfo_t::Shape::SINE;
            lfo_rates[i] = 0.0;
            lfo_rate_mods[i] = 0.0;
            lfo_deforms[i] = 0.0;
            lfo_shifts[i] = 0.0;
            lfo_unipolars[i] = 0.0;

            m_lfos[i]->attack(lfo_shapes[i]);
        }
        for (int i = 0; i < numModulationSources; ++i)
        {
            for (int j = 0; j < numModulationDestinations; ++j)
            {
                mod_matrix[i][j] = 0.0f;
            }
        }
        for (int i = 0; i < numOutputs; ++i)
        {
            output_values[i] = 0.0;
        }
    }
    std::array<double, numModulationDestinations> modmixes;
    void process_block()
    {
        for (size_t i = 0; i < numLfos; ++i)
        {
            m_lfos[i]->applyPhaseOffset(lfo_shifts[i]);
            m_lfos[i]->process_block(lfo_rates[i] + lfo_rate_mods[i], lfo_deforms[i],
                                     lfo_shapes[i]);
            if (lfo_unipolars[i] >= 0.5)
                m_lfos[i]->outputBlock[0] = 0.5f + 0.5f * m_lfos[i]->outputBlock[0];
        }

        for (size_t i = 0; i < numModulationDestinations; ++i)
        {
            modmixes[i] = 0.0f;
        }
        modmixes[MD_LFO0Amount] = 1.0f;
        modmixes[MD_LFO1Amount] = 1.0f;
        modmixes[MD_LFO2Amount] = 1.0f;
        modmixes[MD_LFO3Amount] = 1.0f;
        for (size_t i = 0; i < numModulationDestinations; ++i)
        {
            for (size_t j = 0; j < numModulationSources; ++j)
            {
                float v = mod_matrix[j][i] * m_lfos[j]->outputBlock[0] * lfo_amt_mods[j];
                modmixes[i] += v;
            }
        }
        for (int i = 0; i < numOutputs; ++i)
        {
            output_values[i] = modmixes[MD_Output0 + i];
        }
        for (size_t i = 0; i < numLfos; ++i)
        {
            lfo_amt_mods[i] = lfo_amounts[i] * modmixes[MD_LFO0Amount + i];
            lfo_rate_mods[i] = 2.0 * modmixes[MD_LFO0Rate + i];
        }
    }
    void set_modulation_amount(int source, int destination, double amt)
    {
        if (source >= 0 && source < numModulationSources && destination >= 0 &&
            destination < numModulationDestinations)
        {
            mod_matrix[source][destination] = amt;
        }
        else
            throw std::runtime_error(
                "Modulation source must be in range 0..3 and destination must be between 0..11");
    }
    void check_lfo_index(int index)
    {
        if (index < 0 || index >= numLfos)
            throw std::runtime_error(
                std::format("LFO index {} out of allowed range 0..{}", index, numLfos - 1));
    }
    void set_lfo_rate(int lfo_index, double rate)
    {
        check_lfo_index(lfo_index);
        lfo_rates[lfo_index] = rate;
    }
    void set_lfo_shape(int lfo_index, int sh)
    {
        check_lfo_index(lfo_index);
        lfo_shapes[lfo_index] = sh;
    }
    void set_lfo_deform(int lfo_index, double d)
    {
        check_lfo_index(lfo_index);
        lfo_deforms[lfo_index] = d;
    }
    void set_lfo_shift(int lfo_index, double s)
    {
        check_lfo_index(lfo_index);
        lfo_shifts[lfo_index] = s;
    }
    void set_lfo_randseed(int lfo_index, int seed)
    {
        check_lfo_index(lfo_index);
        lfo_randseeds[lfo_index] = seed;
    }
    std::vector<std::pair<double, double>> get_as_vector(int from_output, double duration,
                                                         double shift, double scale, int skip = 1)
    {
        if (from_output < 0 || from_output >= numOutputs)
            throw std::runtime_error("Output must be in range 0..3");
        if (duration < 0.05 || duration > 3600.0)
            throw std::runtime_error("Duration must be in range 0.05-3600.0");
        if (skip < 1)
            throw std::runtime_error("Skip must be at least 1");
        for (int i = 0; i < numLfos; ++i)
        {
            m_rngs[i].reseed(lfo_randseeds[i]);
        }
        std::vector<std::pair<double, double>> result;
        result.reserve(1024);
        int lensamples = duration * samplerate;
        int outcounter = 0;
        int blockcounter = 0;
        while (outcounter < lensamples)
        {
            process_block();
            if (blockcounter == 0)
            {
                double v = shift + output_values[from_output] * scale;
                result.emplace_back(outcounter / samplerate, v);
            }
            ++blockcounter;
            if (blockcounter == skip)
                blockcounter = 0;
            outcounter += BLOCKSIZE;
        }
        return result;
    }
    void add_to_sequence(ClapEventSequence &destination, int from_output, clap_id dest_par_id,
                         double duration, double shift, double scale, int skip = 1)
    {
        if (duration < 0.05 || duration > 3600.0)
            throw std::runtime_error("Duration must be in range 0.05-3600.0");
        if (skip < 1)
            throw std::runtime_error("Skip must be at least 1");
        for (int i = 0; i < numLfos; ++i)
        {
            m_rngs[i].reseed(lfo_randseeds[i]);
        }
        int lensamples = duration * samplerate;
        int outcounter = 0;
        int blockcounter = 0;
        while (outcounter < lensamples)
        {
            process_block();
            if (blockcounter == 0)
            {
                double v = shift + output_values[from_output] * scale;
                destination.addParameterEvent(false, outcounter / samplerate, -1, -1, -1, -1,
                                              dest_par_id, v);
            }
            ++blockcounter;
            if (blockcounter == skip)
                blockcounter = 0;
            outcounter += BLOCKSIZE;
        }
    }
    double samplerate = 0.0;
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
    alignas(32) std::array<std::unique_ptr<lfo_t>, numLfos> m_lfos;
    alignas(32) float table_envrate_linear[512];
    alignas(32) std::array<sst::basic_blocks::dsp::RNG, numLfos> m_rngs;
};

inline void init_multimod_from_json(AltMultiModulator &modulator, std::string jsonfilename)
{
    try
    {
        auto jsontxt = choc::file::loadFileAsString(jsonfilename);
        auto tree = choc::json::parse(jsontxt);
        for (int i = 0; i < AltMultiModulator::numLfos; ++i)
        {
            std::string prefix = "lfo" + std::to_string(i);
            modulator.lfo_rates[i] =
                std::clamp(tree[prefix + "rate"].getWithDefault(0.0), -7.0, 6.0);
            modulator.lfo_shapes[i] = std::clamp(tree[prefix + "shape"].getWithDefault(0), 0, 8);
            modulator.lfo_deforms[i] =
                std::clamp(tree[prefix + "deform"].getWithDefault(0.0), -1.0, 1.0);
            modulator.lfo_shifts[i] =
                std::clamp(tree[prefix + "shift"].getWithDefault(0.0), -1.0, 1.0);
            modulator.lfo_randseeds[i] = tree[prefix + "randseed"].getWithDefault(100 + 107 * i);
            modulator.lfo_unipolars[i] = tree[prefix + "unipolar"].getWithDefault(0);
        }
        auto matrix = tree["modmatrix"];
        if (matrix.isArray())
        {
            for (int i = 0; i < matrix.size(); ++i)
            {
                auto mentry = matrix[i];
                int source = mentry["s"].get<int>();
                int dest = mentry["d"].get<int>();
                double amt = mentry["a"].get<double>();
                if (source >= 0 && source < AltMultiModulator::numModulationSources && dest >= 0 &&
                    dest < AltMultiModulator::numModulationDestinations)
                {
                    modulator.mod_matrix[source][dest] = amt;
                }
            }
        }
    }
    catch (std::exception &ex)
    {
        std::cout << "error parsing " << jsonfilename << " : " << ex.what() << "\n";
    }
    for (int i = 0; i < AltMultiModulator::numLfos; ++i)
    {
        modulator.m_rngs[i].reseed(modulator.lfo_randseeds[i]);
    }
}
