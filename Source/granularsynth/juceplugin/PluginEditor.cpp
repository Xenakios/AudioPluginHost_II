#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor &p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    juce::ignoreUnused(processorRef);
    auto &params = p.getParameters();
    for (int i = 0; i < params.size(); ++i)
    {
        auto pare = std::make_unique<GUIParam>();
        pare->parLabel = std::make_unique<juce::Label>();
        addAndMakeVisible(pare->parLabel.get());
        auto pa = static_cast<juce::RangedAudioParameter*>(params[i]);
        pare->parLabel->setText(pa->getName(100), juce::dontSendNotification);
        if (!pa->isDiscrete())
        {
            pare->slider = std::make_unique<juce::Slider>();
            pare->slidAttach =
                std::make_unique<juce::SliderParameterAttachment>(*pa, *pare->slider, nullptr);
            addAndMakeVisible(pare->slider.get());
        }
        paramEntries.push_back(std::move(pare));
    }
    setSize(500, 500);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor() {}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint(juce::Graphics &g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll(juce::Colours::darkgrey);
}

void AudioPluginAudioProcessorEditor::resized()
{
    for (int i = 0; i < paramEntries.size(); ++i)
    {
        paramEntries[i]->parLabel->setBounds(1, 1 + i * 25, 200, 24);
        if (paramEntries[i]->slider)
            paramEntries[i]->slider->setBounds(202, 1 * i * 25, getWidth() - 204, 24);
    }
}
