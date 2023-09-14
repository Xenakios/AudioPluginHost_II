#include <iostream>
#include "testprocessors.h"
#include <memory>
#include <vector>

#include <clap/helpers/event-list.hh>
#include "clap_xaudioprocessor.h"

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

inline void test_node_connecting()
{

    std::string pathprefix = R"(C:\Program Files\Common Files\)";
    std::vector<std::unique_ptr<XAPNode>> proc_nodes;
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

    // connectModulation(findByName(proc_nodes, "LFO 1"), 0, findByName(proc_nodes, "Valhalla"), 0,
    //                   true, 1.0);
    connectModulation(findByName(proc_nodes, "LFO 2"), 0, findByName(proc_nodes, "Surge XT 2"),
                      *findParameterFromName(findByName(proc_nodes, "Surge XT 2")->processor.get(),
                                             "B Osc 1 Pitch"),
                      false, 0.1);
    connectModulation(findByName(proc_nodes, "LFO 3"), 0, findByName(proc_nodes, "Surge XT 2"),
                      *findParameterFromName(findByName(proc_nodes, "Surge XT 2")->processor.get(),
                                             "B Osc 1 Pitch"),
                      false, 0.00);

    findByName(proc_nodes, "Valhalla")->PreProcessFunc = [](XAPNode *node) {
        // set reverb mix
        xenakios::pushParamEvent(node->inEvents, false, 0, 0, 0.0);
    };

    auto runOrder = topoSort(findByName(proc_nodes, "Valhalla"));
    std::cout << "**** GRAPH RUN ORDER ****\n";
    for (auto &n : runOrder)
    {
        std::cout << n->displayName << "\n";
    }
    std::cout << "****  ****\n";
    int procbufsize = 128;
    clap_event_transport transport;
    memset(&transport, 0, sizeof(clap_event_transport));
    clap_process ctx;
    memset(&ctx, 0, sizeof(clap_process));
    ctx.frames_count = procbufsize;
    ctx.transport = &transport;
    double sr = 44100.0;
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
    int outcounter = 0;
    juce::File outfile(R"(C:\develop\AudioPluginHost_mk2\Source\Experimental\graph_out_02.wav)");
    outfile.deleteFile();
    auto ostream = outfile.createOutputStream();
    WavAudioFormat wav;
    auto writer = wav.createWriterFor(ostream.release(), sr, 2, 32, {}, 0);
    std::vector<clap_event_header *> eventMergeList;
    eventMergeList.reserve(1024);
    clap::helpers::EventList modulationMergeList;
    std::unordered_map<clap_id, double> accumModValues;
    transport.flags = CLAP_TRANSPORT_HAS_SECONDS_TIMELINE | CLAP_TRANSPORT_IS_PLAYING;
    while (outcounter < outlen)
    {
        double x = outcounter / sr; // in seconds
        clap_sectime y = std::round(CLAP_SECTIME_FACTOR * x);
        transport.song_pos_seconds = y;
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
        writer->writeFromFloatArrays(outbufs.data(), 2, procbufsize);
        outcounter += procbufsize;
    }
    delete writer;
}

class MyApp : public juce::JUCEApplication
{
  public:
};

class GuiAppApplication : public juce::JUCEApplication
{
  public:
    //==============================================================================
    GuiAppApplication() {}

    // We inject these as compile definitions from the CMakeLists.txt
    // If you've enabled the juce header with `juce_generate_juce_header(<thisTarget>)`
    // you could `#include <JuceHeader.h>` and use `ProjectInfo::projectName` etc. instead.
    const juce::String getApplicationName() override { return "FOO"; }
    const juce::String getApplicationVersion() override { return "0.0.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    //==============================================================================
    void initialise(const juce::String &commandLine) override
    {
        // This method is where you should put your application's initialisation code..
        juce::ignoreUnused(commandLine);

        mainWindow.reset(new MainWindow(getApplicationName()));
    }

    void shutdown() override
    {
        // Add your application's shutdown code here..

        mainWindow = nullptr; // (deletes our window)
    }

    //==============================================================================
    void systemRequestedQuit() override
    {
        // This is called when the app is being asked to quit: you can ignore this
        // request and let the app carry on running, or call quit() to allow the app to close.
        quit();
    }

    void anotherInstanceStarted(const juce::String &commandLine) override
    {
        // When another instance of the app is launched while this one is running,
        // this method is invoked, and the commandLine parameter tells you what
        // the other instance's command-line arguments were.
        juce::ignoreUnused(commandLine);
    }

    //==============================================================================
    /*
        This class implements the desktop window that contains an instance of
        our MainComponent class.
    */
    class MainWindow : public juce::DocumentWindow
    {
      std::unique_ptr<xenakios::XAudioProcessor> m_test_proc;
      public:
        explicit MainWindow(juce::String name)
            : DocumentWindow(name,
                             juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                                 ResizableWindow::backgroundColourId),
                             DocumentWindow::allButtons)
        {
            m_test_proc = std::make_unique<FilePlayerProcessor>();
            setUsingNativeTitleBar(true);
            auto comp = m_test_proc->createEditor();
            comp->setSize(400,300);
            setContentOwned(comp, true);

            setResizable(true, true);
            centreWithSize(getWidth(), getHeight());

            setVisible(true);
        }

        void closeButtonPressed() override
        {
            // This is called when the user tries to close this window. Here, we'll just
            // ask the app to quit when this happens, but you can change this to do
            // whatever you need.
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

        /* Note: Be careful if you override any DocumentWindow methods - the base
           class uses a lot of them, so by overriding you might break its functionality.
           It's best to do all your work in your content component instead, but if
           you really have to override any DocumentWindow methods, make sure your
           subclass also calls the superclass's method.
        */

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
    test_node_connecting();
    return 0;
}
#endif
