#pragma once

#include "xaudioprocessor.h"
#include "platform/choc_DynamicLibrary.h"
#include "xap_generic_editor.h"

inline void my_host_par_rescan(const clap_host_t *host, clap_param_rescan_flags flags)
{
    // auto cp = (ClapProcessor*)host->host_data;
    // cp->updateParameterInfos();
    std::cout << "plugin parameters changed, host should rescan\n";
}

inline void my_host_par_clear(const clap_host_t *host, clap_id param_id,
                              clap_param_clear_flags flags)
{
}

inline void my_host_par_request_flush(const clap_host_t *host) {}

inline void request_restart(const struct clap_host *) {}

inline void request_process(const struct clap_host *) {}

inline void request_callback_nop(const struct clap_host *)
{
    std::cout << "nop request_callback\n";
}

class ClapPluginFormatProcessor : public xenakios::XAudioProcessor
{
    choc::file::DynamicLibrary m_plugdll;
    clap_plugin_entry_t *m_entry = nullptr;
    const clap_plugin_t *m_plug = nullptr;
    clap_host xen_host_info{CLAP_VERSION_INIT, nullptr, "XAPHOST", "Xenakios", "no website",
                            "0.0.0",           nullptr, nullptr,   nullptr,    nullptr};
    std::atomic<bool> m_inited{false};
    clap_plugin_params_t *m_ext_params = nullptr;
    bool m_processingStarted = false;
    std::atomic<bool> m_activated{false};
    choc::fifo::SingleReaderSingleWriterFIFO<xenakios::CrossThreadMessage> m_from_generic_editor;

