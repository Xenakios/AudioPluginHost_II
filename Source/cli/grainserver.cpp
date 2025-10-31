#include "../Common/granularsynth.h"
#include "RtAudio.h"

inline int audiocb(void *outputBuffer, void *inputBuffer, unsigned int nFrames, double streamTime,
                   RtAudioStreamStatus status, void *userData)
{
    ToneGranulator *gran = (ToneGranulator *)userData;
    float *obuf = (float *)outputBuffer;

    gran->process_block(obuf, nFrames);
    return 0;
}

inline events_t generate_events(double pulselen)
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
        evt[GranulatorVoice::PAR_FREQHZ] =
            440.0 * std::pow(2.0, (1.0 / 12) * (pitches[count] - 69.0));
        result.push_back(evt);
        t += pulselen;
        ++count;
        if (count == pitches.size())
            count = 0;
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
    if (rtaudio->openStream(&spars, nullptr, RTAUDIO_FLOAT32, sr, &bsize, audiocb,
                            granulator.get()) == RTAUDIO_NO_ERROR)
    {
        std::print("opened rtaudio with buffer size {}\n", bsize);
        
        rtaudio->startStream();
        while (true)
        {
            double plen = 0.0;
            std::cin >> plen;
            if (plen < 0.01)
                break;
            events_t events = generate_events(plen);
            granulator->prepare(events);
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
