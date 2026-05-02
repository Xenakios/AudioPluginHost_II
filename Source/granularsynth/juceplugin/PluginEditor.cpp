#include "PluginProcessor.h"
#include "PluginEditor.h"
void init_step_sequencer_js();
void deinit_step_sequencer_js();
void cancel_js();
std::vector<float> generate_from_js(std::string jscode, std::vector<float> currentsteps,
                                    int startstep, int endstep, std::vector<float> params);

inline void updateAllFonts(juce::Component &parent, const juce::Font &newFont)
{
    for (auto *child : parent.getChildren())
    {
        if (auto *drop = dynamic_cast<DropDownComponent *>(child))
        {
            drop->myfont = newFont;
        }
        if (auto *xaps = dynamic_cast<XapSlider *>(child))
        {
            xaps->m_font = newFont;
        }
        // Keep digging deeper
        updateAllFonts(*child, newFont);
    }
}

AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor &p)
    : juce::AudioProcessorEditor(p), mainPage(p),
      dashPage(p), mainTabs(juce::TabbedButtonBar::Orientation::TabsAtTop)
{
    mainTabs.addTab("DASHBOARD", juce::Colours::grey, &dashPage, false);
    mainTabs.addTab("DETAILS", juce::Colours::grey, &mainPage, false);
    mainTabs.setCurrentTabIndex(1);
    addAndMakeVisible(mainTabs);
    setSize(1500, 830);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor() {}

void AudioPluginAudioProcessorEditor::resized()
{
    mainTabs.setBounds(0, 0, getWidth(), getHeight());
}

MainPageComponent::MainPageComponent(AudioPluginAudioProcessor &p)
    : processorRef(p), envcomp(&p.granulator, false), auxenvcomp(&p.granulator, true),
      lfoTabs(juce::TabbedButtonBar::Orientation::TabsAtTop)
{
    

    perfcomp = std::make_unique<PerformanceComponent>();
    perfcomp->RequestData = [this](int &maxvoices, int &usedvoices, float &cpu) {
        maxvoices = processorRef.granulator.voices.size();
        usedvoices = processorRef.granulator.numVoicesUsed;
        cpu = processorRef.perfMeasurer.getLoadAsProportion();
    };
    init_step_sequencer_js();
    addAndMakeVisible(envcomp);
    addAndMakeVisible(auxenvcomp);

    addAndMakeVisible(oscillatorComponent);
    addAndMakeVisible(spatParamsComponent);
    addAndMakeVisible(miscParamsComponent);
    addAndMakeVisible(mainParamsComponent);
    addAndMakeVisible(volumeParamsComponent);
    addAndMakeVisible(insert1ParamsComponent);
    addAndMakeVisible(insert2ParamsComponent);
    addAndMakeVisible(stackParamsComponent);
    addAndMakeVisible(timeParamsComponent);

    recordButton = std::make_unique<juce::TextButton>();
    recordButton->setButtonText("Record");
    recordButton->onClick = [this]() {
        if (processorRef.isRecording)
            processorRef.stopRecording();
        else
            processorRef.startRecording();
        if (processorRef.isRecording)
            recordButton->setButtonText("Stop");
        else
            recordButton->setButtonText("Record");
    };
    mainParamsComponent.addHeaderComponent(recordButton.get());
    mainParamsComponent.addHeaderComponent(perfcomp.get());
    addAndMakeVisible(infoLabel);

    filter1Drop = std::make_unique<DropDownComponent>();
    fillDropWithFilters(0, *filter1Drop, "Filter 1");
    filter1Drop->OnItemSelected = [this]() { handleFilterSelection(0); };
    insert1ParamsComponent.addHeaderComponent(filter1Drop.get());

    filter2Drop = std::make_unique<DropDownComponent>();
    fillDropWithFilters(1, *filter2Drop, "Filter 2");
    filter2Drop->OnItemSelected = [this]() { handleFilterSelection(1); };
    insert2ParamsComponent.addHeaderComponent(filter2Drop.get());

    for (int i = 0; i < processorRef.granulator.parmetadatas.size(); ++i)
    {
        auto &pmd = processorRef.granulator.parmetadatas[i];
        if (!choc::text::startsWith(pmd.groupName, "LFO"))
        {
            XapSlider::Style style = XapSlider::SS_HorizontalSlider;
            if (pmd.groupName == "Time" || pmd.groupName == "Stacking" ||
                pmd.groupName == "Insert A" || pmd.groupName == "Insert B" ||
                pmd.groupName == "Volume")
                style = XapSlider::SS_Knob;
            auto slid = std::make_unique<XapSlider>(style, pmd);
            slid->OnValueChanged = [this, pid = pmd.id, sli = slid.get()]() {
                ParameterMessage msg;
                msg.id = pid;
                msg.value = sli->getValue();
                processorRef.params_from_gui_fifo.push(msg);
            };
            idToSlider[pmd.id] = slid.get();
            if (pmd.groupName == "Oscillator")
            {
                oscillatorComponent.addSlider(std::move(slid));
            }
            else if (pmd.groupName == "Spatialization")
            {
                spatParamsComponent.addSlider(std::move(slid));
            }
            else if (pmd.groupName == "Main output")
            {
                mainParamsComponent.addSlider(std::move(slid));
            }
            else if (pmd.groupName == "Volume")
            {
                volumeParamsComponent.addSlider(std::move(slid));
            }
            else if (pmd.groupName == "Insert A")
            {
                insert1ParamsComponent.addSlider(std::move(slid));
            }
            else if (pmd.groupName == "Insert B")
            {
                insert2ParamsComponent.addSlider(std::move(slid));
            }
            else if (pmd.groupName == "Stacking")
            {
                stackParamsComponent.addSlider(std::move(slid));
            }
            else if (pmd.groupName == "Time")
            {
                timeParamsComponent.addSlider(std::move(slid));
            }
            else
            {
                miscParamsComponent.addSlider(std::move(slid));
            }
        }
    }

    for (int i = 0; i < 16; ++i)
    {
        auto modcomp = std::make_unique<ModulationRowComponent>(&processorRef.granulator);
        modcomp->modslotindex = i;
        modcomp->stateChangedCallback = [this](ModulationRowComponent::CallbackParams args) {
            if (args.slot >= 0 && args.source >= 0 && args.target >= 0)
            {
                processorRef.updateHostDisplay(
                    juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
                ThreadMessage msg;
                msg.modslot = args.slot;
                msg.depth = args.depth;
                msg.modsource = args.source;
                msg.modvia = args.via;
                msg.moddest = args.target;
                msg.modcurve = args.curve;
                msg.modcurvepar0 = args.curvepar0;
                msg.opcode = ThreadMessage::OP_MODROUTING;
                if (args.onlydepth)
                {
                    msg.opcode = ThreadMessage::OP_MODPARAM;
                }
                processorRef.from_gui_fifo.push(msg);
            }
        };
        addAndMakeVisible(*modcomp);
        modRowComps.push_back(std::move(modcomp));
    }
    auto &idtomd = processorRef.granulator.idtoparmetadata;
    for (int i = 0; i < 8; ++i)
    {
        auto lfoc = std::make_unique<LFOComponent>(i, &processorRef.granulator);
        lfoc->stateChangedCallback = [this](uint32_t parid, float val) {
            ParameterMessage parmsg;
            parmsg.id = parid;
            parmsg.value = val;
            processorRef.params_from_gui_fifo.push(parmsg);
        };
        idToSlider[ToneGranulator::PAR_LFORATES + i] = &lfoc->rateSlider;
        idToSlider[ToneGranulator::PAR_LFODEFORMS + i] = &lfoc->deformSlider;
        idToSlider[ToneGranulator::PAR_LFOSHIFTS + i] = &lfoc->shiftSlider;
        idToSlider[ToneGranulator::PAR_LFOWARPS + i] = &lfoc->warpSlider;
        idToSlider[ToneGranulator::PAR_LFOSHAPES + i] = &lfoc->shapeSlider;
        idToSlider[ToneGranulator::PAR_LFOUNIPOLARS + i] = &lfoc->unipolarSlider;
        lfoTabs.addTab("LFO " + juce::String(i + 1), juce::Colours::darkgrey, lfoc.get(), false);
        lfocomps.push_back(std::move(lfoc));
    }
    for (int i = 0; i < 8; ++i)
    {
        auto stepcomp =
            std::make_unique<StepSeqComponent>(i, &processorRef.granulator, &processorRef.tpool);
        lfoTabs.addTab("STEP SEQ " + juce::String(i + 1), juce::Colours::darkgrey, stepcomp.get(),
                       false);
        stepcomps.push_back(std::move(stepcomp));
    }
    addAndMakeVisible(lfoTabs);
    // lfoTabs.getTabbedButtonBar().setColour(juce::TabbedButtonBar::ColourIds::tabOutlineColourId,
    //                                        juce::Colours::yellowgreen);
    lfoTabs.setCurrentTabIndex(0);

    // addAndMakeVisible(insertsTabs);

    // setLookAndFeel(&lnf);
    // updateAllFonts(*this, lnf.myFont);
    
    setSize(1500, 930);
    startTimer(50);
}

void MainPageComponent::saveSnapShot(int index)
{
    auto state = processorRef.getState();
    std::ofstream ostream(std::format(
        R"(C:\develop\AudioPluginHost_mk2\audio\granulatorpresets\{}.json)", index + 1));
    choc::json::writeAsJSON(ostream, state, true);
    processorRef.saveSnapShot(index, state);
}

void MainPageComponent::loadSnapShot(int index)
{
    processorRef.loadSnapShot(index);
    // juce::Timer::callAfterDelay(100, [this]() { processorRef.sendExtraStatesToGUI(); });
}

MainPageComponent::~MainPageComponent()
{
    setLookAndFeel(nullptr);
    deinit_step_sequencer_js();
}

void MainPageComponent::updateInsertParameterMetaDatas()
{
    auto f = [this](ParameterGroupComponent *g) {
        for (auto &s : g->sliders)
        {
            auto id = s->getParameterMetaData().id;
            auto pmd = processorRef.granulator.idtoparmetadata[id];
            if (s->getParameterMetaData().name != pmd->name)
            {
                s->setParameterMetaData(*pmd, false);
            }
        }
    };
    f(&insert1ParamsComponent);
    f(&insert2ParamsComponent);
    for (auto &c : modRowComps)
    {
        c->initDestinationDrop();
    }
}

void MainPageComponent::handleFilterSelection(int filterindex)
{
    DropDownComponent *c = filter1Drop.get();
    if (filterindex == 1)
        c = filter2Drop.get();
    auto it = filterInfoMap.find(c->selectedId);
    if (it != filterInfoMap.end())
    {
        DBG(it->second.displayname);
        ThreadMessage msg;
        msg.opcode = ThreadMessage::OP_FILTERTYPE;
        msg.filterindex = filterindex;
        msg.insertmainmode = it->second.mainmode;
        msg.awtype = it->second.awtype;
        msg.filtermodel = it->second.sstmodel;
        msg.filterconfig = it->second.sstconfig;
        processorRef.from_gui_fifo.push(msg);
    }
    juce::Timer::callAfterDelay(250, [this]() { updateInsertParameterMetaDatas(); });
}

void MainPageComponent::fillDropWithFilters(int filterIndex, DropDownComponent &drop,
                                            std::string rootText)
{
    drop.rootNode.text = rootText;
    std::map<std::string, DropDownComponent::Node *> nodemap;
    drop.rootNode.children.reserve(32);
    auto inserttypes = GrainInsertFX::getAvailableModes();
    int filterID = 0;
    for (auto &mod : inserttypes)
    {
        if (!mod.groupname.empty() && !nodemap.contains(mod.groupname))
        {
            drop.rootNode.children.push_back({mod.groupname, -1});
            nodemap[mod.groupname] = &drop.rootNode.children.back();
        }
        if (!mod.groupname.empty())
        {
            nodemap[mod.groupname]->children.push_back({mod.displayname, filterID});
            filterInfoMap[filterID] = mod;
            ++filterID;
        }
        else
        {
            drop.rootNode.children.push_back({mod.displayname, filterID});
            filterInfoMap[filterID] = mod;
            ++filterID;
        }
    }
    drop.setSelectedId(0);
#ifdef JUSTSSTFILTERS
    auto models = sfpp::Filter::availableModels();
    int filterID = 0;
    for (auto &mod : models)
    {
        auto modelname = sfpp::toString(mod);
        if (!nodemap.contains(modelname))
        {
            drop.rootNode.children.push_back({sfpp::toString(mod), filterID});
            filterInfoMap[filterID] = {mod};
            ++filterID;
            nodemap[modelname] = &drop.rootNode.children.back();
        }
        auto subm = sfpp::Filter::availableModelConfigurations(mod, true);
        if (subm.size() > 0)
        {
            for (auto s : subm)
            {
                std::string subname = "";
                auto [pt, st, dt, smt] = s;
                if (pt != sfpp::Passband::UNSUPPORTED)
                {
                    subname += " " + sfpp::toString(pt);
                }
                if (st != sfpp::Slope::UNSUPPORTED)
                {
                    subname += " " + sfpp::toString(st);
                }
                if (dt != sfpp::DriveMode::UNSUPPORTED)
                {
                    subname += " " + sfpp::toString(dt);
                }
                if (smt != sfpp::FilterSubModel::UNSUPPORTED)
                {
                    subname += " " + sfpp::toString(smt);
                }
                nodemap[modelname]->children.push_back({subname, filterID});
                filterInfoMap[filterID] = {mod, s};
                ++filterID;
            }
        }
    }

    drop.setSelectedId(0);
#endif
}

void MainPageComponent::showFilterMenu(int whichfilter)
{
    juce::PopupMenu menu;
    auto models = sfpp::Filter::availableModels();
    for (auto &mod : models)
    {
        juce::PopupMenu submenu;
        auto subm = sfpp::Filter::availableModelConfigurations(mod, true);
        if (subm.size() > 0)
        {
            for (auto s : subm)
            {
                std::string address;
                auto [pt, st, dt, smt] = s;
                if (pt != sfpp::Passband::UNSUPPORTED)
                {
                    address += " " + sfpp::toString(pt);
                }
                if (st != sfpp::Slope::UNSUPPORTED)
                {
                    address += " " + sfpp::toString(st);
                }
                if (dt != sfpp::DriveMode::UNSUPPORTED)
                {
                    address += " " + sfpp::toString(dt);
                }
                if (smt != sfpp::FilterSubModel::UNSUPPORTED)
                {
                    address += " " + sfpp::toString(smt);
                }
                submenu.addItem(address, [this, mod, s, address, whichfilter]() {
                    ThreadMessage msg;
                    msg.opcode = ThreadMessage::OP_FILTERTYPE;
                    msg.filterindex = whichfilter;
                    msg.filtermodel = mod;
                    msg.filterconfig = s;
                    processorRef.from_gui_fifo.push(msg);
                    // if (whichfilter == 0)
                    //     filter0But.setButtonText(sfpp::toString(mod) + " : " + address);
                    // if (whichfilter == 1)
                    //     filter1But.setButtonText(sfpp::toString(mod) + " : " + address);
                });
            }
            menu.addSubMenu(sfpp::toString(mod), submenu);
        }
        else
        {
            menu.addItem(sfpp::toString(mod), [this, mod, whichfilter]() {
                ThreadMessage msg;
                msg.opcode = ThreadMessage::OP_FILTERTYPE;
                msg.filterindex = whichfilter;
                msg.filtermodel = mod;
                processorRef.from_gui_fifo.push(msg);
                // if (whichfilter == 0)
                //     filter0But.setButtonText(sfpp::toString(mod));
                // if (whichfilter == 1)
                //     filter1But.setButtonText(sfpp::toString(mod));
            });
        }
    }
    menu.showMenuAsync(juce::PopupMenu::Options{});
}

void MainPageComponent::timerCallback()
{

    envcomp.updateIfNeeded();
    auxenvcomp.updateIfNeeded();

    infoLabel.setText(
        std::format("[CPU Load {:3.0f}%] [{}/{} voices {}/{} scheduled] [{} in {} out] ",
                    processorRef.perfMeasurer.getLoadAsPercentage(),
                    processorRef.granulator.numVoicesUsed.load(), processorRef.granulator.numvoices,
                    processorRef.granulator.scheduledGrains.size(),
                    processorRef.granulator.scheduledGrains.capacity(),
                    processorRef.getTotalNumInputChannels(),
                    processorRef.getTotalNumOutputChannels()),

        juce::dontSendNotification);
    for (auto &c : stepcomps)
    {
        c->updateGUI();
    }
    ParameterMessage parmsg;
    while (processorRef.params_to_gui_fifo.pop(parmsg))
    {
        auto it = idToSlider.find(parmsg.id);
        if (it != idToSlider.end())
        {
            auto xs = it->second;
            xs->setValue(parmsg.value);
        }
    }
    ThreadMessage msg;
    while (processorRef.to_gui_fifo.pop(msg))
    {
        if (msg.opcode == ThreadMessage::OP_STEPSEQUENCER)
        {
            auxenvcomp.repaint();
        }
        if (msg.opcode == ThreadMessage::OP_FILTERTYPE)
        {
            for (auto &e : filterInfoMap)
            {
                if (e.second.mainmode == msg.insertmainmode && e.second.awtype == msg.awtype &&
                    e.second.sstmodel == msg.filtermodel && e.second.sstconfig == msg.filterconfig)
                {
                    if (msg.filterindex == 0)
                        filter1Drop->setSelectedId(e.first);
                    if (msg.filterindex == 1)
                        filter2Drop->setSelectedId(e.first);
                    break;
                }
            }
            updateInsertParameterMetaDatas();
        }
        if (msg.opcode == ThreadMessage::OP_MODROUTING && msg.modslot < modRowComps.size())
        {
            modRowComps[msg.modslot]->sourceDrop.setSelectedId(msg.modsource);

            modRowComps[msg.modslot]->viaDrop.setSelectedId(msg.modvia);

            modRowComps[msg.modslot]->depthSlider.setValue(msg.depth, juce::dontSendNotification);
            modRowComps[msg.modslot]->destDrop.setSelectedId(msg.moddest);

            modRowComps[msg.modslot]->setTarget(msg.moddest);
            modRowComps[msg.modslot]->curveDrop.setSelectedId(msg.modcurve);
        }
    }
}

//==============================================================================
void MainPageComponent::paint(juce::Graphics &g) { g.fillAll(juce::Colours::darkgrey); }

void MainPageComponent::resized()
{
    oscillatorComponent.setBounds(0, 0, 500, 175);
    volumeParamsComponent.setBounds(0, oscillatorComponent.getBottom() + 1, 500, 125);
    timeParamsComponent.setBounds(502, 0, 300, 125);

    envcomp.setBounds(502, timeParamsComponent.getBottom() + 1, 175, 175);
    auxenvcomp.setBounds(envcomp.getRight() + 2, timeParamsComponent.getBottom() + 1, 175, 175);

    spatParamsComponent.setBounds(0, 302, 500, 125);
    mainParamsComponent.setBounds(502, 302, 500, 125);
    insert1ParamsComponent.setBounds(880, 0, 620, 100);
    insert2ParamsComponent.setBounds(880, 151, 620, 100);
    stackParamsComponent.setBounds(1004, 302, 500, 125);

    lfoTabs.setBounds(0, mainParamsComponent.getBottom() + 1, getWidth(), 110);

    int yoffs = lfoTabs.getBottom() + 1;
    juce::FlexBox modrowflex;
    modrowflex.flexDirection = juce::FlexBox::Direction::column;
    modrowflex.flexWrap = juce::FlexBox::Wrap::wrap;
    for (int i = 0; i < modRowComps.size(); ++i)
    {
        modrowflex.items.add(
            juce::FlexItem(*modRowComps[i]).withFlex(1).withMinHeight(25).withMargin(1));
    }
    modrowflex.performLayout(juce::Rectangle<int>{0, yoffs, getWidth(), 220});
    infoLabel.setBounds(0, getHeight() - 25, getWidth() - 71, 24);

    
}

void StepSeqComponent::paint(juce::Graphics &g)
{
    g.fillAll(juce::Colours::black);
    auto &msrc = gr->stepModSources[sindex];
    int maxstepstodraw = (getWidth() - graphxpos) / 16;
    int stepstodraw = std::min<int>(maxstepstodraw, msrc.numactivesteps);
    for (int i = 0; i < maxstepstodraw; ++i)
    {
        float xcor = graphxpos + i * 16.0;
        float v = msrc.steps[i];
        if (msrc.unipolar)
            v = (v + 1.0) * 0.5;
        if (i == playingStep)
            g.setColour(juce::Colours::white);
        else
        {
            if (i >= msrc.loopstartstep && i < msrc.loopstartstep + msrc.looplen)
                g.setColour(juce::Colours::green);
            else
                g.setColour(juce::Colours::darkgreen.darker());
        }

        if (v < 0.0)
        {
            float h = juce::jmap<float>(v, -1.0, 0.0, getHeight() / 2.0, 0.0);
            g.fillRect(xcor, getHeight() / 2.0, 15.0, h);
        }
        else
        {

            float h = juce::jmap<float>(v, 0.0, 1.0, 0.0, getHeight() / 2.0);
            g.fillRect(xcor, getHeight() / 2.0 - h, 15.0, h);
        }
    }
    g.setColour(juce::Colours::white);
    g.drawRect(graphxpos + editRange.getStart() * 16.0, 1.0f, editRange.getLength() * 16.0,
               (float)getHeight() - 2.0f, 2.0f);
    g.setColour(juce::Colours::cyan);
    float xcor = graphxpos + (msrc.loopstartstep + msrc.loopoffset) * 16.0;
    g.drawLine(xcor, 0, xcor, getHeight(), 3.0f);
    g.setColour(juce::Colours::black);
    g.drawLine(float(graphxpos), getHeight() / 2.0f, getWidth(), getHeight() / 2.0f);
    juce::String txt;
    txt << editRange.getStart() << " " << editRange.getEnd() << "\n";
    if (autoSetLoop)
        txt << "LOOP FOLLOWS SELECTION";
    g.setColour(juce::Colours::white);
    g.drawMultiLineText(txt, graphxpos, 20, 200);
}

bool StepSeqComponent::keyPressed(const juce::KeyPress &ev)
{
    auto &msrc = gr->stepModSources[sindex];
    int curstart = msrc.loopstartstep;
    int curlooplen = msrc.looplen;

    int actiontaken = 0;
    auto incdecstep = [this, &msrc](float step) {
        int index = editRange.getStart();
        float v = msrc.steps[index];
        v = std::clamp(v + step, -1.0f, 1.0f);
        gr->fifo.push({StepModSource::Message::OP_SETSTEP, sindex, v, index});
    };
    if (ev.getKeyCode() == 'R')
    {
        scriptParamsEditor.setVisible(true);
        scriptParamsEditor.setBounds(graphxpos + 1, 1, 150, 25);
        scriptParamsEditor.grabKeyboardFocus();
        scriptParamsEditor.onReturnKey = [this]() {
            scriptParamsEditor.setVisible(false);
            runJSInThread();
        };
        scriptParamsEditor.onEscapeKey = [this]() { scriptParamsEditor.setVisible(false); };
    }
    else if (ev.getKeyCode() == 'Y')
    {
        autoSetLoop = !autoSetLoop;
        actiontaken = 1;
    }
    else if (ev.getKeyCode() == 'P')
    {
        runExternalProgram();
        actiontaken = 2;
    }
    else if (ev.getKeyCode() == 'T')
    {
        incdecstep(0.1f);
        actiontaken = 2;
    }
    else if (ev.getKeyCode() == 'G')
    {
        incdecstep(-0.1f);
        actiontaken = 2;
    }
    else if (ev.getKeyCode() == 'Z')
    {
        int curoff = msrc.loopoffset;
        curoff = (curoff + 1) % msrc.looplen;
        gr->fifo.push({StepModSource::Message::OP_OFFSET, sindex, 0.0f, curoff});
        actiontaken = 2;
    }
    else if (ev.getKeyCode() == 'E')
    {
        setLoopFromSelection();
        actiontaken = 2;
    }
    else if (ev.getKeyCode() == 'D' && ev.getModifiers() == juce::ModifierKeys::noModifiers)
    {
        for (int i = 0; i < editRange.getLength(); ++i)
        {
            int index = editRange.getStart() + i;
            gr->fifo.push({StepModSource::Message::OP_SETSTEP, sindex,
                           rng.nextFloatInRange(-1.0f, 1.0f), index});
        }
        actiontaken = 2;
    }
    else if (ev.getKeyCode() == 'D' && ev.getModifiers().isShiftDown())
    {
        if (editRange.getLength() > 1)
        {
            for (int i = 0; i < editRange.getLength(); ++i)
            {
                int index = editRange.getStart() + i;
                float v = -1.0 + 2.0 / (editRange.getLength() - 1) * i;
                gr->fifo.push({StepModSource::Message::OP_SETSTEP, sindex, v, index});
            }
        }
        else
        {
            gr->fifo.push({StepModSource::Message::OP_SETSTEP, sindex, 0.0f, editRange.getStart()});
        }

        actiontaken = 2;
    }
    else if (ev.getKeyCode() == 'Q' && ev.getModifiers() == juce::ModifierKeys::noModifiers)
    {
        editRange = editRange.movedToStartAt(editRange.getStart() + 1);
        actiontaken = 1;
    }
    else if (ev.getKeyCode() == 'Q' && ev.getModifiers().isShiftDown())
    {
        if (editRange.getLength() > 1)
            editRange.setStart(editRange.getStart() + 1);
        actiontaken = 1;
    }
    else if (ev.getKeyCode() == 'A' && ev.getModifiers() == juce::ModifierKeys::noModifiers)
    {
        editRange = editRange.movedToStartAt(editRange.getStart() - 1);
        actiontaken = 1;
    }
    else if (ev.getKeyCode() == 'A' && ev.getModifiers().isShiftDown())
    {
        if (editRange.getStart() > 0)
            editRange.setStart(editRange.getStart() - 1);
        actiontaken = 1;
    }

    else if (ev.getKeyCode() == 'W')
    {
        editRange = editRange.withLength(editRange.getLength() + 1);
        actiontaken = 1;
    }
    else if (ev.getKeyCode() == 'S')
    {
        editRange = editRange.withLength(editRange.getLength() - 1);
        actiontaken = 1;
    }
    editRange = juce::Range<int>{0, 4096}.constrainRange(editRange);
    if (editRange.getLength() < 1)
        editRange.setLength(1);
    if (actiontaken == 1 && autoSetLoop)
    {
        setLoopFromSelection();
    }
    return actiontaken > 0;
}

StepSeqComponent::StepSeqComponent(int seqindex, ToneGranulator *g, juce::ThreadPool *tp)
    : gr(g), sindex(seqindex), threadPool(tp)
{
    addChildComponent(scriptParamsEditor);
    rng.seed(11400714819323198485ULL, 17 + sindex * 31);
    editRange.setStart(0);
    editRange.setLength(g->stepModSources[sindex].looplen);
    setWantsKeyboardFocus(true);

    addAndMakeVisible(cancelButton);
    cancelButton.setButtonText("Stop JS script");
    cancelButton.onClick = [this]() {
        cancel_js();
        cancelButton.setVisible(false);
    };
    cancelButton.setVisible(false);

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

void StepSeqComponent::runJSInThread()
{
    auto tokens = juce::StringArray::fromTokens(scriptParamsEditor.getText(), true);
    if (tokens.size() < 1 || tokens.size() > 1024)
    {
        return;
    }
    js_status.store(1);
    cancelButton.setVisible(true);
    std::vector<float> params;
    params.reserve(tokens.size());
    for (int i = 0; i < tokens.size(); ++i)
    {
        params.push_back(tokens[i].getFloatValue());
    }

    threadPool->addJob([this, params]() {
        try
        {
            auto jscode = choc::file::loadFileAsString(
                R"(C:\develop\AudioPluginHost_mk2\Source\granularsynth\generatesteps.js)");
            auto steps = gr->stepModSources[sindex].steps;
            steps =
                generate_from_js(jscode, steps, editRange.getStart(), editRange.getEnd(), params);
            for (size_t i = 0; i < steps.size(); ++i)
            {
                gr->fifo.push({StepModSource::Message::OP_SETSTEP, sindex, steps[i],
                               (int)i + editRange.getStart()});
            }
        }
        catch (std::exception &ex)
        {
            DBG(ex.what());
        }
        js_status.store(0);
        juce::MessageManager::callAsync([this]() { cancelButton.setVisible(false); });
    });
}

void StepSeqComponent::runExternalProgram()
{
    threadPool->addJob([this]() {
        juce::ChildProcess cp;
        double t0 = juce::Time::getMillisecondCounterHiRes();
        cp.start(std::format(
            R"(python C:\develop\AudioPluginHost_mk2\Source\granularsynth\stepseq.py {} {} {})",
            sindex, editRange.getStart(), editRange.getLength()));
        auto data = cp.readAllProcessOutput();
        double t1 = juce::Time::getMillisecondCounterHiRes();
        DBG("running ext program took " << t1 - t0 << " millisecods");
        if (!data.containsIgnoreCase("error"))
        {
            auto tokens = juce::StringArray::fromTokens(data, false);
            for (int i = 0; i < tokens.size(); ++i)
            {
                if (tokens[i].isEmpty() || i == 4096 || i > editRange.getLength())
                    break;
                float v = std::clamp(tokens[i].getFloatValue(), -1.0f, 1.0f);
                // DBG(v);
                int index = editRange.getStart() + i;
                gr->fifo.push({StepModSource::Message::OP_SETSTEP, sindex, v, index});
            }
        }
        else
        {
            DBG(data);
        }
    });
}
