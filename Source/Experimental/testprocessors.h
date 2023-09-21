#pragma once

#include "xaudioprocessor.h"
#include "JuceHeader.h"
#include "signalsmith-stretch.h"
#include "containers/choc_NonAllocatingStableSort.h"
#include "containers/choc_SingleReaderSingleWriterFIFO.h"
#include "sst/basic-blocks/modulators/SimpleLFO.h"
#include "dejavurandom.h"
#include "xap_generic_editor.h"

inline clap_event_param_value makeClapParameterValueEvent(int time, clap_id paramId, double value,
                                                          void *cookie = nullptr, int port = -1,
                                                          int channel = -1, int key = -1,
                                                          int noteid = -1)
{
    clap_event_param_value pv;
    pv.header.time = time;
    pv.header.size = sizeof(clap_event_param_value);
    pv.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    pv.header.flags = 0;
    pv.header.type = CLAP_EVENT_PARAM_VALUE;
    pv.channel = channel;
    pv.cookie = cookie;
    pv.key = key;
    pv.note_id = noteid;
    pv.param_id = paramId;
    pv.port_index = port;
    pv.value = value;
    return pv;
}

inline clap_event_param_mod makeClapParameterModEvent(int time, clap_id paramId, double value,
                                                      void *cookie = nullptr, int port = -1,
                                                      int channel = -1, int key = -1,
                                                      int noteid = -1)
{
    clap_event_param_mod pv;
    pv.header.time = time;
    pv.header.size = sizeof(clap_event_param_mod);
    pv.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    pv.header.flags = 0;
    pv.header.type = CLAP_EVENT_PARAM_MOD;
    pv.channel = channel;
    pv.cookie = cookie;
    pv.key = key;
    pv.note_id = noteid;
    pv.param_id = paramId;
    pv.port_index = port;
    pv.amount = value;
    return pv;
}

inline void insertToEndOfEventListFromFIFO(
    choc::fifo::SingleReaderSingleWriterFIFO<xenakios::CrossThreadMessage> &source,
    clap_input_events *processDestination, clap_output_events *outputList = nullptr)
{
    int lastpos = 0;
    int lasteventindex = processDestination->size(processDestination) - 1;
    if (lasteventindex > 0)
    {
        auto ev = processDestination->get(processDestination, lasteventindex);
        lastpos = ev->time;
    }

    xenakios::CrossThreadMessage msg;
    while (source.pop(msg))
    {
        if (msg.eventType == CLAP_EVENT_PARAM_VALUE)
        {
            auto pv = makeClapParameterValueEvent(lastpos, msg.paramId, msg.value);
        }
        else if (msg.eventType == CLAP_EVENT_PARAM_GESTURE_BEGIN ||
                 msg.eventType == CLAP_EVENT_PARAM_GESTURE_END)
        {
            clap_event_param_gesture ge;
        }
    }
}

template <typename T> inline clap_id to_clap_id(T x) { return static_cast<clap_id>(x); }

inline clap_param_info makeParamInfo(clap_id paramId, juce::String name, double minval,
                                     double maxval, double defaultVal, clap_param_info_flags flags,
                                     void *cookie = nullptr)
{
    clap_param_info result;
    result.cookie = cookie;
    result.default_value = defaultVal;
    result.min_value = minval;
    result.max_value = maxval;
    result.id = paramId;
    result.flags = flags;
    auto ptr = name.toUTF8();
    strcpy_s(result.name, ptr);
    result.module[0] = 0;
    return result;
}

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

