#include "AudioFilePlayerPlugin.h"

AudioFilePlayerPlugin::AudioFilePlayerPlugin()
    : AudioProcessor(BusesProperties().withOutput("Output", AudioChannelSet::stereo()))
{
    m_from_gui_fifo.reset(1024);
    m_to_gui_fifo.reset(1024);
#define XENAKIOSDEBUG
#ifdef XENAKIOSDEBUG
    // importFile(juce::File(R"(C:\MusicAudio\sourcesamples\there was a time .wav)"));
    // importFile(juce::File(R"(C:\MusicAudio\sourcesamples\test_signals\440hz_sine_0db.wav)"));
    importFile(juce::File(R"(C:\MusicAudio\sourcesamples\count_96k.wav)"));
#endif
    auto par = new juce::AudioParameterFloat("PITCHSHIFT", "Pitch shift", -12.0f, 12.0f, 0.0f);
    addParameter(par);
    juce::NormalisableRange<float> nr{0.1f, 4.0f, 0.01f};
    nr.setSkewForCentre(1.0f);
    par = new juce::AudioParameterFloat("RATE", "Play rate", nr, 1.0f);
    addParameter(par);
    par = new juce::AudioParameterFloat("VOLUME", "Volume", -24.0f, 6.0f, -18.0f);
    addParameter(par);
    auto bpar =
        new juce::AudioParameterBool("PRESERVEPITCH", "Preserve pitch when changing rate", true);
    addParameter(bpar);
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
        m_from_gui_fifo.push(
            {CrossThreadMessage::Opcode::SwapBufferInAudioThread, 0, 0, (float)reader->sampleRate});
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
            // clear only large large buffers, so we can reuse the temp buffer for smaller files
            if (m_file_temp_buf.getNumSamples() > 3000000)
            {
                DBG("AudioFilePlayerPlugin : Clearing temp audio file buffer");
                // could setSize(0,0) be used instead...?
                m_file_temp_buf = juce::AudioBuffer<float>();
            }
        }
    }
}

void AudioFilePlayerPlugin::prepareToPlay(double newSampleRate, int maxBlocksize)
{
    m_buf_playpos = 0;
    m_buf_playpos_atomic.store(0);
    m_stretch.presetDefault(2, newSampleRate);
    m_work_buf.setSize(2, maxBlocksize * 16);
    juce::dsp::ProcessSpec spec;
    spec.maximumBlockSize = maxBlocksize;
    spec.sampleRate = newSampleRate;
    spec.numChannels = 2;
    m_gain.prepare(spec);
    m_gain.setRampDurationSeconds(0.1);
    for (auto &rs : m_resamplers)
        rs.reset();
    m_resampler_work_buf.resize(maxBlocksize * 32);
}

void AudioFilePlayerPlugin::processBlock(AudioBuffer<float> &buffer, MidiBuffer &)
{
    CrossThreadMessage msg;
    while (m_from_gui_fifo.pop(msg))
    {
        if (msg.opcode == CrossThreadMessage::Opcode::SwapBufferInAudioThread)
        {
            // IIRC swapping AudioBuffers like this is going to be fast, but in case it isn't
            // really, need to figure out something else
            std::swap(m_file_temp_buf, m_file_buf);
            m_file_sample_rate = msg.par2;
            if (m_buf_playpos >= m_file_buf.getNumSamples())
                m_buf_playpos = 0;
            m_to_gui_fifo.push({CrossThreadMessage::Opcode::ClearTempBufferInGuiThread});
        }
    }
    buffer.clear();
    if (m_file_buf.getNumSamples() == 0)
        return;

    auto filebuf = m_file_buf.getArrayOfReadPointers();

    auto wbuf = m_work_buf.getArrayOfWritePointers();
    int cachedpos = m_buf_playpos;
    float compensrate = m_file_sample_rate / getSampleRate();
    bool preserve_pitch = *getBoolParam(3);
    float rate = *getFloatParam(1);
    if (preserve_pitch)
    {
        float pshift = *getFloatParam(0);
        float pitchratio = std::pow(2.0, pshift / 12.0);
        m_stretch.setTransposeFactor(pitchratio * compensrate);
        rate *= compensrate;
        int samplestopush = buffer.getNumSamples() * rate;
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
    }
    else
    {
        rate *= compensrate;
        int samplestopush = buffer.getNumSamples() * rate;
        int consumed[2] = {0, 0};
        samplestopush += 1;
        for (int i = 0; i < samplestopush; ++i)
        {
            wbuf[0][i] = filebuf[0][m_buf_playpos];
            wbuf[1][i] = filebuf[1][m_buf_playpos];
            ++m_buf_playpos;
            if (m_buf_playpos >= m_file_buf.getNumSamples())
                m_buf_playpos = 0;
        }
        for (int ch = 0; ch < 2; ++ch)
        {
            consumed[ch] = m_resamplers[ch].process(rate, wbuf[ch], buffer.getWritePointer(ch),
                                                    buffer.getNumSamples(), samplestopush, 0);
        }
        jassert(consumed[0] == consumed[1]);
        m_buf_playpos = (cachedpos + consumed[0]) % m_file_buf.getNumSamples();
    }

    float volume = *getFloatParam(2);
    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> ctx(block);
    m_gain.setGainDecibels(volume);
    m_gain.process(ctx);
    m_buf_playpos_atomic.store(m_buf_playpos);
}
