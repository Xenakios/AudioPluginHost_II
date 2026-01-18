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

struct LFOComponent : public juce::Component
{
    LFOComponent()
    {
        auto upfunc = [this]() {
            stateChangedCallback(lfoindex, shapeCombo.getSelectedId() - 1, rateSlider.getValue(),
                                 deformSlider.getValue());
        };
        addAndMakeVisible(shapeCombo);
        shapeCombo.addItem("SINE", 1);
        shapeCombo.addItem("RAMP", 2);
        shapeCombo.addItem("TRIANGLE", 4);
        shapeCombo.addItem("SQUARE", 5);
        shapeCombo.addItem("SMOOTH NOISE", 6);
        shapeCombo.addItem("S&H NOISE", 7);
        shapeCombo.onChange = upfunc;

        addAndMakeVisible(rateSlider);
        rateSlider.setRange(-3.0, 5.0);
        rateSlider.setNumDecimalPlacesToDisplay(2);
        rateSlider.setTextBoxStyle(juce::Slider::TextEntryBoxPosition::TextBoxRight, false, 50, 20);
        rateSlider.onValueChange = upfunc;

        addAndMakeVisible(deformSlider);
        deformSlider.setRange(-1.0, 1.0);
        deformSlider.setNumDecimalPlacesToDisplay(2);
        deformSlider.setTextBoxStyle(juce::Slider::TextEntryBoxPosition::TextBoxRight, false, 50,
                                     20);
        deformSlider.onValueChange = upfunc;
    }
    void resized()
    {
        shapeCombo.setBounds(0, 0, 100, 25);
        rateSlider.setBounds(0, shapeCombo.getBottom() + 1, 200, 25);
        deformSlider.setBounds(0, rateSlider.getBottom() + 1, 200, 25);
    }
    int lfoindex = -1;
    std::function<void(int, int, float, float)> stateChangedCallback;
    juce::ComboBox shapeCombo;
    juce::Slider rateSlider;
    juce::Slider deformSlider;
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
        for (int i = 0; i < 8; ++i)
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
    std::vector<std::unique_ptr<LFOComponent>> lfocomps;
    juce::TabbedComponent lfoTabs;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessorEditor)
};
