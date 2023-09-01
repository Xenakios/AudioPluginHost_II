#include "AudioFilePlayerPlugin.h"

void AudioFilePlayerPlugin::processBlock(AudioBuffer<float> &buffer, MidiBuffer &)
{
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
