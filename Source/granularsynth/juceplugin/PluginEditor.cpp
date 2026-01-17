#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor &p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    addAndMakeVisible(loadModulationSettingsBut);
    loadModulationSettingsBut.setButtonText("LOAD MOD");
    loadModulationSettingsBut.onClick = [this]() {
        processorRef.settingsLoadRequested.store(true);
    };

    auto &params = p.getParameters();
    for (int i = 0; i < params.size(); ++i)
    {
        auto pare = std::make_unique<GUIParam>();
        pare->parLabel = std::make_unique<juce::Label>();
        addAndMakeVisible(pare->parLabel.get());
        auto pa = static_cast<juce::RangedAudioParameter *>(params[i]);
        pare->parLabel->setText(pa->getName(100), juce::dontSendNotification);
        if (!pa->isDiscrete())
        {
            pare->slider = std::make_unique<juce::Slider>();
            pare->slider->setTextBoxStyle(juce::Slider::TextEntryBoxPosition::TextBoxRight, false,
                                          50, 20);
            pare->slidAttach =
                std::make_unique<juce::SliderParameterAttachment>(*pa, *pare->slider, nullptr);
            addAndMakeVisible(pare->slider.get());
        }
        else
        {
            pare->combo = std::make_unique<juce::ComboBox>();
            auto strs = pa->getAllValueStrings();
            int j = 1;
            for (auto &str : strs)
            {
                pare->combo->addItem(str, j);
                ++j;
            }
            pare->choiceAttach =
                std::make_unique<juce::ComboBoxParameterAttachment>(*pa, *pare->combo, nullptr);
            addAndMakeVisible(pare->combo.get());
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
        paramEntries[i]->parLabel->setBounds(1, 1 + i * 25, 180, 24);
        if (paramEntries[i]->slider)
            paramEntries[i]->slider->setBounds(182, 1 * i * 25, getWidth() - 184, 24);
        if (paramEntries[i]->combo)
            paramEntries[i]->combo->setBounds(182, 1 * i * 25, getWidth() - 184, 24);
    }
    loadModulationSettingsBut.setBounds(1, getHeight() - 25, 100, 24);
}
