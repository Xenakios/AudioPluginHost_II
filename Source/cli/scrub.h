#pragma once
#include <array>
#include <cmath>
#include <complex>
#include "audio/choc_SampleBuffers.h"
#include "../Common/xap_utils.h"
#include "xcli_utils.h"

// adapted from https://github.com/Chowdhury-DSP/chowdsp_utils
// begin Chowdhury code
/**
    Successive samples in the delay line will be interpolated using Sinc
    interpolation. This method is somewhat less efficient than the others,
    but gives a very smooth and flat frequency response.

    Note that Sinc interpolation cannot currently be used with SIMD data types!
*/
template <typename T, size_t N, size_t M = 256> struct Sinc
{
    Sinc()
    {
        T cutoff = 0.455f;
        size_t j;
        for (j = 0; j < M + 1; j++)
        {
            for (size_t i = 0; i < N; i++)
            {
                T t = -T(i) + T(N / (T)2.0) + T(j) / T(M) - (T)1.0;
                sinctable[j * N * 2 + i] =
                    symmetric_blackman(t, (int)N) * cutoff * sincf(cutoff * t);
            }
        }
        for (j = 0; j < M; j++)
        {
            for (size_t i = 0; i < N; i++)
                sinctable[j * N * 2 + N + i] =
                    (sinctable[(j + 1) * N * 2 + i] - sinctable[j * N * 2 + i]) / (T)65536.0;
        }
    }

    inline T sincf(T x) const noexcept
    {
        if (x == (T)0)
            return (T)1;
        return (std::sin(g_pi * x)) / (g_pi * x);
    }

    inline T symmetric_blackman(T i, int n) const noexcept
    {
        i -= (n / 2);
        const double twoPi = g_pi * 2;
        return ((T)0.42 - (T)0.5 * std::cos(twoPi * i / (n)) +
                (T)0.08 * std::cos(4 * g_pi * i / (n)));
    }

    void reset(int newTotalSize) { totalSize = newTotalSize; }

    void updateInternalVariables(int & /*delayIntOffset*/, T & /*delayFrac*/) {}
    alignas(16) float srcbuf[N];
    // #define SIMDSINC

    template <typename Source>
    inline T call(Source &buffer, int delayInt, double delayFrac, const T & /*state*/, int channel,
                  int sposmin, int sposmax)
    {
        auto sincTableOffset = (size_t)((1.0 - delayFrac) * (T)M) * N * 2;
        for (int i = 0; i < N; ++i)
        {
            srcbuf[i] =
                buffer.getBufferSampleSafeAndFade(delayInt + i, channel, sposmin, sposmax, 512);
        }

#ifndef SIMDSINC
        auto out = ((T)0);
        for (size_t i = 0; i < N; i += 1)
        {
            auto buff_reg = srcbuf[i];
            auto sinc_reg = sinctable[sincTableOffset + i];
            out += buff_reg * sinc_reg;
        }
        return out;
#else
        alignas(16) simd::float_4 out{0.0f, 0.0f, 0.0f, 0.0f};
        for (size_t i = 0; i < N; i += 4)
        {
            // auto buff_reg = SIMDUtils::loadUnaligned (&buffer[(size_t) delayInt + i]);
            // auto buff_reg = buffer.getBufferSampleSafeAndFade(delayInt + i,channel,512);
            alignas(16) simd::float_4 buff_reg;
            buff_reg.load(&srcbuf[i]);
            // auto sinc_reg = juce::dsp::SIMDRegister<T>::fromRawArray (&sinctable[sincTableOffset
            // + i]); auto sinc_reg = sinctable[sincTableOffset + i];
            alignas(16) simd::float_4 sinc_reg;
            sinc_reg.load(&sinctable[sincTableOffset + i]);
            out = out + (buff_reg * sinc_reg);
        }
        float sum = 0.0f;
        for (int i = 0; i < 4; ++i)
            sum += out[i];
        return sum;
#endif
    }

    int totalSize = 0;
    // T sinctable alignas (SIMDUtils::CHOWDSP_DEFAULT_SIMD_ALIGNMENT)[(M + 1) * N * 2];
    T sinctable alignas(16)[(M + 1) * N * 2];
};

// end Chowdhury code

