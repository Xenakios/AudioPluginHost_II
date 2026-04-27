#include "xenfxbase.h"
#include "../Common/xap_utils.h"
#include "sst/basic-blocks/dsp/SmoothingStrategies.h"

using SmoothingStrategy = sst::basic_blocks::dsp::LagSmoothingStrategy;

class DustFX : public XenFXBase
{
  public:
    void prepare(double samplerate, size_t maxblocksize) override
    {
        sr = samplerate;
        gains[0].setRateInMilliseconds(2.0, sr, 1.0);
        gains[1].setRateInMilliseconds(2.0, sr, 1.0);
        SmoothingStrategy::setValueInstant(gains[0], 1.0);
        SmoothingStrategy::setValueInstant(gains[1], 1.0);
    }
    void reset() override {}
    size_t num_params() override { return 2; }
    std::string get_param_name(size_t paramindex) override
    {
        if (paramindex == 0)
            return "Probability";
        if (paramindex == 1)
            return "Size";
        return "unknown";
    }
    float get_parameter(size_t index) override
    {
        if (index == 0)
            return prob;
        else if (index == 1)
            return size;
        return 0.0f;
    }
    void set_parameter(size_t paramindex, float value) override
    {
        if (paramindex == 0)
            prob = value;
        if (paramindex == 1)
        {
            size = value;
            pulselensamples = sr * size * 0.05;
        }
    }
    void process(float **inbuffer, float **outbuffer, size_t num_frames) override
    {
        assert(sr > 0.0);
        for (int i = 0; i < num_frames; ++i)
        {
            if (phases[0] == 0)
            {
                if (rng.nextFloat() > prob)
                {
                    gains[0].setTarget(0.0);
                }
                else
                {
                    gains[0].setTarget(1.0);
                }
            }
            if (phases[1] == 0)
            {
                if (rng.nextFloat() > prob)
                {
                    gains[1].setTarget(0.0);
                }
                else
                {
                    gains[1].setTarget(1.0);
                }
            }
            gains[0].process();
            gains[1].process();
            outbuffer[0][i] = inbuffer[0][i] * SmoothingStrategy::getValue(gains[0]);
            outbuffer[1][i] = inbuffer[1][i] * SmoothingStrategy::getValue(gains[1]);
            
            ++phases[0];
            if (phases[0] >= pulselensamples)
                phases[0] = 0;
            ++phases[1];
            if (phases[1] >= pulselensamples)
                phases[1] = 0;
        }
    }
    float prob = 1.0f;
    float size = 0.0f;
    float sr = 0.0f;
    int pulselensamples = 0;
    int phases[2] = {0, 0};
    sst::basic_blocks::dsp::LagSmoothingStrategy::smoothValue_t gains[2];
    xenakios::Xoroshiro128Plus rng;
};
