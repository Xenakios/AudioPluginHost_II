#pragma once

#include "audio/choc_SampleBuffers.h"
#include "../xaudioprocessor.h"

class XapMemoryBufferPlayer : public xenakios::XAudioProcessor
{

  public:
    using BufViewType = choc::buffer::ChannelArrayView<double>;
    XapMemoryBufferPlayer() {}
    void setBuffer(BufViewType bufview, double bufSampleRate)
    {
        m_inputsr = bufSampleRate;
        m_buf_view = bufview;
        m_bufplaypos = 0;
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
        auto nframes = process->frames_count;
        auto outchans = process->audio_outputs[0].channel_count;
        if (m_buf_view.getNumFrames() == 0)
        {
            for (int i = 0; i < nframes; ++i)
            {
                for (int j = 0; j < outchans; ++j)
                {
                    process->audio_outputs[0].data32[j][i] = 0.0f;
                }
            }
            return CLAP_PROCESS_CONTINUE;
        }
        int inchans = m_buf_view.getNumChannels();
        if (inchans == 1 && outchans == 2)
        {
            for (int i = 0; i < nframes; ++i)
            {
                double s = m_buf_view.getSample(0, m_bufplaypos);
                for (int j = 0; j < outchans; ++j)
                {
                    process->audio_outputs[0].data32[j][i] = s;
                }
                ++m_bufplaypos;
                if (m_bufplaypos == m_buf_view.getNumFrames())
                {
                    m_bufplaypos = 0;
                }
            }
        }
        return CLAP_PROCESS_CONTINUE;
    }

  private:
    BufViewType m_buf_view;
    double m_inputsr = 0.0;
    double m_outsr = 0.0;
    int m_bufplaypos = 0;
};
