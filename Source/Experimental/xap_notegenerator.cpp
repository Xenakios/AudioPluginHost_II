#include "xap_notegenerator.h"

clap_process_status ClapEventSequencerProcessor::process(const clap_process *process) noexcept
{
    if (!m_processing_started)
    {
        m_processing_started = true;
    }
    mergeParameterEvents(process);
    auto inevents = m_merge_list.clapInputEvents();
    const clap_event_header *next_event = nullptr;
    auto esz = inevents->size(inevents);
    uint32_t nextEventIndex{0};
    if (esz > 0)
        next_event = inevents->get(inevents, nextEventIndex);
    if (process->transport)
    {
        // note here the oddity that the clap transport doesn't contain floating point seconds!
        auto curtime = process->transport->song_pos_seconds / (double)CLAP_SECTIME_FACTOR;

        for (int i = 0; i < process->frames_count; ++i)
        {
            while (next_event && next_event->time == i)
            {
                handleInboundEvent(next_event);
                nextEventIndex++;
                if (nextEventIndex >= esz)
                    next_event = nullptr;
                else
                    next_event = inevents->get(inevents, nextEventIndex);
            }
            // this is stupid, need to figure out a better solution for this
            for (int j = m_active_notes.size() - 1; j >= 0; --j)
            {
                auto &actnote = m_active_notes[j];
                if (m_phase >= actnote.ontime && !actnote.active)
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
                    cen.channel = 0;
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
                    cexp.channel = -1;
                    cexp.note_id = -1;
                    cexp.port_index = actnote.port;
                    cexp.key = (int)actnote.key;
                    cexp.expression_id = CLAP_NOTE_EXPRESSION_TUNING;
                    cexp.value = pitchexp;
                    process->out_events->try_push(process->out_events,
                                                  (const clap_event_header *)&cexp);
                }
                if (m_phase >= actnote.offtime)
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
                    cen.channel = 0;
                    cen.port_index = actnote.port;
                    cen.velocity = 0.0;
                    process->out_events->try_push(process->out_events,
                                                  (const clap_event_header *)&cen);
                    m_active_notes.erase(m_active_notes.begin() + j);
                }
            }
            if (m_phase >= m_next_note_time)
            {
                auto basenote = m_dvpitchrand.nextFloatInRange(36.0f, 72.0f);
                double hz = std::pow(2.0, m_clock_rate);
                double notedur = (1.0 / hz) * m_note_dur_mult * m_sr;
                double velo = m_dvvelorand.nextFloat();
                if (velo < 0.5)
                    velo = 0.7;
                else
                    velo = 1.0;
                double z = m_dvtimerand.nextFloat();
                int port = 0;
                if (z > m_outport_bias)
                    port = 1;
                generateChordNotes(1, m_phase, basenote, notedur, hz, port, velo);
                m_next_note_time = m_phase + (1.0 / hz * m_sr);
            }
            m_phase += 1.0;
        }
    }
    return CLAP_PROCESS_CONTINUE;
}
