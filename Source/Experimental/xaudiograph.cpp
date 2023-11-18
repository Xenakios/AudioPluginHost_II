#include "xaudiograph.h"

bool XAPGraph::activate(double sampleRate, uint32_t minFrameCount, uint32_t maxFrameCount) noexcept
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
    std::cout << "****                 ****\n";
    int outlen = 30 * sr;

    eventMergeVector.reserve(1024);

    transport.flags = CLAP_TRANSPORT_HAS_SECONDS_TIMELINE | CLAP_TRANSPORT_IS_PLAYING;
    m_activated = true;
    return true;
}

clap_process_status XAPGraph::process(const clap_process *process) noexcept
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
            choc::sorting::stable_sort(eventMergeVector.begin(), eventMergeVector.end(),
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
