#pragma once

#include "clap/clap.h"
#include <clap/helpers/event-list.hh>
#include "sst/basic-blocks/params/ParamMetadata.h"
#include <memory>
#include <vector>
#include <functional>

namespace xenakios
{

using ParamDesc = sst::basic_blocks::params::ParamMetaData;

struct CrossThreadMessage
{
    CrossThreadMessage() {}
    CrossThreadMessage(clap_id parId, int eType, double val)
        : paramId(parId), eventType(eType), value(val)
    {
    }
    template <typename T> CrossThreadMessage withParamId(T id)
    {
        auto result = *this;
        result.paramId = static_cast<clap_id>(id);
        return result;
    }
    CrossThreadMessage withType(int etype)
    {
        auto result = *this;
        result.eventType = etype;
        return result;
    }
    CrossThreadMessage withValue(double v)
    {
        auto result = *this;
        result.value = v;
        return result;
    }
    template <typename T> CrossThreadMessage asParamChange(T parid, double v)
    {
        auto result = *this;
        result.eventType = CLAP_EVENT_PARAM_VALUE;
        result.paramId = static_cast<clap_id>(parid);
        result.value = v;
        return result;
    }
    clap_id paramId = CLAP_INVALID_ID;
    int eventType = CLAP_EVENT_PARAM_VALUE;
    double value = 0.0;
};

inline void pushParamEvent(clap::helpers::EventList &elist, bool is_mod, uint32_t timeStamp,
                           clap_id paramId, double value)
{
    if (!is_mod)
    {
        clap_event_param_value pv;
        pv.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        pv.header.size = sizeof(clap_event_param_value);
        pv.header.flags = 0;
        pv.header.time = timeStamp;
        pv.header.type = CLAP_EVENT_PARAM_VALUE;
        pv.cookie = nullptr;
        pv.param_id = paramId;
        pv.value = value;
        pv.channel = -1;
        pv.key = -1;
        pv.note_id = -1;
        pv.port_index = -1;
        elist.push(reinterpret_cast<const clap_event_header *>(&pv));
    }
    else
    {
        clap_event_param_mod pv;
        pv.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        pv.header.size = sizeof(clap_event_param_mod);
        pv.header.flags = 0;
        pv.header.time = timeStamp;
        pv.header.type = CLAP_EVENT_PARAM_MOD;
        pv.cookie = nullptr;
        pv.param_id = paramId;
        pv.amount = value;
        pv.channel = -1;
        pv.key = -1;
        pv.note_id = -1;
        pv.port_index = -1;
        elist.push(reinterpret_cast<const clap_event_header *>(&pv));
    }
}

/*
We mirror Clap C++ plugin helper as much as possible/sensible, but we do assume certain
things are implemented even though not required by the official Clap standard, such
as audio/MIDI processing, parameters, state etc
*/

class XAudioProcessor
{
  public:
    XAudioProcessor() {}
    virtual ~XAudioProcessor() {}
    virtual bool getDescriptor(clap_plugin_descriptor *desc) const { return false; }
    // would we ever have a reason to return false here...?
    virtual bool init() noexcept { return true; }
    // should perhaps be pure virtual
    virtual bool activate(double sampleRate, uint32_t minFrameCount,
                          uint32_t maxFrameCount) noexcept
    {
        return true;
    }
    virtual void deactivate() noexcept {}

    virtual bool startProcessing() noexcept { return false; }
    virtual void stopProcessing() noexcept {}

    // This could maybe be pure virtual because the processor is useless if it doesn't process!
    virtual clap_process_status process(const clap_process *process) noexcept
    {
        return CLAP_PROCESS_SLEEP;
    }
    virtual void reset() noexcept {}
    virtual void onMainThread() noexcept {}
    virtual const void *extension(const char *id) noexcept { return nullptr; }

    virtual uint32_t tailGet() const noexcept { return 0; }

    // Parameters
    // We will simply allow a parameter count of 0 instead of a separate call
    // to check if the parameters are implemented, since processors without parameters
    // are rare
    virtual uint32_t paramsCount() const noexcept { return 0; }
    virtual bool paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept
    {
        return false;
    }
    virtual bool paramsValue(clap_id paramId, double *value) noexcept { return false; }
    virtual bool paramsValueToText(clap_id paramId, double value, char *display,
                                   uint32_t size) noexcept
    {
        return false;
    }
    virtual bool paramsTextToValue(clap_id paramId, const char *display, double *value) noexcept
    {
        return false;
    }
    std::vector<ParamDesc> paramDescriptions;
    // paramsFlush is intended to be used to set parameters even when the processing isn't
    // actively called. We might want to somehow abstract this, if possible...
    virtual void paramsFlush(const clap_input_events *in, const clap_output_events *out) noexcept {}
    std::function<void(clap_param_rescan_flags)> OnPluginRequestedParameterRescan{
        [](clap_param_rescan_flags) {}};

