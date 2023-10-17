#pragma once

#include "xaudioprocessor.h"
#include "JuceHeader.h"

#include "containers/choc_NonAllocatingStableSort.h"
#include "containers/choc_SingleReaderSingleWriterFIFO.h"
#include "sst/basic-blocks/modulators/SimpleLFO.h"
#include "xap_generic_editor.h"
#include "xapwithjucegui.h"

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

class ToneProcessorTest : public XAPWithJuceGUI
{
  public:
    std::vector<clap_param_info> m_param_infos;
    juce::dsp::Oscillator<float> m_osc;
    double m_pitch = 60.0;
    double m_pitch_mod = 0.0f;
    double m_distortion_amt = 0.3;
    double m_distortion_mod = 0.0;
    enum class ParamIds
    {
        Pitch = 5034,
        Distortion = 9

    };

    ToneProcessorTest()
    {
        m_osc.initialise([](float x) { return std::sin(x); }, 1024);
        m_param_infos.push_back(
            makeParamInfo((clap_id)ParamIds::Pitch, "Pitch", 0.0, 127.0, 60.0,
                          CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE));

        m_param_infos.push_back(
            makeParamInfo((clap_id)ParamIds::Distortion, "Distortion", 0.0, 1.0, 0.3,
                          CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE));
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
    void handleEvent(const clap_event_header *ev)
    {
        // if (ev->space_id != CLAP_CORE_EVENT_SPACE_ID)
        //     return;
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
            if (pvev->param_id == (clap_id)ParamIds::Distortion)
            {
                m_distortion_mod = pvev->amount;
            }
        }
    }
    
    clap_process_status process(const clap_process *process) noexcept override
    {
        xenakios::CrossThreadMessage msg;
        while (m_from_ui_fifo.pop(msg))
        {
            if (msg.eventType == CLAP_EVENT_PARAM_VALUE)
            {
                auto pev = makeClapParameterValueEvent(0, msg.paramId, msg.value);
                handleEvent(reinterpret_cast<const clap_event_header *>(&pev));
            }
        }

        auto inEvents = process->in_events;
        for (int i = 0; i < inEvents->size(inEvents); ++i)
        {
            auto ev = inEvents->get(inEvents, i);
            handleEvent(ev);
        }
        double finalpitch = m_pitch + m_pitch_mod;
        finalpitch = std::clamp(finalpitch, 0.0, 127.0);
        double hz = 440.0 * std::pow(2.0, 1.0 / 12 * (finalpitch - 69.0));
        m_osc.setFrequency(hz, true);
        double finaldistamount = (m_distortion_amt + m_distortion_mod);
        finaldistamount = std::clamp(finaldistamount, 0.0, 1.0);
        double distvolume = finaldistamount * 48.0;
        double distgain = juce::Decibels::decibelsToGain(distvolume);
        double distmix = 1.0f;
        if (finaldistamount < 0.001)
            distmix = 0.0;
        for (int i = 0; i < process->frames_count; ++i)
        {
            float s = m_osc.processSample(0.0f);
            if (distmix > 0.0)
                s = std::tanh(s * distgain);
            s *= 0.25;
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
            makeParamInfo((clap_id)ParamIds::Volume, "Gain", -96.0, 0.0, -12.0,
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
            strcpy_s(info->name, "Gain processor input");
        }
        else
        {
            info->id = 250;
            strcpy_s(info->name, "Gain processor output");
        }

        info->in_place_pair = false;
        info->port_type = "";
        return true;
    }
    bool activate(double sampleRate, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override
    {
        m_gain_proc.prepare({sampleRate, maxFrameCount, 2});
        m_gain_proc.setRampDurationSeconds(0.01);
        return true;
    }
    uint32_t paramsCount() const noexcept override { return m_param_infos.size(); }
    bool paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept override
    {
        *info = m_param_infos[paramIndex];
        return true;
    }
    void handleEvent(const clap_event_header *ev)
    {
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
    clap_process_status process(const clap_process *process) noexcept override
    {
        auto numInChans = process->audio_inputs->channel_count;
        auto numOutChans = process->audio_outputs->channel_count;
        int frames = process->frames_count;
        xenakios::CrossThreadMessage msg;
        while (m_from_ui_fifo.pop(msg))
        {
            if (msg.eventType == CLAP_EVENT_PARAM_VALUE)
            {
                auto pev = makeClapParameterValueEvent(0, msg.paramId, msg.value);
                handleEvent((const clap_event_header *)&pev);
                // should also add these into output events, but since we don't
                // have automation recording yet in the host...
            }
        }
        auto inEvents = process->in_events;
        for (int i = 0; i < inEvents->size(inEvents); ++i)
        {
            auto ev = inEvents->get(inEvents, i);
            handleEvent(ev);
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
