#include "offlineclaphost.h"
#include "RtAudio.h"
#include "audio/choc_SampleBuffers.h"
#include "clap/audio-buffer.h"
#include "clap/events.h"
#include "clap/ext/audio-ports.h"
#include "clap/ext/note-ports.h"
#include "clap/plugin.h"
#include "clap/process.h"
#include "clap_eventsequence.h"
#include "gui/choc_MessageLoop.h"
#include "text/choc_Files.h"
#include "xap_utils.h"
#include "xaps/xap_memorybufferplayer.h"
#include <cstring>
#include <format>
#include <stdexcept>
#include <vector>
#include "xaps/clap_xaudioprocessor.h"

using namespace std::chrono_literals;

ClapProcessingEngine::ClapProcessingEngine()
{
    choc::messageloop::initialise();
    m_rtaudio = std::make_unique<RtAudio>();
    outputbuffers.resize(32);
    inputbuffers.resize(32);
    for (int i = 0; i < 32; ++i)
    {
        memset(&m_clap_outbufs[i], 0, sizeof(clap_audio_buffer));
        memset(&m_clap_inbufs[i], 0, sizeof(clap_audio_buffer));
    }
}

ClapProcessingEngine::~ClapProcessingEngine()
{
    size_t numUnhandled = 0;
    for (auto &ce : m_chain)
    {
        if (auto cp = dynamic_cast<ClapPluginFormatProcessor *>(ce->m_proc.get()))
        {
            if (cp->on_main_thread_fifo.getUsedSlots() > 0)
            {
                ++numUnhandled;
            }
        }
    }
    if (numUnhandled > 0)
    {
        std::cout << numUnhandled << " unhandled main thread call requests from Clap plugins\n";
    }
}

void ClapProcessingEngine::addProcessorToChain(std::string plugfilename, int pluginindex)
{
    std::unique_ptr<xenakios::XAudioProcessor> plug;
    if (plugfilename == "XenakiosMemoryBufferPlayer")
    {
        plug = std::make_unique<XapMemoryBufferPlayer>();
    }
    else
    {
        plug = std::make_unique<ClapPluginFormatProcessor>(plugfilename, pluginindex);
    }
    if (plug)
    {
        auto chainEntry = std::make_unique<ProcessorEntry>();
        clap_plugin_descriptor desc;
        if (plug->getDescriptor(&desc))
        {
            std::cout << "created : " << desc.name << "\n";
            chainEntry->name = desc.name;
        }
        plug->runMainThreadTasks();
        chainEntry->m_proc = std::move(plug);
        m_chain.push_back(std::move(chainEntry));
    }
    else
    {
        throw std::runtime_error("Could not create CLAP plugin");
    }
}

void ClapProcessingEngine::removeProcessorFromChain(int index)
{
    if (index >= 0 && index < m_chain.size())
    {
        m_chain.erase(m_chain.begin() + index);
    }
    else
    {
        throw std::runtime_error("Processor index out of range");
    }
}

std::vector<std::filesystem::path> ClapProcessingEngine::scanPluginDirectories()
{
    std::vector<std::filesystem::path> result;
    // actually there are more paths we should scan...and make this cross platform
    std::filesystem::recursive_directory_iterator dirit(R"(C:\Program Files\Common Files\CLAP)");
    for (auto &f : dirit)
    {
        if (f.is_regular_file() && f.path().extension() == ".clap")
        {
            result.push_back(f.path());
        }
    }
    return result;
}

std::string ClapProcessingEngine::scanPluginFile(std::filesystem::path plugfilepath)
{
    auto plugfilename = plugfilepath.generic_string();
    if (!std::filesystem::exists(plugfilepath))
        return "Plugin file does not exist " + plugfilename;

    choc::file::DynamicLibrary dll(plugfilename);
    if (dll.handle)
    {
        clap_plugin_entry_t *entry = (clap_plugin_entry_t *)dll.findFunction("clap_entry");
        if (entry)
        {
            entry->init(plugfilename.c_str());
            auto fac = (clap_plugin_factory_t *)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
            auto plugin_count = fac->get_plugin_count(fac);
            if (plugin_count <= 0)
            {
                return "No plugins to manufacture in " + plugfilename;
            }
            std::string result;
            for (uint32_t i = 0; i < plugin_count; ++i)
            {
                auto desc = fac->get_plugin_descriptor(fac, i);
                if (desc)
                {
                    result += std::to_string(i) + " : " + std::string(desc->name) + " [" +
                              desc->id + "]\n";
                }
            }
            entry->deinit();
            return result;
        }
        else
            return "No plugin entry point";
    }
    else
        return "could not open dll for " + plugfilename;
    return "No info (should not happen)";
}