class ToneProcessorTest : public XAPWithJuceGUI
{
  public:
    std::vector<clap_param_info> m_param_infos;
    juce::dsp::Oscillator<float> m_osc;
    double m_pitch = 60.0;
    double m_pitch_mod = 0.0f;
    double m_distortion_amt = 0.3;
    enum class ParamIds
    {
        Pitch = 5034,
        Distortion = 9

    };
    bool m_outputs_modulation = false;
    clap_id m_mod_out_par_id = 0;
    ToneProcessorTest(bool outputsmodulation = false, clap_id destid = 0)
    {
        m_outputs_modulation = outputsmodulation;
        m_mod_out_par_id = destid;
        m_osc.initialise([](float x) { return std::sin(x); }, 1024);
        m_param_infos.push_back(
            makeParamInfo((clap_id)ParamIds::Pitch, "Pitch", 0.0, 127.0, 60.0,
                          CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE));
        if (!outputsmodulation)
            m_param_infos.push_back(
                makeParamInfo((clap_id)ParamIds::Distortion, "Distortion", 0.0, 1.0, 0.3,
                              CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE));
        if (outputsmodulation)
        {
            m_pitch = 0.0; // about 8hz
        }
    }
    bool activate(double sampleRate, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override
    {
        juce::dsp::ProcessSpec spec;
        spec.maximumBlockSize = maxFrameCount;
        spec.numChannels = 2;
        spec.sampleRate = sampleRate;
        m_osc.prepare(spec);

        return true;
    }
    uint32_t paramsCount() const noexcept override { return m_param_infos.size(); }
    bool paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept override
    {
        *info = m_param_infos[paramIndex];
        return true;
    }
    clap_process_status process(const clap_process *process) noexcept override
    {
        mergeParameterEvents(process);
        auto inEvents = m_merge_list.clapInputEvents();
        for (int i = 0; i < inEvents->size(inEvents); ++i)
        {
            auto ev = inEvents->get(inEvents, i);
            if (ev->type == CLAP_EVENT_PARAM_VALUE)
            {
                auto pvev = reinterpret_cast<const clap_event_param_value *>(ev);
                if (pvev->param_id == (clap_id)ParamIds::Pitch)
                {
                    m_pitch = pvev->value;
                }
                if (pvev->param_id == (clap_id)ParamIds::Distortion)
                {
                    m_distortion_amt = pvev->value;
                }
            }
            if (ev->type == CLAP_EVENT_PARAM_MOD)
            {
                auto pvev = reinterpret_cast<const clap_event_param_mod *>(ev);
                if (pvev->param_id == (clap_id)ParamIds::Pitch)
                {
                    m_pitch_mod = pvev->amount;
                }
            }
        }
        double finalpitch = m_pitch + m_pitch_mod;
        double hz = 440.0 * std::pow(2.0, 1.0 / 12 * (finalpitch - 69.0));
        m_osc.setFrequency(hz);
        double distvolume = m_distortion_amt * 48.0;
        double distgain = juce::Decibels::decibelsToGain(distvolume);
        for (int i = 0; i < process->frames_count; ++i)
        {
            float s = m_osc.processSample(0.0f);
            s = std::tanh(s * distgain) * 0.25;
            process->audio_outputs[0].data32[0][i] = s;
        }
        return CLAP_PROCESS_CONTINUE;
    }

    uint32_t audioPortsCount(bool isInput) const noexcept override
    {
        if (!isInput)
            return 1;
        return 0;
    }
    bool audioPortsInfo(uint32_t index, bool isInput,
                        clap_audio_port_info *info) const noexcept override
    {
        if (isInput)
            return false;
        info->channel_count = 1;
        info->flags = 0;
        info->id = 1045;
        info->in_place_pair = false;
        strcpy_s(info->name, "Tone generator output");
        info->port_type = "";
        return true;
    }

    bool guiCreate(const char *api, bool isFloating) noexcept override
    {
        m_editor = std::make_unique<xenakios::GenericEditor>(*this);
        if (OnPluginRequestedResize)
            OnPluginRequestedResize(500, 100);
        return true;
    }
    void guiDestroy() noexcept override { m_editor = nullptr; }
    int m_mod_out_block_size = 401;
    int m_mod_out_counter = 0;
};

class ModulatorSource : public XAPWithJuceGUI
{
  public:
    std::vector<clap_param_info> m_param_infos;
    enum class ParamIds
    {
        ModType = 4900,
        Rate = 665,
        PolyShift = 5
    };
    int m_mod_type = 0;
    double m_rate = 0.0;
    double m_poly_shift = 0.0;
    static constexpr int BLOCK_SIZE = 64;
    static constexpr int BLOCK_SIZE_OS = BLOCK_SIZE * 2;
    alignas(32) float table_envrate_linear[512];
    using LFOType = sst::basic_blocks::modulators::SimpleLFO<ModulatorSource, BLOCK_SIZE>;
    std::vector<std::unique_ptr<LFOType>> m_lfos;

