#include "clap_xaudioprocessor.h"
#include <iostream>
#include <stdexcept>

#ifndef JUCE_CORE_H_INCLUDED
WebViewGenericEditor::WebViewGenericEditor(xenakios::XAudioProcessor *xap) : m_xap(xap)
{
    m_webview = std::make_unique<choc::ui::WebView>();
    m_webview->bind("getParameters",
                    [this](const choc::value::ValueView &args) -> choc::value::Value {
                        auto result = choc::value::createEmptyArray();
                        for (int i = 0; i < m_xap->paramsCount(); ++i)
                        {
                            clap_param_info pinfo;
                            m_xap->paramsInfo(i, &pinfo);
                            auto info = choc::value::createObject("paraminfo");
                            info.setMember("name", std::string(pinfo.name));
                            info.setMember("id", (int64_t)pinfo.id);
                            info.setMember("minval", pinfo.min_value);
                            info.setMember("maxval", pinfo.max_value);
                            info.setMember("defaultval", pinfo.default_value);
                            result.addArrayElement(info);
                        }
                        return result;
                    });

    m_webview->navigate(
        R"(C:\develop\AudioPluginHost_mk2\Source\Experimental\clapgenericgui.html)");
}
#endif

ClapPluginFormatProcessor::ClapPluginFormatProcessor(std::string plugfilename, int plugindex)
    : m_plugdll(plugfilename)
{
    mainThreadId = std::this_thread::get_id();
    on_main_thread_fifo.reset(256);
    auto get_extension_lambda = [](const struct clap_host *host, const char *eid) -> const void * {
    // DBG("plugin requested host extension " << eid);
    // std::cout << "plugin requested host extension " << eid << "\n";
#ifdef JUCE_CORE_H_INCLUDED
        if (!strcmp(eid, CLAP_EXT_THREAD_CHECK))
        {
            static clap_host_thread_check ext_thcheck;
            ext_thcheck.is_audio_thread = [](const clap_host *) {
                return !juce::MessageManager::getInstance()->isThisTheMessageThread();
            };
            ext_thcheck.is_main_thread = [](const clap_host *) {
                return juce::MessageManager::getInstance()->isThisTheMessageThread();
            };
            return &ext_thcheck;
        }
#else
        if (!strcmp(eid, CLAP_EXT_THREAD_CHECK))
        {
            static clap_host_thread_check ext_thcheck;
            ext_thcheck.is_audio_thread = [](const clap_host *host_) {
                auto claphost = (ClapPluginFormatProcessor *)host_->host_data;
                return std::this_thread::get_id() != claphost->mainThreadId;
            };
            ext_thcheck.is_main_thread = [](const clap_host *host_) {
                auto claphost = (ClapPluginFormatProcessor *)host_->host_data;
                return std::this_thread::get_id() == claphost->mainThreadId;
            };
            return &ext_thcheck;
        }
#endif
        if (!strcmp(eid, CLAP_EXT_LOG))
        {
            static clap_host_log ext_log;
            ext_log.log = [](const clap_host_t *host_, clap_log_severity severity,
                             const char *msg) {
                auto claphost = (ClapPluginFormatProcessor *)host_->host_data;
                if (claphost->m_host_ext)
                    claphost->m_host_ext->log(msg);
            };
            return nullptr;
            // return &ext_log;
        }
        if (!strcmp(eid, CLAP_EXT_GUI))
        {
            static clap_host_gui ext_gui;
            ext_gui.closed = [](const clap_host *, bool) {};
            ext_gui.request_hide = [](const clap_host *) { return false; };
            ext_gui.request_show = [](const clap_host *) { return false; };
            ext_gui.resize_hints_changed = [](const clap_host *) {};
            ext_gui.request_resize = [](const clap_host *host_, uint32_t w, uint32_t h) {
                auto claphost = (ClapPluginFormatProcessor *)host_->host_data;
                claphost->onPluginRequestedResizeInternal(w, h);
                return true;
            };
            return &ext_gui;
        }
        if (!strcmp(eid, CLAP_EXT_PARAMS))
        {
            static clap_host_params ext_pars;
            ext_pars.clear = [](const clap_host_t *host, clap_id param_id,
                                clap_param_clear_flags flags) {};
            ext_pars.request_flush = [](const clap_host_t *host) {};
            ext_pars.rescan = [](const clap_host_t *host_, clap_param_rescan_flags flags) {
                auto claphost = (ClapPluginFormatProcessor *)host_->host_data;
                claphost->OnPluginRequestedParameterRescan(flags);
            };
            return &ext_pars;
        }
        return nullptr;
    };

    xen_host_info.host_data = this;
    xen_host_info.get_extension = get_extension_lambda;
#ifdef JUCE_CORE_H_INCLUDED
    xen_host_info.request_callback = [](const struct clap_host *host_) {
        auto claphost = (ClapPluginFormatProcessor *)host_->host_data;
        // Note that this is risky because the processor could be
        // destroyed before the callback happens...
        // Also, this might be called from the audio thread, and
        // this is going to make a heap allocation...
        juce::MessageManager::callAsync(
            [claphost]() { claphost->m_plug->on_main_thread(claphost->m_plug); });
    };
#else
    xen_host_info.request_callback = [](const struct clap_host *host_) {
        // std::cout << "plug requested callback on main thread\n";
        auto claphost = (ClapPluginFormatProcessor *)host_->host_data;
        bool suc = claphost->on_main_thread_fifo.push(
            [claphost]() { claphost->m_plug->on_main_thread(claphost->m_plug); });
        if (!suc)
        {
            std::cout << "main thread task fifo ran out of space!\n";
        }
    };
#endif
    xen_host_info.request_process = [](const struct clap_host *host) {};
    xen_host_info.request_restart = [](const struct clap_host *host) {

    };

    if (m_plugdll.handle)
    {
        clap_plugin_entry_t *entry = (clap_plugin_entry_t *)m_plugdll.findFunction("clap_entry");
        if (entry)
        {
            m_entry = entry;
            entry->init(plugfilename.c_str());
            auto fac = (clap_plugin_factory_t *)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
            auto plugin_count = fac->get_plugin_count(fac);
            if (plugin_count <= 0)
            {
                throw std::runtime_error(
                    "No plugins to manufacture, likely an error in the plugin");
            }
            if (plugindex < 0 || plugindex >= plugin_count)
                throw std::runtime_error(
                    std::format("Plugin index {} is out of bounds, allowed range is 0 - {}",
                                plugindex, plugin_count - 1));
            auto desc = fac->get_plugin_descriptor(fac, plugindex);

            auto plug = fac->create_plugin(fac, &xen_host_info, desc->id);
            if (plug)
            {
                if (desc)
                {
                    std::cout << "created : " << desc->name << "\n";
                }
                m_plug = plug;
                m_plug->init(m_plug);
                m_inited = true;
                initParamsExtension();
                initGUIExtension();
                m_ext_state = (clap_plugin_state *)plug->get_extension(plug, CLAP_EXT_STATE);
#ifdef CLAP_PARAM_ORIGIN_AVAILABLE
                m_ext_param_origin =
                    (clap_plugin_param_origin *)plug->get_extension(plug, CLAP_EXT_PARAM_ORIGIN);
#endif
                if (!m_ext_gui && m_ext_params)
                {
                    // create parameter infos for generic GUI
                    auto numparams = m_ext_params->count(m_plug);
                    paramDescriptions.reserve(numparams);
                    using ParamDesc = sst::basic_blocks::params::ParamMetaData;
                    for (int i = 0; i < numparams; ++i)
                    {
                        clap_param_info pinfo;
                        if (m_ext_params->get_info(m_plug, i, &pinfo))
                        {
                            paramDescriptions.push_back(
                                ParamDesc()
                                    .asFloat()
                                    .withRange(pinfo.min_value, pinfo.max_value)
                                    .withName(pinfo.name)
                                    .withDefault(pinfo.default_value)
                                    .withFlags(pinfo.flags)
                                    .withLinearScaleFormatting("")
                                    .withID(pinfo.id));
                        }
                    }
                }
                m_from_generic_editor.reset(2048);
            }
            else
                throw std::runtime_error("could not create clap plugin instance");
        }
        else
            throw std::runtime_error("could not get clap plugin entry point");
    }
    else
        throw std::runtime_error(std::format("could not load clap dll {}", plugfilename));
}

