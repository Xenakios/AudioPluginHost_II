#pragma once

#include "PluginProcessor.h"

struct DropDownComponent : public juce::Component
{
    struct Node
    {
        std::string text;
        int id = -1;
        std::vector<Node> children;
    };
    Node rootNode;
    DropDownComponent() {}
    void buildMenusRecur(Node &n, juce::PopupMenu &menu)
    {
        if (n.children.size() == 0)
            menu.addItem(n.text, [this, n]() {
                selectedId = n.id;
                if (OnItemSelected)
                    OnItemSelected();
                selectedText = n.text;
                repaint();
            });
        if (n.children.size() > 0)
        {
            juce::PopupMenu submenu;
            for (auto &e : n.children)
            {
                buildMenusRecur(e, submenu);
            }
            menu.addSubMenu(n.text, submenu);
        }
    }
    Node *findNodeRecur(Node &n, int tofind)
    {
        if (n.id == tofind)
            return &n;
        for (int i = 0; i < n.children.size(); ++i)
        {
            auto ptr = findNodeRecur(n.children[i], tofind);
            if (ptr)
                return ptr;
        }
        return nullptr;
    }
    void setSelectedId(int id)
    {
        Node *found = nullptr;
        for (auto &e : rootNode.children)
        {
            found = findNodeRecur(e, id);
            if (found)
                break;
        }
        if (found)
        {
            selectedId = found->id;
            selectedText = found->text;
            repaint();
        }
    }

    void showNodes()
    {
        juce::PopupMenu menu;
        if (!rootNode.text.empty())
        {
            menu.addSectionHeader(rootNode.text);
            menu.addSeparator();
        }
        for (auto &e : rootNode.children)
        {
            buildMenusRecur(e, menu);
        }
        menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(this));
    }

    int getSelectedId() { return selectedId; }
    void paint(juce::Graphics &g) override
    {
        g.fillAll(juce::Colours::black);
        g.setColour(juce::Colours::white);
        g.drawRect(0, 0, getWidth(), getHeight(), 2);
        g.drawText(selectedText, 4, 2, getWidth(), getHeight() - 4,
                   juce::Justification::centredLeft);
    }
    void mouseDown(const juce::MouseEvent &ev) override { showMenu(); }
    void showMenu() { showNodes(); }

    int selectedId = 0;
    std::string selectedText;
    std::function<void(void)> OnItemSelected;
};

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
        // shapeCombo.addItem("RAMP", 2);
        // shapeCombo.addItem("TRIANGLE", 4);
        shapeCombo.addItem("SINE->SQUARE->TRIANGLE", 5);
        shapeCombo.addItem("SMOOTH NOISE", 6);
        shapeCombo.addItem("S&H NOISE", 7);
        shapeCombo.addItem("DOWN->TRI->UP", 9);
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
        shapeCombo.setBounds(0, 0, 200, 25);
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
    void runExternalProgram()
    {
        juce::ChildProcess cp;
        double t0 = juce::Time::getMillisecondCounterHiRes();
        cp.start(std::format(
            R"(python C:\develop\AudioPluginHost_mk2\Source\granularsynth\stepseq.py {} {})",
            sindex, par0Slider.getValue()));
        auto data = cp.readAllProcessOutput();
        double t1 = juce::Time::getMillisecondCounterHiRes();
        DBG("running ext program took " << t1 - t0 << " millisecods");
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
            gr->setStepSequenceSteps(sindex, steps);
        }
        else
        {
            DBG(data);
        }
    }
    StepSeqComponent(int seqindex, ToneGranulator *g) : gr(g), sindex(seqindex)
    {
        addAndMakeVisible(loadStepsBut);
        loadStepsBut.setButtonText("Run Python");
        loadStepsBut.onClick = [this, seqindex]() { runExternalProgram(); };
        addAndMakeVisible(unipolarBut);
        unipolarBut.setButtonText("Unipolar");
        unipolarBut.onClick = [this]() {
            gr->stepModSources[sindex].unipolar.store(unipolarBut.getToggleState());
        };
        addAndMakeVisible(par0Slider);
        par0Slider.setRange(0.0, 1.0);
        par0Slider.setNumDecimalPlacesToDisplay(2);
        par0Slider.onDragEnd = [this]() { runExternalProgram(); };
    }
    int graphxpos = 200;
    void resized() override
    {
        loadStepsBut.setBounds(0, 0, 150, 25);
        unipolarBut.setBounds(0, loadStepsBut.getBottom() + 1, graphxpos, 25);
        par0Slider.setBounds(0, unipolarBut.getBottom() + 1, graphxpos, 25);
    }
    void paint(juce::Graphics &g) override
    {
        auto &msrc = gr->stepModSources[sindex];
        g.setColour(juce::Colours::green);
        int maxstepstodraw = (getWidth() - graphxpos) / 16;
        int stepstodraw = std::min<int>(maxstepstodraw, msrc.numactivesteps);
        for (int i = 0; i < stepstodraw; ++i)
        {
            float xcor = graphxpos + i * 16.0;
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
        g.drawLine(float(graphxpos), getHeight() / 2.0f, getWidth(), getHeight() / 2.0f);
    }
    juce::TextButton loadStepsBut;
    juce::ToggleButton unipolarBut;
    juce::Slider par0Slider;
    ToneGranulator *gr = nullptr;
    uint32_t sindex = 0;
};

