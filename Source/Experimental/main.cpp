#include <iostream>
#include "testprocessors.h"
#include <memory>
#include <vector>

#include <clap/helpers/event-list.hh>
#include "clap_xaudioprocessor.h"
#include "juce_xaudioprocessor.h"
#include "fileplayer_xaudioprocessor.h"
#include "xap_utils.h"

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
        XAPNode *destination = nullptr;
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
    ~XAPNode()
    {
        destroyBuffers(inPortBuffers);
        destroyBuffers(outPortBuffers);
    }
    void destroyBuffers(std::vector<clap_audio_buffer_t> &buffers)
    {
        for (auto &buf : buffers)
        {
            for (int i = 0; i < buf.channel_count; ++i)
            {
                delete[] buf.data32[i];
            }
            delete[] buf.data32;
        }
        buffers.clear();
    }

    void initAudioBuffersFromProcessorInfo(int maxFrames)
    {
        destroyBuffers(inPortBuffers);
        destroyBuffers(outPortBuffers);
        inPortBuffers.resize(processor->audioPortsCount(true));
        outPortBuffers.resize(processor->audioPortsCount(false));
        for (int i = 0; i < processor->audioPortsCount(true); ++i)
        {
            clap_audio_port_info pinfo;
            if (processor->audioPortsInfo(i, true, &pinfo))
            {
                inPortBuffers[i].channel_count = pinfo.channel_count;
                inPortBuffers[i].constant_mask = 0;
                inPortBuffers[i].data64 = nullptr;
                inPortBuffers[i].latency = 0;
                inPortBuffers[i].data32 = new float *[pinfo.channel_count];
                for (int j = 0; j < pinfo.channel_count; ++j)
                {
                    inPortBuffers[i].data32[j] = new float[maxFrames];
                }
            }
        }
        for (int i = 0; i < processor->audioPortsCount(false); ++i)
        {
            clap_audio_port_info pinfo;
            if (processor->audioPortsInfo(i, false, &pinfo))
            {
                outPortBuffers[i].channel_count = pinfo.channel_count;
                outPortBuffers[i].constant_mask = 0;
                outPortBuffers[i].data64 = nullptr;
                outPortBuffers[i].latency = 0;
                outPortBuffers[i].data32 = new float *[pinfo.channel_count];
                for (int j = 0; j < pinfo.channel_count; ++j)
                {
                    outPortBuffers[i].data32[j] = new float[maxFrames];
                }
            }
        }
    }

    std::unique_ptr<xenakios::XAudioProcessor> processor;
    std::vector<clap_audio_buffer> inPortBuffers;
    std::vector<clap_audio_buffer> outPortBuffers;
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
    conn.destination = destinationNode;
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

inline void handleNodeEventsAlt(XAPNode::Connection &conn, clap::helpers::EventList &eventMergeList)
{
    // this is wayyyy too messy to handle like this, but we have to live with this for now
    auto &oevents = conn.source->outEvents;
    for (int j = 0; j < oevents.size(); ++j)
    {
        auto ev = oevents.get(j);
        if (ev->type == CLAP_EVENT_NOTE_ON || ev->type == CLAP_EVENT_NOTE_OFF ||
            ev->type == CLAP_EVENT_NOTE_CHOKE)
        {
            auto tev = (clap_event_note *)ev;
            if (tev->port_index == conn.sourcePort)
            {
                clap_event_note nev = *tev;
                nev.port_index = conn.destinationPort;
                eventMergeList.tryPush((const clap_event_header *)&nev);
            }
        }
        if (ev->type == CLAP_EVENT_NOTE_EXPRESSION)
        {
            auto expev = (clap_event_note_expression *)ev;
            if (expev->port_index == conn.sourcePort)
            {
                clap_event_note_expression nev = *expev;
                nev.port_index = conn.destinationPort;
                eventMergeList.tryPush((const clap_event_header *)&nev);
            }
        }
    }
}

inline void handleNodeAudioInputs(XAPNode *n, XAPNode::Connection &conn, int procbufsize)
{
    int srcPort = conn.sourcePort;
    int destPort = conn.destinationPort;
    int srcChan = conn.sourceChannel;
    int destChan = conn.destinationChannel;
    for (int j = 0; j < procbufsize; ++j)
    {
        n->inPortBuffers[destPort].data32[destChan][j] +=
            conn.source->outPortBuffers[srcPort].data32[srcChan][j];
    }
}

inline void clearNodeAudioInputs(XAPNode *n, int procbufsize)
{
    for (int i = 0; i < n->inPortBuffers.size(); ++i)
    {
        for (int j = 0; j < n->inPortBuffers[i].channel_count; ++j)
        {
            for (int k = 0; k < procbufsize; ++k)
            {
                n->inPortBuffers[i].data32[j][k] = 0.0f;
            }
        }
    }
}

