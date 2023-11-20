#include <iostream>
#include "testprocessors.h"
#include <memory>
#include <vector>

#include <clap/helpers/event-list.hh>
#include "clap_xaudioprocessor.h"
#include "juce_xaudioprocessor.h"
#include "fileplayer_xaudioprocessor.h"
#include "xap_utils.h"
#include "xaps/xap_notegenerator.h"
#include "xap_modulator.h"
#include "xclapeventlist.h"
#include "../Plugins/noise-plethora/plugins/NoisePlethoraPlugin.hpp"
#include "../Plugins/noise-plethora/plugins/Banks.hpp"
#include "xaudiograph.h"
#include "xapdsp.h"
#include <sst/jucegui/components/Knob.h>
#include <sst/jucegui/components/HSlider.h>
#include "text/choc_JSON.h"
#include "text/choc_Files.h"
#include "memory/choc_Base64.h"
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
    struct CTMessage
    {
        enum class OPCode
        {
            Nop,
            Shutdown,
            SwitchXAP,
            DeleteXAP
        };
        OPCode op = OPCode::Nop;
        xenakios::XAudioProcessor *proc = nullptr; // switch to, send back to deletion
    };
    choc::fifo::SingleReaderSingleWriterFIFO<CTMessage> from_ui_fifo;
    choc::fifo::SingleReaderSingleWriterFIFO<CTMessage> to_ui_fifo;
    XAPPlayer(xenakios::XAudioProcessor *procToPlay, int subblocksize = 64)
        : m_proc(procToPlay), m_subblocksize(subblocksize)
    {
        // 128 should be enough, we have some problem already if the fifos need more
        from_ui_fifo.reset(128);
        to_ui_fifo.reset(128);
    }
    std::atomic<bool> shutdownReady{false};
    void handleFromUIMessages()
    {
        CTMessage msg;
        while (from_ui_fifo.pop(msg))
        {
            if (msg.op == CTMessage::OPCode::Shutdown)
            {
                if (m_proc)
                {
                    m_proc->stopProcessing();
                    m_proc = nullptr;
                    shutdownReady = true;
                }
            }
            if (msg.op == CTMessage::OPCode::SwitchXAP && msg.proc)
            {
                if (m_proc)
                {
                    m_proc->stopProcessing();
                }
                CTMessage toui;
                toui.op = CTMessage::OPCode::DeleteXAP;
                toui.proc = m_proc;
                m_proc = msg.proc;
                to_ui_fifo.push(toui);
            }
        }
    }
    void audioDeviceIOCallbackWithContext(const float *const *inputChannelData,
                                          int numInputChannels, float *const *outputChannelData,
                                          int numOutputChannels, int numSamples,
                                          const AudioIODeviceCallbackContext &context) override
    {
        jassert(m_output_ring_buf.size() >= numSamples * numOutputChannels);
        handleFromUIMessages();
        if (!m_proc)
        {
            for (int i = 0; i < numOutputChannels; ++i)
            {
                for (int j = 0; j < numSamples; ++j)
                {
                    outputChannelData[i][j] = 0.0f;
                }
            }
            return;
        }
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
            auto err = m_proc->process(&process);
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
    double curSampleRate = 0.0;
    int curBufferSize = 0;
    void audioDeviceAboutToStart(AudioIODevice *device) override
    {
        curSampleRate = device->getCurrentSampleRate();
        curBufferSize = device->getCurrentBufferSizeSamples();
        // m_proc->activate(device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples(),
        //                  device->getCurrentBufferSizeSamples());
    }
    void audioDeviceStopped() override {}

  private:
    xenakios::XAudioProcessor *m_proc = nullptr;
    SimpleRingBuffer<float, 2048> m_input_ring_buf;
    SimpleRingBuffer<float, 2048> m_output_ring_buf;
    int m_subblocksize = 0;
};