struct ModulationRowComponent : public juce::Component
{
    ModulationRowComponent(ToneGranulator *g) : gr(g)
    {
        addAndMakeVisible(sourceDrop);
        addAndMakeVisible(viaDrop);
        addAndMakeVisible(depthSlider);

        auto updatfunc = [this] {
            CallbackParams pars{false,
                                modslotindex,
                                sourceDrop.selectedId,
                                viaDrop.selectedId,
                                curveDrop.selectedId,
                                curveParEditor.getText().getFloatValue(),
                                (float)depthSlider.getValue(),
                                (uint32_t)destDrop.selectedId};
            stateChangedCallback(pars);
        };
        for (int i = 0; i < g->modSources.size(); ++i)
        {
            auto &ms = g->modSources[i];
            sourceDrop.rootNode.children.push_back({ms.name, (int)ms.id.src});
            viaDrop.rootNode.children.push_back({ms.name, (int)ms.id.src});
        }
        sourceDrop.setSelectedId(0);
        sourceDrop.OnItemSelected = updatfunc;
        viaDrop.setSelectedId(0);
        viaDrop.OnItemSelected = updatfunc;

        depthSlider.setRange(-1.0, 1.0);
        depthSlider.setNumDecimalPlacesToDisplay(2);
        depthSlider.onValueChange = [this]() {
            CallbackParams pars{true,
                                modslotindex,
                                sourceDrop.selectedId,
                                viaDrop.selectedId,
                                curveDrop.selectedId,
                                curveParEditor.getText().getFloatValue(),
                                (float)depthSlider.getValue(),
                                (uint32_t)destDrop.selectedId};
            stateChangedCallback(pars);
        };
        depthSlider.setTextBoxStyle(juce::Slider::TextEntryBoxPosition::TextBoxRight, false, 50,
                                    20);

        addAndMakeVisible(curveDrop);
        using mcf = GranulatorModConfig;
        using Node = DropDownComponent::Node;
        curveDrop.rootNode.text = "Curve";
        curveDrop.rootNode.children.push_back(Node{"Linear", GranulatorModConfig::CURVE_LINEAR});
        curveDrop.rootNode.children.push_back(Node{"x^2", GranulatorModConfig::CURVE_SQUARE});
        curveDrop.rootNode.children.push_back(Node{"x^3", GranulatorModConfig::CURVE_CUBE});
        Node xornode{"XOR"};
        for (int i = 0; i < 4; ++i)
        {
            xornode.children.push_back(
                Node{std::format("XOR {}", i + 1), GranulatorModConfig::CURVE_XOR1 + i});
        }
        curveDrop.rootNode.children.emplace_back(xornode);
        Node stepnode{"STEPS"};
        for (int i = 0; i < 4; ++i)
        {
            stepnode.children.push_back(
                Node{std::format("{} STEPS", 4 + i), GranulatorModConfig::CURVE_STEPS4 + i});
        }
        curveDrop.rootNode.children.emplace_back(stepnode);
        curveDrop.rootNode.children.push_back(
            Node{"BIT MIRROR", GranulatorModConfig::CURVE_BITMIRROR});
        curveDrop.setSelectedId(GranulatorModConfig::CURVE_LINEAR);
        curveDrop.OnItemSelected = updatfunc;

        curveParEditor.setText("0.0", juce::dontSendNotification);
        curveParEditor.onReturnKey = updatfunc;

        addAndMakeVisible(destDrop);
        destDrop.rootNode.text = "Modulation target";
        destDrop.rootNode.children.push_back({"No target", 1});
        std::map<std::string, Node *> nodemap;
        destDrop.rootNode.children.reserve(128);
        for (auto &pmd : g->parmetadatas)
        {
            if (pmd.flags & CLAP_PARAM_IS_MODULATABLE && !pmd.groupName.empty())
            {
                if (nodemap.count(pmd.groupName) == 0)
                {
                    destDrop.rootNode.children.push_back({pmd.groupName, 0});
                    nodemap[pmd.groupName] = &destDrop.rootNode.children.back();
                }
            }
        }
        for (auto &pmd : g->parmetadatas)
        {
            if (pmd.flags & CLAP_PARAM_IS_MODULATABLE)
            {
                if (pmd.groupName.empty())
                {
                    destDrop.rootNode.children.push_back({pmd.name, (int)pmd.id});
                }
                else
                {
                    nodemap[pmd.groupName]->children.push_back({pmd.name, (int)pmd.id});
                }
            }
        }
        destDrop.setSelectedId(1);
        destDrop.OnItemSelected = updatfunc;
    }
    void setTarget(uint32_t parid)
    {
        targetID = parid;
        destDrop.setSelectedId(parid);
    }

