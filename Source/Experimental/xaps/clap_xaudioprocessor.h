#pragma once

#include "../../Common/xaudioprocessor.h"
#include "platform/choc_DynamicLibrary.h"
#ifdef JUCE_CORE_H_INCLUDED
#include "../xap_generic_editor.h"
#endif
#include "containers/choc_SingleReaderSingleWriterFIFO.h"
#include "../../Common/xap_utils.h"
#include "../xap_extensions.h"
#include <thread>
#include <iostream>
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
    std::unique_ptr<WebViewGenericEditor> m_webview_ed;
    enum ProcState
    {
        PS_Stopped,
        PS_StartRequested,
        PS_Started,
        PS_StopRequested
    };
    std::atomic<ProcState> m_processing_state{PS_Stopped};
    std::vector<clap_param_info> m_param_infos;
    IHostExtension *m_host_ext = nullptr;
    clap_plugin_tail *m_ext_plugin_tail = nullptr;
    clap_plugin_audio_ports *m_ext_audio_ports = nullptr;
    clap_plugin_note_ports *m_ext_note_ports = nullptr;
    clap_plugin_gui *m_ext_gui = nullptr;

  public:
    ClapPluginFormatProcessor(std::string plugfilename, int plugindex);
    ~ClapPluginFormatProcessor() override;

    bool getDescriptor(clap_plugin_descriptor *desc) const override;
    bool startProcessing() noexcept override;
    void stopProcessing() noexcept override;

    std::thread::id mainThreadId;
    choc::fifo::SingleReaderSingleWriterFIFO<std::function<void()>> on_main_thread_fifo;
    void runMainThreadTasks() noexcept override;

    uint32_t notePortsCount(bool isInput) const noexcept override;
    bool notePortsInfo(uint32_t index, bool isInput,
                       clap_note_port_info *info) const noexcept override;
    void onPluginRequestedResizeInternal(uint32_t w, uint32_t h);

    bool activate(double sampleRate, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override;
    void deactivate() noexcept override;
    uint32_t tailGet() const noexcept override;
    void initParamsExtension();
    uint32_t paramsCount() const noexcept override { return m_ext_params->count(m_plug); }
    bool paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept override;
    bool paramsValue(clap_id paramId, double *value) noexcept override;
    bool paramsValueToText(clap_id paramId, double value, char *display,
                           uint32_t size) noexcept override;

    uint32_t audioPortsCount(bool isInput) const noexcept override;

    bool audioPortsInfo(uint32_t index, bool isInput,
                        clap_audio_port_info *info) const noexcept override;
    clap::helpers::EventList m_eventMergeList;
    bool enqueueParameterChange(xenakios::CrossThreadMessage msg) noexcept override
    {
        return m_from_generic_editor.push(msg);
    }
    clap_process_status process(const clap_process *process) noexcept override;
    void paramsFlush(const clap_input_events *in, const clap_output_events *out) noexcept override;
    clap_plugin_state *m_ext_state = nullptr;
    bool stateSave(const clap_ostream *stream) noexcept override;
    bool stateLoad(const clap_istream *stream) noexcept override;
    bool renderSetMode(clap_plugin_render_mode mode) noexcept override;
    uint32_t remoteControlsPageCount() noexcept override;
    bool remoteControlsPageGet(uint32_t pageIndex,
                               clap_remote_controls_page *page) noexcept override;

    void initGUIExtension();
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

    // if external plugin doesn't implement GUI, we'll use our generic editor,
    bool implementsGui() const noexcept override { return true; }
    bool guiCreate(const char *api, bool isFloating) noexcept override;
    void guiDestroy() noexcept override;

    bool guiShow() noexcept override;
    bool guiHide() noexcept override;
    bool guiGetSize(uint32_t *width, uint32_t *height) noexcept override;
    bool guiCanResize() const noexcept override;
    // virtual bool guiGetResizeHints(clap_gui_resize_hints_t *hints) noexcept { return false; }
    bool guiAdjustSize(uint32_t *width, uint32_t *height) noexcept override;
    bool guiSetSize(uint32_t width, uint32_t height) noexcept override;

    bool guiSetParent(const clap_window *window) noexcept override;
#endif
};
