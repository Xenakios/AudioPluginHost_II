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

struct PresetsComponent : public juce::Component
{
    PresetsComponent()
    {
        for (int i = 0; i < 64; ++i)
        {
            auto but = std::make_unique<juce::TextButton>();
            but->setButtonText(juce::String(i + 1));
            but->onClick = [this, i]() {
                auto mods = juce::ModifierKeys::getCurrentModifiers();
                if (mods.isCommandDown())
                {
                    lastSaved = i;
                    if (OnSave)
                        OnSave(i);
                }
                else
                {
                    lastLoaded = i;
                    if (OnLoad)
                        OnLoad(i);
                }
                updateButtonColors();
            };
            addAndMakeVisible(*but);
            buttons.push_back(std::move(but));
        }
        defaultButtonColor =
            buttons.front()->findColour(juce::TextButton::ColourIds::buttonColourId);
    }
    void resized() override
    {
        juce::FlexBox flex;
        flex.flexDirection = juce::FlexBox::Direction::row;
        flex.flexWrap = juce::FlexBox::Wrap::wrap;
        for (auto &b : buttons)
        {
            flex.items.add(
                juce::FlexItem(*b).withFlex(1.0).withMinWidth(40.0f).withMaxWidth(40.0f));
        }
        flex.performLayout(getLocalBounds());
    }
    void updateButtonColors()
    {
        for (int i = 0; i < buttons.size(); ++i)
        {
            buttons[i]->setColour(juce::TextButton::ColourIds::buttonColourId, defaultButtonColor);
            if (i == lastLoaded)
                buttons[i]->setColour(juce::TextButton::ColourIds::buttonColourId,
                                      juce::Colours::orange);
            if (i == lastSaved)
                buttons[i]->setColour(juce::TextButton::ColourIds::buttonColourId,
                                      juce::Colours::red);
        }
    }
    std::function<void(int)> OnSave;
    std::function<void(int)> OnLoad;
    int lastSaved = -1;
    int lastLoaded = -1;
    juce::Colour defaultButtonColor;
    std::vector<std::unique_ptr<juce::TextButton>> buttons;
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
            stateChangedCallback(rateSlider.getParameterMetaData().id, rateSlider.getValue());
        };

        addAndMakeVisible(deformSlider);
        deformSlider.OnValueChanged = [this]() {
            stateChangedCallback(deformSlider.getParameterMetaData().id, deformSlider.getValue());
        };

        addAndMakeVisible(shiftSlider);
        shiftSlider.OnValueChanged = [this]() {
            stateChangedCallback(shiftSlider.getParameterMetaData().id, shiftSlider.getValue());
        };

        addAndMakeVisible(warpSlider);
        warpSlider.OnValueChanged = [this]() {
            stateChangedCallback(warpSlider.getParameterMetaData().id, warpSlider.getValue());
        };

        addAndMakeVisible(shapeSlider);
        shapeSlider.OnValueChanged = [this]() {
            stateChangedCallback(shapeSlider.getParameterMetaData().id, shapeSlider.getValue());
        };

        addAndMakeVisible(unipolarSlider);
        unipolarSlider.OnValueChanged = [this]() {
            stateChangedCallback(unipolarSlider.getParameterMetaData().id,
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
    void runJSInThread();
    std::atomic<int> js_status{0};
    juce::TextButton cancelButton;
    StepSeqComponent(int seqindex, ToneGranulator *g, juce::ThreadPool *tp);
    void mouseDown(const juce::MouseEvent &ev) override
    {
        if (!ev.mods.isRightButtonDown())
            return;
        juce::PopupMenu menu;
        menu.addSectionHeader("Play mode");
        for (int i = 0; i < StepModSource::NUMPLAYMODES; ++i)
        {
            menu.addItem(StepModSource::getPlayModeName(i), [i, this]() {
                StepModSource::Message msg;
                msg.opcode = StepModSource::Message::OP_PLAYMODE;
                msg.dest = sindex;
                msg.ival0 = i;
                gr->fifo.push(msg);
            });
        }
        menu.showMenuAsync(juce::PopupMenu::Options{});
    }
    bool keyPressed(const juce::KeyPress &ev) override;

    int graphxpos = 200;
    void resized() override
    {
        unipolarBut.setBounds(0, 1, graphxpos, 25);
        cancelButton.setBounds(0, unipolarBut.getBottom() + 1, graphxpos, 25);
    }
    void paint(juce::Graphics &g) override;

    void setLoopFromSelection()
    {
        gr->fifo.push({StepModSource::Message::OP_LOOPSTART, sindex, 0.0f, editRange.getStart()});
        gr->fifo.push({StepModSource::Message::OP_LOOPLEN, sindex, 0.0f, editRange.getLength()});
    }

    juce::ToggleButton unipolarBut;
    juce::Slider par0Slider;
    juce::TextEditor scriptParamsEditor;
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
    using Node = DropDownComponent::Node;
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

        curveDrop.rootNode.text = "Curve";
        curveDrop.rootNode.children.push_back(Node{"Linear", GranulatorModConfig::CURVE_LINEAR});
        curveDrop.rootNode.children.push_back(
            Node{"UNIPOL TO BIPOL", GranulatorModConfig::CURVE_UNIPOLARTOBIPOLAR});
        curveDrop.rootNode.children.push_back(
            Node{"BIPOL TO UNIPOL", GranulatorModConfig::CURVE_BIPOLARTOUNIPOLAR});
        curveDrop.rootNode.children.push_back(Node{"x^2", GranulatorModConfig::CURVE_SQUARE});
        curveDrop.rootNode.children.push_back(Node{"x^3", GranulatorModConfig::CURVE_CUBE});
        curveDrop.rootNode.children.push_back(Node{"x^16", GranulatorModConfig::CURVE_TOPOWER16});

        curveDrop.rootNode.children.push_back(Node{"EXPSIN 1", GranulatorModConfig::CURVE_EXPSIN1});
        curveDrop.rootNode.children.push_back(Node{"EXPSIN 2", GranulatorModConfig::CURVE_EXPSIN2});
        curveDrop.rootNode.children.push_back(
            Node{"HARMONICS 3 OCTAVES", GranulatorModConfig::CURVE_HARMONICSERIES3OCTAVES});
        curveDrop.rootNode.children.push_back(
            Node{"HARMONICS 4 OCTAVES", GranulatorModConfig::CURVE_HARMONICSERIES4OCTAVES});
        curveDrop.rootNode.children.push_back(
            Node{"HARMONICS 5 OCTAVES", GranulatorModConfig::CURVE_HARMONICSERIES5OCTAVES});
        Node xornode{"XOR"};
        for (int i = 0; i < 4; ++i)
        {
            xornode.children.push_back(
                Node{std::format("XOR {}", i + 1), GranulatorModConfig::CURVE_XOR1 + i});
        }
        curveDrop.rootNode.children.emplace_back(xornode);
        Node stepnode{"STEPS"};
        for (int i = 0; i < 6; ++i)
        {
            stepnode.children.push_back(
                Node{std::format("{} STEPS", 2 + i), GranulatorModConfig::CURVE_STEPS2 + i});
        }
        curveDrop.rootNode.children.emplace_back(stepnode);
        curveDrop.rootNode.children.push_back(
            Node{"BIT MIRROR", GranulatorModConfig::CURVE_BITMIRROR});
        curveDrop.setSelectedId(GranulatorModConfig::CURVE_LINEAR);
        curveDrop.OnItemSelected = updatfunc;

        curveParEditor.setText("0.0", juce::dontSendNotification);
        curveParEditor.onReturnKey = updatfunc;

        addAndMakeVisible(destDrop);
        initDestinationDrop();
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
        addAndMakeVisible(slotLabel);
        slotLabel.setJustificationType(juce::Justification::centred);
    }
    void initDestinationDrop()
    {
        destDrop.rootNode.children.clear();
        destDrop.rootNode.text = "Modulation target";
        destDrop.rootNode.children.push_back({"No target", 1});
        std::map<std::string, Node *> nodemap;
        destDrop.rootNode.children.reserve(128);
        for (auto &pmd : gr->parmetadatas)
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
        for (auto &pmd : gr->parmetadatas)
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
        destDrop.setSelectedId(destDrop.getSelectedId());
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
        slotLabel.setText(juce::String(modslotindex + 1), juce::dontSendNotification);
        auto layout = juce::FlexBox(juce::FlexBox::Direction::row, juce::FlexBox::Wrap::noWrap,
                                    juce::FlexBox::AlignContent::spaceAround,
                                    juce::FlexBox::AlignItems::stretch,
                                    juce::FlexBox::JustifyContent::flexStart);
        layout.items.add(juce::FlexItem(slotLabel).withFlex(0.15));
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
    juce::Label slotLabel;
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

class VolumeEnvelopeComponent : public juce::Component
{
  public:
    VolumeEnvelopeComponent(ToneGranulator *gr, bool auxenv) : granul(gr), auxenvmode(auxenv)
    {
        curvepath.preallocateSpace(512);
        rng.seed(65537, 90004);
    }
    xenakios::Xoroshiro128Plus rng;
    void generate_steps(int mode)
    {
        auto numsteps = SimpleEnvelope<false>::maxnumsteps;
        for (int i = 0; i < numsteps; ++i)
        {
            float val = rng.nextFloatInRange(-1.0f, 1.0f);
            StepModSource::Message msg;
            msg.opcode = StepModSource::Message::OP_SETSTEP;
            msg.fval0 = val;
            msg.dest = 1000;
            msg.ival0 = i;
            granul->fifo.push(msg);
        }
        juce::Timer::callAfterDelay(100, [this]() { repaint(); });
    }
    void set_interpolation_mode(int m)
    {
        granul->set_aux_envelope_interpolation_mode(m);
        juce::Timer::callAfterDelay(100, [this]() { repaint(); });
    }
    void mouseDown(const juce::MouseEvent &ev) override
    {
        if (!auxenvmode)
            return;
        auto numsteps = SimpleEnvelope<false>::maxnumsteps;
        if (ev.mods.isRightButtonDown())
        {
            juce::PopupMenu menu;
            menu.addSectionHeader("Interpolation mode");
            menu.addItem("None", [this]() { set_interpolation_mode(0); });
            menu.addItem("Linear", [this]() { set_interpolation_mode(1); });
            menu.addItem("Spline", [this]() { set_interpolation_mode(2); });
            menu.addSectionHeader("Generate");
            menu.addItem("Random Uniform", [this]() { generate_steps(0); });
            menu.showMenuAsync(juce::PopupMenu::Options{});
        }
        else
        {
            int stepindex = numsteps / (float)getWidth() * ev.x;
            if (stepindex >= 0 && stepindex < numsteps)
            {
                float val = juce::jmap<float>(ev.y, 0, getHeight(), 1.1, -1.1);
                StepModSource::Message msg;
                msg.opcode = StepModSource::Message::OP_SETSTEP;
                msg.fval0 = val;
                msg.dest = 1000;
                msg.ival0 = stepindex;
                granul->fifo.push(msg);
            }
        }
        juce::Timer::callAfterDelay(100, [this]() { repaint(); });
    }
    void mouseWheelMove(const juce::MouseEvent &event,
                        const juce::MouseWheelDetails &wheel) override
    {
        if (!auxenvmode)
            return;
        auto numsteps = SimpleEnvelope<false>::maxnumsteps;
        int stepindex = numsteps / (float)getWidth() * event.x;
        if (stepindex >= 0 && stepindex < numsteps)
        {
            float delta = wheel.deltaY * 0.2;
            auto &auxenv = granul->voices[0]->aux_envelope;
            float val = auxenv.steps[stepindex] + delta;
            StepModSource::Message msg;
            msg.opcode = StepModSource::Message::OP_SETSTEP;
            msg.fval0 = val;
            msg.dest = 1000;
            msg.ival0 = stepindex;
            granul->fifo.push(msg);
        }
        repaint();
    }
    void paint(juce::Graphics &g) override
    {
        g.fillAll(juce::Colours::black);
        g.setColour(juce::Colours::yellow);
        curvepath.clear();
        auto curvemorph = priormorph;
        auto curvestart = priorstartcurve;
        auto curveend = priorendcurve;
        float sinfreq = getWidth() / 8.0;
        auto &eluts = granul->eluts;
        auto &auxenv = granul->voices[0]->aux_envelope;

        for (int i = 0; i < getWidth(); ++i)
        {
            float normx = 1.0 / getWidth() * i;
            float sinvalue = std::sin(2 * M_PI * normx * sinfreq);
            float normy = 0.0f;
            if (!auxenvmode)
            {
                if (normx < curvemorph)
                {
                    normx = xenakios::mapvalue(normx, 0.0f, curvemorph, 0.0f, 1.0f);
                    // normy = easing_table[curvestart].function(normx);
                    normy = eluts.getValueLERP<true>(curvestart, normx);
                }
                else
                {
                    normx = xenakios::mapvalue(normx, curvemorph, 1.0f, 1.0f, 0.0f);
                    // normy = easing_table[curveend].function(normx);
                    normy = eluts.getValueLERP<true>(curveend, normx);
                }
                normy *= sinvalue;
            }
            else
            {
                normy = auxenv.get_value(normx, priorauxwarp);
                normy *= 1.0;
            }

            float ycor = xenakios::mapvalue<float>(normy, -1.1f, 1.1f, getHeight(), 0);
            if (i == 0)
                curvepath.startNewSubPath({(float)i, ycor});
            else
                curvepath.lineTo({(float)i, ycor});
        }
        g.strokePath(curvepath, juce::PathStrokeType(1.0f));
        if (auxenvmode)
        {
            g.setColour(juce::Colours::white);
            auto numsteps = SimpleEnvelope<false>::maxnumsteps;
            for (int i = 0; i < numsteps; ++i)
            {
                float x0 = (float)getWidth() / numsteps * i;
                float x1 = (float)getWidth() / numsteps * (i + 1);
                float y = juce::jmap<float>(auxenv.steps[i], -1.1f, 1.1f, getHeight(), 0);
                g.drawLine(x0, y, x1, y, 2.0f);
            }
        }
    }
    float cubic_interpolate(float x)
    {
        int index = x;
        auto &test_table = granul->voices[0]->aux_envelope.steps;
        float y0 = test_table[index + 0];
        float y1 = test_table[index + 1];
        float y2 = test_table[index + 2];
        float y3 = test_table[index + 3];
        float mu = x - index;
        return sst::basic_blocks::dsp::quad_bspline(y0, y1, y2, mu);
        // return sst::basic_blocks::dsp::cubic_ipol(y0, y1, y2, y3, mu);
        return y0 + (y1 - y0) * mu;
    }
    void updateIfNeeded()
    {
        if (!auxenvmode)
        {
            int curvestart = *granul->idtoparvalptr[ToneGranulator::PAR_VOLENVEASINGSTART];
            int curveend = *granul->idtoparvalptr[ToneGranulator::PAR_VOLENVEASINGEND];
            float curvemorph = *granul->idtoparvalptr[ToneGranulator::PAR_ENVMORPH];
            if (priorstartcurve != curvestart || priorendcurve != curveend ||
                priormorph != curvemorph)
            {
                priorstartcurve = curvestart;
                priorendcurve = curveend;
                priormorph = curvemorph;
                repaint();
                // DBG(priorstartcurve << " " << priorendcurve << " " << priormorph);
            }
        }
        else
        {
            float warp = granul->auxenvwarpmodulated;
            if (warp != priorauxwarp)
            {
                priorauxwarp = warp;
                repaint();
            }
        }
    }
    ToneGranulator *granul = nullptr;
    juce::Path curvepath;
    int priorstartcurve = 0;
    int priorendcurve = 0;
    float priormorph = 0.0f;
    float priorauxwarp = 0.0f;
    bool auxenvmode = false;
};

class ModSourcesDebugComponent : public juce::Component
{
  public:
    ToneGranulator *gr = nullptr;
    std::vector<ToneGranulator::GrainVisualizerMessage> persisted_events;
    double timespantoshow = 8.0;
    int throttlecounter = 0;
    float visualfadecoefficient = 1.0;
    bool showModulatorValues = false;
    juce::ColourGradient pitchGradient;
    std::unique_ptr<juce::VBlankAttachment> vblankAttachment;
    ModSourcesDebugComponent(ToneGranulator *g) : gr(g)
    {
        timespantoshow = gr->gvsettings.timespantoshow;
        pitchGradient.clearColours();
        pitchGradient.addColour(0.00, juce::Colours::red);
        pitchGradient.addColour(0.25, juce::Colours::green);
        pitchGradient.addColour(0.50, juce::Colours::yellow);
        pitchGradient.addColour(0.75, juce::Colours::cyan);
        pitchGradient.addColour(1.00, juce::Colours::white);
        if (!is_debug())
            visualfadecoefficient = 0.93;
        else
            visualfadecoefficient = std::pow(0.93, 4);
        persisted_events.reserve(4096);
        vblankAttachment = std::make_unique<juce::VBlankAttachment>(this, [this]() {
            updateGrainData();
            if (is_debug())
            {
                if (throttlecounter % 4 == 0)
                    repaint();
                ++throttlecounter;
            }
            else
            {
                repaint();
            }
        });
    }
    void paint(juce::Graphics &g) override;
    void paintAmbisonicFieldHammerProjection(juce::Graphics &g);
    bool is_extended_size = false;
    void mouseDown(const juce::MouseEvent &ev) override
    {
        if (!ev.mods.isRightButtonDown())
            return;
        juce::PopupMenu menu;
        menu.addSectionHeader("Time span to show");
        menu.addItem("1 second", [this]() { gr->gvsettings.timespantoshow = 1.0; });
        menu.addItem("2 seconds", [this]() { gr->gvsettings.timespantoshow = 2.0; });
        menu.addItem("4 seconds", [this]() { gr->gvsettings.timespantoshow = 4.0; });
        menu.addItem("8 seconds", [this]() { gr->gvsettings.timespantoshow = 8.0; });
        menu.addItem("16 seconds", [this]() { gr->gvsettings.timespantoshow = 16.0; });
        menu.addSectionHeader("Options");
        menu.addItem("Toggle size", [this]() {
            is_extended_size = !is_extended_size;
            getParentComponent()->resized();
        });
        menu.addItem("Show modulator values", true, showModulatorValues,
                     [this]() { showModulatorValues = !showModulatorValues; });
        menu.showMenuAsync(juce::PopupMenu::Options{});
    }
    void updateGrainData()
    {
        timespantoshow = gr->gvsettings.timespantoshow;
        ToneGranulator::GrainVisualizerMessage msg;
        while (gr->visualizer_fifo.pop(msg))
        {
            persisted_events.push_back(msg);
        }
        double enginetime = gr->playposframes / gr->m_sr;
        std::erase_if(persisted_events, [this, enginetime](auto const &ev) {
            return ev.timepos + ev.duration < enginetime - timespantoshow;
        });
    }
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
                                 .withMargin(2.0)
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
                                 .withMaxHeight(40)
                                 .withMinWidth(50)
                                 .withMaxWidth(getWidth()));
        }
        layout.performLayout(juce::Rectangle<int>(7, 17, getWidth() - 14, getHeight() - 25));
    }
    std::vector<juce::Component *> headerComponents;
    std::vector<std::unique_ptr<XapSlider>> sliders;
};

