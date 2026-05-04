#pragma once

#include "PluginProcessor.h"
#include "IEM/HammerAitovGrid.h"
class DashBoardComponent : public juce::Component
{
  public:
    ToneGranulator *gr = nullptr;
    std::vector<ToneGranulator::GrainVisualizerMessage> persisted_events;
    HammerAitovGrid haGrid;
    struct ParamEvent
    {
        double timestamp = 0.0;
        double value = 0.0;
        double cpu_usage = 0.0;
    };
    std::vector<ParamEvent> paramValuesHistory;
    juce::Path paramHistoryPath;
    double timespantoshow = 8.0;
    int throttlecounter = 0;
    float visualfadecoefficient = 1.0;
    bool showModulatorValues = false;

    juce::ColourGradient pitchGradient;
    std::unique_ptr<juce::VBlankAttachment> vblankAttachment;
    std::function<double()> GetCPULoad;
    DashBoardComponent(ToneGranulator *g) : gr(g)
    {
        // addAndMakeVisible(haGrid);
        paramHistoryPath.preallocateSpace(2048);
        timespantoshow = gr->gvsettings.timespantoshow;
        pitchGradient.clearColours();
        pitchGradient.addColour(0.00, juce::Colours::red);
        pitchGradient.addColour(0.25, juce::Colours::green);
        pitchGradient.addColour(0.50, juce::Colours::yellow);
        pitchGradient.addColour(0.75, juce::Colours::cyan);
        pitchGradient.addColour(1.00, juce::Colours::white);
        //if (!is_debug())
            visualfadecoefficient = 0.93;
        //else
        //    visualfadecoefficient = std::pow(0.93, 4);
        persisted_events.reserve(4096);
        paramValuesHistory.reserve(1024);
        vblankAttachment = std::make_unique<juce::VBlankAttachment>(this, [this]() {
            updateGrainData();
            if (is_debug())
            {
                if (throttlecounter % 1 == 0)
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
    void paintAmbisonicFieldPolar(juce::Graphics &g);
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
        juce::PopupMenu param_menu;
        param_menu.addItem("-None-", [this]() { gr->modulatedParamToStore.store(0); });
        auto metadata = gr->parmetadatas;
        std::sort(metadata.begin(), metadata.end(), [](auto &lhs, auto &rhs) {
            return lhs.groupName + "/" + lhs.name < rhs.groupName + "/" + rhs.name;
        });
        for (auto &e : metadata)
        {
            if (e.flags & CLAP_PARAM_IS_MODULATABLE)
            {
                param_menu.addItem(e.groupName + "/" + e.name, [this, id = e.id]() {
                    paramValuesHistory.clear();
                    gr->modulatedParamToStore.store(id);
                });
            }
        }
        menu.addSubMenu("Parameter scope", param_menu);
        menu.showMenuAsync(juce::PopupMenu::Options{});
    }
    void drawCPUGraph(juce::Graphics &g, double enginetime, juce::Rectangle<float> area);
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
        auto cpuload = 0.0;
        if (GetCPULoad)
            cpuload = GetCPULoad();
        paramValuesHistory.emplace_back(enginetime, gr->modulatedParValueForGUI.load(), cpuload);
        std::erase_if(paramValuesHistory, [this, enginetime](auto const &ev) {
            return ev.timestamp < enginetime - timespantoshow;
        });
    }

    void resized() override
    {
        float h = getHeight();
        haGrid.setBounds(0.0, h / 2 - 150.0f, 499, 300);
        //haGrid.toArea = haGrid.toArea.translated(0.0f, h / 2 - 150.0f);
    }
};
