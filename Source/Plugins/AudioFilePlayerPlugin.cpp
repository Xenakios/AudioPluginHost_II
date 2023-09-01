#include "AudioFilePlayerPlugin.h"

AudioFilePlayerPlugin::AudioFilePlayerPlugin()
    : AudioProcessor(BusesProperties().withOutput("Output", AudioChannelSet::stereo()))
{
    importFile(juce::File(R"(C:\MusicAudio\sourcesamples\there was a time .wav)"));
    auto par = new juce::AudioParameterFloat("PITCHSHIFT", "Pitch shift", -12.0f, 12.0f, 0.0f);
    addParameter(par);
    juce::NormalisableRange<float> nr{0.1f,4.0f,0.01f};
    nr.setSkewForCentre(1.0f);
    par = new juce::AudioParameterFloat("RATE", "Play rate", nr, 1.0f);
    addParameter(par);
    par = new juce::AudioParameterFloat("VOLUME", "Volume", -24.0f, 6.0f, -6.0f);
    addParameter(par);
}

void AudioFilePlayerPlugin::importFile(juce::File f)
{
    juce::AudioFormatManager man;
    man.registerBasicFormats();
    auto reader = man.createReaderFor(f);
    jassert(reader);
    if (reader)
    {
        juce::AudioBuffer<float> temp(2, reader->lengthInSamples);
        reader->read(&temp, 0, reader->lengthInSamples, 0, true, true);
        m_cs.enter();
        std::swap(temp, m_file_buf);
        if (m_buf_playpos >= m_file_buf.getNumSamples())
            m_buf_playpos = 0;
        m_cs.exit();
        delete reader;
    }
}

void AudioFilePlayerPlugin::processBlock(AudioBuffer<float> &buffer, MidiBuffer &)
{
    // lock is used to guard file buffer switch, going to do something better later...
    juce::ScopedLock locker(m_cs);
    buffer.clear();
    if (m_file_buf.getNumSamples() == 0)
        return;
    float pshift = *getFloatParam(0);
    m_stretch.setTransposeSemitones(pshift);
    float rate = *getFloatParam(1);
    int samplestopush = buffer.getNumSamples() * rate;
    auto wbuf = m_work_buf.getArrayOfWritePointers();
    auto filebuf = m_file_buf.getArrayOfReadPointers();
    for (int i = 0; i < samplestopush; ++i)
    {
        wbuf[0][i] = filebuf[0][m_buf_playpos];
        wbuf[1][i] = filebuf[1][m_buf_playpos];
        ++m_buf_playpos;
        if (m_buf_playpos >= m_file_buf.getNumSamples())
            m_buf_playpos = 0;
    }
    m_stretch.process(m_work_buf.getArrayOfReadPointers(), samplestopush,
                      buffer.getArrayOfWritePointers(), buffer.getNumSamples());
    float volume = *getFloatParam(2);
    float gain = juce::Decibels::decibelsToGain(volume);
    buffer.applyGain(gain);
}
