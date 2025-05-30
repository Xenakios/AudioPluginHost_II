#include "audio/choc_AudioFileFormat.h"
#include "audio/choc_SampleBuffers.h"
#include "audio/choc_AudioFileFormat_WAV.h"
#include <algorithm>
#include <exception>
#include <filesystem>
#include <iostream>
#include <print>
#include <stdexcept>
#include "signalsmith-stretch.h"
#include "CLI11.hpp"
#include "../Common/xap_breakpoint_envelope.h"
#include "text/choc_Files.h"
#include "text/choc_StringUtilities.h"
#include "sst\basic-blocks\dsp\LanczosResampler.h"
#include "scrub.h"
#include "xcli_utils.h"

inline int render_scrub(std::string infile, std::string outfile, xenakios::Envelope &scan_env,
                        bool use_sinc)
{
    if (scan_env.getNumPoints() < 2)
    {
        std::cout << "error : scan envelope should have at least 2 breakpoints\n";
        return 1;
    }

    choc::audio::WAVAudioFileFormat<true> wavformat;
    auto reader = wavformat.createReader(infile);
    if (!reader)
    {
        std::cout << "could not create reader for " << infile << "\n";
        return 1;
    }

    auto inprops = reader->getProperties();
    std::print("infile [samplerate {} Hz] [length {} seconds]\n", inprops.sampleRate,
               inprops.numFrames / inprops.sampleRate);
    choc::audio::AudioFileProperties outprops;
    outprops.sampleRate = inprops.sampleRate;
    outprops.bitDepth = choc::audio::BitDepth::float32;
    outprops.numChannels = 2;
    auto writer = wavformat.createWriter(outfile, outprops);
    if (!writer)
    {
        std::cout << "could not create writer for " << outfile << "\n";
        return 1;
    }

    choc::buffer::ChannelArrayBuffer<float> scanbuffer{
        {inprops.numChannels, (unsigned int)inprops.numFrames}};
    scanbuffer.clear();
    reader->readFrames(0, scanbuffer.getView());
    auto scrubber = std::make_unique<BufferScrubber>(scanbuffer, inprops.sampleRate);
    if (use_sinc)
        scrubber->m_resampler_type = 1;
    unsigned int blocksize = 128;
    int outcounter = 0;
    int outlen =
        (scan_env.getPointSafe(scan_env.getNumPoints() - 1).getX() + 0.5) * inprops.sampleRate;
    if (outlen < blocksize)
        return 1;
    choc::buffer::ChannelArrayBuffer<float> writebuffer{{2, (unsigned int)blocksize}};
    writebuffer.clear();
    // scrubber->setSeparation(0.51);
    while (outcounter < outlen)
    {
        double tpos = outcounter / inprops.sampleRate;
        double scanpos = scan_env.getValueAtPosition(tpos);
        scanpos = std::clamp(scanpos, 0.0, 1.0);
        scrubber->setNextPosition(scanpos);
        float outputs[2];
        for (int i = 0; i < blocksize; ++i)
        {
            scrubber->processFrame(outputs, inprops.numChannels, inprops.sampleRate, 2.0);
            writebuffer.getSample(0, i) = outputs[0];
            writebuffer.getSample(1, i) = outputs[1];
        }
        writer->appendFrames(writebuffer);
        outcounter += blocksize;
    }
    return 0;
}

