#pragma once
#include "xaudioprocessor.h"
#include "JuceHeader.h"
#include "xap_utils.h"
#include "containers/choc_NonAllocatingStableSort.h"
#include "xapfactory.h"

// utility that is used by the graph to receive and output audio
// input is got via magical means, output will be available normally
class IOProcessor : public xenakios::XAudioProcessor
{
    bool m_is_input = false;

  public:
    IOProcessor(bool isInput) : m_is_input(isInput) {}
    bool getDescriptor(clap_plugin_descriptor *desc) const override
    {
        memset(desc, 0, sizeof(clap_plugin_descriptor));
        if (m_is_input)
            desc->name = "Audio Input";
        else
            desc->name = "Audio Output";
        desc->vendor = "Xenakios";
        desc->id = "org.xenakios.xupic.graph_io_input";
        if (!m_is_input)
            desc->id = "org.xenakios.xupic.graph_io_output";
        return true;
    }
    uint32_t audioPortsCount(bool isInput) const noexcept override
    {
        if (!m_is_input && isInput)
            return 1;
        return 0;
        if (m_is_input && isInput)
            return 0;
        if (m_is_input && !isInput)
            return 1;
        
        return 0;
    }
    bool audioPortsInfo(uint32_t index, bool isInput,
                        clap_audio_port_info *info) const noexcept override
    {
        info->channel_count = 2;
        info->flags = 0;
        if (!isInput)
        {
            info->id = 250;
            strcpy_s(info->name, "IO output");
        }
        if (isInput)
        {
            info->id = 300;
            strcpy_s(info->name, "IO input");
        }
        info->in_place_pair = false;
        info->port_type = "";
        return true;
    }
    int numChans = 2;
    bool activate(double sampleRate, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override
    {

        m_ext_input_buffer.resize(numChans);
        for (int i = 0; i < numChans; ++i)
        {
            m_ext_input_buffer[i].resize(maxFrameCount);
        }
        return true;
    }
    std::vector<std::vector<float>> m_ext_input_buffer;
    clap_process_status process(const clap_process *process) noexcept override
    {
        // for safety, silence all output channels first
        for (int i = 0; i < process->audio_outputs[0].channel_count; ++i)
        {
            for (int j = 0; j < process->frames_count; ++j)
            {
                process->audio_outputs[0].data32[i][j] = 0.0f;
            }
        }

        // copy from external input buffer
        if (m_is_input && process->audio_outputs[0].channel_count >= numChans)
        {
            for (int i = 0; i < numChans; ++i)
            {
                for (int j = 0; j < process->frames_count; ++j)
                {
                    process->audio_outputs[0].data32[i][j] = m_ext_input_buffer[i][j];
                }
            }
        }
        // just pass through
        if (!m_is_input && process->audio_inputs[0].channel_count >= numChans)
        {
            for (int i = 0; i < numChans; ++i)
            {
                for (int j = 0; j < process->frames_count; ++j)
                {
                    process->audio_outputs[0].data32[i][j] = process->audio_inputs[0].data32[i][j];
                }
            }
        }
        return CLAP_PROCESS_CONTINUE;
    }
};

static xenakios::RegisterXap reg_audio_in{"Audio input", "org.xenakios.xupic.graph_io_input",
                                        []() { return std::make_unique<IOProcessor>(true); }};

static xenakios::RegisterXap reg_audio_out{"Audio output", "org.xenakios.xupic.graph_io_output",
                                        []() { return std::make_unique<IOProcessor>(false); }};

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
    bool isActiveInGraph = false;
    uint64_t ID = 0;
    XAPNode(std::unique_ptr<xenakios::XAudioProcessor> nodeIn, std::string name = "",
            uint64_t id = 0)
        : processor(std::move(nodeIn)), displayName(name)
    {
        ID = id;
        clap_plugin_descriptor desc;
        if (processor->getDescriptor(&desc))
        {
            processorName = desc.name;
        }
        scanParameters();
        // we may get a double scan of the parameters at init time, but such is life...
        processor->OnPluginRequestedParameterRescan = [this](auto) { scanParameters(); };
        // some leeway to allow manipulating these in realtime context
        inputConnections.reserve(16);
        inputPins.reserve(16);
        outputPins.reserve(16);
    }
    ~XAPNode()
    {
        destroyBuffers(inPortBuffers);
        destroyBuffers(outPortBuffers);
    }
    void scanParameters()
    {
        // DBG("scanning parameters for " << displayName);
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
    std::function<void(XAPNode *)> OnRequestGUIClose;
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
                                       xenakios::SortingEventList &modulationMergeList)
{
    auto &oevents = conn.source->outEvents;
    if (oevents.size() > 0)
    {
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
                    auto routed_ev = xenakios::make_event_param_value(
                        sourcemev->header.time, conn.destinationParameter, val, nullptr);
                    modulationMergeList.pushEvent(&routed_ev);
                    // xenakios::pushParamEvent(modulationMergeList, false, sourcemev->header.time,
                    //                          conn.destinationParameter, val);
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

inline void handleNodeEventsAlt(XAPNode::Connection &conn,
                                xenakios::SortingEventList &eventMergeList)
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
                eventMergeList.pushEvent((const clap_event_header *)&nev);
            }
        }
        if (ev->type == CLAP_EVENT_NOTE_EXPRESSION)
        {
            auto expev = (clap_event_note_expression *)ev;
            if (expev->port_index == conn.sourcePort)
            {
                clap_event_note_expression nev = *expev;
                nev.port_index = conn.destinationPort;
                eventMergeList.pushEvent((const clap_event_header *)&nev);
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
    struct CTMessage
    {
        enum class Opcode
        {
            None,
            RemoveNodeInputs,
            RemoveNodeInput,
            RemoveNode,
            DeleteNode
        };
        Opcode op = Opcode::None;
        XAPNode *node = nullptr;
        XAPNode *inputNode = nullptr;
        int connectionIndex = -1;
        int port = -1;
        int channel = -1;
    };
    choc::fifo::SingleReaderSingleWriterFIFO<CTMessage> from_ui_fifo;
    choc::fifo::SingleReaderSingleWriterFIFO<CTMessage> to_ui_fifo;
    XAPGraph()
    {
        // should be enough for infrequent requests from the UI
        from_ui_fifo.reset(128);
        to_ui_fifo.reset(128);
        // reserve to allow for realtime manipulation, but obviously at some point
        // might need to allocate...
        proc_nodes.reserve(1024);
        runOrder.reserve(1024);
        //m_graph->addProcessorAsNode(std::make_unique<IOProcessor>(true), "Audio Input", 100);
        //    m_graph->addProcessorAsNode(std::make_unique<IOProcessor>(false), "Audio Output", 101);
        
    }
    std::unique_ptr<IOProcessor> m_input_processor;
    std::unique_ptr<IOProcessor> m_output_processor;
    void updateRunOrder()
    {
        for (auto &n : proc_nodes)
        {
            n->isActiveInGraph = false;
        }
        runOrder = topoSort(findNodeByName(outputNodeId));
        for (auto &n : runOrder)
        {
            n->isActiveInGraph = true;
        }
        for (auto &n : proc_nodes)
        {
            if (!n->isActiveInGraph)
            {
                n->processor->stopProcessing();
            }
        }
    }
    void removeInputNodeFromNode(XAPNode *targetNode, XAPNode *sourceNode)
    {
        if (targetNode->inputConnections.size() == 0)
            return;
        auto &inconns = targetNode->inputConnections;
        std::erase_if(
            inconns, [sourceNode](XAPNode::Connection &conn) { return conn.source == sourceNode; });
    }
    void removeNode(XAPNode *n)
    {
        for (size_t i = 0; i < proc_nodes.size(); ++i)
        {
            if (proc_nodes[i].get() == n)
            {
                for (auto &node : proc_nodes)
                {
                    removeInputNodeFromNode(node.get(), n);
                }
                auto nodeptr = proc_nodes[i].release();
                nodeptr->processor->stopProcessing();
                proc_nodes.erase(proc_nodes.begin() + i);
                updateRunOrder();
                CTMessage uimsg;
                uimsg.node = nodeptr;
                uimsg.op = CTMessage::Opcode::DeleteNode;
                to_ui_fifo.push(uimsg);
                break;
            }
        }
    }
    void handleUIMessages()
    {
        CTMessage msg;
        while (from_ui_fifo.pop(msg))
        {
            if (msg.op == CTMessage::Opcode::RemoveNodeInputs)
            {
                msg.node->inputConnections.clear();
                updateRunOrder();
            }
            if (msg.op == CTMessage::Opcode::RemoveNodeInput)
            {
                msg.node->inputConnections.erase(msg.node->inputConnections.begin() +
                                                 msg.connectionIndex);
                // this might not really require re-evaluating the whole run order,
                // but for now it will work like this...
                updateRunOrder();
            }
            if (msg.op == CTMessage::Opcode::RemoveNode)
            {
                auto sinknode = findNodeByName(outputNodeId);
                // sink node must remain
                if (msg.node != sinknode)
                {
                    removeNode(msg.node);
                }
            }
        }
    }
    uint64_t addProcessorAsNode(std::unique_ptr<xenakios::XAudioProcessor> proc,
                                std::string displayid, std::optional<uint64_t> id = {})
    {
        uint64_t id_to_use = runningNodeID;
        if (id)
            id_to_use = *id;
        proc_nodes.emplace_back(std::make_unique<XAPNode>(std::move(proc), displayid, id_to_use));
        // auto result = runningNodeID;
        //++runningNodeID;
        auto result = proc_nodes.size();
        return result;
    }
    void removeNodeFromGraph(XAPNode *n) {}
    XAPNode *findNodeByID(uint64_t id)
    {
        for (auto &n : proc_nodes)
            if (n->ID == id)
                return n.get();
        return nullptr;
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
    bool makeConnection(XAPNode::ConnectionType conntype, uint64_t sourceNodeID, int sourcePort,
                        int sourceChannel, uint64_t destinationNodeID, int destinationPort,
                        int destinationChannel)
    {
        auto sourceNode = findNodeByID(sourceNodeID);
        auto destinationNode = findNodeByID(destinationNodeID);
        if (sourceNode == nullptr || destinationNode == nullptr)
            return false;
        XAPNode::Connection conn;
        conn.type = conntype;
        conn.source = sourceNode;
        conn.sourceChannel = sourceChannel;
        conn.sourcePort = sourcePort;
        conn.sourcePinIndex = findPinIndex(sourceNode, conntype, false, sourcePort, sourceChannel);
        conn.destinationPinIndex =
            findPinIndex(destinationNode, conntype, true, destinationPort, destinationChannel);

        conn.destination = destinationNode;
        conn.destinationPort = destinationPort;
        conn.destinationChannel = destinationChannel;
        destinationNode->inputConnections.push_back(conn);

        return true;
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
    uint64_t runningNodeID = 0;
    std::string outputNodeId = "";
    bool m_activated = false;
    void deactivate() noexcept override { m_activated = false; }
    bool activate(double sampleRate, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override;
    std::atomic<bool> m_processing_started{false};
    bool startProcessing() noexcept override
    {
        // error if called again when already started
        jassert(!m_processing_started);
        m_processing_started = true;
        return true;
    }
    void stopProcessing() noexcept override
    {
        // error if called when not already started?
        jassert(m_processing_started);
        for (auto &n : runOrder)
        {
            n->processor->stopProcessing();
        }
        m_processing_started = false;
    }
    clap_process_status process(const clap_process *process) noexcept override;

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
    std::pair<XAPNode *, clap_id> getLastTouchedNodeAndParameter()
    {
        return {m_last_touched_node, m_last_touched_param};
    }
    XAPNode *m_last_touched_node = nullptr;
    clap_id m_last_touched_param = CLAP_INVALID_ID;
    double m_last_touched_param_value = 0.0;

  private:
    std::vector<XAPNode *> runOrder;
    clap_event_transport transport;
    int outcounter = 0;

    xenakios::SortingEventList eventMergeList;

    std::unordered_map<clap_id, double> accumModValues;
    double m_sr = 0.0;
};
