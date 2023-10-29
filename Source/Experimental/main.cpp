#include <iostream>
#include "testprocessors.h"
#include <memory>
#include <vector>

#include <clap/helpers/event-list.hh>
#include "clap_xaudioprocessor.h"
#include "juce_xaudioprocessor.h"
#include "fileplayer_xaudioprocessor.h"
#include "xap_utils.h"
#include "xap_notegenerator.h"
#include "xap_modulator.h"
#include "xclapeventlist.h"
#include "noise-plethora/plugins/NoisePlethoraPlugin.hpp"
#include "noise-plethora/plugins/Banks.hpp"
#include "xaudiograph.h"
#include "xapdsp.h"
#include <sst/jucegui/components/Knob.h>
#include <sst/jucegui/components/HSlider.h>
#include "text/choc_JSON.h"
#include "text/choc_Files.h"
#include <fstream>

inline void mapModulationEvents(const clap::helpers::EventList &sourceList, clap_id sourceParId,
                                clap::helpers::EventList &destList, clap_id destParId,
                                double destAmount)
{
    for (int i = 0; i < sourceList.size(); ++i)
    {
        auto ev = sourceList.get(i);
        if (ev->type == CLAP_EVENT_PARAM_MOD)
        {
            auto pev = reinterpret_cast<clap_event_param_mod *>(ev);
            double amt = pev->amount * destAmount;
            xenakios::pushParamEvent(destList, true, 0, destParId, amt);
        }
    }
}

class XAPPlayer : public juce::AudioIODeviceCallback
{
  public:
    XAPPlayer(xenakios::XAudioProcessor &procToPlay, int subblocksize = 64)
        : m_proc(procToPlay), m_subblocksize(subblocksize)
    {
    }
    void audioDeviceIOCallbackWithContext(const float *const *inputChannelData,
                                          int numInputChannels, float *const *outputChannelData,
                                          int numOutputChannels, int numSamples,
                                          const AudioIODeviceCallbackContext &context) override
    {
        jassert(m_output_ring_buf.size() >= numSamples * numOutputChannels);
        clap_process process;
        juce::zerostruct(process);
        clap_audio_buffer cab[1];
        cab[0].channel_count = 2;
        cab[0].constant_mask = 0;
        cab[0].data64 = nullptr;
        cab[0].latency = 0;
        cab[0].data32 = (float **)outputChannelData;
        process.audio_outputs_count = 1;
        process.audio_outputs = cab;
        process.frames_count = m_subblocksize;
        while (m_output_ring_buf.available() < numSamples * numOutputChannels)
        {
            auto err = m_proc.process(&process);
            if (err == CLAP_PROCESS_ERROR)
            {
                for (int i = 0; i < numOutputChannels; ++i)
                {
                    for (int j = 0; j < m_subblocksize; ++j)
                    {
                        // outputChannelData[i][j] = 0.0f;
                        m_output_ring_buf.push(0.0f);
                    }
                }
            }
            else
            {
                for (int j = 0; j < m_subblocksize; ++j)
                {
                    for (int i = 0; i < numOutputChannels; ++i)
                    {
                        m_output_ring_buf.push(outputChannelData[i][j]);
                    }
                }
            }
        }
        for (int j = 0; j < numSamples; ++j)
        {
            for (int i = 0; i < numOutputChannels; ++i)
            {
                outputChannelData[i][j] = m_output_ring_buf.pop();
            }
        }
        return;
        for (int i = 0; i < numOutputChannels; ++i)
        {
            for (int j = 0; j < numSamples; ++j)
            {
                outputChannelData[i][j] *= 0.01;
            }
        }
    }
    void audioDeviceAboutToStart(AudioIODevice *device) override
    {
        m_proc.activate(device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples(),
                        device->getCurrentBufferSizeSamples());
    }
    void audioDeviceStopped() override {}

  private:
    xenakios::XAudioProcessor &m_proc;
    SimpleRingBuffer<float, 2048> m_input_ring_buf;
    SimpleRingBuffer<float, 2048> m_output_ring_buf;
    int m_subblocksize = 0;
};

inline void test_graph_processor_realtime()
{
    auto g = std::make_unique<XAPGraph>();
    juce::AudioDeviceManager man;
    man.initialiseWithDefaultDevices(0, 2);
    XAPPlayer xplayer(*g);
    man.addAudioCallback(&xplayer);
    double t0 = juce::Time::getMillisecondCounterHiRes();
    while (true)
    {
        double t1 = juce::Time::getMillisecondCounterHiRes();
        if (t1 - t0 > 5000)
            break;
        std::cout << t1 - t0 << "\t" << man.getCpuUsage() * 100.0 << "% CPU" << std::endl;
        juce::Thread::sleep(500);
    }
}

