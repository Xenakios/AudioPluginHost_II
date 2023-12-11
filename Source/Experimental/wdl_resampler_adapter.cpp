#include "wdl_resampler_adapter.h"
#include "WDL/resample.h"

namespace xenakios
{

Resampler::Resampler(bool use_sinc)
{
    m_impl = std::make_unique<WDL_Resampler>();
    if (use_sinc)
        m_impl->SetMode(true, 1, true);
    else
        m_impl->SetMode(true, 1, false);
}
Resampler::~Resampler() {}
int Resampler::ResamplePrepare(int requested, int chans, double **buf)
{
    return m_impl->ResamplePrepare(requested, chans, buf);
}
void Resampler::SetRates(double in, double out) { m_impl->SetRates(in, out); }
int Resampler::ResampleOut(double *out, int nsamples_in, int nsamples_out, int nch)
{
    return m_impl->ResampleOut(out, nsamples_in, nsamples_out, nch);
}

void Resampler::Reset(double fracpos) { m_impl->Reset(fracpos); }

} // namespace xenakios
