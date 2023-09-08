#include <iostream>
#include "testprocessors.h"
#include <memory>
#include <vector>

#include <clap/helpers/event-list.hh>
#include "clap_xaudioprocessor.h"

inline void mapModulationEvents(const clap::helpers::EventList &sourceList, clap_id sourceParId,
                                clap::helpers::EventList &destList, clap_id destParId,
                                double destAmount)
{
    for (int i = 0; i < sourceList.size(); ++i)
    {
        auto ev = sourceList.get(i);
        if (ev->type == CLAP_EVENT_PARAM_MOD)
        {
            auto pev = reinterpret_cast<clap_event_param_mod *>(ev);
            double amt = pev->amount * destAmount;
            xenakios::pushParamEvent(destList, true, 0, destParId, amt);
        }
    }
}

class XAPNode
{
  public:
    XAPNode(std::unique_ptr<xenakios::XAudioProcessor> nodeIn) : processor(std::move(nodeIn)) {}
    std::unique_ptr<xenakios::XAudioProcessor> processor;
    clap::helpers::EventList inEvents;
    clap::helpers::EventList outEvents;
};

int main()
{
    juce::ScopedJuceInitialiser_GUI gui_init;
    int blocksize = 512;
    int numchans = 2;
    double sr = 44100;

    std::vector<std::unique_ptr<XAPNode>> proc_nodes;
    // proc_nodes.emplace_back(std::make_unique<XAPNode>(std::make_unique<ToneProcessorTest>()));
    proc_nodes.emplace_back(std::make_unique<XAPNode>(std::make_unique<FilePlayerProcessor>()));
    // proc_nodes.emplace_back(std::make_unique<XAPNode>(std::make_unique<JucePluginWrapper>(
    //    R"(C:\Program Files\Common Files\VST3\Surge Synth Team\Surge
    //    XT.vst3\Contents\x86_64-win\Surge XT.vst3)")));
    proc_nodes.emplace_back(std::make_unique<XAPNode>(std::make_unique<GainProcessorTest>()));
    proc_nodes.emplace_back(std::make_unique<XAPNode>(std::make_unique<JucePluginWrapper>(
        R"(C:\Program Files\Common Files\VST3\ValhallaVintageVerb.vst3)")));
    // auto surge = std::make_unique<XAPNode>(std::make_unique<JucePluginWrapper>(
    //     R"(C:\Program Files\Common Files\VST3\Surge Synth Team\Surge
    //     XT.vst3\Contents\x86_64-win\Surge XT.vst3)"));
    // surge->processor->activate(44100, 0, blocksize);

    auto surgefxclap = std::make_unique<ClapPluginFormatProcessor>(
        R"(C:\Program Files\Common Files\CLAP\Surge Synth Team\Surge XT Effects.clap)", 0);
    for (auto &n : proc_nodes)
        n->processor->activate(sr, 0, blocksize);

    std::vector<std::unique_ptr<XAPNode>> mod_nodes;
    mod_nodes.emplace_back(std::make_unique<XAPNode>(std::make_unique<ToneProcessorTest>(true, 0)));

    for (auto &m : mod_nodes)
        m->processor->activate(sr, 0, blocksize);
    clap_event_transport transport;
    memset(&transport, 0, sizeof(clap_event_transport));
    clap_process ctx;
    ctx.transport = &transport;
    ctx.frames_count = blocksize;
    // we need 4 buffers because inputs and outputs are separate!
    std::vector<float> procbuf(2 * numchans * blocksize);
    float *inputbuffers[2] = {&procbuf[0 * blocksize], &procbuf[1 * blocksize]};
    float *outputbuffers[2] = {&procbuf[2 * blocksize], &procbuf[3 * blocksize]};
    ctx.audio_inputs_count = 1;
    clap_audio_buffer clapinputbuffers[1];
    clapinputbuffers[0].channel_count = 2;
    clapinputbuffers[0].constant_mask = 0;
    clapinputbuffers[0].data32 = inputbuffers;
    ctx.audio_inputs = clapinputbuffers;

    ctx.audio_outputs_count = 1;
    clap_audio_buffer clapoutputbuffers[1];
    clapoutputbuffers[0].channel_count = 2;
    clapoutputbuffers[0].constant_mask = 0;
    clapoutputbuffers[0].data32 = outputbuffers;
    ctx.audio_outputs = clapoutputbuffers;

    juce::File outfile(R"(C:\develop\AudioPluginHost_mk2\Source\Experimental\out.wav)");
    outfile.deleteFile();
    auto ostream = outfile.createOutputStream();
    WavAudioFormat wav;
    auto writer = wav.createWriterFor(ostream.release(), sr, 2, 32, {}, 0);

    int outlen = 44100 * 10;
    int outcounter = 0;

    juce::Random rng;
    while (outcounter < outlen)
    {
        if (outcounter == 0)
        {
            xenakios_event_change_file ev;
            ev.header.flags = 0;
            ev.header.size = sizeof(xenakios_event_change_file);
            ev.header.space_id = XENAKIOS_CLAP_NAMESPACE;
            ev.header.time = 0;
            ev.header.type = XENAKIOS_EVENT_CHANGEFILE;
            ev.target = 0;
            strcpy_s(ev.filepath, R"(C:\MusicAudio\sourcesamples\lareskitta01.wav)");
            proc_nodes[0]->inEvents.push(reinterpret_cast<const clap_event_header *>(&ev));
        }
        transport.song_pos_seconds = outcounter * sr;
        for (size_t index = 0; index < proc_nodes.size(); ++index)
        {
            auto &node = proc_nodes[index];
            if (index == 0)
            {
                double rate = juce::jmap<float>(outcounter, 0, outlen, 1.0, 0.1);
                if (rate < 0.1)
                    rate = 0.1;
                xenakios::pushParamEvent(node->inEvents, false, 0,
                                         (clap_id)FilePlayerProcessor::ParamIds::Playrate, rate);
                for (int i = 0; i < blocksize; ++i)
                {
                    int pos = outcounter + i;
                    if (pos % 22050 == 0)
                    {
                        double pitch = -12.0 + 24.0 * rng.nextDouble();
                        xenakios::pushParamEvent(node->inEvents, false, 0,
                                                 (clap_id)FilePlayerProcessor::ParamIds::Pitch,
                                                 pitch);
                    }
                }
            }
            if (index == 0 && false)
            {
#ifdef INTERNAL_TONEGEN
                if (outcounter < outlen / 2)
                    xenakios::pushParamEvent(node->inEvents, false, 0,
                                             (clap_id)ToneProcessorTest::ParamIds::Pitch, 69.0);
                else
                    xenakios::pushParamEvent(node->inEvents, false, 0,
                                             (clap_id)ToneProcessorTest::ParamIds::Pitch, 63.0);
                auto &modnode = mod_nodes[0];
                xenakios::pushParamEvent(modnode->inEvents, false, 0,
                                         (clap_id)ToneProcessorTest::ParamIds::Pitch, -6.0);
                ctx.in_events = modnode->inEvents.clapInputEvents();
                ctx.out_events = modnode->outEvents.clapOutputEvents();
                modnode->processor->process(&ctx);
                mapModulationEvents(modnode->outEvents, 0, node->inEvents,
                                    (clap_id)ToneProcessorTest::ParamIds::Pitch, 0.4);
                modnode->inEvents.clear();
                modnode->outEvents.clear();
#else
                auto pushmidimsg = [](clap::helpers::EventList &dest,
                                      const juce::MidiMessage &midimsg, int timestamp) {
                    clap_event_midi clapmsg;
                    clapmsg.header.flags = 0;
                    clapmsg.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                    clapmsg.header.size = sizeof(clap_event_midi);
                    clapmsg.header.time = timestamp;
                    clapmsg.header.type = CLAP_EVENT_MIDI;
                    clapmsg.port_index = 0;
                    clapmsg.data[0] = midimsg.getRawData()[0];
                    clapmsg.data[1] = midimsg.getRawData()[1];
                    clapmsg.data[2] = midimsg.getRawData()[2];
                    dest.push(reinterpret_cast<const clap_event_header *>(&clapmsg));
                };
                auto pushclapnotemsg = [](clap::helpers::EventList &dest, uint16_t msgtype,
                                          int timestamp, int key, int channel, double velo) {
                    clap_event_note clapmsg;
                    clapmsg.header.flags = 0;
                    clapmsg.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                    clapmsg.header.size = sizeof(clap_event_note);
                    clapmsg.header.time = timestamp;
                    clapmsg.header.type = msgtype;

                    clapmsg.port_index = 0;
                    clapmsg.channel = channel;
                    clapmsg.key = key;
                    clapmsg.note_id = -1;
                    clapmsg.velocity = velo;
                    dest.push(reinterpret_cast<const clap_event_header *>(&clapmsg));
                };
                for (int i = 0; i < blocksize; ++i)
                {
                    int pos = outcounter + i;
                    if (pos % 44100 == 0)
                    {
                        pushmidimsg(node->inEvents, juce::MidiMessage::noteOn(1, 60, 0.5f), i);
                        pushmidimsg(node->inEvents, juce::MidiMessage::noteOn(1, 63, 0.5f), i);
                        pushclapnotemsg(node->inEvents, CLAP_EVENT_NOTE_ON, i, 67, 0, 0.5);
                    }
                    if (pos % 44100 == 11025)
                    {
                        pushclapnotemsg(node->inEvents, CLAP_EVENT_NOTE_OFF, i, 60, 0, 0.5);
                        pushmidimsg(node->inEvents, juce::MidiMessage::noteOff(1, 63, 0.5f), i);
                        pushmidimsg(node->inEvents, juce::MidiMessage::noteOff(1, 67, 0.5f), i);
                    }
                }
#endif
            }
            if (index == 1)
            {
                float volume = juce::jmap<float>(outcounter, 0, outlen / 2, -36.0, 0.0);
                if (outcounter >= outlen / 2)
                    volume = -12.0;
                // xenakios::pushParamEvent(node->inEvents, false, 0,
                //                          (clap_id)GainProcessorTest::ParamIds::Volume, volume);
            }
            if (index == 2)
            {
                xenakios::pushParamEvent(node->inEvents, false, 0, 0, 0.2);
            }
            ctx.in_events = node->inEvents.clapInputEvents();
            ctx.out_events = node->outEvents.clapOutputEvents();
            node->processor->process(&ctx);
            node->inEvents.clear();
            node->outEvents.clear();
            for (int i = 0; i < 2; ++i)
                for (int j = 0; j < blocksize; ++j)
                    ctx.audio_inputs[0].data32[i][j] = ctx.audio_outputs[0].data32[i][j];
        }

        writer->writeFromFloatArrays(outputbuffers, 2, blocksize);
        outcounter += blocksize;
    }
    delete writer;
    for (auto &p : proc_nodes)
    {
        jassert(p->inEvents.size() == 0);
        jassert(p->outEvents.size() == 0);
    }
    for (auto &p : mod_nodes)
    {
        jassert(p->inEvents.size() == 0);
        jassert(p->outEvents.size() == 0);
    }
    return 0;
}
