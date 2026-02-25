#pragma once

#include "PluginProcessor.h"
#include "../../Experimental/xap_slider.h"

class MyCustomLNF : public juce::LookAndFeel_V4
{
  public:
    // Update the constructor/member to avoid that deprecation warning!
    juce::Font myFont{juce::FontOptions("Comic Sans MS", 20.0f, juce::Font::bold)};

    // This covers Labels (and many components that use Labels internally)
    juce::Font getLabelFont(juce::Label &l) override { return myFont; }

    // This covers standard TextButtons
    juce::Font getTextButtonFont(juce::TextButton &b, int buttonHeight) override { return myFont; }

    // This ensures custom typefaces are retrieved if something asks for it
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font &f) override
    {
        return myFont.getTypefacePtr();
    }
};

struct DropDownComponent : public juce::Component
{

    /* investigate using this later, thanks to Ryan Robinson in AudioProgrammer Discord
    struct Group;
    struct Item;
    using Node = std::variant<Group, Item>;

    struct Group
    {
        std::string name{};
        std::vector<Node> children{};
    };

    struct Item
    {
        std::string name{};
        int64_t id = -1;
    };
    */
    struct Node
    {
        std::string text;
        int64_t id = -1;
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
    Node *findNodeRecur(Node &n, int64_t tofind)
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
    void setSelectedId(int64_t id)
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
    juce::Font myfont;
    void paint(juce::Graphics &g) override
    {
        g.fillAll(juce::Colours::black);
        g.setFont(myfont);
        g.setColour(juce::Colours::white);
        g.drawRect(0, 0, getWidth(), getHeight(), 2);
        g.drawText(selectedText, 4, 2, getWidth(), getHeight() - 4,
                   juce::Justification::centredLeft);
    }
    void mouseDown(const juce::MouseEvent &ev) override { showMenu(); }
    void showMenu() { showNodes(); }

    int64_t selectedId = 0;
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
    LFOComponent(int index, ToneGranulator *g)
        : lfoindex(index), gr(g),
          rateSlider(true, *g->idtoparmetadata[ToneGranulator::PAR_LFORATES + index]),
          deformSlider(true, *g->idtoparmetadata[ToneGranulator::PAR_LFODEFORMS + index]),
          shiftSlider(true, *g->idtoparmetadata[ToneGranulator::PAR_LFOSHIFTS + index]),
          warpSlider(true, *g->idtoparmetadata[ToneGranulator::PAR_LFOWARPS + index]),
          shapeSlider(true, *g->idtoparmetadata[ToneGranulator::PAR_LFOSHAPES + index]),
          unipolarSlider(true, *g->idtoparmetadata[ToneGranulator::PAR_LFOUNIPOLARS + index])
    {
        addAndMakeVisible(rateSlider);
        rateSlider.OnValueChanged = [this]() {
            stateChangedCallback(rateSlider.getParamDescription().id, rateSlider.getValue());
        };

        addAndMakeVisible(deformSlider);
        deformSlider.OnValueChanged = [this]() {
            stateChangedCallback(deformSlider.getParamDescription().id, deformSlider.getValue());
        };

        addAndMakeVisible(shiftSlider);
        shiftSlider.OnValueChanged = [this]() {
            stateChangedCallback(shiftSlider.getParamDescription().id, shiftSlider.getValue());
        };

        addAndMakeVisible(warpSlider);
        warpSlider.OnValueChanged = [this]() {
            stateChangedCallback(warpSlider.getParamDescription().id, warpSlider.getValue());
        };

        addAndMakeVisible(shapeSlider);
        shapeSlider.OnValueChanged = [this]() {
            stateChangedCallback(shapeSlider.getParamDescription().id, shapeSlider.getValue());
        };

        addAndMakeVisible(unipolarSlider);
        unipolarSlider.OnValueChanged = [this]() {
            stateChangedCallback(unipolarSlider.getParamDescription().id,
                                 unipolarSlider.getValue());
        };
    }
    void resized()
    {
        // shapeCombo.setBounds(0, 0, 200, 25);
        shapeSlider.setBounds(0, 0, 200, 25);
        unipolarSlider.setBounds(shapeSlider.getRight() + 1, 0, 200, 25);
        rateSlider.setBounds(0, shapeSlider.getBottom() + 1, 400, 25);
        deformSlider.setBounds(0, rateSlider.getBottom() + 1, 400, 25);
        shiftSlider.setBounds(rateSlider.getRight() + 1, shapeSlider.getBottom() + 1, 400, 25);
        warpSlider.setBounds(rateSlider.getRight() + 1, shiftSlider.getBottom() + 1, 400, 25);
    }
    int lfoindex = -1;
    ToneGranulator *gr = nullptr;
    std::function<void(uint32_t, float)> stateChangedCallback;

