#pragma once

#include <vector>
#include <optional>
#include <iostream>
#include "audio/choc_AudioFileFormat_WAV.h"
#include "xapdsp.h"
#include "xap_utils.h"
#include "xaps/clap_xaudioprocessor.h"
#include "containers/choc_Span.h"
#include "sst/basic-blocks/modulators/SimpleLFO.h"
#include "gui/choc_DesktopWindow.h"
#include "gui/choc_MessageLoop.h"

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
    size_t getNumEvents() const { return m_evlist.size(); }
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
    void addNoteExpression(double time, int port, int channel, int key, int note_id, int net,
                           double amt)
    {
        auto ev = xenakios::make_event_note_expression(0, net, port, channel, key, note_id, amt);
        m_evlist.push_back(Event(time, &ev));
    }
    void addParameterEvent(bool ismod, double time, int port, int channel, int key, int note_id,
                           uint32_t par_id, double value)
    {
        if (ismod)
        {
            auto ev = xenakios::make_event_param_mod(0, par_id, value, nullptr, port, channel, key,
                                                     note_id, 0);
            m_evlist.push_back(Event(time, &ev));
        }
        else
        {
            auto ev = xenakios::make_event_param_value(0, par_id, value, nullptr, port, channel,
                                                       key, note_id, 0);
            m_evlist.push_back(Event(time, &ev));
        }
    }
    void addProgramChange(double time, int port, int channel, int program)
    {
        clap_event_midi ev;
        ev.header.flags = 0;
        ev.header.size = sizeof(clap_event_midi);
        ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev.header.time = 0;
        ev.header.type = CLAP_EVENT_MIDI;
        ev.port_index = port;
        ev.data[0] = 0xc0 + (channel % 16);
        ev.data[1] = program & 0x7f;
        ev.data[2] = 0;
        m_evlist.push_back(Event(time, &ev));
    }
    void addMIDI1Message(double time, int port, uint8_t b0, uint8_t b1, uint8_t b2)
    {
        clap_event_midi ev;
        ev.header.flags = 0;
        ev.header.size = sizeof(clap_event_midi);
        ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        ev.header.time = 0;
        ev.header.type = CLAP_EVENT_MIDI;
        ev.port_index = port;
        ev.data[0] = b0;
        ev.data[1] = b1;
        ev.data[2] = b2;
        m_evlist.push_back(Event(time, &ev));
    }

    struct Iterator
    {
        /// Creates an iterator positioned at the start of the sequence.
        Iterator(const ClapEventSequence &s) : owner(s) {}
        Iterator(const Iterator &) = default;
        Iterator(Iterator &&) = default;

        /// Seeks the iterator to the given time

        void setTime(double newTimeStamp)
        {
            auto eventData = owner.m_evlist.data();

            while (nextIndex != 0 && eventData[nextIndex - 1].timestamp >= newTimeStamp)
                --nextIndex;

            while (nextIndex < owner.m_evlist.size() &&
                   eventData[nextIndex].timestamp < newTimeStamp)
                ++nextIndex;

            currentTime = newTimeStamp;
        }

        /// Returns the current iterator time
        double getTime() const noexcept { return currentTime; }

        /// Returns a set of events which lie between the current time, up to (but not
        /// including) the given duration. This function then increments the iterator to
        /// set its current time to the end of this block.

        choc::span<const ClapEventSequence::Event> readNextEvents(double duration)
        {
            auto start = nextIndex;
            auto eventData = owner.m_evlist.data();
            auto end = start;
            auto total = owner.m_evlist.size();
            auto endTime = currentTime + duration;
            currentTime = endTime;

            while (end < total && eventData[end].timestamp < endTime)
                ++end;

            nextIndex = end;

            return {eventData + start, eventData + end};
        }

      private:
        const ClapEventSequence &owner;
        double currentTime = 0;
        size_t nextIndex = 0;
    };
};

template <size_t BLOCK_SIZE> class SimpleLFO
{
  public:
    static constexpr size_t BLOCK_SIZE_OS = BLOCK_SIZE * 2;
    double samplerate = 44100;
    alignas(32) float table_envrate_linear[512];
    void initTables()
    {
        double dsamplerate_os = samplerate * 2;
        for (int i = 0; i < 512; ++i)
        {
            double k =
                dsamplerate_os * pow(2.0, (((double)i - 256.0) / 16.0)) / (double)BLOCK_SIZE_OS;
            table_envrate_linear[i] = (float)(1.f / k);
        }
    }
    float envelope_rate_linear_nowrap(float x)
    {
        x *= 16.f;
        x += 256.f;
        int e = std::clamp<int>((int)x, 0, 0x1ff - 1);

        float a = x - (float)e;

        return (1 - a) * table_envrate_linear[e & 0x1ff] +
               a * table_envrate_linear[(e + 1) & 0x1ff];
    }
    sst::basic_blocks::modulators::SimpleLFO<SimpleLFO, BLOCK_SIZE> m_lfo;
    SimpleLFO(double sr) : samplerate(sr), m_lfo(this) { initTables(); }
};