    virtual uint32_t audioPortsCount(bool isInput) const noexcept { return 0; }
    virtual bool audioPortsInfo(uint32_t index, bool isInput,
                                clap_audio_port_info *info) const noexcept
    {
        return false;
    }

    virtual uint32_t notePortsCount(bool isInput) const noexcept { return 0; }
    virtual bool notePortsInfo(uint32_t index, bool isInput,
                               clap_note_port_info *info) const noexcept
    {
        return false;
    }

    virtual bool stateSave(const clap_ostream *stream) noexcept { return false; }
    virtual bool stateLoad(const clap_istream *stream) noexcept { return false; }

    virtual bool renderSetMode(clap_plugin_render_mode mode) noexcept { return false; }

    // This should thread safely insert a parameter change into the processor's event list.
    // That may be a bit challenging/annoying to do, so we might want some kind of ready-to-use
    // solution for that...
    // eventType : CLAP_EVENT_PARAM_GESTURE_BEGIN, CLAP_EVENT_PARAM_GESTURE_END,
    // CLAP_EVENT_PARAM_VALUE
    virtual bool enqueueParameterChange(CrossThreadMessage msg) noexcept { return false; }
    virtual bool dequeueParameterChange(CrossThreadMessage &msg) noexcept { return false; }
    virtual bool dequeueEventForGUI(CrossThreadMessage &msg) noexcept { return false; }

    virtual uint32_t remoteControlsPageCount() noexcept { return 0; }
    virtual bool remoteControlsPageGet(uint32_t pageIndex, clap_remote_controls_page *page) noexcept
    {
        return false;
    }

    // the original Clap C++ helper methods for GUI.
    // We really might not want to handle *all* this, but I wonder
    // if we are kind of forced when hosting actual Clap plugins?
    virtual bool implementsGui() const noexcept { return false; }
    virtual bool guiIsApiSupported(const char *api, bool isFloating) noexcept { return false; }
    virtual bool guiGetPreferredApi(const char **api, bool *is_floating) noexcept { return false; }
    virtual bool guiCreate(const char *api, bool isFloating) noexcept { return false; }
    virtual void guiDestroy() noexcept {}
    virtual bool guiSetScale(double scale) noexcept { return false; }
    virtual bool guiShow() noexcept { return false; }
    virtual bool guiHide() noexcept { return false; }
    virtual bool guiGetSize(uint32_t *width, uint32_t *height) noexcept { return false; }
    virtual bool guiCanResize() const noexcept { return false; }
    virtual bool guiGetResizeHints(clap_gui_resize_hints_t *hints) noexcept { return false; }
    virtual bool guiAdjustSize(uint32_t *width, uint32_t *height) noexcept
    {
        return guiGetSize(width, height);
    }
    virtual bool guiSetSize(uint32_t width, uint32_t height) noexcept { return false; }
    virtual void guiSuggestTitle(const char *title) noexcept {}
    virtual bool guiSetParent(const clap_window *window) noexcept { return false; }
    virtual bool guiSetTransient(const clap_window *window) noexcept { return false; }
    std::function<void(uint32_t w, uint32_t h)> OnPluginRequestedResize;
};

using CreationFunc = std::function<std::unique_ptr<XAudioProcessor>()>;

class XapFactory
{
  private:
    XapFactory() {}

  public:
    static XapFactory &getInstance()
    {
        static XapFactory fact;
        return fact;
    }
    void registerEntry(std::string name, CreationFunc createFunc)
    {
        m_entries.emplace_back(name, createFunc);
    }
    std::unique_ptr<XAudioProcessor> createFromName(std::string name)
    {
        for (auto &e : m_entries)
        {
            if (e.name == name)
                return e.createfunc();
        }
        return nullptr;
    }
    struct Entry
    {
        Entry() {}
        Entry(std::string name_, CreationFunc func_) : name(name_), createfunc(func_) {}
        std::string name;
        CreationFunc createfunc;
    };
    std::vector<Entry> m_entries;
};

class RegisterXap
{
  public:
    RegisterXap(std::string name, CreationFunc createFunc)
    {
        XapFactory::getInstance().registerEntry(name, createFunc);
    }
};

} // namespace xenakios