    XapSlider rateSlider;
    XapSlider deformSlider;
    XapSlider shiftSlider;
    XapSlider warpSlider;
    XapSlider shapeSlider;
    XapSlider unipolarSlider;
};

struct StepSeqComponent : public juce::Component
{
    uint16_t playingStep = 0;
    xenakios::Xoroshiro128Plus rng;
    juce::Range<int> editRange{0, 8};
    void updateGUI()
    {
        playingStep = gr->stepModSources[sindex].curstepforgui;
        unipolarBut.setToggleState(gr->stepModSources[sindex].unipolar.load(),
                                   juce::dontSendNotification);
        repaint();
    }
    void runExternalProgram();

    StepSeqComponent(int seqindex, ToneGranulator *g, juce::ThreadPool *tp)
        : gr(g), sindex(seqindex), threadPool(tp)
    {
        rng.seed(11400714819323198485ULL, 17 + sindex * 31);
        editRange.setStart(0);
        editRange.setLength(g->stepModSources[sindex].looplen);
        setWantsKeyboardFocus(true);

        addAndMakeVisible(unipolarBut);
        unipolarBut.setButtonText("Unipolar");
        unipolarBut.onClick = [this]() {
            gr->fifo.push(
                {StepModSource::Message::OP_UNIPOLAR, sindex, 0.0f, unipolarBut.getToggleState()});
        };
        addAndMakeVisible(par0Slider);
        par0Slider.setRange(0.0, 1.0);
        par0Slider.setNumDecimalPlacesToDisplay(2);
        par0Slider.onDragEnd = [this]() { runExternalProgram(); };
    }
    bool keyPressed(const juce::KeyPress &ev) override;

    int graphxpos = 200;
    void resized() override
    {
        unipolarBut.setBounds(0, 1, graphxpos, 25);
        // par0Slider.setBounds(0, unipolarBut.getBottom() + 1, graphxpos, 25);
    }
    void paint(juce::Graphics &g) override;

    void setLoopFromSelection()
    {
        gr->fifo.push({StepModSource::Message::OP_LOOPSTART, sindex, 0.0f, editRange.getStart()});
        gr->fifo.push({StepModSource::Message::OP_LOOPLEN, sindex, 0.0f, editRange.getLength()});
    }

