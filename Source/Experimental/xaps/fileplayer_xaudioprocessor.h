#pragma once

#include "../xapwithjucegui.h"
#include "signalsmith-stretch.h"
#include "../xap_generic_editor.h"
#include "../xap_utils.h"
#include "../xapfactory.h"

class FilePlayerProcessor : public XAPWithJuceGUI, public juce::Thread
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
    double m_temp_file_sample_rate = 1.0;
    struct FilePlayerMessage
    {
        enum class Opcode
        {
            Nop,
            ParamBegin,
            ParamChange,
            ParamEnd,
            RequestFileChange,
            FileChanged,
            StopIOThread
        };
        Opcode opcode = Opcode::Nop;
        clap_id parid = CLAP_INVALID_ID;
        double value = 0.0;
        std::string_view filename;
    };
    choc::fifo::SingleReaderSingleWriterFIFO<FilePlayerMessage> messages_to_ui;
    choc::fifo::SingleReaderSingleWriterFIFO<FilePlayerMessage> messages_from_ui;
    juce::dsp::Gain<float> m_gain_proc;
    double m_volume = 0.0f;
    double m_volume_mod = 0.0f;
    double m_rate = 0.0;       // time octaves!
    double m_rate_mod = 0.0;   // as above
    double m_pitch = 0.0;      // semitones
    double m_pitch_mod = 0.0;  // semitones
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
    juce::AudioFormatManager fman;
    void run() override
    {
        bool stop = false;
        while (!stop)
        {
            if (threadShouldExit())
                break;
            FilePlayerMessage msg;
            while (messages_to_io.pop(msg))
            {
                if (msg.opcode == FilePlayerMessage::Opcode::StopIOThread)
                {
                    stop = true;
                    break;
                }
                if (msg.opcode == FilePlayerMessage::Opcode::RequestFileChange)
                {
                    juce::String tempstring(msg.filename.data());
                    juce::File afile(tempstring);
                    auto reader = fman.createReaderFor(afile);
                    if (reader)
                    {
                        m_temp_file_sample_rate = reader->sampleRate;
                        m_file_temp_buf.setSize(2, reader->lengthInSamples);
                        reader->read(&m_file_temp_buf, 0, reader->lengthInSamples, 0, true, true);
                        FilePlayerMessage readymsg;
                        readymsg.opcode = FilePlayerMessage::Opcode::FileChanged;
                        readymsg.filename = msg.filename;
                        messages_from_io.push(readymsg);
                        delete reader;
                    }
                }
            }
            juce::Thread::sleep(50);
        }
    }
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
    using ParamDesc = xenakios::ParamDesc;
    ~FilePlayerProcessor() override
    {
        stopThread(5000);
    }
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
                .withFlags(CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE)
                .withName("Playrate")
                .withID((clap_id)ParamIds::Playrate));
        paramDescriptions.push_back(
            ParamDesc()
                .asFloat()
                .extendable()
                .withRange(-12.0f, 12.0f)
                .withDefault(0.0)
                .withExtendFactors(4.0f, 4.0f)
                .withLinearScaleFormatting("semitones")
                .withFlags(CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE)
                .withName("Pitch")
                .withID((clap_id)ParamIds::Pitch));
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
                .withLinearScaleFormatting("%")
                .withFlags(CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE)
                .withName("Loop start")
                .withID((clap_id)ParamIds::LoopStart));
        paramDescriptions.push_back(
            ParamDesc()
                .asFloat()
                .withRange(0.0f, 1.0f)
                .withDefault(1.0)
                .withLinearScaleFormatting("%")
                .withFlags(CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE)
                .withName("Loop end")
                .withID((clap_id)ParamIds::LoopEnd));
        messages_to_ui.reset(256);
        messages_from_ui.reset(256);
        messages_to_io.reset(8);
        messages_from_io.reset(8);
        // importFile(juce::File(R"(C:\MusicAudio\sourcesamples\there was a time .wav)"));

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
        m_work_buf.clear();
        for (auto &rs : m_resamplers)
            rs.reset();
        m_resampler_work_buf.resize(maxFrameCount * 32);

        return true;
    }
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
    std::string currentfilename;

    void importFile(juce::File f)
    {
        juce::AudioFormatManager man;
        man.registerBasicFormats();
        auto reader = man.createReaderFor(f);
        jassert(reader);
        if (reader)
        {
            juce::AudioBuffer<float> tempbuf(2, reader->lengthInSamples);
            tempbuf.clear();
            reader->read(&tempbuf, 0, reader->lengthInSamples, 0, true, true);

            // m_file_buf.setSize(2, reader->lengthInSamples);
            m_file_sample_rate = reader->sampleRate;
            // m_stretch.reset();
            m_work_buf.clear();
            std::swap(tempbuf, m_file_buf);
            // m_file_buf.clear();

            // reader->read(&m_file_buf, 0, reader->lengthInSamples, 0, true, true);

            delete reader;
            currentfilename = f.getFileName().toStdString();
            FilePlayerMessage msg;
            msg.opcode = FilePlayerMessage::Opcode::FileChanged;
            msg.filename = currentfilename;
            messages_to_ui.push(msg);
        }
    }
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
    choc::fifo::SingleReaderSingleWriterFIFO<FilePlayerMessage> messages_to_io;
    choc::fifo::SingleReaderSingleWriterFIFO<FilePlayerMessage> messages_from_io;
    void handleMessagesFromIO()
    {
        FilePlayerMessage msg;
        while (messages_from_io.pop(msg))
        {
            if (msg.opcode == FilePlayerMessage::Opcode::FileChanged)
            {
                m_file_sample_rate = m_temp_file_sample_rate;
                // double t0 = juce::Time::getMillisecondCounterHiRes();
                std::swap(m_file_temp_buf, m_file_buf);
                // double t1 = juce::Time::getMillisecondCounterHiRes();
                // DBG("swap took " << t1-t0 << " millisecons");
                // the swap seems to be very fast
                FilePlayerMessage outmsg;
                outmsg.opcode = FilePlayerMessage::Opcode::FileChanged;
                outmsg.filename = msg.filename;
                messages_to_ui.push(outmsg);
            }
        }
    }
    void handleMessagesFromUI()
    {
        FilePlayerMessage msg;
        while (messages_from_ui.pop(msg))
        {
            if (msg.opcode == FilePlayerMessage::Opcode::ParamChange)
            {
                auto pev = xenakios::make_event_param_value(0, msg.parid, msg.value, nullptr);
                handleEvent((const clap_event_header *)&pev, true);
            }
            // ok, so this is tricky, we can't actually load and change the file
            // in the audio thread, so offload to another thread
            if (msg.opcode == FilePlayerMessage::Opcode::RequestFileChange)
            {
                messages_to_io.push(msg);
            }
                }
    }
    clap_process_status process(const clap_process *process) noexcept override;
};
static xenakios::RegisterXap reg_fileplayer{"File Player", "com.xenakios.fileplayer", []() {
                                                return std::make_unique<FilePlayerProcessor>();
                                            }};
