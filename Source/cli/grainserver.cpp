#include "../Common/granularsynth.h"
#include "RtAudio.h"
#include "text/choc_Files.h"
#include "platform/choc_FileWatcher.h"

inline int audiocb(void *outputBuffer, void *inputBuffer, unsigned int nFrames, double streamTime,
                   RtAudioStreamStatus status, void *userData)
{
    ToneGranulator *gran = (ToneGranulator *)userData;
    float *obuf = (float *)outputBuffer;

    gran->process_block(obuf, nFrames);
    return 0;
}

inline events_t generate_events(double pulselen, double transpose)
{
    events_t result;
    double t = 0.0;
    std::vector<float> evt;
    evt.resize(GranulatorVoice::NUM_PARS);
    evt[GranulatorVoice::PAR_SYNCRATIO] = 1.0;
    evt[GranulatorVoice::PAR_TONETYPE] = 3.0;
    evt[GranulatorVoice::PAR_VOLUME] = 1.0;
    evt[GranulatorVoice::PAR_ENVTYPE] = 0.0;
    evt[GranulatorVoice::PAR_ENVSHAPE] = 0.5;
    evt[GranulatorVoice::PAR_VER_ANGLE] = 0.0;
    evt[GranulatorVoice::PAR_HOR_ANGLE] = 0.0;
    evt[GranulatorVoice::PAR_FILTERFEEDBACKAMOUNT] = 0.0;
    evt[GranulatorVoice::PAR_FILT1CUTOFF] = 120.0;
    evt[GranulatorVoice::PAR_FILT1RESON] = 0.0;
    evt[GranulatorVoice::PAR_FILT2CUTOFF] = 120.0;
    evt[GranulatorVoice::PAR_FILT2RESON] = 0.0;
    evt[GranulatorVoice::PAR_FILT2RESON] = 0.0;
    std::vector<float> pitches{24.0f, 36.0f, 48.0f, 60.0f};
    int count = 0;
    while (t < 10.0)
    {
        evt[GranulatorVoice::PAR_TPOS] = t;
        evt[GranulatorVoice::PAR_DUR] = pulselen * 2.0;
        double pitch = pitches[count] + transpose;
        evt[GranulatorVoice::PAR_FREQHZ] = 440.0 * std::pow(2.0, (1.0 / 12) * (pitch - 69.0));
        result.push_back(evt);
        t += pulselen;
        ++count;
        if (count == pitches.size())
            count = 0;
    }
    return result;
}

inline events_t load_events_file(std::string path)
{
    events_t result;
    result.reserve(4096);
    try
    {
        auto txt = choc::file::loadFileAsString(path);
        auto lines = choc::text::splitIntoLines(txt, false);
        std::vector<float> evt;
        evt.resize(GranulatorVoice::NUM_PARS);
        evt[GranulatorVoice::PAR_SYNCRATIO] = 1.0;
        evt[GranulatorVoice::PAR_TONETYPE] = 4.0;
        evt[GranulatorVoice::PAR_VOLUME] = 1.0;
        evt[GranulatorVoice::PAR_ENVTYPE] = 0.0;
        evt[GranulatorVoice::PAR_PULSEWIDTH] = 0.5;
        evt[GranulatorVoice::PAR_ENVSHAPE] = 0.5;
        evt[GranulatorVoice::PAR_VER_ANGLE] = 0.0;
        evt[GranulatorVoice::PAR_HOR_ANGLE] = 0.0;
        evt[GranulatorVoice::PAR_FILTERFEEDBACKAMOUNT] = 0.0;
        evt[GranulatorVoice::PAR_FILT1CUTOFF] = 120.0;
        evt[GranulatorVoice::PAR_FILT1RESON] = 0.0;
        evt[GranulatorVoice::PAR_FILT2CUTOFF] = 120.0;
        evt[GranulatorVoice::PAR_FILT2RESON] = 0.0;
        evt[GranulatorVoice::PAR_FILT2RESON] = 0.0;
        for (const auto &line : lines)
        {
            auto tokens = choc::text::splitAtWhitespace(line);
            if (tokens.size() >= 7)
            {
                evt[GranulatorVoice::PAR_TPOS] = std::stof(tokens[0]);
                evt[GranulatorVoice::PAR_DUR] = std::stof(tokens[1]);
                evt[GranulatorVoice::PAR_FREQHZ] = std::stof(tokens[2]);
                evt[GranulatorVoice::PAR_VOLUME] = std::stof(tokens[3]);
                evt[GranulatorVoice::PAR_HOR_ANGLE] = std::stof(tokens[4]);
                evt[GranulatorVoice::PAR_VER_ANGLE] = std::stof(tokens[5]);
                evt[GranulatorVoice::PAR_ENVTYPE] = std::stof(tokens[6]);
                evt[GranulatorVoice::PAR_ENVSHAPE] = std::stof(tokens[7]);
                evt[GranulatorVoice::PAR_SYNCRATIO] = std::stof(tokens[8]);
                result.push_back(evt);
            }
        }
    }
    catch (std::exception &ex)
    {
        std::print("{}\n", ex.what());
    }
    return result;
}

int main()
{
    double sr = 44100.0;
    double phase = 0.0;

    auto granulator = std::make_unique<ToneGranulator>(sr, 0, "none", "none");
    auto rtaudio = std::make_unique<RtAudio>();
    RtAudio::StreamParameters spars;
    spars.firstChannel = 0;
    spars.nChannels = 2;
    spars.deviceId = rtaudio->getDefaultOutputDevice();
    std::print("trying to open {}\n", rtaudio->getDeviceInfo(spars.deviceId).name);
    unsigned int bsize = 256;
    std::string grainfile = R"(C:\MusicAudio\sourcesamples\test_signals\tones\grains.txt)";
    choc::file::Watcher watcher{grainfile, [&](const choc::file::Watcher::Event &ev) {
                                    // std::print("file was changed {}\n",ev.time);
                                    auto evlist = load_events_file(grainfile);
                                    if (evlist.size() > 0)
                                    {
                                        granulator->prepare(evlist);
                                    }
                                }};
    if (rtaudio->openStream(&spars, nullptr, RTAUDIO_FLOAT32, sr, &bsize, audiocb,
                            granulator.get()) == RTAUDIO_NO_ERROR)
    {
        std::print("opened rtaudio with buffer size {}\n", bsize);

        rtaudio->startStream();
        while (true)
        {
            int cmd = 0;
            std::cin >> cmd;
            if (cmd == 1)
                break;
            // events_t events = load_events_file(grainfile);
            // granulator->prepare(events);
        }
        rtaudio->stopStream();
        rtaudio->closeStream();
    }
    else
    {
        std::print("could not open rtaudio stream\n");
    }
    return 0;
}
