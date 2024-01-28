#pragma once

#include <vector>
#include <optional>
#include <iostream>
#include "audio/choc_AudioFileFormat_WAV.h"
#include "xapdsp.h"
#include "xap_utils.h"
#include "xaps/clap_xaudioprocessor.h"

class ClapEventSequence
{

  public:
    union clap_multi_event
    {
        clap_event_header_t header;
        clap_event_note note;
        clap_event_midi_t midi;
        clap_event_midi2_t midi2;
        clap_event_param_value_t param;
        clap_event_param_mod_t parammod;
        clap_event_param_gesture_t paramgest;
        clap_event_note_expression_t noteexpression;
        clap_event_transport_t transport;
    };
    struct Event
    {
        Event() {}
        template <typename EType>
        Event(double time, EType *e) : timestamp(time), event(*(clap_multi_event *)e)
        {
        }
        double timestamp = 0.0;
        clap_multi_event event;
        bool operator<(const Event &other) { return timestamp < other.timestamp; }
    };
    std::vector<Event> m_evlist;
    ClapEventSequence() {}
    void sortEvents() { choc::sorting::stable_sort(m_evlist.begin(), m_evlist.end()); }
    void addNoteOn(double time, int port, int channel, int key, double velo, int note_id)
    {
        auto ev =
            xenakios::make_event_note(0, CLAP_EVENT_NOTE_ON, port, channel, key, note_id, velo);
        m_evlist.push_back(Event(time, &ev));
    }
    void addNoteOff(double time, int port, int channel, int key, double velo, int note_id)
    {
        auto ev =
            xenakios::make_event_note(0, CLAP_EVENT_NOTE_OFF, port, channel, key, note_id, velo);
        m_evlist.push_back(Event(time, &ev));
    }
    // std::vector<SeqEvent> m_seq_events;
    // void addEvent(SeqEvent e) { m_seq_events.push_back(e); }
};

class ClapProcessingEngine
{
    std::unique_ptr<ClapPluginFormatProcessor> m_plug;

  public:
    ClapEventSequence m_seq;
    void setSequencer(ClapEventSequence seq)
    {
        m_seq = seq;
        // m_seq.m_evlist.sortEvents();
    }
    ClapProcessingEngine(std::string plugfilename, int plugindex)
    {
        ClapPluginFormatProcessor::mainthread_id() = std::this_thread::get_id();
        m_plug = std::make_unique<ClapPluginFormatProcessor>(plugfilename, plugindex);
        if (m_plug)
        {

            clap_plugin_descriptor desc;
            if (m_plug->getDescriptor(&desc))
            {
                std::cout << "created : " << desc.name << "\n";
            }
        }
    }
    void processToFile(std::string filename, double duration, double samplerate)
    {
        int procblocksize = 512;
        std::atomic<bool> renderloopfinished{false};
        m_plug->activate(samplerate, procblocksize, procblocksize);
        std::thread th([&] {
            clap_process cp;
            memset(&cp, 0, sizeof(clap_process));
            cp.frames_count = procblocksize;
            cp.audio_inputs_count = 1;
            choc::buffer::ChannelArrayBuffer<float> ibuf{2, (unsigned int)procblocksize};
            ibuf.clear();
            clap_audio_buffer inbufs[1];
            inbufs[0].channel_count = 2;
            inbufs[0].constant_mask = 0;
            inbufs[0].latency = 0;
            auto ichansdata = ibuf.getView().data.channels;
            inbufs[0].data32 = (float **)ichansdata;
            cp.audio_inputs = inbufs;

            cp.audio_outputs_count = 1;
            choc::buffer::ChannelArrayBuffer<float> buf{2, (unsigned int)procblocksize};
            buf.clear();
            clap_audio_buffer outbufs[1];
            outbufs[0].channel_count = 2;
            outbufs[0].constant_mask = 0;
            outbufs[0].latency = 0;
            auto chansdata = buf.getView().data.channels;
            outbufs[0].data32 = (float **)chansdata;
            cp.audio_outputs = outbufs;

            clap_event_transport transport;
            memset(&transport, 0, sizeof(clap_event_transport));
            cp.transport = &transport;
            transport.tempo = 120;
            transport.header.flags = 0;
            transport.header.size = sizeof(clap_event_transport);
            transport.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            transport.header.time = 0;
            transport.header.type = CLAP_EVENT_TRANSPORT;
            transport.flags = CLAP_TRANSPORT_IS_PLAYING;

            clap::helpers::EventList list_in;
            clap::helpers::EventList list_out;
            cp.in_events = list_in.clapInputEvents();
            cp.out_events = list_out.clapOutputEvents();

            std::cout << "plug activated\n";
            m_plug->startProcessing();
            int outcounter = 0;
            int outlensamples = duration * samplerate;
            choc::audio::AudioFileProperties outfileprops;
            outfileprops.formatName = "WAV";
            outfileprops.bitDepth = choc::audio::BitDepth::float32;
            outfileprops.numChannels = 2;
            outfileprops.sampleRate = samplerate;
            choc::audio::WAVAudioFileFormat<true> wavformat;
            auto writer = wavformat.createWriter(filename, outfileprops);

            // choc::buffer::ChannelArrayView<float> bufferview(
            //     choc::buffer::SeparateChannelLayout<float>(cp.audio_outputs->data32));
            // bufferview.clear();
            bool offsent = false;
            while (outcounter < outlensamples)
            {
                if (outcounter == 0)
                {
                    auto ev = xenakios::make_event_note(0, CLAP_EVENT_NOTE_ON, 0, 0, 60, -1, 1.0);
                    list_in.push((const clap_event_header *)&ev);
                    std::cout << "sent on at " << outcounter << "\n";
                }
                if (outcounter >= outlensamples / 2 && offsent == false)
                {
                    auto ev = xenakios::make_event_note(0, CLAP_EVENT_NOTE_OFF, 0, 0, 60, -1, 1.0);
                    list_in.push((const clap_event_header *)&ev);
                    offsent = true;
                    std::cout << "sent off at " << outcounter << "\n";
                }
                m_plug->process(&cp);
                list_out.clear();
                list_in.clear();
                writer->appendFrames(buf.getView());
                // std::cout << outcounter << " ";
                cp.steady_time = outcounter;

                outcounter += procblocksize;
            }
            m_plug->stopProcessing();
            writer->flush();
            std::cout << "\nfinished\n";
            renderloopfinished = true;
        });
        using namespace std::chrono_literals;
        // fake event loop to flush the on main thread requests from the plugin
        while (!renderloopfinished)
        {
            m_plug->runMainThreadTasks();
            // might be needlessly short sleep
            std::this_thread::sleep_for(1ms);
        }
        th.join();
    }
};