inline int render_varispeed(std::string infile, std::string outfile,
                            xenakios::Envelope &pitch_envelope)
{
    if (pitch_envelope.getNumPoints() == 0)
        pitch_envelope.addPoint({0.0, 0.0});
    choc::audio::WAVAudioFileFormat<true> wavformat;
    auto reader = wavformat.createReader(infile);
    if (!reader)
        return 1;
    auto resampler = std::make_unique<sst::basic_blocks::dsp::LanczosResampler<128>>(1.0, 1.0);

    auto inprops = reader->getProperties();
    std::print("infile [samplerate {} Hz] [length {} seconds]\n", inprops.sampleRate,
               inprops.numFrames / inprops.sampleRate);
    unsigned int blockSize = 128;
    choc::audio::AudioFileProperties outprops;
    outprops.sampleRate = inprops.sampleRate;
    outprops.bitDepth = choc::audio::BitDepth::float32;
    outprops.numChannels = inprops.numChannels;

    std::unique_ptr<choc::audio::AudioFileWriter> writer;
    writer = wavformat.createWriter(outfile, outprops);
    if (!writer)
        return 1;
    choc::buffer::ChannelArrayBuffer<float> readbuffer{inprops.numChannels, blockSize * 33};
    readbuffer.clear();
    choc::buffer::ChannelArrayBuffer<float> writebuffer{inprops.numChannels, blockSize};
    writebuffer.clear();
    choc::buffer::ChannelArrayBuffer<float> processbuffer{2, blockSize};
    processbuffer.clear();
    int inposcounter = 0;
    int outposcounter = 0;
    while (inposcounter < inprops.numFrames)
    {
        double tpos = inposcounter / inprops.sampleRate;
        double rate = pitch_envelope.getValueAtPosition(tpos);
        rate = std::clamp(rate, -60.0, 60.0);
        rate = std::pow(2.0, 1.0 / 12 * rate);
        resampler->sri = inprops.sampleRate;
        resampler->sro = inprops.sampleRate / rate;
        resampler->dPhaseO = inprops.sampleRate / resampler->sro;
        unsigned int framestoread = resampler->inputsRequiredToGenerateOutputs(blockSize);
        auto inview = readbuffer.getFrameRange({0, framestoread});
        reader->readFrames(inposcounter, inview);
        for (int i = 0; i < framestoread; ++i)
        {
            if (inprops.numChannels == 1)
            {
                resampler->push(inview.getSample(0, i), 0.0f);
            }
            if (inprops.numChannels == 2)
            {
                resampler->push(inview.getSample(0, i), inview.getSample(1, i));
            }
        }
        float *rsoutleft = processbuffer.getView().data.channels[0];
        float *rsoutright = processbuffer.getView().data.channels[1];
        auto produced = resampler->populateNext(rsoutleft, rsoutright, blockSize);
        for (int j = 0; j < inprops.numChannels; ++j)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                writebuffer.getSample(j, i) = processbuffer.getSample(j, i);
            }
        }
        writer->appendFrames(writebuffer.getView());
        inposcounter += framestoread;
    }
    return 0;
}

