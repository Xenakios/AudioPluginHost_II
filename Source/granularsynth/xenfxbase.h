#pragma once
#include <string>

class XenFXBase
{
  public:
    virtual ~XenFXBase() {}
    virtual void prepare(double samplerate, size_t maxblocksize) = 0;
    virtual void reset() = 0;
    virtual size_t num_params() = 0;
    virtual std::string get_param_name(size_t paramindex) = 0;
    virtual float get_parameter(size_t index) = 0;
    virtual void set_parameter(size_t paramindex, float value) = 0;
    virtual void process(float **inbuffer, float **outbuffer, size_t num_frames) = 0;
};

