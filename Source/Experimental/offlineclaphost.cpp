#include "offlineclaphost.h"
#include "text/choc_Files.h"

using namespace std::chrono_literals;

ClapProcessingEngine::ClapProcessingEngine() {}

ClapProcessingEngine::~ClapProcessingEngine() {}

void ClapProcessingEngine::addProcessorToChain(std::string plugfilename, int pluginindex)
{
    auto plug = std::make_unique<ClapPluginFormatProcessor>(plugfilename, pluginindex);
    if (plug)
    {
        auto chainEntry = std::make_unique<ProcessorEntry>();
        clap_plugin_descriptor desc;
        if (plug->getDescriptor(&desc))
        {
            std::cout << "created : " << desc.name << "\n";
            chainEntry->name = desc.name;
        }
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
            auto plugcount = fac->get_plugin_count(fac);
            for (int i = 0; i < plugcount; ++i)
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
    return "No info";
}

void ClapProcessingEngine::processToFile(std::string filename, double duration, double samplerate)
{
    using namespace std::chrono_literals;

    int procblocksize = 64;
    std::atomic<bool> renderloopfinished{false};
    for (auto &c : m_chain)
    {
        c->m_seq.sortEvents();
        if (!c->m_proc->activate(samplerate, procblocksize, procblocksize))
            std::cout << "could not activate " << c->name << "\n";
        if (!c->m_proc->renderSetMode(CLAP_RENDER_OFFLINE))
            std::cout << "was not able to set offline render mode for " << c->name << "\n";
    }

    // if (!m_stateFileToLoad.empty())
    //     loadStateFromFile(m_stateFileToLoad);
    //  even offline, do the processing in another another thread because things
    //  can get complicated with plugins like Surge XT because of the thread checks
    std::thread th([&] {
        std::vector<ClapEventSequence::Iterator> eviters;
        for (auto &p : m_chain)
        {
            eviters.emplace_back(p->m_seq);
        }

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
        inbufs[0].data64 = nullptr;
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
        outbufs[0].data64 = nullptr;
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
        for (auto &p : m_chain)
        {
            p->m_proc->startProcessing();
        }

        int outcounter = 0;
        int outlensamples = duration * samplerate;
        choc::audio::AudioFileProperties outfileprops;
        outfileprops.formatName = "WAV";
        outfileprops.bitDepth = choc::audio::BitDepth::float32;
        outfileprops.numChannels = 2;
        outfileprops.sampleRate = samplerate;
        choc::audio::WAVAudioFileFormat<true> wavformat;
        auto writer = wavformat.createWriter(filename, outfileprops);
        // eviter.setTime(0.0);
        int blockcount = 0;
        using clock = std::chrono::system_clock;
        using ms = std::chrono::duration<double, std::milli>;
        const auto start_time = clock::now();
        using namespace std::chrono_literals;
        while (outcounter < outlensamples)
        {
            list_in.clear();
            list_out.clear();
            for (size_t i = 0; i < m_chain.size(); ++i)
            {

                auto blockevts = eviters[i].readNextEvents(procblocksize / samplerate);
                for (auto &e : blockevts)
                {
                    auto ecopy = e.event;
                    ecopy.header.time = (e.timestamp * samplerate) - outcounter;
                    list_in.push((const clap_event_header *)&ecopy);
                }

                auto status = m_chain[i]->m_proc->process(&cp);
                if (status == CLAP_PROCESS_ERROR)
                    throw std::runtime_error("Clap processing failed");
                list_in.clear();
                list_out.clear();
                choc::buffer::copy(inputbuffer, outputbuffer);
            }
            uint32_t framesToWrite = std::min(outlensamples - outcounter, procblocksize);
            auto writeSectionView =
                outputbuffer.getSection(choc::buffer::ChannelRange{0, 2}, {0, framesToWrite});
            writer->appendFrames(writeSectionView);
            // std::cout << outcounter << " ";
            cp.steady_time = outcounter;

            outcounter += procblocksize;
        }
        const ms duration = clock::now() - start_time;
        for (auto &p : m_chain)
        {
            p->m_proc->stopProcessing();
        }

        writer->flush();
        std::cout << "finished in " << duration.count() / 1000.0 << " seconds\n";
        renderloopfinished = true;
    });
    using namespace std::chrono_literals;
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

void ClapProcessingEngine::saveStateToFile(size_t chainIndex, const std::filesystem::path &filepath)
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

void ClapProcessingEngine::loadStateFromFile(size_t chainIndex,
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

void ClapProcessingEngine::openPluginGUIBlocking(size_t chainIndex)
{
    if (chainIndex >= m_chain.size())
        throw std::runtime_error("Chain index out of bounds");
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
    clap::helpers::EventList inlist;
    clap::helpers::EventList outlist;
    choc::messageloop::Timer timer(1000, [&inlist, &outlist, this]() {
        // m_plug->paramsFlush(inlist.clapInputEvents(), outlist.clapOutputEvents());
        // std::cout << "plugin outputted " << outlist.size() << " output events\n";
        return true;
    });
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