#pragma once

#include "xapwithjucegui.h"
#include "dejavurandom.h"
#include "xap_generic_editor.h"
#include "xapfactory.h"

class ClapEventSequencerProcessor : public XAPWithJuceGUI
{
    static constexpr size_t numNoteGeneratorParams = 22;
    double m_sr = 1.0;
    DejaVuRandom m_dvpitchrand;
    DejaVuRandom m_dvtimerand;
    DejaVuRandom m_dvchordrand;
    DejaVuRandom m_dvvelorand;
    DejaVuRandom m_dvexpr1rand;
    DejaVuRandom m_dvexpr2rand;
    static constexpr size_t numDejaVuGenerators = 6;
    DejaVuRandom *m_dv_array[numDejaVuGenerators];
    struct SimpleNoteEvent
    {
        SimpleNoteEvent() {}
        SimpleNoteEvent(double ontime_, double offtime_, double key_, int port_, int chan_,
                        double velo_)
            : ontime(ontime_), offtime(offtime_), key(key_), port(port_), channel(chan_),
              velo(velo_)
        {
        }
        bool active = false;
        double ontime = -1.0;
        double offtime = -1.0;
        int port = -1;
        int channel = -1;
        double key = -1;
        int noteid = -1;
        double velo = 0.0;
    };
    std::vector<SimpleNoteEvent> m_active_notes;
    double m_phase = 0.0; // clock phase from 0 to 1
    // when clock phase jumps back, generate new note/chord
    bool m_phase_was_reset = true;
    // ever increasing for now, used for timing the outputted notes
    uint64_t m_sample_pos = 0;

  public:
    enum class OutputMode
    {
        Channels,
        Ports
    };
    OutputMode m_output_mode = OutputMode::Channels;
    enum class ParamIDs
    {
        ClockRate = 2000,
        NoteDurationMultiplier = 3000,
        ChordPolyphony = 3500,
        ArpeggioDivision = 4000,
        ArpeggioScatter = 4500,
        OutPortBias = 5000,
        // SharedDejaVu = 6000,
        DejavuTime = 6010,
        DejavuTimeEnabled = 6011,
        DejavuPitch = 6020,
        DejavuPitchEnabled = 6021,
        DejavuChord = 6030,
        DejavuChordEnabled = 6031,
        DejavuVelocity = 6040,
        DejavuVelocityEnabled = 6041,
        DejavuExpression1 = 6050,
        DejavuExpression1Enabled = 6051,
        DejavuExpression2 = 6060,
        DejavuExpression2Enabled = 6061,
        SharedLoopLen = 7000,
        OutputMode = 8000,
        PitchCenter = 9000,
        PitchSpread = 10000
    };