    double samplerate = 44100;
    void initTables()
    {
        double dsamplerate_os = samplerate * 2;
        for (int i = 0; i < 512; ++i)
        {
            double k =
                dsamplerate_os * pow(2.0, (((double)i - 256.0) / 16.0)) / (double)BLOCK_SIZE_OS;
            table_envrate_linear[i] = (float)(1.f / k);
        }
    }
    float envelope_rate_linear_nowrap(float x)
    {
        x *= 16.f;
        x += 256.f;
        int e = std::clamp<int>((int)x, 0, 0x1ff - 1);

        float a = x - (float)e;

        return (1 - a) * table_envrate_linear[e & 0x1ff] +
               a * table_envrate_linear[(e + 1) & 0x1ff];
    }
    bool guiCreate(const char *api, bool isFloating) noexcept override
    {
        m_editor = std::make_unique<xenakios::GenericEditor>(*this);
        m_editor->setSize(500, 120);
        return true;
    }
    void guiDestroy() noexcept override { m_editor = nullptr; }
    ModulatorSource(int maxpolyphony, double initialRate)
    {
        m_rate = initialRate;
        for (int i = 0; i < maxpolyphony; ++i)
        {
            m_lfos.push_back(std::make_unique<LFOType>(this));
        }
        m_param_infos.push_back(makeParamInfo((clap_id)ParamIds::ModType, "Modulation type", 0.0,
                                              2.0, 0.0,
                                              CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED));
        m_param_infos.push_back(
            makeParamInfo((clap_id)ParamIds::Rate, "Rate", -1.0, 3.0, 0.00,
                          CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE));
        m_param_infos.push_back(
            makeParamInfo((clap_id)ParamIds::PolyShift, "Phase shift", 0.0, 1.0, 0.00,
                          CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE));
    }
    bool activate(double sampleRate, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override
    {
        samplerate = sampleRate;
        initTables();

        return true;
    }
    uint32_t paramsCount() const noexcept override { return m_param_infos.size(); }
    bool paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept override
    {
        *info = m_param_infos[paramIndex];
        return true;
    }
    int m_update_counter = 0;

    void handleInboundEvent(const clap_event_header *nextEvent)
    {
        if (nextEvent->space_id != CLAP_CORE_EVENT_SPACE_ID)
            return;
        if (nextEvent->type == CLAP_EVENT_PARAM_VALUE)
        {
            auto pev = reinterpret_cast<const clap_event_param_value *>(nextEvent);
            if (pev->param_id == to_clap_id(ParamIds::Rate))
                m_rate = pev->value;
            if (pev->param_id == to_clap_id(ParamIds::PolyShift))
                m_poly_shift = pev->value;
            if (pev->param_id == to_clap_id(ParamIds::ModType))
                m_mod_type = static_cast<int>(pev->value);
        }
    }
    clap_process_status process(const clap_process *process) noexcept override
    {
        mergeParameterEvents(process);
        auto inevents = m_merge_list.clapInputEvents();
        const clap_event_header *next_event = nullptr;
        auto esz = inevents->size(inevents);
        uint32_t nextEventIndex{0};
        if (esz > 0)
            next_event = inevents->get(inevents, nextEventIndex);
        auto frames = process->frames_count;
        for (uint32_t i = 0; i < frames; ++i)
        {
            // sample accurate event handling is overkill here because we do block processing
            // of the LFO anyway...maybe look into doing this in some other way later
            while (next_event && next_event->time == i)
            {
                handleInboundEvent(next_event);
                nextEventIndex++;
                if (nextEventIndex >= esz)
                    next_event = nullptr;
                else
                    next_event = inevents->get(inevents, nextEventIndex);
            }
            if (m_update_counter == 0)
            {
                for (int j = 0; j < m_lfos.size(); ++j)
                {
                    m_lfos[j]->applyPhaseOffset(m_poly_shift);
                    m_lfos[j]->process_block(m_rate, 0.0f, m_mod_type, false);
                    clap_event_param_mod ev;
                    ev.header.size = sizeof(clap_event_param_mod);
                    ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                    ev.header.flags = 0;
                    ev.header.type = CLAP_EVENT_PARAM_MOD;
                    ev.header.time = i;
                    ev.cookie = nullptr;
                    ev.channel = -1;
                    ev.port_index = -1;
                    ev.key = -1;
                    ev.note_id = -1;
                    ev.param_id = j;
                    ev.amount = m_lfos[j]->outputBlock[0];
                    process->out_events->try_push(process->out_events,
                                                  (const clap_event_header *)&ev);
                }
            }
            ++m_update_counter;
            if (m_update_counter == BLOCK_SIZE)
                m_update_counter = 0;
        }

        return CLAP_PROCESS_CONTINUE;
    }
};

class GainProcessorTest : public XAPWithJuceGUI
{
  public:
    std::vector<clap_param_info> m_param_infos;
    juce::dsp::Gain<float> m_gain_proc;
    double m_volume = 0.0f;
    double m_volume_mod = 0.0f;
    enum class ParamIds
    {
        Volume = 42,
        Smoothing = 666
    };
    GainProcessorTest()
    {
        m_param_infos.push_back(
            makeParamInfo((clap_id)ParamIds::Volume, "Gain", -96.0, 0.0, -6.0,
                          CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE));
        m_param_infos.push_back(makeParamInfo((clap_id)ParamIds::Smoothing, "Smoothing length", 0.0,
                                              1.0, 0.02, CLAP_PARAM_IS_AUTOMATABLE));
    }
    uint32_t audioPortsCount(bool isInput) const noexcept override { return 1; }
    bool audioPortsInfo(uint32_t index, bool isInput,
                                clap_audio_port_info *info) const noexcept override
    {
        info->channel_count = 2;
        info->flags = 0;
        if (isInput)
        {
            info->id = 210;
            strcpy_s(info->name,"Gain processor input");
        }
        else 
        {
            info->id = 250;
            strcpy_s(info->name,"Gain processor output");
        }
        
        info->in_place_pair = false;
        info->port_type = "";
        return true;
    }
    bool activate(double sampleRate, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override
    {
        juce::dsp::ProcessSpec spec;
        spec.maximumBlockSize = maxFrameCount;
        spec.numChannels = 2;
        spec.sampleRate = sampleRate;
        m_gain_proc.prepare(spec);
        m_gain_proc.setRampDurationSeconds(0.01);
        return true;
    }
    uint32_t paramsCount() const noexcept override { return m_param_infos.size(); }
    bool paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept override
    {
        *info = m_param_infos[paramIndex];
        return true;
    }
    clap_process_status process(const clap_process *process) noexcept override
    {
        auto numInChans = process->audio_inputs->channel_count;
        auto numOutChans = process->audio_outputs->channel_count;
        int frames = process->frames_count;
        mergeParameterEvents(process);
        auto inEvents = m_merge_list.clapInputEvents();
        for (int i = 0; i < inEvents->size(inEvents); ++i)
        {
            auto ev = inEvents->get(inEvents, i);
            if (ev->type == CLAP_EVENT_PARAM_VALUE)
            {
                auto pvev = reinterpret_cast<const clap_event_param_value *>(ev);
                if (pvev->param_id == (clap_id)ParamIds::Volume)
                {
                    m_volume = pvev->value;
                }
                else if (pvev->param_id == (clap_id)ParamIds::Smoothing)
                {
                    m_gain_proc.setRampDurationSeconds(pvev->value);
                }
            }
            if (ev->type == CLAP_EVENT_PARAM_MOD)
            {
                auto pvev = reinterpret_cast<const clap_event_param_mod *>(ev);
                if (pvev->param_id == (clap_id)ParamIds::Volume)
                {
                    m_volume_mod = pvev->amount;
                }
            }
        }
        double finalvolume = m_volume + m_volume_mod;
        m_gain_proc.setGainDecibels(finalvolume);
        juce::dsp::AudioBlock<float> inblock(process->audio_inputs[0].data32, 2, frames);
        juce::dsp::AudioBlock<float> outblock(process->audio_outputs[0].data32, 2, frames);
        juce::dsp::ProcessContextNonReplacing<float> ctx(inblock, outblock);
        m_gain_proc.process(ctx);

        return CLAP_PROCESS_CONTINUE;
    }

    bool guiCreate(const char *api, bool isFloating) noexcept override
    {
        m_editor = std::make_unique<xenakios::GenericEditor>(*this);
        m_editor->setSize(500, 100);
        return true;
    }
    void guiDestroy() noexcept override { m_editor = nullptr; }
};

class FilePlayerProcessor : public XAPWithJuceGUI
{
  public:
    double m_sr = 44100;
    std::atomic<bool> m_running_offline{false};
    juce::AudioBuffer<float> m_file_buf;
    juce::AudioBuffer<float> m_file_temp_buf;
    juce::AudioBuffer<float> m_work_buf;
    int m_buf_playpos = 0;
    signalsmith::stretch::SignalsmithStretch<float> m_stretch;
    std::array<juce::LagrangeInterpolator, 2> m_resamplers;
    std::vector<float> m_resampler_work_buf;
    double m_file_sample_rate = 1.0;
    std::vector<clap_param_info> m_param_infos;
    juce::dsp::Gain<float> m_gain_proc;
    double m_volume = 0.0f;
    double m_volume_mod = 0.0f;
    double m_rate = 1.0;
    double m_rate_mod = 0.0;
    double m_pitch = 0.0;
    double m_pitch_mod = 0.0;
    double m_loop_start = 0.0;
    double m_loop_end = 1.0;
    bool m_preserve_pitch = true;
    enum class ParamIds
    {
        Volume = 42,
        Playrate = 666,
        Pitch = 91155,
        PreservePitch = 6543,
        LoopStart = 3322,
        LoopEnd = 888777
    };
    bool getDescriptor(clap_plugin_descriptor *desc) const override
    {
        memset(desc, 0, sizeof(clap_plugin_descriptor));
        desc->clap_version = CLAP_VERSION;
        desc->id = "com.xenakios.fileplayer";
        desc->name = "File player";
        desc->vendor = "Xenakios";
        return true;
    }

