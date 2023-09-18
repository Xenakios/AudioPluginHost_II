#include <iostream>
#include "testprocessors.h"
#include <memory>
#include <vector>

#include <clap/helpers/event-list.hh>
#include "clap_xaudioprocessor.h"
#include "juce_xaudioprocessor.h"

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

class XAPNode
{
  public:
    /*
    Audio : 32 bit floating point audio buffers
    Events : Events that should be passed directly from the source node to the destination,
    typically musical notes
    Modulation : the source node sends Clap parameter modulation messages with values between
    -1 and 1 that are mapped by the graph playback system into suitable parameter mod/value
    messages for the desination node
    */
    enum ConnectionType
    {
        Audio,
        Events,
        Modulation
    };
    struct Connection
    {
        XAPNode *source = nullptr;
        ConnectionType type = ConnectionType::Audio;
        int sourcePort = 0;
        // we could use this for events but we probably want to do stuff like note channel
        // mapping as a separate thing
        int sourceChannel = 0;
        int destinationPort = 0;
        int destinationChannel = 0;
        bool isDestructiveModulation = false;
        clap_id sourceParameter = 0;
        double modulationDepth = 0.0;
        clap_id destinationParameter = 0;
        double modulationValue = 0.0;
    };
    XAPNode(std::unique_ptr<xenakios::XAudioProcessor> nodeIn, std::string name = "")
        : processor(std::move(nodeIn)), displayName(name)
    {
    }
    void initBuffers(int numInChans, int numOutChans, int maxFrames)
    {
        audioBuffer.resize(numInChans * maxFrames + numOutChans * maxFrames);
        inChannels.clear();
        for (int i = 0; i < numInChans; ++i)
        {
            inChannels.push_back(&audioBuffer[i * maxFrames]);
        }
        inPortBuffers[0].channel_count = numInChans;
        inPortBuffers[0].constant_mask = 0;
        inPortBuffers[0].latency = 0;
        inPortBuffers[0].data64 = nullptr;
        inPortBuffers[0].data32 = inChannels.data();

        outChannels.clear();
        for (int i = numInChans; i < numInChans + numOutChans; ++i)
        {
            outChannels.push_back(&audioBuffer[i * maxFrames]);
        }
        outPortBuffers[0].channel_count = numOutChans;
        outPortBuffers[0].constant_mask = 0;
        outPortBuffers[0].latency = 0;
        outPortBuffers[0].data64 = nullptr;
        outPortBuffers[0].data32 = outChannels.data();
    }
    std::unique_ptr<xenakios::XAudioProcessor> processor;
    std::vector<float> audioBuffer;

    std::vector<float *> inChannels;
    clap_audio_buffer inPortBuffers[1];

    std::vector<float *> outChannels;
    clap_audio_buffer outPortBuffers[1];
    clap::helpers::EventList inEvents;
    clap::helpers::EventList outEvents;
    std::vector<Connection> inputConnections;
    std::string displayName;
    std::function<void(XAPNode *)> PreProcessFunc;
};

inline bool connectAudioBetweenNodes(XAPNode *sourceNode, int sourcePort, int sourceChannel,
                                     XAPNode *destinationNode, int destinationPort,
                                     int destinationChannel)
{
    if (sourceNode == nullptr || destinationNode == nullptr)
        return false;
    XAPNode::Connection conn;
    conn.type = XAPNode::ConnectionType::Audio;
    conn.source = sourceNode;
    conn.sourceChannel = sourceChannel;
    conn.sourcePort = sourcePort;
    conn.destinationPort = destinationPort;
    conn.destinationChannel = destinationChannel;
    destinationNode->inputConnections.push_back(conn);
    return true;
}

inline bool connectEventPorts(XAPNode *sourceNode, int sourcePort, XAPNode *destinationNode,
                              int destinationPort)
{
    if (sourceNode == nullptr || destinationNode == nullptr)
        return false;
    XAPNode::Connection conn;
    conn.type = XAPNode::ConnectionType::Events;
    conn.source = sourceNode;
    conn.sourcePort = sourcePort;
    conn.destinationPort = destinationPort;
    destinationNode->inputConnections.push_back(conn);
    return true;
}

inline bool connectModulation(XAPNode *sourceNode, clap_id sourceParamId, XAPNode *destinationNode,
                              clap_id destinationParamId, bool destructive, double initialModDepth)
{
    if (sourceNode == nullptr || destinationNode == nullptr)
        return false;
    XAPNode::Connection conn;
    conn.type = XAPNode::ConnectionType::Modulation;
    conn.source = sourceNode;
    conn.sourcePort = 0;
    conn.destinationPort = 0;
    conn.sourceParameter = sourceParamId;
    conn.destinationParameter = destinationParamId;
    conn.modulationDepth = initialModDepth;
    conn.isDestructiveModulation = destructive;
    destinationNode->inputConnections.push_back(conn);
    return true;
}

