#pragma once
#include <memory>

/*
The WDL stuff is problematic because of all the macros etc it uses, so we don't
want to pollute our namespace by including any of that directly.
Work around by this pimpl pattern.
*/

class WDL_Resampler;

namespace xenakios
{
class Resampler
{
public:
    // if not sinc, linear
    Resampler(bool use_sinc);
    ~Resampler();
    int ResamplePrepare(int requested, int chans, double **buf);
    void SetRates(double in, double out);
    int ResampleOut(double *out, int nsamples_in, int nsamples_out, int nch);
    void Reset(double fracpos = 0.0);
private:
    
    std::unique_ptr<WDL_Resampler> m_impl;
};
} // namespace xenakios
