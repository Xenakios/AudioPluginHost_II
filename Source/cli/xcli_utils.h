#pragma once
#include <iostream>
#include "../Common/xap_breakpoint_envelope.h"
#include "text/choc_Files.h"
#include "text/choc_StringUtilities.h"
#include "audio/choc_SampleBuffers.h"

const double g_pi = M_PI;
namespace dsp
{
/** Digital IIR filter processor.
https://en.wikipedia.org/wiki/Infinite_impulse_response
*/
template <int B_ORDER, int A_ORDER, typename T = float> struct IIRFilter
{
    /** transfer function numerator coefficients: b_0, b_1, etc.
     */
    T b[B_ORDER] = {};
    /** transfer function denominator coefficients: a_1, a_2, etc.
    a_0 is fixed to 1 and omitted from the `a` array, so its indices are shifted down by 1.
    */
    T a[A_ORDER - 1] = {};
    /** input state
    x[0] = x_{n-1}
    x[1] = x_{n-2}
    etc.
    */
    T x[B_ORDER - 1];
    /** output state */
    T y[A_ORDER - 1];

    IIRFilter() { reset(); }

    void reset()
    {
        for (int i = 1; i < B_ORDER; i++)
        {
            x[i - 1] = 0.f;
        }
        for (int i = 1; i < A_ORDER; i++)
        {
            y[i - 1] = 0.f;
        }
    }

    void setCoefficients(const T *b, const T *a)
    {
        for (int i = 0; i < B_ORDER; i++)
        {
            this->b[i] = b[i];
        }
        for (int i = 1; i < A_ORDER; i++)
        {
            this->a[i - 1] = a[i - 1];
        }
    }

    T process(T in)
    {
        T out = 0.f;
        // Add x state
        if (0 < B_ORDER)
        {
            out = b[0] * in;
        }
        for (int i = 1; i < B_ORDER; i++)
        {
            out += b[i] * x[i - 1];
        }
        // Subtract y state
        for (int i = 1; i < A_ORDER; i++)
        {
            out -= a[i - 1] * y[i - 1];
        }
        // Shift x state
        for (int i = B_ORDER - 1; i >= 2; i--)
        {
            x[i - 1] = x[i - 2];
        }
        x[0] = in;
        // Shift y state
        for (int i = A_ORDER - 1; i >= 2; i--)
        {
            y[i - 1] = y[i - 2];
        }
        y[0] = out;
        return out;
    }

    /** Computes the complex transfer function $H(s)$ at a particular frequency
    s: normalized angular frequency equal to $2 \pi f / f_{sr}$ ($\pi$ is the Nyquist frequency)
    */
    /*
    std::complex<T> getTransferFunction(T s)
    {
        // Compute sum(a_k z^-k) / sum(b_k z^-k) where z = e^(i s)
        std::complex<T> bSum(b[0], 0);
        std::complex<T> aSum(1, 0);
        for (int i = 1; i < std::max(B_ORDER, A_ORDER); i++)
        {
            T p = -i * s;
            std::complex<T> z(simd::cos(p), simd::sin(p));
            if (i < B_ORDER)
                bSum += b[i] * z;
            if (i < A_ORDER)
                aSum += a[i - 1] * z;
        }
        return bSum / aSum;
    }
    */
    T getFrequencyResponse(T f)
    {
        // T hReal, hImag;
        // getTransferFunction(2 * M_PI * f, &hReal, &hImag);
        // return simd::hypot(hReal, hImag);
        return 0.0;
        // return simd::abs(getTransferFunction(2 * M_PI * f));
    }

    // T getFrequencyPhase(T f) { return simd::arg(getTransferFunction(2 * M_PI * f)); }
};
template <typename T = float> struct TBiquadFilter : IIRFilter<3, 3, T>
{
    enum Type
    {
        LOWPASS_1POLE,
        HIGHPASS_1POLE,
        LOWPASS,
        HIGHPASS,
        LOWSHELF,
        HIGHSHELF,
        BANDPASS,
        PEAK,
        NOTCH,
        NUM_TYPES
    };

    TBiquadFilter() { setParameters(LOWPASS, 0.f, 0.f, 1.f); }

    /** Calculates and sets the biquad transfer function coefficients.
    f: normalized frequency (cutoff frequency / sample rate), must be less than 0.5
    Q: quality factor
    V: gain
    */
    void setParameters(Type type, float f, float Q, float V)
    {
        float K = std::tan(M_PI * f);
        switch (type)
        {
        case LOWPASS_1POLE:
        {
            this->a[0] = -std::exp(-2.f * M_PI * f);
            this->a[1] = 0.f;
            this->b[0] = 1.f + this->a[0];
            this->b[1] = 0.f;
            this->b[2] = 0.f;
        }
        break;

        case HIGHPASS_1POLE:
        {
            this->a[0] = std::exp(-2.f * M_PI * (0.5f - f));
            this->a[1] = 0.f;
            this->b[0] = 1.f - this->a[0];
            this->b[1] = 0.f;
            this->b[2] = 0.f;
        }
        break;

        case LOWPASS:
        {
            float norm = 1.f / (1.f + K / Q + K * K);
            this->b[0] = K * K * norm;
            this->b[1] = 2.f * this->b[0];
            this->b[2] = this->b[0];
            this->a[0] = 2.f * (K * K - 1.f) * norm;
            this->a[1] = (1.f - K / Q + K * K) * norm;
        }
        break;

        case HIGHPASS:
        {
            float norm = 1.f / (1.f + K / Q + K * K);
            this->b[0] = norm;
            this->b[1] = -2.f * this->b[0];
            this->b[2] = this->b[0];
            this->a[0] = 2.f * (K * K - 1.f) * norm;
            this->a[1] = (1.f - K / Q + K * K) * norm;
        }
        break;

        case LOWSHELF:
        {
            float sqrtV = std::sqrt(V);
            if (V >= 1.f)
            {
                float norm = 1.f / (1.f + M_SQRT2 * K + K * K);
                this->b[0] = (1.f + M_SQRT2 * sqrtV * K + V * K * K) * norm;
                this->b[1] = 2.f * (V * K * K - 1.f) * norm;
                this->b[2] = (1.f - M_SQRT2 * sqrtV * K + V * K * K) * norm;
                this->a[0] = 2.f * (K * K - 1.f) * norm;
                this->a[1] = (1.f - M_SQRT2 * K + K * K) * norm;
            }
            else
            {
                float norm = 1.f / (1.f + M_SQRT2 / sqrtV * K + K * K / V);
                this->b[0] = (1.f + M_SQRT2 * K + K * K) * norm;
                this->b[1] = 2.f * (K * K - 1) * norm;
                this->b[2] = (1.f - M_SQRT2 * K + K * K) * norm;
                this->a[0] = 2.f * (K * K / V - 1.f) * norm;
                this->a[1] = (1.f - M_SQRT2 / sqrtV * K + K * K / V) * norm;
            }
        }
        break;

        case HIGHSHELF:
        {
            float sqrtV = std::sqrt(V);
            if (V >= 1.f)
            {
                float norm = 1.f / (1.f + M_SQRT2 * K + K * K);
                this->b[0] = (V + M_SQRT2 * sqrtV * K + K * K) * norm;
                this->b[1] = 2.f * (K * K - V) * norm;
                this->b[2] = (V - M_SQRT2 * sqrtV * K + K * K) * norm;
                this->a[0] = 2.f * (K * K - 1.f) * norm;
                this->a[1] = (1.f - M_SQRT2 * K + K * K) * norm;
            }
            else
            {
                float norm = 1.f / (1.f / V + M_SQRT2 / sqrtV * K + K * K);
                this->b[0] = (1.f + M_SQRT2 * K + K * K) * norm;
                this->b[1] = 2.f * (K * K - 1.f) * norm;
                this->b[2] = (1.f - M_SQRT2 * K + K * K) * norm;
                this->a[0] = 2.f * (K * K - 1.f / V) * norm;
                this->a[1] = (1.f / V - M_SQRT2 / sqrtV * K + K * K) * norm;
            }
        }
        break;

        case BANDPASS:
        {
            float norm = 1.f / (1.f + K / Q + K * K);
            this->b[0] = K / Q * norm;
            this->b[1] = 0.f;
            this->b[2] = -this->b[0];
            this->a[0] = 2.f * (K * K - 1.f) * norm;
            this->a[1] = (1.f - K / Q + K * K) * norm;
        }
        break;

        case PEAK:
        {
            if (V >= 1.f)
            {
                float norm = 1.f / (1.f + K / Q + K * K);
                this->b[0] = (1.f + K / Q * V + K * K) * norm;
                this->b[1] = 2.f * (K * K - 1.f) * norm;
                this->b[2] = (1.f - K / Q * V + K * K) * norm;
                this->a[0] = this->b[1];
                this->a[1] = (1.f - K / Q + K * K) * norm;
            }
            else
            {
                float norm = 1.f / (1.f + K / Q / V + K * K);
                this->b[0] = (1.f + K / Q + K * K) * norm;
                this->b[1] = 2.f * (K * K - 1.f) * norm;
                this->b[2] = (1.f - K / Q + K * K) * norm;
                this->a[0] = this->b[1];
                this->a[1] = (1.f - K / Q / V + K * K) * norm;
            }
        }
        break;

        case NOTCH:
        {
            float norm = 1.f / (1.f + K / Q + K * K);
            this->b[0] = (1.f + K * K) * norm;
            this->b[1] = 2.f * (K * K - 1.f) * norm;
            this->b[2] = this->b[0];
            this->a[0] = this->b[1];
            this->a[1] = (1.f - K / Q + K * K) * norm;
        }
        break;

        default:
            break;
        }
    }
};

typedef TBiquadFilter<> BiquadFilter;

/** Blackman-Harris window function.
https://en.wikipedia.org/wiki/Window_function#Blackman%E2%80%93Harris_window
*/
template <typename T> inline T blackmanHarris(T p)
{
    return +T(0.35875) - T(0.48829) * std::cos(2 * T(M_PI) * p) +
           T(0.14128) * std::cos(4 * T(M_PI) * p) - T(0.01168) * std::cos(6 * T(M_PI) * p);
}

/** The normalized sinc function
See https://en.wikipedia.org/wiki/Sinc_function
*/
inline float sinc(float x)
{
    if (x == 0.f)
        return 1.f;
    x *= M_PI;
    return std::sin(x) / x;
}

/** Computes the impulse response of a boxcar lowpass filter */
inline void boxcarLowpassIR(float *out, int len, float cutoff = 0.5f)
{
    for (int i = 0; i < len; i++)
    {
        float t = i - (len - 1) / 2.f;
        out[i] = 2 * cutoff * sinc(2 * cutoff * t);
    }
}

inline void blackmanHarrisWindow(float *x, int len)
{
    for (int i = 0; i < len; i++)
    {
        x[i] *= blackmanHarris(float(i) / (len - 1));
    }
}

/** Downsamples by an integer factor. */
template <int OVERSAMPLE, int QUALITY, typename T = float> struct Decimator
{
    T inBuffer[OVERSAMPLE * QUALITY];
    float kernel[OVERSAMPLE * QUALITY];
    int inIndex;

    Decimator(float cutoff = 0.9f)
    {
        boxcarLowpassIR(kernel, OVERSAMPLE * QUALITY, cutoff * 0.5f / OVERSAMPLE);
        blackmanHarrisWindow(kernel, OVERSAMPLE * QUALITY);
        reset();
    }
    void reset()
    {
        inIndex = 0;
        std::memset(inBuffer, 0, sizeof(inBuffer));
    }
    /** `in` must be length OVERSAMPLE */
    T process(T *in)
    {
        // Copy input to buffer
        std::memcpy(&inBuffer[inIndex], in, OVERSAMPLE * sizeof(T));
        // Advance index
        inIndex += OVERSAMPLE;
        inIndex %= OVERSAMPLE * QUALITY;
        // Perform naive convolution
        T out = 0.f;
        for (int i = 0; i < OVERSAMPLE * QUALITY; i++)
        {
            int index = inIndex - 1 - i;
            index = (index + OVERSAMPLE * QUALITY) % (OVERSAMPLE * QUALITY);
            out += kernel[i] * inBuffer[index];
        }
        return out;
    }
};

/** Upsamples by an integer factor. */
template <int OVERSAMPLE, int QUALITY> struct Upsampler
{
    float inBuffer[QUALITY];
    float kernel[OVERSAMPLE * QUALITY];
    int inIndex;

    Upsampler(float cutoff = 0.9f)
    {
        boxcarLowpassIR(kernel, OVERSAMPLE * QUALITY, cutoff * 0.5f / OVERSAMPLE);
        blackmanHarrisWindow(kernel, OVERSAMPLE * QUALITY);
        reset();
    }
    void reset()
    {
        inIndex = 0;
        std::memset(inBuffer, 0, sizeof(inBuffer));
    }
    /** `out` must be length OVERSAMPLE */
    void process(float in, float *out)
    {
        // Zero-stuff input buffer
        inBuffer[inIndex] = OVERSAMPLE * in;
        // Advance index
        inIndex++;
        inIndex %= QUALITY;
        // Naively convolve each sample
        // TODO replace with polyphase filter hierarchy
        for (int i = 0; i < OVERSAMPLE; i++)
        {
            float y = 0.f;
            for (int j = 0; j < QUALITY; j++)
            {
                int index = inIndex - 1 - j;
                index = (index + QUALITY) % QUALITY;
                int kernelIndex = OVERSAMPLE * j + i;
                y += kernel[kernelIndex] * inBuffer[index];
            }
            out[i] = y;
        }
    }
};

} // namespace dsp