    void resized() override
    {
        auto layout = juce::FlexBox(juce::FlexBox::Direction::row, juce::FlexBox::Wrap::noWrap,
                                    juce::FlexBox::AlignContent::spaceAround,
                                    juce::FlexBox::AlignItems::stretch,
                                    juce::FlexBox::JustifyContent::flexStart);
        layout.items.add(juce::FlexItem(sourceDrop).withFlex(1.0));
        layout.items.add(juce::FlexItem(viaDrop).withFlex(1.0));
        layout.items.add(juce::FlexItem(depthSlider).withFlex(2.0));
        layout.items.add(juce::FlexItem(curveDrop).withFlex(0.5));
        layout.items.add(juce::FlexItem(destDrop).withFlex(1.0));
        layout.performLayout(juce::Rectangle<int>{0, 0, getWidth(), getHeight()});
    }
    ToneGranulator *gr = nullptr;
    struct CallbackParams
    {
        bool onlydepth = false;
        int slot = 0;
        int source = 0;
        int via = 0;
        int curve = 1;
        float curvepar0 = 0.0f;
        float depth = 0.0f;
        uint32_t target;
    };
    std::function<void(CallbackParams)> stateChangedCallback;
    int modslotindex = -1;
    uint32_t targetID = 1;
    DropDownComponent sourceDrop;
    DropDownComponent viaDrop;
    juce::Slider depthSlider;
    DropDownComponent curveDrop;
    juce::TextEditor curveParEditor;
    DropDownComponent destDrop;
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
    juce::Label infoLabel;
    void showFilterMenu(int whichfilter);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessorEditor)
};
