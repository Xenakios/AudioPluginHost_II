#include "fileplayer_xaudioprocessor.h"
#include "../xap_slider.h"

class WaveFormComponent : public juce::Component, public juce::ChangeListener
{
  public:
    WaveFormComponent() : m_thumb_cache(50)
    {
        m_afman.registerBasicFormats();
        m_thumb = std::make_unique<juce::AudioThumbnail>(128, m_afman, m_thumb_cache);
        m_thumb->addChangeListener(this);
    }
    void mouseDown(const juce::MouseEvent &ev) override
    {
        double newpos = juce::jmap<double>(ev.x, 0.0, getWidth(), 0.0, 1.0);
        newpos = juce::jlimit<double>(0.0, 1.0, newpos);
        if (OnSeek)
            OnSeek(newpos);
    }
    void changeListenerCallback(juce::ChangeBroadcaster *source) override { repaint(); }
    juce::Font m_font;

    void paint(juce::Graphics &g) override
    {
        g.fillAll(juce::Colours::black);
        if (m_thumb->getTotalLength() > 0.0)
        {
            g.setColour(juce::Colours::darkgrey);
            m_thumb->drawChannels(g, juce::Rectangle<int>(0, 0, getWidth(), getHeight()), 0.0,
                                  m_thumb->getTotalLength(), 1.0f);
            g.setColour(juce::Colours::white);
            double xcor = juce::jmap<double>(m_filepos, 0.0, 1.0, 0.0, getWidth());
            g.drawLine(xcor, 0, xcor, getHeight());
        }
        g.setColour(juce::Colours::white);
        g.setFont(m_font.withHeight(20));
        g.drawText(m_text, 0, 0, getWidth(), getHeight(), juce::Justification::topLeft);
    }
    std::function<void(double)> OnSeek;
    juce::AudioFormatManager m_afman;
    juce::AudioThumbnailCache m_thumb_cache;
    std::unique_ptr<juce::AudioThumbnail> m_thumb;
    void setFilePosition(double pos)
    {
        m_filepos = pos;
        repaint();
    }
    void setTextToDisplay(juce::String txt)
    {
        m_text = txt;
        repaint();
    }

  private:
    double m_filepos = -1.0;
    juce::String m_text;
};