  public:
    std::vector<clap_param_info> m_param_infos;
    bool getDescriptor(clap_plugin_descriptor *desc) const override
    {
        if (!m_plug)
            return false;
        *desc = *m_plug->desc;
        return true;
    }
    ClapPluginFormatProcessor(std::string plugfilename, int plugindex) : m_plugdll(plugfilename)
    {
        auto get_extension_lambda = [](const struct clap_host *host,
                                       const char *eid) -> const void * {
            // DBG("plugin requested host extension " << eid);
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
            return nullptr;
        };

        xen_host_info.host_data = this;
        xen_host_info.get_extension = get_extension_lambda;
        xen_host_info.request_callback = request_callback_nop;
        xen_host_info.request_process = request_process;
        xen_host_info.request_restart = request_restart;
        if (m_plugdll.handle)
        {
            // entry is not really a function but a struct, but luckily the choc
            // dll loader seems to work for this anyway, at least on Windows!
            clap_plugin_entry_t *entry =
                (clap_plugin_entry_t *)m_plugdll.findFunction("clap_entry");
            if (entry)
            {
                m_entry = entry;
                entry->init(plugfilename.c_str());
                auto fac = (clap_plugin_factory_t *)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
                auto plugin_count = fac->get_plugin_count(fac);
                if (plugin_count <= 0)
                {
                    std::cout << "no plugins to manufacture\n";
                    return;
                }
                auto desc = fac->get_plugin_descriptor(fac, plugindex);
                if (desc)
                {
                    std::cout << desc->name << "\n";
                }
                auto plug = fac->create_plugin(fac, &xen_host_info, desc->id);
                if (plug)
                {
                    m_plug = plug;
                    m_plug->init(m_plug);
                    m_inited = true;
                    initParamsExtension();
                    initGUIExtension();
                    m_from_generic_editor.reset(2048);
                }
                else
                    std::cout << "could not create clap plugin instance\n";
            }
            else
                std::cout << "could not get clap plugin entry point function\n";
        }
        else
            std::cout << "could not load clap dll " << plugfilename << "\n";
    }
    ~ClapPluginFormatProcessor() override
    {
        if (m_plug)
        {
            if (m_processingStarted)
                m_plug->stop_processing(m_plug);
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
    void onPluginRequestedResizeInternal(uint32_t w, uint32_t h)
    {
        // DBG("Plugin requested to be resized to " << (int)w << " " << (int)h);
        if (OnPluginRequestedResize)
            OnPluginRequestedResize(w, h);
    }
    bool activate(double sampleRate, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override
    {
        if (m_plug)
        {
            if (m_plug->activate(m_plug, sampleRate, minFrameCount, maxFrameCount))
            {

                m_activated = true;
                initParamsExtension();
                m_ext_audio_ports =
                    (clap_plugin_audio_ports *)m_plug->get_extension(m_plug, CLAP_EXT_AUDIO_PORTS);
                return true;
            }
        }
        return false;
    }
    void initParamsExtension()
    {
        if (m_plug)
        {
            if (!m_ext_params)
            {
                m_ext_params =
                    (clap_plugin_params_t *)m_plug->get_extension(m_plug, CLAP_EXT_PARAMS);
            }
        }
    }
    uint32_t paramsCount() const noexcept override { return m_ext_params->count(m_plug); }
    bool paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept override
    {
        return m_ext_params->get_info(m_plug, paramIndex, info);
    }
    bool paramsValue(clap_id paramId, double *value) noexcept override
    {
        if (m_ext_params)
            return m_ext_params->get_value(m_plug, paramId, value);
        return false;
    }
    clap_plugin_audio_ports *m_ext_audio_ports = nullptr;
    uint32_t audioPortsCount(bool isInput) const noexcept override
    {
        if (!m_ext_audio_ports)
            return 0;
        return m_ext_audio_ports->count(m_plug, isInput);
    }

    bool audioPortsInfo(uint32_t index, bool isInput,
                        clap_audio_port_info *info) const noexcept override
    {
        if (!m_ext_audio_ports)
            return false;
        return m_ext_audio_ports->get(m_plug, index, isInput, info);
    }
    clap::helpers::EventList m_eventMergeList;
    bool enqueueParameterChange(xenakios::CrossThreadMessage msg) noexcept override
    {
        return m_from_generic_editor.push(msg);
    }
    clap_process_status process(const clap_process *process) noexcept override
    {
        if (!m_processingStarted)
        {
            m_plug->start_processing(m_plug);
            m_processingStarted = true;
        }
        // we need a copy to be able to switch the used event list because the passed in process
        // is const
        clap_process processCtxCopy = *process;
        // shenanigans to get parameter changes from the GenericEditor
        //if (m_generic_editor)
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

    void paramsFlush(const clap_input_events *in, const clap_output_events *out) noexcept override
    {
        if (m_ext_params)
        {
            // FIXME should flush events from the GenericEditor too
            m_ext_params->flush(m_plug, in, out);
        }
    }

    clap_plugin_gui *m_ext_gui = nullptr;
    void initGUIExtension()
    {
        m_ext_gui = (clap_plugin_gui *)m_plug->get_extension(m_plug, CLAP_EXT_GUI);
    }
    std::unique_ptr<juce::Component> m_generic_editor;
    // if external plugin doesn't implement GUI, we'll use our generic editor,
    bool implementsGui() const noexcept override { return true; }
    bool guiCreate(const char *api, bool isFloating) noexcept override
    {
        if (!m_ext_gui)
        {
            m_generic_editor = std::make_unique<xenakios::GenericEditor>(*this);
            return true;
        }
        if (m_ext_gui)
        {
            return m_ext_gui->create(m_plug, "win32", false);
        }
        return false;
    }
    void guiDestroy() noexcept override
    {
        m_generic_editor = nullptr;
        if (m_ext_gui)
        {
            m_ext_gui->destroy(m_plug);
        }
    }
    // virtual bool guiIsApiSupported(const char *api, bool isFloating) noexcept { return false; }
    // virtual bool guiGetPreferredApi(const char **api, bool *is_floating) noexcept { return false;
    // }

    // virtual bool guiSetScale(double scale) noexcept { return false; }
    bool guiShow() noexcept override
    {
        if (m_ext_gui)
        {
            return m_ext_gui->show(m_plug);
        }
        return true;
    }
    bool guiHide() noexcept override
    {
        if (m_ext_gui)
        {
            return m_ext_gui->hide(m_plug);
        }
        return true;
    }
    bool guiGetSize(uint32_t *width, uint32_t *height) noexcept override
    {
        if (m_generic_editor)
        {
            *width = m_generic_editor->getWidth();
            *height = m_generic_editor->getHeight();
            return true;
        }
        if (m_ext_gui)
        {
            return m_ext_gui->get_size(m_plug, width, height);
        }
        return false;
    }
    bool guiCanResize() const noexcept override
    {
        if (m_generic_editor)
            return true;
        if (m_ext_gui)
        {
            return m_ext_gui->can_resize(m_plug);
        }
        return false;
    }
    // virtual bool guiGetResizeHints(clap_gui_resize_hints_t *hints) noexcept { return false; }
    bool guiAdjustSize(uint32_t *width, uint32_t *height) noexcept override
    {
        if (m_generic_editor)
            return guiGetSize(width, height);
        if (m_ext_gui)
        {
            return m_ext_gui->adjust_size(m_plug, width, height);
        }
        return false;
    }
    bool guiSetSize(uint32_t width, uint32_t height) noexcept override
    {
        if (m_generic_editor)
        {
            m_generic_editor->setSize(width, height);
            return true;
        }
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
    // virtual void guiSuggestTitle(const char *title) noexcept {}
    bool guiSetParent(const clap_window *window) noexcept override
    {
        auto parent = (juce::Component *)window->ptr;
        // we only support attaching the GenericEditor to Juce components
        if (m_generic_editor && std::string(window->api) == "JUCECOMPONENT")
        {
            parent->addAndMakeVisible(*m_generic_editor);
            return true;
        }
        if (m_ext_gui)
        {
            clap_window win;
            win.api = "win32";
            win.win32 = parent->getWindowHandle();
            return m_ext_gui->set_parent(m_plug, &win);
        }
        return false;
    }
};
