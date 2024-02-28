#include "clap/clap.h"
#include "clap/helpers/plugin.hh"
#include "clap/helpers/plugin.hxx"

#include "clap/helpers/host-proxy.hh"
#include "clap/helpers/host-proxy.hxx"
#include "../Experimental/noiseplethoraengine.h"
#include "sst/basic-blocks/params/ParamMetadata.h"

using ParamDesc = sst::basic_blocks::params::ParamMetaData;

struct xen_noise_plethora
    : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                   clap::helpers::CheckingLevel::Maximal>
{
    std::vector<ParamDesc> paramDescriptions;

    float sampleRate = 44100.0f;
    NoisePlethoraSynth m_synth;
    xen_noise_plethora(const clap_host *host, const clap_plugin_descriptor *desc)
        : clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                clap::helpers::CheckingLevel::Maximal>(desc, host)

    {
        paramDescriptions.push_back(ParamDesc()
                                        .withLinearScaleFormatting("dB")
                                        .withRange(-24.0, 0.0)
                                        .withDefault(-6.0)
                                        .withFlags(CLAP_PARAM_IS_AUTOMATABLE |
                                                   CLAP_PARAM_IS_MODULATABLE |
                                                   CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID)
                                        .withName("Volume")
                                        .withID((clap_id)NoisePlethoraSynth::ParamIDs::Volume));
        paramDescriptions.push_back(ParamDesc()
                                        .asPercentBipolar()
                                        .withFlags(CLAP_PARAM_IS_AUTOMATABLE |
                                                   CLAP_PARAM_IS_MODULATABLE |
                                                   CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID)
                                        .withName("Pan")
                                        .withID((clap_id)NoisePlethoraSynth::ParamIDs::Pan));
        paramDescriptions.push_back(ParamDesc()
                                        .withLinearScaleFormatting("")
                                        .withRange(0.0, 1.0)
                                        .withDefault(0.5)
                                        .withFlags(CLAP_PARAM_IS_AUTOMATABLE |
                                                   CLAP_PARAM_IS_MODULATABLE |
                                                   CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID)
                                        .withName("X")
                                        .withID((clap_id)NoisePlethoraSynth::ParamIDs::X));
        paramDescriptions.push_back(ParamDesc()
                                        .withLinearScaleFormatting("")
                                        .withRange(0.0, 1.0)
                                        .withDefault(0.5)
                                        .withFlags(CLAP_PARAM_IS_AUTOMATABLE |
                                                   CLAP_PARAM_IS_MODULATABLE |
                                                   CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID)
                                        .withName("Y")
                                        .withID((clap_id)NoisePlethoraSynth::ParamIDs::Y));
        paramDescriptions.push_back(
            ParamDesc()
                .withRange(0.0, 29.0)
                .withDefault(0.5)
                .withFlags(CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE |
                           CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID | CLAP_PARAM_IS_STEPPED)
                .withName("Algorithm")
                .withID((clap_id)NoisePlethoraSynth::ParamIDs::Algo));
        paramDescriptions.push_back(ParamDesc()
                                        .withLinearScaleFormatting("")
                                        .withRange(0.0, 127.0)
                                        .withDefault(127)
                                        .withFlags(CLAP_PARAM_IS_AUTOMATABLE |
                                                   CLAP_PARAM_IS_MODULATABLE |
                                                   CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID)
                                        .withName("Filter cut off")
                                        .withID((clap_id)NoisePlethoraSynth::ParamIDs::FiltCutoff));
        paramDescriptions.push_back(
            ParamDesc()
                .withLinearScaleFormatting("")
                .withRange(0.01, 0.99)
                .withDefault(0.01)
                .withFlags(CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE |
                           CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID)
                .withName("Filter resonance")
                .withID((clap_id)NoisePlethoraSynth::ParamIDs::FiltResonance));
        paramDescriptions.push_back(
            ParamDesc()
                .withUnorderedMapFormatting({{0, "Lowpass"},
                                             {1, "Highpass"},
                                             {2, "Bandpass"},
                                             {3, "Peak"},
                                             {4, "Notch"},
                                             {5, "Allpass"}})
                .withDefault(0.0)
                .withFlags(CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE |
                           CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID | CLAP_PARAM_IS_STEPPED)
                .withName("Filter type")
                .withID((clap_id)NoisePlethoraSynth::ParamIDs::FiltType));
    }
    clap_id timerId = 0;

    bool activate(double sampleRate_, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override
    {
        m_synth.prepare(sampleRate_, maxFrameCount);
        return true;
    }

  protected:
    bool implementsParams() const noexcept override { return true; }
    bool isValidParamId(clap_id paramId) const noexcept override { return false; }
    uint32_t paramsCount() const noexcept override { return paramDescriptions.size(); }
    bool paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept override
    {
        if (paramIndex > paramDescriptions.size())
            return false;
        paramDescriptions[paramIndex].toClapParamInfo<CLAP_NAME_SIZE>(info);
        return true;
    }
    bool paramsValue(clap_id paramId, double *value) noexcept override { return false; }
    bool paramsValueToText(clap_id paramId, double value, char *display,
                           uint32_t size) noexcept override
    {
        for (auto &e : paramDescriptions)
        {
            if (e.id == paramId)
            {
                auto s = e.valueToString(value);
                if (s)
                {
                    strcpy(display, s->c_str());
                    return true;
                }
            }
        }
        return false;
    }
    bool implementsNotePorts() const noexcept override { return true; }
    uint32_t notePortsCount(bool isInput) const noexcept override
    {
        if (isInput)
            return 1;
        return 0;
    }
    bool notePortsInfo(uint32_t index, bool isInput,
                       clap_note_port_info *info) const noexcept override
    {
        if (isInput)
        {
            info->id = 5012;
            strcpy(info->name, "Note input");
            info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
            info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
        }
        return false;
    }
    bool implementsAudioPorts() const noexcept override { return true; }
    uint32_t audioPortsCount(bool isInput) const noexcept override
    {
        if (!isInput)
            return 1;
        return 0;
    }
    bool audioPortsInfo(uint32_t index, bool isInput,
                        clap_audio_port_info *info) const noexcept override
    {
        // port id can be a "random" number
        info->id = isInput ? 2112 : 90210;
        strncpy(info->name, "main", sizeof(info->name));
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        return true;
    }
    void handleNextEvent(const clap_event_header_t *nextEvent)
    {
        if (nextEvent->space_id != CLAP_CORE_EVENT_SPACE_ID)
            return;

        switch (nextEvent->type)
        {
        case CLAP_EVENT_NOTE_OFF:
        {
            auto nevt = reinterpret_cast<const clap_event_note *>(nextEvent);
            m_synth.stopNote(nevt->port_index, nevt->channel, nevt->key, nevt->note_id,
                             nevt->velocity);
            break;
        }
        case CLAP_EVENT_NOTE_ON:
        {
            auto nevt = reinterpret_cast<const clap_event_note *>(nextEvent);
            m_synth.startNote(nevt->port_index, nevt->channel, nevt->key, nevt->note_id,
                              nevt->velocity);
            break;
        }

        case CLAP_EVENT_PARAM_VALUE:
        {
            auto pevt = reinterpret_cast<const clap_event_param_value *>(nextEvent);
            m_synth.applyParameter(pevt->port_index, pevt->channel, pevt->key, pevt->note_id,
                                   pevt->param_id, pevt->value);
            break;
        }
        case CLAP_EVENT_PARAM_MOD:
        {
            auto pevt = reinterpret_cast<const clap_event_param_mod *>(nextEvent);
            m_synth.applyParameterModulation(pevt->port_index, pevt->channel, pevt->key,
                                             pevt->note_id, pevt->param_id, pevt->amount);
            break;
        }
        default:
            break;
        }
    }
    void paramsFlush(const clap_input_events *in, const clap_output_events *out) noexcept override
    {
        auto sz = in->size(in);

        // This pointer is the sentinel to our next event which we advance once an event is
        // processed
        for (auto e = 0U; e < sz; ++e)
        {
            auto nextEvent = in->get(in, e);
            handleNextEvent(nextEvent);
        }
    }
    clap_process_status process(const clap_process *process) noexcept override
    {
        auto fc = process->frames_count;
        float *ip[2], *op[2];
        ip[0] = &process->audio_inputs->data32[0][0];
        ip[1] = &process->audio_inputs->data32[1][0];
        op[0] = &process->audio_outputs->data32[0][0];
        op[1] = &process->audio_outputs->data32[1][0];
        auto smp = 0U;

        auto ev = process->in_events;
        auto sz = ev->size(ev);

        // This pointer is the sentinel to our next event which we advance once an event is
        // processed

        /*
        const clap_event_header_t *nextEvent{nullptr};
        uint32_t nextEventIndex{0};
        if (sz != 0)
        {
            nextEvent = ev->get(ev, nextEventIndex);
        }

        for (uint32_t i = 0; i < fc; ++i)
        {
            // while, because we need to scan for events that could be at the same buffer position
            while (nextEvent && nextEvent->time == i)
            {
                handleNextEvent(nextEvent);
                nextEventIndex++;
                if (nextEventIndex >= sz)
                    nextEvent = nullptr;
                else
                    nextEvent = ev->get(ev, nextEventIndex);
            }
        }
        */
        for (int i = 0; i < sz; ++i)
        {
            auto evt = ev->get(ev, i);
            handleNextEvent(evt);
        }
        choc::buffer::SeparateChannelLayout<float> layout(process->audio_outputs->data32);
        choc::buffer::ChannelArrayView<float> bufview(layout, {2, process->frames_count});
        bufview.clear();
        m_synth.processBlock(bufview);
        return CLAP_PROCESS_CONTINUE;
    }

    bool implementsState() const noexcept override { return false; }
    bool stateSave(const clap_ostream *stream) noexcept override { return false; }
    bool stateLoad(const clap_istream *stream) noexcept override { return false; }
};

const char *features[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, nullptr};
clap_plugin_descriptor desc = {CLAP_VERSION,
                               "com.xenakios.noiseplethora",
                               "xenakios noiseplethora",
                               "Unsupported",
                               "https://xenakios.com",
                               "",
                               "",
                               "0.0.0",
                               "xenakios noise plethora",
                               features};

static const clap_plugin *clap_create_plugin(const clap_plugin_factory *f, const clap_host *host,
                                             const char *plugin_id)
{
    // I know it looks like a leak right? but the clap-plugin-helpers basically
    // take ownership and destroy the wrapper when the host destroys the
    // underlying plugin (look at Plugin<h, l>::clapDestroy if you don't believe me!)
    auto wr = new xen_noise_plethora(host, &desc);
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