    bool guiCreate(const char *api, bool isFloating) noexcept override
    {
        m_editor = std::make_unique<xenakios::GenericEditor>(*this);
        m_editor->setSize(500, 400);
        return true;
    }
    void guiDestroy() noexcept override { m_editor = nullptr; }

    FilePlayerProcessor()
    {
        m_param_infos.push_back(
            makeParamInfo((clap_id)ParamIds::Volume, "Volume", -36.0, 0.0, -6.0,
                          CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE));
        m_param_infos.push_back(
            makeParamInfo((clap_id)ParamIds::Playrate, "Playrate", 0.1, 4.0, 1.0,
                          CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE));
        m_param_infos.push_back(
            makeParamInfo((clap_id)ParamIds::Pitch, "Pitch", -12.0, 12.0, 0.0,
                          CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE));
        m_param_infos.push_back(makeParamInfo((clap_id)ParamIds::PreservePitch, "Preserve pitch",
                                              0.0, 1.0, 1.0,
                                              CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED));
        m_param_infos.push_back(
            makeParamInfo((clap_id)ParamIds::LoopStart, "Loop start", 0.0, 1.0, 0.0,
                          CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE));
        m_param_infos.push_back(
            makeParamInfo((clap_id)ParamIds::LoopEnd, "Loop end", 0.0, 1.0, 1.0,
                          CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE));
        importFile(juce::File(R"(C:\MusicAudio\sourcesamples\there was a time .wav)"));
    }

