#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../granularsynth.h"
#include "containers/choc_SingleReaderSingleWriterFIFO.h"

inline std::string getHexDump(const void *data, size_t size)
{
    const unsigned char *p = static_cast<const unsigned char *>(data);
    std::string output;

    for (size_t i = 0; i < size; i += 16)
    {
        // 1. Format the offset (8 chars, hex, zero-padded)
        output += std::format("{:08x}: ", i);

        // 2. Format the hex bytes
        for (size_t j = 0; j < 16; ++j)
        {
            if (i + j < size)
            {
                output += std::format("{:02x} ", p[i + j]);
            }
            else
            {
                output += "   "; // Alignment padding
            }
        }

        output += " ";

        // 3. Format printable ASCII
        for (size_t j = 0; j < 16; ++j)
        {
            if (i + j < size)
            {
                unsigned char ch = p[i + j];
                output += std::isprint(ch) ? static_cast<char>(ch) : '.';
            }
        }
        output += "\n";
    }
    return output;
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

  private:
    alignas(32) std::vector<float> workBuffer;
    alignas(32) choc::fifo::SingleReaderSingleWriterFIFO<std::array<float, 16>> buffer_adapter;
    int prior_ambi_order = -1;
    std::unordered_map<juce::AudioProcessorParameter *, int> jucepartoindex;

    void sendExtraStatesToGUI();
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessor)
};