inline void dep_resolve_det_circ2(XAPNode *node, std::vector<XAPNode *> &result,
                                  std::unordered_set<XAPNode *> &resolved,
                                  std::unordered_set<XAPNode *> &unresolved)
{
    unresolved.insert(node);
    for (auto &edge : node->inputConnections)
    {
        if (resolved.count(edge.source) == 0)
        // if (is_in(resolved,edge)==false)
        {
            if (unresolved.count(edge.source) > 0)
                // if (is_in(unresolved,edge)==true)
                throw std::invalid_argument("Graph circular dependency detected");
            dep_resolve_det_circ2(edge.source, result, resolved, unresolved);
        }
    }
    resolved.insert(node);
    result.emplace_back(node);
    unresolved.erase(node);
}

inline std::vector<XAPNode *> topoSort(XAPNode *start_node)
{
    std::vector<XAPNode *> result;
    try
    {
        std::unordered_set<XAPNode *> resolved;
        std::unordered_set<XAPNode *> unresolved;
        dep_resolve_det_circ2(start_node, result, resolved, unresolved);
    }
    catch (std::exception &exc)
    {
        std::cout << exc.what() << "\n";
        result.clear();
    }
    return result;
}

inline XAPNode *findByName(std::vector<std::unique_ptr<XAPNode>> &nodes, std::string name)
{
    for (auto &n : nodes)
        if (n->displayName == name)
            return n.get();
    return nullptr;
}

inline void printClapEvents(clap::helpers::EventList &elist)
{
    for (int i = 0; i < elist.size(); ++i)
    {
        auto ev = elist.get(i);
        if (ev->type == CLAP_EVENT_NOTE_ON || ev->type == CLAP_EVENT_NOTE_OFF)
        {
            auto nev = reinterpret_cast<const clap_event_note *>(ev);
            if (ev->type == CLAP_EVENT_NOTE_ON)
                std::cout << nev->header.time << " CLAP NOTE ON " << nev->key << "\n";
            else
                std::cout << nev->header.time << " CLAP NOTE OFF " << nev->key << "\n";
        }
        if (ev->type == CLAP_EVENT_PARAM_VALUE)
        {
            auto pev = reinterpret_cast<const clap_event_param_value *>(ev);
            std::cout << pev->header.time << " CLAP PARAM VALUE " << pev->param_id << " "
                      << pev->value << "\n";
        }
        if (ev->type == CLAP_EVENT_PARAM_MOD)
        {
            auto pev = reinterpret_cast<const clap_event_param_mod *>(ev);
            std::cout << pev->header.time << " CLAP PARAM MOD " << pev->param_id << " "
                      << pev->amount << "\n";
        }
    }
}

inline std::optional<clap_id> findParameterFromName(xenakios::XAudioProcessor *proc,
                                                    std::string parNameToFind)
{
    for (int i = 0; i < proc->paramsCount(); ++i)
    {
        clap_param_info pinfo;
        proc->paramsInfo(i, &pinfo);
        juce::String pname(pinfo.name);
        if (pname.containsIgnoreCase(parNameToFind))
        {
            return pinfo.id;
        }
    }
    return {};
}

inline void handleNodeModulationEvents(XAPNode::Connection &conn,
                                       clap::helpers::EventList &modulationMergeList)
{
    auto &oevents = conn.source->outEvents;
    for (int j = 0; j < oevents.size(); ++j)
    {
        auto ev = oevents.get(j);
        if (ev->type == CLAP_EVENT_PARAM_MOD)
        {
            auto sourcemev = (const clap_event_param_mod *)ev;
            if (conn.sourceParameter == sourcemev->param_id)
            {
                double val = sourcemev->amount;
                if (conn.isDestructiveModulation)
                {
                    val = 0.5 + 0.5 * sourcemev->amount;
                    xenakios::pushParamEvent(modulationMergeList, false, sourcemev->header.time,
                                             conn.destinationParameter, val);
                }
                else
                {
                    val *= conn.modulationDepth;
                    xenakios::pushParamEvent(modulationMergeList, true, sourcemev->header.time,
                                             conn.destinationParameter, val);
                }
            }
        }
    }
}