class FilePlayerEditor : public juce::Component,
                         public juce::Timer,
                         public juce::FilenameComponentListener,
                         public juce::ChangeListener
{
  public:
    void changeListenerCallback(juce::ChangeBroadcaster *source) override { repaint(); }

    WaveFormComponent m_wavecomponent;
    void filenameComponentChanged(juce::FilenameComponent *fc) override
    {
        DBG(fc->getCurrentFile().getFullPathName());
        std::string fn = fc->getCurrentFile().getFullPathName().toStdString();
        FilePlayerProcessor::FilePlayerMessage msg;
        msg.opcode = FilePlayerProcessor::FilePlayerMessage::Opcode::RequestFileChange;
        msg.filename = fn;
        m_proc->messages_to_io.push(msg);
        // m_proc->messages_from_ui.push(msg);
    }
    std::unique_ptr<juce::FileChooser> m_file_chooser;
    juce::AudioFormatManager m_afman;
    juce::Font m_font;
    FilePlayerEditor(FilePlayerProcessor *proc)
        : m_proc(proc),
          m_file_comp("", juce::File(), false, false, false, "*.wav", "", "Choose audio file")
    {
        auto monospace = juce::Font::getDefaultMonospacedFontName();
        m_font = juce::Font(monospace, 13.0f, juce::Font::bold);
        juce::StringArray presetFiles;
        presetFiles.add(R"(C:\MusicAudio\sourcesamples\sheila.wav)");
        presetFiles.add(R"(C:\MusicAudio\sourcesamples\songs\chrono.wav)");
        presetFiles.add(R"(C:\MusicAudio\sourcesamples\there was a time .wav)");
        presetFiles.add(R"(C:\MusicAudio\sourcesamples\_Fails_to_load.wav)");
        presetFiles.add(R"(C:\MusicAudio\sourcesamples\test_signals\440hz_sine_0db.wav)");

        m_file_comp.setRecentlyUsedFilenames(presetFiles);
        m_afman.registerBasicFormats();

        addAndMakeVisible(m_wavecomponent);
        m_wavecomponent.m_font = m_font;

        addAndMakeVisible(m_file_comp);
        m_file_comp.addListener(this);

        for (int i = 0; i < m_proc->paramDescriptions.size(); ++i)
        {
            ParamDesc::FeatureState *pfs = nullptr;
            if (m_proc->paramDescriptions[i].id == (clap_id)FilePlayerProcessor::ParamIds::Pitch)
                pfs = &m_proc->m_fs_pitch;
            auto comp = std::make_unique<XapSlider>(true, m_proc->paramDescriptions[i], pfs);
            comp->m_font = m_font;
            comp->OnValueChanged = [this, pslider = comp.get(), i]() {
                FilePlayerProcessor::FilePlayerMessage msg;
                msg.opcode = FilePlayerProcessor::FilePlayerMessage::Opcode::ParamChange;
                msg.parid = (clap_id)m_proc->paramDescriptions[i].id;
                msg.value = pslider->getValue();
                m_proc->messages_from_ui.push(msg);
            };
            addAndMakeVisible(comp.get());
            mapParToComponent[m_proc->paramDescriptions[i].id] = comp.get();
            m_par_comps.push_back(std::move(comp));
        }
        startTimer(100);
    }

    juce::String m_cur_file_text{"No file loaded"};

    double m_offlineprogress = -1.0;
    void timerCallback() override
    {
        FilePlayerProcessor::FilePlayerMessage msg;
        while (m_proc->messages_to_ui.pop(msg))
        {
            if (msg.opcode == FilePlayerProcessor::FilePlayerMessage::Opcode::FileChanged)
            {
                if (!msg.filename.empty())
                {
                    juce::File f(msg.filename);
                    m_wavecomponent.m_thumb->setSource(new juce::FileInputSource(f));
                    m_wavecomponent.setTextToDisplay(f.getFileName());
                }
                m_offlineprogress = -1.0;
                repaint();
            }
            if (msg.opcode == FilePlayerProcessor::FilePlayerMessage::Opcode::FileLoadError)
            {
                m_wavecomponent.m_thumb->setSource(nullptr);
                m_wavecomponent.setTextToDisplay("Error loading file");
            }
            if (msg.opcode == FilePlayerProcessor::FilePlayerMessage::Opcode::OfflineProgress)
            {
                m_file_comp.setEnabled(false);
                m_offlineprogress = msg.value;
                repaint();
            }
            if (msg.opcode == FilePlayerProcessor::FilePlayerMessage::Opcode::FilePlayPosition)
            {
                m_wavecomponent.setFilePosition(msg.value);
            }
            if (msg.opcode == FilePlayerProcessor::FilePlayerMessage::Opcode::ParamChange)
            {
                if (mapParToComponent.count(msg.parid))
                {
                    mapParToComponent[msg.parid]->setValue(msg.value);

                    if (msg.parid == (clap_id)FilePlayerProcessor::ParamIds::TriggeredMode)
                    {
                        m_triggered_mode = msg.value;
                    }
                    if (msg.parid == (clap_id)FilePlayerProcessor::ParamIds::PreservePitch)
                    {
                        auto pitchcomp =
                            mapParToComponent[(clap_id)FilePlayerProcessor::ParamIds::Pitch];
                        pitchcomp->setEnabled(msg.value >= 0.5);
                        jassert(pitchcomp->isEnabled() == msg.value >= 0.5);
                    }
                }
            }
        }
        if (m_offlineprogress < 0.0)
            m_file_comp.setEnabled(true);
    }
    bool m_triggered_mode = false;

    void resized() override
    {
        juce::FlexBox flex;
        float margin = 5.0f;
        flex.flexDirection = juce::FlexBox::Direction::column;
        flex.items.add(juce::FlexItem(m_wavecomponent).withFlex(4.0).withMargin(margin));
        flex.items.add(juce::FlexItem(m_file_comp).withFlex(1.0).withMargin(margin));
        for (int i = 0; i < m_par_comps.size(); ++i)
        {
            flex.items.add(juce::FlexItem(*m_par_comps[i]).withFlex(1.0).withMargin(margin));
        }
        flex.performLayout(getBounds());
    }
    void mouseDown(const juce::MouseEvent &ev) override
    {
        double newpos = juce::jmap<double>(ev.x, 0.0, getWidth(), 0.0, 1.0);
        newpos = juce::jlimit<double>(0.0, 1.0, newpos);
        FilePlayerProcessor::FilePlayerMessage msg;
        msg.opcode = FilePlayerProcessor::FilePlayerMessage::Opcode::FilePlayPosition;
        msg.value = newpos;
        m_proc->messages_from_ui.push(msg);
    }
    void paint(juce::Graphics &g) override {}

  private:
    FilePlayerProcessor *m_proc = nullptr;

    juce::FilenameComponent m_file_comp;

    std::vector<std::unique_ptr<XapSlider>> m_par_comps;
    std::unordered_map<clap_id, XapSlider *> mapParToComponent;
};

