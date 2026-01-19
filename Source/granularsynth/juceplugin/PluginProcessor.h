#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../granularsynth.h"
#include "containers/choc_SingleReaderSingleWriterFIFO.h"

struct ThreadMessage
{
    int opcode = 0;
    int modslot = -1;
    int modsource = -1;
    float depth = 0.0f;
    int moddest = -1;
    int lfoindex = -1;
    int lfoshape = -1;
    float lforate = 0.0f;
    float lfodeform = 0.0f;
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
    ToneGranulator granulator{44100.0, 0, "fast_svf/lowpass", "none", 0.001f, 0.001f};
    choc::fifo::SingleReaderSingleWriterFIFO<ThreadMessage> from_gui_fifo;
    choc::fifo::SingleReaderSingleWriterFIFO<ThreadMessage> to_gui_fifo;
  private:
    std::vector<float> workBuffer;
    juce::AudioParameterChoice *parAmbiOrder = nullptr;
    int prior_ambi_order = -1;
    juce::AudioParameterChoice *parOscType = nullptr;
    juce::AudioParameterFloat *parGrainRate = nullptr;
    juce::AudioParameterFloat *parGrainDuration = nullptr;
    juce::AudioParameterFloat *parGrainCenterPitch = nullptr;
    juce::AudioParameterFloat *parGrainCenterAzimuth = nullptr;
    juce::AudioParameterFloat *parGrainCenterElevation = nullptr;
    juce::AudioParameterFloat *parGrainFilter0Cutoff = nullptr;
    juce::AudioParameterFloat *parGrainFilter0Reson = nullptr;
    juce::AudioParameterFloat *parGrainFMPitch = nullptr;
    juce::AudioParameterFloat *parGrainFMDepth = nullptr;
    void sendLFOStatesToGUI();
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessor)
};