    bool renderSetMode(clap_plugin_render_mode mode) noexcept override
    {
        if (mode == CLAP_RENDER_OFFLINE)
            m_running_offline = true;
        else
            m_running_offline = false;
        return true;
    }
    bool activate(double sampleRate, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override
    {
        m_sr = sampleRate;
        juce::dsp::ProcessSpec spec;
        spec.maximumBlockSize = maxFrameCount;
        spec.numChannels = 2;
        spec.sampleRate = sampleRate;
        m_gain_proc.prepare(spec);
        m_gain_proc.setRampDurationSeconds(0.01);

        m_buf_playpos = 0;

        m_stretch.presetDefault(2, sampleRate);
        m_work_buf.setSize(2, maxFrameCount * 16);

        for (auto &rs : m_resamplers)
            rs.reset();
        m_resampler_work_buf.resize(maxFrameCount * 32);

        return true;
    }
    uint32_t paramsCount() const noexcept override { return m_param_infos.size(); }
    bool paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept override
    {
        *info = m_param_infos[paramIndex];
        return true;
    }
    bool paramsValue(clap_id paramId, double *value) noexcept override
    {
        return false;
        std::optional<double> result;
        if (paramId == (clap_id)ParamIds::Volume)
            result = m_volume;
        else if (paramId == (clap_id)ParamIds::Playrate)
            result = m_rate;
        else if (paramId == (clap_id)ParamIds::Pitch)
            result = m_pitch;
        else if (paramId == (clap_id)ParamIds::PreservePitch)
            result = m_preserve_pitch;
        else if (paramId == (clap_id)ParamIds::LoopStart)
            result = m_loop_start;
        else if (paramId == (clap_id)ParamIds::LoopEnd)
            result = m_loop_end;
        if (result)
        {
            return true;
        }
        return false;
    }
    void importFile(juce::File f)
    {
        juce::AudioFormatManager man;
        man.registerBasicFormats();
        auto reader = man.createReaderFor(f);
        jassert(reader);
        if (reader)
        {
            m_file_buf.setSize(2, reader->lengthInSamples);
            reader->read(&m_file_buf, 0, reader->lengthInSamples, 0, true, true);
            m_file_sample_rate = reader->sampleRate;
            delete reader;
        }
    }
    void handleEvent(const clap_event_header *ev)
    {
        if (ev->type == CLAP_EVENT_PARAM_VALUE)
        {
            auto aev = reinterpret_cast<const clap_event_param_value *>(ev);
            if (aev->param_id == to_clap_id(ParamIds::Volume))
                m_volume = aev->value;
            if (aev->param_id == to_clap_id(ParamIds::Playrate))
                m_rate = aev->value;
            if (aev->param_id == to_clap_id(ParamIds::Pitch))
                m_pitch = aev->value;
            if (aev->param_id == to_clap_id(ParamIds::LoopStart))
                m_loop_start = aev->value;
            if (aev->param_id == to_clap_id(ParamIds::LoopEnd))
                m_loop_end = aev->value;
            if (aev->param_id == to_clap_id(ParamIds::PreservePitch))
                m_preserve_pitch = aev->value;
        }
        if (ev->space_id == XENAKIOS_CLAP_NAMESPACE && ev->type == XENAKIOS_EVENT_CHANGEFILE)
        {
            auto fch = reinterpret_cast<const xenakios_event_change_file *>(ev);
            // this should obviously be asynced in some way when realtime...
            if (m_running_offline)
                importFile(juce::File(fch->filepath));
            else
                importFile(juce::File(fch->filepath));
        }
    }
    clap_process_status process(const clap_process *process) noexcept override
    {
        auto inevts = process->in_events;
        for (int i = 0; i < inevts->size(inevts); ++i)
        {
            auto ev = inevts->get(inevts, i);
            handleEvent(ev);
        }
        if (m_file_buf.getNumSamples() == 0)
        {
            jassert(false);
            return CLAP_PROCESS_CONTINUE;
        }

        int loop_start_samples = m_loop_start * m_file_buf.getNumSamples();
        int loop_end_samples = m_loop_end * m_file_buf.getNumSamples();
        if (loop_start_samples > loop_end_samples)
            std::swap(loop_start_samples, loop_end_samples);
        if (loop_start_samples == loop_end_samples)
        {
            loop_end_samples += 4100;
            if (loop_end_samples >= m_file_buf.getNumSamples())
            {
                loop_start_samples = m_file_buf.getNumSamples() - 4100;
                loop_end_samples = m_file_buf.getNumSamples() - 1;
            }
        }
        if (m_buf_playpos < loop_start_samples)
            m_buf_playpos = loop_start_samples;
        if (m_buf_playpos >= loop_end_samples)
            m_buf_playpos = loop_start_samples;
        auto filebuf = m_file_buf.getArrayOfReadPointers();

        auto wbuf = m_work_buf.getArrayOfWritePointers();
        int cachedpos = m_buf_playpos;
        float compensrate = m_file_sample_rate / m_sr;
        bool preserve_pitch = true;
        float rate = m_rate;
        auto getxfadedsample = [](const float *srcbuf, int index, int start, int end,
                                  int xfadelen) {
            // not within xfade region so just return original sample
            int xfadestart = end - xfadelen;
            if (index >= start && index < xfadestart)
                return srcbuf[index];

            float xfadegain = juce::jmap<float>(index, xfadestart, end - 1, 1.0f, 0.0f);
            jassert(xfadegain >= 0.0f && xfadegain <= 1.0);
            float s0 = srcbuf[index];
            int temp = index - xfadestart + (start - xfadelen);
            if (temp < 0)
                return s0 * xfadegain;
            jassert(temp >= 0 && temp < end);
            float s1 = srcbuf[temp];
            return s0 * xfadegain + s1 * (1.0f - xfadegain);
        };
        int xfadelen = 4000;
        if (preserve_pitch)
        {
            float pshift = m_pitch;
            float pitchratio = std::pow(2.0, pshift / 12.0);
            m_stretch.setTransposeFactor(pitchratio * compensrate);
            rate *= compensrate;
            int samplestopush = process->frames_count * rate;
            for (int i = 0; i < samplestopush; ++i)
            {
                wbuf[0][i] = getxfadedsample(filebuf[0], m_buf_playpos, loop_start_samples,
                                             loop_end_samples, xfadelen);
                wbuf[1][i] = getxfadedsample(filebuf[1], m_buf_playpos, loop_start_samples,
                                             loop_end_samples, xfadelen);
                ++m_buf_playpos;
                if (m_buf_playpos >= loop_end_samples)
                    m_buf_playpos = loop_start_samples;
            }
            m_stretch.process(m_work_buf.getArrayOfReadPointers(), samplestopush,
                              process->audio_outputs[0].data32, process->frames_count);
        }
        else
        {
            rate *= compensrate;
            int samplestopush = process->frames_count * rate;
            int consumed[2] = {0, 0};
            samplestopush += 1;
            for (int i = 0; i < samplestopush; ++i)
            {
                wbuf[0][i] = getxfadedsample(filebuf[0], m_buf_playpos, loop_start_samples,
                                             loop_end_samples, xfadelen);
                wbuf[1][i] = getxfadedsample(filebuf[1], m_buf_playpos, loop_start_samples,
                                             loop_end_samples, xfadelen);
                ++m_buf_playpos;
                if (m_buf_playpos >= loop_end_samples)
                    m_buf_playpos = loop_start_samples;
            }
            for (int ch = 0; ch < 2; ++ch)
            {
                consumed[ch] =
                    m_resamplers[ch].process(rate, wbuf[ch], process->audio_outputs[0].data32[ch],
                                             process->frames_count, samplestopush, 0);
            }
            jassert(consumed[0] == consumed[1]);
            m_buf_playpos = (cachedpos + consumed[0]);
            if (m_buf_playpos >= loop_end_samples)
            {
                m_buf_playpos = loop_start_samples;
            }
        }
        m_gain_proc.setGainDecibels(m_volume);
        juce::dsp::AudioBlock<float> block(process->audio_outputs[0].data32, 2,
                                           process->frames_count);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        m_gain_proc.process(ctx);
        return CLAP_PROCESS_CONTINUE;
    }
};

const size_t maxHolderDataSize = 128;

struct ClapEventHolder
{
    template <typename T> T *dataAsEvent() { return reinterpret_cast<T *>(m_data.data()); }
    static ClapEventHolder makeNoteExpressionEvent(uint16_t expressionType, double tpos, int port,
                                                   int channel, int key, int32_t noteId, double amt)
    {
        static_assert(sizeof(clap_event_note_expression) <= maxHolderDataSize,
                      "Clap event holder data size exceeded");
        ClapEventHolder holder;
        holder.m_time_stamp = tpos;
        holder.m_eventType = CLAP_EVENT_NOTE_EXPRESSION;
        clap_event_note_expression *ev =
            reinterpret_cast<clap_event_note_expression *>(holder.m_data.data());
        ev->header.flags = 0;
        ev->header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev->header.time = 0;
        ev->header.type = CLAP_EVENT_NOTE_EXPRESSION;
        ev->header.size = sizeof(clap_event_note_expression);
        ev->channel = channel;
        ev->expression_id = expressionType;
        ev->key = key;
        ev->note_id = noteId;
        ev->port_index = 0;
        ev->value = amt;
        return holder;
    }
    static ClapEventHolder makeNoteEvent(uint16_t eventType, double tpos, int port, int channel,
                                         int key, int32_t noteId, double velo)
    {
        static_assert(sizeof(clap_event_note) <= maxHolderDataSize,
                      "Clap event holder data size exceeded");
        ClapEventHolder holder;
        holder.m_time_stamp = tpos;
        holder.m_eventType = eventType;
        clap_event_note *ev = reinterpret_cast<clap_event_note *>(holder.m_data.data());
        ev->header.flags = 0;
        ev->header.type = eventType;
        ev->header.size = sizeof(clap_event_note);
        ev->header.time = 0;
        ev->header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev->channel = channel;
        ev->port_index = port;
        ev->key = key;
        ev->velocity = velo;
        ev->note_id = noteId;
        return holder;
    }
    double m_time_stamp = 0.0;
    uint16_t m_eventType = 0;
    ClapEventHolder *m_assoc_note_off_event = nullptr;
    std::array<unsigned char, maxHolderDataSize> m_data;
};

using SequenceType = std::vector<ClapEventHolder>;

inline void sortSequence(SequenceType &c)
{
    choc::sorting::stable_sort(c.begin(), c.end(), [](const auto &a, const auto &b) {
        return a.m_time_stamp < b.m_time_stamp;
    });
}

struct ClapEventIterator
{
    /// Creates an iterator positioned at the start of the sequence.
    ClapEventIterator(const SequenceType &o) : owner(o) {}
    ClapEventIterator(const ClapEventIterator &) = default;
    ClapEventIterator(ClapEventIterator &&) = default;