inline void test_graph_processor_realtime()
{
    auto g = std::make_unique<XAPGraph>();
    juce::AudioDeviceManager man;
    man.initialiseWithDefaultDevices(0, 2);
    XAPPlayer xplayer(g.get());
    // addAudioCallback(&xplayer);
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

class NodeGraphComponent : public juce::Component, public juce::Timer
{
  public:
    NodeGraphComponent(XAPGraph *g) : m_graph(g)
    {
        startTimer(100);
        refreshPins();
    }
    void timerCallback() override
    {
        XAPGraph::CTMessage msg;
        while (m_graph->to_ui_fifo.pop(msg))
        {
            if (msg.op == XAPGraph::CTMessage::Opcode::DeleteNode)
            {
                msg.node->OnRequestGUIClose(msg.node);
                delete msg.node;
                if (m_dragging_node == msg.node)
                {
                    m_dragging_node = nullptr;
                }
            }
        }
        repaint();
    }
    void paint(juce::Graphics &g) override
    {
        g.fillAll(juce::Colours::black);
        juce::Path connpath;
        connpath.preallocateSpace(16);
        for (auto &n : m_graph->proc_nodes)
        {
            auto nodebounds = n->nodeSceneBounds;
            g.setColour(juce::Colours::darkgrey);
            g.fillRect(nodebounds);
            if (n->isActiveInGraph)
            {
                g.setColour(juce::Colours::darkorange);
                g.fillRect(nodebounds.getX(), nodebounds.getY(), 10, 10);
            }
            g.setColour(juce::Colours::white);
            g.drawText(n->processorName, nodebounds, juce::Justification::centred);
            g.setColour(juce::Colours::green);
            auto proc = n->processor.get();
            int audioinportcount = proc->audioPortsCount(true);
            int noteinportcount = proc->notePortsCount(true);
            int totalinports = audioinportcount + noteinportcount + 1;
            /*
            for (int i = 0; i < audioinportcount; ++i)
            {
                clap_audio_port_info pinfo;
                if (proc->audioPortsInfo(i, true, &pinfo))
                {
                    for (int j = 0; j < pinfo.channel_count; ++j)
                    {
                        auto pt = getNodePinPosition(n.get(), 0, true, i, j);
                        g.fillEllipse(pt.x, pt.y - 5.0f, 10.0, 10.0f);
                    }
                }
            }
            g.setColour(juce::Colours::red);
            for (int i=0;i<noteinportcount;++i)
            {
                clap_note_port_info pinfo;
                if (proc->notePortsInfo(i,true,&pinfo))
                {
                    auto pt = getNodePinPosition(n.get(), 1, true, i, 0);
                    g.fillEllipse(pt.x, pt.y - 5.0f, 10.0, 10.0f);
                }
            }
            */
            for (int i = 0; i < n->inputPins.size(); ++i)
            {
                float xpos = getPinXPos(n.get(), i, true);
                if (n->inputPins[i].type == XAPNode::ConnectionType::Audio)
                    g.setColour(juce::Colours::green);
                else if (n->inputPins[i].type == XAPNode::ConnectionType::Events)
                    g.setColour(juce::Colours::yellow);
                else
                    g.setColour(juce::Colours::red);
                g.fillEllipse(xpos, nodebounds.getY() - 5.0f, 10.0, 10.0f);
            }

            for (int i = 0; i < n->outputPins.size(); ++i)
            {
                float xpos = getPinXPos(n.get(), i, false);
                if (n->outputPins[i].type == XAPNode::ConnectionType::Audio)
                    g.setColour(juce::Colours::green);
                else if (n->outputPins[i].type == XAPNode::ConnectionType::Events)
                    g.setColour(juce::Colours::yellow);
                else
                    g.setColour(juce::Colours::red);
                g.fillEllipse(xpos, nodebounds.getBottom() - 5.0f, 10.0, 10.0f);
            }

            int audiooutportcount = proc->audioPortsCount(false);
            int noteoutportcount = proc->notePortsCount(false);
            int totaloutports = audiooutportcount + noteoutportcount + 1;
            g.setColour(juce::Colours::green);
            /*
            for (int i = 0; i < audiooutportcount; ++i)
            {
                clap_audio_port_info pinfo;
                if (proc->audioPortsInfo(i, false, &pinfo))
                {
                    for (int j = 0; j < pinfo.channel_count; ++j)
                    {
                        auto pt = getNodePinPosition(n.get(), 0, false, i, j);
                        g.fillEllipse(pt.x, pt.y - 5.0f, 10.0, 10.0f);
                    }
                }
            }
            */
            g.setColour(juce::Colours::white);

            for (auto &conn : n->inputConnections)
            {
                connpath.clear();
                int type = (int)conn.type;
                int destIndex = findPinIndex(n.get(), conn.type, true, conn.destinationPort,
                                             conn.destinationChannel);
                // jassert(destIndex >= 0);
                float x0 = getPinXPos(n.get(), destIndex, true);
                int sourceIndex = findPinIndex(conn.source, conn.type, false, conn.sourcePort,
                                               conn.sourceChannel);
                // jassert(sourceIndex >= 0);
                float x1 = getPinXPos(conn.source, sourceIndex, false);
                float y0 = n->nodeSceneBounds.getY();
                float y1 = conn.source->nodeSceneBounds.getBottom();
                g.drawArrow(juce::Line<float>(x1 + 5.0f, y1, x0 + 5.0f, y0), 2.0f, 10.0f, 10.0f);
            }
        }
        g.setColour(juce::Colours::white);
        g.drawText(m_debug_text, 0, 0, getWidth(), 20, juce::Justification::centredLeft);
    }
    juce::Point<float> getNodePinPosition(XAPNode *node, int type, bool isInput, int port,
                                          int channel)
    {
        if (!node)
            return {};
        auto nbounds = node->nodeSceneBounds;
        auto proc = node->processor.get();
        double availw = (double)nbounds.getWidth() / 3;
        if (type == 0) // audio
        {
            if (isInput)
            {
                clap_audio_port_info pinfo;
                if (proc->audioPortsInfo(port, true, &pinfo))
                {
                    float x = availw / pinfo.channel_count * channel;
                    return {nbounds.getX() + x, (float)nbounds.getY()};
                }
            }
            else
            {
                clap_audio_port_info pinfo;
                if (proc->audioPortsInfo(port, false, &pinfo))
                {
                    float x = availw / pinfo.channel_count * channel;
                    return {nbounds.getX() + x, (float)nbounds.getBottom()};
                }
            }
        }
        if (type == 1) // notes
        {
            clap_note_port_info pinfo;
            if (isInput)
            {
                if (proc->notePortsInfo(port, true, &pinfo))
                {
                    float x = availw + availw / 2.0;
                    return {nbounds.getX() + x, (float)nbounds.getY()};
                }
            }
            else
            {
                if (proc->notePortsInfo(port, false, &pinfo))
                {
                    float x = availw / 2.0;
                    return {nbounds.getX() + x, (float)nbounds.getBottom()};
                }
            }
        }
        return {};
    }
    void showNodeContextMenu(XAPNode *n)
    {
        juce::PopupMenu menu;
        menu.addItem("Remove", [n, this]() {
            XAPGraph::CTMessage msg;
            msg.op = XAPGraph::CTMessage::Opcode::RemoveNode;
            msg.node = n;
            m_graph->from_ui_fifo.push(msg);
        });
        menu.addItem("Remove all input connections", [this, n] {
            XAPGraph::CTMessage msg;
            msg.op = XAPGraph::CTMessage::Opcode::RemoveNodeInputs;
            msg.node = n;
            m_graph->from_ui_fifo.push(msg);
        });
        juce::PopupMenu removeinconnmenu;
        for (size_t i = 0; i < n->inputConnections.size(); ++i)
        {
            auto &conn = n->inputConnections[i];
            juce::String txt = juce::String(conn.source->displayName) + " port " +
                               juce::String(conn.sourcePort) + " channel " +
                               juce::String(conn.sourceChannel);
            removeinconnmenu.addItem(txt, [this, n, i]() {
                XAPGraph::CTMessage msg;
                msg.connectionIndex = i;
                msg.node = n;
                msg.op = XAPGraph::CTMessage::Opcode::RemoveNodeInput;
                m_graph->from_ui_fifo.push(msg);
            });
        }
        menu.addSubMenu("Remove input", removeinconnmenu);
        menu.showMenuAsync(juce::PopupMenu::Options());
    }
    void mouseDown(const juce::MouseEvent &ev) override
    {
        m_debug_text = "";
        m_dragging_node = findFromPosition(ev.x, ev.y);
        if (ev.mods.isRightButtonDown())
        {
            if (m_dragging_node)
                showNodeContextMenu(m_dragging_node);
            return;
        }

        if (m_dragging_node)
        {
            m_drag_start_bounds = m_dragging_node->nodeSceneBounds;
            return;
        }
        auto pin = findPinFromPosition(ev.x, ev.y);
        m_drag_connection = XAPNode::Connection();
        if (pin)
        {
            if (pin->isInput)
            {
                m_drag_connection.destination = pin->node;
                m_drag_connection.destinationChannel = pin->channel;
                m_drag_connection.destinationPort = pin->port;
                m_drag_connection.type = pin->type;
            }
            else
            {
                m_drag_connection.source = pin->node;
                m_drag_connection.sourceChannel = pin->channel;
                m_drag_connection.sourcePort = pin->port;
                m_drag_connection.type = pin->type;
            }
        }
    }
    juce::String m_debug_text;
    void mouseDrag(const juce::MouseEvent &ev) override
    {
        if (m_dragging_node)
        {
            auto newbounds = m_drag_start_bounds.translated(ev.getDistanceFromDragStartX(),
                                                            ev.getDistanceFromDragStartY());
            m_dragging_node->nodeSceneBounds = newbounds;
            refreshPins();
            repaint();
            return;
        }
        if (m_drag_connection.source || m_drag_connection.destination)
        {
            if (m_drag_connection.source)
                m_debug_text =
                    "Dragging new connection from " + m_drag_connection.source->displayName;
            else
                m_debug_text =
                    "Dragging new connection from " + m_drag_connection.destination->displayName;
            auto pin = findPinFromPosition(ev.x, ev.y);
            if (pin)
            {
                if (pin->isInput && m_drag_connection.source && pin->type == m_drag_connection.type)
                {
                    m_debug_text = "Would connect " + m_drag_connection.source->displayName +
                                   " to " + pin->node->displayName;
                }
                if (!pin->isInput && m_drag_connection.destination &&
                    pin->type == m_drag_connection.type)
                {
                    m_debug_text = "Would connect " + pin->node->displayName + " to " +
                                   m_drag_connection.source->displayName;
                }
            }
            repaint();
        }
    }
    void mouseUp(const juce::MouseEvent &ev) override
    {
        m_dragging_node = nullptr;
        m_drag_start_bounds = {};
        m_drag_connection = XAPNode::Connection();
        m_debug_text = "";
        repaint();
    }
    void mouseMove(const juce::MouseEvent &ev) override
    {
        auto pin = findPinFromPosition(ev.x, ev.y);
        if (pin)
            setMouseCursor(juce::MouseCursor::CrosshairCursor);
        else
            setMouseCursor(juce::MouseCursor::NormalCursor);
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
    float getPinXPos(XAPNode *node, int index, bool isInput)
    {
        auto nbounds = node->nodeSceneBounds;
        float xpos = nbounds.getCentreX();
        if (isInput && node->inputPins.size() > 1)
        {
            xpos = juce::jmap<float>(index, 0, node->inputPins.size() - 1, nbounds.getX() + 5,
                                     nbounds.getRight() - 10);
        }
        if (!isInput && node->outputPins.size() > 1)
        {
            xpos = juce::jmap<float>(index, 0, node->outputPins.size() - 1, nbounds.getX() + 5,
                                     nbounds.getRight() - 10);
        }
        return xpos;
    }
    XAPNode::Pin *findPinFromPosition(int x, int y)
    {
        for (auto &n : m_graph->proc_nodes)
        {
            auto nbounds = n->nodeSceneBounds;
            for (int i = 0; i < n->inputPins.size(); ++i)
            {
                float xpos = getPinXPos(n.get(), i, true);
                juce::Rectangle<float> rect{xpos - 10, nbounds.getY() - 10.0f, 20.0f, 20.0f};
                if (rect.contains(x, y))
                {
                    return &n->inputPins[i];
                }
            }
            for (int i = 0; i < n->outputPins.size(); ++i)
            {
                float xpos = getPinXPos(n.get(), i, false);
                juce::Rectangle<float> rect(xpos - 10, nbounds.getBottom() - 10, 20, 20);
                if (rect.contains(x, y))
                {
                    return &n->outputPins[i];
                }
            }
        }
        return nullptr;
    }
    void refreshPins()
    {
        return;
        m_pins.clear();
        for (auto &n : m_graph->proc_nodes)
        {
            auto nbounds = n->nodeSceneBounds;
            PinUIProperties pin;
            pin.node = n.get();
            int total_in_ports_count = 0;
            int audio_in_ports_count = n->processor->audioPortsCount(true);
            int note_in_ports_count = n->processor->notePortsCount(true);
            total_in_ports_count = audio_in_ports_count + note_in_ports_count + 1;
            if (total_in_ports_count > 0)
            {
                double availw = nbounds.getWidth() / total_in_ports_count;
                for (int i = 0; i < audio_in_ports_count; ++i)
                {
                    clap_audio_port_info pinfo;
                    if (n->processor->audioPortsInfo(i, true, &pinfo))
                    {
                        int nchs = pinfo.channel_count;
                        for (int j = 0; j < nchs; ++j)
                        {
                            pin.color = juce::Colours::green;
                            float x = nbounds.getX() + availw * j;
                            float y = nbounds.getY();
                            pin.pos = {x, y};
                        }
                    }
                }
            }
        }
    }

  private:
    XAPGraph *m_graph = nullptr;
    XAPNode *m_dragging_node = nullptr;
    juce::Rectangle<int> m_drag_start_bounds;
    struct PinUIProperties
    {
        PinUIProperties() {}
        juce::Point<float> pos;
        juce::Colour color;
        XAPNode *node = nullptr;
    };
    std::vector<PinUIProperties> m_pins;
    XAPNode::Connection m_drag_connection;
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
    juce::TextEditor m_log_ed;
    juce::TextButton m_plug_test_but;
    std::unique_ptr<NodeGraphComponent> m_graph_component;
    void handleMessagesFromAudioThread()
    {
        XAPPlayer::CTMessage msg;
        while (m_player->to_ui_fifo.pop(msg))
        {
            if (msg.op == XAPPlayer::CTMessage::OPCode::DeleteXAP)
            {
                DBG("audio thread sent processor to delete " << (uint64_t)msg.proc);
                if (msg.proc && msg.proc != m_graph.get())
                {
                    delete msg.proc;
                }
                if (msg.proc == m_graph.get())
                {
                    DBG("player asked to delete current processor!");
                    jassert(false);
                }
            }
        }
    }
    void timerCallback() override
    {
        handleMessagesFromAudioThread();
        int usage = m_aman.getCpuUsage() * 100.0;
        juce::String txt;
        if (m_graph && m_graph->m_last_touched_node)
        {
            auto lasttouchedparam = m_graph->m_last_touched_param;
            auto lasttouchedproc = m_graph->m_last_touched_node;
            auto lasttouchedparamname =
                juce::String(lasttouchedproc->parameterInfos[lasttouchedparam].name);
            txt += juce::String(lasttouchedproc->processorName) + " ";
            txt +=
                lasttouchedparamname + " " + juce::String(m_graph->m_last_touched_param_value, 2);
        }
        m_infolabel.setText("CPU " + juce::String(usage) + "% " + txt, juce::dontSendNotification);
    }
    std::string pathprefix = R"(C:\Program Files\Common Files\)";
    void addNoteGeneratorTestNodes()
    {
        auto logfunc = [this](clap_log_severity sev, const char *msg) {
            DBG("Clap plugin logged at level " << sev << " : " << msg);
        };
        auto notegenid = m_graph->addProcessorAsNode(
            std::make_unique<ClapEventSequencerProcessor>(2), "Note Gen");
        auto surgeid = m_graph->addProcessorAsNode(
            std::make_unique<ClapPluginFormatProcessor>(
                pathprefix + R"(CLAP\Surge Synth Team\Surge XT.clap)", 0, logfunc),
            "Surge XT 1");
        auto vintageverbid = m_graph->addProcessorAsNode(
            std::make_unique<JucePluginWrapper>(
                R"(C:\Program Files\Common Files\VST3\ValhallaVintageVerb.vst3)"),
            "Valhalla");
        auto vdelayid = m_graph->addProcessorAsNode(
            std::make_unique<JucePluginWrapper>(
                R"(C:\\Program Files\\Common Files\\VST3\\ValhallaDelay.vst3)"),
            "Delay");
        // m_graph->addProcessorAsNode(
        //     std::make_unique<ClapPluginFormatProcessor>(pathprefix + "/CLAP/Conduit.clap", 0),
        //     "Surge XT 1");

        // m_graph->addProcessorAsNode(std::make_unique<ClapPluginFormatProcessor>(
        //                                pathprefix + R"(CLAP\Surge Synth Team\Surge XT.clap)", 0),
        //                            "Surge XT 2");
        auto volumeid = m_graph->addProcessorAsNode(std::make_unique<XAPGain>(), "Main");
        m_graph->makeConnection(XAPNode::ConnectionType::Events, notegenid, 0, 0, surgeid, 0, 0);
        // connectEventPorts(m_graph->findNodeByName("Note Gen"), 0,
        //                   m_graph->findNodeByName("Surge XT 1"), 0);
        //  connectEventPorts(m_graph->findNodeByName("Note Gen"), 1,
        //                   m_graph->findNodeByName("Surge XT 2"), 0);
        m_graph->makeConnection(XAPNode::ConnectionType::Audio, surgeid, 0, 0, volumeid, 0, 0);
        m_graph->makeConnection(XAPNode::ConnectionType::Audio, surgeid, 0, 1, volumeid, 0, 1);
        // m_graph->connectAudio("Surge XT 1", 0, 0, "Main", 0, 0);
        // m_graph->connectAudio("Surge XT 1", 0, 1, "Main", 0, 1);
        m_graph->connectAudio("Surge XT 1", 1, 0, "Valhalla", 0, 0);
        m_graph->connectAudio("Surge XT 1", 1, 1, "Valhalla", 0, 1);
        m_graph->connectAudio("Surge XT 1", 2, 0, "Delay", 0, 0);
        m_graph->connectAudio("Surge XT 1", 2, 1, "Delay", 0, 1);
        m_graph->connectAudio("Valhalla", 0, 0, "Main", 0, 0);
        m_graph->connectAudio("Valhalla", 0, 1, "Main", 0, 1);
        m_graph->connectAudio("Delay", 0, 0, "Main", 0, 0);
        m_graph->connectAudio("Delay", 0, 1, "Main", 0, 1);
        // m_graph->connectAudio("Surge XT 2", 0, 0, "Main", 0, 0);
        // m_graph->connectAudio("Surge XT 2", 0, 1, "Main", 0, 1);
        m_graph->outputNodeId = "Main";
    }
    void addFilePlayerTestNodes()
    {
        m_graph->addProcessorAsNode(std::make_unique<FilePlayerProcessor>(), "File 1");
        auto fp = m_graph->findProcessorAndCast<FilePlayerProcessor>("File 1");
        m_graph->addProcessorAsNode(std::make_unique<XAPGain>(), "Main");
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
        m_graph->addProcessorAsNode(std::make_unique<XAPGain>(), "Main");
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
        xenakios::XapFactory::getInstance().scanClapPlugins();
        xenakios::XapFactory::getInstance().scanVST3Plugins();
        addAndMakeVisible(m_log_ed);
        addAndMakeVisible(m_infolabel);
        addAndMakeVisible(m_mod_rout_combo);
        addAndMakeVisible(m_plug_test_but);
        m_plug_test_but.onClick = [this]() { showNodeAddMenu(); };
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
        // addNoteGeneratorTestNodes();
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

        m_player = std::make_unique<XAPPlayer>(nullptr);
        m_aman.initialiseWithDefaultDevices(0, 2);

        m_graph_component = std::make_unique<NodeGraphComponent>(m_graph.get());
        addAndMakeVisible(m_graph_component.get());
        setSize(1000, 600);
        juce::MessageManager::callAsync([this]() {
            loadState(juce::File());
            m_graph_component->repaint();
            m_aman.addAudioCallback(m_player.get());
            jassert(m_player->curBufferSize > 0 && m_player->curSampleRate > 0.0);
            m_graph->activate(m_player->curSampleRate, m_player->curBufferSize,
                              m_player->curBufferSize);
            XAPPlayer::CTMessage msg;
            msg.op = XAPPlayer::CTMessage::OPCode::SwitchXAP;
            msg.proc = m_graph.get();
            m_player->from_ui_fifo.push(msg);
        });
    }
    void saveState(juce::File file)
    {
        return;
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
            nodestate.addMember("idx", static_cast<int64_t>(n->ID));
            clap_plugin_descriptor desc;
            if (n->processor->getDescriptor(&desc))
            {
                if (desc.id)
                    nodestate.addMember("procid", desc.id);
            }

            /*
            auto procstate = XAPGraph::getProcessorState(n->processor.get());
            if (procstate.size() > 0)
            {
                DBG(n->processorName << " returned " << procstate.size() << " bytes of state");
                auto b64state = choc::base64::encodeToString(procstate);
                DBG(b64state.size() << " bytes as base64");
                nodestate.addMember("procstatebase64", b64state);
            }
            else
            {
                DBG(n->processorName << " did not return state");
            }
            */
            juce::Rectangle<int> bounds;
            bool plugvisible = false;
            for (auto &w : m_xap_windows)
            {
                if (&w->m_proc == n->processor.get())
                {
                    bounds = w->getScreenBounds();
                    plugvisible = w->isVisible();
                    break;
                }
            }
            nodestate.addMember("plugwinvisible", plugvisible);
            if (!bounds.isEmpty())
            {
                nodestate.addMember("plugwinbounds", bounds.toString().toStdString());
            }
            nodestate.addMember("nodebounds", n->nodeSceneBounds.toString().toStdString());
            for (auto &conn : n->inputConnections)
            {
                auto jconn = choc::value::createObject("");
                jconn.addMember("type", static_cast<int>(conn.type));
                jconn.addMember("src", static_cast<int64_t>(conn.source->ID));
                jconn.addMember("src_port", conn.sourcePort);
                jconn.addMember("src_chan", conn.sourceChannel);
                jconn.addMember("dest", static_cast<int64_t>(conn.destination->ID));
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
        m_graph->outputNodeId = "Main";
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

            auto jnodesarr = jroot["nodestates"];
            auto sz = jnodesarr.size();
            std::optional<uint64_t> sink_node_id;
            if (sz > 0)
            {

                for (uint32 i = 0; i < sz; ++i)
                {
                    auto jnodestate = jnodesarr[i];
                    auto jid = jnodestate["id"].toString();
                    auto jidx = jnodestate["idx"].getInt64();
                    auto is_sink = jnodestate["is_sink"].getWithDefault<bool>(false);
                    if (is_sink)
                    {
                        // there should only be one sink node!
                        // we probably should figure out something else for determining the sink
                        // node, like have a node type that is going to be the final audio output
                        jassert(!sink_node_id);
                        sink_node_id = jidx;
                        m_graph->outputNodeId = jid;
                    }

                    auto jprocid = jnodestate["procid"].toString();
                    auto uproc = xenakios::XapFactory::getInstance().createFromID(jprocid);
                    if (uproc)
                    {
                        auto proc = uproc.get();
                        m_graph->addProcessorAsNode(std::move(uproc), jid, jidx);
                        DBG("Added " << m_graph->proc_nodes.back()->displayName
                                     << " to graph from state");
                        m_graph->proc_nodes.back()->OnRequestGUIClose = [this](XAPNode *dn) {
                            for (size_t i = 0; i < m_xap_windows.size(); ++i)
                            {
                                if (&m_xap_windows[i]->m_proc == dn->processor.get())
                                {
                                    m_xap_windows.erase(m_xap_windows.begin() + i);
                                    break;
                                }
                            }
                        };
                        bool plugvis = jnodestate["plugwinvisible"].getBool();
                        if (plugvis)
                        {
                            m_xap_windows.emplace_back(std::make_unique<XapWindow>(*proc));
                            m_xap_windows.back()->nodeID = m_graph->proc_nodes.back()->displayName;
                            auto jbounds = jnodestate["plugwinbounds"].toString();
                            auto bounds = juce::Rectangle<int>::fromString(jbounds);
                            if (!bounds.isEmpty())
                            {
                                m_xap_windows.back()->setBounds(bounds);
                            }
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
                    }
                }
                jassert(sink_node_id);
                std::unordered_map<uint64_t, XAPNode *> nodemap;
                for (auto &e : m_graph->proc_nodes)
                {
                    nodemap[e->ID] = e.get();
                }
                auto jconns = jroot["nodeconnections"];
                for (int i = 0; i < jconns.size(); ++i)
                {
                    auto jconn = jconns[i];
                    auto ctype = (XAPNode::ConnectionType)jconn["type"].getInt64();
                    uint64_t sourceid = jconn["src"].getInt64();
                    int sourceport = jconn["src_port"].getInt64();
                    int sourcechannel = jconn["src_chan"].getInt64();
                    uint64_t destid = jconn["dest"].getInt64();
                    int destport = jconn["dest_port"].getInt64();
                    int destchannel = jconn["dest_chan"].getInt64();
                    m_graph->makeConnection(ctype, sourceid, sourceport, sourcechannel, destid,
                                            destport, destchannel);
                }
                for (uint32 i = 0; i < sz; ++i)
                {
                    auto jnodestate = jnodesarr[i];
                    auto jid = jnodestate["id"];
                    uint64_t jidx = jnodestate["idx"].getInt64();
                    auto jnodebounds = jnodestate["nodebounds"];
                    auto nodebounds = juce::Rectangle<int>::fromString(jnodebounds.toString());
                    if (!nodebounds.isEmpty())
                    {
                        // DBG(jid.toString() << " has node scene bound " << nodebounds.toString());
                        nodemap[jidx]->nodeSceneBounds = nodebounds;
                    }
                    auto b64state = jnodestate["procstatebase64"].toString();
                    if (b64state.size() > 0)
                    {
                        std::vector<unsigned char> vec;
                        if (choc::base64::decodeToContainer(vec, b64state))
                        {
                            auto *proc = nodemap[jidx]->processor.get();
                            if (!m_graph->setProcessorState(proc, vec))
                            {
                                DBG("could not set state");
                            }
                        }
                        else
                            DBG("could not decode base64");
                    }
                    else
                    {
                        // DBG("no base64 string");
                    }
                }
            }
        }
        catch (std::exception &excep)
        {
            std::cout << excep.what() << "\n";
        }
    }
    void createAndAddNode(std::string procid)
    {
        DBG("Should create : " << procid);
        auto newproc = xenakios::XapFactory::getInstance().createFromID(procid);
        clap_plugin_descriptor desc;
        if (newproc && newproc->getDescriptor(&desc))
        {
            DBG("Created : " << desc.name << " " << desc.id);
        }
    }
    void showNodeAddMenu()
    {
        juce::PopupMenu menu;
        std::map<std::string, juce::PopupMenu> menumap;
        for (auto &e : xenakios::XapFactory::getInstance().m_entries)
        {
            if (menumap.count(e.manufacturer) == 0)
            {
                menumap[e.manufacturer] = juce::PopupMenu();
            }
            menumap[e.manufacturer].addItem(e.proctype + " : " + e.name,
                                            [this, pid = e.procid]() { createAndAddNode(pid); });
        }

        for (auto &m : menumap)
        {
            menu.addSubMenu(m.first, m.second);
        }

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
        XAPPlayer::CTMessage msg;
        msg.op = XAPPlayer::CTMessage::OPCode::Shutdown;
        m_player->from_ui_fifo.push(msg);
        // need to wait that the processors have stopped, which has
        // to be done for Claps in the audio thread
        int sleepcount = 0;
        while (!m_player->shutdownReady.load())
        {
            juce::Thread::sleep(10);
            ++sleepcount;
            if (sleepcount > 500) // arbitrary but let's not hang when quitting
            {
                DBG("Shutting down took too long, breaking");
                break;
            }
        }
        // now ready to shutdown audio playback
        m_aman.removeAudioCallback(m_player.get());
    }
    void resized() override
    {
        // m_node_list->setBounds(0, 0, 300, getHeight() - 25);
        m_plug_test_but.setButtonText("Add...");
        m_plug_test_but.setBounds(0, 0, 100, 19);
        m_log_ed.setBounds(0, 20, 300, getHeight() - 45);
        m_graph_component->setBounds(m_log_ed.getRight() + 1, 0, getWidth() - 298,
                                     getHeight() - 25);
        m_infolabel.setBounds(0, m_log_ed.getBottom(), getWidth(), 25);
    }
};

class GuiAppApplication : public juce::JUCEApplication
{
  public:
    //==============================================================================
    GuiAppApplication() {}

    const juce::String getApplicationName() override { return "XAPHost"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
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
