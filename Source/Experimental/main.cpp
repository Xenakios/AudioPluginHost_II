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
    int blocksize = 441;
    int numInchans = 2;
    int numOutChans = 2;
    double sr = 44100;
    std::string pathprefix = R"(C:\Program Files\Common Files\)";
    std::vector<std::unique_ptr<XAPNode>> proc_nodes;
    proc_nodes.emplace_back(
        std::make_unique<XAPNode>(std::make_unique<ClapEventSequencerProcessor>()));
    // proc_nodes.emplace_back(std::make_unique<XAPNode>(std::make_unique<ToneProcessorTest>()));
    // proc_nodes.emplace_back(std::make_unique<XAPNode>(std::make_unique<FilePlayerProcessor>()));
    proc_nodes.emplace_back(std::make_unique<XAPNode>(std::make_unique<ClapPluginFormatProcessor>(
        pathprefix + R"(CLAP\Surge Synth Team\Surge XT.clap)", 0)));
    proc_nodes.emplace_back(std::make_unique<XAPNode>(std::make_unique<GainProcessorTest>()));
    proc_nodes.emplace_back(std::make_unique<XAPNode>(
        std::make_unique<JucePluginWrapper>(pathprefix + R"(VST3\ValhallaVintageVerb.vst3)")));
    // proc_nodes.emplace_back(std::make_unique<XAPNode>(std::make_unique<ClapPluginFormatProcessor>(
    //     R"(C:\Program Files\Common Files\CLAP\Surge Synth Team\Surge XT Effects.clap)", 0)));

    // auto surgefxclap = std::make_unique<ClapPluginFormatProcessor>(
    //     R"(C:\Program Files\Common Files\CLAP\Surge Synth Team\Surge XT Effects.clap)", 0);
    for (auto &n : proc_nodes)
        n->processor->activate(sr, 1, blocksize);
    auto &p = proc_nodes.back()->processor;
    for (int i = 0; i < p->paramsCount(); ++i)
    {
        clap_param_info pinfo;
        p->paramsInfo(i, &pinfo);
        std::cout << i << "\t" << pinfo.name << "\t" << pinfo.default_value << "\n";
    }
    std::vector<std::unique_ptr<XAPNode>> mod_nodes;
    mod_nodes.emplace_back(std::make_unique<XAPNode>(std::make_unique<ToneProcessorTest>(true, 0)));

    for (auto &m : mod_nodes)
        m->processor->activate(sr, 0, blocksize);
    clap_event_transport transport;
    memset(&transport, 0, sizeof(clap_event_transport));
    transport.flags = CLAP_TRANSPORT_HAS_SECONDS_TIMELINE | CLAP_TRANSPORT_IS_PLAYING;
    clap_process ctx;
    ctx.transport = &transport;
    ctx.frames_count = blocksize;
    std::vector<float> procbuf(numInchans * blocksize + numOutChans * blocksize);
    std::vector<float *> inputbuffers;
    for (int i = 0; i < numInchans; ++i)
        inputbuffers.push_back(&procbuf[i * blocksize]);
    std::vector<float *> outputbuffers;
    for (int i = numInchans; i < numInchans + numOutChans; ++i)
        outputbuffers.push_back(&procbuf[i * blocksize]);
    
    ctx.audio_inputs_count = 1;
    clap_audio_buffer clapinputbuffers[1];
    clapinputbuffers[0].channel_count = numInchans;
    clapinputbuffers[0].constant_mask = 0;
    clapinputbuffers[0].data32 = inputbuffers.data();
    ctx.audio_inputs = clapinputbuffers;

    ctx.audio_outputs_count = 1;
    clap_audio_buffer clapoutputbuffers[1];
    clapoutputbuffers[0].channel_count = numOutChans;
    clapoutputbuffers[0].constant_mask = 0;
    clapoutputbuffers[0].data32 = outputbuffers.data();
    ctx.audio_outputs = clapoutputbuffers;

    juce::File outfile(R"(C:\develop\AudioPluginHost_mk2\Source\Experimental\out.wav)");
    outfile.deleteFile();
    auto ostream = outfile.createOutputStream();
    WavAudioFormat wav;
    auto writer = wav.createWriterFor(ostream.release(), sr, 2, 32, {}, 0);

    int outlen = 44100 * 120;
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
            // proc_nodes[0]->inEvents.push(reinterpret_cast<const clap_event_header *>(&ev));
        }
        // we need to do this stuff here because the clap transport doesn't have floating point
        // seconds!!
        double x = outcounter / sr; // in seconds
        clap_sectime y = std::round(CLAP_SECTIME_FACTOR * x);
        transport.song_pos_seconds = y;
        for (size_t index = 0; index < proc_nodes.size(); ++index)
        {
            auto &node = proc_nodes[index];
            if (index == 0 && false)
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
            if (index == 0 && true)
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
                /*
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
                }*/

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
            if (index == 3)
            {
                xenakios::pushParamEvent(node->inEvents, false, 0, 0, 0.1);
            }
            ctx.in_events = node->inEvents.clapInputEvents();
            ctx.out_events = node->outEvents.clapOutputEvents();
            node->processor->process(&ctx);
            if (index == 0)
            {
                for (int i = 0; i < node->outEvents.size(); ++i)
                {
                    proc_nodes[1]->inEvents.push(node->outEvents.get(i));
                }
            }

            node->inEvents.clear();
            node->outEvents.clear();
            for (int i = 0; i < 2; ++i)
                for (int j = 0; j < blocksize; ++j)
                    ctx.audio_inputs[0].data32[i][j] = ctx.audio_outputs[0].data32[i][j];
        }

        writer->writeFromFloatArrays(outputbuffers.data(), 2, blocksize);
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