    using ParamDesc = xenakios::ParamDesc;
    bool getDescriptor(clap_plugin_descriptor *desc) const override
    {
        memset(desc, 0, sizeof(clap_plugin_descriptor));
        desc->name = "XAP Entropic Sequencer";
        desc->vendor = "Xenakios";
        return true;
    }
    uint32_t notePortsCount(bool isInput) const noexcept override
    {
        if (!isInput)
            return 1;
        return 0;
    }
    bool notePortsInfo(uint32_t index, bool isInput,
                       clap_note_port_info *info) const noexcept override
    {
        if (isInput)
            return false;
        info->id = 8008;
        info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
        info->supported_dialects = CLAP_NOTE_DIALECT_CLAP;
        strcpy_s(info->name, "Entropic Sequencer Output");
        return true;
    }
    ClapEventSequencerProcessor(int seed)
        : m_dvpitchrand(seed), m_dvtimerand(seed + 9003), m_dvchordrand(seed + 13),
          m_dvvelorand(seed + 101), m_dvexpr1rand(seed + 247), m_dvexpr2rand(seed + 31)
    {
        patch.params.resize(numNoteGeneratorParams);

        m_dv_array[0] = &m_dvtimerand;
        m_dv_array[1] = &m_dvpitchrand;
        m_dv_array[2] = &m_dvchordrand;
        m_dv_array[3] = &m_dvvelorand;
        m_dv_array[4] = &m_dvexpr1rand;
        m_dv_array[5] = &m_dvexpr2rand;

        m_active_notes.reserve(256);

        paramDescriptions.push_back(
            ParamDesc()
                .asFloat()
                .withRange(-3.0f, 3.0f)
                .withDefault(0.0)
                .withATwoToTheBFormatting(1, 1, "Hz")
                .withFlags(CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE)
                .withName("Clock rate")
                .withID((clap_id)ParamIDs::ClockRate));
        paramDescriptions.push_back(
            ParamDesc()
                .asFloat()
                .withRange(0.1f, 4.0f)
                .withDefault(1.0)
                .withLinearScaleFormatting("%", 100.0)
                .withFlags(CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE)
                .withName("Note duration")
                .withID((clap_id)ParamIDs::NoteDurationMultiplier));
        paramDescriptions.push_back(
            ParamDesc()
                .asInt()
                .withRange(1.0f, 4.0f)
                .withDefault(1.0)
                .withLinearScaleFormatting("", 1.0)
                .withFlags(CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED)
                .withName("Chord polyphony")
                .withID((clap_id)ParamIDs::ChordPolyphony));
        paramDescriptions.push_back(
            ParamDesc()
                .asInt()
                .withRange(1.0f, 8.0f)
                .withDefault(1.0)
                .withLinearScaleFormatting("/", 1.0)
                .withFlags(CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED)
                .withName("Arpeggio pulse division")
                .withID((clap_id)ParamIDs::ArpeggioDivision));
        paramDescriptions.push_back(
            ParamDesc()
                .asFloat()
                .withRange(0.0f, 1.0f)
                .withDefault(0.0)
                .withLinearScaleFormatting("%", 100.0)
                .withFlags(CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE)
                .withName("Arpeggio time scatter")
                .withID((clap_id)ParamIDs::ArpeggioScatter));
        paramDescriptions.push_back(
            ParamDesc()
                .asFloat()
                .withRange(0.0f, 1.0f)
                .withDefault(0.5)
                .withLinearScaleFormatting("", 1.0)
                .withFlags(CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE)
                .withName("Output select bias")
                .withID((clap_id)ParamIDs::OutPortBias));
        paramDescriptions.push_back(
            ParamDesc()
                .asFloat()
                .withRange(24.0f, 84.0f)
                .withDefault(60.0)
                .withLinearScaleFormatting("semitones/keys", 1.0)
                .withFlags(CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE)
                .withName("Pitch center")
                .withID((clap_id)ParamIDs::PitchCenter));
        paramDescriptions.push_back(
            ParamDesc()
                .asFloat()
                .withRange(0.0f, 48.0f)
                .withDefault(7.0)
                .withLinearScaleFormatting("semitones/keys", 1.0)
                .withFlags(CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE)
                .withName("Pitch spread")
                .withID((clap_id)ParamIDs::PitchSpread));
        std::unordered_map<int, std::string> outChoices;
        outChoices[0] = "Clap Note Channel 0 / 1";
        outChoices[1] = "Clap Port 0 / 1";
        paramDescriptions.push_back(ParamDesc()
                                        .asInt()
                                        .withUnorderedMapFormatting(outChoices)
                                        .withFlags(CLAP_PARAM_IS_STEPPED)
                                        .withDefault(0)
                                        .withID((clap_id)ParamIDs::OutputMode)
                                        .withName("Output Destination Mode"));
        const char *dvgennames[numDejaVuGenerators] = {"Time",     "Pitch",        "Chord type",
                                                       "Velocity", "Expression 1", "Expression 2"};
        for (int i = 0; i < numDejaVuGenerators; ++i)
        {
            clap_id cid = ((clap_id)ParamIDs::DejavuTime) + i * 10;
            paramDescriptions.push_back(
                ParamDesc()
                    .asFloat()
                    .withRange(0.0f, 1.0f)
                    .withDefault(0.0)
                    .withLinearScaleFormatting("", 1.0)
                    .withFlags(CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE)
                    .withName("Deja Vu " + std::string(dvgennames[i]))
                    .withID(cid));
            cid = ((clap_id)ParamIDs::DejavuTime) + i * 10 + 1;
            paramDescriptions.push_back(
                ParamDesc()
                    .asBool()
                    .withDefault(0.0)
                    .withFlags(CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED)
                    .withName("Deja Vu " + std::string(dvgennames[i]) + " enabled")
                    .withID(cid));
        }

        paramDescriptions.push_back(
            ParamDesc()
                .asInt()
                .withRange(1.0f, 16.0f)
                .withDefault(1.0)
                .withLinearScaleFormatting("", 1.0)
                .withFlags(CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED)
                .withName("Num Loop Steps")
                .withID((clap_id)ParamIDs::SharedLoopLen));
        jassert(patch.params.size() == paramDescriptions.size());
        for (size_t i = 0; i < paramDescriptions.size(); ++i)
        {
            auto &p = paramDescriptions[i];
            patch.params[i] = p.defaultVal;
            paramToPatchIndex[p.id] = i;
            paramToValue[p.id] = &patch.params[i];
        }
        updateDejaVuGeneratorInstances();
    }
    bool activate(double sampleRate, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override
    {
        m_sr = sampleRate;
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
    bool m_processing_started = false;

    void updateDejaVuGeneratorInstances()
    {
        float shared_loop_len = patch.params[paramToPatchIndex[(clap_id)ParamIDs::SharedLoopLen]];
        for (int i = 0; i < numDejaVuGenerators; ++i)
        {
            m_dv_array[i]->setLoopLength(shared_loop_len);
            clap_id cid = (clap_id)ParamIDs::DejavuTime + i * 10;
            float v = patch.params[paramToPatchIndex[cid]];
            m_dv_array[i]->setDejaVu(v);
            cid = (clap_id)ParamIDs::DejavuTime + i * 10 + 1;
            v = patch.params[paramToPatchIndex[cid]];
            m_dv_array[i]->setDejaVuEnabled(v > 0.5f);
        }
    }
    clap_process_status process(const clap_process *process) noexcept override;

    void generateChordNotes(int numnotes, int arpdiv, double baseonset, double basepitch,
                            double notedur, double hz, int port, double velo);

    std::default_random_engine m_def_rng;
    bool guiCreate(const char *api, bool isFloating) noexcept override
    {
        m_editor = std::make_unique<xenakios::GenericEditor>(*this);
        m_editor->setSize(500, paramsCount() * 40);
        m_editor_attached = true;
        pushAllParamsToGUI();
        return true;
    }
    void guiDestroy() noexcept override { m_editor = nullptr; }
};
inline xenakios::RegisterXap reg_notegen{
    "Entropic Sequencer", "Internal",
    []() { return std::make_unique<ClapEventSequencerProcessor>(400); }};
