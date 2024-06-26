#include "xaudiograph.h"

XAPGraph::XAPGraph()
{
    // should be enough for infrequent requests from the UI
    from_ui_fifo.reset(128);
    to_ui_fifo.reset(128);
    // reserve to allow for realtime manipulation, but obviously at some point
    // might need to allocate...
    proc_nodes.reserve(1024);
    runOrder.reserve(1024);
    // m_graph->addProcessorAsNode(std::make_unique<IOProcessor>(true), "Audio Input", 100);
    //     m_graph->addProcessorAsNode(std::make_unique<IOProcessor>(false), "Audio Output",
    //     101);
}

uint64_t XAPGraph::addProcessorAsNode(std::unique_ptr<xenakios::XAudioProcessor> proc,
                                      std::string displayid, std::optional<uint64_t> id)
{
    // ok, so this a complete mess, how is the id supposed to be used/generated???
    uint64_t id_to_use = runningNodeID;
    if (id)
        id_to_use = *id;
    proc_nodes.emplace_back(std::make_unique<XAPNode>(std::move(proc), displayid, id_to_use));
    // auto result = runningNodeID;
    //++runningNodeID;
    auto result = proc_nodes.size();
    return result;
}

void XAPGraph::handleUIMessages()
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

void XAPGraph::updateRunOrder()
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

bool XAPGraph::activate(double sampleRate, uint32_t minFrameCount, uint32_t maxFrameCount) noexcept
{
    if (proc_nodes.size() == 0)
        return false;
    updateRunOrder();

    int procbufsize = maxFrameCount;

    memset(&transport, 0, sizeof(clap_event_transport));
    double sr = sampleRate;
    m_sr = sampleRate;
    std::cout << "**** GRAPH RUN ORDER ****\n";
    for (auto &n : proc_nodes)
    {
        n->initAudioBuffersFromProcessorInfo(maxFrameCount);
    }
    for (auto &n : runOrder)
    {
        n->processor->activate(sr, procbufsize, procbufsize);
        std::cout << "\t" << n->displayName << "\n";
        for (int i = 0; i < n->processor->audioPortsCount(true); ++i)
        {
            clap_audio_port_info pinfo;
            if (n->processor->audioPortsInfo(i, true, &pinfo))
            {
                // std::cout << "\t\tInput port " << i << " has " << pinfo.channel_count
                //           << " channels\n";
            }
        }
        for (int i = 0; i < n->processor->audioPortsCount(false); ++i)
        {
            clap_audio_port_info pinfo;
            if (n->processor->audioPortsInfo(i, false, &pinfo))
            {
                // std::cout << "\t\tOutput port " << i << " has " << pinfo.channel_count
                //           << " channels\n";
            }
        }
        n->initAudioBuffersFromProcessorInfo(procbufsize);
        /*
        for (int i = 0; i < n->processor->remoteControlsPageCount(); ++i)
        {
            clap_remote_controls_page page;
            if (n->processor->remoteControlsPageGet(i, &page))
            {
                std::cout << "\t\tRemote control page " << i << " " << page.section_name << "/"
                          << page.page_name << "\n";
                for (int j = 0; j < CLAP_REMOTE_CONTROLS_COUNT; ++j)
                {
                    auto id = page.param_ids[j];
                    std::cout << "\t\t\t" << id << " " << n->parameterInfos[id].name << "\n";
                }
            }
        }
        */
    }
    // std::cout << "****                 ****\n";

    transport.flags = CLAP_TRANSPORT_HAS_SECONDS_TIMELINE | CLAP_TRANSPORT_IS_PLAYING;
    m_activated = true;
    return true;
}

clap_process_status XAPGraph::process(const clap_process *process) noexcept
{
    if (!m_activated)
        return CLAP_PROCESS_ERROR;
    // we tolerate startProcessing not being called before process, but maybe should require
    // explicit start?
    if (!m_processing_started)
    {
        m_processing_started = true;
    }
    handleUIMessages();
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
        eventMergeList.clear();
        accumModValues.clear();

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
                handleNodeEventsAlt(conn, eventMergeList);
            }
            else
            {
                handleNodeModulationEvents(conn, eventMergeList);
            }
        }

        n->inEvents.clear();
        if (n->modulationWasApplied || eventMergeList.size() > 0)
        {
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
                    eventMergeList.pushEvent(&modev);
                    // n->inEvents.push((clap_event_header *)&modev);
                    // DBG(transport.song_pos_seconds
                    //    << " " << n->displayName << " Applied summed modulation for par "
                    //    << (int64_t)modsum.first << " with amount " << moda);
                }
            }
            eventMergeList.sortEvents();
            // choc::sorting::stable_sort(eventMergeVector.begin(), eventMergeVector.end(),
            //                            [](auto &lhs, auto &rhs) { return lhs->time < rhs->time;
            //                            });
            for (int i = 0; i < eventMergeList.size(); ++i)
            {
                auto ev = eventMergeList.get(i);
                n->inEvents.push(ev);
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
        for (size_t i = 0; i < n->outEvents.size(); ++i)
        {
            auto oev = n->outEvents.get(i);
            if (equalsToAny(oev->type, CLAP_EVENT_PARAM_GESTURE_BEGIN,
                            CLAP_EVENT_PARAM_GESTURE_END))
            {
                auto gev = reinterpret_cast<clap_event_param_gesture *>(oev);
                m_last_touched_node = n;
                m_last_touched_param = gev->param_id;
                /*
                if (oev->type == CLAP_EVENT_PARAM_GESTURE_BEGIN)
                    DBG("parameter " << gev->param_id << " begin gesture in proc "
                                     << n->processorName);
                if (oev->type == CLAP_EVENT_PARAM_GESTURE_END)
                    DBG("parameter " << gev->param_id << " end gesture in proc "
                                   << n->processorName);
                */
            }
            if (oev->type == CLAP_EVENT_PARAM_VALUE)
            {
                auto pev = reinterpret_cast<clap_event_param_value *>(oev);
                m_last_touched_node = n;
                m_last_touched_param = pev->param_id;
                m_last_touched_param_value = pev->value;
                // DBG("parameter " << pev->param_id << " changed to " << pev->value << " in proc "
                //                  << n->processorName);
            }
        }
    }
    for (auto &n : runOrder)
    {
        n->inEvents.clear();
        n->outEvents.clear();
    }

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