inline void test_graph_processor_offline()
{
    auto g = std::make_unique<XAPGraph>();
    g->addProcessorAsNode(std::make_unique<ToneProcessorTest>(), "Tone 1");
    g->addProcessorAsNode(std::make_unique<ToneProcessorTest>(), "Tone 2");
    g->addProcessorAsNode(std::make_unique<ClapPluginFormatProcessor>(
                              R"(C:\Program Files\Common Files\CLAP\Conduit.clap)", 3),
                          "Ring Mod");
    std::string pathprefix = R"(C:\Program Files\Common Files\)";
    g->addProcessorAsNode(std::make_unique<ClapPluginFormatProcessor>(
                              pathprefix + R"(CLAP\Surge Synth Team\Surge XT.clap)", 0),
                          "Surge XT 1");
    // g->connectAudio("Tone 1", 0, 0, "Ring Mod", 0, 0);
    // g->connectAudio("Tone 1", 0, 0, "Ring Mod", 0, 1);
    // g->connectAudio("Tone 2", 0, 0, "Ring Mod", 1, 0);
    // g->connectAudio("Tone 2", 0, 0, "Ring Mod", 1, 1);
    g->addProcessorAsNode(std::make_unique<ClapEventSequencerProcessor>(3), "Note Gen");
    connectEventPorts(g->findNodeByName("Note Gen"), 0, g->findNodeByName("Surge XT 1"), 0);
    g->outputNodeId = "Surge XT 1";
    double sr = 44100;
    int blocksize = 128;
    std::vector<float> procbuf(blocksize * 2);
    clap_audio_buffer cab[1];
    cab[0].channel_count = 2;
    cab[0].constant_mask = 0;
    cab[0].data64 = nullptr;
    cab[0].latency = 0;
    float *chandatas[2] = {&procbuf[blocksize * 0], &procbuf[blocksize * 1]};
    cab[0].data32 = chandatas;
    clap_process process;
    memset(&process, 0, sizeof(clap_process));
    process.audio_outputs_count = 1;
    process.audio_outputs = cab;
    process.frames_count = blocksize;
    g->activate(sr, blocksize, blocksize);

    g->findProcessor("Tone 1")->enqueueParameterChange(
        xenakios::CrossThreadMessage().asParamChange(ToneProcessorTest::ParamIds::Pitch, 72.0));
    g->findProcessor("Tone 1")->enqueueParameterChange(
        {(clap_id)ToneProcessorTest::ParamIds::Distortion, CLAP_EVENT_PARAM_VALUE, 0.1});
    g->findProcessor("Tone 2")->enqueueParameterChange(
        {(clap_id)ToneProcessorTest::ParamIds::Pitch, CLAP_EVENT_PARAM_VALUE, 35.5});
    g->findProcessor("Tone 2")->enqueueParameterChange(
        xenakios::CrossThreadMessage().asParamChange(ToneProcessorTest::ParamIds::Distortion, 0.0));
    // ids taken from Conduit source, 842 ring mod mix level, 712 other source is internal
    // osc/sidechain
    g->findNodeByName("Ring Mod")
        ->processor->enqueueParameterChange({(clap_id)842, CLAP_EVENT_PARAM_VALUE, 1.0});
    g->findNodeByName("Ring Mod")
        ->processor->enqueueParameterChange({(clap_id)712, CLAP_EVENT_PARAM_VALUE, 1.0});
    int outlen = 120 * sr;
    int outcounter = 0;
    juce::File outfile(
        R"(C:\develop\AudioPluginHost_mk2\Source\Experimental\audio\graph_out_05.wav)");
    outfile.deleteFile();
    auto ostream = outfile.createOutputStream();
    WavAudioFormat wav;
    auto writer = wav.createWriterFor(ostream.release(), sr, 2, 32, {}, 0);
    while (outcounter < outlen)
    {
        g->process(&process);
        writer->writeFromFloatArrays(chandatas, 2, blocksize);
        outcounter += blocksize;
    }
    delete writer;
}

struct PluginContentComponent : public juce::Component
{
    xenakios::XAudioProcessor &m_proc;
    juce::Label infoLabel;
    PluginContentComponent(xenakios::XAudioProcessor &proc) : m_proc(proc)
    {
        addAndMakeVisible(infoLabel);
        infoLabel.setText("Info label", juce::dontSendNotification);
        juce::MessageManager::callAsync([this]() {
            clap_plugin_descriptor desc;
            auto m_test_proc = &m_proc;
            if (m_test_proc->getDescriptor(&desc))
            {
                if (getParentComponent())
                    getParentComponent()->setName(juce::String(desc.vendor) + " : " + desc.name);
            }
            m_test_proc->guiCreate("", false);
            clap_window win;
            win.api = "JUCECOMPONENT";
            win.ptr = this;

            m_test_proc->guiSetParent(&win);

            uint32 w = 0;
            uint32_t h = 0;
            if (m_test_proc->guiGetSize(&w, &h))
            {
                setSize(w, h + m_info_area_margin);
            }
            m_test_proc->guiShow();
            m_test_proc->OnPluginRequestedResize = [this](uint32_t neww, uint32_t newh) {
                // setSize is a synchronous call, so we can toggle the flag like this(?)
                m_plugin_requested_resize = true;
                setSize(neww, newh + m_info_area_margin);
                m_plugin_requested_resize = false;
            };
        });
        setSize(10, 10);
    }
    void resized() override
    {
        auto m_test_proc = &m_proc;
        uint32_t w = 0;
        uint32_t h = 0;
        m_test_proc->guiGetSize(&w, &h);
        infoLabel.setBounds(0, getHeight() - m_info_area_margin, getWidth(), m_info_area_margin);

        if (!m_plugin_requested_resize && m_test_proc->guiCanResize())
            m_test_proc->guiSetSize(getWidth(), getHeight() - m_info_area_margin);
    }

