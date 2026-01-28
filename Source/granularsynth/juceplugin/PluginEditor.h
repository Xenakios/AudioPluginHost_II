#pragma once

#include "PluginProcessor.h"

struct GUIParam : public juce::Component
{
    std::unique_ptr<juce::Label> parLabel;
    std::unique_ptr<juce::Slider> slider;
    std::unique_ptr<juce::ComboBox> combo;
    std::unique_ptr<juce::SliderParameterAttachment> slidAttach;
    std::unique_ptr<juce::ComboBoxParameterAttachment> choiceAttach;
    GUIParam() {}
    void resized() override
    {
        auto layout = juce::FlexBox(juce::FlexBox::Direction::row, juce::FlexBox::Wrap::noWrap,
                                    juce::FlexBox::AlignContent::spaceAround,
                                    juce::FlexBox::AlignItems::stretch,
                                    juce::FlexBox::JustifyContent::flexStart);
        layout.items.add(juce::FlexItem(*parLabel).withFlex(1.0));
        if (slider)
            layout.items.add(juce::FlexItem(*slider).withFlex(2.5));
        if (combo)
            layout.items.add(juce::FlexItem(*combo).withFlex(2.5));
        layout.performLayout(juce::Rectangle<int>{0, 0, getWidth(), getHeight()});
    }
};

struct LFOComponent : public juce::Component
{
    LFOComponent()
    {
        auto upfunc = [this]() {
            stateChangedCallback(lfoindex, shapeCombo.getSelectedId() - 1, rateSlider.getValue(),
                                 deformSlider.getValue(), shiftSlider.getValue(), warpSlider.getValue(),
                                 unipolarButton.getToggleState());
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

        addAndMakeVisible(shiftSlider);
        shiftSlider.setRange(-1.0, 1.0);
        shiftSlider.setNumDecimalPlacesToDisplay(2);
        shiftSlider.setTextBoxStyle(juce::Slider::TextEntryBoxPosition::TextBoxRight, false, 50,
                                    20);
        shiftSlider.onValueChange = upfunc;

        addAndMakeVisible(warpSlider);
        warpSlider.setRange(-1.0, 1.0);
        warpSlider.setNumDecimalPlacesToDisplay(2);
        warpSlider.setTextBoxStyle(juce::Slider::TextEntryBoxPosition::TextBoxRight, false, 50,
                                    20);
        warpSlider.onValueChange = upfunc;

        addAndMakeVisible(unipolarButton);
        unipolarButton.setButtonText("Unipolar");
        unipolarButton.onClick = upfunc;
    }
    void resized()
    {
        shapeCombo.setBounds(0, 0, 100, 25);
        unipolarButton.setBounds(shapeCombo.getRight() + 1, 0, 100, 25);
        rateSlider.setBounds(0, shapeCombo.getBottom() + 1, 200, 25);
        deformSlider.setBounds(0, rateSlider.getBottom() + 1, 200, 25);
        shiftSlider.setBounds(rateSlider.getRight() + 1, shapeCombo.getBottom() + 1, 200, 25);
        warpSlider.setBounds(rateSlider.getRight() + 1, shiftSlider.getBottom() + 1, 200, 25);
    }
    int lfoindex = -1;
    std::function<void(int, int, float, float, float, float, bool)> stateChangedCallback;
    juce::ComboBox shapeCombo;
    juce::Slider rateSlider;
    juce::Slider deformSlider;
    juce::Slider shiftSlider;
    juce::Slider warpSlider;
    juce::ToggleButton unipolarButton;
};

struct ModulationRowComponent : public juce::Component
{
    ModulationRowComponent(ToneGranulator *g)
    {
        addAndMakeVisible(sourceCombo);
        addAndMakeVisible(viaCombo);
        addAndMakeVisible(depthSlider);
        addAndMakeVisible(destCombo);
        auto updatfunc = [this]() {
            stateChangedCallback(modslotindex, sourceCombo.getSelectedId() - 1,
                                 viaCombo.getSelectedId() - 1, depthSlider.getValue(),
                                 destCombo.getSelectedId());
        };
        for (int i = 0; i < g->modSources.size(); ++i)
        {
            auto &ms = g->modSources[i];
            sourceCombo.addItem(ms.name, ms.id.src + 1);
            viaCombo.addItem(ms.name, ms.id.src + 1);
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
        for (auto &pmd : g->parmetadatas)
        {
            if (pmd.flags == 1)
            {
                destCombo.addItem(pmd.name, pmd.id);
            }
        }
        destCombo.setSelectedItemIndex(0, juce::dontSendNotification);
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
