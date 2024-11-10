#include "claphost.h"
#include "RtAudio.h"
#include "audio/choc_SampleBuffers.h"
#include "clap/audio-buffer.h"
#include "clap/events.h"
#include "clap/ext/audio-ports.h"
#include "clap/ext/note-ports.h"
#include "clap/ext/params.h"
#include "clap/plugin.h"
#include "clap/process.h"

#include "containers/choc_Value.h"
#include "gui/choc_MessageLoop.h"
#include "text/choc_Files.h"
#include "text/choc_JSON.h"

#include "../Xaps/xap_memorybufferplayer.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>
#include "../Xaps/clap_xaudioprocessor.h"

using namespace std::chrono_literals;

std::set<ClapProcessingEngine *> g_engineinstances;

ClapProcessingEngine::ClapProcessingEngine()
{
    g_engineinstances.insert(this);
    m_engineCommandFifo.reset(64);
    choc::ui::setWindowsDPIAwareness();
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
    std::cout << "ClapProcessingengine dtor\n";
    stopStreaming();

    g_engineinstances.erase(this);
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

std::set<ClapProcessingEngine *> &ClapProcessingEngine::getEngines() { return g_engineinstances; }

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
            // std::cout << "created : " << desc.name << "\n";
            chainEntry->name = desc.name;
        }
        else
            std::cout << plugfilename
                      << " does not provide Clap description, likely a bug in the plugin"
                      << std::endl;
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

std::string ClapProcessingEngine::getParameterValueAsText(size_t chainIndex, clap_id parId,
                                                          double val)
{
    checkPluginIndex(chainIndex);
    auto plug = m_chain[chainIndex]->m_proc.get();
    constexpr size_t bufSize = 256;
    char buf[bufSize];
    memset(buf, 0, bufSize);
    if (plug->paramsValueToText(parId, val, buf, bufSize))
    {
        return std::string(buf);
    }
    return std::format("{} *", val);
}

void ClapProcessingEngine::checkPluginIndex(size_t index)
{
    if (index >= m_chain.size())
        throw std::runtime_error(
            std::format("Plugin index {} out of allowed range 0..{}", index, m_chain.size() - 1));
}

std::string ClapProcessingEngine::scanPluginFile(std::filesystem::path plugfilepath)
{
    auto plugfilename = plugfilepath.generic_string();
    if (!std::filesystem::exists(plugfilepath))
        throw std::runtime_error("Plugin file does not exist " + plugfilename);
    auto pluginfoarray = choc::value::createEmptyArray();
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
                throw std::runtime_error("No plugins to manufacture in " + plugfilename);
            }

            for (uint32_t i = 0; i < plugin_count; ++i)
            {
                auto desc = fac->get_plugin_descriptor(fac, i);
                if (desc)
                {
                    auto ob = choc::value::createObject("clap_plugin");
                    ob.setMember("name", std::string(desc->name));
                    ob.setMember("id", std::string(desc->id));
                    ob.setMember("description", std::string(desc->description));
                    ob.setMember("version", std::string(desc->version));
                    ob.setMember("vendor", std::string(desc->vendor));
                    ob.setMember("url", std::string(desc->url));
                    ob.setMember("manual_url", std::string(desc->manual_url));
                    auto featarray = choc::value::createEmptyArray();
                    auto ptr = desc->features;
                    while (ptr)
                    {
                        if (*ptr == nullptr)
                            break;
                        featarray.addArrayElement(std::string(*ptr));
                        ++ptr;
                    }
                    ob.setMember("features", featarray);
                    pluginfoarray.addArrayElement(ob);
                }
            }
            entry->deinit();
        }
        else
            throw std::runtime_error("No plugin entry point");
    }
    else
        throw std::runtime_error("could not open dll for " + plugfilename);
    return choc::json::toString(pluginfoarray, true);
}

template <typename BufType> inline void sanityCheckBuffer(BufType &buf)
{
    for (int j = 0; j < buf.getNumFrames(); ++j)
    {
        for (int k = 0; k < buf.getNumChannels(); ++k)
        {
            float tempsample = buf.getSample(k, j);
            if (std::isnan(tempsample) || std::isinf(tempsample))
            {
                assert(false);
                // tempsample = 0.0f;
                // buf.getSample(k, j) = tempsample;
            }
        }
    }
}

