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
#include "../Common/xapdsp.h"
#include "xcli_utils.h"

enum PARAMS
{
    PAR_DRIVE = 0,
    PAR_TYPE,
    PAR_OUTGAIN,
    PAR_END
};

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
                             std::vector<xenakios::Envelope> &envelopes)
{
    auto t0 = std::chrono::high_resolution_clock::now();
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
    choc::buffer::ChannelArrayBuffer<float> xfadebuffer{inprops.numChannels, blocksize};
    xfadebuffer.clear();
    StereoSimperSVF highpassfilter;
    highpassfilter.setCoeff(12.0f, 0.01f, 1.0 / inprops.sampleRate);
    highpassfilter.init();
    int outcounter = 0;
    auto wsptr = sst::waveshapers::GetQuadWaveshaper(sst::waveshapers::WaveshaperType::wst_none);
    dsp::BiquadFilter drive_smoother;
    drive_smoother.setParameters(dsp::BiquadFilter::LOWPASS_1POLE, 10.0 / inprops.sampleRate, 1.0,
                                 1.0f);
    dsp::BiquadFilter outgain_smoother;
    outgain_smoother.setParameters(dsp::BiquadFilter::LOWPASS_1POLE, 10.0 / inprops.sampleRate, 1.0,
                                   1.0f);
    constexpr int osfactor = 2;
    auto oversampler = std::make_unique<OverSampler<osfactor, 16>>(inprops.numChannels, blocksize);
    auto t1 = std::chrono::high_resolution_clock::now();
    auto t2 = std::chrono::high_resolution_clock::now();
    while (outcounter < inprops.numFrames)
    {
        auto readview = readbuffer.getView();
        reader->readFrames(outcounter, readview);
        auto osleftchannel = oversampler->oversampledbuffer.getView().data.channels[0];
        auto osrightchannel =
            oversampler->oversampledbuffer.getView().data.channels[1 % inprops.numChannels];

        double tpos = outcounter / inprops.sampleRate;
        int next_type = envelopes[PAR_TYPE].getValueAtPosition(tpos);
        next_type = std::clamp(next_type, 1, (int)sst::waveshapers::WaveshaperType::n_ws_types - 1);
        bool do_xfade = false;
        if (next_type != oldtype)
        {
            do_xfade = true;
            // std::cout << "switching to " << sst::waveshapers::wst_names[next_type] << "\n";
            oldtype = next_type;
            auto wstype = sst::waveshapers::WaveshaperType(oldtype);
            initializeWaveshaperRegister(wstype, R);
            for (int i = 0; i < sst::waveshapers::n_waveshaper_registers; ++i)
            {
                wss.R[i] = _mm_set1_ps(R[i]);
            }
            wss.init = _mm_cmpeq_ps(_mm_setzero_ps(), _mm_setzero_ps());
            wsptr = sst::waveshapers::GetQuadWaveshaper(wstype);
        }
        double ingain = envelopes[PAR_DRIVE].getValueAtPosition(tpos);
        ingain = std::clamp(ingain, -60.0, 60.0);
        ingain = xenakios::decibelsToGain(ingain);

        oversampler->pushBuffer(readview);

        auto *leftData = readbuffer.getView().data.channels[0];
        auto *rightData = readbuffer.getView().data.channels[1 % inprops.numChannels];
        process_waveshaper(wsptr, osleftchannel, osrightchannel, ingain, blocksize, osfactor,
                           drive_smoother, wss);
        oversampler->downSample(readview);
        double ogain = envelopes[PAR_OUTGAIN].getValueAtPosition(tpos);
        ogain = std::clamp(ogain, -101.0, 24.0);
        ogain = xenakios::decibelsToGain(ogain);
        for (int i = 0; i < blocksize; ++i)
        {
            double smoothedgain = outgain_smoother.process(ogain);
            leftData[i] *= smoothedgain;
            rightData[i] *= smoothedgain;
        }
        writer->appendFrames(readbuffer.getView());
        outcounter += blocksize;
    }
    auto t3 = std::chrono::high_resolution_clock::now();
    auto setupmillis = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    auto rendermillis = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();
    double rtfactor = (inprops.numFrames / inprops.sampleRate * 1000.0) / rendermillis;
    std::print("setup took {} milliseconds, render took {} milliseconds {:.2f}x real time\n",
               setupmillis, rendermillis, rtfactor);
    return 0;
}

struct ParamInfo
{
    int id = 0;
    std::string envfn_or_value;
};

int main(int argc, char **argv)
{
    CLI::App app{"modifyspeed"};
    argv = app.ensure_utf8(argv);
    app.require_option();
    std::string infile;
    std::string outfile;
    std::string ingainstring = "0.0";
    std::string wstypestring = "1";
    std::string outgainstring = "-24.0";
    bool list_shaper_types = false;
    app.add_option("-i", infile, "Input file")->mandatory(false);
    app.add_option("-o", outfile, "Output file")->mandatory(false);
    app.add_option("--ws", wstypestring, "Waveshaper type");
    app.add_option("--ingain", ingainstring, "Input gain");
    app.add_option("--outgain", outgainstring, "Output gain");
    app.add_flag("--types", list_shaper_types, "List waveshaper types");
    CLI11_PARSE(app, argc, argv);
    if (list_shaper_types)
    {
        for (size_t i = 1; i < (size_t)sst::waveshapers::WaveshaperType::n_ws_types; ++i)
        {
            std::print("{:2} : {:30}", i, sst::waveshapers::wst_names[i]);
            if (i % 3 == 0)
                std::print("\n");
        }
        return 0;
    }
    std::vector<xenakios::Envelope> envelopes;
    envelopes.resize(PAR_END);
    init_envelope_from_string(envelopes[PAR_TYPE], wstypestring, "waveshaper_type");
    init_envelope_from_string(envelopes[PAR_DRIVE], ingainstring, "drive");
    init_envelope_from_string(envelopes[PAR_OUTGAIN], outgainstring, "out_gain");
    for (auto &e : envelopes)
    {
        if (e.getNumPoints() == 0)
        {
            std::cout << "parameter " << e.envelope_id << " was not initialized\n";
            return 1;
        }
    }
    return render_waveshaper(infile, outfile, envelopes);
}
