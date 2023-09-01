#pragma once

#include "JuceHeader.h"
#include "signalsmith-stretch.h"
#include "containers/choc_SingleReaderSingleWriterFIFO.h"

class AudioFilePlayerPlugin : public AudioProcessor, public juce::Timer
{
    class Editor : public juce::AudioProcessorEditor, public juce::Timer
    {
        juce::TextButton m_import_file_but;
        juce::GenericAudioProcessorEditor m_gen_ed;
        std::unique_ptr<juce::FileChooser> m_chooser;
        juce::Label m_infolabel;
        AudioFilePlayerPlugin &m_plug;

      public:
        Editor(AudioFilePlayerPlugin &p) : juce::AudioProcessorEditor(p), m_gen_ed(p), m_plug(p)
        {
            addAndMakeVisible(m_import_file_but);
            m_import_file_but.setButtonText("Import file...");
            m_import_file_but.onClick = [&p, this]() {
                m_chooser =
                    std::make_unique<juce::FileChooser>("Choose audio file", juce::File(), "*.wav");
                m_chooser->launchAsync(0, [&p](const juce::FileChooser &chooser) {
                    if (chooser.getResult() != juce::File())
                    {
                        p.importFile(chooser.getResult());
                    }
                });
            };
            addAndMakeVisible(m_gen_ed);
            addAndMakeVisible(m_infolabel);
            setSize(600, 200);
            startTimerHz(10);
        }
        void timerCallback() override
        {
            double dur = m_plug.getFileDurationSeconds();
            double pos = m_plug.getFilePlayPositionSeconds();
            m_infolabel.setText(juce::String(pos, 1) + " / " + juce::String(dur, 1),
                                juce::dontSendNotification);
        }
        void resized() override
        {
            m_import_file_but.setBounds(1, 1, 200, 25);
            m_gen_ed.setBounds(1, 27, getWidth() - 2, getHeight() - 28);
            m_infolabel.setBounds(1, getHeight() - 20, getWidth() - 2, 20);
        }
    };

  public:
    AudioFilePlayerPlugin();
    void timerCallback() override;
    void importFile(juce::File f);
    static String getIdentifier() { return "AudioFilePlayer"; }
    void prepareToPlay(double newSampleRate, int maxBlocksize) override;
    void reset() override {}
    void releaseResources() override {}
    juce::AudioParameterFloat *getFloatParam(int index)
    {
        return dynamic_cast<juce::AudioParameterFloat *>(getParameters()[index]);
    }

    void processBlock(AudioBuffer<float> &buffer, MidiBuffer &) override;

    using AudioProcessor::processBlock;

    const String getName() const override { return getIdentifier(); }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    AudioProcessorEditor *createEditor() override { return new Editor(*this); }
    bool hasEditor() const override { return true; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const String getProgramName(int) override { return {}; }
    void changeProgramName(int, const String &) override {}
    void getStateInformation(juce::MemoryBlock &) override {}
    void setStateInformation(const void *, int) override {}
    struct CrossThreadMessage
    {
        enum class Opcode
        {
            NoAction,
            SwapBufferInAudioThread,
            ClearTempBufferInGuiThread
        };
        CrossThreadMessage() {}
        CrossThreadMessage(Opcode opcode_) : opcode(opcode_) {}
        Opcode opcode = Opcode::NoAction;
    };
    choc::fifo::SingleReaderSingleWriterFIFO<CrossThreadMessage> m_from_gui_fifo;
    choc::fifo::SingleReaderSingleWriterFIFO<CrossThreadMessage> m_to_gui_fifo;
    double getFileDurationSeconds() const
    {
        if (m_file_sample_rate >= 1.0)
            return m_file_buf.getNumSamples() / m_file_sample_rate;
        return 0.0;
    }
    double getFilePlayPositionSeconds() const
    {
        if (m_file_sample_rate >= 1.0)
            return m_buf_playpos_atomic.load() / m_file_sample_rate;
        return 0.0;
    }

  private:
    juce::AudioBuffer<float> m_file_buf;
    juce::AudioBuffer<float> m_file_temp_buf;
    juce::AudioBuffer<float> m_work_buf;
    int m_buf_playpos = 0;
    // we don't want to make the hot play position atomic for perf reasons, but we need atomicity
    // for the GUI
    std::atomic<int> m_buf_playpos_atomic{0};
    signalsmith::stretch::SignalsmithStretch<float> m_stretch;
    juce::dsp::Gain<float> m_gain;
    double m_file_sample_rate = 1.0;
};
