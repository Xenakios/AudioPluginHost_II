#include "AudioFilePlayerPlugin.h"

AudioFilePlayerPlugin::AudioFilePlayerPlugin()
    : AudioProcessor(BusesProperties().withOutput("Output", AudioChannelSet::stereo()))
{
    m_from_gui_fifo.reset(1024);
    m_to_gui_fifo.reset(1024);
    importFile(juce::File(R"(C:\MusicAudio\sourcesamples\there was a time .wav)"));
    auto par = new juce::AudioParameterFloat("PITCHSHIFT", "Pitch shift", -12.0f, 12.0f, 0.0f);
    addParameter(par);
    juce::NormalisableRange<float> nr{0.1f, 4.0f, 0.01f};
    nr.setSkewForCentre(1.0f);
    par = new juce::AudioParameterFloat("RATE", "Play rate", nr, 1.0f);
    addParameter(par);
    par = new juce::AudioParameterFloat("VOLUME", "Volume", -24.0f, 6.0f, -6.0f);
    addParameter(par);
    
    startTimer(1000);
}

void AudioFilePlayerPlugin::importFile(juce::File f)
{
    juce::AudioFormatManager man;
    man.registerBasicFormats();
    auto reader = man.createReaderFor(f);
    jassert(reader);
    if (reader)
    {
        m_file_temp_buf.setSize(2, reader->lengthInSamples);
        reader->read(&m_file_temp_buf, 0, reader->lengthInSamples, 0, true, true);
        m_from_gui_fifo.push({CrossThreadMessage::Opcode::SwapBufferInAudioThread});
        delete reader;
    }
}

void AudioFilePlayerPlugin::timerCallback()
{
    CrossThreadMessage msg;
    while (m_to_gui_fifo.pop(msg))
    {
        if (msg.opcode == CrossThreadMessage::Opcode::ClearTempBufferInGuiThread)
        {
            if (m_file_temp_buf.getNumSamples() > 3000000)
            {
                DBG("AudioFilePlayerPlugin : Clearing temp audio file buffer");
                m_file_temp_buf = juce::AudioBuffer<float>();
            }
        }
    }
}

void AudioFilePlayerPlugin::processBlock(AudioBuffer<float> &buffer, MidiBuffer &)
{
    CrossThreadMessage msg;
    while (m_from_gui_fifo.pop(msg))
    {
        if (msg.opcode == CrossThreadMessage::Opcode::SwapBufferInAudioThread)
        {
            // IIRC swapping AudioBuffers like this is going to be fast, but in case it isn't really,
            // need to figure out something else
            std::swap(m_file_temp_buf, m_file_buf);
            if (m_buf_playpos >= m_file_buf.getNumSamples())
                m_buf_playpos = 0;
            // send message to GUI to clear the temp buffer
            m_to_gui_fifo.push({CrossThreadMessage::Opcode::ClearTempBufferInGuiThread});
        }
    }
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