    /// Seeks the iterator to the given time
    void setTime(double newTimeStamp)
    {
        auto eventData = owner.data();

        while (nextIndex != 0 && eventData[nextIndex - 1].m_time_stamp >= newTimeStamp)
            --nextIndex;

        while (nextIndex < owner.size() && eventData[nextIndex].m_time_stamp < newTimeStamp)
            ++nextIndex;

        currentTime = newTimeStamp;
    }

    /// Returns the current iterator time
    double getTime() const noexcept { return currentTime; }

    /// Returns a set of events which lie between the current time, up to (but not
    /// including) the given duration. This function then increments the iterator to
    /// set its current time to the end of this block.
    std::pair<int, int> readNextEvents(double duration)
    {
        auto start = nextIndex;
        auto eventData = owner.data();
        auto end = start;
        auto total = owner.size();
        auto endTime = currentTime + duration;
        currentTime = endTime;

        while (end < total && eventData[end].m_time_stamp < endTime)
            ++end;

        nextIndex = end;

        return {start, end};
    }

  private:
    const SequenceType &owner;
    double currentTime = 0;
    size_t nextIndex = 0;
};

class ClapEventSequencerProcessor : public xenakios::XAudioProcessor
{
    SequenceType m_events;
    ClapEventIterator m_event_iter;
    double m_sr = 1.0;
    DejaVuRandom m_dvpitchrand;
    DejaVuRandom m_dvchordrand;
    DejaVuRandom m_dvvelorand;

