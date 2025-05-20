#include <string>
#include <cmath>
#include "include/sst/waveshapers/WaveshaperConfiguration.h"
#include "include/sst/waveshapers/WaveshaperTables.h"
#include "sst/basic-blocks/simd/setup.h"
#include "include/sst/waveshapers.h"
#include <iostream>
#include <print>
#include "CLI11.hpp"
#include "audio/choc_AudioFileFormat.h"
#include "audio/choc_SampleBuffers.h"
#include "audio/choc_AudioFileFormat_WAV.h"
#include "../Common/xap_breakpoint_envelope.h"
#include "xcli_utils.h"

inline void process_waveshaper(sst::waveshapers::QuadWaveshaperPtr wsptr, float *osleftchannel,
                               float *osrightchannel, double ingain, unsigned int blocksize,
                               unsigned int osfactor, auto &gain_smoother, auto &wss)
{
    float din alignas(16)[4] = {0, 0, 0, 0};
    for (int i = 0; i < blocksize * osfactor; ++i)
    {
        alignas(16) double smoothedgain = gain_smoother.process(ingain);
        din[0] = osleftchannel[i];
        din[1] = osrightchannel[i];
        auto dat = _mm_load_ps(din);
        auto drv = _mm_set1_ps(smoothedgain);

        dat = wsptr(&wss, dat, drv);

        float res alignas(16)[4];

        _mm_store_ps(res, dat);
#ifdef DETECT_WS_BAD
        if (res[0] < -2.0 || res[0] > 2.0 || std::isnan(res[0]) || std::isinf(res[0]) ||
            res[1] < -2.0 || res[1] > 2.0 || std::isinf(res[1]) || std::isnan(res[1]))
        {
            std::print("bad sample produced by waveshaper {} {}\n", res[0], res[1]);
        }
#endif
        osleftchannel[i] = res[0];
        osrightchannel[i] = res[1];
    }
}

inline int render_waveshaper(std::string infile, std::string outfile,
                             xenakios::Envelope &wstype_env, xenakios::Envelope &ingain_env)
{
    choc::audio::WAVAudioFileFormat<true> wavformat;
    auto reader = wavformat.createReader(infile);
    if (!reader)
    {
        std::cout << "can't open input file\n";
        return 1;
    }
    auto inprops = reader->getProperties();
    choc::audio::AudioFileProperties outprops;
    outprops.bitDepth = choc::audio::BitDepth::float32;
    outprops.numChannels = inprops.numChannels;
    outprops.sampleRate = inprops.sampleRate;
    auto writer = wavformat.createWriter(outfile, outprops);
    if (!writer)
    {
        std::cout << "can't open output file\n";
        return 1;
    }
    sst::waveshapers::QuadWaveshaperState wss{};
    float R[sst::waveshapers::n_waveshaper_registers];
    int oldtype = (int)sst::waveshapers::WaveshaperType::wst_none;

    unsigned int blocksize = 64;
    choc::buffer::ChannelArrayBuffer<float> readbuffer{inprops.numChannels, blocksize};
    readbuffer.clear();
    choc::buffer::ChannelArrayBuffer<float> writebuffer{inprops.numChannels, blocksize};
    writebuffer.clear();
    int outcounter = 0;
    auto wsptr = sst::waveshapers::GetQuadWaveshaper(sst::waveshapers::WaveshaperType::wst_none);
    dsp::BiquadFilter gain_smoother;
    gain_smoother.setParameters(dsp::BiquadFilter::LOWPASS_1POLE, 10.0 / inprops.sampleRate, 1.0,
                                1.0f);

    constexpr int osfactor = 4;
    auto oversampler = std::make_unique<OverSampler<osfactor, 16>>(inprops.numChannels, blocksize);
    while (outcounter < inprops.numFrames)
    {
        auto osleftchannel = oversampler->oversampledbuffer.getView().data.channels[0];
        auto osrightchannel =
            oversampler->oversampledbuffer.getView().data.channels[1 % inprops.numChannels];

        double tpos = outcounter / inprops.sampleRate;
        int next_type = wstype_env.getValueAtPosition(tpos);
        next_type = std::clamp(next_type, 1, 44);
        if (next_type != oldtype)
        {
            // std::cout << "switching to " << sst::waveshapers::wst_names[next_type] << "\n";
            oldtype = next_type;
            auto wstype = sst::waveshapers::WaveshaperType(oldtype);
            initializeWaveshaperRegister(wstype, R);
            for (int i = 0; i < sst::waveshapers::n_waveshaper_registers; ++i)
            {
                wss.R[i] = _mm_set1_ps(R[i]);
            }
            wss.init = _mm_cmpneq_ps(_mm_setzero_ps(), _mm_setzero_ps());
            wsptr = sst::waveshapers::GetQuadWaveshaper(wstype);
            
            struct DummySmoother
            {
                float process(float x) { return x; }
            };
            DummySmoother smth;
            oversampler->oversampledbuffer.clear();
            process_waveshaper(wsptr, osleftchannel, osrightchannel, 0.0, blocksize, osfactor, smth,
                               wss);
        }
        double ingain = ingain_env.getValueAtPosition(tpos);
        ingain = std::clamp(ingain, -60.0, 60.0);
        ingain = xenakios::decibelsToGain(ingain);
        auto readview = readbuffer.getView();
        reader->readFrames(outcounter, readview);
        oversampler->pushBuffer(readview);

        auto *leftData = readbuffer.getView().data.channels[0];
        auto *rightData = readbuffer.getView().data.channels[1 % inprops.numChannels];
        process_waveshaper(wsptr, osleftchannel, osrightchannel, ingain, blocksize, osfactor,
                           gain_smoother, wss);
        oversampler->downSample(readview);
        writer->appendFrames(readbuffer.getView());
        outcounter += blocksize;
    }
    return 0;
}

int main(int argc, char **argv)
{
    CLI::App app{"modifyspeed"};
    argv = app.ensure_utf8(argv);
    std::string infile;
    std::string outfile;
    std::string ingainstring;
    std::string wstypestring;

    app.add_option("-i", infile, "Input file");
    app.add_option("-o", outfile, "Output file");
    app.add_option("--ws", wstypestring, "Waveshaper type");
    app.add_option("--ingain", ingainstring, "Input gain");
    CLI11_PARSE(app, argc, argv);

    xenakios::Envelope ws_envelope;
    init_envelope_from_string(ws_envelope, wstypestring);
    xenakios::Envelope ingain_envelope;
    init_envelope_from_string(ingain_envelope, ingainstring);
    render_waveshaper(infile, outfile, ws_envelope, ingain_envelope);
}
