#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor &p)
    : AudioProcessorEditor(&p), processorRef(p),
      lfoTabs(juce::TabbedButtonBar::Orientation::TabsAtTop)
{
    addAndMakeVisible(loadModulationSettingsBut);
    loadModulationSettingsBut.setButtonText("LOAD MOD");
    loadModulationSettingsBut.onClick = [this]() {
        processorRef.suspendProcessing(true);
        processorRef.granulator.modmatrix.init_from_json_file(
            R"(C:\develop\AudioPluginHost_mk2\Source\granularsynth\modmatrixconf.json)");
        processorRef.suspendProcessing(false);
    };
    addAndMakeVisible(filter0But);
    filter0But.setButtonText("F1");
    filter0But.onClick = [this]() { showFilterMenu(0); };
    addAndMakeVisible(filter1But);
    filter1But.setButtonText("F2");
    filter1But.onClick = [this]() { showFilterMenu(1); };
    auto &params = p.getParameters();
    for (int i = 0; i < params.size(); ++i)
    {
        auto pare = std::make_unique<GUIParam>();
        addAndMakeVisible(*pare);
        pare->parLabel = std::make_unique<juce::Label>();
        pare->addAndMakeVisible(pare->parLabel.get());
        auto pa = static_cast<juce::RangedAudioParameter *>(params[i]);
        pare->parLabel->setText(pa->getName(100), juce::dontSendNotification);
        if (!pa->isDiscrete())
        {
            pare->slider = std::make_unique<juce::Slider>();
            pare->slider->setTextBoxStyle(juce::Slider::TextEntryBoxPosition::TextBoxRight, false,
                                          50, 20);
            pare->slidAttach =
                std::make_unique<juce::SliderParameterAttachment>(*pa, *pare->slider, nullptr);
            pare->addAndMakeVisible(pare->slider.get());
        }
        else
        {
            pare->combo = std::make_unique<juce::ComboBox>();
            auto strs = pa->getAllValueStrings();
            int j = 1;
            for (auto &str : strs)
            {
                pare->combo->addItem(str, j);
                ++j;
            }
            pare->choiceAttach =
                std::make_unique<juce::ComboBoxParameterAttachment>(*pa, *pare->combo, nullptr);
            pare->addAndMakeVisible(pare->combo.get());
        }
        paramEntries.push_back(std::move(pare));
    }
    for (int i = 0; i < 8; ++i)
    {
        auto modcomp = std::make_unique<ModulationRowComponent>(&processorRef.granulator);
        modcomp->modslotindex = i;
        modcomp->stateChangedCallback = [this](int slot, int src, int via, float val, int dest) {
            if (slot >= 0 && src >= 0 && dest >= 0)
            {
                ThreadMessage msg;
                msg.opcode = 1;
                msg.modslot = slot;
                msg.modsource = src;
                msg.modvia = via;
                msg.depth = val;
                msg.moddest = dest;
                processorRef.from_gui_fifo.push(msg);
            }
        };
        addAndMakeVisible(*modcomp);
        modRowComps.push_back(std::move(modcomp));
    }

    for (int i = 0; i < 8; ++i)
    {
        auto lfoc = std::make_unique<LFOComponent>();
        // addAndMakeVisible(*lfoc);
        lfoc->lfoindex = i;
        lfoc->stateChangedCallback = [this](int lfoindex, int shape, float rateval, float deformval,
                                            float shift, float warp, bool uni) {
            ThreadMessage msg;
            msg.opcode = 2;
            msg.lfoindex = lfoindex;
            msg.lfoshape = shape;
            msg.lforate = rateval;
            msg.lfodeform = deformval;
            msg.lfoshift = shift;
            msg.lfowarp = warp;
            msg.lfounipolar = uni;
            processorRef.from_gui_fifo.push(msg);
        };
        lfoTabs.addTab("LFO " + juce::String(i + 1), juce::Colours::grey, lfoc.get(), false);
        lfocomps.push_back(std::move(lfoc));
    }
    addAndMakeVisible(lfoTabs);
    lfoTabs.setCurrentTabIndex(0);
    addAndMakeVisible(loadStepsBut);
    loadStepsBut.setButtonText("Run");
    loadStepsBut.onClick = [this]() {
        juce::ChildProcess cp;
        cp.start(R"(python C:\develop\AudioPluginHost_mk2\Source\granularsynth\stepseq.py)");
        auto data = cp.readAllProcessOutput();
        // DBG(data);
        auto tokens = juce::StringArray::fromTokens(data, false);
        std::vector<float> steps;
        for (auto &e : tokens)
        {
            float v = std::clamp(e.getFloatValue(), -1.0f, 1.0f);
            steps.push_back(v);
        }
        processorRef.granulator.stepModSources[1].setSteps(steps);
    };
    setSize(1200, 650);
    startTimer(100);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor() {}

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
                    msg.opcode = 100;
                    msg.filterindex = whichfilter;
                    msg.filtermodel = mod;
                    msg.filterconfig = s;
                    processorRef.from_gui_fifo.push(msg);
                    if (whichfilter == 0)
                        filter0But.setButtonText(sfpp::toString(mod) + " : " + address);
                    if (whichfilter == 1)
                        filter1But.setButtonText(sfpp::toString(mod) + " : " + address);
                });
            }
            menu.addSubMenu(sfpp::toString(mod), submenu);
        }
        else
        {
            menu.addItem(sfpp::toString(mod), [this, mod, whichfilter]() {
                ThreadMessage msg;
                msg.opcode = 100;
                msg.filterindex = whichfilter;
                msg.filtermodel = mod;
                processorRef.from_gui_fifo.push(msg);
                if (whichfilter == 0)
                    filter0But.setButtonText(sfpp::toString(mod));
                if (whichfilter == 1)
                    filter1But.setButtonText(sfpp::toString(mod));
            });
        }
    }
    menu.showMenuAsync(juce::PopupMenu::Options{});
}