void ClapProcessingEngine::processToFile(std::string filename, double duration, double samplerate,
                                         int numoutchans)
{
    if (m_chain.size() == 0)
        throw std::runtime_error("There are no plugins in the chain to process");
    int procblocksize = 64;
    std::atomic<bool> renderloopfinished{false};
    double maxTailSeconds = 0.0;
    size_t chainIndex = 0;
    for (auto &c : m_chain)
    {
        c->m_seq.sortEvents();
        std::cout << std::format("{} has {} audio output ports\n", c->name,
                                 c->m_proc->audioPortsCount(false));
        for (int i = 0; i < c->m_proc->audioPortsCount(false); ++i)
        {
            clap_audio_port_info_t apinfo;
            c->m_proc->audioPortsInfo(i, false, &apinfo);
            std::cout << std::format("\t{} : {} channels\n", i, apinfo.channel_count);
        }
        if (!c->m_proc->activate(samplerate, procblocksize, procblocksize))
            std::cout << "could not activate " << c->name << "\n";
        if (deferredStateFiles.count(chainIndex))
        {
            loadStateFromBinaryFile(chainIndex, deferredStateFiles[chainIndex]);
        }
        ++chainIndex;

        // if (!c->m_proc->renderSetMode(CLAP_RENDER_OFFLINE))
        //     std::cout << "was not able to set offline render mode for " << c->name << "\n";
        auto tail = c->m_proc->tailGet() / samplerate;
        if (tail > 0.0)
        {
            std::cout << c->name << " has tail of " << tail << " seconds\n";
        }
        maxTailSeconds = std::max(tail, maxTailSeconds);
        c->m_proc->runMainThreadTasks();
    }
    // if more than 15 seconds, it's likely infinite
    maxTailSeconds = std::min(maxTailSeconds, 15.0);
    //  even offline, do the processing in another another thread because things
    //  can get complicated with plugins like Surge XT because of the thread checks
    std::thread th([&] {
        std::vector<ClapEventSequence::IteratorSampleTime> eviters;
        int events_in_seqs = 0;
        for (auto &p : m_chain)
        {
            events_in_seqs += p->m_seq.getNumEvents();
            eviters.emplace_back(p->m_seq, samplerate);
        }
        numoutchans = std::clamp(numoutchans, 1, 256);
        clap_process cp;
        memset(&cp, 0, sizeof(clap_process));
        cp.frames_count = procblocksize;
        cp.audio_inputs_count = 1;
        choc::buffer::ChannelArrayBuffer<float> inputbuffer{(unsigned int)numoutchans,
                                                            (unsigned int)procblocksize};
        inputbuffer.clear();
        clap_audio_buffer inbufs[1];
        inbufs[0].channel_count = numoutchans;
        inbufs[0].constant_mask = 0;
        inbufs[0].latency = 0;
        auto ichansdata = inputbuffer.getView().data.channels;
        inbufs[0].data32 = (float **)ichansdata;
        inbufs[0].data64 = nullptr;
        cp.audio_inputs = inbufs;

        constexpr size_t numports = 1;

        cp.audio_outputs_count = numports;

        for (int i = 0; i < numports; ++i)
        {
            outputbuffers[i] = choc::buffer::ChannelArrayBuffer<float>((unsigned int)2,
                                                                       (unsigned int)procblocksize);
            outputbuffers[i].clear();
            m_clap_outbufs[i].channel_count = numoutchans;
            m_clap_outbufs[i].constant_mask = 0;
            m_clap_outbufs[i].latency = 0;
            auto chansdata = outputbuffers[i].getView().data.channels;

            m_clap_outbufs[i].data32 = (float **)chansdata;

            m_clap_outbufs[i].data64 = nullptr;
        }

        cp.audio_outputs = m_clap_outbufs;

        clap_event_transport transport;
        memset(&transport, 0, sizeof(clap_event_transport));

        transport.tempo = 120;
        transport.tsig_denom = 4;
        transport.tsig_num = 4;
        transport.header.flags = 0;
        transport.header.size = sizeof(clap_event_transport);
        transport.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        transport.header.time = 0;
        transport.header.type = CLAP_EVENT_TRANSPORT;
        transport.flags = CLAP_TRANSPORT_IS_PLAYING;
        cp.transport = nullptr;

        cp.in_events = list_in.clapInputEvents();
        cp.out_events = list_out.clapOutputEvents();
        for (auto &p : m_chain)
        {
            p->m_proc->startProcessing();
        }

        int outcounter = 0;
        int outlensamples = duration * samplerate;

        outfileprops.formatName = "WAV";
        outfileprops.bitDepth = choc::audio::BitDepth::float32;
        outfileprops.numChannels = numoutchans;
        outfileprops.sampleRate = samplerate;
        choc::audio::WAVAudioFileFormat<true> wavformat;
        auto writer = wavformat.createWriter(filename, outfileprops);
        if (!writer)
            throw std::runtime_error("Could not create audio file for writing: " + filename);

        int blockcount = 0;
        using clock = std::chrono::system_clock;
        using ms = std::chrono::duration<double, std::milli>;
        const auto start_time = clock::now();
        int eventssent = 0;
        while (outcounter < outlensamples)
        {
            list_in.clear();
            list_out.clear();
            double pos_seconds = outcounter / samplerate;
            for (size_t i = 0; i < m_chain.size(); ++i)
            {
                auto blockevts = eviters[i].readNextEvents(procblocksize);
                for (auto &e : blockevts)
                {
                    auto ecopy = e.event;
                    ecopy.header.time = (e.timestamp * samplerate) - outcounter;
                    if (ecopy.header.time >= cp.frames_count)
                    {
                        // this should not happen but in case it does, place the event at the last
                        // buffer sample position
                        ecopy.header.time = cp.frames_count - 1;
                    }
                    if (ecopy.header.type == CLAP_EVENT_PARAM_VALUE)
                    {
                        auto aev = (clap_event_param_value *)&ecopy;
                        // std::cout << outcounter / samplerate << "\t" << aev->param_id << "\t"
                        //          << aev->value << "\t"
                        //          << (outcounter + aev->header.time) / samplerate << "\n";
                    }
                    if (ecopy.header.type == CLAP_EVENT_TRANSPORT)
                    {
                        auto tev = (clap_event_transport *)&ecopy;
                        if (tev->flags & CLAP_TRANSPORT_HAS_TEMPO)
                        {
                            std::cout << pos_seconds << " transport event with tempo " << tev->tempo
                                      << "\n";
                        }
                        else
                        {
                            std::cout << pos_seconds
                                      << " transport event with unsupported properties\n";
                        }
                    }
                    list_in.push((const clap_event_header *)&ecopy);
                    ++eventssent;
                }

                auto status = m_chain[i]->m_proc->process(&cp);
                if (status == CLAP_PROCESS_ERROR)
                    throw std::runtime_error("Clap processing failed");
                list_in.clear();
                list_out.clear();
                choc::buffer::copy(inputbuffer, outputbuffers[0]);
            }
            uint32_t framesToWrite = std::min(int(outlensamples - outcounter), procblocksize);
            auto writeSectionView = outputbuffers[0].getSection(
                choc::buffer::ChannelRange{0, (unsigned int)numoutchans}, {0, framesToWrite});
            writer->appendFrames(writeSectionView);
            // std::cout << outcounter << " ";
            cp.steady_time = outcounter;

            outcounter += procblocksize;
        }
        const ms render_duration = clock::now() - start_time;
        double rtfactor = (duration * 1000.0) / render_duration.count();
        for (auto &p : m_chain)
        {
            p->m_proc->stopProcessing();
        }
        writer->flush();
        // std::cout << events_in_seqs << " events in sequecnes, sent " << eventssent
        //          << " events to plugin\n";
        std::cout << std::format("finished in {} seconds, {}x realtime",
                                 render_duration.count() / 1000.0, rtfactor);

        renderloopfinished = true;
    });

    // fake event loop to flush the on main thread requests from the plugin
    while (!renderloopfinished)
    {
        for (auto &p : m_chain)
        {
            p->m_proc->runMainThreadTasks();
        }
        std::this_thread::sleep_for(5ms);
    }
    th.join();
    for (auto &p : m_chain)
    {
        p->m_proc->deactivate();
    }
}

