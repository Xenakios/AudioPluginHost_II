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
        addAndMakeVisible(viaCombo);
        addAndMakeVisible(depthSlider);
        addAndMakeVisible(destCombo);
        auto updatfunc = [this]() {
            stateChangedCallback(modslotindex, sourceCombo.getSelectedItemIndex(),
                                 viaCombo.getSelectedItemIndex(), depthSlider.getValue(),
                                 destCombo.getSelectedItemIndex());
        };
        sourceCombo.addItem("Off", 1);
        viaCombo.addItem("Off", 1);
        for (int i = 0; i < 8; ++i)
        {
            sourceCombo.addItem("LFO " + juce::String(i + 1), i + 2);
            viaCombo.addItem("LFO " + juce::String(i), i + 2);
        }
        for (int i = 0; i < 8; ++i)
        {
            sourceCombo.addItem("MIDI CC " + juce::String(21 + i), 100 + i);
            viaCombo.addItem("MIDI CC " + juce::String(21 + i), 100 + i);
        }

        sourceCombo.setSelectedItemIndex(0, juce::dontSendNotification);
        viaCombo.setSelectedItemIndex(0, juce::dontSendNotification);
        sourceCombo.onChange = updatfunc;
        viaCombo.onChange = updatfunc;

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
        auto layout = juce::FlexBox(juce::FlexBox::Direction::row, juce::FlexBox::Wrap::noWrap,
                                    juce::FlexBox::AlignContent::spaceAround,
                                    juce::FlexBox::AlignItems::stretch,
                                    juce::FlexBox::JustifyContent::flexStart);
        layout.items.add(juce::FlexItem(sourceCombo).withFlex(1.0));
        layout.items.add(juce::FlexItem(viaCombo).withFlex(1.0));
        layout.items.add(juce::FlexItem(depthSlider).withFlex(2.0));
        layout.items.add(juce::FlexItem(destCombo).withFlex(1.0));
        layout.performLayout(juce::Rectangle<int>{0, 0, getWidth(), getHeight()});
    }
    std::function<void(int, int, int, float, int)> stateChangedCallback;
    int modslotindex = -1;
    juce::ComboBox sourceCombo;
    juce::ComboBox viaCombo;
    juce::Slider depthSlider;
    juce::ComboBox destCombo;
};

//==============================================================================
class AudioPluginAudioProcessorEditor final : public juce::AudioProcessorEditor, public juce::Timer
{
  public:
    explicit AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor &);
    ~AudioPluginAudioProcessorEditor() override;
    void timerCallback() override;
    //==============================================================================
    void paint(juce::Graphics &) override;
    void resized() override;

  private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    AudioPluginAudioProcessor &processorRef;
    std::vector<std::unique_ptr<GUIParam>> paramEntries;
    juce::TextButton loadModulationSettingsBut;
    juce::TextButton filter0But;
    juce::TextButton filter1But;
    std::vector<std::unique_ptr<ModulationRowComponent>> modRowComps;
    std::vector<std::unique_ptr<LFOComponent>> lfocomps;
    juce::TabbedComponent lfoTabs;
    void showFilterMenu(int whichfilter);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessorEditor)
};
