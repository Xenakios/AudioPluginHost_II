#pragma once

#include "xaudioprocessor.h"
#include "JuceHeader.h"

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

class ToneProcessorTest : public xenakios::XAudioProcessor
{
  public:
    std::vector<clap_param_info> m_param_infos;
    juce::dsp::Oscillator<float> m_osc;
    double m_pitch = 60.0;
    double m_pitch_mod = 0.0f;
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
        auto inEvents = process->in_events;
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
        if (!m_outputs_modulation)
        {
            for (int i = 0; i < process->frames_count; ++i)
            {
                float s = m_osc.processSample(0.0f);
                s = std::tanh(s) * 0.5;
                process->audio_outputs[0].data32[0][i] = s;
                process->audio_outputs[0].data32[1][i] = s;
            }
        }
        else
        {
            for (int i = 0; i < process->frames_count; ++i)
            {
                float s = m_osc.processSample(0.0f);
                if (m_mod_out_counter == 0)
                {
                    clap_event_param_mod pv;
                    pv.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                    pv.header.size = sizeof(clap_event_param_mod);
                    pv.header.flags = 0;
                    pv.header.time = i;
                    pv.header.type = CLAP_EVENT_PARAM_MOD;
                    pv.cookie = nullptr;
                    pv.param_id = m_mod_out_par_id;
                    pv.amount = s;
                    process->out_events->try_push(process->out_events,
                                                  reinterpret_cast<const clap_event_header *>(&pv));
                }
                ++m_mod_out_counter;
                if (m_mod_out_counter == m_mod_out_block_size)
                {
                    m_mod_out_counter = 0;
                }
            }
        }

        return CLAP_PROCESS_CONTINUE;
    }
    int m_mod_out_block_size = 401;
    int m_mod_out_counter = 0;
};

class GainProcessorTest : public xenakios::XAudioProcessor
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
        auto inEvents = process->in_events;
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
};

class JucePluginWrapper : public xenakios::XAudioProcessor, public juce::AudioPlayHead
{
  public:
    std::vector<clap_param_info> m_param_infos;
    std::unique_ptr<juce::AudioPluginInstance> m_internal;

    JucePluginWrapper(juce::String plugfile)
    {
        juce::AudioPluginFormatManager plugmana;
        plugmana.addDefaultFormats();
        for (auto &e : plugmana.getFormats())
        {
            std::cout << e->getName() << "\n";
        }
        juce::KnownPluginList klist;
        juce::OwnedArray<juce::PluginDescription> typesFound;
        juce::VST3PluginFormat f;
        klist.scanAndAddFile(plugfile, true, typesFound, f);
        for (auto &e : typesFound)
        {
            std::cout << e->name << "\n";
        }
        if (typesFound.size() > 0)
        {
            juce::String err;
            m_internal = plugmana.createPluginInstance(*typesFound[0], 44100, 512, err);
            std::cout << err << "\n";
        }
    }
    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override
    {
        return m_transport_pos;
    }
    juce::AudioPlayHead::PositionInfo m_transport_pos;
    bool activate(double sampleRate, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override
    {
        jassert(m_internal);
        m_internal->setPlayHead(this);
        m_work_buf.setSize(2, maxFrameCount);
        m_work_buf.clear();
        m_internal->enableAllBuses();
        m_internal->setPlayConfigDetails(2, 2, sampleRate, maxFrameCount);
        m_internal->prepareToPlay(sampleRate, maxFrameCount);
        m_param_infos.clear();
        auto &pars = m_internal->getParameters();
        clap_id parid = 0;
        for (auto &par : pars)
        {
            auto pinfo = makeParamInfo(parid, par->getName(100), 0.0, 1.0, par->getDefaultValue(),
                                       CLAP_PARAM_IS_AUTOMATABLE);
            m_param_infos.push_back(pinfo);
            // std::cout << parid << "\t" << par->getName(100) << "\t" << par->getDefaultValue()
            //           << "\n";
            ++parid;
        }
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
        jassert(m_internal);
        if (process->transport)
        {
            m_transport_pos.setBpm(process->transport->tempo);
        }

        m_midi_buffer.clear();
        auto numInChans = process->audio_inputs->channel_count;
        auto numOutChans = process->audio_outputs->channel_count;
        int frames = process->frames_count;
        auto inEvents = process->in_events;
        auto &pars = m_internal->getParameters();
        for (int i = 0; i < inEvents->size(inEvents); ++i)
        {
            auto ev = inEvents->get(inEvents, i);
            if (ev->type == CLAP_EVENT_PARAM_VALUE)
            {
                auto pvev = reinterpret_cast<const clap_event_param_value *>(ev);
                if (pvev->param_id >= 0 && pvev->param_id < pars.size())
                {
                    pars[pvev->param_id]->setValue(pvev->value);
                }
            }
            if (m_internal->acceptsMidi())
            {
                if (ev->type == CLAP_EVENT_MIDI)
                {
                    auto mev = reinterpret_cast<const clap_event_midi *>(ev);
                    juce::MidiMessage midimsg(mev->data[0], mev->data[1], mev->data[2], 0);
                    m_midi_buffer.addEvent(midimsg, mev->header.time);
                }
                auto mev = reinterpret_cast<const clap_event_note *>(ev);
                if (ev->type == CLAP_EVENT_NOTE_ON)
                {
                    m_midi_buffer.addEvent(
                        juce::MidiMessage::noteOn(mev->channel + 1, mev->key, (float)mev->velocity),
                        mev->header.time);
                }
                else if (ev->type == CLAP_EVENT_NOTE_OFF || ev->type == CLAP_EVENT_NOTE_CHOKE)
                {
                    m_midi_buffer.addEvent(juce::MidiMessage::noteOff(mev->channel + 1, mev->key,
                                                                      (float)mev->velocity),
                                           mev->header.time);
                }
            }
        }
        for (int i = 0; i < 2; ++i)
        {
            for (int j = 0; j < frames; ++j)
            {
                m_work_buf.setSample(i, j, process->audio_inputs[0].data32[i][j]);
            }
        }

        m_internal->processBlock(m_work_buf, m_midi_buffer);
        for (int i = 0; i < 2; ++i)
        {
            for (int j = 0; j < frames; ++j)
            {
                process->audio_outputs[0].data32[i][j] = m_work_buf.getSample(i, j);
            }
        }
        return CLAP_PROCESS_CONTINUE;
    }
    uint32_t notePortsCount(bool isInput) const noexcept override
    {
        if (isInput && m_internal->acceptsMidi())
            return 1;
        return 0;
    }
    bool notePortsInfo(uint32_t, bool isInput, clap_note_port_info *info) const noexcept override
    {
        if (m_internal->acceptsMidi() && isInput)
        {
            info->id = 1000;
            strcpy_s(info->name, "JUCE Wrapper MIDI input");
            info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
            info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
            return true;
        }
        if (m_internal->producesMidi() && !isInput)
        {
            info->id = 2000;
            strcpy_s(info->name, "JUCE Wrapper MIDI output");
            info->supported_dialects = CLAP_NOTE_DIALECT_MIDI;
            info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
            return true;
        }
        return false;
    }
    juce::AudioBuffer<float> m_work_buf;
    juce::MidiBuffer m_midi_buffer;
};