    int m_info_area_margin = 25;
    bool m_plugin_requested_resize = false;
};

class XapWindow : public juce::DocumentWindow
{
  public:
    xenakios::XAudioProcessor &m_proc;
    PluginContentComponent m_content;
    XapWindow(xenakios::XAudioProcessor &proc)
        : juce::DocumentWindow("XAP", juce::Colours::darkgrey, 4, true), m_proc(proc),
          m_content(proc)
    {
        setUsingNativeTitleBar(true);
        setVisible(true);
        // setAlwaysOnTop(true);
        setWantsKeyboardFocus(true);
        setContentNonOwned(&m_content, true);
        setResizable(true, false);
    }
    ~XapWindow() override
    {
        m_proc.guiHide();
        m_proc.guiDestroy();
    }
    void resized() override { m_content.setBounds(0, 0, getWidth(), getHeight()); }
    void closeButtonPressed() override
    {
        if (OnRequestDelete)
        {
            OnRequestDelete(this);
        }
    }
    std::function<void(XapWindow *)> OnRequestDelete;
    bool m_plugin_requested_resize = false;
    int m_info_area_margin = 25;
    std::string nodeID;
    juce::Label m_info_label;
};

class NodeListComponent : public juce::Component, public juce::ListBoxModel
{
  public:
    XAPGraph *m_graph = nullptr;
    juce::ListBox m_node_listbox;
    juce::TextButton m_node_add_but;
    juce::TextButton m_node_remove_but;
    NodeListComponent(XAPGraph *xg) : m_graph(xg)
    {
        m_node_listbox.setModel(this);
        m_node_listbox.updateContent();
        addAndMakeVisible(m_node_listbox);
        addAndMakeVisible(m_node_add_but);
        addAndMakeVisible(m_node_remove_but);
        m_node_add_but.setButtonText("Add...");

        m_node_remove_but.setButtonText("Remove");
    }

    void resized() override
    {
        m_node_listbox.setBounds(0, 0, getWidth(), getHeight() - 25);
        m_node_add_but.setBounds(0, m_node_listbox.getBottom() + 1, getWidth() / 2 - 2, 25);
        m_node_remove_but.setBounds(m_node_add_but.getRight() + 2, m_node_listbox.getBottom() + 1,
                                    getWidth() / 2 - 2, 25);
    }
    int getNumRows() override { return m_graph->proc_nodes.size(); }
    void paintListBoxItem(int rowNumber, Graphics &g, int width, int height,
                          bool rowIsSelected) override
    {
        if (rowNumber < 0 || rowNumber >= m_graph->proc_nodes.size())
            return;
        auto &node = m_graph->proc_nodes[rowNumber];
        if (rowIsSelected)
            g.fillAll(juce::Colours::darkgrey);
        g.setColour(juce::Colours::white);
        g.drawText(node->processorName, 0, 0, width, height, juce::Justification::left);
    }
    void listBoxItemClicked(int row, const juce::MouseEvent &) override
    {
        if (row >= 0 && row < m_graph->proc_nodes.size())
        {
            if (OnListRowClicked)
                OnListRowClicked(row);
        }
    }
    std::function<void(int)> OnListRowClicked;
};

class NodeGraphComponent : public juce::Component
{
  public:
    NodeGraphComponent(XAPGraph *g) : m_graph(g) {}
    void paint(juce::Graphics &g) override
    {
        g.fillAll(juce::Colours::black);
        g.setColour(juce::Colours::white);
        for (auto &n : m_graph->proc_nodes)
        {
            auto nodebounds = n->nodeSceneBounds;
            g.drawRect(nodebounds);
            g.drawText(n->displayName, nodebounds, juce::Justification::centredLeft);
            for (auto &conn : n->inputConnections)
            {
                int x0 = nodebounds.getCentreX();
                int x1 = conn.source->nodeSceneBounds.getCentreX();
                int y0 = nodebounds.getY();
                int y1 = conn.source->nodeSceneBounds.getBottom();
                g.drawLine(x0, y0, x1, y1);
            }
        }
    }
    void mouseDown(const juce::MouseEvent &ev) override
    {
        m_dragging_node = findFromPosition(ev.x, ev.y);
        if (m_dragging_node)
        {
            m_drag_start_bounds = m_dragging_node->nodeSceneBounds;
        }
    }
    void mouseDrag(const juce::MouseEvent &ev) override
    {
        if (m_dragging_node)
        {
            auto newbounds = m_drag_start_bounds.translated(ev.getDistanceFromDragStartX(),
                                                            ev.getDistanceFromDragStartY());
            m_dragging_node->nodeSceneBounds = newbounds;
            repaint();
        }
    }
    void mouseUp(const juce::MouseEvent &ev) override
    {
        m_dragging_node = nullptr;
        m_drag_start_bounds = {};
    }
    XAPNode *findFromPosition(int x, int y)
    {
        for (auto &n : m_graph->proc_nodes)
        {
            if (n->nodeSceneBounds.contains(x, y))
            {
                return n.get();
            }
        }
        return nullptr;
    }

  private:
    XAPGraph *m_graph = nullptr;
    XAPNode *m_dragging_node = nullptr;
    juce::Rectangle<int> m_drag_start_bounds;
};