std::vector<std::string> ClapProcessingEngine::getDeviceNames()
{
    auto devices = m_rtaudio->getDeviceIds();
    std::vector<std::string> result;
    for (auto &d : devices)
    {
        auto dinfo = m_rtaudio->getDeviceInfo(d);
        auto str = std::format("{} : {}", d, dinfo.name);
        if (dinfo.isDefaultOutput)
            str += " DEFAULT OUT";
        if (dinfo.isDefaultInput)
            str += " DEFAULT IN)";
        result.push_back(str);
    }
    return result;
}

// note that this method should always be run in a non-main thread, to keep
// Clap plugins happy with their thread checks etc
void ClapProcessingEngine::processAudio(choc::buffer::ChannelArrayView<float> inputBuffer,
                                        choc::buffer::ChannelArrayView<float> outputBuffer)
{
    assert(m_isPrepared);
    assert(inputBuffer.getNumFrames() == outputBuffer.getNumFrames());
    if (m_processorsState == ProcState::Idle)
    {
        outputBuffer.clear();
        return;
    }
    if (m_processorsState == ProcState::NeedsStopping)
    {
        for (auto &c : m_chain)
        {
            c->m_proc->stopProcessing();
        }
        outputBuffer.clear();
        m_processorsState = ProcState::Idle;
        return;
    }
    if (m_processorsState == ProcState::NeedsStarting)
    {
        for (auto &c : m_chain)
        {
            c->m_proc->startProcessing();
        }
        m_processorsState = ProcState::Started;
    }
    list_in.clear();
    list_out.clear();
    int procblocksize = outputBuffer.getNumFrames();
    double pos_seconds = m_transportposSamples / m_samplerate;
    m_clap_process.frames_count = procblocksize;
    m_clap_process.transport = nullptr;
    m_clap_process.in_events = list_in.clapInputEvents();
    m_clap_process.out_events = list_out.clapOutputEvents();
    for (size_t i = 0; i < m_chain.size(); ++i)
    {
        if (i == 0)
        {
            ClapEventSequence::Event msg;
            while (m_to_test_tone_fifo.pop(msg))
            {
                ClapEventSequence::Event msgcopy{msg};
                msgcopy.timestamp = msg.timestamp + m_samplePlayPos;
                m_delayed_messages.push_back(msgcopy);
            }
            for (auto &dm : m_delayed_messages)
            {
                if (dm.timestamp >= m_samplePlayPos &&
                    dm.timestamp < m_samplePlayPos + procblocksize)
                {
                    dm.timestamp = -1.0;
                    // std::cout << "delayed message at " << m_samplePlayPos << "\n";
                    list_in.push((clap_event_header_t *)&dm.event);
                }
            }
        }

        auto blockevts = m_chain[i]->m_eviter->readNextEvents(procblocksize);
        for (auto &e : blockevts)
        {
            auto ecopy = e.event;
            ecopy.header.time = (e.timestamp * m_samplerate) - m_transportposSamples;
            if (ecopy.header.time >= m_clap_process.frames_count)
            {
                // this should not happen but in case it does, place the event at the last
                // buffer sample position
                ecopy.header.time = m_clap_process.frames_count - 1;
            }
            if (ecopy.header.type == CLAP_EVENT_PARAM_VALUE)
            {
                auto aev = (clap_event_param_value *)&ecopy;
                // std::cout << outcounter / samplerate << "\t" << aev->param_id << "\t"
                //          << aev->value << "\t"
                //          << (outcounter + aev->header.time) / samplerate << "\n";
            }
            if (ecopy.header.type == CLAP_EVENT_TRANSPORT)
            {
                auto tev = (clap_event_transport *)&ecopy;
                if (tev->flags & CLAP_TRANSPORT_HAS_TEMPO)
                {
                    std::cout << pos_seconds << " transport event with tempo " << tev->tempo
                              << "\n";
                }
                else
                {
                    std::cout << pos_seconds << " transport event with unsupported properties\n";
                }
            }
            list_in.push((const clap_event_header *)&ecopy);
            // ++eventssent;
        }

        auto status = m_chain[i]->m_proc->process(&m_clap_process);
        // if (status == CLAP_PROCESS_ERROR)
        //     throw std::runtime_error("Clap processing failed");
        list_in.clear();
        list_out.clear();
        // choc::buffer::copy(inputbuffer, outputbuffers[0]);
    }
    choc::buffer::copy(outputBuffer, outputbuffers[0]);
    m_samplePlayPos += procblocksize;
    std::erase_if(m_delayed_messages, [](auto &&dm) { return dm.timestamp < 0.0; });
}

