#pragma once

#include "sst/basic-blocks/modulators/SimpleLFO.h"
#include "sst/basic-blocks/mod-matrix/ModMatrix.h"
#include "xap_utils.h"

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

inline xenakios::Envelope<64> generateEnvelopeFromLFO(double rate, double deform, int shape,
                                                      double envlen, double envgranul)
{
    double sr = 44100.0;
    constexpr size_t blocklen = 64;
    xenakios::Envelope<blocklen> result;
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
    template<typename SeqType>
    void applyToSequence(SeqType &destSeq, double startTime, double duration)
    {
        m.prepare(rt);
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
                    env.processBlock(tpos, sr, 2);
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
    std::array<xenakios::Envelope<blocklen>, 4> envs;
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
