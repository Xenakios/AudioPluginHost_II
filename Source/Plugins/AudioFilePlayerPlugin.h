#pragma once

#include "JuceHeader.h"
#include "signalsmith-stretch.h"
#include "containers/choc_SingleReaderSingleWriterFIFO.h"

class AudioFilePlayerPlugin;

class WaveFormComponent : public juce::Component, public juce::Timer
{
  public:
    WaveFormComponent(AudioFilePlayerPlugin &p);
    void timerCallback() override;
    void paint(juce::Graphics &g) override;
    void loadFile(juce::File f);

  private:
    AudioFilePlayerPlugin &m_proc;
    juce::AudioThumbnailCache m_thumb_cache;
    std::unique_ptr<juce::AudioThumbnail> m_thumb;
    juce::AudioFormatManager m_aman;
};

class AudioFilePlayerPlugin : public AudioProcessor, public juce::Timer
{

  public:
    AudioFilePlayerPlugin();
    void timerCallback() override;
    void importFile(juce::File f);
    static String getIdentifier() { return "AudioFilePlayer"; }
    void prepareToPlay(double newSampleRate, int maxBlocksize) override;
    void reset() override {}
    void releaseResources() override {}

    void processBlock(AudioBuffer<float> &buffer, MidiBuffer &) override;

    using AudioProcessor::processBlock;

    const String getName() const override { return getIdentifier(); }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    AudioProcessorEditor *createEditor() override;
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
        CrossThreadMessage(Opcode opcode_, int par0_ = 0, int par1_ = 0, float par2_ = 0.0f)
            : opcode(opcode_), par0(par0_), par1(par1_), par2(par2_)
        {
        }
        Opcode opcode = Opcode::NoAction;
        int par0 = 0;
        int par1 = 0;
        float par2 = 0.0f;
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
    juce::AudioParameterFloat *m_par_loop_start = nullptr;
    juce::AudioParameterFloat *m_par_loop_end = nullptr;
    juce::AudioParameterBool *m_par_preserve_pitch = nullptr;
    juce::File getCurrentFile() const { return m_cur_file; }

  private:
    juce::AudioBuffer<float> m_file_buf;
    juce::AudioBuffer<float> m_file_temp_buf;
    juce::AudioBuffer<float> m_work_buf;
    int m_buf_playpos = 0;
    // we don't want to make the hot play position atomic for perf reasons, but we need atomicity
    // for the GUI
    std::atomic<int> m_buf_playpos_atomic{0};
    signalsmith::stretch::SignalsmithStretch<float> m_stretch;
    std::array<juce::LagrangeInterpolator, 2> m_resamplers;
    std::vector<float> m_resampler_work_buf;
    juce::dsp::Gain<float> m_gain;
    double m_file_sample_rate = 1.0;
    juce::Range<double> m_loop_range{0.0, 1.0};
    juce::AudioParameterFloat *m_par_pitch = nullptr;
    juce::AudioParameterFloat *m_par_rate = nullptr;
    juce::AudioParameterFloat *m_par_volume = nullptr;
    
    juce::File m_cur_file;
};

class AudioFilePlayerPluginEditor : public juce::AudioProcessorEditor, public juce::Timer
{
    juce::TextButton m_import_file_but;
    
    juce::Slider m_slider_rate;
    juce::Slider m_slider_pitch;
    juce::Slider m_slider_volume;
    juce::Slider m_slider_loop_start;
    juce::Slider m_slider_loop_end;
    juce::ToggleButton m_toggle_preserve_pitch;
    std::unique_ptr<juce::FileChooser> m_chooser;
    juce::Label m_infolabel;
    AudioFilePlayerPlugin &m_plug;
    WaveFormComponent m_wavecomponent;

  public:
    AudioFilePlayerPluginEditor(AudioFilePlayerPlugin &p);
    void timerCallback() override;
    void resized() override;
};