inline void generateNoteExpressionsFromEnvelope(ClapEventSequence &targetSeq,
                                                xenakios::Envelope<64> &sourceEnvelope,
                                                double eventsStartTime, double duration,
                                                double granularity, int net, int port, int chan,
                                                int key, int note_id)
{
    double t = eventsStartTime;
    while (t < duration + granularity + eventsStartTime)
    {
        double v = sourceEnvelope.getValueAtPosition(t - eventsStartTime, 0.0);
        targetSeq.addNoteExpression(t, port, chan, key, note_id, net, v);
        t += granularity;
    }
}

inline void generateParameterEventsFromEnvelope(bool is_mod, ClapEventSequence &targetSeq,
                                                xenakios::Envelope<64> &sourceEnvelope,
                                                double eventsStartTime, double duration,
                                                double granularity, clap_id parid, int port,
                                                int chan, int key, int note_id)
{
    double t = eventsStartTime;
    while (t < duration + granularity + eventsStartTime)
    {
        double v = sourceEnvelope.getValueAtPosition(t - eventsStartTime, 0.0);
        targetSeq.addParameterEvent(is_mod, t, port, chan, key, note_id, parid, v);
        t += granularity;
    }
}

inline xenakios::Envelope<64> generateEnvelopeFromLFO(double rate, double deform, int shape, double envlen,
                                               double envgranul)
{
    double sr = 44100.0;
    constexpr size_t blocklen = 64;
    xenakios::Envelope<blocklen> result;
    SimpleLFO<blocklen> lfo{sr};
    int outpos = 0;
    int outlen = sr * envlen;
    int granlen = envgranul * sr;
    int granpos = 0;
    while (outpos < outlen)
    {
        lfo.m_lfo.process_block(rate, deform, shape, false);
        granpos += blocklen;
        if (granpos >= granlen)
        {
            granpos = 0;
            result.addPoint({outpos / sr, lfo.m_lfo.outputBlock[0]});
        }
        outpos += blocklen;
    }
    return result;
}

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
        else
            throw std::runtime_error("Could not create CLAP plugin");
    }
    std::map<std::string, clap_id> getParameters()
    {
        std::map<std::string, clap_id> result;
        for (size_t i = 0; i < m_plug->paramsCount(); ++i)
        {
            clap_param_info pinfo;
            if (m_plug->paramsInfo(i, &pinfo))
            {
                result[std::string(pinfo.name)] = pinfo.id;
            }
        }
        return result;
    }
    void processToFile(std::string filename, double duration, double samplerate)
    {
        m_seq.sortEvents();
        int procblocksize = 512;
        std::atomic<bool> renderloopfinished{false};
        m_plug->activate(samplerate, procblocksize, procblocksize);
        // even offline, do the processing in another another thread because things
        // can get complicated with plugins like Surge XT because of the thread checks
        std::thread th([&] {
            ClapEventSequence::Iterator eviter(m_seq);
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
            eviter.setTime(0.0);
            while (outcounter < outlensamples)
            {
                auto blockevts = eviter.readNextEvents(procblocksize / samplerate);
                for (auto &e : blockevts)
                {
                    auto ecopy = e.event;
                    ecopy.header.time = (e.timestamp * samplerate) - outcounter;
                    list_in.push((const clap_event_header *)&ecopy);
                    // std::cout << "sent event type " << e.event.header.type << " at samplepos "
                    //           << outcounter + ecopy.header.time << "\n";
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
            std::cout << "finished\n";
            renderloopfinished = true;
        });
        using namespace std::chrono_literals;
        // fake event loop to flush the on main thread requests from the plugin
        while (!renderloopfinished)
        {
            m_plug->runMainThreadTasks();
            std::this_thread::sleep_for(5ms);
        }
        th.join();
    }
    void openPluginGUIBlocking()
    {
        m_plug->mainthread_id() = std::this_thread::get_id();
        choc::ui::setWindowsDPIAwareness(); // For Windows, we need to tell the OS we're
                                            // high-DPI-aware
        m_plug->guiCreate("win32", false);
        uint32_t pw = 0;
        uint32_t ph = 0;
        m_plug->guiGetSize(&pw, &ph);
        choc::ui::DesktopWindow window({100, 100, (int)pw, (int)ph});

        window.setWindowTitle("CHOC Window");
        window.setResizable(true);
        window.setMinimumSize(300, 300);
        window.setMaximumSize(1500, 1200);
        window.windowClosed = [this] {
            m_plug->guiDestroy();
            choc::messageloop::stop();
        };

        clap_window clapwin;
        clapwin.api = "win32";
        clapwin.win32 = window.getWindowHandle();
        m_plug->guiSetParent(&clapwin);
        m_plug->guiShow();
        window.toFront();
        choc::messageloop::run();
    }
    std::unique_ptr<choc::ui::DesktopWindow> m_desktopwindow;
    void openPersistentWindow(std::string title)
    {
        std::thread th([this, title]() {
            // choc::messageloop::initialise();
            choc::ui::setWindowsDPIAwareness();
            m_desktopwindow =
                std::make_unique<choc::ui::DesktopWindow>(choc::ui::Bounds{100, 100, 300, 200});
            m_desktopwindow->setWindowTitle(title);
            m_desktopwindow->toFront();
            m_desktopwindow->windowClosed = [this] {
                std::cout << "window closed\n";
                choc::messageloop::stop();
            };
            choc::messageloop::run();
            std::cout << "finished message loop\n", m_desktopwindow = nullptr;
        });
        th.detach();
    }
};
