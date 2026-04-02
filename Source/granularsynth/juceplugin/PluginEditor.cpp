#include "PluginProcessor.h"
#include "PluginEditor.h"
void init_step_sequencer_js();
void deinit_step_sequencer_js();
void cancel_js();
std::vector<float> generate_from_js(std::string jscode, std::vector<float> currentsteps,
                                    int startstep, int endstep, float par0, float par1, float par2,
                                    float par3);

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

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor &p)
    : AudioProcessorEditor(&p), processorRef(p), envcomp(&p.granulator, false),
      auxenvcomp(&p.granulator, true), lfoTabs(juce::TabbedButtonBar::Orientation::TabsAtTop),
      msDebug(&p.granulator)
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
            auto slid = std::make_unique<XapSlider>(true, pmd);
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

    for (int i = 0; i < 12; ++i)
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
    addAndMakeVisible(msDebug);
    setSize(1500, 880);
    startTimer(50);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
    deinit_step_sequencer_js();
}

void AudioPluginAudioProcessorEditor::updateInsertParameterMetaDatas()
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

void AudioPluginAudioProcessorEditor::handleFilterSelection(int filterindex)
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

void AudioPluginAudioProcessorEditor::fillDropWithFilters(int filterIndex, DropDownComponent &drop,
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

void AudioPluginAudioProcessorEditor::showFilterMenu(int whichfilter)
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

void AudioPluginAudioProcessorEditor::timerCallback()
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
void AudioPluginAudioProcessorEditor::paint(juce::Graphics &g)
{
    g.fillAll(juce::Colours::darkgrey);
}

void AudioPluginAudioProcessorEditor::resized()
{
    oscillatorComponent.setBounds(0, 0, 500, 175);
    volumeParamsComponent.setBounds(0, oscillatorComponent.getBottom() + 1, 500, 125);
    timeParamsComponent.setBounds(502, 0, 500, 125);

    envcomp.setBounds(502, timeParamsComponent.getBottom() + 1, 175, 175);
    auxenvcomp.setBounds(envcomp.getRight() + 2, timeParamsComponent.getBottom() + 1, 175, 175);

    spatParamsComponent.setBounds(0, 302, 500, 125);
    mainParamsComponent.setBounds(502, 302, 500, 125);
    insert1ParamsComponent.setBounds(1004, 0, 500, 150);
    insert2ParamsComponent.setBounds(1004, 151, 500, 150);
    stackParamsComponent.setBounds(1004, 302, 500, 175);

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
    modrowflex.performLayout(juce::Rectangle<int>{0, yoffs, getWidth(), 170});
    infoLabel.setBounds(0, getHeight() - 25, getWidth(), 24);
    int vish = 140;
    if (msDebug.is_extended_size)
        vish = 400;
    msDebug.setBounds(1, getHeight() - vish - 25, getWidth() - 2, vish);
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
            float h = juce::jmap<float>(v, -1.0, 0.0, getHeight() / 2, 0.0);
            g.fillRect(xcor, getHeight() / 2.0, 15.0, h);
        }
        else
        {

            float h = juce::jmap<float>(v, 0.0, 1.0, 0.0, getHeight() / 2);
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
    js_status.store(1);
    cancelButton.setVisible(true);
    auto tokens = juce::StringArray::fromTokens(scriptParamsEditor.getText(), true);
    std::array<float, 4> params = {0, 0, 0, 0};
    for (int i = 0; i < params.size(); ++i)
        if (i < tokens.size())
            params[i] = tokens[i].getFloatValue();
    threadPool->addJob([this, params]() {
        try
        {
            auto jscode = choc::file::loadFileAsString(
                R"(C:\develop\AudioPluginHost_mk2\Source\granularsynth\generatesteps.js)");
            auto steps = gr->stepModSources[sindex].steps;
            steps = generate_from_js(jscode, steps, editRange.getStart(), editRange.getEnd(),
                                     params[0], params[1], params[2], params[3]);
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

void ModSourcesDebugComponent::paint(juce::Graphics &g)
{
    g.fillAll(juce::Colours::black);
    /*
    g.setColour(juce::Colours::green);
    for (int i = 0; i < 40; ++i)
    {
        float xcor = i * 4;
        float ycor = juce::jmap<float>(gr->modSourceValues[i], -1.0f, 1.0, getHeight(), 0.0);
        g.drawLine(xcor, 0.0f, xcor, ycor, 3.9);
    }
        */
    // g.setColour(juce::Colours::white);
    float xoffs = 400.0f;
    float w = getWidth() - xoffs;
    double enginetime = gr->playposframes / gr->m_sr;
    for (auto &e : persisted_events)
    {
        float hue = juce::jmap<float>(e.pitch, -48.0f, 64.0f, 0.0f, 0.8f);
        float alpha = juce::jmap<float>(e.gain, 0.0f, 1.0f, 0.0f, 1.0f);
        g.setColour(juce::Colour::fromHSV(hue, 0.8f, 1.0f, alpha));
        float xcor = w - ((enginetime - e.timepos) / timespantoshow * w);
        float ycor = juce::jmap<float>(e.pitch, -48.0, 64.0, getHeight(), 0.0);
        float gw = getWidth() / timespantoshow * e.duration;
        xcor = std::clamp<float>(xcor + xoffs, xoffs, getWidth());
        g.fillEllipse(xcor, ycor, gw, 5.0);
        // g.setColour(juce::Colours::yellow);
        // ycor = juce::jmap<float>(e.azimuthdegrees, -180.0, 180.0, getHeight(), 0.0);
        // g.fillRect(xcor, ycor, 4.0f, 4.0f);
    }
    g.setColour(juce::Colours::white);
    float ellipW = 400.0f;
    float h = ellipW / 2;
    g.drawEllipse(0.0f, 0.0f, ellipW, h, 2.0f);
    float halfW = ellipW / 2.0f;
    float halfH = h / 2.0;
    float centerX = halfW;
    float centerY = halfH;
    for (auto &e : persisted_events)
    {
        g.setColour(juce::Colours::lightblue.withAlpha(e.visualfade));
        /*

        float xcor = juce::jmap<float>(e.azimuthdegrees, -180.0f, 180.0f, 2.0f, 396.0f);
        float ycor =
            juce::jmap<float>(e.elevationdegrees, -90.0f, 90.0f, getHeight() - 4.0f, 2.0f);
        g.fillEllipse(xcor, ycor, 5.0, 5.0);
        */

        if (e.visualfade > 0.01)
        {
#ifdef FOOPROJECTION
            float radElev = juce::degreesToRadians(e.elevationdegrees);
            float cosElev = std::cos(radElev);

            // Map Azimuth (-180 to 180) and Elevation (-90 to 90) to the ellipse
            // Azimuth 0 is Center, Elevation 0 is Center
            float xOffset = (-e.azimuth0degrees / 180.0f) * halfW * cosElev;
            float yOffset = (e.elevationdegrees / 90.0f) * halfH;

            float pixelX = centerX + xOffset;
            float pixelY = centerY - yOffset; // Subtract because Y is down in JUCE
            g.fillEllipse(pixelX - 6.0f, pixelY - 6.0f, 12.0f, 12.0f);

            if (false) // (e.azimuth0degrees != e.azimuth1degrees)
            {
                xOffset = (-e.azimuth1degrees / 180.0f) * halfW * cosElev;
                yOffset = (e.elevationdegrees / 90.0f) * halfH;

                pixelX = centerX + xOffset;
                pixelY = centerY - yOffset; // Subtract because Y is down in JUCE
                g.setColour(juce::Colours::lightgreen.withAlpha(e.visualfade));
                g.fillEllipse(pixelX - 6.0f, pixelY - 6.0f, 12.0f, 12.0f);
            }
#else
            float radAzi = juce::degreesToRadians(-e.azimuth0degrees);
            float radElev = juce::degreesToRadians(-e.elevationdegrees);
            const float sqr2 = std::sqrt(2.0);
            const float halfPI = M_PI * 0.5;
            float x = 2.0 * sqr2 * std::cos(radElev) * std::sin(radAzi / 2.0) /
                      (std::sqrt(1.0 + std::cos(radElev) * std::cos(radAzi / 2.0)));
            float y = sqr2 * std::sin(radElev) /
                      (std::sqrt(1.0 + std::cos(radElev) * std::cos(radAzi / 2.0)));
            float pixelX = centerX + (x * 0.35 * halfW);
            float pixelY = centerY + (y * 0.7 * halfH);
            float alpha = e.visualfade * e.gain;
            g.setColour(juce::Colours::lightgreen.withAlpha(alpha));
            g.fillEllipse(pixelX - 6.0f, pixelY - 6.0f, 12.0f, 12.0f);
#endif
            e.visualfade = e.visualfade * 0.93;
        }
    }
    g.setColour(juce::Colours::white);
    int mins = static_cast<int>(enginetime / 60.0);
    int secs = static_cast<int>(std::fmod(enginetime, 60.0));
    int ms = static_cast<int>(std::fmod(enginetime * 1000.0, 1000.0));

    // 2. Format with leading zeros
    juce::String timeText = juce::String::formatted("%02d:%02d.%03d", mins, secs, ms);
    timeText += " " + juce::String(persisted_events.size()) + " events in history";
    g.setFont(18.0f);
    g.drawText(timeText, xoffs, 1.0f, getWidth() - xoffs, 25, juce::Justification::left);
}