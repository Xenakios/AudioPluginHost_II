#include "offlineclaphost.h"

using namespace std::chrono_literals;

ClapProcessingEngine::ClapProcessingEngine(std::string plugfilename, int plugindex)
{

    m_to_plugin_thread_fifo.reset(64);
    m_from_plugin_thread_fifo.reset(64);
    bool plugin_in_another_thread = false;
    if (plugin_in_another_thread)
    {

        Message msg;
        msg.op = Message::Op::CreatePlugin;
        msg.action = plugfilename;
        msg.idata = plugindex;
        m_to_plugin_thread_fifo.push(msg);
        m_plugin_thread = std::make_unique<std::thread>([this]() {
            std::cout << "start plugin thread\n";
            // ClapPluginFormatProcessor::mainthread_id() = std::this_thread::get_id();
            while (true)
            {
                Message msg;
                while (m_to_plugin_thread_fifo.pop(msg))
                {
                    if (msg.op == Message::Op::EndThread)
                    {
                        return;
                    }
                    if (msg.op == Message::Op::DestroyPlugin)
                    {
                        std::cout << "Message::Op::DestroyPlugin\n";
                        m_plug = nullptr;
                        return;
                    }
                    if (msg.op == Message::Op::CreatePlugin)
                    {
                        std::cout << "Message::Op::CreatePlugin\n";
                        m_plug = std::make_unique<ClapPluginFormatProcessor>(msg.action, msg.idata);
                        if (m_plug)
                        {
                            Message omsg;
                            omsg.op = Message::Op::CreationSucceeded;
                            m_from_plugin_thread_fifo.push(omsg);
                        }
                        else
                        {
                            Message omsg;
                            omsg.op = Message::Op::CreationFailed;
                            m_from_plugin_thread_fifo.push(omsg);
                            return;
                        }
                    }
                }
                std::this_thread::sleep_for(10ms);
            }
            std::cout << "end plugin thread\n";
        });
        processMessagesFromPluginBlocking();
        return;
    }
    else
    {
        // ClapPluginFormatProcessor::mainthread_id() = std::this_thread::get_id();
        m_plug = std::make_unique<ClapPluginFormatProcessor>(plugfilename, plugindex);
        if (m_plug)
        {
            clap_plugin_descriptor desc;
            if (m_plug->getDescriptor(&desc))
            {
                std::cout << "created : " << desc.name << "\n";
            }
        }
        else
            throw std::runtime_error("Could not create CLAP plugin");
    }
}

void ClapProcessingEngine::processMessagesFromPluginBlocking()
{
    bool hadMessages = false;
    while (!hadMessages)
    {
        Message imsg;
        while (m_from_plugin_thread_fifo.pop(imsg))
        {
            hadMessages = true;
            if (imsg.op == Message::Op::CreationFailed)
            {
                m_plugin_thread->join();
                m_plugin_thread = nullptr;
                throw std::runtime_error("Could not create CLAP plugin");
                return;
            }
            if (imsg.op == Message::Op::CreationSucceeded)
            {
                clap_plugin_descriptor desc;
                if (m_plug->getDescriptor(&desc))
                {
                    std::cout << "created : " << desc.name << "\n";
                }
                return;
            }
        }
        std::this_thread::sleep_for(10ms);
    }
}

void ClapProcessingEngine::processToFile(std::string filename, double duration, double samplerate)
{
    using namespace std::chrono_literals;
    m_seq.sortEvents();
    int procblocksize = 64;
    std::atomic<bool> renderloopfinished{false};
    m_plug->activate(samplerate, procblocksize, procblocksize);

    // even offline, do the processing in another another thread because things
    // can get complicated with plugins like Surge XT because of the thread checks
    std::thread th([&] {
        ClapEventSequence::Iterator eviter(m_seq);
        clap_process cp;
        memset(&cp, 0, sizeof(clap_process));
        cp.frames_count = procblocksize;
        cp.audio_inputs_count = 1;
        choc::buffer::ChannelArrayBuffer<float> inputbuffer{2, (unsigned int)procblocksize};
        inputbuffer.clear();
        clap_audio_buffer inbufs[1];
        inbufs[0].channel_count = 2;
        inbufs[0].constant_mask = 0;
        inbufs[0].latency = 0;
        auto ichansdata = inputbuffer.getView().data.channels;
        inbufs[0].data32 = (float **)ichansdata;
        cp.audio_inputs = inbufs;

        cp.audio_outputs_count = 1;
        choc::buffer::ChannelArrayBuffer<float> outputbuffer{2, (unsigned int)procblocksize};
        outputbuffer.clear();
        clap_audio_buffer outbufs[1];
        outbufs[0].channel_count = 2;
        outbufs[0].constant_mask = 0;
        outbufs[0].latency = 0;
        auto chansdata = outputbuffer.getView().data.channels;
        outbufs[0].data32 = (float **)chansdata;
        cp.audio_outputs = outbufs;

        clap_event_transport transport;
        memset(&transport, 0, sizeof(clap_event_transport));
        cp.transport = &transport;
        transport.tempo = 120;
        transport.header.flags = 0;
        transport.header.size = sizeof(clap_event_transport);
        transport.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        transport.header.time = 0;
        transport.header.type = CLAP_EVENT_TRANSPORT;
        transport.flags = CLAP_TRANSPORT_IS_PLAYING;

        clap::helpers::EventList list_in;
        clap::helpers::EventList list_out;
        cp.in_events = list_in.clapInputEvents();
        cp.out_events = list_out.clapOutputEvents();

        m_plug->startProcessing();
        int outcounter = 0;
        int outlensamples = duration * samplerate;
        choc::audio::AudioFileProperties outfileprops;
        outfileprops.formatName = "WAV";
        outfileprops.bitDepth = choc::audio::BitDepth::float32;
        outfileprops.numChannels = 2;
        outfileprops.sampleRate = samplerate;
        choc::audio::WAVAudioFileFormat<true> wavformat;
        auto writer = wavformat.createWriter(filename, outfileprops);
        eviter.setTime(0.0);
        int blockcount = 0;
        using clock = std::chrono::system_clock;
        using ms = std::chrono::duration<double, std::milli>;
        const auto start_time = clock::now();
        while (outcounter < outlensamples)
        {
            auto blockevts = eviter.readNextEvents(procblocksize / samplerate);
            for (auto &e : blockevts)
            {
                auto ecopy = e.event;
                ecopy.header.time = (e.timestamp * samplerate) - outcounter;
                list_in.push((const clap_event_header *)&ecopy);
                // std::cout << "sent event type " << e.event.header.type << " at samplepos "
                //           << outcounter + ecopy.header.time << "\n";
            }

            auto status = m_plug->process(&cp);
            if (status == CLAP_PROCESS_ERROR)
                throw std::runtime_error("Clap processing failed");
            list_out.clear();
            list_in.clear();
            uint32_t framesToWrite = std::min(outlensamples - outcounter, procblocksize);
            auto writeSectionView =
                outputbuffer.getSection(choc::buffer::ChannelRange{0, 2}, {0, framesToWrite});
            writer->appendFrames(writeSectionView);
            // std::cout << outcounter << " ";
            cp.steady_time = outcounter;

            outcounter += procblocksize;
        }
        const ms duration = clock::now() - start_time;
        m_plug->stopProcessing();
        writer->flush();
        std::cout << "finished in " << duration.count() / 1000.0 << " seconds\n";
        renderloopfinished = true;
    });
    using namespace std::chrono_literals;
    // fake event loop to flush the on main thread requests from the plugin
    while (!renderloopfinished)
    {
        m_plug->runMainThreadTasks();
        std::this_thread::sleep_for(5ms);
    }
    th.join();
}

