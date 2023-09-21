#pragma once

#include "xapwithjucegui.h"
#include "signalsmith-stretch.h"

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
    double m_rate = 0.0;     // time octaves!
    double m_rate_mod = 0.0; // as above
    double m_pitch = 0.0; // semitones
    double m_pitch_mod = 0.0; // semitones
    double m_loop_start = 0.0; // proportion of whole file
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
    static constexpr double minRate = -3.0;
    static constexpr double maxRate = 2.0;
    FilePlayerProcessor()
    {
        m_param_infos.push_back(
            makeParamInfo((clap_id)ParamIds::Volume, "Volume", -36.0, 0.0, -6.0,
                          CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE));
        m_param_infos.push_back(
            makeParamInfo((clap_id)ParamIds::Playrate, "Playrate", minRate, maxRate, 0.0,
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
    uint32_t audioPortsCount(bool isInput) const noexcept override
    {
        if (isInput)
            return 0;
        return 1;
    }
    bool audioPortsInfo(uint32_t index, bool isInput,
                        clap_audio_port_info *info) const noexcept override
    {
        if (isInput)
            return false;
        info->channel_count = 2;
        info->flags = 0;
        info->id = 4400;
        info->in_place_pair = false;
        info->port_type = "";
        strcpy_s(info->name, "File player output");
        return true;
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
        if (ev->type == CLAP_EVENT_PARAM_MOD)
        {
            auto aev = reinterpret_cast<const clap_event_param_mod *>(ev);
            if (aev->param_id == to_clap_id(ParamIds::Volume))
                m_volume_mod = aev->amount;
            if (aev->param_id == to_clap_id(ParamIds::Playrate))
                m_rate_mod = aev->amount;
            if (aev->param_id == to_clap_id(ParamIds::Pitch))
                m_pitch_mod = aev->amount;
            // if (aev->param_id == to_clap_id(ParamIds::LoopStart))
            //     m_loop_start = aev->value;
            // if (aev->param_id == to_clap_id(ParamIds::LoopEnd))
            //     m_loop_end = aev->value;
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
        mergeParameterEvents(process);
        auto inevts = m_merge_list.clapInputEvents();
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
        bool preserve_pitch = m_preserve_pitch;
        // time octaves
        double rate = m_rate;
        rate += m_rate_mod;
        // we could allow modulation to make it go a bit over these limits...
        rate = std::clamp(rate, -3.0, 2.0);
        // then convert to actual playback ratio
        rate = std::pow(2.0, rate);
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
