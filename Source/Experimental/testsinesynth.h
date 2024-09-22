#pragma once

#include <cmath>
#include <vector>
#include <memory>
#include <algorithm>
#include "sst/basic-blocks/modulators/ADSREnvelope.h"
#include "sst/effects/ConcreteConfig.h"
#include "sst/effects/Reverb2.h"

struct SRProviderB
{
    static constexpr int BLOCK_SIZE = 32;
    static constexpr int BLOCK_SIZE_OS = BLOCK_SIZE * 2;
    SRProviderB() { initTables(); }
    alignas(32) float table_envrate_linear[512];
    double samplerate = 44100.0;
    void initTables()
    {
        double dsamplerate_os = samplerate * 2;
        for (int i = 0; i < 512; ++i)
        {
            double k = dsamplerate_os * std::pow(2.0, (((double)i - 256.0) / 16.0)) /
                       (double)BLOCK_SIZE_OS;
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

class TestSineSynth
{
  public:
    SRProviderB srprovider;
    using FxConfig = sst::effects::core::ConcreteConfig;
    FxConfig m_fxconfig;
    FxConfig::GlobalStorage m_fxgs;
    FxConfig::EffectStorage m_fxes;
    using ReverbType = sst::effects::reverb2::Reverb2<FxConfig>;
    ReverbType m_reverb;
    TestSineSynth() : m_fxgs{44100.0}, m_reverb{&m_fxgs, &m_fxes, nullptr}
    {
        for (int i = 0; i < 16; ++i)
        {
            auto v = std::make_unique<Voice>(&srprovider);
            m_voices.push_back(std::move(v));
        }
        for (int i = 0; i < ReverbType::numParams; ++i)
            m_reverb.paramStorage[i] = m_reverb.paramAt(i).defaultVal;
    }
    void prepare(double samplerate, int bufsize)
    {
        m_sr = samplerate;
        srprovider.samplerate = samplerate;
        srprovider.initTables();
        for (auto &v : m_voices)
        {
            v->volEnv.onSampleRateChanged();
        }
        m_mixbuf.resize(bufsize * 2);
        m_fxgs.sampleRate = samplerate;
        // m_reverb.initialize();
    }
    void startNote(int key, float gain)
    {
        for (auto &v : m_voices)
        {
            if (!v->isActive)
            {
                v->isActive = true;
                v->key = key;
                float hz = 440.0 * std::pow(2.0, (key - 69) / 12.0);
                v->phaseinc = 2 * M_PI / m_sr * hz;
                v->gate = true;
                v->gain = gain;
                v->volEnv.attackFrom(0.0f, 0.0f, 0, true);
                break;
            }
        }
    }
    void stopNote(int key)
    {
        for (auto &v : m_voices)
        {
            if (v->isActive && v->key == key)
            {
                v->gate = false;
                break;
            }
        }
    }
    void process(float &outLeft, float &outRight)
    {
        outLeft = 0.0f;
        outRight = 0.0f;
        for (auto &v : m_voices)
        {
            if (v->isActive)
            {
                if (blockCounter == 0)
                {
                    v->volEnv.processBlock(0.5, 0.1, 1.0, 0.5, 1, 1, 1, v->gate);
                }
                float envGain = v->volEnv.outputCache[blockCounter];
                float vout = std::sin(v->phase) * v->gain * envGain;
                outLeft += vout;
                outRight += vout;
                v->phase += v->phaseinc;
                if (v->volEnv.stage == decltype(v->volEnv)::s_eoc)
                {
                    v->isActive = false;
                }
            }
        }
        ++blockCounter;
        if (blockCounter == 32)
        {
            blockCounter = 0;
        }
        // m_reverb.processBlock(&outLeft, &outRight);
    }
    int blockCounter = 0;

  private:
    struct Voice
    {
        Voice(SRProviderB *srp) : volEnv(srp) {}
        bool isActive = false;
        int key = 0;
        double phase = 0.0;
        double phaseinc = 0.0;
        double gain = 0.0;
        bool gate = false;
        sst::basic_blocks::modulators::ADSREnvelope<SRProviderB, 32> volEnv;
    };
    std::vector<std::unique_ptr<Voice>> m_voices;
    double m_sr = 0.0;
    std::vector<float> m_mixbuf;
};