int CPECallback(void *outputBuffer, void *inputBuffer, unsigned int nFrames, double streamTime,
                RtAudioStreamStatus status, void *userData)
{
    float *fobuf = (float *)outputBuffer;
    float *fibuf = (float *)inputBuffer;
    ClapProcessingEngine &cpe = *(ClapProcessingEngine *)userData;
    cpe.processAudio(cpe.inputConversionBuffer, cpe.outputConversionBuffer);
    for (unsigned int i = 0; i < nFrames; ++i)
    {
        fobuf[i * 2 + 0] = cpe.outputConversionBuffer.getSample(0, i);
        fobuf[i * 2 + 1] = cpe.outputConversionBuffer.getSample(1, i);
    }
    cpe.m_timerPosSamples += nFrames;
    if (cpe.m_timerPosSamples >= 44100)
    {
        cpe.m_timerPosSamples = 0;
        // choc::messageloop::postMessage([]() { std::cout << "postmessage callback" << std::endl;
        // });
    }
    return 0;
}

void ClapProcessingEngine::runMainThreadTasks()
{
    for (auto &cp : m_chain)
    {
        cp->m_proc->runMainThreadTasks();
    }
}

// prepare the processing chain here, must be called from main thread
void ClapProcessingEngine::prepareToPlay(double sampleRate, int maxBufferSize)
{
    if (m_chain.size() == 0)
        throw std::runtime_error("There are no plugins in the chain to process");
    int procblocksize = maxBufferSize;
    m_samplerate = sampleRate;
    m_transportposSamples = 0;
    double maxTailSeconds = 0.0;
    size_t chainIndex = 0;
    memset(&m_clap_process, 0, sizeof(clap_process));
    for (auto &c : m_chain)
    {
        c->m_seq.sortEvents();
        if (!c->m_proc->activate(sampleRate, procblocksize, procblocksize))
            std::cout << "could not activate " << c->name << "\n";
        std::cout << std::format("{} has {} audio output ports\n", c->name,
                                 c->m_proc->audioPortsCount(false));
        for (int i = 0; i < c->m_proc->audioPortsCount(false); ++i)
        {
            clap_audio_port_info_t apinfo;
            c->m_proc->audioPortsInfo(i, false, &apinfo);
            std::cout << std::format("\t{} : {} channels\n", i, apinfo.channel_count);
        }

        if (deferredStateFiles.count(chainIndex))
        {
            // loadStateFromBinaryFile(chainIndex, deferredStateFiles[chainIndex]);
        }
        ++chainIndex;

        auto tail = c->m_proc->tailGet() / sampleRate;
        if (tail > 0.0)
        {
            std::cout << c->name << " has tail of " << tail << " seconds\n";
        }
        maxTailSeconds = std::max(tail, maxTailSeconds);
        c->m_proc->runMainThreadTasks();
    }
    // if more than 15 seconds, it's likely infinite
    maxTailSeconds = std::min(maxTailSeconds, 15.0);

    for (auto &p : m_chain)
    {
        p->m_eviter.emplace(p->m_seq, sampleRate);
    }
    m_processorsState = ProcState::NeedsStarting;
    m_isPrepared = true;
    m_clap_process.audio_inputs_count = 1;
    m_clap_inbufs[0].channel_count = 2;
    m_clap_inbufs[0].constant_mask = 0;
    m_clap_inbufs[0].latency = 0;
    m_clap_inbufs[0].data64 = nullptr;

    inputbuffers.clear();
    inputbuffers.emplace_back((unsigned int)2, (unsigned int)maxBufferSize);
    inputbuffers.back().clear();
    m_clap_inbufs[0].data32 = (float **)inputbuffers.back().getView().data.channels;
    m_clap_process.audio_inputs = m_clap_inbufs;

    m_clap_process.audio_outputs_count = 1;
    m_clap_process.audio_outputs = m_clap_outbufs;
    m_clap_outbufs[0].channel_count = 2;
    m_clap_outbufs[0].constant_mask = 0;
    m_clap_outbufs[0].latency = 0;
    m_clap_outbufs[0].data64 = nullptr;
    outputbuffers.clear();
    outputbuffers.emplace_back((unsigned int)2, (unsigned int)maxBufferSize);
    outputbuffers.back().clear();
    m_clap_outbufs[0].data32 = (float **)outputbuffers.back().getView().data.channels;
    /*
    m_gui_tasks_timer = choc::messageloop::Timer{1000, []() {
                                                     std::cout << "choc timer callback"
                                                               << std::endl;
                                                     return true;
                                                 }};
    */
}

