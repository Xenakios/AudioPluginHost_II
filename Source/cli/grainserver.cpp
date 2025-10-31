#include "../Common/granularsynth.h"
#include "RtAudio.h"

inline int audiocb(void *outputBuffer, void *inputBuffer, unsigned int nFrames, double streamTime,
                   RtAudioStreamStatus status, void *userData)
{
    double *phase = (double *)userData;
    float *obuf = (float *)outputBuffer;
    for (int i = 0; i < nFrames; ++i)
    {
        float s = 0.1 * std::sin(*phase);
        obuf[i * 2 + 0] = s;
        obuf[i * 2 + 1] = s;
        (*phase) += M_PI * 440.0 / 44100.0;
    }
    
    return 0;
}

int main()
{
    double sr = 44100.0;
    double phase = 0.0;
    events_t events;
    double t = 0.0;
    double pulselen = 0.25;
    std::vector<float> evt;
    evt.resize(GranulatorVoice::NUM_PARS);
    evt[GranulatorVoice::PAR_SYNCRATIO] = 1.0;
    evt[GranulatorVoice::PAR_TONETYPE] = 2.0;
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
    while (t < 10.0)
    {
        evt[GranulatorVoice::PAR_TPOS] = t;
        evt[GranulatorVoice::PAR_DUR] = pulselen * 2.0;
        evt[GranulatorVoice::PAR_FREQHZ] = 440.0;
        events.push_back(evt);
        t += pulselen;
    }
    auto granulator = std::make_unique<ToneGranulator>(sr, events, 0, "none", "none");
    auto rtaudio = std::make_unique<RtAudio>();
    RtAudio::StreamParameters spars;
    spars.firstChannel = 0;
    spars.nChannels = 2;
    spars.deviceId = rtaudio->getDefaultOutputDevice();
    std::print("trying to open {}\n", rtaudio->getDeviceInfo(spars.deviceId).name);
    unsigned int bsize = 256;
    if (rtaudio->openStream(&spars, nullptr, RTAUDIO_FLOAT32, sr, &bsize, audiocb, &phase) ==
        RTAUDIO_NO_ERROR)
    {
        std::print("opened rtaudio with buffer size {}\n", bsize);
        rtaudio->startStream();
        Sleep(1000);
        rtaudio->stopStream();
        rtaudio->closeStream();
    }
    else
    {
        std::print("could not open rtaudio stream\n");
    }
    return 0;
}