    juce::ToggleButton unipolarBut;
    juce::Slider par0Slider;
    bool autoSetLoop = false;
    ToneGranulator *gr = nullptr;
    uint32_t sindex = 0;
    juce::ThreadPool *threadPool = nullptr;
};

struct ModulationRowComponent : public juce::Component
{
    void fillDropWithSources(DropDownComponent &drop, std::string roottext)
    {
        drop.rootNode.text = roottext;
        std::map<std::string, DropDownComponent::Node *> nodemap;
        drop.rootNode.children.reserve(16);
        for (int i = 0; i < gr->modSources.size(); ++i)
        {
            auto &ms = gr->modSources[i];
            if (!ms.groupname.empty())
            {
                if (nodemap.count(ms.groupname) == 0)
                {
                    drop.rootNode.children.push_back({ms.groupname, -1});
                    nodemap[ms.groupname] = &drop.rootNode.children.back();
                }
            }
        }
        for (int i = 0; i < gr->modSources.size(); ++i)
        {
            auto &ms = gr->modSources[i];
            if (ms.groupname.empty())
            {
                drop.rootNode.children.push_back({ms.name, (int)ms.id.src});
            }
            else
            {
                nodemap[ms.groupname]->children.push_back({ms.name, (int)ms.id.src});
            }
        }
        drop.setSelectedId(0);
    }
    ModulationRowComponent(ToneGranulator *g) : gr(g)
    {
        addAndMakeVisible(sourceDrop);
        addAndMakeVisible(viaDrop);
        addAndMakeVisible(depthSlider);

        auto updatfunc = [this] {
            CallbackParams pars{false,
                                modslotindex,
                                (int)sourceDrop.selectedId,
                                (int)viaDrop.selectedId,
                                (int)curveDrop.selectedId,
                                curveParEditor.getText().getFloatValue(),
                                (float)depthSlider.getValue(),
                                (uint32_t)destDrop.selectedId};
            stateChangedCallback(pars);
        };
        fillDropWithSources(sourceDrop, "Modulation source");
        sourceDrop.OnItemSelected = updatfunc;
        fillDropWithSources(viaDrop, "Modulation via source");
        viaDrop.OnItemSelected = updatfunc;

        depthSlider.OnValueChanged = [this]() {
            CallbackParams pars{true,
                                modslotindex,
                                (int)sourceDrop.selectedId,
                                (int)viaDrop.selectedId,
                                (int)curveDrop.selectedId,
                                curveParEditor.getText().getFloatValue(),
                                (float)depthSlider.getValue(),
                                (uint32_t)destDrop.selectedId};
            stateChangedCallback(pars);
        };

        addAndMakeVisible(curveDrop);
        using mcf = GranulatorModConfig;
        using Node = DropDownComponent::Node;
        curveDrop.rootNode.text = "Curve";
        curveDrop.rootNode.children.push_back(Node{"Linear", GranulatorModConfig::CURVE_LINEAR});
        curveDrop.rootNode.children.push_back(Node{"x^2", GranulatorModConfig::CURVE_SQUARE});
        curveDrop.rootNode.children.push_back(Node{"x^3", GranulatorModConfig::CURVE_CUBE});
        curveDrop.rootNode.children.push_back(Node{"EXPSIN 1", GranulatorModConfig::CURVE_EXPSIN1});
        curveDrop.rootNode.children.push_back(Node{"EXPSIN 2", GranulatorModConfig::CURVE_EXPSIN2});
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
        destDrop.OnItemSelected = [updatfunc, this]() {
            auto id = destDrop.selectedId;
            if (id > 1)
            {
                auto pmd = gr->idtoparmetadata[destDrop.selectedId];
                auto d = gr->modRanges[destDrop.selectedId];
                depthSlider.setModulationDisplayDepth(d, pmd->unit);
                updatfunc();
            }
        };
    }
    void setTarget(uint32_t parid)
    {
        destDrop.setSelectedId(parid);
        if (parid > 1)
        {
            auto pmd = gr->idtoparmetadata[destDrop.selectedId];
            auto d = gr->modRanges[destDrop.selectedId];
            depthSlider.setModulationDisplayDepth(d, pmd->unit);
        }
    }

