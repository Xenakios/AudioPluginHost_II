#pragma once

#include "xapwithjucegui.h"
#include "dejavurandom.h"
#include "xap_generic_editor.h"

class ClapEventSequencerProcessor : public XAPWithJuceGUI
{
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
    
    double m_clock_hz = 1.0; // cached for efficiency
    
    double m_clock_rate = 0.0; // time octave
    double m_note_dur_mult = 1.0;
    double m_arp_time_range = 0.1;
    double m_outport_bias = 0.5;
    double m_shared_deja_vu = 0.0;
    int m_shared_loop_len = 1;
    double m_pitch_center = 60.0f;
    double m_pitch_spread = 7.0f;
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
    ClapEventSequencerProcessor(int seed, double pulselen)
        : m_dvpitchrand(seed), m_dvtimerand(seed + 9003), m_dvchordrand(seed + 13),
          m_dvvelorand(seed + 101)
    {
        m_active_notes.reserve(4096);
        m_dvpitchrand.m_loop_len = m_shared_loop_len;
        m_dvpitchrand.m_deja_vu = m_shared_deja_vu;
        m_dvchordrand.m_loop_len = m_shared_loop_len;
        m_dvchordrand.m_deja_vu = m_shared_deja_vu;
        m_dvvelorand.m_loop_len = m_shared_loop_len;
        m_dvvelorand.m_deja_vu = m_shared_deja_vu;
        m_dvtimerand.m_loop_len = m_shared_loop_len;
        m_dvtimerand.m_deja_vu = m_shared_deja_vu;
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
                .withName("Arpeggio speed")
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
        outChoices[0] = "MIDI Channel 0 / 1";
        outChoices[1] = "Clap Port 0 / 1";
        paramDescriptions.push_back(ParamDesc()
                                        .asInt()
                                        .withUnorderedMapFormatting(outChoices)
                                        .withFlags(CLAP_PARAM_IS_STEPPED)
                                        .withDefault(1)
                                        .withID((clap_id)ParamIDs::OutputMode)
                                        .withName("Output Mode"));
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

    void handleInboundEvent(const clap_event_header *ev)
    {
        if (ev->space_id != CLAP_CORE_EVENT_SPACE_ID)
            return;
        if (ev->type == CLAP_EVENT_PARAM_VALUE)
        {
            auto pev = (const clap_event_param_value *)ev;
            if (pev->param_id == (clap_id)ParamIDs::ClockRate)
            {
                m_clock_rate = pev->value;
                m_clock_hz = std::pow(2.0, m_clock_rate);
            }

            if (pev->param_id == (clap_id)ParamIDs::NoteDurationMultiplier)
                m_note_dur_mult = pev->value;
            if (pev->param_id == (clap_id)ParamIDs::ArpeggioSpeed)
                m_arp_time_range = pev->value;
            if (pev->param_id == (clap_id)ParamIDs::OutPortBias)
                m_outport_bias = pev->value;
            if (pev->param_id == (clap_id)ParamIDs::SharedDejaVu)
            {
                m_shared_deja_vu = pev->value;
                m_dvtimerand.m_deja_vu = m_shared_deja_vu;
                m_dvpitchrand.m_deja_vu = m_shared_deja_vu;
                m_dvvelorand.m_deja_vu = m_shared_deja_vu;
                m_dvchordrand.m_deja_vu = m_shared_deja_vu;
            }
            if (pev->param_id == (clap_id)ParamIDs::SharedLoopLen)
            {
                m_shared_loop_len = (int)pev->value;
                m_dvtimerand.m_loop_len = m_shared_loop_len;
                m_dvpitchrand.m_loop_len = m_shared_loop_len;
                m_dvvelorand.m_loop_len = m_shared_loop_len;
                m_dvchordrand.m_loop_len = m_shared_loop_len;
            }
            if (pev->param_id == (clap_id)ParamIDs::OutputMode)
            {
                m_output_mode = (OutputMode)pev->value;
            }
            if (pev->param_id == (clap_id)ParamIDs::PitchCenter)
            {
                m_pitch_center = pev->value;
            }
            if (pev->param_id == (clap_id)ParamIDs::PitchSpread)
            {
                m_pitch_spread = pev->value;
            }
        }
    }

    clap_process_status process(const clap_process *process) noexcept override;

    void generateChordNotes(int numnotes, double baseonset, double basepitch, double notedur,
                            double hz, int port, double velo);

    std::default_random_engine m_def_rng;
    bool guiCreate(const char *api, bool isFloating) noexcept override
    {
        m_editor = std::make_unique<xenakios::GenericEditor>(*this);
        m_editor->setSize(500, paramsCount() * 40);
        return true;
    }
    void guiDestroy() noexcept override { m_editor = nullptr; }
};
