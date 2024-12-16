#include "../Common/xap_ipc.h"

#include "clap/clap.h"
#include "clap/events.h"
#include "clap/ext/note-ports.h"
#include "clap/helpers/plugin.hh"
#include "clap/helpers/plugin.hxx"
#include "clap/helpers/host-proxy.hh"
#include "clap/helpers/host-proxy.hxx"
#include "clap/plugin-features.h"
#include "sst/basic-blocks/params/ParamMetadata.h"
#include "gui/choc_WebView.h"
#include "containers/choc_SingleReaderSingleWriterFIFO.h"

#include "../Common/xap_utils.h"
#include <filesystem>
#include <unordered_map>
// #include "clap/ext/draft/param-origin.h"

using ParamDesc = sst::basic_blocks::params::ParamMetaData;

struct xen_pipereceiver
    : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                   clap::helpers::CheckingLevel::Maximal>
{
    std::vector<ParamDesc> paramDescs;

    const void *extension(const char *id) noexcept override
    {
        /*
        if (!strcmp(id, CLAP_EXT_PARAM_ORIGIN))
        {
            return &ext_parameter_origin;
        }
        */
        return nullptr;
    }
    std::unordered_map<clap_id, double> paramOrigins;
    xen_pipereceiver(const clap_host *host, const clap_plugin_descriptor *desc)
        : clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                clap::helpers::CheckingLevel::Maximal>(desc, host)
    {
        // bool(CLAP_ABI *get)(const clap_plugin_t *plugin, clap_id param_id, double *out_value);
        /*
        ext_parameter_origin.get = [](const clap_plugin_t *plugin, clap_id param_id,
                                      double *out_value) {
            auto myplugin = (xen_fileplayer *)plugin->plugin_data;
            auto it = myplugin->paramOrigins.find(param_id);
            if (it != myplugin->paramOrigins.end())
            {
                *out_value = it->second;
                return true;
            }
            return false;
        };
        */
    }
    double outSr = 44100.0;
    bool activate(double sampleRate_, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override
    {
        outSr = sampleRate_;
        return true;
    }
    bool implementsParams() const noexcept override { return false; }
    bool isValidParamId(clap_id paramId) const noexcept override { return false; }
    uint32_t paramsCount() const noexcept override { return 0; }
    bool paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept override
    {
        return false;
        if (paramIndex >= paramDescs.size())
            return false;
        paramDescs[paramIndex].toClapParamInfo<CLAP_NAME_SIZE>(info);
        return true;
    }
    bool paramsValue(clap_id paramId, double *value) noexcept override { return false; }
    bool paramsValueToText(clap_id paramId, double value, char *display,
                           uint32_t size) noexcept override
    {
        return false;
    }
    bool implementsAudioPorts() const noexcept override { return false; }
    uint32_t audioPortsCount(bool isInput) const noexcept override { return 0; }
    bool audioPortsInfo(uint32_t index, bool isInput,
                        clap_audio_port_info *info) const noexcept override
    {
        return false;
        // port id can be a "random" number
        info->id = isInput ? 2112 : 90210;
        strncpy(info->name, "main", sizeof(info->name));
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        return true;
    }
    bool implementsNotePorts() const noexcept override { return true; }
    uint32_t notePortsCount(bool isInput) const noexcept override
    {
        if (isInput)
            return 1;
        return 1;
    }
    bool notePortsInfo(uint32_t index, bool isInput,
                       clap_note_port_info *info) const noexcept override
    {
        if (isInput)
        {
            info->id = 100;
            strcpy(info->name, "Note Input");
            info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
            info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
        }
        if (!isInput)
        {
            info->id = 101;
            strcpy(info->name, "Note Output");
            info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
            info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
        }
        return true;
    }
    clap_process_status process(const clap_process *process) noexcept override
    {
        int numEvents = process->in_events->size(process->in_events);
        for (int i = 0; i < numEvents; ++i)
        {
            auto ev = process->in_events->get(process->in_events, i);
            process->out_events->try_push(process->out_events, ev);
            if (ev->type == CLAP_EVENT_NOTE_ON || ev->type == CLAP_EVENT_NOTE_OFF)
            {
                auto nev = (clap_event_note *)ev;
                auto dupev = xenakios::make_event_note(nev->header.time, ev->type, nev->port_index,
                                                       nev->channel, nev->key + 7, nev->note_id,
                                                       nev->velocity);
                process->out_events->try_push(process->out_events, (clap_event_header *)&dupev);
            }
        }
        
        return CLAP_PROCESS_CONTINUE;
    }
};

const char *features[] = {CLAP_PLUGIN_FEATURE_UTILITY, nullptr};
clap_plugin_descriptor desc = {CLAP_VERSION,
                               "com.xenakios.clappipe_receiver",
                               "xenakios clappipe receiver",
                               "Unsupported",
                               "https://xenakios.com",
                               "",
                               "",
                               "0.0.0",
                               "xenakios clappipe_receiver",
                               features};

static const clap_plugin *clap_create_plugin(const clap_plugin_factory *f, const clap_host *host,
                                             const char *plugin_id)
{
    // I know it looks like a leak right? but the clap-plugin-helpers basically
    // take ownership and destroy the wrapper when the host destroys the
    // underlying plugin (look at Plugin<h, l>::clapDestroy if you don't believe me!)
    auto wr = new xen_pipereceiver(host, &desc);
    return wr->clapPlugin();
}

uint32_t get_plugin_count(const struct clap_plugin_factory *factory) { return 1; }

const clap_plugin_descriptor *get_plugin_descriptor(const clap_plugin_factory *f, uint32_t w)
{
    return &desc;
}

const CLAP_EXPORT struct clap_plugin_factory the_factory = {
    get_plugin_count,
    get_plugin_descriptor,
    clap_create_plugin,
};

static const void *get_factory(const char *factory_id) { return &the_factory; }

// clap_init and clap_deinit are required to be fast, but we have nothing we need to do here
bool clap_init(const char *p) { return true; }

void clap_deinit() {}

extern "C"
{

    // clang-format off
const CLAP_EXPORT struct clap_plugin_entry clap_entry = {
        CLAP_VERSION,
        clap_init,
        clap_deinit,
        get_factory
};
    // clang-format on
}
