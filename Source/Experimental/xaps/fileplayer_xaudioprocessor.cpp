#include "fileplayer_xaudioprocessor.h"

class FilePlayerEditor : public juce::Component,
                         public juce::Timer,
                         public juce::FilenameComponentListener,
                         public juce::ChangeListener
{
  public:
    void changeListenerCallback(juce::ChangeBroadcaster *source) override { repaint(); }
    juce::AudioThumbnailCache m_thumb_cache;
    std::unique_ptr<juce::AudioThumbnail> m_thumb;
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
    FilePlayerEditor(FilePlayerProcessor *proc)
        : m_thumb_cache(100), m_proc(proc),
          m_file_comp("", juce::File(), false, false, false, "*.wav", "", "Choose audio file")
    {
        m_afman.registerBasicFormats();
        m_thumb = std::make_unique<juce::AudioThumbnail>(128, m_afman, m_thumb_cache);
        m_thumb->addChangeListener(this);
        addAndMakeVisible(m_load_but);
        addAndMakeVisible(m_file_label);
        addAndMakeVisible(m_file_comp);
        m_file_comp.addListener(this);

        m_load_but.setButtonText("Load file...");
        m_load_but.onClick = [this]() { showFileOpenDialog(); };
        for (int i = 0; i < m_proc->paramDescriptions.size(); ++i)
        {
            auto comp = std::make_unique<SliderAndLabel>(m_proc, m_proc->paramDescriptions[i]);
            addAndMakeVisible(comp.get());
            mapParToComponent[m_proc->paramDescriptions[i].id] = comp.get();
            m_par_comps.push_back(std::move(comp));
        }
        startTimer(100);
    }
    // message to processor doesn't own the file name string so need to keep it alive here
    std::string file_to_change_to;
    void showFileOpenDialog()
    {
        m_file_chooser =
            std::make_unique<juce::FileChooser>("Choose audio file", juce::File(), "*.wav", true);
        m_file_chooser->launchAsync(
            juce::FileBrowserComponent::FileChooserFlags::openMode |
                juce::FileBrowserComponent::FileChooserFlags::canSelectFiles,
            [this](const juce::FileChooser &chooser) {
                if (chooser.getResult() != juce::File())
                {
                    // m_proc->importFile(chooser.getResult());
                    file_to_change_to = chooser.getResult().getFullPathName().toStdString();
                    FilePlayerProcessor::FilePlayerMessage msg;
                    msg.opcode = FilePlayerProcessor::FilePlayerMessage::Opcode::RequestFileChange;
                    msg.filename = file_to_change_to;
                    m_proc->messages_from_ui.push(msg);
                }
            });
    }
    juce::String m_cur_file_text{"No file loaded"};
    double m_file_playpos = 0.0;
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
                    m_cur_file_text = f.getFileName();

                    m_thumb->setSource(new juce::FileInputSource(f));
                }

                repaint();
            }
            if (msg.opcode == FilePlayerProcessor::FilePlayerMessage::Opcode::FileLoadError)
            {
                m_cur_file_text = "Error loading file";
                repaint();
            }
            if (msg.opcode == FilePlayerProcessor::FilePlayerMessage::Opcode::FilePlayPosition)
            {
                m_file_playpos = msg.value;
                repaint();
            }
            if (msg.opcode == FilePlayerProcessor::FilePlayerMessage::Opcode::ParamChange)
            {
                if (mapParToComponent.count(msg.parid))
                {
                    mapParToComponent[msg.parid]->slider.setValue(msg.value,
                                                                  juce::dontSendNotification);
                    if (msg.parid == (clap_id)FilePlayerProcessor::ParamIds::TriggeredMode)
                    {
                        m_triggered_mode = msg.value;
                    }
                }
            }
        }
    }
    bool m_triggered_mode = false;
    int m_wave_h = 148;
    void resized() override
    {
        // m_load_but.setBounds(0, 150, 100, 25);
        // m_file_label.setBounds(102, 150, 300, 25);
        m_file_comp.setBounds(0, 150, getWidth(), 25);
        if (m_par_comps.size() == 0)
            return;
        int h = m_par_comps[0]->h;
        for (int i = 0; i < m_par_comps.size(); ++i)
        {
            m_par_comps[i]->setBounds(0, 175 + h * i, getWidth(), h);
        }
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
    void paint(juce::Graphics &g) override
    {
        g.setColour(juce::Colours::black);
        g.fillRect(0, 0, getWidth(), m_wave_h);
        g.setColour(juce::Colours::darkgrey);
        juce::String txt = m_cur_file_text;
        if (m_thumb->getTotalLength() > 0.0 && m_cur_file_text != "Error loading file")
        {
            m_thumb->drawChannels(g, juce::Rectangle<int>(0, 0, getWidth(), m_wave_h), 0.0,
                                  m_thumb->getTotalLength(), 1.0f);
            g.setColour(juce::Colours::white);
            double xcor = juce::jmap<double>(m_file_playpos, 0.0, 1.0, 0.0, getWidth());
            g.drawLine(xcor, 0.0, xcor, m_wave_h);
            if (m_proc->m_triggered_mode)
                txt += " (Press mouse over waveform to play)";
        }
        g.setColour(juce::Colours::white);
        g.setFont(20);
        g.drawText(txt, 0, 0, getWidth(), m_wave_h, juce::Justification::topLeft);
    }

  private:
    FilePlayerProcessor *m_proc = nullptr;
    juce::TextButton m_load_but;
    juce::FilenameComponent m_file_comp;
    juce::Label m_file_label;
    class SliderAndLabel : public juce::Component
    {
      public:
        clap_id param_id = 0;
        SliderAndLabel(FilePlayerProcessor *proc, xenakios::ParamDesc &desc) : m_proc(proc)
        {
            param_id = desc.id;
            addAndMakeVisible(slider);
            slider.setRange(desc.minVal, desc.maxVal);
            slider.onValueChange = [this]() {
                FilePlayerProcessor::FilePlayerMessage msg;
                msg.opcode = FilePlayerProcessor::FilePlayerMessage::Opcode::ParamChange;
                msg.parid = param_id;
                msg.value = slider.getValue();
                m_proc->messages_from_ui.push(msg);
            };
            addAndMakeVisible(label);
            label.setText(desc.name, juce::dontSendNotification);
        }

        void resized() override
        {
            label.setBounds(0, 0, getWidth() / 2, h);
            slider.setBounds(label.getRight(), 0, getWidth() / 2, h);
        }
        int h = 25;
        juce::Slider slider;
        juce::Label label;
        FilePlayerProcessor *m_proc = nullptr;
    };
    std::vector<std::unique_ptr<SliderAndLabel>> m_par_comps;
    std::unordered_map<clap_id, SliderAndLabel *> mapParToComponent;
};