void ClapProcessingEngine::processToFile2(std::string filename, double duration, double samplerate,
                                          int numoutchans)
{
    size_t blockSize = 64;
    for (auto &e : m_chains)
    {
        e->activate(samplerate, blockSize);
    }
    std::thread th([&]() {
        choc::buffer::ChannelArrayBuffer<float> inbuf{2, (unsigned int)blockSize};
        inbuf.clear();
        choc::buffer::ChannelArrayBuffer<float> outbuf{2, (unsigned int)blockSize};
        outbuf.clear();
        choc::buffer::ChannelArrayBuffer<float> mixbuf{2, (unsigned int)blockSize};
        mixbuf.clear();
        choc::audio::WAVAudioFileFormat<true> format;
        choc::audio::AudioFileProperties props;
        props.bitDepth = choc::audio::BitDepth::float32;
        props.numChannels = 2;
        props.sampleRate = samplerate;
        auto writer = format.createWriter(filename, props);
        int outlen = duration * samplerate;
        int outcounter = 0;
        while (outcounter < outlen)
        {
            mixbuf.clear();
            for (auto &e : m_chains)
            {
                e->processAudio(inbuf.getView(), outbuf.getView());
                for (int i = 0; i < blockSize; ++i)
                {
                    for (int j = 0; j < 2; ++j)
                    {
                        mixbuf.getSample(j, i) += outbuf.getSample(j, i);
                    }
                }
            }
            writer->appendFrames(mixbuf.getView());
            outcounter += blockSize;
        }
        for (auto &e : m_chains)
        {
            e->stopProcessing();
        }
    });
    th.join();
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
        if (!c->m_proc->activate(samplerate, procblocksize, procblocksize))
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
            loadStateFromBinaryFile(chainIndex, deferredStateFiles[chainIndex]);
        }
        ++chainIndex;

        // if (!c->m_proc->renderSetMode(CLAP_RENDER_OFFLINE))
        //     std::cout << "was not able to set offline render mode for " << c->name << "\n";
        auto tail = c->m_proc->tailGet() / samplerate;
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
        cp.transport = &transport;

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
                auto proc = m_chain[i]->m_proc.get();
                // sanityCheckBuffer(inputbuffer);
                auto status = m_chain[i]->m_proc->process(&cp);
                if (status == CLAP_PROCESS_ERROR)
                    throw std::runtime_error("Clap processing failed");
                // sanityCheckBuffer(outputbuffers[0]);
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
        std::cout << std::format("finished in {} seconds, {}x realtime\n",
                                 render_duration.count() / 1000.0, rtfactor);

        renderloopfinished = true;
    });

    choc::messageloop::Timer timer{10, [&]() {
                                       for (auto &p : m_chain)
                                       {
                                           p->m_proc->runMainThreadTasks();
                                       }
                                       if (!renderloopfinished)
                                           return true;
                                       choc::messageloop::stop();
                                       return false;
                                   }};
    choc::messageloop::run();
    th.join();
    for (auto &p : m_chain)
    {
        p->m_proc->deactivate();
    }
    // std::cout << "cleaned up after rendering\n";
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

void ClapProcessingEngine::setSuspended(bool b)
{
    std::lock_guard<std::mutex> locker(m_mutex);
    m_isSuspended = b;
}

