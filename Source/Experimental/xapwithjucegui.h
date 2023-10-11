#pragma once

#include "JuceHeader.h"
#include "xaudioprocessor.h"
#include "xap_utils.h"

class XAPWithJuceGUI : public xenakios::XAudioProcessor
{
  protected:
    std::unique_ptr<juce::Component> m_editor;
    std::atomic<bool> m_editor_attached{false};
    SingleReaderSingleWriterFifoHelper<xenakios::CrossThreadMessage> m_from_ui_fifo;
    SingleReaderSingleWriterFifoHelper<xenakios::CrossThreadMessage> m_to_ui_fifo;
    // clap::helpers::EventList m_merge_list;
    std::unordered_map<clap_id, float *> paramToValue;
    std::unordered_map<clap_id, int> paramToPatchIndex;
    struct Patch
    {
        std::vector<float> params;
    } patch;

  public:
    bool enqueueParameterChange(xenakios::CrossThreadMessage msg) noexcept override
    {
        return m_from_ui_fifo.push(msg);
    }
    bool enqueueToUI(xenakios::CrossThreadMessage msg) noexcept { return m_to_ui_fifo.push(msg); }
    void pushAllParamsToGUI()
    {
        if (!m_editor_attached)
            return;
        for (auto &p : paramToPatchIndex)
        {
            float val = patch.params[p.second];
            m_to_ui_fifo.push(xenakios::CrossThreadMessage{p.first, CLAP_EVENT_PARAM_VALUE, val});
        }
    }
    virtual void handleInboundEvent(const clap_event_header *ev, bool is_from_ui) noexcept
    {
        if (ev->space_id != CLAP_CORE_EVENT_SPACE_ID)
            return;
        if (ev->type == CLAP_EVENT_PARAM_VALUE)
        {
            auto pev = (const clap_event_param_value *)ev;
            auto it = paramToValue.find(pev->param_id);
            if (it != paramToValue.end())
            {
                *(it->second) = pev->value;
            }

            if (!is_from_ui)
            {
                m_to_ui_fifo.push(xenakios::CrossThreadMessage{pev->param_id,
                                                               CLAP_EVENT_PARAM_VALUE, pev->value});
            }
        }
    }
    

    void handleGUIEvents()
    {
        // we should also push the messages out of the processor
        // so that host can do automation recording, detect last touched parameter etc,
        // but our host does not yet handle those anyway...
        xenakios::CrossThreadMessage msg;
        while (m_from_ui_fifo.pop(msg))
        {
            if (msg.eventType == CLAP_EVENT_PARAM_VALUE)
            {
                auto pev = makeClapParameterValueEvent(0, msg.paramId, msg.value);
                handleInboundEvent((const clap_event_header *)&pev, true);
            }
        }
    }
    bool dequeueEventForGUI(xenakios::CrossThreadMessage &msg) noexcept override
    {
        return m_to_ui_fifo.pop(msg);
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