void ClapProcessingEngine::startStreaming(unsigned int id, double sampleRate,
                                          int preferredBufferSize, bool blockExecution)
{
    m_delayed_messages.reserve(512);
    m_delayed_messages.clear();

    m_to_test_tone_fifo.reset(512);

    RtAudio::StreamParameters outpars;
    outpars.deviceId = id;
    outpars.firstChannel = 0;
    outpars.nChannels = 2;
    unsigned int bframes = preferredBufferSize;
    auto err = m_rtaudio->openStream(&outpars, nullptr, RTAUDIO_FLOAT32, sampleRate, &bframes,
                                     CPECallback, this, nullptr);
    prepareToPlay(sampleRate, bframes);
    outputConversionBuffer = choc::buffer::ChannelArrayBuffer<float>{2, bframes};
    inputConversionBuffer = choc::buffer::ChannelArrayBuffer<float>{2, bframes};
    if (err != RTAUDIO_NO_ERROR)
    {
        throw std::runtime_error("Error opening RTAudio stream");
    }
    err = m_rtaudio->startStream();
    if (err != RTAUDIO_NO_ERROR)
    {
        throw std::runtime_error("Error starting RTAudio stream");
    }
    if (blockExecution)
    {
        choc::messageloop::initialise();
        choc::messageloop::run();
    }
}

void ClapProcessingEngine::wait(double seconds)
{
    // the std chrono stuff is awful...
    using clock = std::chrono::system_clock;
    using ms = std::chrono::duration<double, std::milli>;
    const auto start_time = clock::now();
    while (true)
    {
        for (auto &ce : m_chain)
        {
            ce->m_proc->runMainThreadTasks();
        }
        const auto now_time = clock::now();
        const ms timediff = now_time - start_time;
        if (timediff.count() >= (seconds * 1000.0))
            break;
        std::this_thread::sleep_for(10ms);
    }
}

