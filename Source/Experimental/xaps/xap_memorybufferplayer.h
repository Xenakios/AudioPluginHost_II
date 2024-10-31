#pragma once

#include "audio/choc_SampleBuffers.h"
#include "../Common/xaudioprocessor.h"
#include <vector>
#include <iostream>

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
        int fadelensamples = m_outsr * 0.25;
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
                m_bufplaypos = 0;
                std::cout << "set buffer " << m_playbuf << " with " << m_playbufframes << " frames "
                          << bufev->numchans << " channels\n";
            }
            if (ev->type == XENAKIOS_ROUTING_MSG)
            {
                auto roev = (clap_event_xen_audiorouting *)ev;
                if (roev->opcode == 0)
                {
                    for(auto& r : m_routings)
                    {
                        r.fadedir = -1;
                        r.fadecounter = 0;
                    }
                    
                }
                if (roev->opcode == 1)
                {
                    for(auto& r : m_routings)
                    {
                        r.fadedir = -1;
                        r.fadecounter = 0;
                    }
                    for (int j = 0; j < outchans; ++j)
                    {
                        int inchantouse = j % m_playbufchans;
                        m_routings.emplace_back(inchantouse, j);
                        m_routings.back().fadecounter = 0;
                        m_routings.back().fadedir = 1;
                    }
                }
                if (roev->opcode == 2)
                {
                    std::cout << "connecting " << roev->src << " to " << roev->dest << "\n";
                    m_routings.emplace_back(roev->src, roev->dest);
                    m_routings.back().fadedir = 1;
                    m_routings.back().fadecounter = 0;
                }
                if (roev->opcode == 3)
                {
                    for (auto &r : m_routings)
                    {
                        if (r.srcchan == roev->src && r.destchan == roev->dest)
                        {
                            r.fadecounter = 0;
                            r.fadedir = -1;
                        }
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
        if (m_playbuf)
        {
            auto cachedpos = 0;
            for (auto &r : m_routings)
            {
                cachedpos = m_bufplaypos;
                if (r.srcchan < m_playbufchans && r.destchan < outchans)
                {
                    int buf_offset = m_playbufframes * r.srcchan;
                    for (int i = 0; i < nframes; ++i)
                    {
                        float gain = 0.0f;
                        if (r.fadedir == 1)
                            gain = xenakios::mapvalue<float>(r.fadecounter, 0, fadelensamples, 0.0f,
                                                             1.0f);
                        else if (r.fadedir == -1 && r.fadecounter < fadelensamples)
                            gain = xenakios::mapvalue<float>(r.fadecounter, 0, fadelensamples, 1.0f,
                                                             0.0f);
                        gain = std::clamp(gain, 0.0f, 1.0f);
                        process->audio_outputs[0].data32[r.destchan][i] +=
                            m_playbuf[buf_offset + cachedpos] * gain;
                        ++r.fadecounter;
                        ++cachedpos;
                        if (cachedpos >= m_playbufframes)
                        {
                            cachedpos = 0;
                        }
                    }
                }
            }
            m_bufplaypos = cachedpos;
        }

        for (int i = m_routings.size() - 1; i >= 0; i--)
        {
            auto &r = m_routings[i];
            if (r.fadecounter >= fadelensamples && r.fadedir == -1)
            {
                m_routings.erase(m_routings.begin() + i);
            }
        }

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
        bool operator==(const Routing &other) const
        {
            return srcchan == other.srcchan && destchan == other.destchan;
        }
        int fadedir = 0;
        int fadecounter = 0;
    };
    std::vector<Routing> m_routings;
};