inline void init_envelope_from_string(xenakios::Envelope &env, std::string str)
{
    try
    {
        if (std::filesystem::exists(str))
        {
            std::cout << "loading " << str << " as envelope\n";
            auto envtxt = choc::file::loadFileAsString(str);
            auto lines = choc::text::splitIntoLines(envtxt, false);
            for (auto &line : lines)
            {
                line = choc::text::trim(line);
                auto tokens = choc::text::splitString(line, ' ', false);
                if (tokens.size() >= 2)
                {
                    double time = std::stod(tokens[0]);
                    double value = std::stod(tokens[1]);
                    int shape = 0;
                    double p0 = 0.0;
                    if (tokens.size() >= 3)
                    {
                        shape = std::stoi(tokens[2]);
                    }
                    if (tokens.size() == 4)
                    {
                        p0 = stod(tokens[3]);
                    }

                    env.addPoint({time, value, shape, p0});
                }
            }
        }
        else
        {
            double val = std::stod(str);
            std::cout << "parsed value " << val << "\n";
            env.addPoint({0.0, val});
        }
    }
    catch (std::exception &ex)
    {
        std::cout << ex.what() << "\n";
    }
}

template <int OSFACTOR, int QUALITY> class OverSampler
{
  public:
    dsp::Upsampler<OSFACTOR, QUALITY> upsamplers[2];
    dsp::Decimator<OSFACTOR, QUALITY> downsamplers[2];
    choc::buffer::ChannelArrayBuffer<float> oversampledbuffer;
    OverSampler(unsigned int numchannels, unsigned int maxframes)
    {
        oversampledbuffer =
            choc::buffer::ChannelArrayBuffer<float>{numchannels, maxframes * OSFACTOR};
        oversampledbuffer.clear();
    }
    void pushBuffer(choc::buffer::ChannelArrayView<float> inview)
    {
        for (int j = 0; j < inview.getNumChannels(); ++j)
        {
            auto chandata = oversampledbuffer.getView().data.channels[j];
            for (int i = 0; i < inview.getNumFrames(); ++i)
            {
                upsamplers[j].process(inview.getSample(j, i), &chandata[i * OSFACTOR]);
            }
        }
    }
    void downSample(choc::buffer::ChannelArrayView<float> outview)
    {
        for (int ch = 0; ch < outview.getNumChannels(); ++ch)
        {
            auto chandata = oversampledbuffer.getView().data.channels[ch];
            for (int i = 0; i < outview.getNumFrames(); ++i)
            {
                outview.getSample(ch, i) = downsamplers[ch].process(&chandata[i * OSFACTOR]);
            }
        }
    }
};