void ClapProcessingEngine::saveStateToFile(std::string filename)
{
    std::ofstream outstream(filename, std::ios::binary);
    clap_ostream clapostream;
    clapostream.ctx = &outstream;
    clapostream.write = [](const clap_ostream *stream, const void *buffer, uint64_t size) {
        auto os = (std::ofstream *)stream->ctx;
        auto castbuf = (unsigned char *)buffer;
        std::cout << "asked to write " << size << " bytes\n";
        for (int i = 0; i < size; ++i)
        {
            (*os) << castbuf[i];
        }
        return (int64_t)size;
    };
    m_plug->stateSave(&clapostream);
}

void ClapProcessingEngine::loadStateFromFile(std::string filename)
{
    std::ifstream infilestream(filename, std::ios::binary);
    if (!infilestream.is_open())
        return;
    clap_istream clapisteam;
    clapisteam.ctx = &infilestream;
    // int64_t(CLAP_ABI *read)(const struct clap_istream *stream, void *buffer, uint64_t size);
    clapisteam.read = [](const clap_istream *stream, void *buffer, uint64_t size) {
        auto is = (std::ifstream *)stream->ctx;
        if (is->eof())
            return (int64_t)0;
        auto castbuf = (unsigned char *)buffer;
        std::cout << "\nasked to read " << size << " bytes\n";
        for (int i = 0; i < size; ++i)
        {
            unsigned char c = 0;
            (*is) >> c;
            castbuf[i] = c;
            // std::cout << (char)c;
            if (is->eof())
                return (int64_t)(i - 1);
        }

        return (int64_t)size;
    };
    // m_plug->activate(44100.0,512,512);
    if (m_plug->stateLoad(&clapisteam))
    {
        clap::helpers::EventList inlist;
        clap::helpers::EventList outlist;
        // m_plug->paramsFlush(inlist.clapInputEvents(), outlist.clapOutputEvents());
    }
    else
    {
        std::cout << "failed to set clap state\n";
    }
    // m_plug->deactivate();
}

std::string ClapProcessingEngine::getParameterInfoString(size_t index)
{
    std::string result;
    result.reserve(128);
    if (index >= 0 && m_plug->paramsCount())
    {
        clap_param_info pinfo;
        if (m_plug->paramsInfo(index, &pinfo))
        {
            result += std::string(pinfo.module) + std::string(pinfo.name) + " (ID " +
                      std::to_string(pinfo.id) + ")\n";
            result += "\trange [" + std::to_string(pinfo.min_value) + " - " +
                      std::to_string(pinfo.max_value) + "]\n\t";
            if (pinfo.flags & CLAP_PARAM_IS_AUTOMATABLE)
            {
                result += "Automatable";
            }
            if (pinfo.flags & CLAP_PARAM_IS_MODULATABLE)
            {
                result += "/Modulatable";
            }
            if (pinfo.flags & CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID)
            {
                result += "/PerNoteID";
            }
            if (pinfo.flags & CLAP_PARAM_IS_STEPPED)
            {
                result += "/Stepped\n";
                int numsteps = pinfo.max_value - pinfo.min_value;
                if (numsteps >= 2 && numsteps < 64)
                {
                    // result += "/[";
                    char tbuf[256];
                    for (int i = 0; i < numsteps; ++i)
                    {
                        double v = pinfo.min_value + i;
                        if (m_plug->paramsValueToText(pinfo.id, v, tbuf, 256))
                        {
                            result += "\t\t" + std::string(tbuf);
                            result += "\n";
                        }
                    }
                }
            }
        }
    }
    return result;
}