bool FilePlayerProcessor::guiCreate(const char *api, bool isFloating) noexcept
{
    m_editor = std::make_unique<FilePlayerEditor>(this);
    m_editor->setSize(600, 500);
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
                    reader->read(&m_file_temp_buf, 0, reader->lengthInSamples, 0, true, true);
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
        jassert(msg.filename.empty());
        if (msg.opcode == FilePlayerMessage::Opcode::ParamChange)
        {
            auto pev = xenakios::make_event_param_value(0, msg.parid, msg.value, nullptr);
            handleEvent((const clap_event_header *)&pev, true);
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
        if (msg.opcode == FilePlayerMessage::Opcode::FileLoadError)
        {
            FilePlayerMessage outmsg;
            outmsg.opcode = FilePlayerMessage::Opcode::FileLoadError;
            messages_to_ui.push(outmsg);
        }
    }
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
    int xfadelen = 4000;
    if (preserve_pitch)
    {
        double pshift = m_pitch;
        pshift += m_pitch_mod;
        pshift = std::clamp(pshift, -12.0, 12.0);
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
        rate *= compensrate;
        int samplestopush = process->frames_count * rate;
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
            consumed[ch] =
                m_resamplers[ch].process(rate, wbuf[ch], process->audio_outputs[0].data32[ch],
                                         process->frames_count, samplestopush, 0);
        }
        jassert(consumed[0] == consumed[1]);
        m_buf_playpos = (cachedpos + consumed[0]);
        if (m_buf_playpos >= loop_end_samples)
        {
            m_buf_playpos = loop_start_samples;
        }
    }
    m_gain_proc.setGainDecibels(m_volume);
    juce::dsp::AudioBlock<float> block(process->audio_outputs[0].data32, 2, process->frames_count);
    juce::dsp::ProcessContextReplacing<float> ctx(block);
    m_gain_proc.process(ctx);
    return CLAP_PROCESS_CONTINUE;
}