class MyContinuous : public sst::jucegui::data::Continuous
{
  public:
    float m_value = 0.0;
    float getValue() const override { return m_value; };
    void setValueFromGUI(const float &f) override { m_value = f; };
    void setValueFromModel(const float &f) override { m_value = f; };
    float getDefaultValue() const override { return 0.0f; };
    std::string getLabel() const override { return "Test Parameter"; };
};

class MainComponent : public juce::Component, public juce::Timer
{
  public:
    std::vector<std::unique_ptr<XapWindow>> m_xap_windows;
    juce::AudioDeviceManager m_aman;
    std::unique_ptr<XAPGraph> m_graph;
    std::unique_ptr<XAPPlayer> m_player;
    int m_info_area_margin = 25;
    juce::Label m_infolabel;
    juce::ComboBox m_mod_rout_combo;
    MyContinuous m_knob_data_source;
    sst::jucegui::components::HSlider m_sst_knob;
    std::unique_ptr<NodeListComponent> m_node_list;
    juce::TextEditor m_node_info_ed;
    std::unique_ptr<NodeGraphComponent> m_graph_component;
    void timerCallback() override
    {
        int usage = m_aman.getCpuUsage() * 100.0;
        m_infolabel.setText("CPU " + juce::String(usage) + "%", juce::dontSendNotification);
    }
    std::string pathprefix = R"(C:\Program Files\Common Files\)";
    void addNoteGeneratorTestNodes()
    {
        m_graph->addProcessorAsNode(std::make_unique<ClapEventSequencerProcessor>(2), "Note Gen");
        // m_graph->addProcessorAsNode(std::make_unique<ClapPluginFormatProcessor>(
        //                                 pathprefix + R"(CLAP\Surge Synth Team\Surge XT.clap)",
        //                                 0),
        //                             "Surge XT 1");

        m_graph->addProcessorAsNode(
            std::make_unique<ClapPluginFormatProcessor>(pathprefix + "/CLAP/Conduit.clap", 0),
            "Surge XT 1");

        // m_graph->addProcessorAsNode(std::make_unique<ClapPluginFormatProcessor>(
        //                                pathprefix + R"(CLAP\Surge Synth Team\Surge XT.clap)", 0),
        //                            "Surge XT 2");
        m_graph->addProcessorAsNode(std::make_unique<GainProcessorTest>(), "Main");
        connectEventPorts(m_graph->findNodeByName("Note Gen"), 0,
                          m_graph->findNodeByName("Surge XT 1"), 0);
        // connectEventPorts(m_graph->findNodeByName("Note Gen"), 1,
        //                  m_graph->findNodeByName("Surge XT 2"), 0);

        m_graph->connectAudio("Surge XT 1", 0, 0, "Main", 0, 0);
        m_graph->connectAudio("Surge XT 1", 0, 1, "Main", 0, 1);
        // m_graph->connectAudio("Surge XT 2", 0, 0, "Main", 0, 0);
        // m_graph->connectAudio("Surge XT 2", 0, 1, "Main", 0, 1);
        m_graph->outputNodeId = "Main";
    }
    void addFilePlayerTestNodes()
    {
        m_graph->addProcessorAsNode(std::make_unique<FilePlayerProcessor>(), "File 1");
        auto fp = m_graph->findProcessorAndCast<FilePlayerProcessor>("File 1");
        m_graph->addProcessorAsNode(std::make_unique<GainProcessorTest>(), "Main");
        m_graph->connectAudio("File 1", 0, 0, "Main", 0, 0);
        m_graph->connectAudio("File 1", 0, 1, "Main", 0, 1);
        m_graph->outputNodeId = "Main";
    }
    void addConduitTestNodes()
    {
        m_graph->addProcessorAsNode(std::make_unique<FilePlayerProcessor>(), "File 1");
        auto fp = m_graph->findProcessorAndCast<FilePlayerProcessor>("File 1");
        m_graph->addProcessorAsNode(
            std::make_unique<ClapPluginFormatProcessor>(pathprefix + "CLAP/Conduit.clap", 3),
            "Main");
        m_graph->connectAudio("File 1", 0, 0, "Main", 0, 0);
        m_graph->connectAudio("File 1", 0, 1, "Main", 0, 1);
        m_graph->outputNodeId = "Main";
    }
    void addToneGeneratorTestNodes()
    {
        m_graph->addProcessorAsNode(std::make_unique<ToneProcessorTest>(), "Tone 1");
        m_graph->addProcessorAsNode(std::make_unique<GainProcessorTest>(), "Main");
        m_graph->addProcessorAsNode(std::make_unique<ModulatorSource>(1, 1.0), "LFO 1");
        m_graph->addProcessorAsNode(std::make_unique<ModulatorSource>(1, 4.0), "LFO 2");
        m_graph->connectAudio("Tone 1", 0, 0, "Main", 0, 0);
        m_graph->connectAudio("Tone 1", 0, 0, "Main", 0, 1);
        m_graph->connectModulationByNames("LFO 1", 0, "Tone 1",
                                          (clap_id)ToneProcessorTest::ParamIds::Pitch, false, 6.0);
        m_graph->connectModulationByNames("LFO 2", 0, "Tone 1",
                                          (clap_id)ToneProcessorTest::ParamIds::Pitch, false, 1.0);
        m_graph->outputNodeId = "Main";
    }
    MainComponent()
    {

        addAndMakeVisible(m_infolabel);
        addAndMakeVisible(m_mod_rout_combo);
        sst::jucegui::style::StyleSheet::initializeStyleSheets([]() {});
        auto style = sst::jucegui::style::StyleSheet::getBuiltInStyleSheet(
            sst::jucegui::style::StyleSheet::BuiltInTypes::DARK);
        m_sst_knob.setShowLabel(true);
        // m_sst_knob.setS
        m_sst_knob.setStyle(style);
        m_sst_knob.setSource(&m_knob_data_source);

        addAndMakeVisible(m_sst_knob);
        startTimerHz(10);

        m_graph = std::make_unique<XAPGraph>();
        addNoteGeneratorTestNodes();
        // addFilePlayerTestNodes();
        // addToneGeneratorTestNodes();
        // addConduitTestNodes();
        m_node_list = std::make_unique<NodeListComponent>(m_graph.get());
        m_node_list->m_node_add_but.onClick = [this]() { showNodeAddMenu(); };
        addAndMakeVisible(m_node_list.get());
        addAndMakeVisible(m_node_info_ed);
        m_node_list->OnListRowClicked = [this](int row) {
            populateNodeInfoBox(m_graph->proc_nodes[row].get());
        };
        m_node_info_ed.setMultiLine(true);
        m_node_info_ed.setReadOnly(true);

        juce::Random rng{7};
        for (auto &n : m_graph->proc_nodes)
        {
            // continue;
            m_xap_windows.emplace_back(std::make_unique<XapWindow>(*n->processor));
            m_xap_windows.back()->nodeID = n->displayName;
            m_xap_windows.back()->setTopLeftPosition(rng.nextInt({10, 600}),
                                                     rng.nextInt({100, 400}));
            n->nodeSceneBounds = {rng.nextInt({10, 600}), rng.nextInt({10, 600}), 100, 20};
            m_xap_windows.back()->OnRequestDelete = [this](XapWindow *w) {
                for (int i = 0; i < m_xap_windows.size(); ++i)
                {
                    if (m_xap_windows[i].get() == w)
                    {
                        m_xap_windows.erase(m_xap_windows.begin() + i);
                        break;
                    }
                }
            };
        }

        m_player = std::make_unique<XAPPlayer>(*m_graph);
        m_aman.initialiseWithDefaultDevices(0, 2);
        m_aman.addAudioCallback(m_player.get());
        m_graph_component = std::make_unique<NodeGraphComponent>(m_graph.get());
        addAndMakeVisible(m_graph_component.get());
        setSize(1000, 600);
        juce::MessageManager::callAsync([this]() {
            loadState(juce::File());
            m_graph_component->repaint();
        });
    }
    void saveState(juce::File file)
    {
        auto root = choc::value::createObject("");
        auto wstate = choc::value::createObject("");
        wstate.addMember("win_bounds", getScreenBounds().toString().toStdString());
        root.addMember("mainwindowstate", wstate);
        std::vector<choc::value::Value> nodestates;
        std::vector<choc::value::Value> connstates;
        for (auto &n : m_graph->proc_nodes)
        {
            auto nodestate = choc::value::createObject("");
            nodestate.addMember("id", n->displayName);
            juce::Rectangle<int> bounds;
            for (auto &w : m_xap_windows)
            {
                if (&w->m_proc == n->processor.get())
                {
                    bounds = w->getScreenBounds();
                    break;
                }
            }
            if (!bounds.isEmpty())
            {
                nodestate.addMember("plugwinbounds", bounds.toString().toStdString());
            }
            nodestate.addMember("nodebounds", n->nodeSceneBounds.toString().toStdString());
            for (auto &conn : n->inputConnections)
            {
                auto jconn = choc::value::createObject("");
                jconn.addMember("type", static_cast<int>(conn.type));
                jconn.addMember("src", conn.source->displayName);
                jconn.addMember("src_port", conn.sourcePort);
                jconn.addMember("src_chan", conn.sourceChannel);
                jconn.addMember("dest", conn.destination->displayName);
                jconn.addMember("dest_port", conn.destinationPort);
                jconn.addMember("dest_chan", conn.destinationChannel);
                connstates.push_back(jconn);
            }
            nodestates.push_back(nodestate);
        }
        root.addMember("nodestates", choc::value::createArray(nodestates));
        root.addMember("nodeconnections", choc::value::createArray(connstates));
        std::ofstream outfile("C:/develop/AudioPluginHost_mk2/Source/Experimental/state.json");
        choc::json::writeAsJSON(outfile, root, true);
    }
    void loadState(juce::File file)
    {
        try
        {
            auto jsontxt = choc::file::loadFileAsString(
                "C:/develop/AudioPluginHost_mk2/Source/Experimental/state.json");
            auto jroot = choc::json::parse(jsontxt);
            auto jwstate = jroot["mainwindowstate"];
            auto jbounds = jwstate["win_bounds"];
            auto str = jbounds.toString();
            auto boundsrect = juce::Rectangle<int>::fromString(str);
            if (!boundsrect.isEmpty())
            {
                getParentComponent()->setBounds(boundsrect);
            }
            std::unordered_map<std::string, XAPNode *> nodemap;
            for (auto &e : m_graph->proc_nodes)
            {
                nodemap[e->displayName] = e.get();
            }
            auto jnodesarr = jroot["nodestates"];
            auto sz = jnodesarr.size();
            if (sz > 0)
            {
                for (uint32 i = 0; i < sz; ++i)
                {
                    auto jnodestate = jnodesarr[i];
                    auto jid = jnodestate["id"];
                    auto jnodebounds = jnodestate["nodebounds"];
                    auto nodebounds = juce::Rectangle<int>::fromString(jnodebounds.toString());
                    if (!nodebounds.isEmpty())
                    {
                        DBG(jid.toString() << " has node scene bound " << nodebounds.toString());
                        nodemap[jid.toString()]->nodeSceneBounds = nodebounds;
                    }
                    for (auto &w : m_xap_windows)
                    {
                        if (w->nodeID == jid.toString())
                        {
                            auto jbounds = jnodestate["plugwinbounds"].toString();
                            auto bounds = juce::Rectangle<int>::fromString(jbounds);
                            if (!bounds.isEmpty())
                            {
                                w->setBounds(bounds);
                            }
                            break;
                        }
                    }
                }
            }
        }
        catch (std::exception &excep)
        {
            std::cout << excep.what() << "\n";
        }
    }
    void createAndAddNode(std::string name)
    {
        DBG("Should create : " << name);
        auto newproc = xenakios::XapFactory::getInstance().createFromName(name);
        clap_plugin_descriptor desc;
        if (newproc && newproc->getDescriptor(&desc))
        {
            DBG("Created : " << desc.name);
        }
    }
    void showNodeAddMenu()
    {
        juce::PopupMenu menu;
        juce::PopupMenu menuInternals;
        juce::PopupMenu menuClaps;
        juce::PopupMenu menuVST;
        for (auto &e : xenakios::XapFactory::getInstance().m_entries)
        {
            if (e.name.starts_with("Internal/"))
            {
                menuInternals.addItem(e.name, [this, name = e.name]() { createAndAddNode(name); });
            }
        }

        menuClaps.addItem("Surge XT", []() {});
        menuClaps.addItem("Conduit Ring Modulator", []() {});
        menuClaps.addItem("Conduit Polysynth", []() {});

        menuVST.addItem("Valhalla VintageVerb", []() {});
        menuVST.addItem("Valhalla Room", []() {});
        menu.addSubMenu("Internal", menuInternals);
        menu.addSubMenu("CLAP", menuClaps);
        menu.addSubMenu("VST3", menuVST);
        menu.showMenuAsync(juce::PopupMenu::Options());
    }
    void populateNodeInfoBox(XAPNode *node)
    {
        juce::String txt;
        txt << node->displayName << "\n";
        txt << node->processorName << "\n";
        auto note_in_ports = node->processor->notePortsCount(true);
        if (note_in_ports)
        {
            txt << "Note input ports\n";
            clap_note_port_info pinfo;
            if (node->processor->notePortsInfo(0, true, &pinfo))
            {
                txt << "  " << (int64_t)pinfo.id << " " << pinfo.name << "\n";
            }
        }
        auto note_out_ports = node->processor->notePortsCount(false);
        if (note_out_ports)
        {
            txt << "Note output ports\n";
            clap_note_port_info pinfo;
            if (node->processor->notePortsInfo(0, false, &pinfo))
            {
                txt << "  " << (int64_t)pinfo.id << " " << pinfo.name << "\n";
            }
        }
        m_node_info_ed.setText(txt, false);
    }
    ~MainComponent() override
    {
        saveState(juce::File());
        m_xap_windows.clear();
        m_aman.removeAudioCallback(m_player.get());
    }
    void resized() override
    {

        // m_mod_rout_combo.setBounds(0, m_infolabel.getBottom() + 1, 50, 25);
        // m_sst_knob.setBounds(200, 10, 400, 50);
        m_node_list->setBounds(0, 0, 300, getHeight() - 25);
        m_graph_component->setBounds(m_node_list->getRight() + 1, 0, getWidth() - 298,
                                     getHeight() - 25);
        // m_node_info_ed.setBounds(m_node_list->getRight() + 1, 0, 300, 200);
        m_infolabel.setBounds(0, m_node_list->getBottom(), getWidth(), 25);
    }
};

