#include "xap_notegenerator.h"

clap_process_status ClapEventSequencerProcessor::process(const clap_process *process) noexcept
{
    if (!m_processing_started)
    {
        m_processing_started = true;
    }
    handleGUIEvents();
    auto inevents = process->in_events;
    const clap_event_header *next_event = nullptr;
    auto esz = inevents->size(inevents);
    uint32_t nextEventIndex{0};
    if (esz > 0)
        next_event = inevents->get(inevents, nextEventIndex);
    if (process->transport)
    {
        // note here the oddity that the clap transport doesn't contain floating point seconds!
        auto curtime = process->transport->song_pos_seconds / (double)CLAP_SECTIME_FACTOR;
        // cache the pointer here because we use the value per-sample!
        auto clockrate = paramToValue[(clap_id)ParamIDs::ClockRate];
        for (int i = 0; i < process->frames_count; ++i)
        {
            while (next_event && next_event->time == i)
            {
                handleInboundEvent(next_event, false);
                nextEventIndex++;
                if (nextEventIndex >= esz)
                    next_event = nullptr;
                else
                    next_event = inevents->get(inevents, nextEventIndex);
            }
            double m_clock_hz = std::pow(2.0, *clockrate);
            // this is stupid, need to figure out a better solution for this
            for (int j = m_active_notes.size() - 1; j >= 0; --j)
            {
                auto &actnote = m_active_notes[j];
                if (m_sample_pos >= actnote.ontime && !actnote.active)
                {
                    actnote.active = true;
                    clap_event_note cen;
                    cen.header.flags = 0;
                    cen.header.size = sizeof(clap_event_note);
                    cen.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                    cen.header.time = i;
                    cen.header.type = CLAP_EVENT_NOTE_ON;
                    cen.key = (int)actnote.key;
                    cen.note_id = -1;
                    cen.channel = actnote.channel;
                    cen.port_index = actnote.port;
                    cen.velocity = actnote.velo;
                    process->out_events->try_push(process->out_events,
                                                  (const clap_event_header *)&cen);
                    double pitchexp = actnote.key - (int)actnote.key;
                    clap_event_note_expression cexp;
                    cexp.header.size = sizeof(clap_event_note_expression);
                    cexp.header.flags = 0;
                    cexp.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                    cexp.header.time = i;
                    cexp.header.type = CLAP_EVENT_NOTE_EXPRESSION;
                    cexp.channel = actnote.channel;
                    cexp.note_id = -1;
                    cexp.port_index = actnote.port;
                    cexp.key = (int)actnote.key;
                    cexp.expression_id = CLAP_NOTE_EXPRESSION_TUNING;
                    cexp.value = pitchexp;
                    process->out_events->try_push(process->out_events,
                                                  (const clap_event_header *)&cexp);
                }
                if (m_sample_pos >= actnote.offtime)
                {
                    // std::cout << "note off " << m_active_notes[j].key << " at "
                    //           << m_phase / m_sr << " seconds\n";
                    clap_event_note cen;
                    cen.header.flags = 0;
                    cen.header.size = sizeof(clap_event_note);
                    cen.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                    cen.header.time = i;
                    cen.header.type = CLAP_EVENT_NOTE_OFF;
                    cen.key = (int)actnote.key;
                    cen.note_id = -1;
                    cen.channel = actnote.channel;
                    cen.port_index = actnote.port;
                    cen.velocity = 0.0;
                    process->out_events->try_push(process->out_events,
                                                  (const clap_event_header *)&cen);
                    m_active_notes.erase(m_active_notes.begin() + j);
                }
            }
            if (m_phase_was_reset)
            {
                m_phase_was_reset = false;
                // we can do these look ups probably just fine here because this runs
                // only when notes are generated
                updateDejaVuGeneratorInstances(); // also does the lookups
                double m_pitch_center =
                    patch.params[paramToPatchIndex[(clap_id)ParamIDs::PitchCenter]];
                double m_pitch_spread =
                    patch.params[paramToPatchIndex[(clap_id)ParamIDs::PitchSpread]];
                double minpitch = m_pitch_center - m_pitch_spread;
                double maxpitch = m_pitch_center + m_pitch_spread;
                auto basenote = std::round(m_dvpitchrand.nextFloatInRange(minpitch, maxpitch));
                basenote = std::clamp(basenote, 24.0f, 84.0f);
                double m_note_dur_mult =
                    patch.params[paramToPatchIndex[(clap_id)ParamIDs::NoteDurationMultiplier]];
                double notedur = (1.0 / m_clock_hz) * m_note_dur_mult * m_sr;
                double velo = m_dvvelorand.nextFloat();
                if (velo < 0.5)
                    velo = 0.7;
                else
                    velo = 1.0;
                double z = m_dvtimerand.nextFloat();
                int port = 0;
                double m_outport_bias =
                    patch.params[paramToPatchIndex[(clap_id)ParamIDs::OutPortBias]];
                if (z > m_outport_bias)
                    port = 1;
                int arpdiv = patch.params[paramToPatchIndex[(clap_id)ParamIDs::ArpeggioDivision]];
                int numchordnotes =
                    patch.params[paramToPatchIndex[(clap_id)ParamIDs::ChordPolyphony]];
                generateChordNotes(numchordnotes, arpdiv, m_sample_pos, basenote, notedur,
                                   m_clock_hz, port, velo);
            }
            m_phase += (1.0 / m_sr) * m_clock_hz;
            if (m_phase >= 1.0)
            {
                m_phase -= 1.0;
                m_phase_was_reset = true;
            }
            ++m_sample_pos;
        }
    }
    return CLAP_PROCESS_CONTINUE;
}

