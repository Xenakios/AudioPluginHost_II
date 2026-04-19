#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../granularsynth.h"
#include "containers/choc_SingleReaderSingleWriterFIFO.h"

inline bool is_debug()
{
#ifdef JUCE_DEBUG
    return true;
#else
    return false;
#endif
}

struct ParameterMessage
{
    uint32_t id = 0;
    float value = 0.0f;
};

struct ThreadMessage
{
    enum OpCode
    {
        OP_NOOP,
        OP_MODROUTING,
        OP_MODPARAM,
        OP_FILTERTYPE
    };
    OpCode opcode = OP_NOOP;
    int16_t modslot = -1;
    int modsource = -1;
    int modvia = 0;
    int modcurve = 0;
    float modcurvepar0 = 0.0f;
    float depth = 0.0f;
    int moddest = -1;
    int16_t filterindex = -1;
    uint8_t insertmainmode = 0;
    uint8_t awtype = 0;
    sfpp::FilterModel filtermodel;
    sfpp::ModelConfig filterconfig;
};

class AudioPluginAudioProcessor final : public juce::AudioProcessor
{
  public:
    AudioPluginAudioProcessor();
    ~AudioPluginAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout &layouts) const override;

    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor *createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String &newName) override;

    void getStateInformation(juce::MemoryBlock &destData) override;
    void setStateInformation(const void *data, int sizeInBytes) override;
    ToneGranulator granulator;
    choc::fifo::SingleReaderSingleWriterFIFO<ThreadMessage> from_gui_fifo;
    choc::fifo::SingleReaderSingleWriterFIFO<ParameterMessage> params_from_gui_fifo;
    choc::fifo::SingleReaderSingleWriterFIFO<ParameterMessage> params_to_gui_fifo;
    choc::fifo::SingleReaderSingleWriterFIFO<ThreadMessage> to_gui_fifo;
    juce::AudioProcessLoadMeasurer perfMeasurer;
    juce::ThreadPool tpool{juce::ThreadPool::Options{"granulatorworker", 1}};
    juce::TimeSliceThread sliceThread{"granulatortimeslicethread"};
    void startRecording();
    void stopRecording();
    std::atomic<bool> isRecording{false};
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter;
    juce::AudioBuffer<float> recordBuffer;
    choc::value::Value getState();
    void setState(choc::value::ValueView state, bool ignoreMasterVolume);
    void sendExtraStatesToGUI();
  private:
    alignas(32) std::vector<float> workBuffer;
    alignas(32) choc::fifo::SingleReaderSingleWriterFIFO<std::array<float, 16>> buffer_adapter;
    void setStateDirtyHack();
    int prior_ambi_order = -1;
    std::unordered_map<juce::AudioProcessorParameter *, int> jucepartoindex;
    juce::AudioParameterFloat* dirtyStateParam = nullptr;
    
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessor)
};
