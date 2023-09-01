#pragma once

#include "JuceHeader.h"
#include "signalsmith-stretch.h"
#include "containers/choc_SingleReaderSingleWriterFIFO.h"

class AudioFilePlayerPlugin : public AudioProcessor, public juce::Timer
{
    class Editor : public juce::AudioProcessorEditor
    {
        juce::TextButton m_import_file_but;
        juce::GenericAudioProcessorEditor m_gen_ed;
        std::unique_ptr<juce::FileChooser> m_chooser;

      public:
        Editor(AudioFilePlayerPlugin &p) : juce::AudioProcessorEditor(p), m_gen_ed(p)
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
            setSize(600, 200);
        }
        void resized() override
        {
            m_import_file_but.setBounds(1, 1, 200, 25);
            m_gen_ed.setBounds(1, 27, getWidth(), getHeight() - 28);
        }
    };

  public:
    AudioFilePlayerPlugin();
    void timerCallback() override;
    void importFile(juce::File f);
    static String getIdentifier() { return "AudioFilePlayer"; }
    void prepareToPlay(double newSampleRate, int maxBlocksize) override
    {
        m_buf_playpos = 0;
        m_stretch.presetDefault(2, newSampleRate);
        m_work_buf.setSize(2, maxBlocksize * 16);
    }

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
  private:
    juce::AudioBuffer<float> m_file_buf;
    juce::AudioBuffer<float> m_file_temp_buf;
    juce::AudioBuffer<float> m_work_buf;
    int m_buf_playpos = 0;
    signalsmith::stretch::SignalsmithStretch<float> m_stretch;
    
};
