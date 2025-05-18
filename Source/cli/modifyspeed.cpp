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
#include "../Common/xen_modulators.h"
#include "text/choc_Files.h"
#include "text/choc_StringUtilities.h"

inline int render_signalsmith(std::string infile, std::string outfile,
                              xenakios::Envelope &rate_envelope, xenakios::Envelope &pitch_envelope,
                              xenakios::Envelope &formant_envelope, bool compensate_formant_pitch,
                              bool nowrite)
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
    auto multimod = std::make_unique<AltMultiModulator>(inprops.sampleRate);
    auto jsontxt =
        choc::file::loadFileAsString(R"(C:\develop\AudioPluginHost_mk2\python\lfosettings2.json)");
    auto tree = choc::json::parse(jsontxt);
    init_multimod_from_value(*multimod, tree);
    double pitch_base = tree["pitch"].getWithDefault(0.0);
    double pitch_mod_amt = tree["pitch_mod_depth"].getWithDefault(0.0);
    double rate_base = tree["playrate"].getWithDefault(0.0);
    double rate_mod_amt = tree["playrate_mod_depth"].getWithDefault(0.0);
    unsigned int blockSize = 64;
    auto stretch = std::make_unique<signalsmith::stretch::SignalsmithStretch<float>>();
    stretch->presetDefault(inprops.numChannels, inprops.sampleRate);
    int olatency = stretch->outputLatency();
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
    while (inposcounter < inprops.numFrames)
    {
        multimod->process_block();
        double tpos = inposcounter / inprops.sampleRate;
        // double pitch = pitch_base + pitch_mod_amt * multimod->output_values[1];
        double pitch = pitch_envelope.getValueAtPosition(tpos);
        pitch = std::clamp(pitch, -36.0, 36.0);
        // double pfac = pitch_envelope.getValueAtPosition(tpos);
        double pfac = std::pow(2.0, 1.0 / 12 * pitch);
        pfac = std::clamp(pfac, 0.125, 8.0);
        stretch->setTransposeFactor(pfac);
        double forfac = formant_envelope.getValueAtPosition(tpos);
        forfac = std::clamp(forfac, 0.125, 8.0);
        stretch->setFormantFactor(forfac, compensate_formant_pitch);
        double timefactor = rate_envelope.getValueAtPosition(tpos);
        // double timefactor = rate_base + rate_mod_amt * multimod->output_values[0];
        timefactor = std::clamp(timefactor, -4.0, 4.0);
        // timefactor = std::pow(2.0, timefactor);
        unsigned int framesToRead = timefactor * blockSize;
        auto inview = readbuffer.getFrameRange({0, framesToRead});
        reader->readFrames(inposcounter, inview);
        if (!nowrite)
        {
            stretch->process(inview.data.channels, framesToRead,
                             writebuffer.getView().data.channels, blockSize);
            int framesToWrite = blockSize;
            if (outposcounter < olatency)
            {
                framesToWrite = (outposcounter - olatency) + blockSize;
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
    if (nowrite)
        std::cout << "output file would have length of " << outposcounter / inprops.sampleRate
                  << " seconds\n";
    return 0;
}

inline void init_envelope_from_string(xenakios::Envelope &env, std::string str)
{
    try
    {
        if (std::filesystem::exists(str))
        {
            std::cout << "loading " << str << " as envelope\n";
            auto envtxt = choc::file::loadFileAsString(str);
            auto lines = choc::text::splitIntoLines(envtxt, false);
            for (const auto &line : lines)
            {
                auto tokens = choc::text::splitString(line, ' ', false);
                if (tokens.size() >= 2)
                {
                    double time = std::stod(tokens[0]);
                    double value = std::stod(tokens[1]);
                    env.addPoint({time, value});
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
    bool compen_formant_pitch = false;
    bool dontwrite = false;
    double foo_float = 1.0;
    std::string foo_string;
    sscommand->add_option("-i,--infile", infilename, "Input WAV file");
    sscommand->add_option("-o,--outfile", outfilename, "Output WAV file (32 bit float)");
    sscommand->add_option("--rate", rate_automation, "Time stretch amount (factor/octave)");
    sscommand->add_option("--pitch", pitchautomation, "Pitch shift (semitones)");
    sscommand->add_option("--formant", formant_automation, "Formant shift (semitones)");
    sscommand->add_flag("--overwrite", allow_overwrite, "Overwrite output file even if it exists");
    sscommand->add_flag("--roct", rate_in_octaves, "Play rate is in time octaves");
    sscommand->add_flag("--cfp", compen_formant_pitch,
                        "Attempt formant correction when pitch shifting");
    sscommand->add_flag("--odur", dontwrite,
                        "Don't write output file, only print out how long it would be with the "
                        "play rate processing");

    sscommand->add_flag("--foo", foo_string, "Foo string");
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
        init_envelope_from_string(pitch_envelope, foo_string);
        xenakios::Envelope formant_envelope;
        if (pitchautomation.size() == 1)
        {
            // pitch_envelope.addPoint({0.0, std::pow(2.0, 1.0 / 12 * pitchautomation[0])});
        }
        if (pitchautomation.size() > 1 && pitchautomation.size() % 2 == 0)
        {
            std::cout << "command line has pitch automation\n";
            for (int i = 0; i < pitchautomation.size(); i += 2)
            {
                std::cout << pitchautomation[i] << "\t" << pitchautomation[i + 1] << "\n";
                // pitch_envelope.addPoint(
                //     {pitchautomation[i], std::pow(2.0, 1.0 / 12.0 * pitchautomation[i + 1])});
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
        render_signalsmith(infilename, outfilename, rate_envelope, pitch_envelope, formant_envelope,
                           compen_formant_pitch, dontwrite);
    }
    if (app.got_subcommand("varispeed"))
    {
        std::cout << "processing with resampling varispeed...\n";
    }
    return 0;
}
