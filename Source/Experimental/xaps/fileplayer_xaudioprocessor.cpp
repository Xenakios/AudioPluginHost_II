#include "fileplayer_xaudioprocessor.h"

class FilePlayerEditor : public juce::Component, public juce::Timer
{
  public:
    std::unique_ptr<juce::FileChooser> m_file_chooser;
    FilePlayerEditor(FilePlayerProcessor *proc) : m_proc(proc)
    {
        addAndMakeVisible(m_load_but);
        addAndMakeVisible(m_file_label);
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

    void timerCallback() override
    {
        FilePlayerProcessor::FilePlayerMessage msg;
        while (m_proc->messages_to_ui.pop(msg))
        {
            if (msg.opcode == FilePlayerProcessor::FilePlayerMessage::Opcode::FileChanged)
            {
                if (msg.filename.data())
                    m_file_label.setText(msg.filename.data(), juce::dontSendNotification);
            }
            if (msg.opcode == FilePlayerProcessor::FilePlayerMessage::Opcode::ParamChange)
            {
                if (mapParToComponent.count(msg.parid))
                {
                    mapParToComponent[msg.parid]->slider.setValue(msg.value,
                                                                  juce::dontSendNotification);
                }
            }
        }
    }
    void resized() override
    {
        m_load_but.setBounds(0, 150, 100, 25);
        m_file_label.setBounds(102, 150, 300, 25);
        if (m_par_comps.size() == 0)
            return;
        int h = m_par_comps[0]->h;
        for (int i = 0; i < m_par_comps.size(); ++i)
        {
            m_par_comps[i]->setBounds(0, 175 + h * i, getWidth(), h);
        }
    }

  private:
    FilePlayerProcessor *m_proc = nullptr;
    juce::TextButton m_load_but;
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
        void update() {}
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

clap_process_status FilePlayerProcessor::process(const clap_process *process) noexcept
{
    handleMessagesFromUI();
    auto inevts = process->in_events;
    for (int i = 0; i < inevts->size(inevts); ++i)
    {
        auto ev = inevts->get(inevts, i);
        handleEvent(ev, false);
    }
    if (m_file_buf.getNumSamples() == 0)
    {
        // jassert(false);
        return CLAP_PROCESS_CONTINUE;
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