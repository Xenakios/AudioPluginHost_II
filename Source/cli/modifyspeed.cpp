#include "audio/choc_AudioFileFormat.h"
#include "audio/choc_SampleBuffers.h"
#include "audio/choc_AudioFileFormat_WAV.h"
#include <exception>
#include <filesystem>
#include <iostream>
#include <print>
#include "signalsmith-stretch.h"
#include "CLI11.hpp"

inline int render_signal_smith(std::string infile, std::string outfile, double timefactor,
                               double pitchfactor)
{
    choc::audio::WAVAudioFileFormat<true> wavformat;
    auto reader = wavformat.createReader(infile);
    if (!reader)
        return 1;

    auto inprops = reader->getProperties();
    std::print("infile [samplerate {} Hz] [length {} seconds]\n", inprops.sampleRate,
               inprops.numFrames / inprops.sampleRate);
    unsigned int blockSize = 512;
    auto stretch = std::make_unique<signalsmith::stretch::SignalsmithStretch<float>>();
    stretch->presetDefault(inprops.numChannels, inprops.sampleRate);
    choc::audio::AudioFileProperties outprops;
    outprops.sampleRate = inprops.sampleRate;
    outprops.bitDepth = choc::audio::BitDepth::float32;
    outprops.numChannels = inprops.numChannels;
    auto writer = wavformat.createWriter(outfile, outprops);
    if (!writer)
        return 1;
    choc::buffer::ChannelArrayBuffer<float> readbuffer{inprops.numChannels, blockSize * 16};
    readbuffer.clear();
    choc::buffer::ChannelArrayBuffer<float> writebuffer{inprops.numChannels, blockSize};
    writebuffer.clear();
    int inposcounter = 0;
    stretch->setTransposeFactor(pitchfactor);
    while (inposcounter < inprops.numFrames)
    {
        unsigned int framesToRead = timefactor * blockSize;
        auto inview = readbuffer.getFrameRange({0, framesToRead});
        reader->readFrames(inposcounter, inview);
        stretch->process(inview.data.channels, framesToRead, writebuffer.getView().data.channels,
                         blockSize);
        writer->appendFrames(writebuffer.getView());
        inposcounter += framesToRead;
    }
    return 0;
}

int main(int argc, char **argv)
{
    CLI::App app{"App description"};
    argv = app.ensure_utf8(argv);
    auto sscommand = app.add_subcommand("signalsmith");
    auto rscommand = app.add_subcommand("varispeed");
    app.require_subcommand();
    std::string infilename;
    std::string outfilename;
    double strechfactor = 1.0;
    std::optional<double> pitchfactor;
    std::optional<double> pitchsemis;
    sscommand->add_option("-i,--infile", infilename, "A help string");
    sscommand->add_option("-o,--outfile", outfilename, "A help string");
    sscommand->add_option("--timefactor", strechfactor, "A help string");
    sscommand->add_option("--pitchfactor", pitchfactor, "A help string");
    sscommand->add_option("--pitchsemis", pitchsemis, "A help string");
    CLI11_PARSE(app, argc, argv);
    if (app.got_subcommand("signalsmith"))
    {
        if (!std::filesystem::exists(infilename))
        {
            std::cout << "input file " << infilename << " does not exist\n";
            return 1;
        }
        std::cout << "processing with signalsmith stretch...\n";
        std::cout << infilename << " -> " << outfilename << "\n";
        std::cout << strechfactor << " time stretch\n";
        if (pitchfactor)
            std::cout << "pitch factor " << *pitchfactor << "\n";
        if (pitchsemis)
        {
            std::cout << "pitch semitones " << *pitchsemis << "\n";
            pitchfactor = std::pow(2.0, 1.0 / 12 * *pitchsemis);
        }
        strechfactor = std::clamp(strechfactor, 0.01, 15.9);
        *pitchfactor = std::clamp(*pitchfactor, 0.125, 8.0);
        render_signal_smith(infilename, outfilename, strechfactor, *pitchfactor);
    }
    if (app.got_subcommand("varispeed"))
    {
        std::cout << "processing with resampling varispeed...\n";
    }
    return 0;
}
