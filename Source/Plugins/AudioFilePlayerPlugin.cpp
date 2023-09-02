#include "AudioFilePlayerPlugin.h"

AudioFilePlayerPlugin::AudioFilePlayerPlugin()
    : AudioProcessor(BusesProperties().withOutput("Output", AudioChannelSet::stereo()))
{
    m_from_gui_fifo.reset(1024);
    m_to_gui_fifo.reset(1024);
#define XENAKIOSDEBUG
#ifdef XENAKIOSDEBUG
    // importFile(juce::File(R"(C:\MusicAudio\sourcesamples\there was a time .wav)"));
    importFile(juce::File(R"(C:\MusicAudio\sourcesamples\test_signals\440hz_sine_0db.wav)"));
    // importFile(juce::File(R"(C:\MusicAudio\sourcesamples\count_96k.wav)"));
#endif
    auto par = new juce::AudioParameterFloat("PITCHSHIFT", "Pitch shift", -12.0f, 12.0f, 0.0f);
    m_par_pitch = par;
    addParameter(par);
    juce::NormalisableRange<float> nr{0.1f, 4.0f, 0.01f};
    nr.setSkewForCentre(1.0f);
    par = new juce::AudioParameterFloat("RATE", "Play rate", nr, 1.0f);
    m_par_rate = par;
    addParameter(par);
    par = new juce::AudioParameterFloat("VOLUME", "Volume", -24.0f, 6.0f, -18.0f);
    m_par_volume = par;
    addParameter(par);
    auto bpar =
        new juce::AudioParameterBool("PRESERVEPITCH", "Preserve pitch when changing rate", true);
    m_par_preserve_pitch = bpar;
    addParameter(bpar);
    par = new juce::AudioParameterFloat("LOOPSTART", "Loop start", 0.0f, 1.0f, 0.0f);
    m_par_loop_start = par;
    addParameter(par);
    par = new juce::AudioParameterFloat("LOOPEND", "Loop end", 0.0f, 1.0f, 1.0f);
    m_par_loop_end = par;
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
        m_cur_file = f;
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
    int loop_start_samples = (*m_par_loop_start) * m_file_buf.getNumSamples();
    int loop_end_samples = (*m_par_loop_end) * m_file_buf.getNumSamples();
    if (loop_start_samples > loop_end_samples)
        std::swap(loop_start_samples, loop_end_samples);
    if (loop_start_samples == loop_end_samples)
    {
        loop_end_samples += 512;
        if (loop_end_samples >= m_file_buf.getNumSamples())
        {
            loop_start_samples = m_file_buf.getNumSamples() - 512;
            loop_end_samples = m_file_buf.getNumSamples() - 1;
        }
    }
    if (m_buf_playpos < loop_start_samples)
        m_buf_playpos = loop_start_samples;
    if (m_buf_playpos >= loop_end_samples)
        m_buf_playpos = loop_start_samples;
    auto filebuf = m_file_buf.getArrayOfReadPointers();

    auto wbuf = m_work_buf.getArrayOfWritePointers();
    int cachedpos = m_buf_playpos;
    float compensrate = m_file_sample_rate / getSampleRate();
    bool preserve_pitch = *m_par_preserve_pitch;
    float rate = *m_par_rate;
    auto getxfadedsample = [](const float *srcbuf, int index, int start, int end, int xfadelen) {
        // not within xfade region so just return original sample
        int xfadestart = end - xfadelen;
        if (index >= start && index < xfadestart)
            return srcbuf[index];

        float xfadegain = juce::jmap<float>(index, xfadestart, end - 1, 1.0f, 0.0f);
        jassert(xfadegain >= 0.0f && xfadegain <= 1.0);
        float s0 = srcbuf[index];
        int temp = index - xfadestart + (start - xfadelen);
        if (temp < 0)
            return s0 * xfadegain;
        jassert(temp >= 0 && temp < end);
        float s1 = srcbuf[temp];
        return s0 * xfadegain + s1 * (1.0f - xfadegain);
    };
    int xfadelen = 256;
    if (preserve_pitch)
    {
        float pshift = *m_par_pitch;
        float pitchratio = std::pow(2.0, pshift / 12.0);
        m_stretch.setTransposeFactor(pitchratio * compensrate);
        rate *= compensrate;
        int samplestopush = buffer.getNumSamples() * rate;
        for (int i = 0; i < samplestopush; ++i)
        {
            wbuf[0][i] = getxfadedsample(filebuf[0], m_buf_playpos, loop_start_samples,
                                         loop_end_samples, xfadelen);
            wbuf[1][i] = getxfadedsample(filebuf[1], m_buf_playpos, loop_start_samples,
                                         loop_end_samples, xfadelen);
            ++m_buf_playpos;
            if (m_buf_playpos >= loop_end_samples)
                m_buf_playpos = loop_start_samples;
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
            wbuf[0][i] = getxfadedsample(filebuf[0], m_buf_playpos, loop_start_samples,
                                         loop_end_samples, xfadelen);
            wbuf[1][i] = getxfadedsample(filebuf[1], m_buf_playpos, loop_start_samples,
                                         loop_end_samples, xfadelen);
            ++m_buf_playpos;
            if (m_buf_playpos >= loop_end_samples)
                m_buf_playpos = loop_start_samples;
        }
        for (int ch = 0; ch < 2; ++ch)
        {
            consumed[ch] = m_resamplers[ch].process(rate, wbuf[ch], buffer.getWritePointer(ch),
                                                    buffer.getNumSamples(), samplestopush, 0);
        }
        jassert(consumed[0] == consumed[1]);
        m_buf_playpos = (cachedpos + consumed[0]);
        if (m_buf_playpos >= loop_end_samples)
        {
            m_buf_playpos = loop_start_samples;
        }
    }

    float volume = *m_par_volume;
    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> ctx(block);
    m_gain.setGainDecibels(volume);
    m_gain.process(ctx);
    m_buf_playpos_atomic.store(m_buf_playpos);
}

