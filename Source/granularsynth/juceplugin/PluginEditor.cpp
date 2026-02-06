#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor &p)
    : AudioProcessorEditor(&p), processorRef(p),
      lfoTabs(juce::TabbedButtonBar::Orientation::TabsAtTop)
{
    /*
    addAndMakeVisible(loadModulationSettingsBut);
    loadModulationSettingsBut.setButtonText("LOAD MOD");
    loadModulationSettingsBut.onClick = [this]() {
        processorRef.suspendProcessing(true);
        processorRef.granulator.modmatrix.init_from_json_file(
            R"(C:\develop\AudioPluginHost_mk2\Source\granularsynth\modmatrixconf.json)");
        processorRef.suspendProcessing(false);
    };
    */
    addAndMakeVisible(infoLabel);

    addAndMakeVisible(filter1Drop);
    fillDropWithFilters(0, filter1Drop, "Filter 1");
    filter1Drop.OnItemSelected = [this]() { handleFilterSelection(0); };

    addAndMakeVisible(filter2Drop);
    fillDropWithFilters(1, filter2Drop, "Filter 2");
    filter2Drop.OnItemSelected = [this]() { handleFilterSelection(1); };

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
            addAndMakeVisible(slid.get());
            paramComponents.push_back(std::move(slid));
        }
    }

    for (int i = 0; i < 8; ++i)
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
        lfoTabs.addTab("LFO " + juce::String(i + 1), juce::Colours::lightgrey, lfoc.get(), false);
        lfocomps.push_back(std::move(lfoc));
    }
    for (int i = 0; i < 8; ++i)
    {
        auto stepcomp = std::make_unique<StepSeqComponent>(i, &processorRef.granulator);
        lfoTabs.addTab("STEP SEQ " + juce::String(i + 1), juce::Colours::lightgrey, stepcomp.get(),
                       false);
        stepcomps.push_back(std::move(stepcomp));
    }
    addAndMakeVisible(lfoTabs);
    lfoTabs.setCurrentTabIndex(0);

    setSize(1500, 700);
    startTimer(100);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor() {}

void AudioPluginAudioProcessorEditor::handleFilterSelection(int filterindex)
{
    DropDownComponent *c = &filter1Drop;
    if (filterindex == 1)
        c = &filter2Drop;
    auto it = filterInfoMap.find(c->selectedId);
    if (it != filterInfoMap.end())
    {
        DBG(sfpp::toString(it->second.filtermodel)
            << " " << sfpp::toString(it->second.filterconfig.pt));
        ThreadMessage msg;
        msg.opcode = ThreadMessage::OP_FILTERTYPE;
        msg.filterindex = filterindex;
        msg.filtermodel = it->second.filtermodel;
        msg.filterconfig = it->second.filterconfig;
        processorRef.from_gui_fifo.push(msg);
    }
}

void AudioPluginAudioProcessorEditor::fillDropWithFilters(int filterIndex, DropDownComponent &drop,
                                                          std::string rootText)
{
    drop.rootNode.text = rootText;
    std::map<std::string, DropDownComponent::Node *> nodemap;
    drop.rootNode.children.reserve(32);
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
    infoLabel.setText(std::format("[CPU Load {:3.0f}%] [{} in {} out]",
                                  processorRef.perfMeasurer.getLoadAsPercentage(),
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
            it->second->setValue(parmsg.value);
        }
    }
    ThreadMessage msg;
    while (processorRef.to_gui_fifo.pop(msg))
    {
        if (msg.opcode == ThreadMessage::OP_FILTERTYPE)
        {
            for (auto &e : filterInfoMap)
            {
                if (e.second.filtermodel == msg.filtermodel &&
                    e.second.filterconfig == msg.filterconfig)
                {
                    if (msg.filterindex == 0)
                        filter1Drop.setSelectedId(e.first);
                    if (msg.filterindex == 1)
                        filter2Drop.setSelectedId(e.first);
                    break;
                }
            }
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
    juce::FlexBox layout;
    layout.flexDirection = juce::FlexBox::Direction::column;
    layout.flexWrap = juce::FlexBox::Wrap::wrap;

    for (int i = 0; i < paramComponents.size(); ++i)
    {
        layout.items.add(juce::FlexItem(*paramComponents[i])
                             .withFlex(1.0)
                             .withMinHeight(25)
                             .withMinWidth(50)
                             .withMaxWidth(getWidth() / 2));
    }

    layout.performLayout(juce::Rectangle<int>(0, 0, getWidth(), 330));
    lfoTabs.setBounds(0, paramComponents.back()->getBottom() + 1, 900, 110);
    filter1Drop.setBounds(lfoTabs.getRight() + 1, lfoTabs.getY(), 300, 25);
    filter2Drop.setBounds(lfoTabs.getRight() + 1, filter1Drop.getBottom() + 1, 300, 25);

    int yoffs = lfoTabs.getBottom() + 1;
    for (int i = 0; i < modRowComps.size(); ++i)
    {
        modRowComps[i]->setBounds(1, yoffs + i * 26, getWidth() - 2, 25);
    }
    infoLabel.setBounds(0, getHeight() - 25, getWidth(), 24);
    // loadModulationSettingsBut.setBounds(1, getHeight() - 25, 100, 24);
}