ClapPluginFormatProcessor::~ClapPluginFormatProcessor()
{
    if (m_plug)
    {
        // it would be an error if the processing has not already been stopped at this point!
        assert(!m_processingStarted);
        // if (m_processingStarted)
        //     m_plug->stop_processing(m_plug);
        if (m_activated.load())
            m_plug->deactivate(m_plug);
        m_plug->destroy(m_plug);
        m_plug = nullptr;
    }
    if (m_entry)
    {
        m_entry->deinit();
    }
}

bool ClapPluginFormatProcessor::activate(double sampleRate, uint32_t minFrameCount,
                                         uint32_t maxFrameCount) noexcept
{
    m_host_ext =
        static_cast<IHostExtension *>(GetHostExtension("com.xenakios.xupic-test-extension"));
    if (m_plug)
    {
        if (m_plug->activate(m_plug, sampleRate, minFrameCount, maxFrameCount))
        {

            m_activated = true;
            initParamsExtension();
            m_ext_audio_ports =
                (clap_plugin_audio_ports *)m_plug->get_extension(m_plug, CLAP_EXT_AUDIO_PORTS);
            m_ext_note_ports =
                (clap_plugin_note_ports *)m_plug->get_extension(m_plug, CLAP_EXT_NOTE_PORTS);
            m_ext_state = (clap_plugin_state *)m_plug->get_extension(m_plug, CLAP_EXT_STATE);
            m_ext_plugin_tail = (clap_plugin_tail *)m_plug->get_extension(m_plug, CLAP_EXT_TAIL);
            m_ext_remote_controls = (clap_plugin_remote_controls *)m_plug->get_extension(
                m_plug, CLAP_EXT_REMOTE_CONTROLS);
            m_ext_render_mode =
                (clap_plugin_render *)m_plug->get_extension(m_plug, CLAP_EXT_RENDER);
            return true;
        }
        else
        {
            std::cout << "could not activate plugin!\n";
        }
    }
    return false;
}