class BufferScrubber
{
  public:
    alignas(32) Sinc<float, 16, 65536> m_sinc_interpolator;
    double m_sample_rate = 0.0;
    BufferScrubber(choc::buffer::ChannelArrayView<float> src, double sample_rate)
        : m_sample_rate(sample_rate), source_buffer(src)
    {
        // m_filter_divider.setDivision(128);
        updateFiltersIfNeeded(44100.0f, 20.0f, true);
    }
    std::array<double, 2> m_last_pos = {0.0f, 0.0f};
    int m_resampler_type = 0;
    int m_compensate_volume = 0;
    float m_last_sr = 0.0f;
    float m_last_pos_smoother_cutoff = 0.0f;
    // dsp::ClockDivider m_filter_divider;
    void updateFiltersIfNeeded(float sr, float scrubSmoothingCutoff, bool force = false)
    {
        // if (force || m_filter_divider.process())
        {
            if (sr != m_last_sr || m_last_pos_smoother_cutoff != scrubSmoothingCutoff)
            {
                for (auto &f : m_position_smoothers)
                    f.setParameters(dsp::BiquadFilter::LOWPASS_1POLE, scrubSmoothingCutoff / sr,
                                    1.0, 1.0f);
                for (auto &f : m_gain_smoothers)
                    f.setParameters(dsp::BiquadFilter::LOWPASS_1POLE, 8.0 / sr, 1.0, 1.0f);
                m_last_sr = sr;
                m_last_pos_smoother_cutoff = scrubSmoothingCutoff;
            }
        }
    }
    // OK, probably not the most efficient implementation, but will have to see later if can be
    // optimized
    float getBufferSampleSafeAndFade(int frame, int channel, int minFramePos, int maxFramePos,
                                     int fadelen)
    {
        using namespace xenakios;
        if (source_buffer.getNumChannels() == 1)
            channel = 0;
        if (frame >= 0 && frame < source_buffer.getNumFrames())
        {
            float gain = 1.0f;
            if (frame >= minFramePos && frame < minFramePos + fadelen)
                gain =
                    mapvalue<double>((float)frame, minFramePos, minFramePos + fadelen, 0.0f, 1.0f);
            if (frame >= maxFramePos - fadelen && frame < maxFramePos)
                gain =
                    mapvalue<double>((float)frame, maxFramePos - fadelen, maxFramePos, 1.0f, 0.0f);
            if (frame < minFramePos || frame >= maxFramePos)
                gain = 0.0f;
            return source_buffer.getSample(channel, frame);
            // return m_audioBuffers[m_playbackBufferIndex][frame * m_channels + channel] * gain;
        }
        return 0.0;
    }

    void processFrame(float *outbuf, int nchs, float sr, float scansmoothingCutoff)
    {
        updateFiltersIfNeeded(sr, scansmoothingCutoff);
        double positions[2] = {m_next_pos - m_separation, m_next_pos + m_separation};
        int srcstartsamples = 0; // m_src->getSourceNumSamples() * m_reg_start;
        int srcendsamples = source_buffer.getNumFrames();
        int srclensamps = srcendsamples - srcstartsamples;
        // m_src->setSubSection(srcstartsamples,srcendsamples);
        for (int i = 0; i < 2; ++i)
        {
            double target_pos = m_position_smoothers[i].process(positions[i]);
            m_smoothed_positions[i] = m_reg_start + target_pos * (m_reg_end - m_reg_start);

            double temp = (double)srcstartsamples + target_pos * srclensamps;
            double posdiff = m_last_pos[i] - temp;
            double posdiffa = std::abs(posdiff);
            if (posdiffa < 1.0f / 128) // so slow already that can just as well cut output
            {
                m_out_gains[i] = 0.0;
            }
            else
            {
                m_out_gains[i] = 1.0f;
                if (m_compensate_volume == 1 && posdiffa >= 4.0)
                {
                    m_out_gains[i] = xenakios::mapvalue(posdiffa, 4.0, 32.0, 1.0, 0.0);
                    if (m_out_gains[i] < 0.0f)
                        m_out_gains[i] = 0.0f;
                }
            }
            m_last_pos[i] = temp;
            int index0 = temp;
            int index1 = index0 + 1;
            double frac = (temp - (double)index0);
            if (m_resampler_type == 1) // for sinc...
                frac = 1.0 - frac;
            float gain = m_gain_smoothers[i].process(m_out_gains[i]);
            if (gain >= 0.00001) // over -100db, process
            {
                m_stopped = false;
                float bogus = 0.0f;
                if (m_resampler_type == 0)
                {
                    float y0 =
                        getBufferSampleSafeAndFade(index0, i, srcstartsamples, srcendsamples, 256);
                    float y1 =
                        getBufferSampleSafeAndFade(index1, i, srcstartsamples, srcendsamples, 256);
                    float y2 = y0 + (y1 - y0) * frac;
                    outbuf[i] = y2 * gain;
                }
                else
                {
                    float y2 = m_sinc_interpolator.call(*this, index0, frac, bogus, i,
                                                        srcstartsamples, srcendsamples);
                    outbuf[i] = y2 * gain;
                }
            }
            else
            {
                m_stopped = true;
                outbuf[i] = 0.0f;
            }
        }
    }
    void setNextPosition(double npos) { m_next_pos = npos; }
    void setRegion(float startpos, float endpos)
    {
        m_reg_start = startpos;
        m_reg_end = endpos;
    }
    void setSeparation(float s) { m_separation = xenakios::mapvalue(s, 0.0f, 1.0f, -0.1f, 0.1f); }
    float m_separation = 0.0f;
    float m_reg_start = 0.0f;
    float m_reg_end = 1.0f;
    bool m_stopped = false;
    double m_cur_pos = 0.0;
    std::array<float, 2> m_smoothed_positions = {0.0f, 0.0f};
    std::array<float, 2> m_out_gains = {0.0f, 0.0f};
    double m_smoothed_out_gain = 0.0f;

  private:
    choc::buffer::ChannelArrayView<float> source_buffer;

    double m_next_pos = 0.0f;
    std::array<dsp::BiquadFilter, 2> m_gain_smoothers;
    std::array<dsp::BiquadFilter, 2> m_position_smoothers;
};
