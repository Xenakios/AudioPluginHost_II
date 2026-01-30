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
                                 deformSlider.getValue(), shiftSlider.getValue(),
                                 warpSlider.getValue(), unipolarButton.getToggleState());
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
        warpSlider.setTextBoxStyle(juce::Slider::TextEntryBoxPosition::TextBoxRight, false, 50, 20);
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

struct StepSeqComponent : public juce::Component
{

    void updateGUI()
    {
        unipolarBut.setToggleState(gr->stepModSources[sindex].unipolar.load(),
                                   juce::dontSendNotification);
        repaint();
    }
    StepSeqComponent(int seqindex, ToneGranulator *g) : gr(g), sindex(seqindex)
    {
        addAndMakeVisible(loadStepsBut);
        loadStepsBut.setButtonText("Run Python");
        loadStepsBut.onClick = [this, seqindex]() {
            juce::ChildProcess cp;
            cp.start(std::format(
                R"(python C:\develop\AudioPluginHost_mk2\Source\granularsynth\stepseq.py {})",
                seqindex));
            auto data = cp.readAllProcessOutput();
            if (!data.containsIgnoreCase("error"))
            {
                auto tokens = juce::StringArray::fromTokens(data, false);
                std::vector<float> steps;
                for (auto &e : tokens)
                {
                    if (e.isEmpty())
                        break;
                    float v = std::clamp(e.getFloatValue(), -1.0f, 1.0f);
                    // DBG(v);
                    steps.push_back(v);
                }
                gr->stepModSources[sindex].setSteps(steps);
            }
            else
            {
                DBG(data);
            }
        };
        addAndMakeVisible(unipolarBut);
        unipolarBut.setButtonText("Unipolar");
        unipolarBut.onClick = [this]() {
            gr->stepModSources[sindex].unipolar.store(unipolarBut.getToggleState());
        };
        
    }
    void resized() override
    {
        loadStepsBut.setBounds(0, 0, 150, 25);
        unipolarBut.setBounds(0, loadStepsBut.getBottom() + 1, 150, 25);
    }
    void paint(juce::Graphics &g) override
    {
        auto &msrc = gr->stepModSources[sindex];
        g.setColour(juce::Colours::green);
        int maxstepstodraw = (getWidth() - 150) / 16;
        int stepstodraw = std::min<int>(maxstepstodraw, msrc.numactivesteps);
        for (int i = 0; i < stepstodraw; ++i)
        {
            float xcor = 150.0 + i * 16.0;
            float v = msrc.steps[i];
            if (v < 0.0)
            {
                float h = juce::jmap<float>(v, -1.0, 0.0, getHeight() / 2, 0.0);
                g.fillRect(xcor, getHeight() / 2.0, 15.0, h);
            }
            else
            {

                float h = juce::jmap<float>(v, 0.0, 1.0, 0.0, getHeight() / 2);
                g.fillRect(xcor, getHeight() / 2.0 - h, 15.0, h);
            }
        }
        g.setColour(juce::Colours::black);
        g.drawLine(150.0f, getHeight() / 2.0f, getWidth(), getHeight() / 2.0f);
    }
    juce::TextButton loadStepsBut;
    juce::ToggleButton unipolarBut;
    ToneGranulator *gr = nullptr;
    uint32_t sindex = 0;
};

struct ModulationRowComponent : public juce::Component
{
    ModulationRowComponent(ToneGranulator *g) : gr(g)
    {
        addAndMakeVisible(sourceCombo);
        addAndMakeVisible(viaCombo);
        addAndMakeVisible(depthSlider);
        addAndMakeVisible(destCombo);
        auto updatfunc = [this]() {
            stateChangedCallback(modslotindex, sourceCombo.getSelectedId() - 1,
                                 viaCombo.getSelectedId() - 1, depthSlider.getValue(), targetID);
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
            if (pmd.flags & CLAP_PARAM_IS_MODULATABLE)
            {
                destCombo.addItem(pmd.name, pmd.id);
            }
        }
        destCombo.setSelectedItemIndex(0, juce::dontSendNotification);
        destCombo.onChange = updatfunc;

        addAndMakeVisible(destButton);
        destButton.onClick = [this, g, updatfunc]() {
            juce::PopupMenu menu;
            std::map<std::string, juce::PopupMenu> submenus;
            auto updf = [this, updatfunc](uint32_t parid) {
                setTarget(parid);
                updatfunc();
            };
            menu.addItem("No target", [updf]() { updf(1); });
            for (auto &pmd : g->parmetadatas)
            {
                if (pmd.flags & CLAP_PARAM_IS_MODULATABLE)
                {
                    if (pmd.groupName.empty())
                    {
                        menu.addItem(pmd.name, [updf, pmd]() { updf(pmd.id); });
                    }
                    else
                    {
                        if (!submenus.count(pmd.groupName))
                        {
                            submenus[pmd.groupName] = juce::PopupMenu();
                        }
                        submenus[pmd.groupName].addItem(pmd.name, [updf, pmd]() { updf(pmd.id); });
                    }
                }
            }
            for (auto &e : submenus)
            {
                menu.addSubMenu(e.first, e.second);
            }
            menu.showMenuAsync(juce::PopupMenu::Options{});
        };
    }
    void setTarget(uint32_t parid)
    {
        targetID = parid;
        for (auto &pmd : gr->parmetadatas)
        {
            if (pmd.id == parid)
            {

                destButton.setButtonText(pmd.name);
                return;
            }
        }
        if (parid == 1)
            destButton.setButtonText("No target");
        else
            destButton.setButtonText("INVALID ID");
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
        // layout.items.add(juce::FlexItem(destCombo).withFlex(1.0));
        layout.items.add(juce::FlexItem(destButton).withFlex(1.0));
        layout.performLayout(juce::Rectangle<int>{0, 0, getWidth(), getHeight()});
    }
    ToneGranulator *gr = nullptr;
    std::function<void(int, int, int, float, int)> stateChangedCallback;
    int modslotindex = -1;
    uint32_t targetID = 1;
    juce::ComboBox sourceCombo;
    juce::ComboBox viaCombo;
    juce::Slider depthSlider;
    juce::ComboBox destCombo;
    juce::TextButton destButton;
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
    std::vector<std::unique_ptr<StepSeqComponent>> stepcomps;
    void showFilterMenu(int whichfilter);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessorEditor)
};