clap_process_status ClapPluginFormatProcessor::process(const clap_process *process) noexcept
{
    // everything pointless if we don't have the plugin instance
    assert(m_plug);
    // could maybe just call our own startProcessing here?
    if (!m_processingStarted)
    {
        if (m_plug->start_processing(m_plug))
        {
            m_processingStarted = true;
        }
        else
        {
            // plugin didn't start processing, fatal error
            assert(false);
        }
    }
    // we need a copy to be able to switch the used event list because the passed in process
    // is const
    clap_process processCtxCopy = *process;
    // shenanigans to get parameter changes from the GenericEditor
    // if (m_generic_editor)
    {
        m_eventMergeList.clear();
        processCtxCopy.in_events = m_eventMergeList.clapInputEvents();
        // first insert all GenericEditor events at position 0 in the list
        xenakios::CrossThreadMessage msg;
        while (m_from_generic_editor.pop(msg))
        {
            if (msg.eventType == CLAP_EVENT_PARAM_VALUE)
            {
                auto pev = makeClapParameterValueEvent(0, msg.paramId, msg.value);
                m_eventMergeList.push((const clap_event_header *)&pev);
            }
        }
        // then copy messages from original process context
        auto sz = process->in_events->size(process->in_events);
        for (uint32_t i = 0; i < sz; ++i)
        {
            auto ev = process->in_events->get(process->in_events, i);
            m_eventMergeList.push(ev);
        }
        // we should now have a properly time sorted list for use by the plugin
    }
    return m_plug->process(m_plug, &processCtxCopy);
}
uint32_t ClapPluginFormatProcessor::audioPortsCount(bool isInput) const noexcept
{
    if (!m_ext_audio_ports)
        return 0;
    return m_ext_audio_ports->count(m_plug, isInput);
}
bool ClapPluginFormatProcessor::audioPortsInfo(uint32_t index, bool isInput,
                                               clap_audio_port_info *info) const noexcept
{
    if (!m_ext_audio_ports)
        return false;
    return m_ext_audio_ports->get(m_plug, index, isInput, info);
}
void ClapPluginFormatProcessor::paramsFlush(const clap_input_events *in,
                                            const clap_output_events *out) noexcept
{
    if (m_ext_params)
    {
        // FIXME should flush events from the GenericEditor too
        m_ext_params->flush(m_plug, in, out);
    }
}
bool ClapPluginFormatProcessor::stateSave(const clap_ostream *stream) noexcept
{
    if (!m_ext_state)
        return false;
    return m_ext_state->save(m_plug, stream);
}
bool ClapPluginFormatProcessor::stateLoad(const clap_istream *stream) noexcept
{
    if (!m_ext_state)
        return false;
    // have to trust the hosted plugin does this thread safely...
    return m_ext_state->load(m_plug, stream);
}
bool ClapPluginFormatProcessor::renderSetMode(clap_plugin_render_mode mode) noexcept
{
    if (!m_ext_render_mode)
        return false;
    return m_ext_render_mode->set(m_plug, mode);
}
uint32_t ClapPluginFormatProcessor::remoteControlsPageCount() noexcept
{
    if (!m_ext_remote_controls)
        return 0;
    return m_ext_remote_controls->count(m_plug);
}
bool ClapPluginFormatProcessor::remoteControlsPageGet(uint32_t pageIndex,
                                                      clap_remote_controls_page *page) noexcept
{
    if (!m_ext_remote_controls)
        return false;
    return m_ext_remote_controls->get(m_plug, pageIndex, page);
}
void ClapPluginFormatProcessor::initGUIExtension()
{
    m_ext_gui = (clap_plugin_gui *)m_plug->get_extension(m_plug, CLAP_EXT_GUI);
}
bool ClapPluginFormatProcessor::paramsValueToText(clap_id paramId, double value, char *display,
                                                  uint32_t size) noexcept
{
    if (m_ext_params)
        return m_ext_params->value_to_text(m_plug, paramId, value, display, size);
    return false;
}
bool ClapPluginFormatProcessor::paramsValue(clap_id paramId, double *value) noexcept
{
    if (m_ext_params)
        return m_ext_params->get_value(m_plug, paramId, value);
    return false;
}
bool ClapPluginFormatProcessor::paramsInfo(uint32_t paramIndex,
                                           clap_param_info *info) const noexcept
{
    return m_ext_params->get_info(m_plug, paramIndex, info);
}
void ClapPluginFormatProcessor::initParamsExtension()
{
    if (m_plug)
    {
        if (!m_ext_params)
        {
            m_ext_params = (clap_plugin_params_t *)m_plug->get_extension(m_plug, CLAP_EXT_PARAMS);
        }
    }
}
uint32_t ClapPluginFormatProcessor::tailGet() const noexcept
{
    if (!m_plug || !m_ext_plugin_tail)
        return 0;
    return m_ext_plugin_tail->get(m_plug);
}
void ClapPluginFormatProcessor::deactivate() noexcept
{
    if (m_plug)
    {
        m_plug->deactivate(m_plug);
        m_activated = false;
    }
}
void ClapPluginFormatProcessor::onPluginRequestedResizeInternal(uint32_t w, uint32_t h)
{
    // DBG("Plugin requested to be resized to " << (int)w << " " << (int)h);
    if (OnPluginRequestedResize)
        OnPluginRequestedResize(w, h);
}
bool ClapPluginFormatProcessor::notePortsInfo(uint32_t index, bool isInput,
                                              clap_note_port_info *info) const noexcept
{
    if (!m_ext_note_ports)
        return false;
    return m_ext_note_ports->get(m_plug, index, isInput, info);
}
uint32_t ClapPluginFormatProcessor::notePortsCount(bool isInput) const noexcept
{
    if (!m_ext_note_ports)
        return 0;
    return m_ext_note_ports->count(m_plug, isInput);
}
void ClapPluginFormatProcessor::runMainThreadTasks() noexcept
{
    std::function<void()> task;
    while (on_main_thread_fifo.pop(task))
    {
        task();
    }
}
void ClapPluginFormatProcessor::stopProcessing() noexcept
{
    if (!m_plug)
        return;
    // error if stopProcessing called when already stopped?
    if (m_processingStarted)
    {
        // jassert(m_processingStarted);
        m_plug->stop_processing(m_plug);
        m_processingStarted = false;
    }
}
bool ClapPluginFormatProcessor::startProcessing() noexcept
{
    if (!m_plug)
        return false;
    // it is an error to call startProcessing when processing has already started
    assert(!m_processingStarted);
    if (m_plug->start_processing(m_plug))
    {
        m_processingStarted = true;
        return true;
    }
    // something went wrong in the plugin when starting processing
    assert(false);
    return false;
}
bool ClapPluginFormatProcessor::getDescriptor(clap_plugin_descriptor *desc) const
{
    if (!m_plug)
        return false;
    *desc = *m_plug->desc;
    return true;
}
bool ClapPluginFormatProcessor::guiCreate(const char *api, bool isFloating) noexcept
{
    if (m_ext_gui)
    {
        return m_ext_gui->create(m_plug, "win32", false);
    }
    m_webview_ed = std::make_unique<WebViewGenericEditor>(this);
    return true;
}
void ClapPluginFormatProcessor::guiDestroy() noexcept
{
    if (m_ext_gui)
    {
        m_ext_gui->destroy(m_plug);
    }
    m_webview_ed = nullptr;
}
bool ClapPluginFormatProcessor::guiShow() noexcept
{
    if (m_ext_gui)
    {
        return m_ext_gui->show(m_plug);
    }
    return true;
}
bool ClapPluginFormatProcessor::guiHide() noexcept
{
    if (m_ext_gui)
    {
        return m_ext_gui->hide(m_plug);
    }
    return true;
}
bool ClapPluginFormatProcessor::guiGetSize(uint32_t *width, uint32_t *height) noexcept
{

    if (m_ext_gui)
    {
        return m_ext_gui->get_size(m_plug, width, height);
    }
    *width = 500;
    *height = paramsCount() * 25;
    return true;
}
bool ClapPluginFormatProcessor::guiCanResize() const noexcept
{
    if (m_ext_gui)
    {
        return m_ext_gui->can_resize(m_plug);
    }
    return false;
}
bool ClapPluginFormatProcessor::guiAdjustSize(uint32_t *width, uint32_t *height) noexcept
{
    if (m_ext_gui)
    {
        return m_ext_gui->adjust_size(m_plug, width, height);
    }
    return false;
}
bool ClapPluginFormatProcessor::guiSetSize(uint32_t width, uint32_t height) noexcept
{
    if (m_ext_gui)
    {
        uint32_t w = width;
        uint32_t h = height;
        if (guiAdjustSize(&w, &h))
        {
            return m_ext_gui->set_size(m_plug, w, h);
        }
    }
    return false;
}
bool ClapPluginFormatProcessor::guiSetParent(const clap_window *window) noexcept
{
    if (m_ext_gui)
    {
        clap_window win;
        win.api = "win32";
        win.win32 = window->win32;
        return m_ext_gui->set_parent(m_plug, &win);
    }
    choc::ui::DesktopWindow *dwp = (choc::ui::DesktopWindow *)window->ptr;
    dwp->setContent(m_webview_ed->m_webview->getViewHandle());
    return true;
}