WaveFormComponent::WaveFormComponent(AudioFilePlayerPlugin &p) : m_proc(p), m_thumb_cache(10)
{
    // startTimerHz(10);
    m_aman.registerBasicFormats();
    m_thumb = std::make_unique<juce::AudioThumbnail>(256, m_aman, m_thumb_cache);
    loadFile(p.getCurrentFile());
}

void WaveFormComponent::timerCallback() { repaint(); }

void WaveFormComponent::paint(juce::Graphics &g)
{
    g.fillAll(juce::Colours::black);
    if (m_thumb->getTotalLength() > 0)
    {
        g.setColour(juce::Colours::grey);
        m_thumb->drawChannels(g, getLocalBounds(), 0.0, m_thumb->getTotalLength(), 1.0f);
        double dur = m_proc.getFileDurationSeconds();
        double pos = m_proc.getFilePlayPositionSeconds();
        g.setColour(juce::Colours::lightgrey.withAlpha(0.5f));
        float loopstart = *m_proc.m_par_loop_start;
        float loopend = *m_proc.m_par_loop_end;
        if (loopstart > loopend)
            std::swap(loopstart, loopend);
        float xcor0 = juce::jmap<float>(loopstart, 0.0, 1.0, 0, getWidth());
        float xcor1 = juce::jmap<float>(loopend, 0.0, 1.0, 0, getWidth());
        g.fillRect(xcor0, 0.0f, xcor1 - xcor0, (float)getHeight());
        float xcor = juce::jmap<float>(pos, 0.0, dur, 0, getWidth());
        g.setColour(juce::Colours::white);
        g.drawLine(xcor, 0, xcor, getHeight());
    }
}

void WaveFormComponent::loadFile(juce::File f) { m_thumb->setSource(new juce::FileInputSource(f)); }

void AudioFilePlayerPlugin::AudioFilePlayerPluginEditor::resized()
{
    juce::FlexBox flex;
    flex.flexDirection = juce::FlexBox::Direction::column;
    flex.items.add(juce::FlexItem(m_import_file_but).withFlex(0.5f));
    flex.items.add(juce::FlexItem(m_gen_ed).withFlex(3.0f));
    flex.items.add(juce::FlexItem(m_wavecomponent).withFlex(2.0f));
    flex.items.add(juce::FlexItem(m_infolabel).withFlex(0.5f));
    flex.performLayout(getBounds());
}