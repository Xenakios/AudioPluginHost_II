#include "sst/basic-blocks/simd/setup.h"
#include <iostream>
#include "CLI11.hpp"
#include "audio/choc_AudioFileFormat.h"
#include "audio/choc_SampleBuffers.h"
#include "audio/choc_AudioFileFormat_WAV.h"
#include "include/sst/waveshapers.h"
#include "include/sst/waveshapers/WaveshaperConfiguration.h"
#include "../Common/xap_breakpoint_envelope.h"

inline int render_waveshaper(std::string infile, std::string outfile,
                             xenakios::Envelope &wstype_env)
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
    double ingain = 4.0;

    unsigned int blocksize = 64;
    choc::buffer::ChannelArrayBuffer<float> readbuffer{inprops.numChannels, blocksize};
    readbuffer.clear();
    choc::buffer::ChannelArrayBuffer<float> writebuffer{inprops.numChannels, blocksize};
    writebuffer.clear();
    int outcounter = 0;
    auto wsptr = sst::waveshapers::GetQuadWaveshaper(sst::waveshapers::WaveshaperType::wst_none);
    while (outcounter < inprops.numFrames)
    {
        double tpos = outcounter / inprops.sampleRate;
        int next_type = wstype_env.getValueAtPosition(tpos);
        next_type = std::clamp(next_type, 1, 44);
        if (next_type != oldtype)
        {
            oldtype = next_type;
            auto wstype = sst::waveshapers::WaveshaperType(oldtype);
            initializeWaveshaperRegister(wstype, R);

            for (int i = 0; i < sst::waveshapers::n_waveshaper_registers; ++i)
            {
                wss.R[i] = _mm_set1_ps(R[i]);
            }

            wss.init = _mm_cmpneq_ps(_mm_setzero_ps(), _mm_setzero_ps());
            wsptr = sst::waveshapers::GetQuadWaveshaper(wstype);
        }
        reader->readFrames(outcounter, readbuffer.getView());
        float din alignas(16)[4] = {0, 0, 0, 0};
        auto *leftData = readbuffer.getView().data.channels[0];
        auto *rightData = readbuffer.getView().data.channels[1 % inprops.numChannels];
        for (int i = 0; i < blocksize; ++i)
        {
            din[0] = leftData[i];
            din[1] = rightData[i];

            auto dat = _mm_load_ps(din);
            auto drv = _mm_set1_ps(ingain);

            dat = wsptr(&wss, dat, drv);

            float res alignas(16)[4];

            _mm_store_ps(res, dat);

            leftData[i] = res[0];
            rightData[i] = res[1];
        }
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
    int wstype = (int)sst::waveshapers::WaveshaperType::wst_none;
    app.add_option("-i", infile, "Input file");
    app.add_option("-o", outfile, "Output file");
    app.add_option("--ws", wstype, "Waveshaper type");
    CLI11_PARSE(app, argc, argv);
    wstype = std::clamp(wstype, 0, 44);
    xenakios::Envelope ws_envelope;
    ws_envelope.addPoint({0.0, 1.0});
    ws_envelope.addPoint({2.0, 44.0});
    ws_envelope.addPoint({10.0, 20.0});
    render_waveshaper(infile, outfile, ws_envelope);
}
