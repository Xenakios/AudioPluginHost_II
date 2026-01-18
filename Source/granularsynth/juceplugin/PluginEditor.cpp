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

    auto &params = p.getParameters();
    for (int i = 0; i < params.size(); ++i)
    {
        auto pare = std::make_unique<GUIParam>();
        pare->parLabel = std::make_unique<juce::Label>();
        addAndMakeVisible(pare->parLabel.get());
        auto pa = static_cast<juce::RangedAudioParameter *>(params[i]);
        pare->parLabel->setText(pa->getName(100), juce::dontSendNotification);
        if (!pa->isDiscrete())
        {
            pare->slider = std::make_unique<juce::Slider>();
            pare->slider->setTextBoxStyle(juce::Slider::TextEntryBoxPosition::TextBoxRight, false,
                                          50, 20);
            pare->slidAttach =
                std::make_unique<juce::SliderParameterAttachment>(*pa, *pare->slider, nullptr);
            addAndMakeVisible(pare->slider.get());
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
            addAndMakeVisible(pare->combo.get());
        }
        paramEntries.push_back(std::move(pare));
    }
    for (int i = 0; i < 8; ++i)
    {
        auto modcomp = std::make_unique<ModulationRowComponent>();
        modcomp->modslotindex = i;
        modcomp->stateChangedCallback = [this](int slot, int src, float val, int dest) {
            if (slot >= 0 && src >= 0 && dest >= 0)
            {
                ThreadMessage msg;
                msg.opcode = 1;
                msg.modslot = slot;
                msg.modsource = src;
                msg.depth = val;
                msg.moddest = dest;
                processorRef.from_gui_fifo.push(msg);
            }
        };
        addAndMakeVisible(*modcomp);
        modRowComps.push_back(std::move(modcomp));
    }
    addAndMakeVisible(lfoTabs);
    for (int i = 0; i < 8; ++i)
    {
        auto lfoc = std::make_unique<LFOComponent>();
        //addAndMakeVisible(*lfoc);
        lfoc->lfoindex = i;
        lfoc->stateChangedCallback = [this](int lfoindex, int shape, float rateval,
                                            float deformval) {
            ThreadMessage msg;
            msg.opcode = 2;
            msg.lfoindex = lfoindex;
            msg.lfoshape = shape;
            msg.lforate = rateval;
            msg.lfodeform = deformval;
            processorRef.from_gui_fifo.push(msg);
        };
        lfoTabs.addTab(juce::String(i + 1), juce::Colours::grey, lfoc.get(), false);
        lfocomps.push_back(std::move(lfoc));
    }
    lfoTabs.setCurrentTabIndex(0);
    setSize(800, 650);
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor() {}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint(juce::Graphics &g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll(juce::Colours::darkgrey);
}

void AudioPluginAudioProcessorEditor::resized()
{
    for (int i = 0; i < paramEntries.size(); ++i)
    {
        paramEntries[i]->parLabel->setBounds(1, 1 + i * 25, 180, 24);
        if (paramEntries[i]->slider)
            paramEntries[i]->slider->setBounds(182, 1 * i * 25, getWidth() - 184, 24);
        if (paramEntries[i]->combo)
            paramEntries[i]->combo->setBounds(182, 1 * i * 25, getWidth() - 184, 24);
    }
    lfoTabs.setBounds(0, paramEntries.back()->parLabel->getBottom() + 1, 400, 110);
    for (auto &c : lfocomps)
        c->setBounds(0, 25, 300, 75);

    int yoffs = lfoTabs.getBottom() + 1;
    for (int i = 0; i < modRowComps.size(); ++i)
    {
        modRowComps[i]->setBounds(1, yoffs + i * 26, getWidth() - 2, 25);
    }
    loadModulationSettingsBut.setBounds(1, getHeight() - 25, 100, 24);
}
