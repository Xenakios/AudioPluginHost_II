#pragma once

#include "audio/choc_SampleBuffers.h"
#include "../xaudioprocessor.h"
#include <vector>

class XapMemoryBufferPlayer : public xenakios::XAudioProcessor
{

  public:
    using BufViewType = choc::buffer::ChannelArrayView<double>;
    XapMemoryBufferPlayer() { m_routings.reserve(64); }

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
        info->channel_count = 2;
        info->flags = 0;
        info->id = 50068;
        info->in_place_pair = CLAP_INVALID_ID;
        info->port_type = nullptr;
        strcpy_s(info->name, "Output");
        return true;
    }
    bool activate(double sampleRate, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override
    {
        m_outsr = sampleRate;
        m_bufplaypos = 0;
        return true;
    }
    clap_process_status process(const clap_process *process) noexcept override
    {
        auto inEvents = process->in_events;
        auto sz = inEvents->size(inEvents);
        auto outchans = process->audio_outputs[0].channel_count;
        for (uint32_t i = 0; i < sz; ++i)
        {
            auto ev = inEvents->get(inEvents, i);
            if (ev->type == XENAKIOS_AUDIOBUFFER_MSG)
            {
                auto bufev = (clap_event_xen_audiobuffer *)ev;
                m_playbuf = bufev->buffer;
                m_playbufchans = bufev->numchans;
                m_playbufframes = bufev->numframes;
                m_inputsr = bufev->samplerate;
                std::cout << "set buffer " << m_playbuf << " with " << m_playbufframes << " frames "
                          << bufev->numchans << " channels\n";
                m_routings.clear();
                if (outchans >= 2 && m_playbufchans == 1)
                {
                    for (int j = 0; j < outchans; ++j)
                    {
                        m_routings.emplace_back(0, j);
                    }
                }
                if (outchans == m_playbufchans)
                {
                    for (int j = 0; j < outchans; ++j)
                    {
                        m_routings.emplace_back(j, j);
                    }
                }
            }
        }
        auto nframes = process->frames_count;

        for (int i = 0; i < nframes; ++i)
        {
            for (int j = 0; j < outchans; ++j)
            {
                process->audio_outputs[0].data32[j][i] = 0.0f;
            }
        }
        int inchans = m_playbufchans;
        auto cachedpos = 0;
        for (const auto &r : m_routings)
        {
            cachedpos = m_bufplaypos;
            if (r.srcchan < inchans && r.destchan < outchans)
            {
                int buf_offset = m_playbufframes * r.srcchan;
                for (int i = 0; i < nframes; ++i)
                {
                    process->audio_outputs[0].data32[r.destchan][i] +=
                        m_playbuf[buf_offset + cachedpos];
                    ++cachedpos;
                    if (cachedpos >= m_playbufframes)
                    {
                        cachedpos = 0;
                    }
                }
            }
        }
        m_bufplaypos = cachedpos;
        return CLAP_PROCESS_CONTINUE;
    }

  private:
    double *m_playbuf = nullptr;
    int m_playbufframes = 0;
    int m_playbufchans = 0;
    double m_inputsr = 0.0;
    double m_outsr = 0.0;
    int m_bufplaypos = 0;
    struct Routing
    {
        Routing() {}
        Routing(int src, int dest) : srcchan(src), destchan(dest) {}
        int srcchan = 0;
        int destchan = 0;
    };
    std::vector<Routing> m_routings;
};