bool FilePlayerProcessor::guiCreate(const char *api, bool isFloating) noexcept
{
    m_editor = std::make_unique<FilePlayerEditor>(this);
    m_editor->setSize(1000, 500);
    return true;
}

void FilePlayerProcessor::run()
{
    bool stop = false;
    while (!stop)
    {
        if (threadShouldExit())
            break;
        FilePlayerMessage msg;
        while (messages_to_io.pop(msg))
        {
            if (msg.opcode == FilePlayerMessage::Opcode::StopIOThread)
            {
                stop = true;
                break;
            }

            if (msg.opcode == FilePlayerMessage::Opcode::RequestFileChange)
            {
                juce::String tempstring(msg.filename.data());
                juce::File afile(tempstring);
                auto reader = fman.createReaderFor(afile);
                if (reader)
                {
                    m_temp_file_sample_rate = reader->sampleRate;
                    m_file_temp_buf.setSize(2, reader->lengthInSamples);
                    int blocksize = 65536;
                    int filepos = 0;
                    FilePlayerMessage progressmsg;
                    progressmsg.opcode = FilePlayerMessage::Opcode::OfflineProgress;
                    while (filepos < reader->lengthInSamples)
                    {
                        int numtoread =
                            std::min<int64_t>(blocksize, reader->lengthInSamples - filepos);
                        reader->read(&m_file_temp_buf, filepos, numtoread, filepos, true, true);
                        double progress = 1.0 / reader->lengthInSamples * filepos;
                        progressmsg.value = progress;
                        messages_to_ui.push(progressmsg);
                        filepos += numtoread;
                        // artificially slow down for testing
                        // juce::Thread::sleep(10);
                    }

                    currentfilename = afile.getFullPathName().toStdString();
                    FilePlayerMessage readymsg;
                    readymsg.opcode = FilePlayerMessage::Opcode::FileChanged;
                    messages_from_io.push(readymsg);
                    readymsg.opcode = FilePlayerMessage::Opcode::FileChanged;
                    readymsg.filename = msg.filename;
                    messages_to_ui.push(readymsg);
                    delete reader;
                }
                else
                {
                    FilePlayerMessage readymsg;
                    readymsg.opcode = FilePlayerMessage::Opcode::FileLoadError;
                    messages_to_ui.push(readymsg);
                }
            }
        }
        juce::Thread::sleep(50);
    }
}

void FilePlayerProcessor::handleMessagesFromUI()
{
    FilePlayerMessage msg;
    while (messages_from_ui.pop(msg))
    {
        // the string needs to be empty so we don't have to deallocate in the audio thread
        jassert(msg.filename.empty());
        if (msg.opcode == FilePlayerMessage::Opcode::ParamChange)
        {
            auto pev = xenakios::make_event_param_value(0, msg.parid, msg.value, nullptr);
            handleEvent((const clap_event_header *)&pev, true);
        }
        if (msg.opcode == FilePlayerMessage::Opcode::ParamFeatureStateChanged)
        {
            if (msg.parid == (clap_id)ParamIds::Pitch)
            {
                m_fs_pitch.isExtended = msg.value >= 0.5;
            }
        }
        if (msg.opcode == FilePlayerMessage::Opcode::FilePlayPosition)
        {
            if (m_file_buf.getNumSamples() > 0)
            {
                m_buf_playpos = msg.value * m_file_buf.getNumSamples();
            }
        }
    }
}

void FilePlayerProcessor::handleMessagesFromIO()
{
    FilePlayerMessage msg;
    while (messages_from_io.pop(msg))
    {
        // the string needs to be empty so we don't have to deallocate in the audio thread
        jassert(msg.filename.empty());
        if (msg.opcode == FilePlayerMessage::Opcode::FileChanged)
        {
            m_file_sample_rate = m_temp_file_sample_rate;
            // swapping AudioBuffers should be fast and not block
            // but might need more investigation
            // double t0 = juce::Time::getMillisecondCounterHiRes();
            std::swap(m_file_temp_buf, m_file_buf);
            // double t1 = juce::Time::getMillisecondCounterHiRes();
            // DBG("swap took " << t1-t0 << " millisecons");
        }
    }
}

