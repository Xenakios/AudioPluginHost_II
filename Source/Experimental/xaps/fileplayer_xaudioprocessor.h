#pragma once

#include "../xapwithjucegui.h"
#include "signalsmith-stretch.h"
#include "../xap_generic_editor.h"
#include "../xap_utils.h"
#include "../xapfactory.h"
#include "containers/choc_SingleReaderMultipleWriterFIFO.h"
#include "sst/basic-blocks/dsp/LanczosResampler.h"

inline float getXFadedSampleFromBuffer(const float *srcbuf, int index, int start, int end,
                                       int xfadelen)
{
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

class ResamplerEngine
{
  public:
    ResamplerEngine() {}
    void prepare(double samplerate, int maxblocksize)
    {
        m_sr = samplerate;
        m_rs_out_buf.resize(2048);
        std::fill(m_rs_out_buf.begin(), m_rs_out_buf.end(), 0.0f);
    }
    void processBlock(float **outbuffer, int numFrames, int numOutchans)
    {
        jassert(m_file_buf);
        jassert(m_loop_end > 4000);
        m_lanczos.sri = m_source_sr;
        m_lanczos.sro = m_sr / m_play_rate;
        m_lanczos.dPhaseO = m_lanczos.sri / m_lanczos.sro;
        auto samplestopush = m_lanczos.inputsRequiredToGenerateOutputs(numFrames);
        auto filebuf = m_file_buf->getArrayOfReadPointers();
        for (size_t i = 0; i < samplestopush; ++i)
        {
            float in0 = getXFadedSampleFromBuffer(filebuf[0], m_buf_playpos, m_loop_start,
                                        m_loop_end, m_xfadelen);
            float in1 = getXFadedSampleFromBuffer(filebuf[1], m_buf_playpos, m_loop_start,
                                        m_loop_end, m_xfadelen);
            // jassert(in0>=-2.0f && in0<=2.0);
            // jassert(in1>=-2.0f && in1<=2.0);
            m_lanczos.push(in0, in1);
            ++m_buf_playpos;
            if (m_buf_playpos >= m_loop_end)
                m_buf_playpos = m_loop_start;
        }
        float *rsoutleft = &m_rs_out_buf[0];
        float *rsoutright = &m_rs_out_buf[512];
        auto produced = m_lanczos.populateNext(rsoutleft, rsoutright, numFrames);
        jassert(produced == numFrames);
        for (int j = 0; j < numFrames; ++j)
        {
            float outl = rsoutleft[j];
            float outr = rsoutright[j];
            outbuffer[0][j] = outl;
            outbuffer[1][j] = outr;
        }
    }
    void setSourceBuffer(juce::AudioBuffer<float> *buf, double sr)
    {
        m_source_sr = sr;
        m_file_buf = buf;
    }
    int m_loop_start = 0;
    int m_loop_end = 0;
    int m_xfadelen = 4000;
    double m_play_rate = 1.0;
    int m_buf_playpos = 0;
  private:
    juce::AudioBuffer<float> *m_file_buf = nullptr;
    std::vector<float> m_rs_out_buf;
    sst::basic_blocks::dsp::LanczosResampler<128> m_lanczos{44100.0f, 44100.0f};
    double m_sr = 44100;
    double m_source_sr = 44100;
    
};

class FilePlayerProcessor : public XAPWithJuceGUI, public juce::Thread
{
  public:
    std::vector<float> m_rs_out_buf;
    sst::basic_blocks::dsp::LanczosResampler<128> m_lanczos{44100.0f, 44100.0f};
    double m_sr = 44100;
    std::atomic<bool> m_running_offline{false};
    juce::AudioBuffer<float> m_file_buf;
    juce::AudioBuffer<float> m_file_temp_buf;
    juce::AudioBuffer<float> m_work_buf;
    int m_buf_playpos = 0;
    signalsmith::stretch::SignalsmithStretch<float> m_stretch;

    double m_file_sample_rate = 1.0;
    double m_temp_file_sample_rate = 1.0;
    struct FilePlayerMessage
    {
        enum class Opcode
        {
            Nop,
            ParamBegin,
            ParamChange,
            ParamEnd,
            ParamFeatureStateChanged,
            RequestFileChange,
            FileChanged,
            OfflineProgress,
            FileLoadError,
            FilePlayPosition,
            Trigger,
            StopIOThread
        };
        Opcode opcode = Opcode::Nop;
        clap_id parid = CLAP_INVALID_ID;
        double value = 0.0;
        bool gate = false;
        std::string filename;
    };
    // Note that this isn't *strictly* realtime safe since it uses a spinlock inside,
    // but we are not expecting lots of contention
    choc::fifo::SingleReaderMultipleWriterFIFO<FilePlayerMessage> messages_to_ui;
    choc::fifo::SingleReaderSingleWriterFIFO<FilePlayerMessage> messages_from_ui;
    choc::fifo::SingleReaderSingleWriterFIFO<FilePlayerMessage> messages_to_io;
    choc::fifo::SingleReaderSingleWriterFIFO<FilePlayerMessage> messages_from_io;
    juce::dsp::Gain<float> m_gain_proc;
    double m_volume = 0.0f;
    double m_volume_mod = 0.0f;
    double m_rate = 0.0;      // time octaves!
    double m_rate_mod = 0.0;  // as above
    double m_pitch = 0.0;     // semitones
    double m_pitch_mod = 0.0; // semitones
    double m_tonality_limit = 1.0;
    double m_loop_start = 0.0; // proportion of whole file
    double m_loop_end = 1.0;
    bool m_triggered_mode = false;
    bool m_trigger_active = false;
    bool m_preserve_pitch = true;
    int m_filepos_throttle_counter = 0;
    int m_filepos_throttle_frames = 2048;
    enum class ParamIds
    {
        Volume = 42,
        Playrate = 666,
        Pitch = 91155,
        TonalityLimit = 207,
        PreservePitch = 6543,
        LoopStart = 3322,
        LoopEnd = 888777,
        TriggeredMode = 900
    };
    juce::AudioFormatManager fman;
    void run() override;

    bool getDescriptor(clap_plugin_descriptor *desc) const override
    {
        memset(desc, 0, sizeof(clap_plugin_descriptor));
        desc->clap_version = CLAP_VERSION;
        desc->id = "com.xenakios.fileplayer";
        desc->name = "File player";
        desc->vendor = "Xenakios";
        return true;
    }

    bool guiCreate(const char *api, bool isFloating) noexcept override;

    void guiDestroy() noexcept override { m_editor = nullptr; }
    static constexpr double minRate = -3.0;
    static constexpr double maxRate = 2.0;
    static constexpr double pitchExtendFactor = 4.0;
    using ParamDesc = xenakios::ParamDesc;
    ParamDesc::FeatureState m_fs_pitch;
    ~FilePlayerProcessor() override { stopThread(5000); }
    FilePlayerProcessor() : juce::Thread("filerplayeriothread")
    {
        fman.registerBasicFormats();
        paramDescriptions.push_back(
            ParamDesc()
                .asDecibel()
                .withRange(-36.0, 6.0)
                .withDefault(-6.0)
                .withFlags(CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE)
                .withName("Volume")
                .withID((clap_id)ParamIds::Volume));
        paramDescriptions.push_back(
            ParamDesc()
                .asFloat()
                .withRange(-3.0f, 2.0f)
                .withDefault(0.0)
                .withATwoToTheBFormatting(1, 1, "x")
                .withDecimalPlaces(3)
                .withFlags(CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE)
                .withName("Playrate")
                .withID((clap_id)ParamIds::Playrate));
        m_fs_pitch.isExtended = false;
        paramDescriptions.push_back(
            ParamDesc()
                .asFloat()
                .extendable()
                .withRange(-12.0f, 12.0f)
                .withDefault(0.0)
                .withExtendFactors(pitchExtendFactor)
                .withLinearScaleFormatting("semitones")
                .withFlags(CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE)
                .withName("Pitch")
                .withID((clap_id)ParamIds::Pitch));
        paramDescriptions.push_back(ParamDesc()
                                        .asFloat()
                                        .withRange(0.0f, 1.0f)
                                        .withDefault(1.0)
                                        .withLinearScaleFormatting("%", 100.0f)
                                        .withFlags(CLAP_PARAM_IS_AUTOMATABLE)
                                        .withName("Tonality limit")
                                        .withID((clap_id)ParamIds::TonalityLimit));
        paramDescriptions.push_back(
            ParamDesc()
                .asBool()
                .withDefault(1.0f)
                .withFlags(CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED)
                .withName("Preserve pitch")
                .withID((clap_id)ParamIds::PreservePitch));
        paramDescriptions.push_back(
            ParamDesc()
                .asFloat()
                .withRange(0.0f, 1.0f)
                .withDefault(0.0)
                .withLinearScaleFormatting("%", 100.0f)
                .withFlags(CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE)
                .withName("Loop start")
                .withID((clap_id)ParamIds::LoopStart));
        paramDescriptions.push_back(
            ParamDesc()
                .asFloat()
                .withRange(0.0f, 1.0f)
                .withDefault(1.0)
                .withLinearScaleFormatting("%", 100.0f)
                .withFlags(CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE)
                .withName("Loop end")
                .withID((clap_id)ParamIds::LoopEnd));
        paramDescriptions.push_back(
            ParamDesc()
                .asBool()
                .withDefault(0.0f)
                .withFlags(CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED)
                .withName("Triggered mode")
                .withID((clap_id)ParamIds::TriggeredMode));
        messages_to_ui.reset(256);
        messages_from_ui.reset(256);
        messages_to_io.reset(8);
        messages_from_io.reset(8);

        for (auto &pd : paramDescriptions)
        {
            FilePlayerMessage msg;
            msg.opcode = FilePlayerMessage::Opcode::ParamChange;
            msg.parid = pd.id;
            msg.value = pd.defaultVal;
            messages_to_ui.push(msg);
        }
        startThread(juce::Thread::Priority::normal);
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
                  uint32_t maxFrameCount) noexcept override;

    uint32_t paramsCount() const noexcept override { return paramDescriptions.size(); }
    bool paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept override
    {
        if (paramIndex >= paramDescriptions.size())
            return false;
        const auto &pd = paramDescriptions[paramIndex];
        pd.template toClapParamInfo<CLAP_NAME_SIZE>(info);
        return true;
    }
    bool paramsValue(clap_id paramId, double *value) noexcept override
    {
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
        else if (paramId == (clap_id)ParamIds::TriggeredMode)
            result = m_triggered_mode;
        else if (paramId == (clap_id)ParamIds::TonalityLimit)
            result = m_tonality_limit;
        if (result)
        {
            *value = *result;
            return true;
        }
        return false;
    }
    std::string currentfilename;

    void handleEvent(const clap_event_header *ev, bool is_from_ui)
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
            if (aev->param_id == to_clap_id(ParamIds::TonalityLimit))
                m_tonality_limit = aev->value;
            if (aev->param_id == to_clap_id(ParamIds::LoopStart))
                m_loop_start = aev->value;
            if (aev->param_id == to_clap_id(ParamIds::LoopEnd))
                m_loop_end = aev->value;
            if (aev->param_id == to_clap_id(ParamIds::PreservePitch))
                m_preserve_pitch = aev->value >= 0.5;
            if (aev->param_id == to_clap_id(ParamIds::TriggeredMode))
            {
                m_triggered_mode = aev->value >= 0.5;
                m_trigger_active = false;
            }
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
    }

    void handleMessagesFromIO();

    // must only be called from audio thread
    void handleMessagesFromUI();

    clap_process_status process(const clap_process *process) noexcept override;
};
static xenakios::RegisterXap reg_fileplayer{"File Player", "com.xenakios.fileplayer", []() {
                                                return std::make_unique<FilePlayerProcessor>();
                                            }};