inline void handleNodeEvents(XAPNode::Connection &conn,
                             std::vector<clap_event_header *> &eventMergeList)
{
    auto &oevents = conn.source->outEvents;
    for (int j = 0; j < oevents.size(); ++j)
    {
        eventMergeList.push_back(oevents.get(j));
    }
}

inline void handleNodeAudioInputs(XAPNode *n, XAPNode::Connection &conn, int procbufsize)
{
    int srcChan = conn.sourceChannel;
    int destChan = conn.destinationChannel;
    for (int j = 0; j < procbufsize; ++j)
    {
        n->inPortBuffers[0].data32[destChan][j] +=
            conn.source->outPortBuffers[0].data32[srcChan][j];
    }
}

class XAPGraph : public xenakios::XAudioProcessor
{
  public:
    XAPGraph() {}
    bool activate(double sampleRate, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override
    {
        std::string pathprefix = R"(C:\Program Files\Common Files\)";
        proc_nodes.clear();
        proc_nodes.emplace_back(std::make_unique<XAPNode>(
            std::make_unique<ClapEventSequencerProcessor>(444, 0.25), "Event Gen 1"));
        proc_nodes.emplace_back(std::make_unique<XAPNode>(
            std::make_unique<ClapEventSequencerProcessor>(2349, 0.5), "Event Gen 2"));
        proc_nodes.emplace_back(
            std::make_unique<XAPNode>(std::make_unique<ClapPluginFormatProcessor>(
                                          pathprefix + R"(CLAP\Surge Synth Team\Surge XT.clap)", 0),
                                      "Surge XT 1"));

        proc_nodes.emplace_back(std::make_unique<XAPNode>(
            std::make_unique<JucePluginWrapper>(pathprefix + R"(VST3\ValhallaVintageVerb.vst3)"),
            "Valhalla"));
        proc_nodes.emplace_back(
            std::make_unique<XAPNode>(std::make_unique<ClapPluginFormatProcessor>(
                                          pathprefix + R"(CLAP\Surge Synth Team\Surge XT.clap)", 0),
                                      "Surge XT 2"));
        proc_nodes.emplace_back(
            std::make_unique<XAPNode>(std::make_unique<ModulatorSource>(1, 0.0), "LFO 1"));
        proc_nodes.emplace_back(
            std::make_unique<XAPNode>(std::make_unique<ModulatorSource>(1, 2.0), "LFO 2"));
        proc_nodes.emplace_back(
            std::make_unique<XAPNode>(std::make_unique<ModulatorSource>(1, 1.03), "LFO 3"));

        connectEventPorts(findByName(proc_nodes, "Event Gen 1"), 0,
                          findByName(proc_nodes, "Surge XT 1"), 0);
        connectEventPorts(findByName(proc_nodes, "Event Gen 2"), 0,
                          findByName(proc_nodes, "Surge XT 2"), 0);
        // connectAudioBetweenNodes(findByName(proc_nodes, "Surge XT 1"), 0, 0,
        //                          findByName(proc_nodes, "Valhalla"), 0, 0);
        // connectAudioBetweenNodes(findByName(proc_nodes, "Surge XT 1"), 0, 1,
        //                           findByName(proc_nodes, "Valhalla"), 0, 1);
        connectAudioBetweenNodes(findByName(proc_nodes, "Surge XT 2"), 0, 0,
                                 findByName(proc_nodes, "Valhalla"), 0, 0);
        connectAudioBetweenNodes(findByName(proc_nodes, "Surge XT 2"), 0, 1,
                                 findByName(proc_nodes, "Valhalla"), 0, 1);

        // connectModulation(findByName(proc_nodes, "LFO 1"), 0, findByName(proc_nodes, "Valhalla"),
        // 0,
        //                   true, 1.0);
        connectModulation(
            findByName(proc_nodes, "LFO 2"), 0, findByName(proc_nodes, "Surge XT 2"),
            *findParameterFromName(findByName(proc_nodes, "Surge XT 2")->processor.get(),
                                   "B Osc 1 Pitch"),
            false, 0.1);
        connectModulation(
            findByName(proc_nodes, "LFO 3"), 0, findByName(proc_nodes, "Surge XT 2"),
            *findParameterFromName(findByName(proc_nodes, "Surge XT 2")->processor.get(),
                                   "B Osc 1 Pitch"),
            false, 0.00);

        findByName(proc_nodes, "Valhalla")->PreProcessFunc = [](XAPNode *node) {
            // set reverb mix
            xenakios::pushParamEvent(node->inEvents, false, 0, 0, 0.0);
        };

        runOrder = topoSort(findByName(proc_nodes, "Valhalla"));
        std::cout << "**** GRAPH RUN ORDER ****\n";
        for (auto &n : runOrder)
        {
            std::cout << n->displayName << "\n";
        }
        std::cout << "****  ****\n";
        int procbufsize = maxFrameCount;

        memset(&transport, 0, sizeof(clap_event_transport));
        clap_process ctx;
        memset(&ctx, 0, sizeof(clap_process));
        ctx.frames_count = procbufsize;
        ctx.transport = &transport;
        double sr = sampleRate;
        m_sr = sampleRate;
        for (auto &n : runOrder)
        {
            n->initBuffers(2, 2, procbufsize);
            n->processor->activate(sr, procbufsize, procbufsize);
            n->processor->renderSetMode(CLAP_RENDER_OFFLINE);
        }
        auto surge2 = findByName(proc_nodes, "Surge XT 2")->processor.get();
        auto parid = findParameterFromName(surge2, "Active Scene");
        if (parid)
        {
            findByName(proc_nodes, "Surge XT 2")->PreProcessFunc = [parid](XAPNode *node) {
                // set surge param
                xenakios::pushParamEvent(node->inEvents, false, 0, *parid, 1.0);
            };
        }
        int outlen = 30 * sr;

        eventMergeList.reserve(1024);

        transport.flags = CLAP_TRANSPORT_HAS_SECONDS_TIMELINE | CLAP_TRANSPORT_IS_PLAYING;
        return true;
    }

    clap_process_status process(const clap_process *process) noexcept override
    {
        auto procbufsize = process->frames_count;
        double x = outcounter / m_sr; // in seconds
        clap_sectime y = std::round(CLAP_SECTIME_FACTOR * x);
        transport.song_pos_seconds = y;
        clap_process ctx;
        memset(&ctx, 0, sizeof(clap_process));
        ctx.frames_count = procbufsize;
        ctx.transport = &transport;
        for (auto &n : runOrder)
        {
            // could bypass if we don't have audio inputs
            for (int j = 0; j < procbufsize; ++j)
            {
                n->inPortBuffers[0].data32[0][j] = 0.0f;
                n->inPortBuffers[0].data32[1][j] = 0.0f;
            }
            eventMergeList.clear();
            accumModValues.clear();

            for (auto &conn : n->inputConnections)
            {
                if (conn.type == XAPNode::ConnectionType::Audio)
                {
                    handleNodeAudioInputs(n, conn, procbufsize);
                }
                else if (conn.type == XAPNode::ConnectionType::Events)
                {
                    handleNodeEvents(conn, eventMergeList);
                }
                else
                {
                    handleNodeModulationEvents(conn, modulationMergeList);
                }
            }

            n->inEvents.clear();
            if (eventMergeList.size() > 0 || modulationMergeList.size() > 0)
            {
                xenakios::CrossThreadMessage ctmsg;
                while (n->processor->dequeueParameterChange(ctmsg))
                {
                    if (ctmsg.eventType == CLAP_EVENT_PARAM_VALUE)
                    {
                        auto from_ui_ev =
                            makeClapParameterValueEvent(0, ctmsg.paramId, ctmsg.value);
                        modulationMergeList.push((clap_event_header *)&from_ui_ev);
                    }
                }
                for (int i = 0; i < modulationMergeList.size(); ++i)
                    eventMergeList.push_back(modulationMergeList.get(i));

                choc::sorting::stable_sort(
                    eventMergeList.begin(), eventMergeList.end(),
                    [](auto &lhs, auto &rhs) { return lhs->time < rhs->time; });
                for (auto &e : eventMergeList)
                {
                    n->inEvents.push(e);
                }
            }

            ctx.audio_inputs_count = 1;
            ctx.audio_inputs = n->inPortBuffers;
            ctx.audio_outputs_count = 1;
            ctx.audio_outputs = n->outPortBuffers;
            ctx.in_events = n->inEvents.clapInputEvents();
            ctx.out_events = n->outEvents.clapOutputEvents();
            if (n->PreProcessFunc)
            {
                n->PreProcessFunc(n);
            }
            if (n->displayName == "Surge XT 2")
            {
                // printClapEvents(n->inEvents);
            }
            n->processor->process(&ctx);
        }
        for (auto &n : runOrder)
        {
            n->inEvents.clear();
            n->outEvents.clear();
        }
        modulationMergeList.clear();
        auto outbufs = runOrder.back()->outChannels;
        for (int i = 0; i < process->audio_outputs[0].channel_count; ++i)
        {
            for (int j = 0; j < process->frames_count; ++j)
            {
                process->audio_outputs[0].data32[i][j] = outbufs[i][j];
            }
        }

        outcounter += procbufsize;
        return CLAP_PROCESS_CONTINUE;
    }

  private:
    std::vector<std::unique_ptr<XAPNode>> proc_nodes;
    std::vector<XAPNode *> runOrder;
    clap_event_transport transport;
    int outcounter = 0;

    std::vector<clap_event_header *> eventMergeList;
    clap::helpers::EventList modulationMergeList;
    std::unordered_map<clap_id, double> accumModValues;
    double m_sr = 0.0;
};

class XAPPlayer : public juce::AudioIODeviceCallback
{
  public:
    XAPPlayer(xenakios::XAudioProcessor &procToPlay) : m_proc(procToPlay) {}
    void audioDeviceIOCallbackWithContext(const float *const *inputChannelData,
                                          int numInputChannels, float *const *outputChannelData,
                                          int numOutputChannels, int numSamples,
                                          const AudioIODeviceCallbackContext &context) override
    {
        clap_process process;
        juce::zerostruct(process);
        clap_audio_buffer cab[1];
        cab[0].channel_count = 2;
        cab[0].constant_mask = 0;
        cab[0].data64 = nullptr;
        cab[0].latency = 0;
        // float *chandatas[2] = {&procbuf[blocksize * 0], &procbuf[blocksize * 1]};
        cab[0].data32 = (float **)outputChannelData;
        process.audio_outputs_count = 1;
        process.audio_outputs = cab;
        process.frames_count = numSamples;
        m_proc.process(&process);
    }
    void audioDeviceAboutToStart(AudioIODevice *device) override
    {
        m_proc.activate(device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples(),
                        device->getCurrentBufferSizeSamples());
    }
    void audioDeviceStopped() override {}

