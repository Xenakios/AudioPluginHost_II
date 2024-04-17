#pragma once

#include "../xaudioprocessor.h"
#include "platform/choc_DynamicLibrary.h"
#ifdef JUCE_CORE_H_INCLUDED
#include "../xap_generic_editor.h"
#endif
#include "containers/choc_SingleReaderSingleWriterFIFO.h"
#include "../xap_utils.h"
#include "../xap_extensions.h"
#include <thread>

#ifndef JUCE_CORE_H_INCLUDED
#include "gui/choc_WebView.h"
#include "gui/choc_DesktopWindow.h"
class WebViewGenericEditor
{
  public:
    WebViewGenericEditor(xenakios::XAudioProcessor *xap);

    xenakios::XAudioProcessor *m_xap = nullptr;
    std::unique_ptr<choc::ui::WebView> m_webview;
};
#endif

class ClapPluginFormatProcessor : public xenakios::XAudioProcessor
{
    choc::file::DynamicLibrary m_plugdll;
    clap_plugin_entry_t *m_entry = nullptr;
    const clap_plugin_t *m_plug = nullptr;
    clap_host xen_host_info{CLAP_VERSION_INIT, nullptr, "XAPHOST", "Xenakios", "no website",
                            "0.0.0",           nullptr, nullptr,   nullptr,    nullptr};
    std::atomic<bool> m_inited{false};
    clap_plugin_params_t *m_ext_params = nullptr;
    clap_plugin_remote_controls *m_ext_remote_controls = nullptr;
    clap_plugin_render *m_ext_render_mode = nullptr;
    std::atomic<bool> m_processingStarted{false};
    std::atomic<bool> m_activated{false};
    choc::fifo::SingleReaderSingleWriterFIFO<xenakios::CrossThreadMessage> m_from_generic_editor;

    enum ProcState
    {
        PS_Stopped,
        PS_StartRequested,
        PS_Started,
        PS_StopRequested
    };
    std::atomic<ProcState> m_processing_state{PS_Stopped};

  public:
    std::vector<clap_param_info> m_param_infos;
    bool getDescriptor(clap_plugin_descriptor *desc) const override
    {
        if (!m_plug)
            return false;
        *desc = *m_plug->desc;
        return true;
    }
    bool startProcessing() noexcept override
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
    void stopProcessing() noexcept override
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
    /*
    static std::thread::id& mainthread_id()
    {
        static std::thread::id id;
        return id;
    }
    */
    std::thread::id mainThreadId;
    choc::fifo::SingleReaderSingleWriterFIFO<std::function<void()>> on_main_thread_fifo;
    void runMainThreadTasks()
    {
        std::function<void()> task;
        while (on_main_thread_fifo.pop(task))
        {
            task();
        }
    }
    IHostExtension *m_host_ext = nullptr;
    ClapPluginFormatProcessor(std::string plugfilename, int plugindex);

    ~ClapPluginFormatProcessor() override;

    uint32_t notePortsCount(bool isInput) const noexcept override
    {
        if (!m_ext_note_ports)
            return 0;
        return m_ext_note_ports->count(m_plug, isInput);
    }
    bool notePortsInfo(uint32_t index, bool isInput,
                       clap_note_port_info *info) const noexcept override
    {
        if (!m_ext_note_ports)
            return false;
        return m_ext_note_ports->get(m_plug, index, isInput, info);
    }
    void onPluginRequestedResizeInternal(uint32_t w, uint32_t h)
    {
        // DBG("Plugin requested to be resized to " << (int)w << " " << (int)h);
        if (OnPluginRequestedResize)
            OnPluginRequestedResize(w, h);
    }

    bool activate(double sampleRate, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override;
    void deactivate() noexcept override
    {
        if (m_plug)
        {
            m_plug->deactivate(m_plug);
            m_activated = false;
        }
    }
    uint32_t tailGet() const noexcept override
    {
        if (!m_plug)
            return 0;
        return m_ext_plugin_tail->get(m_plug);
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
    bool paramsValueToText(clap_id paramId, double value, char *display,
                           uint32_t size) noexcept override
    {
        if (m_ext_params)
            return m_ext_params->value_to_text(m_plug, paramId, value, display, size);
        return false;
    }
    clap_plugin_tail *m_ext_plugin_tail = nullptr;
    clap_plugin_audio_ports *m_ext_audio_ports = nullptr;
    clap_plugin_note_ports *m_ext_note_ports = nullptr;
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
    clap_process_status process(const clap_process *process) noexcept override;
    void paramsFlush(const clap_input_events *in, const clap_output_events *out) noexcept override
    {
        if (m_ext_params)
        {
            // FIXME should flush events from the GenericEditor too
            m_ext_params->flush(m_plug, in, out);
        }
    }
    clap_plugin_state *m_ext_state = nullptr;
    bool stateSave(const clap_ostream *stream) noexcept override
    {
        if (!m_ext_state)
            return false;
        return m_ext_state->save(m_plug, stream);
    }
    bool stateLoad(const clap_istream *stream) noexcept override
    {
        if (!m_ext_state)
            return false;
        // have to trust the hosted plugin does this thread safely...
        return m_ext_state->load(m_plug, stream);
    }
    bool renderSetMode(clap_plugin_render_mode mode) noexcept override
    {
        if (!m_ext_render_mode)
            return false;
        return m_ext_render_mode->set(m_plug, mode);
    }
    uint32_t remoteControlsPageCount() noexcept override
    {
        if (!m_ext_remote_controls)
            return 0;
        return m_ext_remote_controls->count(m_plug);
    }
    bool remoteControlsPageGet(uint32_t pageIndex,
                               clap_remote_controls_page *page) noexcept override
    {
        if (!m_ext_remote_controls)
            return false;
        return m_ext_remote_controls->get(m_plug, pageIndex, page);
    }

    clap_plugin_gui *m_ext_gui = nullptr;
    void initGUIExtension()
    {
        m_ext_gui = (clap_plugin_gui *)m_plug->get_extension(m_plug, CLAP_EXT_GUI);
    }
#ifdef JUCE_CORE_H_INCLUDED
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
#else

    std::unique_ptr<WebViewGenericEditor> m_webview_ed;
    // if external plugin doesn't implement GUI, we'll use our generic editor,
    bool implementsGui() const noexcept override { return true; }
    bool guiCreate(const char *api, bool isFloating) noexcept override
    {
        if (m_ext_gui)
        {
            return m_ext_gui->create(m_plug, "win32", false);
        }
        m_webview_ed = std::make_unique<WebViewGenericEditor>(this);
        return true;
    }
    void guiDestroy() noexcept override
    {
        if (m_ext_gui)
        {
            m_ext_gui->destroy(m_plug);
        }
        m_webview_ed = nullptr;
    }

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

        if (m_ext_gui)
        {
            return m_ext_gui->get_size(m_plug, width, height);
        }
        *width = 500;
        *height = paramsCount() * 25;
        return true;
    }
    bool guiCanResize() const noexcept override
    {
        if (m_ext_gui)
        {
            return m_ext_gui->can_resize(m_plug);
        }
        return false;
    }
    // virtual bool guiGetResizeHints(clap_gui_resize_hints_t *hints) noexcept { return false; }
    bool guiAdjustSize(uint32_t *width, uint32_t *height) noexcept override
    {
        if (m_ext_gui)
        {
            return m_ext_gui->adjust_size(m_plug, width, height);
        }
        return false;
    }
    bool guiSetSize(uint32_t width, uint32_t height) noexcept override
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

    bool guiSetParent(const clap_window *window) noexcept override
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
#endif
};
