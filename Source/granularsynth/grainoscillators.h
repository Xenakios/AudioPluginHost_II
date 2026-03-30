#pragma once

#include <algorithm>
#include <random>
#include "sst/basic-blocks/dsp/SmoothingStrategies.h"
#include "sst/basic-blocks/dsp/CorrelatedNoise.h"
#include "easing.h"

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
    void setFrequencySmoothingRateMS(float ms) {}
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
    void setFrequencySmoothingRateMS(float ms) {}
    float step()
    {
        phase += phaseinc.getValue();
        if (phase >= 1.0)
        {
            phase -= 1.0;
            lastvalue = nextvalue;
            if (imode < 4)
                nextvalue = sst::basic_blocks::dsp::correlated_noise_o2mk2_supplied_value(
                    history[0], history[1], correlation.getValue(), bpdist(rng));
            else
            {
                double logisticx1 = logisticx0 * (1.0 - logisticx0) * logisticr;
                nextvalue = -1.0 + 2.0 * logisticx0;
                logisticx0 = logisticx1;
            }
        }
        float outvalue = lastvalue;
        if (imode == 1 || imode == 4)
        {
            outvalue = lastvalue + (nextvalue - lastvalue) * phase;
        }
        if (imode == 2)
        {
            uint8_t phaseinteger = 255 * phase;
            uint8_t oscinteger = (nextvalue + 1.0 * 0.5) * 255;
            // oscinteger = oscinteger ^ phaseinteger;
            oscinteger = phaseinteger ^ oscinteger;
            outvalue = -1.0f + (oscinteger / 255.0) * 2.0f;
        }
        if (imode == 3)
        {
            outvalue = lastvalue + (nextvalue - lastvalue) * BounceEaseIn(phase);
            // outvalue = lastvalue + (nextvalue - lastvalue) * ElasticEaseIn(phase);
        }
        outvalue = std::clamp(outvalue, -1.0f, 1.0f);
        SmoothingStrategy::process(phaseinc);
        SmoothingStrategy::process(correlation);
        return outvalue;
    }
    void reset()
    {
        history[0] = 0.0f;
        history[1] = 0.0f;
        phase = 0.0;
        lastvalue = 0.0f;
        nextvalue = 0.0;
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
    float lastvalue = 0.0;
    float nextvalue = 0.0;
    double logisticx0 = 0.4;
    double logisticr = 3.0;
    int imode = 0;
    double sr = 0.0;
    double frequency = 1.0;
    alignas(16) double phase = 0.0;
    alignas(16) sst::basic_blocks::dsp::LagSmoothingStrategy::smoothValue_t phaseinc;
    alignas(16) sst::basic_blocks::dsp::LagSmoothingStrategy::smoothValue_t correlation;
};