  private:
    xenakios::XAudioProcessor &m_proc;
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
    int outlen = 10 * sr;
    int outcounter = 0;
    juce::File outfile(R"(C:\develop\AudioPluginHost_mk2\Source\Experimental\graph_out_03.wav)");
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

class MainComponent : public juce::Component
{
  public:
    std::unique_ptr<xenakios::XAudioProcessor> m_test_proc;
    MainComponent()
    {
        // m_test_proc = std::make_unique<ClapPluginFormatProcessor>(
        //     R"(C:\Program Files\Common Files\CLAP\ChowMultiTool.clap)", 0);
        //m_test_proc = std::make_unique<ClapPluginFormatProcessor>(
        //    R"(C:\Program Files\Common Files\CLAP\airwin-to-clap.clap)", 0);
         m_test_proc = std::make_unique<FilePlayerProcessor>();
        m_test_proc->activate(44100.0, 512, 512);
        // for attaching plugin provided GUIs we need the native window handle
        // which might not exist yet, so init the GUI later in the message loop
        juce::MessageManager::callAsync([this] {
            clap_plugin_descriptor desc;
            if (m_test_proc->getDescriptor(&desc))
            {
                getParentComponent()->setName(juce::String(desc.vendor) + " : " + desc.name);
            }
            m_test_proc->guiCreate("", false);
            clap_window win;
            win.api = "JUCECOMPONENT";
            win.ptr = this;
            if (m_test_proc->guiSetParent(&win))
            {
                // DBG("set clap plugin gui parent");
            }
            uint32 w = 0;
            uint32_t h = 0;
            if (m_test_proc->guiGetSize(&w, &h))
            {
                setSize(w, h);
            }
            m_test_proc->guiShow();
            m_test_proc->OnPluginRequestedResize = [this](uint32_t neww, uint32_t newh) {
                // setSize is a synchronous call, so we can toggle the flag like this(?)
                m_plugin_requested_resize = true;
                setSize(neww, newh);
                m_plugin_requested_resize = false;
            };
        });
        setSize(100, 100);
    }
    ~MainComponent() override
    {
        m_test_proc->guiHide();
        m_test_proc->guiDestroy();
    }
    bool m_plugin_requested_resize = false;
    void resized() override
    {
        // if it was the plugin that requested the resize, don't
        // resize the plugin again!
        if (m_plugin_requested_resize)
            return;
        if (m_test_proc->guiCanResize())
            m_test_proc->guiSetSize(getWidth(), getHeight());
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
int main()
{
    juce::ScopedJuceInitialiser_GUI gui_init;
    // test_node_connecting();
    test_graph_processor_realtime();
    return 0;
}
#endif