void AudioPluginAudioProcessorEditor::timerCallback()
{
    ThreadMessage msg;
    while (processorRef.to_gui_fifo.pop(msg))
    {
        if (msg.opcode == 2 && msg.lfoindex < lfocomps.size())
        {
            lfocomps[msg.lfoindex]->rateSlider.setValue(msg.lforate, juce::dontSendNotification);
            lfocomps[msg.lfoindex]->deformSlider.setValue(msg.lfodeform,
                                                          juce::dontSendNotification);
            lfocomps[msg.lfoindex]->shiftSlider.setValue(msg.lfoshift, juce::dontSendNotification);
            lfocomps[msg.lfoindex]->warpSlider.setValue(msg.lfowarp, juce::dontSendNotification);
            lfocomps[msg.lfoindex]->shapeCombo.setSelectedId(msg.lfoshape + 1,
                                                             juce::dontSendNotification);
            lfocomps[msg.lfoindex]->unipolarButton.setToggleState(msg.lfounipolar,
                                                                  juce::dontSendNotification);
        }
        if (msg.opcode == 1 && msg.modslot < modRowComps.size())
        {
            modRowComps[msg.modslot]->sourceCombo.setSelectedId(msg.modsource + 1,
                                                                juce::dontSendNotification);
            modRowComps[msg.modslot]->viaCombo.setSelectedId(msg.modvia + 1,
                                                             juce::dontSendNotification);
            modRowComps[msg.modslot]->depthSlider.setValue(msg.depth, juce::dontSendNotification);
            modRowComps[msg.modslot]->destCombo.setSelectedId(msg.moddest,
                                                              juce::dontSendNotification);
            modRowComps[msg.modslot]->setTarget(msg.moddest);
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
    for (int i = 0; i < paramEntries.size(); ++i)
    {
        layout.items.add(juce::FlexItem(*paramEntries[i])
                             .withFlex(1.0)
                             .withMinHeight(25)
                             .withMinWidth(50)
                             .withMaxWidth(getWidth() / 2));
    }
    layout.performLayout(juce::Rectangle<int>(0, 0, getWidth(), 230));
    lfoTabs.setBounds(0, paramEntries.back()->getBottom() + 1, 400, 110);
    filter0But.setBounds(lfoTabs.getRight() + 1, lfoTabs.getY(), 300, 25);
    filter1But.setBounds(lfoTabs.getRight() + 1, filter0But.getBottom() + 1, 300, 25);
    loadStepsBut.setBounds(lfoTabs.getRight() + 1, filter1But.getBottom() + 1, 100, 25);
    int yoffs = lfoTabs.getBottom() + 1;
    for (int i = 0; i < modRowComps.size(); ++i)
    {
        modRowComps[i]->setBounds(1, yoffs + i * 26, getWidth() - 2, 25);
    }
    loadModulationSettingsBut.setBounds(1, getHeight() - 25, 100, 24);
}
