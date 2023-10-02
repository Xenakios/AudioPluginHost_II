#pragma once

#include "xapwithjucegui.h"
#include "dejavurandom.h"
#include "xap_generic_editor.h"

class ClapEventSequencerProcessor : public XAPWithJuceGUI
{
    static constexpr size_t numNoteGeneratorParams = 9;
    double m_sr = 1.0;
    DejaVuRandom m_dvpitchrand;
    DejaVuRandom m_dvtimerand;
    DejaVuRandom m_dvchordrand;
    DejaVuRandom m_dvvelorand;
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

    struct Patch
    {
        float params[numNoteGeneratorParams];
        // typename TConfig::PatchExtension extension;
    } patch;

    std::unordered_map<clap_id, float *> paramToValue;
    std::unordered_map<clap_id, int> paramToPatchIndex;

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
        ArpeggioSpeed = 4000,
        OutPortBias = 5000,
        SharedDejaVu = 6000,
        SharedLoopLen = 7000,
        OutputMode = 8000,
        PitchCenter = 9000,
        PitchSpread = 10000
    };

    using ParamDesc = xenakios::ParamDesc;
    bool getDescriptor(clap_plugin_descriptor *desc) const override
    {
        memset(desc,0,sizeof(clap_plugin_descriptor));
        desc->name = "XAP Entropic Sequencer";
        desc->vendor = "Xenakios";
        return true;
    }
    ClapEventSequencerProcessor(int seed)
        : m_dvpitchrand(seed), m_dvtimerand(seed + 9003), m_dvchordrand(seed + 13),
          m_dvvelorand(seed + 101)
    {
        m_active_notes.reserve(4096);

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
                .asFloat()
                .withRange(0.0f, 1.0f)
                .withDefault(0.0)
                .withLinearScaleFormatting("%", 100.0)
                .withFlags(CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE)
                .withName("Arpeggio time scatter")
                .withID((clap_id)ParamIDs::ArpeggioSpeed));
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
        paramDescriptions.push_back(
            ParamDesc()
                .asFloat()
                .withRange(0.0f, 1.0f)
                .withDefault(0.0)
                .withLinearScaleFormatting("", 1.0)
                .withFlags(CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE)
                .withName("Deja Vu")
                .withID((clap_id)ParamIDs::SharedDejaVu));
        paramDescriptions.push_back(
            ParamDesc()
                .asInt()
                .withRange(1.0f, 16.0f)
                .withDefault(1.0)
                .withLinearScaleFormatting("", 1.0)
                .withFlags(CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_STEPPED)
                .withName("Num Loop Steps")
                .withID((clap_id)ParamIDs::SharedLoopLen));
        jassert(numNoteGeneratorParams == paramDescriptions.size());
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
    void pushAllParamsToGUI()
    {
        if (!m_editor_attached)
            return;
        for (auto &p : paramToPatchIndex)
        {
            float val = patch.params[p.second];
            m_to_ui_fifo.push(xenakios::CrossThreadMessage{p.first, CLAP_EVENT_PARAM_VALUE, val});
        }
    }
    void handleInboundEvent(const clap_event_header *ev, bool is_from_ui) noexcept override
    {
        if (ev->space_id != CLAP_CORE_EVENT_SPACE_ID)
            return;
        if (ev->type == CLAP_EVENT_PARAM_VALUE)
        {
            auto pev = (const clap_event_param_value *)ev;
            auto it = paramToValue.find(pev->param_id);
            if (it != paramToValue.end())
            {
                *(it->second) = pev->value;
            }

            if (!is_from_ui)
            {
                m_to_ui_fifo.push(xenakios::CrossThreadMessage{pev->param_id,
                                                               CLAP_EVENT_PARAM_VALUE, pev->value});
            }
        }
    }
    void updateDejaVuGeneratorInstances()
    {
        float m_shared_deja_vu = patch.params[paramToPatchIndex[(clap_id)ParamIDs::SharedDejaVu]];
        m_dvtimerand.setDejaVu(m_shared_deja_vu);
        m_dvpitchrand.setDejaVu(m_shared_deja_vu);
        m_dvvelorand.setDejaVu(m_shared_deja_vu);
        m_dvchordrand.setDejaVu(m_shared_deja_vu);
        float m_shared_loop_len = patch.params[paramToPatchIndex[(clap_id)ParamIDs::SharedLoopLen]];
        m_dvtimerand.setLoopLength(m_shared_loop_len);
        m_dvpitchrand.setLoopLength(m_shared_loop_len);
        m_dvvelorand.setLoopLength(m_shared_loop_len);
        m_dvchordrand.setLoopLength(m_shared_loop_len);
    }
    clap_process_status process(const clap_process *process) noexcept override;

    void generateChordNotes(int numnotes, double baseonset, double basepitch, double notedur,
                            double hz, int port, double velo);

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
