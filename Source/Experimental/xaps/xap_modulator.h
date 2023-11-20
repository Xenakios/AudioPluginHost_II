#pragma once

#include "../xapwithjucegui.h"
#include "../xap_generic_editor.h"

#include "sst/basic-blocks/modulators/SimpleLFO.h"

class ModulatorSource : public XAPWithJuceGUI
{
  public:
    std::vector<clap_param_info> m_param_infos;
    enum class ParamIds
    {
        ModType = 4900,
        Rate = 665,
        PolyShift = 5,
        Deform = 900
    };
    int m_mod_type = 0;
    double m_rate = 0.0;
    double m_poly_shift = 0.0;
    double m_deform = 0.0;
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
        m_editor->setSize(500, 150);
        return true;
    }
    void guiDestroy() noexcept override { m_editor = nullptr; }
    bool m_is_audio_rate = false;
    uint32_t audioPortsCount(bool isInput) const noexcept override
    {
        if (isInput)
            return 0;
        if (!m_is_audio_rate)
            return 0;
        return 1;
    }
    bool audioPortsInfo(uint32_t index, bool isInput, clap_audio_port_info *info) const noexcept override
    {
        if (isInput || !m_is_audio_rate)
            return false;
        info->channel_count = m_lfos.size();
        info->flags = 0;
        info->id = 49137;
        info->in_place_pair = 0;
        strcpy_s(info->name, "XAP Modulator output");
        info->port_type = nullptr;
        return true;
    }
    ModulatorSource(int maxpolyphony, double initialRate, bool audiorate = false)
    {
        jassert(maxpolyphony > 0);
        m_is_audio_rate = audiorate;
        m_rate = initialRate;
        for (int i = 0; i < maxpolyphony; ++i)
        {
            m_lfos.push_back(std::make_unique<LFOType>(this));
        }
        m_param_infos.push_back(makeParamInfo((clap_id)ParamIds::ModType, "Modulation type", 0.0,
                                              6.0, 0.0,
                                              CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED));
        m_param_infos.push_back(
            makeParamInfo((clap_id)ParamIds::Deform, "Deform", -1.0, 1.0, 0.0,
                          CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE));
        m_param_infos.push_back(
            makeParamInfo((clap_id)ParamIds::Rate, "Rate", -3.0, 3.0, 0.00,
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
        for (int i = 0; i < BLOCK_SIZE; ++i)
            m_ring_buf[i] = 0.0f;
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
            if (pev->param_id == to_clap_id(ParamIds::Deform))
                m_deform = pev->value;
            if (pev->param_id == to_clap_id(ParamIds::ModType))
                m_mod_type = static_cast<int>(pev->value);
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
                handleInboundEvent(reinterpret_cast<const clap_event_header *>(&pev));
            }
        }
        auto inevents = process->in_events;
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
                    double polyshift_to_use = juce::jmap<double>(j, 0, m_lfos.size(), 0.0, 1.0);
                    m_lfos[j]->applyPhaseOffset(polyshift_to_use);
                    m_lfos[j]->process_block(m_rate, m_deform, m_mod_type, false);
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
    std::array<float, BLOCK_SIZE> m_ring_buf;
};
