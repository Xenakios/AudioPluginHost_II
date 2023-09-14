#pragma once

#include <clap/clap.h>
#include <clap/helpers/event-list.hh>

#define XENAKIOS_CLAP_NAMESPACE 11111

#define XENAKIOS_EVENT_CHANGEFILE 103
struct xenakios_event_change_file
{
    clap_event_header header;
    int target = 0; // which file should be changed
    char filepath[256];
};

namespace xenakios
{

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

class XAudioProcessorEditor;

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
    virtual bool getDescriptor(clap_plugin_descriptor* desc) const
    {
        return false;
    }
    // would we ever have a reason to return false here...?
    virtual bool init() noexcept { return true; }
    // should perhaps be pure virtual
    virtual bool activate(double sampleRate, uint32_t minFrameCount,
                          uint32_t maxFrameCount) noexcept
    {
        return true;
    }
    virtual void deactivate() noexcept {}
    // doubtful we'd ever want to have control over these...?
    // the processor should be ready to process once activated etc,
    // but let's leave these on for the moment
    virtual bool startProcessing() noexcept { return true; }
    virtual void stopProcessing() noexcept {}

    // This could maybe be pure virtual because the processor is useless if it doesn't process!
    virtual clap_process_status process(const clap_process *process) noexcept
    {
        return CLAP_PROCESS_SLEEP;
    }
    virtual void reset() noexcept {}
    virtual void onMainThread() noexcept {}
    virtual const void *extension(const char *id) noexcept { return nullptr; }

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
    // paramsFlush is intended to be used to set parameters even when the processing isn't
    // actively called. We might want to somehow abstract this, if possible...
    virtual void paramsFlush(const clap_input_events *in, const clap_output_events *out) noexcept {}

    virtual uint32_t notePortsCount(bool isInput) const noexcept { return 0; }
    virtual bool notePortsInfo(uint32_t index, bool isInput,
                               clap_note_port_info *info) const noexcept
    {
        return false;
    }

    virtual bool renderSetMode(clap_plugin_render_mode mode) noexcept { return false; }

    // Juce GUI
    virtual bool hasEditor() noexcept { return false; }
    virtual XAudioProcessorEditor *createEditorIfNeeded() noexcept { return nullptr; }
    virtual XAudioProcessorEditor *createEditor() noexcept { return nullptr; }
};
} // namespace xenakios
