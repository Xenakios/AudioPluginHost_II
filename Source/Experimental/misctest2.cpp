#include "../Common/xap_utils.h"
#include "../Common/xap_breakpoint_envelope.h"
#include <print>
#include <array>

inline double weierstrass(double x, double a, int b = 7, size_t iters = 16)
{
    double sum = 0.0;
    for (size_t i = 0; i < iters; ++i)
    {
        sum += std::pow(a, i) * std::cos(std::pow(b, i) * M_PI * x);
    }
    return sum;
}

void test_weierstrass()
{
    double sr = 192000;
    auto writer =
        xenakios::createWavWriter(R"(C:\develop\AudioPluginHost_mk2\audio\footest01.wav)", 1, sr);
    size_t outlen = sr * 5;
    auto outbuf = choc::buffer::ChannelArrayBuffer<float>(1, outlen);
    int blockcounter = 0;
    int blocklen = 64;
    double a = 0.5;
    double b = 9.0;
    int iters = 0;
    int hzcounter = 0;
    std::array<double, 5> hzs{1.0, 2.0, 3.0, 4.0, 0.5};
    double hz = 1.0;
    xenakios::Envelope env_a{{{0.0, 0.3}, {2.5, 0.7}, {5.0, 0.1}}};
    xenakios::Envelope env_b{{{0.0, 0.0}}};
    xenakios::Envelope env_iters{{{0.0, 1.0}, {4.5, 8.0}, {5.0, 1.0}}};
    xenakios::Envelope env_hz{{{0.0, 1.0}, {0.5, 0.5}, {2.5, 1.0}, {4.0, 0.01}}};
    for (auto &pt : env_hz)
        pt = pt.withShape(xenakios::EnvelopePoint::Shape::Hold).withP0(0.5);
    double phase = 0.0;
    double max_gain = 0.0;
    for (int i = 0; i < outlen; ++i)
    {
        double time = i / sr;
        if (blockcounter == 0)
        {
            a = env_a.getValueAtPosition(time);
            b = 7 + 2 * std::floor(6 * env_b.getValueAtPosition(time));
            hz = env_hz.getValueAtPosition(time);
        }
        double x = weierstrass(phase, a, b, 8);
        outbuf.getSample(0, i) = x;
        if (std::abs(x) > max_gain)
            max_gain = std::abs(x);
        ++blockcounter;
        if (blockcounter == blocklen)
            blockcounter = 0;
        phase += (2 * M_PI) / sr * hz;
        // if (phase >= 2 * M_PI)
        //     phase -= 2 * M_PI;
    }
    std::print("max gain {}\n", max_gain);
    choc::buffer::applyGain(outbuf, 1.0 / max_gain);
    writer->appendFrames(outbuf.getView());
}