void ClapProcessingEngine::stopStreaming()
{
    m_processorsState = ProcState::NeedsStopping;
    // aaaaarrrrggghhh....
    std::this_thread::sleep_for(100ms);
    if (m_rtaudio->isStreamOpen())
        m_rtaudio->stopStream();
    if (m_delayed_messages.size() > 0)
        std::cout << m_delayed_messages.size() << " delayed messages left\n";
    m_gui_tasks_timer.clear();
}

void ClapProcessingEngine::postNoteMessage(double delay, double duration, int key, double velo)
{
    double sr = m_rtaudio->getStreamSampleRate();
    auto ev = xenakios::make_event_note(0, CLAP_EVENT_NOTE_ON, -1, -1, key, -1, velo);
    m_to_test_tone_fifo.push(ClapEventSequence::Event(delay * sr, &ev));
    ev = xenakios::make_event_note(0, CLAP_EVENT_NOTE_OFF, -1, -1, key, -1, velo);
    m_to_test_tone_fifo.push(ClapEventSequence::Event((delay + duration) * sr, &ev));
}

void ClapProcessingEngine::saveStateToJSONFile(size_t chainIndex,
                                               const std::filesystem::path &filepath)
{
    if (chainIndex >= m_chain.size())
        throw std::runtime_error("Chain index out of bounds");
    auto m_plug = m_chain[chainIndex]->m_proc.get();
    clap_plugin_descriptor desc;
    if (m_plug->getDescriptor(&desc))
    {
        auto ob = choc::value::createObject("pluginstate");
        ob.setMember("plugin_id", desc.id);
        std::vector<char> ovec;
        ovec.reserve(4096);

        clap_ostream clapostream;
        clapostream.ctx = &ovec;
        clapostream.write = [](const clap_ostream *stream, const void *buffer, uint64_t size) {
            auto ov = (std::vector<char> *)stream->ctx;
            const char *chars = (const char *)buffer;
            for (int i = 0; i < size; ++i)
                ov->push_back(chars[i]);
            return (int64_t)size;
        };

        if (m_plug->stateSave(&clapostream))
        {
            auto str = choc::base64::encodeToString(ovec.data(), ovec.size());
            ob.setMember("plugin_data", str);
            std::ofstream ofs(filepath);
            choc::json::writeAsJSON(ofs, ob, true);
        }
        else
            throw std::runtime_error("Clap plugin could not save state");
    }
    else
        throw std::runtime_error(
            "Can not store state of plugin that doesn't implement plugin descriptor");
}

void ClapProcessingEngine::loadStateFromJSONFile(size_t chainIndex,
                                                 const std::filesystem::path &filepath)
{
    if (chainIndex >= m_chain.size())
        throw std::runtime_error("Chain index out of bounds");
    auto m_plug = m_chain[chainIndex]->m_proc.get();
    auto str = choc::file::loadFileAsString(filepath.string());
    auto json = choc::json::parse(str);
    if (json.hasObjectMember("plugin_id"))
    {
        auto id = json["plugin_id"].getString();
        clap_plugin_descriptor desc;
        if (m_plug->getDescriptor(&desc))
        {
            std::string myid(desc.id);
            if (myid != id)
                throw std::runtime_error("Plugin ID in state does not match current plugin ID");
        }
        auto datastr = json["plugin_data"].getString();
        using VecWithPos = std::pair<std::vector<uint8_t>, int>;
        VecWithPos datavec;
        datavec.second = 0;
        datavec.first.reserve(65536);

        choc::base64::decode(datastr, [&datavec](uint8_t byte) { datavec.first.push_back(byte); });
        std::cout << "decoded datavec has " << datavec.first.size() << " bytes\n";

        clap_istream clapisteam;
        clapisteam.ctx = &datavec;
        clapisteam.read = [](const clap_istream *stream, void *buffer, uint64_t size) {
            auto vpos = (VecWithPos *)stream->ctx;
            if (vpos->second >= vpos->first.size())
                return (int64_t)0;
            char *bufi = (char *)buffer;
            for (int i = 0; i < size; ++i)
            {
                bufi[i] = vpos->first[vpos->second];
                (*vpos).second++;
                if (vpos->second >= vpos->first.size())
                    return (int64_t)(i + 1);
            }
            return (int64_t)size;
        };
        if (!m_plug->stateLoad(&clapisteam))
            throw std::runtime_error("Plugin could not load state data");
    }
    else
        throw std::runtime_error("File is not json with a plugin_id");
}

