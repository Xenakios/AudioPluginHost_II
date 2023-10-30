#pragma once
#include "xaudioprocessor.h"
#include "JuceHeader.h"
#include "xap_utils.h"
#include "containers/choc_NonAllocatingStableSort.h"

class XAPNode
{
  public:
    /*
    Audio : 32 bit floating point audio buffers
    Events : Events that should be passed directly from the source node to the destination,
    typically musical notes
    Modulation : the source node sends Clap parameter modulation messages with values between
    -1 and 1 that are mapped by the graph playback system into suitable parameter mod/value
    messages for the destination node
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
        int sourcePinIndex = -1;
        int destinationPinIndex = -1;
    };
    struct Pin
    {
        Pin() {}
        Pin(XAPNode *node_, bool isInput_, ConnectionType type_, int port_, int channel_)
            : node(node_), isInput(isInput_), type(type_), port(port_), channel(channel_)
        {
        }
        XAPNode *node = nullptr;
        bool isInput = false;
        ConnectionType type;
        int port = 0;
        int channel = 0;
    };
    std::vector<Pin> inputPins;
    std::vector<Pin> outputPins;
    std::string processorName;
    XAPNode(std::unique_ptr<xenakios::XAudioProcessor> nodeIn, std::string name = "")
        : processor(std::move(nodeIn)), displayName(name)
    {
        clap_plugin_descriptor desc;
        if (processor->getDescriptor(&desc))
        {
            processorName = desc.name;
        }
        scanParameters();
        // we may get a double scan of the parameters at init time, but such is life...
        processor->OnPluginRequestedParameterRescan = [this](auto) { scanParameters(); };
    }
    ~XAPNode()
    {
        destroyBuffers(inPortBuffers);
        destroyBuffers(outPortBuffers);
    }
    void scanParameters()
    {
        DBG("scanning parameters for " << displayName);
        parameterInfos.clear();
        for (int i = 0; i < processor->paramsCount(); ++i)
        {
            clap_param_info info;
            if (processor->paramsInfo(i, &info))
            {
                parameterInfos[info.id] = info;
            }
        }
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
        inputPins.clear();
        outputPins.clear();
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
                    inputPins.emplace_back(this, true, XAPNode::ConnectionType::Audio, i, j);
                    inPortBuffers[i].data32[j] = new float[maxFrames];
                    for (int k = 0; k < maxFrames; ++k)
                        inPortBuffers[i].data32[j][k] = 0.0f;
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
                    outputPins.emplace_back(this, false, XAPNode::ConnectionType::Audio, i, j);
                    outPortBuffers[i].data32[j] = new float[maxFrames];
                    for (int k = 0; k < maxFrames; ++k)
                        outPortBuffers[i].data32[j][k] = 0.0f;
                }
            }
        }
        for (int i = 0; i < processor->notePortsCount(true); ++i)
        {
            inputPins.emplace_back(this, true, XAPNode::ConnectionType::Events, i, 0);
        }
        for (int i = 0; i < processor->notePortsCount(false); ++i)
        {
            outputPins.emplace_back(this, false, XAPNode::ConnectionType::Events, i, 0);
        }
        inputPins.emplace_back(this, true, XAPNode::ConnectionType::Modulation, 0, 0);
    }

    std::unique_ptr<xenakios::XAudioProcessor> processor;
    std::vector<clap_audio_buffer> inPortBuffers;
    std::vector<clap_audio_buffer> outPortBuffers;
    clap::helpers::EventList inEvents;
    clap::helpers::EventList outEvents;
    std::vector<Connection> inputConnections;
    std::string displayName;
    std::function<void(XAPNode *)> PreProcessFunc;
    std::unordered_map<clap_id, double> modulationSums;
    bool modulationWasApplied = false;
    std::unordered_map<clap_id, clap_param_info> parameterInfos;
    juce::Rectangle<int> nodeSceneBounds;
};

inline int findPinIndex(XAPNode *node, XAPNode::ConnectionType type, bool isInput, int port,
                        int channel)
{
    auto pins = &node->inputPins;
    if (!isInput)
        pins = &node->outputPins;
    for (int i = 0; i < pins->size(); ++i)
    {
        auto &pin = (*pins)[i];
        if (pin.type == type && pin.port == port && pin.channel == channel)
        {
            return i;
        }
    }
    return -1;
}

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
    conn.sourcePinIndex =
        findPinIndex(sourceNode, XAPNode::ConnectionType::Audio, false, sourcePort, sourceChannel);
    conn.destinationPinIndex = findPinIndex(destinationNode, XAPNode::ConnectionType::Audio, true,
                                            destinationPort, destinationChannel);

    conn.destination = destinationNode;
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
    conn.destination = destinationNode;
    conn.destinationPort = destinationPort;

    conn.sourcePinIndex =
        findPinIndex(sourceNode, XAPNode::ConnectionType::Events, false, sourcePort, 0);
    conn.destinationPinIndex =
        findPinIndex(destinationNode, XAPNode::ConnectionType::Events, true, destinationPort, 0);

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
    if (!conn.isDestructiveModulation)
    {
        destinationNode->modulationSums[destinationParamId] = 0.0;
    }
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
    // for (int j = 0; j < oevents.size(); ++j)
    if (oevents.size() > 0)
    {
        // auto ev = oevents.get(j);
        auto ev = oevents.get(oevents.size() - 1);
        if (ev->type == CLAP_EVENT_PARAM_MOD)
        {
            auto sourcemev = (const clap_event_param_mod *)ev;
            if (conn.sourceParameter == sourcemev->param_id)
            {
                double val = sourcemev->amount;
                if (conn.isDestructiveModulation)
                {
                    val = 0.5 + 0.5 * sourcemev->amount;
                    // for now, we don't sum destructive modulations
                    xenakios::pushParamEvent(modulationMergeList, false, sourcemev->header.time,
                                             conn.destinationParameter, val);
                }
                else
                {
                    val *= conn.modulationDepth;
                    conn.destination->modulationSums[conn.destinationParameter] += val;
                    conn.destination->modulationWasApplied = true;
                    // xenakios::pushParamEvent(modulationMergeList, true, sourcemev->header.time,
                    //                          conn.destinationParameter, val);
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
        if (equalsToAny(ev->type, CLAP_EVENT_NOTE_ON, CLAP_EVENT_NOTE_OFF, CLAP_EVENT_NOTE_CHOKE))
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
    template <typename T> T *findProcessorAndCast(const std::string &name)
    {
        return dynamic_cast<T *>(findProcessor(name));
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
    bool connectModulationByNames(const std::string &sourceNodeName, clap_id sourceParamId,
                                  const std::string &destinationNodeName,
                                  clap_id destinationParamId, bool destructive,
                                  double initialModDepth)
    {
        auto srcNode = findNodeByName(sourceNodeName);
        auto destNode = findNodeByName(destinationNodeName);
        return connectModulation(srcNode, sourceParamId, destNode, destinationParamId, destructive,
                                 initialModDepth);
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

    std::string outputNodeId = "";
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
            n->modulationWasApplied = false;
            for (auto &summod : n->modulationSums)
            {
                summod.second = 0.0;
            }
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
                noteMergeList.size() > 0 || n->modulationWasApplied)
            {
                // i've forgotten why this cross thread message stuff is here...?
                /*
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
                */
                for (int i = 0; i < modulationMergeList.size(); ++i)
                    eventMergeVector.push_back(modulationMergeList.get(i));
                for (int i = 0; i < noteMergeList.size(); ++i)
                    eventMergeVector.push_back(noteMergeList.get(i));
                // we can't easily retain the timestamps of summed modulations
                // so we just put all the summed modulations at timestamp 0.
                // there might be ways around this, but it would complicate things too much
                // at this point
                for (const auto &modsum : n->modulationSums)
                {
                    // could perhaps be done more robustly, as float comparisons
                    // are not that nice, but this should work
                    if (modsum.second != 0.0)
                    {
                        double moda = modsum.second;
                        const auto &pinfo = n->parameterInfos[modsum.first];
                        moda = std::clamp(moda, pinfo.min_value, pinfo.max_value);
                        auto modev = makeClapParameterModEvent(0, modsum.first, moda);
                        n->inEvents.push((clap_event_header *)&modev);
                        // DBG(transport.song_pos_seconds
                        //    << " " << n->displayName << " Applied summed modulation for par "
                        //    << (int64_t)modsum.first << " with amount " << moda);
                    }
                }
                choc::sorting::stable_sort(
                    eventMergeVector.begin(), eventMergeVector.end(),
                    [](auto &lhs, auto &rhs) { return lhs->time < rhs->time; });
                for (auto &e : eventMergeVector)
                {
                    n->inEvents.push(e);
                }
            }
            // printClapEvents(n->inEvents);
            ctx.audio_inputs_count = 1;
            ctx.audio_inputs = n->inPortBuffers.data();
            ctx.audio_outputs_count = n->outPortBuffers.size();
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
    static std::vector<unsigned char> getProcessorState(XAudioProcessor *proc)
    {
        std::vector<unsigned char> result;
        result.reserve(1024);
        clap_ostream os;
        os.ctx = &result;
        auto write = [](const struct clap_ostream *stream, const void *buffer, uint64_t size) {
            unsigned char *charptr = (unsigned char *)buffer;
            auto ov = (std::vector<unsigned char> *)stream->ctx;
            for (uint64_t i = 0; i < size; ++i)
                ov->push_back(charptr[i]);
            return (int64_t)size;
        };
        os.write = write;
        proc->stateSave(&os);
        return result;
    }
    static bool setProcessorState(XAudioProcessor *proc, std::vector<unsigned char> &chunk)
    {
        if (chunk.size() == 0)
            return false;
        clap_istream is;
        VecToStreamAdapter adapter{chunk};
        is.ctx = &adapter;
        is.read = [](const struct clap_istream *stream, void *buffer, uint64_t size) {
            unsigned char *charptr = (unsigned char *)buffer;
            auto adap = (VecToStreamAdapter *)stream->ctx;
            return (int64_t)adap->read(buffer, size);
        };
        return proc->stateLoad(&is);
    }

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