void ClapEventSequencerProcessor::generateChordNotes(int numchordnotes, int arpdiv,
                                                     double baseonset, double basepitch,
                                                     double notedur, double hz, int port,
                                                     double velo)
{
    /*
    const float chord_notes[5][3] = {{0.0f, 3.1564f, 7.02f},
                                     {0.0f, 3.8631f, 7.02f},
                                     {-12.0f, 7.02f, 10.8827f},
                                     {0.0f, 4.9804f, 10.1760f},
                                     {-12.0f, 0.0f, 12.0f}};
    */
    const float chord_notes[8][4] = {
        {0.0f, 2.0f, 4.0f, 7.0f}, {0.0f, 2.0f, 4.0f, 6.0f},  {-2.0, 0.0f, 2.0f, 5.0f},
        {0.0, 3.0f, 4.0f, 7.0f},  {-7.0f, 1.0f, 4.0f, 7.0f}, {0.0, 3.0f, 6.0f, 9.0f},
        {-3.0, 0.0f, 2.0f, 8.0f}, {-3.0, 0.0f, 4.0f, 8.0f},
    };
    int ctype = m_dvchordrand.nextIntInRange(0, 7);
    double m_arp_scat_percent = patch.params[paramToPatchIndex[(clap_id)ParamIDs::ArpeggioScatter]];
    int port_to_use = 0;
    int chan_to_use = 0;
    if (m_output_mode == OutputMode::Channels)
        chan_to_use = port;
    if (m_output_mode == OutputMode::Ports)
        port_to_use = port;

    std::uniform_real_distribution<double> arpdirdist{0.0, 1.0};
    bool up = true; // arpdirdist(m_def_rng) < 0.5;

    int events_to_generate = numchordnotes;
    if (arpdiv > numchordnotes)
    {
        events_to_generate = arpdiv;
    }
    double time_interval = (1.0 / hz) / arpdiv;
    std::uniform_real_distribution<double> stagdist{0.0, m_arp_scat_percent * 0.99 * time_interval};

    for (int i = 0; i < events_to_generate; ++i)
    {
        int pulsepos = 0;
        int noteindex = 0;
        if (arpdiv > numchordnotes)
        {
            pulsepos = i;
            noteindex = i % numchordnotes;
        }
        else
        {
            pulsepos = std::floor((double)arpdiv / numchordnotes * i);
            noteindex = i;
        }
        double pitch = basepitch + chord_notes[ctype][noteindex];
        double stag = stagdist(m_def_rng);
        double tpos = baseonset + ((pulsepos * time_interval + stag) * m_sr);
        SimpleNoteEvent ne{tpos, tpos + notedur, pitch, port_to_use, chan_to_use, velo};
        m_active_notes.push_back(ne);
    }
}