void ClapProcessingEngine::saveStateToBinaryFile(size_t chainIndex,
                                                 const std::filesystem::path &filepath)
{
    if (chainIndex >= m_chain.size())
        throw std::runtime_error("Processor index out of bounds");
    auto plug = m_chain[chainIndex]->m_proc.get();
    clap_plugin_descriptor desc;
    if (!plug->getDescriptor(&desc))
    {
        throw std::runtime_error("Can't save state of plugin without descriptor");
    }
    std::ofstream ofs(filepath, std::ios::binary);
    if (ofs.is_open())
    {
        ofs.write("CLAPSTATE   ", 12);
        int version = 0;
        ofs.write((const char *)&version, sizeof(int));
        int len = strlen(desc.id);
        ofs.write((const char *)&len, sizeof(int));
        ofs.write(desc.id, len);
        clap_ostream costream;
        costream.ctx = &ofs;
        // returns the number of bytes written; -1 on write error
        // int64_t(CLAP_ABI *write)(const struct clap_ostream *stream, const void *buffer,
        // uint64_t size);
        costream.write = [](const struct clap_ostream *stream, const void *buffer, uint64_t size) {
            std::cout << "plugin asked to write " << size << " bytes\n";
            auto &ctxofs = *((std::ofstream *)stream->ctx);
            ctxofs.write((const char *)buffer, size);
            return int64_t(size);
        };
        if (plug->stateSave(&costream))
        {
            std::cout << ofs.tellp() << " bytes written to file\n";
        }
    }
}

void ClapProcessingEngine::loadStateFromBinaryFile(size_t chainIndex,
                                                   const std::filesystem::path &filepath)
{
    if (chainIndex >= m_chain.size())
        throw std::runtime_error("Processor index out of bounds");
    auto plug = m_chain[chainIndex]->m_proc.get();
    clap_plugin_descriptor desc;
    if (!plug->getDescriptor(&desc))
    {
        throw std::runtime_error("Can't load state of plugin without descriptor");
    }
    std::ifstream ins(filepath, std::ios::binary);
    if (ins.is_open())
    {
        char magic[13];
        memset(magic, 0, 13);
        ins.read(magic, 12);
        if (strcmp(magic, "CLAPSTATE   "))
        {
            throw std::runtime_error("Invalid magic in file");
        }
        int i = -1;
        ins.read((char *)&i, sizeof(int));
        std::cout << "clap state version " << i << "\n";
        ins.read((char *)&i, sizeof(int));
        std::cout << "clap id len : " << i << "\n";
        std::string id;
        id.resize(i);
        ins.read(id.data(), i);
        std::cout << "clap id : " << id << "\n";
        if (strcmp(desc.id, id.c_str()) != 0)
        {
            throw std::runtime_error(std::format("{} can't load state saved from {}", desc.id, id));
        }
        clap_istream clapistream;
        clapistream.ctx = &ins;
        // returns the number of bytes read; 0 indicates end of file and -1 a read error
        // int64_t(CLAP_ABI *read)(const struct clap_istream *stream, void *buffer, uint64_t
        // size);
        clapistream.read = [](const struct clap_istream *stream, void *buffer, uint64_t size) {
            auto &ctxifstream = *(std::ifstream *)stream->ctx;
            ctxifstream.read((char *)buffer, size);
            // std::cout << "stream gcount " << ctxifstream.gcount() << "\n";
            return int64_t(ctxifstream.gcount());
        };
        plug->stateLoad(&clapistream);
        plug->runMainThreadTasks();
    }
}

void ClapProcessingEngine::openPluginGUIBlocking(size_t chainIndex, bool closeImmediately)
{
    if (chainIndex >= m_chain.size())
        throw std::runtime_error("Chain index out of bounds");
    choc::messageloop::initialise();
    choc::ui::setWindowsDPIAwareness(); // For Windows, we need to tell the OS we're
                                        // high-DPI-aware
    auto m_plug = m_chain[chainIndex]->m_proc.get();
    m_plug->activate(44100.0, 512, 512);
    m_plug->guiCreate("win32", false);
    uint32_t pw = 0;
    uint32_t ph = 0;
    m_plug->guiGetSize(&pw, &ph);
    choc::ui::DesktopWindow window({100, 100, (int)pw, (int)ph});
    window.setWindowTitle("CHOC Window");
    clap_plugin_descriptor desc;
    if (m_plug->getDescriptor(&desc))
    {
        window.setWindowTitle(desc.name);
    }

    window.setResizable(true);
    window.setMinimumSize(300, 300);
    window.setMaximumSize(1500, 1200);
    window.windowClosed = [this, m_plug] {
        m_plug->guiDestroy();
        choc::messageloop::stop();
    };
    clap_window clapwin;
    clapwin.api = "win32";
    clapwin.win32 = window.getWindowHandle();
    m_plug->guiSetParent(&clapwin);
    m_plug->guiShow();
    window.toFront();

    if (closeImmediately)
    {
        choc::messageloop::Timer timer(5000, [this, m_plug]() {
            m_plug->guiDestroy();
            choc::messageloop::stop();
            return false;
        });
    }

    choc::messageloop::run();
    m_plug->deactivate();
}