inline int render_signalsmith(std::string infile, std::string outfile,
                              xenakios::Envelope &rate_envelope, bool rate_is_octaves,
                              xenakios::Envelope &pitch_envelope,
                              xenakios::Envelope &formant_envelope, bool compensate_formant_pitch,
                              bool nowrite)
{
    if (rate_envelope.getNumPoints() == 0)
        rate_envelope.addPoint({0.0, 1.0});
    if (pitch_envelope.getNumPoints() == 0)
        pitch_envelope.addPoint({0.0, 0.0});
    if (formant_envelope.getNumPoints() == 0)
        formant_envelope.addPoint({0.0, 1.0});

    choc::audio::WAVAudioFileFormat<true> wavformat;
    auto reader = wavformat.createReader(infile);
    if (!reader)
        return 1;

    auto inprops = reader->getProperties();
    std::print("infile [samplerate {} Hz] [length {} seconds]\n", inprops.sampleRate,
               inprops.numFrames / inprops.sampleRate);
    unsigned int blockSize = 128;
    auto stretch = std::make_unique<signalsmith::stretch::SignalsmithStretch<float>>();
    stretch->presetDefault(inprops.numChannels, inprops.sampleRate);
    int totlatency = stretch->inputLatency() + stretch->outputLatency();
    std::cout << "input latency " << stretch->inputLatency() << " output latency "
              << stretch->outputLatency() << "\n";
    choc::audio::AudioFileProperties outprops;
    outprops.sampleRate = inprops.sampleRate;
    outprops.bitDepth = choc::audio::BitDepth::float32;
    outprops.numChannels = inprops.numChannels;

    std::unique_ptr<choc::audio::AudioFileWriter> writer;
    if (!nowrite)
        writer = wavformat.createWriter(outfile, outprops);
    if (!nowrite && !writer)
        return 1;
    choc::buffer::ChannelArrayBuffer<float> readbuffer{inprops.numChannels, blockSize * 17};
    readbuffer.clear();
    choc::buffer::ChannelArrayBuffer<float> writebuffer{inprops.numChannels, blockSize};
    writebuffer.clear();
    int inposcounter = 0;
    int outposcounter = 0;
    while (inposcounter < inprops.numFrames + totlatency)
    {
        double tpos = inposcounter / inprops.sampleRate;
        double pitch = pitch_envelope.getValueAtPosition(tpos);
        pitch = std::clamp(pitch, -36.0, 36.0);
        double pfac = std::pow(2.0, 1.0 / 12 * pitch);
        stretch->setTransposeFactor(pfac);
        double forfac = formant_envelope.getValueAtPosition(tpos);
        forfac = std::clamp(forfac, -36.0, 36.0);
        stretch->setFormantSemitones(forfac, compensate_formant_pitch);
        double timefactor = rate_envelope.getValueAtPosition(tpos);
        if (rate_is_octaves)
        {
            timefactor = std::clamp(timefactor, -4.0, 4.0);
            timefactor = std::pow(2.0, timefactor);
        }
        else
        {
            timefactor = std::clamp(timefactor, 0.0625, 16.0);
        }
        unsigned int framesToRead = timefactor * blockSize;
        auto inview = readbuffer.getFrameRange({0, framesToRead});
        reader->readFrames(inposcounter, inview);
        if (!nowrite)
        {
            stretch->process(inview.data.channels, framesToRead,
                             writebuffer.getView().data.channels, blockSize);
            int framesToWrite = blockSize;
            if (outposcounter < totlatency)
            {
                framesToWrite = (outposcounter - totlatency) + blockSize;
                if (framesToWrite < 0)
                    framesToWrite = 0;
            }
            if (framesToWrite > 0)
            {
                unsigned int startFrame = blockSize - framesToWrite;
                unsigned int endFrame = startFrame + framesToWrite;
                auto outview = writebuffer.getFrameRange({startFrame, endFrame});
                writer->appendFrames(outview);
            }
        }

        inposcounter += framesToRead;
        outposcounter += blockSize;
    }
    std::cout << "output file length is " << outposcounter / inprops.sampleRate << " seconds\n";
    return 0;
}