class XAPGraph : public xenakios::XAudioProcessor
{
  public:
    XAPGraph() {}
    void addProcessorAsNode(std::unique_ptr<xenakios::XAudioProcessor> proc, std::string id)
    {
        proc_nodes.emplace_back(std::make_unique<XAPNode>(std::move(proc), id));
    }
    XAPNode *findNodeByName(std::string name)
    {
        for (auto &n : proc_nodes)
            if (n->displayName == name)
                return n.get();
        return nullptr;
    }
    xenakios::XAudioProcessor *findProcessor(const std::string &name)
    {
        for (auto &n : proc_nodes)
            if (n->displayName == name)
                return n->processor.get();
        return nullptr;
    }
    bool connectAudio(const std::string &sourceNodeName, int sourcePort, int sourceChannel,
                      const std::string &destinationNodeName, int destinationPort,
                      int destinationChannel)
    {
        auto srcNode = findNodeByName(sourceNodeName);
        auto destNode = findNodeByName(destinationNodeName);
        return connectAudioBetweenNodes(srcNode, sourcePort, sourceChannel, destNode,
                                        destinationPort, destinationChannel);
    }
    std::vector<XAPNode::Connection *> getModulationConnections()
    {
        std::vector<XAPNode::Connection *> result;
        for (auto &n : proc_nodes)
        {
            for (auto &conn : n->inputConnections)
            {
                if (conn.type == XAPNode::ConnectionType::Modulation)
                {
                    result.push_back(&conn);
                }
            }
        }
        return result;
    }
    void addTestNodes()
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
        auto surge2 = findByName(proc_nodes, "Surge XT 2")->processor.get();
        auto parid = findParameterFromName(surge2, "Active Scene");
        if (parid)
        {
            findByName(proc_nodes, "Surge XT 2")->PreProcessFunc = [parid](XAPNode *node) {
                // set surge param
                xenakios::pushParamEvent(node->inEvents, false, 0, *parid, 1.0);
            };
        }
    }
    std::string outputNodeId = "Valhalla";
    bool m_activated = false;
    void deactivate() noexcept override { m_activated = false; }
    bool activate(double sampleRate, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override
    {
        if (proc_nodes.size() == 0)
            return false;
        runOrder = topoSort(findNodeByName(outputNodeId));

        int procbufsize = maxFrameCount;

        memset(&transport, 0, sizeof(clap_event_transport));
        double sr = sampleRate;
        m_sr = sampleRate;
        std::cout << "**** GRAPH RUN ORDER ****\n";
        for (auto &n : runOrder)
        {
            n->processor->activate(sr, procbufsize, procbufsize);
            std::cout << "\t" << n->displayName << "\n";
            for (int i = 0; i < n->processor->audioPortsCount(true); ++i)
            {
                clap_audio_port_info pinfo;
                if (n->processor->audioPortsInfo(i, true, &pinfo))
                {
                    std::cout << "\t\tInput port " << i << " has " << pinfo.channel_count
                              << " channels\n";
                }
            }
            for (int i = 0; i < n->processor->audioPortsCount(false); ++i)
            {
                clap_audio_port_info pinfo;
                if (n->processor->audioPortsInfo(i, false, &pinfo))
                {
                    std::cout << "\t\tOutput port " << i << " has " << pinfo.channel_count
                              << " channels\n";
                }
            }
            n->initAudioBuffersFromProcessorInfo(procbufsize);
            // n->initBuffers(2, 2, procbufsize);
            // n->processor->renderSetMode(CLAP_RENDER_OFFLINE);
        }
        std::cout << "****                 ****\n";
        int outlen = 30 * sr;

        eventMergeVector.reserve(1024);

        transport.flags = CLAP_TRANSPORT_HAS_SECONDS_TIMELINE | CLAP_TRANSPORT_IS_PLAYING;
        m_activated = true;
        return true;
    }

    clap_process_status process(const clap_process *process) noexcept override
    {
        if (!m_activated)
            return CLAP_PROCESS_ERROR;
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
            eventMergeVector.clear();
            accumModValues.clear();
            noteMergeList.clear();
            // clear node audio input buffers before summing into them
            clearNodeAudioInputs(n, procbufsize);
            for (auto &conn : n->inputConnections)
            {
                if (conn.type == XAPNode::ConnectionType::Audio)
                {
                    // sum
                    handleNodeAudioInputs(n, conn, procbufsize);
                }
                else if (conn.type == XAPNode::ConnectionType::Events)
                {
                    handleNodeEventsAlt(conn, noteMergeList);
                }
                else
                {
                    handleNodeModulationEvents(conn, modulationMergeList);
                }
            }

            n->inEvents.clear();
            if (eventMergeVector.size() > 0 || modulationMergeList.size() > 0 ||
                noteMergeList.size() > 0)
            {
                // i've forgotten why this cross thread message stuff is here...?
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
                    eventMergeVector.push_back(modulationMergeList.get(i));
                for (int i = 0; i < noteMergeList.size(); ++i)
                    eventMergeVector.push_back(noteMergeList.get(i));
                choc::sorting::stable_sort(
                    eventMergeVector.begin(), eventMergeVector.end(),
                    [](auto &lhs, auto &rhs) { return lhs->time < rhs->time; });
                for (auto &e : eventMergeVector)
                {
                    n->inEvents.push(e);
                }
            }

            ctx.audio_inputs_count = 1;
            ctx.audio_inputs = n->inPortBuffers.data();
            ctx.audio_outputs_count = 1;
            ctx.audio_outputs = n->outPortBuffers.data();
            ctx.in_events = n->inEvents.clapInputEvents();
            ctx.out_events = n->outEvents.clapOutputEvents();
            if (n->PreProcessFunc)
            {
                n->PreProcessFunc(n);
            }

            n->processor->process(&ctx);
        }
        for (auto &n : runOrder)
        {
            n->inEvents.clear();
            n->outEvents.clear();
        }
        modulationMergeList.clear();
        noteMergeList.clear();
        auto outbufs = runOrder.back()->outPortBuffers[0].data32;
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
    std::vector<std::unique_ptr<XAPNode>> proc_nodes;

  private:
    std::vector<XAPNode *> runOrder;
    clap_event_transport transport;
    int outcounter = 0;

    std::vector<clap_event_header *> eventMergeVector;
    clap::helpers::EventList modulationMergeList;
    clap::helpers::EventList noteMergeList;
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
        cab[0].data32 = (float **)outputChannelData;
        process.audio_outputs_count = 1;
        process.audio_outputs = cab;
        process.frames_count = numSamples;
        auto err = m_proc.process(&process);
        if (err == CLAP_PROCESS_ERROR)
        {
            for (int i = 0; i < numOutputChannels; ++i)
            {
                for (int j = 0; j < numSamples; ++j)
                {
                    outputChannelData[i][j] = 0.0f;
                }
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
    g->addProcessorAsNode(std::make_unique<ClapEventSequencerProcessor>(3, 1.0), "Note Gen");
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
        setAlwaysOnTop(true);
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
    juce::Label m_info_label;
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
    void timerCallback() override
    {
        int usage = m_aman.getCpuUsage() * 100.0;
        m_infolabel.setText("CPU " + juce::String(usage) + "%", juce::dontSendNotification);
    }
    MainComponent()
    {
        addAndMakeVisible(m_infolabel);
        addAndMakeVisible(m_mod_rout_combo);

        startTimerHz(10);
        std::string pathprefix = R"(C:\Program Files\Common Files\)";

        m_graph = std::make_unique<XAPGraph>();
        m_graph->addProcessorAsNode(std::make_unique<ClapEventSequencerProcessor>(2, 1.0),
                                    "Note Gen");
        m_graph->addProcessorAsNode(std::make_unique<ClapPluginFormatProcessor>(
                                        pathprefix + R"(CLAP\Surge Synth Team\Surge XT.clap)", 0),
                                    "Surge XT 1");
        m_graph->addProcessorAsNode(std::make_unique<ClapPluginFormatProcessor>(
                                        pathprefix + R"(CLAP\Surge Synth Team\Surge XT.clap)", 0),
                                    "Surge XT 2");
        m_graph->addProcessorAsNode(std::make_unique<GainProcessorTest>(), "Main");
        connectEventPorts(m_graph->findNodeByName("Note Gen"), 0,
                          m_graph->findNodeByName("Surge XT 1"), 0);
        connectEventPorts(m_graph->findNodeByName("Note Gen"), 1,
                          m_graph->findNodeByName("Surge XT 2"), 0);

        m_graph->connectAudio("Surge XT 1", 0, 0, "Main", 0, 0);
        m_graph->connectAudio("Surge XT 1", 0, 1, "Main", 0, 1);
        m_graph->connectAudio("Surge XT 2", 0, 0, "Main", 0, 0);
        m_graph->connectAudio("Surge XT 2", 0, 1, "Main", 0, 1);
        m_graph->outputNodeId = "Main";

        juce::Random rng{7};
        for (auto &n : m_graph->proc_nodes)
        {
            // continue;
            m_xap_windows.emplace_back(std::make_unique<XapWindow>(*n->processor));
            m_xap_windows.back()->setTopLeftPosition(rng.nextInt({10, 600}),
                                                     rng.nextInt({100, 400}));
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
        initModBox();
        setSize(500, 100);
    }
    ~MainComponent() override
    {
        m_xap_windows.clear();
        m_aman.removeAudioCallback(m_player.get());
    }
    void initModBox()
    {
        auto valhnode = m_graph->findNodeByName("Valhalla");
        if (!valhnode)
            return;
        auto valh = valhnode->processor.get();
        for (int i = 0; i < valh->paramsCount(); ++i)
        {
            m_mod_rout_combo.addItem(juce::String(i), i + 1);
        }
        m_mod_rout_combo.setSelectedItemIndex(0, juce::dontSendNotification);
        m_mod_rout_combo.onChange = [this, valhnode]() {
            int destpar = m_mod_rout_combo.getSelectedItemIndex();
            for (auto &conn : valhnode->inputConnections)
            {
                if (conn.type == XAPNode::ConnectionType::Modulation)
                {
                    conn.destinationParameter = destpar;
                    break;
                }
            }
        };
    }
    bool m_plugin_requested_resize = false;
    void resized() override
    {
        m_infolabel.setBounds(0, 0, getWidth(), 25);
        m_mod_rout_combo.setBounds(0, m_infolabel.getBottom() + 1, 50, 25);
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