class GuiAppApplication : public juce::JUCEApplication
{
  public:
    //==============================================================================
    GuiAppApplication() {}

    const juce::String getApplicationName() override { return "FOO"; }
    const juce::String getApplicationVersion() override { return "0.0.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    //==============================================================================
    void initialise(const juce::String &commandLine) override
    {
        juce::ignoreUnused(commandLine);
        mainWindow.reset(new MainWindow(getApplicationName()));
    }

    void shutdown() override
    {
        mainWindow = nullptr; // (deletes our window)
    }

    //==============================================================================
    void systemRequestedQuit() override { quit(); }

    void anotherInstanceStarted(const juce::String &commandLine) override
    {
        juce::ignoreUnused(commandLine);
    }

    class MainWindow : public juce::DocumentWindow
    {

      public:
        explicit MainWindow(juce::String name)
            : DocumentWindow(name,
                             juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                                 ResizableWindow::backgroundColourId),
                             DocumentWindow::allButtons)
        {

            setUsingNativeTitleBar(true);

            setContentOwned(new MainComponent, true);
            setResizable(true, true);
            setTopLeftPosition(50, 50);

            setVisible(true);
        }

        void closeButtonPressed() override
        {

            JUCEApplication::getInstance()->systemRequestedQuit();
        }

      private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

  private:
    std::unique_ptr<MainWindow> mainWindow;
};

#define TESTJUCEGUI 1

#if TESTJUCEGUI

START_JUCE_APPLICATION(GuiAppApplication)

#else

inline clap_event_note makeClapNote(int samplepos, int etype, int port, int chan, int key,
                                    int noteid, double velo)
{
    clap_event_note en;
    en.header.flags = 0;
    en.header.size = sizeof(clap_event_note);
    en.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    en.header.time = samplepos;
    en.header.type = etype;
    en.channel = chan;
    en.key = key;
    en.note_id = noteid;
    en.port_index = port;
    en.velocity = velo;
    return en;
}

inline bool isClapNoteEventType(uint16_t et)
{
    return et >= CLAP_EVENT_NOTE_ON && et <= CLAP_EVENT_NOTE_END;
}

// just a raw unchecked cast, use at your own peril
template <typename EventType> inline const EventType *clapCast(const clap_event_header *e)
{
    return reinterpret_cast<const EventType *>(e);
}

// This has not been benchmarked etc, so depending on intended use pattern,
// might not want to always use this because of the runtime
// conditional checking involved. If new event types are added to Clap, this needs to
// be updated...
// Should probably also use switch case in this instead of the ifs...

template <typename EventType> inline const EventType *clapDynCast(const clap_event_header *e)
{
    if (e->space_id == CLAP_CORE_EVENT_SPACE_ID)
    {
        if (e->type >= CLAP_EVENT_NOTE_ON && e->type <= CLAP_EVENT_NOTE_END)
        {
            if constexpr (std::is_same_v<EventType, clap_event_note>)
                return reinterpret_cast<const clap_event_note *>(e);
        }
        if (e->type == CLAP_EVENT_NOTE_EXPRESSION)
        {
            if constexpr (std::is_same_v<EventType, clap_event_note_expression>)
                return reinterpret_cast<const clap_event_note_expression *>(e);
        }
        if (e->type == CLAP_EVENT_PARAM_VALUE)
        {
            if constexpr (std::is_same_v<EventType, clap_event_param_value>)
                return reinterpret_cast<const clap_event_param_value *>(e);
        }
        if (e->type == CLAP_EVENT_PARAM_MOD)
        {
            if constexpr (std::is_same_v<EventType, clap_event_param_mod>)
                return reinterpret_cast<const clap_event_param_mod *>(e);
        }
        if (e->type == CLAP_EVENT_PARAM_GESTURE_BEGIN || e->type == CLAP_EVENT_PARAM_GESTURE_END)
        {
            if constexpr (std::is_same_v<EventType, clap_event_param_gesture>)
                return reinterpret_cast<const clap_event_param_gesture *>(e);
        }
        if (e->type == CLAP_EVENT_MIDI)
        {
            if constexpr (std::is_same_v<EventType, clap_event_midi>)
                return reinterpret_cast<const clap_event_midi *>(e);
        }
        if (e->type == CLAP_EVENT_TRIGGER)
        {
            if constexpr (std::is_same_v<EventType, clap_event_trigger>)
                return reinterpret_cast<const clap_event_trigger *>(e);
        }
        if (e->type == CLAP_EVENT_TRANSPORT)
        {
            if constexpr (std::is_same_v<EventType, clap_event_transport>)
                return reinterpret_cast<const clap_event_transport *>(e);
        }
        if (e->type == CLAP_EVENT_MIDI2)
        {
            if constexpr (std::is_same_v<EventType, clap_event_midi2>)
                return reinterpret_cast<const clap_event_midi2 *>(e);
        }
        if (e->type == CLAP_EVENT_MIDI_SYSEX)
        {
            if constexpr (std::is_same_v<EventType, clap_event_midi_sysex>)
                return reinterpret_cast<const clap_event_midi_sysex *>(e);
        }
    }
    return nullptr;
}

inline void printXList(const xenakios::ClapEventList &elist)
{
    for (int i = 0; i < elist.size(); ++i)
    {
        auto e = elist.get(i);
        if (auto ne = clapDynCast<clap_event_note>(e))
            std::cout << e->time << "\tNOTE EVENT " << ne->key << "\n";
        else if (auto pe = clapDynCast<clap_event_param_value>(e))
        {
            std::cout << e->time << "\tPAR VALUE  " << pe->param_id << " " << pe->value << "\n";
        }
        else
            std::cout << e->time << "\tUNHANDLED FOR PRINTING\n";
    }
}

inline void testNewEventList()
{
    xenakios::ClapEventList elist;
    clap_process process;
    auto en = makeClapNote(500, CLAP_EVENT_NOTE_ON, 0, 0, 60, -1, 1.0);
    elist.tryPushAs(&en);
    en = makeClapNote(0, CLAP_EVENT_NOTE_ON, 0, 0, 67, -1, 1.0);
    elist.tryPushAs(&en);
    en = makeClapNote(400, CLAP_EVENT_NOTE_OFF, 0, 0, 67, -1, 1.0);
    elist.tryPushAs(&en);
    en = makeClapNote(100, CLAP_EVENT_NOTE_ON, 0, 0, 48, -1, 1.0);
    elist.tryPushAs(&en);
    elist.getLastAs<clap_event_note>()->key = 49;
    auto parev = makeClapParameterValueEvent(333, 42, 0.666, nullptr);

    elist.tryPushAs(&parev);
    clap_event_midi midiev;
    midiev.header.type = CLAP_EVENT_MIDI;
    midiev.header.size = sizeof(CLAP_EVENT_MIDI);
    midiev.header.time = 3;
    elist.tryPushAs(&midiev);
    printXList(elist);
    elist.sort();
    std::cout << "sorted\n";
    printXList(elist);
}

void test_np_code()
{
    std::shared_ptr<NoisePlethoraPlugin> plug;
    std::string plugToCreate = "satanWorkout";
    std::unordered_map<int, std::string> availablePlugins;
    int k = 0;
    for (int i = 0; i < numBanks; ++i)
    {
        auto &bank = getBankForIndex(i);
        std::cout << "bank " << i << "\n";
        for (int j = 0; j < programsPerBank; ++j)
        {
            std::cout << "\t" << bank.getProgramName(j) << "\n";
            availablePlugins[k] = bank.getProgramName(j);
            ++k;
        }
    }
    plug = MyFactory::Instance()->Create(availablePlugins[0]);
    if (!plug)
    {
        std::cout << "could not create plugin\n";
        return;
    }

    double sr = 44100;
    StereoSimperSVF filter;
    filter.init();
    StereoSimperSVF dcblocker;
    dcblocker.setCoeff(0.0, 0.01, 1.0 / sr);
    dcblocker.init();
    int outlen = sr * 10;
    juce::File outfile(
        R"(C:\develop\AudioPluginHost_mk2\Source\Experimental\audio\noise_plethora_out_02.wav)");
    outfile.deleteFile();
    auto ostream = outfile.createOutputStream();
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(ostream.release(), sr, 2, 32, {}, 0);
    juce::AudioBuffer<float> buf(2, outlen);
    buf.clear();

    plug->init();
    plug->m_sr = sr;
    std::minstd_rand0 rng;
    std::uniform_real_distribution<float> whitenoise{-1.0f, 1.0f};
    for (int i = 0; i < outlen; ++i)
    {
        float p0 = 0.5 + 0.5 * std::sin(2 * 3.141592653 / sr * i * 0.3);
        float p1 = 0.5 + 0.5 * std::sin(2 * 3.141592653 / sr * i * 0.4);
        float fcutoff = 84.0 + 12.0 * std::sin(2 * 3.141592653 / sr * i * 4.0);
        filter.setCoeff(fcutoff, 0.7, 1.0 / sr);
        plug->process(p0, p1);
        float outL = plug->processGraph();
        float outR = outL;
        dcblocker.step<StereoSimperSVF::HP>(dcblocker, outL, outR);
        filter.step<StereoSimperSVF::LP>(filter, outL, outR);
        buf.setSample(0, i, outL);
        buf.setSample(1, i, outR);
    }
    writer->writeFromAudioSampleBuffer(buf, 0, outlen);
    delete writer;
}

template <typename ContType> inline void test_keyvaluemap(int iters, std::string benchname)
{
    ContType pinfos;
    std::minstd_rand0 rng;
    std::uniform_real_distribution<double> dist{-1.0, 1.0};
    std::vector<clap_id> valid_ids;
    int paridcount = 1000;
    for (int i = 0; i < paridcount; ++i)
    {
        clap_param_info pinfo;
        pinfo.id = std::hash<int>()(i);
        pinfo.default_value = dist(rng);
        pinfos[pinfo.id] = pinfo;
        valid_ids.push_back(pinfo.id);
    }
    std::uniform_int_distribution<int> disti{0, paridcount - 1};
    std::vector<clap_id> ids_to_look_for;
    for (int i = 0; i < iters; ++i)
        ids_to_look_for.push_back(valid_ids[disti(rng)]);
    double t0 = juce::Time::getMillisecondCounterHiRes();
    double accum = 0.0;
    for (int i = 0; i < iters; ++i)
    {
        auto id = ids_to_look_for[i];
        auto &parinfo = pinfos[id];
        accum += parinfo.default_value;
    }
    double t1 = juce::Time::getMillisecondCounterHiRes();
    std::cout << benchname << " took " << (t1 - t0) / 1000.0 << " seconds\n";
    std::cout << accum << "\n";
}

int main()
{
    // test_polym();
    // test_keyvaluemap<KeyValueTable<clap_id, clap_param_info>>(1000000, "custom kvmap");
    // test_keyvaluemap<std::unordered_map<clap_id, clap_param_info>>(1000000,
    // "std::unordered_map"); test_np_code();
    //  testNewEventList();
    return 0;
    juce::ScopedJuceInitialiser_GUI gui_init;
    // test_node_connecting();
    test_graph_processor_offline();
    return 0;
    clap::helpers::EventList list;
    auto ev = makeClapParameterValueEvent(1, 666, 0.987);
    list.push((const clap_event_header *)&ev);
    auto mev = makeClapParameterModEvent(46, 78, 0.444);
    list.push((const clap_event_header *)&mev);
    printClapEvents(list);
    return 0;
}
#endif
