#include "../Common/xap_utils.h"
#include "../Common/xap_breakpoint_envelope.h"
#include "../Common/clap_eventsequence.h"
#include "../Common/xen_modulators.h"
#include <istream>
#include <print>
#include <array>
#include "AirwinRegistry.h"
#include "audio/choc_AudioFileFormat.h"
#include "audio/choc_AudioFileFormat_WAV.h"
#include "audio/choc_SampleBuffers.h"
#include "clap/events.h"
#include <complex>


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

inline void dump_airwindows_info()
{
    char textbuf[2048];
    std::ofstream ofs(
        R"(C:\develop\AudioPluginHost_mk2\Source\Experimental\airwindowsplugins.txt)");
    auto &reg = AirwinRegistry::registry;
    for (size_t i = 0; i < reg.size(); ++i)
    {
        auto plug = reg[i].generator();
        if (plug)
        {
            ofs << i << "\t" << reg[i].name << "\n";
            for (int j = 0; j < reg[i].nParams; ++j)
            {
                plug->getParameterName(j, textbuf);
                ofs << "\t" << j << "\t" << textbuf << "\n";
            }
        }
    }
}

void test_airwin_registry()
{
    char textbuf[2048];
    auto &reg = AirwinRegistry::registry;
    int plugId = 182;
    auto plug = reg[plugId].generator();
    if (plug)
    {
        ClapEventSequence seq;
        seq.addParameterEvent(false, 0.0, -1, -1, -1, -1, 3, 0.2);
        seq.addParameterEvent(false, 1.0, -1, -1, -1, -1, 3, 1.0);
        seq.addParameterEvent(false, 2.0, -1, -1, -1, -1, 3, 0.2);
        seq.addParameterEvent(false, 3.0, -1, -1, -1, -1, 3, 1.0);
        xenakios::Envelope par0env{
            {{0.0, 0.0}, {0.5, 1.0}, {2.0, 0.0}, {3.0, 0.0}, {3.2, 1.0}, {3.4, 0.0}}};
        choc::audio::WAVAudioFileFormat<false> informat;
        auto reader = informat.createReader(R"(C:\MusicAudio\sourcesamples\sheila.wav)");
        auto &inprops = reader->getProperties();
        ClapEventSequence::IteratorSampleTime eviter(seq, inprops.sampleRate);
        unsigned int blockSize = 64;
        choc::buffer::ChannelArrayBuffer<float> inbuf{inprops.numChannels, blockSize};
        choc::buffer::ChannelArrayBuffer<float> outbuf{2, blockSize};
        plug->setNumInputs(2);
        plug->setNumOutputs(2);
        plug->setSampleRate(inprops.sampleRate);
        auto writer = xenakios::createWavWriter(
            R"(C:\develop\AudioPluginHost_mk2\Source\Experimental\awtest01.wav)", 2,
            inprops.sampleRate);
        int infilepos = 0;
        int tailLen = inprops.sampleRate * 5.0;
        while (infilepos < inprops.numFrames + tailLen)
        {
            reader->readFrames(infilepos, inbuf.getView());

            if (inprops.numChannels == 1)
            {
                choc::buffer::copy(outbuf.getChannel(0), inbuf.getChannel(0));
                choc::buffer::copy(outbuf.getChannel(1), inbuf.getChannel(0));
            }
            else
            {
                choc::buffer::copy(outbuf.getChannel(0), inbuf.getChannel(0));
                choc::buffer::copy(outbuf.getChannel(1), inbuf.getChannel(1));
            }
            auto evts = eviter.readNextEvents(blockSize);
            for (const auto &e : evts)
            {
                if (e.event.header.type == CLAP_EVENT_PARAM_VALUE)
                {
                    auto pev = (clap_event_param_value *)&e.event.header;
                    plug->setParameter(pev->param_id, pev->value);
                }
            }
            // double tpos = infilepos / inprops.sampleRate;
            // plug->setParameter(3, par0env.getValueAtPosition(tpos));
            plug->processReplacing((float **)outbuf.getView().data.channels,
                                   (float **)outbuf.getView().data.channels, blockSize);
            writer->appendFrames(outbuf.getView());
            infilepos += blockSize;
        }
    }
}

void test_alt_multilfo()
{
    double sr = 44100;
    AltMultiModulator modulator{sr};
    auto writer = xenakios::createWavWriter(
        R"(C:\develop\AudioPluginHost_mk2\python\cdp8\multilfo.wav)", 2, sr);
    unsigned long outnumsamples = sr * 4.0;
    choc::buffer::ChannelArrayBuffer<float> outbuf{2, outnumsamples + 64};
    outbuf.clear();
    unsigned long outcounter = 0;
    auto blocksize = AltMultiModulator::BLOCKSIZE;
    modulator.mod_matrix[AltMultiModulator::MS_LFO0][AltMultiModulator::MD_Output0] = 1.00;
    modulator.mod_matrix[AltMultiModulator::MS_LFO2][AltMultiModulator::MD_Output1] = 1.00;
    modulator.mod_matrix[AltMultiModulator::MS_LFO1][AltMultiModulator::MD_LFO0Rate] = 1.0;
    modulator.lfo_rates[AltMultiModulator::MS_LFO0] = 2.0;
    modulator.lfo_rates[AltMultiModulator::MS_LFO1] = 0.4;
    modulator.lfo_rates[AltMultiModulator::MS_LFO2] = 3.5;
    modulator.lfo_shapes[AltMultiModulator::MS_LFO1] = AltMultiModulator::lfo_t::Shape::SINE;
    modulator.lfo_unipolars[AltMultiModulator::MS_LFO1] = 1.0;
    modulator.lfo_shapes[AltMultiModulator::MS_LFO2] = AltMultiModulator::lfo_t::Shape::SH_NOISE;
    while (outcounter < outnumsamples)
    {
        modulator.process_block();
        for (int i = 0; i < blocksize; ++i)
        {
            for (int j = 0; j < 2; ++j)
            {
                outbuf.getSample(j, outcounter + i) = modulator.output_values[j];
            }
        }
        outcounter += blocksize;
    }
    writer->appendFrames(outbuf.getView());
}

void print_mandelbrot()
{
    size_t w = 120;
    size_t h = 40;
    std::vector<char> framebuffer(w * h);
    size_t num_iters = 100;
    double bound = 2.0;
    for (size_t x = 0; x < w; ++x)
    {
        for (size_t y = 0; y < h; ++y)
        {
            std::complex<double> c{-2.0 + 4.0 / w * x, -1.0 + 2.0 / h * y};
            std::complex<double> z0;
            for (size_t i = 0; i < num_iters; ++i)
            {
                auto z1 = std::pow(z0, 2.0) + c;
                z0 = z1;
                if (std::abs(z1) > bound)
                {
                    framebuffer[w * y + x] = ' ';
                    break;
                }
                else
                    framebuffer[w * y + x] = '*';
            }
        }
    }
    for (size_t y = 0; y < h; ++y)
    {
        for (size_t x = 0; x < w; ++x)
        {
            std::cout << framebuffer[w * y + x];
        }
        std::cout << "\n";
    }
}