  public:
    ClapEventSequencerProcessor(int seed, double pulselen)
        : m_event_iter(m_events), m_dvpitchrand(seed), m_dvchordrand(seed + 13),
          m_dvvelorand(seed + 101)
    {
        m_events.reserve(4096);
        std::uniform_real_distribution<float> pitchdist{48.0f, 72.0f};
        std::uniform_int_distribution<int> chorddist{0, 4};
        std::uniform_real_distribution<float> accentdist{0.0f, 1.0f};
        // std::discrete_distribution<> d({40, 10, 10, 40});

        m_dvpitchrand.m_loop_len = 3;
        m_dvpitchrand.m_deja_vu = 0.48;
        m_dvchordrand.m_loop_len = 3;
        m_dvchordrand.m_deja_vu = 0.4;
        m_dvvelorand.m_loop_len = 3;
        m_dvvelorand.m_deja_vu = 0.3;

        float chord_notes[5][3] = {{0.0f, 3.1564f, 7.02f},
                                   {0.0f, 3.8631f, 7.02f},
                                   {-12.0f, 7.02f, 10.8827f},
                                   {0.0f, 4.9804f, 10.1760f},
                                   {-12.0f, 0.0f, 12.0f}};
        for (int i = 0; i < 480; ++i)
        {
            int key = pitchdist(m_dvpitchrand);
            float root = key;
            int chordtype = chorddist(m_dvchordrand);

            double velo = 0.5;
            if (accentdist(m_dvvelorand) > 0.75)
                velo = 1.0;
            double tpos = i * pulselen;
            for (int j = 0; j < 3; ++j)
            {
                double offtpos = tpos + j * 0.07;
                float pitch = key + chord_notes[chordtype][j];
                m_events.push_back(ClapEventHolder::makeNoteEvent(CLAP_EVENT_NOTE_ON, offtpos, 0, 0,
                                                                  pitch, -1, velo));
                float fracpitch = pitch - (int)pitch;
                m_events.push_back(ClapEventHolder::makeNoteExpressionEvent(
                    CLAP_NOTE_EXPRESSION_TUNING, offtpos, 0, 0, pitch, -1, fracpitch));
                double pan = juce::jmap<double>(j, 0, 2, 0.05, 0.95);
                m_events.push_back(ClapEventHolder::makeNoteExpressionEvent(
                    CLAP_NOTE_EXPRESSION_PAN, offtpos, 0, 0, pitch, -1, pan));
                m_events.push_back(ClapEventHolder::makeNoteEvent(
                    CLAP_EVENT_NOTE_OFF, offtpos + pulselen * 0.90, 0, 0, pitch, -1, 0.0));
            }
        }
        sortSequence(m_events);
    }
    bool activate(double sampleRate, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override
    {
        m_sr = sampleRate;
        return true;
    }
    uint32_t paramsCount() const noexcept override { return 0; }
    bool paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept override
    {
        return false;
    }
    bool m_processing_started = false;
    clap_process_status process(const clap_process *process) noexcept override
    {
        if (!m_processing_started)
        {
            m_processing_started = true;
            m_event_iter.setTime(0.0);
        }
        if (process->transport)
        {
            // note here the oddity that the clap transport doesn't contain floating point seconds!
            auto curtime = process->transport->song_pos_seconds / (double)CLAP_SECTIME_FACTOR;
            // m_event_iter.setTime(curtime);
            auto events = m_event_iter.readNextEvents(process->frames_count / m_sr);
            if (events.first != events.second)
            {
                for (int i = events.first; i < events.second; ++i)
                {
                    auto etype = m_events[i].m_eventType;
                    if (etype == CLAP_EVENT_NOTE_ON || etype == CLAP_EVENT_NOTE_OFF)
                    {
                        auto ev = m_events[i].dataAsEvent<clap_event_note>();
                        // std::cout << "GENERATED NOTE EVENT " << ev->header.type << " "
                        //           << m_events[i].m_time_stamp << "\n";
                        process->out_events->try_push(process->out_events,
                                                      reinterpret_cast<clap_event_header *>(ev));
                    }
                    if (etype == CLAP_EVENT_NOTE_EXPRESSION)
                    {
                        auto ev = m_events[i].dataAsEvent<clap_event_note_expression>();
                        // std::cout << "GENERATED NOTE EXP EVENT " << ev->header.type << " "
                        //           << m_events[i].m_time_stamp << " " << ev->value << "\n";
                        process->out_events->try_push(process->out_events,
                                                      reinterpret_cast<clap_event_header *>(ev));
                    }
                }
            }
        }

        return CLAP_PROCESS_CONTINUE;
    }
};
