#pragma once

#include "xaudioprocessor.h"
#include "containers/choc_SingleReaderSingleWriterFIFO.h"

class XAPWithJuceGUI : public xenakios::XAudioProcessor
{
  protected:
    std::unique_ptr<juce::Component> m_editor;
    choc::fifo::SingleReaderSingleWriterFIFO<xenakios::CrossThreadMessage> m_from_ui_fifo;
    clap::helpers::EventList m_merge_list;

  public:
    bool enqueueParameterChange(xenakios::CrossThreadMessage msg) noexcept override
    {
        return m_from_ui_fifo.push(msg);
    }

    void mergeParameterEvents(const clap_process *process_ctx)
    {
        m_merge_list.clear();
        xenakios::CrossThreadMessage msg;
        while (m_from_ui_fifo.pop(msg))
        {
            if (msg.eventType == CLAP_EVENT_PARAM_VALUE)
            {
                auto pev = makeClapParameterValueEvent(0, msg.paramId, msg.value);
                m_merge_list.push((const clap_event_header *)&pev);
            }
        }
        auto in_evts = process_ctx->in_events;
        auto sz = in_evts->size(in_evts);
        for (uint32_t i = 0; i < sz; ++i)
        {
            auto ev = in_evts->get(in_evts, i);
            m_merge_list.push(ev);
        }
    }
    bool implementsGui() const noexcept override { return true; }
    // virtual bool guiIsApiSupported(const char *api, bool isFloating) noexcept { return false; }
    // virtual bool guiGetPreferredApi(const char **api, bool *is_floating) noexcept { return false;
    // }

    // virtual bool guiSetScale(double scale) noexcept { return false; }
    bool guiShow() noexcept override
    {
        if (!m_editor)
            return false;
        // maybe not the best place to init this, but...
        m_from_ui_fifo.reset(2048);
        return true;
    }
    bool guiHide() noexcept override { return false; }
    virtual bool guiGetSize(uint32_t *width, uint32_t *height) noexcept override
    {
        if (!m_editor)
            return false;
        *width = m_editor->getWidth();
        *height = m_editor->getHeight();
        return true;
    }
    bool guiCanResize() const noexcept override { return true; }
    // virtual bool guiGetResizeHints(clap_gui_resize_hints_t *hints) noexcept { return false; }
    // virtual bool guiAdjustSize(uint32_t *width, uint32_t *height) noexcept
    //{
    //     return guiGetSize(width, height);
    // }
    bool guiSetSize(uint32_t width, uint32_t height) noexcept override
    {
        if (!m_editor)
            return false;
        m_editor->setSize(width, height);
        return true;
    }
    // virtual void guiSuggestTitle(const char *title) noexcept {}
    bool guiSetParent(const clap_window *window) noexcept override
    {
        if (!m_editor || std::string(window->api) != "JUCECOMPONENT")
            return false;
        // This is a bit iffy hack but will have to suffice for now.
        // We don't really want to start dealing with the OS native handles
        // and such when both the processor GUI and the host GUI are Juce based anyway...
        auto parent = (juce::Component *)window->ptr;
        parent->addAndMakeVisible(*m_editor);
        return true;
    }
};