std::string ClapProcessingEngine::getParameterInfoString(size_t chainIndex, size_t index)
{
    std::string result;
    result.reserve(128);
    auto m_plug = m_chain[chainIndex]->m_proc.get();
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
                if (numsteps >= 1 && numsteps < 64)
                {
                    // result += "/[";
                    char tbuf[256];
                    for (int i = 0; i <= numsteps; ++i)
                    {
                        double v = pinfo.min_value + i;
                        if (m_plug->paramsValueToText(pinfo.id, v, tbuf, 256))
                        {
                            result += "\t\t" + std::string(tbuf);
                            if (i < numsteps)
                                result += "\n";
                        }
                    }
                }
            }
        }
    }
    return result;
}

void ClapEventSequence::addTransportEvent(double time, double tempo)
{
    clap_event_transport ev{.header{.flags = 0,
                                    .time = 0,
                                    .size = sizeof(clap_event_transport),
                                    .space_id = CLAP_CORE_EVENT_SPACE_ID,
                                    .type = CLAP_EVENT_TRANSPORT},
                            .tempo = tempo,
                            .tsig_denom = 4,
                            .tsig_num = 4,
                            .flags = CLAP_TRANSPORT_HAS_TEMPO};
    m_evlist.push_back(Event(time, &ev));
}
void ClapProcessingEngine::setSequence(int targetProcessorIndex, ClapEventSequence seq)
{
    if (targetProcessorIndex >= 0 && targetProcessorIndex < m_chain.size())
    {
        seq.sortEvents();
        m_chain[targetProcessorIndex]->m_seq = seq;
    }
}
ClapEventSequence &ClapProcessingEngine::getSequence(size_t chainIndex)
{
    if (chainIndex >= 0 && chainIndex < m_chain.size())
    {
        return m_chain[chainIndex]->m_seq;
    }
    throw std::runtime_error("Sequence chain index of out bounds");
}
std::map<std::string, clap_id> ClapProcessingEngine::getParameters(size_t chainIndex)
{
    auto m_plug = m_chain[chainIndex]->m_proc.get();
    std::map<std::string, clap_id> result;
    for (size_t i = 0; i < m_plug->paramsCount(); ++i)
    {
        clap_param_info pinfo;
        if (m_plug->paramsInfo(i, &pinfo))
        {
            result[std::string(pinfo.name)] = pinfo.id;
        }
    }
    return result;
}
void ClapProcessingEngine::openPersistentWindow(std::string title)
{
#ifdef FOO17373
    std::thread th([this, title]() {
        OleInitialize(nullptr);
        // m_plug->mainthread_id() = std::this_thread::get_id();
        choc::ui::setWindowsDPIAwareness(); // For Windows, we need to tell the OS we're
                                            // high-DPI-aware
        m_plug->guiCreate("win32", false);
        uint32_t pw = 0;
        uint32_t ph = 0;
        m_plug->guiGetSize(&pw, &ph);
        m_desktopwindow =
            std::make_unique<choc::ui::DesktopWindow>(choc::ui::Bounds{100, 100, (int)pw, (int)ph});

        m_desktopwindow->setWindowTitle("CHOC Window");
        m_desktopwindow->setResizable(true);
        m_desktopwindow->setMinimumSize(300, 300);
        m_desktopwindow->setMaximumSize(1500, 1200);
        m_desktopwindow->windowClosed = [this] {
            m_plug->guiDestroy();
            choc::messageloop::stop();
        };

        clap_window clapwin;
        clapwin.api = "win32";
        clapwin.win32 = m_desktopwindow->getWindowHandle();
        m_plug->guiSetParent(&clapwin);
        m_plug->guiShow();
        m_desktopwindow->toFront();
        choc::messageloop::run();
        m_desktopwindow = nullptr;
        OleUninitialize();
        return;
        // choc::messageloop::initialise();
        OleInitialize(nullptr);
        choc::ui::setWindowsDPIAwareness();
        m_desktopwindow =
            std::make_unique<choc::ui::DesktopWindow>(choc::ui::Bounds{100, 100, 300, 200});
        m_desktopwindow->setWindowTitle(title);
        m_desktopwindow->toFront();
        m_desktopwindow->windowClosed = [this] {
            // std::cout << "window closed\n";
            // using namespace std::chrono_literals;
            // std::this_thread::sleep_for(1000ms);

            choc::messageloop::stop();
        };
        choc::messageloop::run();
        // std::cout << "finished message loop\n";
        m_desktopwindow = nullptr;

        OleUninitialize();
    });
    th.detach();
#endif
}