int main(int argc, char **argv)
{
    CLI::App app{"modifyspeed"};
    argv = app.ensure_utf8(argv);
    auto stretchcommand = app.add_subcommand("signalsmith");
    auto varispeedcommand = app.add_subcommand("varispeed");
    auto scrubcommand = app.add_subcommand("scrub");

    app.require_subcommand();
    std::string infilename;
    std::string outfilename;

    bool allow_overwrite = false;
    bool rate_in_octaves = false;
    bool compen_formant_pitch = false;
    bool dontwrite = false;
    bool scrub_sinc = false;
    double foo_float = 1.0;
    std::string rate_string;
    std::string pitch_string = "0.0";
    std::string formant_string;
    std::string scrub_string;
    stretchcommand->add_option("-i,--infile", infilename, "Input WAV file");
    stretchcommand->add_option("-o,--outfile", outfilename, "Output WAV file (32 bit float)");
    stretchcommand->add_option("--rate", rate_string, "Time stretch amount (factor/octave)");
    stretchcommand->add_option("--pitch", pitch_string, "Pitch in semitones/breakpoint file name");
    stretchcommand->add_option("--formant", formant_string, "Formant shift (semitones)");
    stretchcommand->add_flag("--overwrite", allow_overwrite,
                             "Overwrite output file even if it exists");
    stretchcommand->add_flag("--roct", rate_in_octaves, "Play rate is in time octaves");
    stretchcommand->add_flag("--cfp", compen_formant_pitch,
                             "Attempt formant correction when pitch shifting");
    stretchcommand->add_flag(
        "--odur", dontwrite,
        "Don't write output file, only print out how long it would be with the "
        "play rate processing");

    varispeedcommand->add_option("-i,--infile", infilename, "Input WAV file");
    varispeedcommand->add_option("-o,--outfile", outfilename, "Output WAV file (32 bit float)");
    varispeedcommand->add_option("--pitch", pitch_string,
                                 "Pitch in semitones/breakpoint file name");
    varispeedcommand->add_flag("--overwrite", allow_overwrite,
                               "Overwrite output file even if it exists");

    scrubcommand->add_option("-i,--infile", infilename, "Input WAV file");
    scrubcommand->add_option("-o,--outfile", outfilename, "Output WAV file (32 bit float)");
    scrubcommand->add_option("--scan", scrub_string, "Breakpoint file with scrub curve");
    scrubcommand->add_flag("--overwrite", allow_overwrite,
                           "Overwrite output file even if it exists");
    scrubcommand->add_flag("--sinc", scrub_sinc, "Use higher quality (sinc) interpolation");
    CLI11_PARSE(app, argc, argv);

    if (app.got_subcommand("signalsmith"))
    {
        if (!std::filesystem::exists(infilename))
        {
            std::cout << "input file " << infilename << " does not exist\n";
            return 1;
        }
        auto ofstatus = can_file_be_written(outfilename, allow_overwrite);
        if (!ofstatus.first)
        {
            std::cout << ofstatus.second << "\n";
            return 1;
        }
        if (!ofstatus.second.empty())
            std::cout << ofstatus.second << "\n";
        
        xenakios::Envelope rate_envelope;
        if (!rate_string.empty())
            init_envelope_from_string(rate_envelope, rate_string, "rate");
        xenakios::Envelope pitch_envelope;
        if (!pitch_string.empty())
            init_envelope_from_string(pitch_envelope, pitch_string, "pitch");
        xenakios::Envelope formant_envelope;
        if (!formant_string.empty())
            init_envelope_from_string(formant_envelope, formant_string, "formant");
        std::cout << "processing with signalsmith stretch...\n";
        std::cout << infilename << " -> " << outfilename << "\n";
        auto err =
            render_signalsmith(infilename, outfilename, rate_envelope, rate_in_octaves,
                               pitch_envelope, formant_envelope, compen_formant_pitch, dontwrite);
        return err;
    }
    if (app.got_subcommand("varispeed"))
    {
        if (!std::filesystem::exists(infilename))
        {
            std::cout << "input file " << infilename << " does not exist\n";
            return 1;
        }
        if (std::filesystem::exists(outfilename))
        {

            if (allow_overwrite)
                std::cout << "output file already exists, but will overwrite it\n";
            else
            {
                std::cout << "output file already exists!\n";
                return 1;
            }
        }

        xenakios::Envelope pitch_envelope;
        if (!pitch_string.empty())
            init_envelope_from_string(pitch_envelope, pitch_string, "pitch");
        std::cout << "processing with resampling varispeed...\n";
        std::cout << infilename << " -> " << outfilename << "\n";
        render_varispeed(infilename, outfilename, pitch_envelope);
    }
    if (app.got_subcommand("scrub"))
    {
        if (!std::filesystem::exists(infilename))
        {
            std::cout << "input file " << infilename << " does not exist\n";
            return 1;
        }
        if (std::filesystem::exists(outfilename))
        {

            if (allow_overwrite)
                std::cout << "output file already exists, but will overwrite it\n";
            else
            {
                std::cout << "output file already exists!\n";
                return 1;
            }
        }

        xenakios::Envelope scrub_envelope;
        if (!scrub_string.empty())
            init_envelope_from_string(scrub_envelope, scrub_string, "scanpos");
        std::cout << "processing with scrub...\n";
        std::cout << infilename << " -> " << outfilename << "\n";
        render_scrub(infilename, outfilename, scrub_envelope, scrub_sinc);
    }
    return 0;
}