class PerformanceComponent : public juce::Component, public juce::Timer
{
  public:
    PerformanceComponent() { startTimer(100); }
    void timerCallback() override { repaint(); }
    void paint(juce::Graphics &g) override
    {
        g.fillAll(juce::Colours::black);
        int maxvoices = 0;
        int usedvoices = 0;
        float cpu_use = 0.0f;
        if (RequestData)
        {
            RequestData(maxvoices, usedvoices, cpu_use);
            float w = (float)(getWidth() - 2) / maxvoices * usedvoices * 0.5;
            g.setColour(juce::Colours::yellow);
            g.fillRect(juce::Rectangle<float>(0.0f, 0.0f, w, 20.0f));
            g.setColour(juce::Colours::white);
            g.drawText(std::format("VOICES {:2}/{:2}", usedvoices, maxvoices), 0, 0,
                       getWidth() / 2 - 2, 20, juce::Justification::centredRight);
            w = cpu_use * (getWidth() - 2) * 0.5;
            g.setColour(juce::Colours::green);
            g.fillRect(juce::Rectangle<float>(getWidth() / 2.0, 0.0f, w, 20.0f));
            g.setColour(juce::Colours::white);
            g.drawText(std::format("CPU {}%", (int)(cpu_use * 100.0)), getWidth() / 2 - 2, 0,
                       getWidth() / 2, 20, juce::Justification::centredRight);
        }
    }
    std::function<void(int &, int &, float &)> RequestData;
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
    VolumeEnvelopeComponent envcomp;
    VolumeEnvelopeComponent auxenvcomp;

    std::map<int64_t, GrainInsertFX::ModeInfo> filterInfoMap;
    std::unique_ptr<DropDownComponent> filter1Drop;
    std::unique_ptr<DropDownComponent> filter2Drop;
    void handleFilterSelection(int filterindex);
    void fillDropWithFilters(int filterIndex, DropDownComponent &drop, std::string rootText);
    std::vector<std::unique_ptr<ModulationRowComponent>> modRowComps;
    std::vector<std::unique_ptr<LFOComponent>> lfocomps;
    juce::TabbedComponent lfoTabs;
    // juce::TabbedComponent insertsTabs;
    std::vector<std::unique_ptr<StepSeqComponent>> stepcomps;
    juce::Label infoLabel;
    juce::TextButton presetButton;
    void showPresetsMenu();
    void saveSnapShot(int index);
    void loadSnapShot(int index);
    std::unordered_map<uint32_t, XapSlider *> idToSlider;
    std::unique_ptr<PerformanceComponent> perfcomp;
    std::unique_ptr<juce::TextButton> recordButton;
    PresetsComponent presetsComponent;
    ModSourcesDebugComponent msDebug;
    void showFilterMenu(int whichfilter);
    void updateInsertParameterMetaDatas();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPluginAudioProcessorEditor)
};