    void resized() override
    {
        auto layout = juce::FlexBox(juce::FlexBox::Direction::row, juce::FlexBox::Wrap::noWrap,
                                    juce::FlexBox::AlignContent::spaceAround,
                                    juce::FlexBox::AlignItems::stretch,
                                    juce::FlexBox::JustifyContent::flexStart);
        layout.items.add(juce::FlexItem(sourceDrop).withFlex(0.5));
        layout.items.add(juce::FlexItem(viaDrop).withFlex(0.5));
        layout.items.add(juce::FlexItem(depthSlider).withFlex(2.0));
        layout.items.add(juce::FlexItem(curveDrop).withFlex(0.5));
        layout.items.add(juce::FlexItem(destDrop).withFlex(0.5));
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

    DropDownComponent sourceDrop;
    DropDownComponent viaDrop;
    XapSlider depthSlider{true, ParamDesc()
                                    .asFloat()
                                    .withName("DEPTH")
                                    .withRange(-1.0f, 1.0f)
                                    .withLinearScaleFormatting("%", 100.0f)};
    DropDownComponent curveDrop;
    juce::TextEditor curveParEditor;
    DropDownComponent destDrop;
};

class ParameterGroupComponent : public juce::GroupComponent
{
  public:
    ParameterGroupComponent(juce::String groupName) : juce::GroupComponent{"", groupName} {}
    void addSlider(std::unique_ptr<XapSlider> &&s)
    {
        addAndMakeVisible(s.get());
        sliders.push_back(std::move(s));
    }
    void addHeaderComponent(juce::Component *c)
    {
        addAndMakeVisible(c);
        headerComponents.push_back(c);
    }
    void resized() override
    {
        juce::FlexBox layout;
        layout.flexDirection = juce::FlexBox::Direction::column;
        layout.flexWrap = juce::FlexBox::Wrap::wrap;
        
        for (int i = 0; i < headerComponents.size(); ++i)
        {
            layout.items.add(juce::FlexItem(*headerComponents[i])
                                 .withFlex(1.0)
                                 .withMinHeight(25)
                                 .withMinWidth(50)
                                 .withMaxWidth(getWidth()));
        }
        for (int i = 0; i < sliders.size(); ++i)
        {
            layout.items.add(juce::FlexItem(*sliders[i])
                                 .withFlex(1.0)
                                 .withMargin(2.0)
                                 .withMinHeight(20)
                                 .withMinWidth(50)
                                 .withMaxWidth(getWidth()));
        }
        layout.performLayout(juce::Rectangle<int>(7, 17, getWidth() - 14, getHeight() - 25));
    }
    std::vector<juce::Component *> headerComponents;
    std::vector<std::unique_ptr<XapSlider>> sliders;
};

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
    // MyCustomLNF lnf;
    AudioPluginAudioProcessor &processorRef;
    ParameterGroupComponent oscillatorComponent{"Oscillator"};
    ParameterGroupComponent mainParamsComponent{"Main"};
    ParameterGroupComponent spatParamsComponent{"Spatialization"};
    ParameterGroupComponent miscParamsComponent{"Misc parameters"};
    ParameterGroupComponent volumeParamsComponent{"Volume"};
    ParameterGroupComponent timeParamsComponent{"Time"};
    ParameterGroupComponent stackParamsComponent{"Stacking"};
    ParameterGroupComponent insert1ParamsComponent{"Insert FX A"};
    ParameterGroupComponent insert2ParamsComponent{"Insert FX B"};
    struct FilterInfo
    {
        sfpp::FilterModel filtermodel;
        sfpp::ModelConfig filterconfig;
    };
    std::map<int64_t, GrainInsertFX::ModeInfo> filterInfoMap;
    std::unique_ptr<DropDownComponent> filter1Drop;
    std::unique_ptr<DropDownComponent> filter2Drop;
    void handleFilterSelection(int filterindex);
    void fillDropWithFilters(int filterIndex, DropDownComponent &drop, std::string rootText);
    std::vector<std::unique_ptr<ModulationRowComponent>> modRowComps;
    std::vector<std::unique_ptr<LFOComponent>> lfocomps;
    juce::TabbedComponent lfoTabs;
    std::vector<std::unique_ptr<StepSeqComponent>> stepcomps;
    juce::Label infoLabel;
    std::unordered_map<uint32_t, XapSlider *> idToSlider;
    void showFilterMenu(int whichfilter);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessorEditor)
};