bool FilePlayerProcessor::activate(double sampleRate, uint32_t minFrameCount,
                                   uint32_t maxFrameCount) noexcept
{
    m_sr = sampleRate;
    juce::dsp::ProcessSpec spec;
    spec.maximumBlockSize = maxFrameCount;
    spec.numChannels = 2;
    spec.sampleRate = sampleRate;
    m_gain_proc.prepare(spec);
    m_gain_proc.setRampDurationSeconds(0.01);

    m_buf_playpos = 0;

    m_stretch.presetDefault(2, sampleRate);
    m_work_buf.setSize(2, maxFrameCount * 16);
    m_work_buf.clear();
    m_rs_out_buf.resize(maxFrameCount * 4);
    m_wresampler.Reset();
    return true;
}

clap_process_status FilePlayerProcessor::process(const clap_process *process) noexcept
{
    handleMessagesFromUI();
    auto inevts = process->in_events;
    for (int i = 0; i < inevts->size(inevts); ++i)
    {
        auto ev = inevts->get(inevts, i);
        handleEvent(ev, false);
    }
    handleMessagesFromIO();
    if (m_file_buf.getNumSamples() == 0)
    {
        // jassert(false);
        return CLAP_PROCESS_CONTINUE;
    }
    if (m_filepos_throttle_counter == 0)
    {
        FilePlayerMessage outmsg;
        outmsg.opcode = FilePlayerMessage::Opcode::FilePlayPosition;
        outmsg.value = 1.0 / m_file_buf.getNumSamples() * m_buf_playpos;
        messages_to_ui.push(outmsg);
    }
    m_filepos_throttle_counter += process->frames_count;
    if (m_filepos_throttle_counter >= m_filepos_throttle_frames)
    {
        m_filepos_throttle_counter = 0;
    }
    int loop_start_samples = m_loop_start * m_file_buf.getNumSamples();
    int loop_end_samples = m_loop_end * m_file_buf.getNumSamples();
    if (loop_start_samples > loop_end_samples)
        std::swap(loop_start_samples, loop_end_samples);
    if (loop_start_samples == loop_end_samples)
    {
        loop_end_samples += 4100;
        if (loop_end_samples >= m_file_buf.getNumSamples())
        {
            loop_start_samples = m_file_buf.getNumSamples() - 4100;
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
    float compensrate = m_file_sample_rate / m_sr;
    bool preserve_pitch = m_preserve_pitch;
    // time octaves
    double rate = m_rate;
    rate += m_rate_mod;
    // we could allow modulation to make it go a bit over these limits...
    rate = std::clamp(rate, -3.0, 2.0);
    // then convert to actual playback ratio
    rate = std::pow(2.0, rate);
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
    int xfadelen = 22050;
    if (preserve_pitch)
    {
        double pshift = m_pitch;
        if (m_fs_pitch.isExtended)
            pshift *= pitchExtendFactor;
        pshift += m_pitch_mod;
        pshift = std::clamp(pshift, -48.0, 48.0);
        double pitchratio = std::pow(2.0, pshift / 12.0);
        m_stretch.setTransposeFactor(pitchratio * compensrate);
        rate *= compensrate;
        int samplestopush = process->frames_count * rate;
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
                          process->audio_outputs[0].data32, process->frames_count);
    }
    else
    {
        m_wresampler.SetRates(m_file_sample_rate, m_sr / rate);
        double *to_rs_buf = nullptr;
        int samplestopush = m_wresampler.ResamplePrepare(process->frames_count, 2, &to_rs_buf);
        for (int i = 0; i < samplestopush; ++i)
        {
            to_rs_buf[i * 2 + 0] = getxfadedsample(filebuf[0], m_buf_playpos, loop_start_samples,
                                                   loop_end_samples, xfadelen);
            to_rs_buf[i * 2 + 1] = getxfadedsample(filebuf[0], m_buf_playpos, loop_start_samples,
                                                   loop_end_samples, xfadelen);
            ++m_buf_playpos;
            if (m_buf_playpos >= loop_end_samples)
                m_buf_playpos = loop_start_samples;
        }
        m_wresampler.ResampleOut(m_rs_out_buf.data(), samplestopush, process->frames_count, 2);
        for (int ch = 0; ch < 2; ++ch)
        {
            for (int j = 0; j < process->frames_count; ++j)
            {
                process->audio_outputs[0].data32[ch][j] = m_rs_out_buf[j * 2 + ch];
            }
        }
        
    }
    m_gain_proc.setGainDecibels(m_volume);
    juce::dsp::AudioBlock<float> block(process->audio_outputs[0].data32, 2, process->frames_count);
    juce::dsp::ProcessContextReplacing<float> ctx(block);
    m_gain_proc.process(ctx);
    return CLAP_PROCESS_CONTINUE;
}