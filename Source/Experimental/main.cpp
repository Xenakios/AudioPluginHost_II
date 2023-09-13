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
    struct Connection
    {
        XAPNode *source = nullptr;
        bool isAudio = true; // otherwise, events
        int sourcePort = 0;
        // we could use this for events but we probably want to do stuff like note channel
        // mapping as a separate thing
        int sourceChannel = 0;
        int destinationPort = 0;
        int destinationChannel = 0;
    };
    XAPNode(std::unique_ptr<xenakios::XAudioProcessor> nodeIn, std::string name = "")
        : processor(std::move(nodeIn)), displayName(name)
    {
        audioBuffer.resize(2048);
        inBuffers[0].channel_count = 2;
        inBuffers[0].constant_mask = 0;
        inBuffers[0].latency = 0;
        inBuffers[0].data64 = nullptr;
        inChannels[0] = &audioBuffer[0];
        inChannels[1] = &audioBuffer[512];
        inBuffers[0].data32 = inChannels;

        outBuffers[0].channel_count = 2;
        outBuffers[0].constant_mask = 0;
        outBuffers[0].latency = 0;
        outBuffers[0].data64 = nullptr;
        outChannels[0] = &audioBuffer[1024];
        outChannels[1] = &audioBuffer[1024 + 512];
        outBuffers[0].data32 = outChannels;
    }
    std::unique_ptr<xenakios::XAudioProcessor> processor;
    std::vector<float> audioBuffer;
    float *inChannels[2];
    clap_audio_buffer inBuffers[1];
    float *outChannels[2];
    clap_audio_buffer outBuffers[1];
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
    conn.isAudio = false;
    conn.source = sourceNode;
    conn.sourcePort = sourcePort;
    conn.destinationPort = destinationPort;
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
    connectEventPorts(findByName(proc_nodes, "Event Gen 1"), 0,
                      findByName(proc_nodes, "Surge XT 1"), 0);
    connectEventPorts(findByName(proc_nodes, "Event Gen 2"), 0,
                      findByName(proc_nodes, "Surge XT 2"), 0);
    //connectAudioBetweenNodes(findByName(proc_nodes, "Surge XT 1"), 0, 0,
    //                         findByName(proc_nodes, "Valhalla"), 0, 0);
    //connectAudioBetweenNodes(findByName(proc_nodes, "Surge XT 1"), 0, 1,
    //                          findByName(proc_nodes, "Valhalla"), 0, 1);
    connectAudioBetweenNodes(findByName(proc_nodes, "Surge XT 2"), 0, 0,
                              findByName(proc_nodes, "Valhalla"), 0, 0);
    connectAudioBetweenNodes(findByName(proc_nodes, "Surge XT 2"), 0, 1,
                             findByName(proc_nodes, "Valhalla"), 0, 1);

    findByName(proc_nodes, "Valhalla")->PreProcessFunc = [](XAPNode *node) {
        // set reverb mix
        xenakios::pushParamEvent(node->inEvents, false, 0, 0, 0.2);
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
        n->processor->activate(sr, procbufsize, procbufsize);
    }
    auto surge2 = findByName(proc_nodes, "Surge XT 2")->processor.get();
    clap_id parid = -1;
    for (int i = 0; i < surge2->paramsCount(); ++i)
    {
        clap_param_info pinfo;
        surge2->paramsInfo(i, &pinfo);
        juce::String pname(pinfo.name);
        if (pname.containsIgnoreCase("Active Scene"))
        {
            std::cout << "Active Scene found " << pinfo.id << "\n";
            parid = pinfo.id;
            break;
        }
    }
    if (parid >= 0)
    {
        findByName(proc_nodes, "Surge XT 2")->PreProcessFunc = [parid](XAPNode *node) {
            // set surge param
            xenakios::pushParamEvent(node->inEvents, false, 0, parid, 1.0);
        };
    }
    int outlen = 180 * sr;
    int outcounter = 0;
    juce::File outfile(R"(C:\develop\AudioPluginHost_mk2\Source\Experimental\graph_out.wav)");
    outfile.deleteFile();
    auto ostream = outfile.createOutputStream();
    WavAudioFormat wav;
    auto writer = wav.createWriterFor(ostream.release(), sr, 2, 32, {}, 0);
    std::vector<clap_event_header *> eventMergeList;
    eventMergeList.reserve(1024);
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
                n->inBuffers[0].data32[0][j] = 0.0f;
                n->inBuffers[0].data32[1][j] = 0.0f;
            }
            eventMergeList.clear();
            if (n->inputConnections.size() > 0)
            {
                for (auto &conn : n->inputConnections)
                {
                    if (conn.isAudio)
                    {
                        int srcChan = conn.sourceChannel;
                        int destChan = conn.destinationChannel;

                        for (int j = 0; j < procbufsize; ++j)
                        {
                            n->inBuffers[0].data32[destChan][j] +=
                                conn.source->outBuffers[0].data32[srcChan][j];
                        }
                    }
                    else
                    {
                        auto &oevents = conn.source->outEvents;
                        for (int j = 0; j < oevents.size(); ++j)
                        {
                            eventMergeList.push_back(oevents.get(j));
                        }
                    }
                }
            }
            n->inEvents.clear();
            if (eventMergeList.size() > 0)
            {
                choc::sorting::stable_sort(
                    eventMergeList.begin(), eventMergeList.end(),
                    [](auto &lhs, auto &rhs) { return lhs->time < rhs->time; });
                for (auto &e : eventMergeList)
                {
                    n->inEvents.push(e);
                }
            }

            ctx.audio_inputs_count = 1;
            ctx.audio_inputs = n->inBuffers;
            ctx.audio_outputs_count = 1;
            ctx.audio_outputs = n->outBuffers;
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
        auto outbufs = runOrder.back()->outChannels;
        writer->writeFromFloatArrays(outbufs, 2, procbufsize);
        outcounter += procbufsize;
    }
    delete writer;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui_init;
    test_node_connecting();
    return 0;
}