// note that this method should always be run in a non-main thread, to keep
// Clap plugins happy with their thread checks etc
void ClapProcessingEngine::processAudio(choc::buffer::ChannelArrayView<float> inputBuffer,
                                        choc::buffer::ChannelArrayView<float> outputBuffer)
{
    // assert(m_isPrepared);
    assert(inputBuffer.getNumFrames() == outputBuffer.getNumFrames());
    std::lock_guard<std::mutex> locker(m_mutex);
    if (m_processorsState == ProcState::Idle || m_isSuspended)
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
    assert(procblocksize <= outputbuffers[0].getNumFrames());
    m_clap_process.frames_count = procblocksize;
    clap_event_transport tp;
    memset(&tp, 0, sizeof(clap_event_transport));
    m_clap_process.transport = &tp;
    m_clap_process.in_events = list_in.clapInputEvents();
    m_clap_process.out_events = list_out.clapOutputEvents();
    EngineMessage engMsg;
    bool sendAllNotesOff = false;
    while (m_engineCommandFifo.pop(engMsg))
    {
        if (engMsg.opcode == EngineMessage::Opcode::AllNotesOff)
            sendAllNotesOff = true;
        else if (engMsg.opcode == EngineMessage::Opcode::MainVolume)
            m_mainGain = xenakios::decibelsToGain(engMsg.farg0);
    }
    ClapEventSequence::Event msg;
    while (m_realtime_messages_to_plugins.pop(msg))
    {
        ClapEventSequence::Event msgcopy{msg};
        msgcopy.timestamp = msg.timestamp + m_samplePlayPos;
        m_delayed_messages.push_back(msgcopy);
    }
    for (size_t i = 0; i < m_chain.size(); ++i)
    {
        if (sendAllNotesOff)
        {
            for (int chan = 0; chan < 16; ++chan)
            {
                for (int key = 0; key < 128; ++key)
                {
                    auto nev =
                        xenakios::make_event_note(0, CLAP_EVENT_NOTE_OFF, -1, chan, key, -1, 0.0);
                    list_in.push((const clap_event_header *)&nev);
                }
            }
        }
        // need to optimize this
        for (auto &dm : m_delayed_messages)
        {
            if (dm.extdata0 == i && NumericRange<int64_t>(m_samplePlayPos)
                                        .withLength(procblocksize)
                                        .contains(dm.timestamp))
            {
                dm.timestamp = -1.0;
                list_in.push((clap_event_header_t *)&dm.event);
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
        choc::buffer::copy(inputbuffers[0], outputbuffers[0]);
    }
    choc::buffer::copy(outputBuffer, outputbuffers[0]);
    choc::buffer::applyGain(outputBuffer, m_mainGain);
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
        fobuf[i * 2 + 0] = std::clamp(fobuf[i * 2 + 0], -1.0f, 1.0f);
        fobuf[i * 2 + 1] = std::clamp(fobuf[i * 2 + 1], -1.0f, 1.0f);
    }
    cpe.m_timerPosSamples += nFrames;
    if (cpe.m_timerPosSamples >= 44100)
    {
        cpe.m_timerPosSamples = 0;
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
            // std::cout << c->name << " has tail of " << tail << " seconds\n";
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
}

void ClapProcessingEngine::startStreaming(std::optional<unsigned int> id, double sampleRate,
                                          int preferredBufferSize, bool blockExecution)
{
    m_delayed_messages.reserve(512);
    m_delayed_messages.clear();

    m_realtime_messages_to_plugins.reset(512);
    RtAudio::StreamParameters outpars;
    if (id)
        outpars.deviceId = *id;
    else
        outpars.deviceId = m_rtaudio->getDefaultOutputDevice();
    outpars.firstChannel = 0;
    outpars.nChannels = 2;
    unsigned int bframes = preferredBufferSize;
    auto err = m_rtaudio->openStream(&outpars, nullptr, RTAUDIO_FLOAT32, sampleRate, &bframes,
                                     CPECallback, this, nullptr);
    prepareToPlay(sampleRate, bframes);
    outputConversionBuffer = choc::buffer::ChannelArrayBuffer<float>{2, bframes};
    outputConversionBuffer.clear();
    inputConversionBuffer = choc::buffer::ChannelArrayBuffer<float>{2, bframes};
    inputConversionBuffer.clear();
    if (err != RTAUDIO_NO_ERROR)
    {
        throw std::runtime_error("Error opening RTAudio stream");
    }
    err = m_rtaudio->startStream();
    if (err != RTAUDIO_NO_ERROR)
    {
        throw std::runtime_error("Error starting RTAudio stream");
    }
    std::cout << "opened " << m_rtaudio->getDeviceInfo(outpars.deviceId).name << "\n";
    if (blockExecution)
    {
        choc::messageloop::initialise();
        choc::messageloop::run();
    }
}

void ClapProcessingEngine::setMainVolume(double decibels)
{
    decibels = std::clamp(decibels, -100.0, 12.0);
    m_engineCommandFifo.push({EngineMessage::Opcode::MainVolume, 0, decibels});
}

void ClapProcessingEngine::allNotesOff()
{
    m_engineCommandFifo.push({EngineMessage::Opcode::AllNotesOff});
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
    if (m_rtaudio->isStreamRunning())
        m_rtaudio->stopStream();
    if (m_rtaudio->isStreamOpen())
        m_rtaudio->closeStream();
    if (m_delayed_messages.size() > 0)
        std::cout << m_delayed_messages.size() << " delayed messages left\n";
    m_gui_tasks_timer.clear();
}

ProcessorChain &ClapProcessingEngine::addChain()
{
    m_chains.push_back(std::make_unique<ProcessorChain>(m_chains.size()));
    return *m_chains.back();
}

ProcessorChain &ClapProcessingEngine::getChain(size_t index) { return *m_chains[index]; }

void ClapProcessingEngine::postParameterMessage(int destination, double delay, clap_id parid,
                                                double value)
{
    if (destination < 0 || destination >= m_chain.size())
        throw std::runtime_error(std::format("Destination index {} outside allowed range 0..{}",
                                             destination, m_chain.size() - 1));
    auto ev = xenakios::make_event_param_value(0, parid, value, nullptr);
    m_realtime_messages_to_plugins.push(
        ClapEventSequence::Event(delay * m_samplerate, &ev, destination));
}

void ClapProcessingEngine::postNoteMessage(int destination, double delay, double duration, int key,
                                           double velo)
{
    if (destination < 0 || destination >= m_chain.size())
        throw std::runtime_error(std::format("Destination index {} outside allowed range 0..{}",
                                             destination, m_chain.size() - 1));
    double sr = m_samplerate;
    auto ev = xenakios::make_event_note(0, CLAP_EVENT_NOTE_ON, -1, 0, key, -1, velo);
    m_realtime_messages_to_plugins.push(ClapEventSequence::Event(delay * sr, &ev, destination));
    ev = xenakios::make_event_note(0, CLAP_EVENT_NOTE_OFF, -1, 0, key, -1, velo);
    m_realtime_messages_to_plugins.push(
        ClapEventSequence::Event((delay + duration) * sr, &ev, destination));
}

void ClapProcessingEngine::saveStateToBinaryFile(size_t chainIndex,
                                                 const std::filesystem::path &filepath)
{
    checkPluginIndex(chainIndex);
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
    checkPluginIndex(chainIndex);
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
    checkPluginIndex(chainIndex);
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
    clap_event_transport ev;
    ev.header.flags = 0;
    ev.header.time = 0;
    ev.header.size = sizeof(clap_event_transport);
    ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    ev.header.type = CLAP_EVENT_TRANSPORT;
    ev.tempo = tempo;
    ev.tsig_denom = 4;
    ev.tsig_num = 4;
    ev.flags = CLAP_TRANSPORT_HAS_TEMPO;
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

std::string ClapProcessingEngine::getParametersAsJSON(size_t chainIndex)
{
    if (chainIndex >= m_chain.size())
        throw std::runtime_error("Chain index out of bounds");
    auto paramsarray = choc::value::createEmptyArray();
    auto plug = m_chain[chainIndex]->m_proc.get();
    // Should not activate if already activated...but we don't have a consistent way to track that
    // yet. But we do need to activate for plugins that fill in the up to date parameter info
    // only after activation.
    plug->activate(44100.0, 512, 512);
    for (int i = 0; i < plug->paramsCount(); ++i)
    {
        clap_param_info info;
        if (plug->paramsInfo(i, &info))
        {
            auto infoobject = choc::value::createObject("info");
            infoobject.setMember("name", std::string(info.name));
            infoobject.setMember("id", (int64_t)info.id);
            infoobject.setMember("defaultval", info.default_value);
            infoobject.setMember("minval", info.min_value);
            infoobject.setMember("maxval", info.max_value);
            double origin = 0.0;
            if (plug->paramsOrigin(info.id, &origin))
            {
                infoobject.setMember("origin", origin);
            }
            infoobject.setMember("module", std::string(info.module));
            // Can do the needed bit checks in Python, but I suppose would be nice
            // to have some kind of string from the flags too
            infoobject.setMember("flags", (int64_t)info.flags);
            paramsarray.addArrayElement(infoobject);
        }
    }
    // Should not deactivate if already deactivated...
    plug->deactivate();
    return choc::json::toString(paramsarray, true);
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

void ClapProcessingEngine::closePersistentWindow(int chainIndex)
{
    if (chainIndex >= m_chain.size())
        return;
    if (!m_chain[chainIndex]->guiWindow)
        return; // GUI wasn't open
    auto m_plug = m_chain[chainIndex]->m_proc.get();
    m_plug->guiDestroy();
    m_chain[chainIndex]->guiWindow = nullptr;
}

void ClapProcessingEngine::openPersistentWindow(int chainindex)
{
    if (chainindex >= m_chain.size())
        return;
    auto m_plug = m_chain[chainindex]->m_proc.get();
    auto &procEntry = m_chain[chainindex];
    if (procEntry->guiWindow)
        return; // GUI already open
    m_plug->guiCreate("win32", false);
    uint32_t pw = 0;
    uint32_t ph = 0;
    m_plug->guiGetSize(&pw, &ph);
    procEntry->guiWindow =
        std::make_unique<choc::ui::DesktopWindow>(choc::ui::Bounds{100, 100, (int)pw, (int)ph});
    auto m_desktopwindow = procEntry->guiWindow.get();
    m_desktopwindow->setWindowTitle(procEntry->name);
    m_desktopwindow->setResizable(true);
    m_desktopwindow->setMinimumSize(300, 300);
    m_desktopwindow->setMaximumSize(1500, 1200);
    // fails if we later allow rearranging the plugin chain!!
    m_desktopwindow->windowClosed = [this, chainindex] { closePersistentWindow(chainindex); };

    clap_window clapwin;
    clapwin.api = "win32";
    clapwin.win32 = m_desktopwindow->getWindowHandle();
    m_plug->guiSetParent(&clapwin);
    m_plug->guiShow();
    m_desktopwindow->toFront();
}

ProcessorChain::ProcessorChain(std::vector<std::pair<std::string, int>> plugins)
{
    main_thread_id = std::this_thread::get_id();
    for (auto &e : plugins)
    {
        addProcessor(e.first, e.second);
    }
}

void ProcessorChain::addProcessor(std::string plugfilename, int pluginindex)
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
            // std::cout << "created : " << desc.name << "\n";
            chainEntry->name = desc.name;
        }
        else
            std::cout << plugfilename
                      << " does not provide Clap description, likely a bug in the plugin"
                      << std::endl;
        plug->runMainThreadTasks();
        chainEntry->m_proc = std::move(plug);
        m_processors.push_back(std::move(chainEntry));
    }
    else
    {
        throw std::runtime_error("Could not create CLAP plugin");
    }
}

void ProcessorChain::activate(double sampleRate, int maxBlockSize)
{
    inputBuffers.resize(32);
    outputBuffers.resize(32);
    inChannelPointers.resize(64);
    outChannelPointers.resize(64);
    int maxInBuffersNeeded = 0;
    int maxOutBuffersNeeded = 0;
    int maxInPortsNeeded = 0;
    int maxOutPortsNeeded = 0;
    for (auto &e : m_processors)
    {
        e->m_eviter.emplace(e->m_seq, sampleRate);
        e->m_proc->activate(sampleRate, maxBlockSize, maxBlockSize);
        int inChans = 0;
        int numInports = e->m_proc->audioPortsCount(true);
        // if (numInports > inputBuffers.size())
        //    inputBuffers.resize(numInports);
        maxInPortsNeeded = std::max<int>(maxInPortsNeeded, numInports);
        for (int i = 0; i < numInports; ++i)
        {
            clap_audio_port_info pinfo;
            e->m_proc->audioPortsInfo(i, true, &pinfo);
            inChans += pinfo.channel_count;
        }
        maxInBuffersNeeded = std::max<int>(inChans, maxInBuffersNeeded);
        int outChans = 0;
        int numOutports = e->m_proc->audioPortsCount(false);
        // if (numOutports > outputBuffers.size())
        //     outputBuffers.resize(numOutports);
        maxOutPortsNeeded = std::max<int>(maxOutPortsNeeded, numOutports);
        for (int i = 0; i < numOutports; ++i)
        {
            clap_audio_port_info pinfo;
            e->m_proc->audioPortsInfo(i, false, &pinfo);
            outChans += pinfo.channel_count;
        }
        maxOutBuffersNeeded = std::max<int>(outChans, maxOutBuffersNeeded);
    }
    // std::cout << "chain " << id << " needs " << maxInBuffersNeeded << " input buffers" << "\n";
    audioInputData.resize(64 * maxBlockSize);

    for (auto &e : inputBuffers)
    {
        e.channel_count = 0;
        e.constant_mask = 0;
        e.data32 = inChannelPointers.data();
        e.data64 = nullptr;
        e.latency = 0;
    }
    for (auto &e : outputBuffers)
    {
        e.channel_count = 0;
        e.constant_mask = 0;
        e.data32 = outChannelPointers.data();
        e.data64 = nullptr;
        e.latency = 0;
    }
    // std::cout << "chain " << id << " needs " << maxOutBuffersNeeded << " output buffers" << "\n";
    audioOutputData.resize(64 * maxBlockSize);
    blockSize = maxBlockSize;
    eventIterator.emplace(chainSequence, sampleRate);
    chainGainSmoother.setParams(1.0f, 1.0f, sampleRate);
}

void ProcessorChain::stopProcessing()
{
    auto task = [this]() {
        for (auto &e : m_processors)
        {
            e->m_proc->stopProcessing();
        }
    };
    if (std::this_thread::get_id() == main_thread_id)
    {
        auto fut = thpool.submit_task(task);
        fut.wait();
    }
    else
    {
        task();
    }
}

void ProcessorChain::processAudio(choc::buffer::ChannelArrayView<float> inputBuffer,
                                  choc::buffer::ChannelArrayView<float> outputBuffer)
{
    if (!isProcessing)
    {
        for (auto &e : m_processors)
        {
            e->m_proc->startProcessing();
        }
        isProcessing = true;
    }
    clap_process cp;
    memset(&cp, 0, sizeof(clap_process));
    cp.frames_count = outputBuffer.getNumFrames();
    cp.in_events = inEventList.clapInputEvents();
    cp.out_events = outEventList.clapOutputEvents();
    cp.audio_inputs_count = 1;
    inputBuffers[0].channel_count = 2;
    inputBuffers[0].data32[0] = getInputBuffer(0);
    inputBuffers[0].data32[1] = getInputBuffer(1);
    cp.audio_inputs = inputBuffers.data();

    cp.audio_outputs_count = 1;
    outputBuffers[0].channel_count = 2;
    outputBuffers[0].data32[0] = getOutputBuffer(0);
    outputBuffers[0].data32[1] = getOutputBuffer(1);
    cp.audio_outputs = outputBuffers.data();
    inEventList.clear();
    outEventList.clear();
    for (int j = 0; j < cp.frames_count; ++j)
    {
        cp.audio_inputs[0].data32[0][j] = inputBuffer.getSample(0, j);
        cp.audio_inputs[0].data32[1][j] = inputBuffer.getSample(0, j);
    }
    for (size_t i = 0; i < m_processors.size(); ++i)
    {
        auto procEntry = m_processors[i].get();
        auto proc = procEntry->m_proc.get();
        auto eventSpan = procEntry->m_eviter->readNextEvents(cp.frames_count);
        for (auto &cev : eventSpan)
        {
            auto evcopy = cev.event;
            evcopy.header.time = (cev.timestamp * currentSampleRate) - samplePosition;
            inEventList.push((clap_event_header *)&evcopy);
        }
        proc->process(&cp);
        for (int j = 0; j < cp.frames_count; ++j)
        {
            cp.audio_inputs[0].data32[0][j] = cp.audio_outputs[0].data32[0][j];
            cp.audio_inputs[0].data32[1][j] = cp.audio_outputs[0].data32[1][j];
        }
    }

    auto eventSpan = eventIterator->readNextEvents(cp.frames_count);
    for (auto &cev : eventSpan)
    {
        if (cev.event.header.type == CLAP_EVENT_PARAM_VALUE)
        {
            auto pev = (clap_event_param_value *)&cev.event;
            if (pev->param_id == (clap_id)ChainParameters::Volume)
                chainGain = xenakios::decibelsToGain(pev->value);
            if (pev->param_id == (clap_id)ChainParameters::Mute)
                muted = pev->value >= 0.5;
        }
    }
    float actualGain = chainGain;
    if (muted)
        actualGain = 0.0f;
    for (int i = 0; i < cp.frames_count; ++i)
    {
        auto smoothedGain = chainGainSmoother.step(actualGain);
        outputBuffer.getSample(0, i) = cp.audio_outputs[0].data32[0][i] * smoothedGain;
        outputBuffer.getSample(1, i) = cp.audio_outputs[0].data32[1][i] * smoothedGain;
    }
    samplePosition += cp.frames_count;
}