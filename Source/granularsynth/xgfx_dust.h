#include "xenfxbase.h"
#include "../Common/xap_utils.h"

class DustFX : public XenFXBase
{
  public:
    void prepare(double samplerate, size_t maxblocksize) override {}
    void reset() override {}
    size_t num_params() override { return 1; }
    std::string get_param_name(size_t paramindex) override
    {
        if (paramindex == 0)
            return "Probability";
        return "unknown";
    }
    float get_parameter(size_t index) override
    {
        if (index == 0)
            return prob;
        return 0.0f;
    }
    void set_parameter(size_t paramindex, float value) override
    {
        if (paramindex == 0)
            prob = value;
    }
    void process(float **inbuffer, float **outbuffer, size_t num_frames) override
    {
        for (int i = 0; i < num_frames; ++i)
        {
            outbuffer[0][i] = inbuffer[0][i];
            outbuffer[1][i] = inbuffer[1][i];
            if (rng.nextFloat() > prob)
            {
                outbuffer[0][i] = 0.0f;
                outbuffer[1][i] = 0.0f;
            }
        }
    }
    float prob = 1.0f;
    xenakios::Xoroshiro128Plus rng;
};
