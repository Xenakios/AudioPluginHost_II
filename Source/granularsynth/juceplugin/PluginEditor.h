#pragma once

#include "PluginProcessor.h"

struct GUIParam
{
    std::unique_ptr<juce::Label> parLabel;
    std::unique_ptr<juce::Slider> slider;
    std::unique_ptr<juce::ComboBox> combo;
    std::unique_ptr<juce::SliderParameterAttachment> slidAttach;
    std::unique_ptr<juce::ComboBoxParameterAttachment> choiceAttach;
};

struct ModulationRowComponent : public juce::Component
{
    ModulationRowComponent()
    {
        addAndMakeVisible(sourceCombo);
        addAndMakeVisible(depthSlider);
        addAndMakeVisible(destCombo);
        auto updatfunc = [this]() {
            stateChangedCallback(modslotindex, sourceCombo.getSelectedItemIndex(),
                                 depthSlider.getValue(), destCombo.getSelectedItemIndex());
        };
        for (int i = 0; i < 4; ++i)
        {
            sourceCombo.addItem("LFO " + juce::String(i + 1), i + 1);
        }
        sourceCombo.onChange = updatfunc;
        depthSlider.setRange(-1.0, 1.0);
        depthSlider.setNumDecimalPlacesToDisplay(2);

        depthSlider.onValueChange = updatfunc;
        depthSlider.setTextBoxStyle(juce::Slider::TextEntryBoxPosition::TextBoxRight, false, 50,
                                    20);
        destCombo.addItem("No target", 1);
        destCombo.addItem("Density", 2);
        destCombo.addItem("Pitch", 3);
        destCombo.addItem("Azimuth", 4);
        destCombo.addItem("Filter 1 Cutoff", 5);
        destCombo.onChange = updatfunc;
    }
    void resized() override
    {
        sourceCombo.setBounds(0, 0, 150, 25);
        depthSlider.setBounds(sourceCombo.getRight() + 1, 0, 250, 25);
        destCombo.setBounds(depthSlider.getRight() + 1, 0, 150, 25);
    }
    std::function<void(int, int, float, int)> stateChangedCallback;
    int modslotindex = -1;
    juce::ComboBox sourceCombo;
    juce::Slider depthSlider;
    juce::ComboBox destCombo;
};

//==============================================================================
class AudioPluginAudioProcessorEditor final : public juce::AudioProcessorEditor
{
  public:
    explicit AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor &);
    ~AudioPluginAudioProcessorEditor() override;

    //==============================================================================
    void paint(juce::Graphics &) override;
    void resized() override;

  private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    AudioPluginAudioProcessor &processorRef;
    std::vector<std::unique_ptr<GUIParam>> paramEntries;
    juce::TextButton loadModulationSettingsBut;
    std::vector<std::unique_ptr<ModulationRowComponent>> modRowComps;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessorEditor)
};
