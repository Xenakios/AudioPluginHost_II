#include "audio/choc_AudioFileFormat.h"
#include "audio/choc_SampleBuffers.h"
#include "audio/choc_AudioFileFormat_WAV.h"
#include <exception>
#include <filesystem>
#include <iostream>
#include <print>
#include "signalsmith-stretch.h"
#include "CLI11.hpp"
#include "../Common/xap_breakpoint_envelope.h"

inline int render_signalsmith(std::string infile, std::string outfile,
                              xenakios::Envelope &rate_envelope, xenakios::Envelope &pitch_envelope,
                              xenakios::Envelope &formant_envelope)
{
    if (rate_envelope.getNumPoints() == 0)
        rate_envelope.addPoint({0.0, 1.0});
    if (pitch_envelope.getNumPoints() == 0)
        pitch_envelope.addPoint({0.0, 1.0});
    if (formant_envelope.getNumPoints() == 0)
        formant_envelope.addPoint({0.0, 1.0});
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
    choc::buffer::ChannelArrayBuffer<float> readbuffer{inprops.numChannels, blockSize * 17};
    readbuffer.clear();
    choc::buffer::ChannelArrayBuffer<float> writebuffer{inprops.numChannels, blockSize};
    writebuffer.clear();
    int inposcounter = 0;
    while (inposcounter < inprops.numFrames)
    {
        double tpos = inposcounter / inprops.sampleRate;
        double pfac = pitch_envelope.getValueAtPosition(tpos);
        pfac = std::clamp(pfac, 0.125, 8.0);
        stretch->setTransposeFactor(pfac);
        double forfac = formant_envelope.getValueAtPosition(tpos);
        forfac = std::clamp(forfac, 0.125, 8.0);
        stretch->setFormantFactor(forfac, true);
        double timefactor = rate_envelope.getValueAtPosition(tpos);
        timefactor = std::clamp(timefactor, 0.01, 16.0);
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

    std::vector<double> pitchautomation;
    std::vector<double> rate_automation;
    std::vector<double> formant_automation;
    bool allow_overwrite = false;
    bool rate_in_octaves = false;
    sscommand->add_option("-i,--infile", infilename, "Input WAV file");
    sscommand->add_option("-o,--outfile", outfilename, "Output WAV file (32 bit float)");
    sscommand->add_option("--rate", rate_automation, "Time stretch amount (factor/octave)");
    sscommand->add_option("--pitch", pitchautomation, "Pitch shift (semitones)");
    sscommand->add_option("--formant", formant_automation, "Formant shift (semitones)");
    sscommand->add_flag("--overwrite", allow_overwrite, "Overwrite output file even if it exists");
    sscommand->add_flag("--roct", rate_in_octaves, "Play rate is in time octaves");
    CLI11_PARSE(app, argc, argv);
    if (app.got_subcommand("signalsmith"))
    {
        if (!std::filesystem::exists(infilename))
        {
            std::cout << "input file " << infilename << " does not exist\n";
            return 1;
        }
        if (std::filesystem::exists(outfilename))
        {
            std::cout << "output file already exists...\n";
            if (allow_overwrite)
                std::cout << "but will overwrite it\n";
            else
                return 1;
        }
        std::cout << "processing with signalsmith stretch...\n";
        std::cout << infilename << " -> " << outfilename << "\n";
        xenakios::Envelope rate_envelope;
        xenakios::Envelope pitch_envelope;
        xenakios::Envelope formant_envelope;
        if (pitchautomation.size() == 1)
        {
            pitch_envelope.addPoint({0.0, std::pow(2.0, 1.0 / 12 * pitchautomation[0])});
        }
        if (pitchautomation.size() > 1 && pitchautomation.size() % 2 == 0)
        {
            std::cout << "command line has pitch automation\n";
            for (int i = 0; i < pitchautomation.size(); i += 2)
            {
                std::cout << pitchautomation[i] << "\t" << pitchautomation[i + 1] << "\n";
                pitch_envelope.addPoint(
                    {pitchautomation[i], std::pow(2.0, 1.0 / 12.0 * pitchautomation[i + 1])});
            }
        }

        if (formant_automation.size() == 1)
        {
            formant_envelope.addPoint({0.0, std::pow(2.0, 1.0 / 12 * formant_automation[0])});
        }

        if (formant_automation.size() > 1 && formant_automation.size() % 2 == 0)
        {
            std::cout << "command line has formant automation\n";
            for (int i = 0; i < formant_automation.size(); i += 2)
            {
                std::cout << formant_automation[i] << "\t" << formant_automation[i + 1] << "\n";
                formant_envelope.addPoint(
                    {formant_automation[i], std::pow(2.0, 1.0 / 12.0 * formant_automation[i + 1])});
            }
        }

        if (rate_automation.size() == 1)
        {
            if (!rate_in_octaves)
                rate_envelope.addPoint({0.0, rate_automation[0]});
            else
                rate_envelope.addPoint({0.0, std::pow(2.0, rate_automation[0])});
        }
        if (rate_automation.size() > 1 && rate_automation.size() % 2 == 0)
        {
            std::cout << "command line has rate automation\n";
            for (int i = 0; i < rate_automation.size(); i += 2)
            {
                std::cout << rate_automation[i] << "\t" << rate_automation[i + 1] << "\n";
                if (!rate_in_octaves)
                    rate_envelope.addPoint({rate_automation[i], rate_automation[i + 1]});
                else
                    rate_envelope.addPoint(
                        {rate_automation[i], std::pow(2.0, rate_automation[i + 1])});
            }
        }
        render_signalsmith(infilename, outfilename, rate_envelope, pitch_envelope,
                           formant_envelope);
    }
    if (app.got_subcommand("varispeed"))
    {
        std::cout << "processing with resampling varispeed...\n";
    }
    return 0;
}
