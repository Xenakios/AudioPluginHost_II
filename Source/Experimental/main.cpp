#include <iostream>
#include "testprocessors.h"
#include <memory>
#include <vector>

#include <clap/helpers/event-list.hh>

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
    // we need 4 buffers because inputs and outputs must be separate!
    std::vector<float> procbuf(2 * numchans * blocksize);

    std::vector<std::unique_ptr<XAPNode>> proc_nodes;
    proc_nodes.emplace_back(std::make_unique<XAPNode>(std::make_unique<ToneProcessorTest>()));
    proc_nodes.emplace_back(std::make_unique<XAPNode>(std::make_unique<GainProcessorTest>()));
    proc_nodes.emplace_back(std::make_unique<XAPNode>(std::make_unique<JucePluginWrapper>()));
    for (auto &n : proc_nodes)
        n->processor->activate(sr, 0, blocksize);

    std::vector<std::unique_ptr<XAPNode>> mod_nodes;
    mod_nodes.emplace_back(std::make_unique<XAPNode>(std::make_unique<ToneProcessorTest>(true, 0)));

    for (auto &m : mod_nodes)
        m->processor->activate(sr, 0, blocksize);

    clap_process ctx;
    ctx.frames_count = blocksize;
    float *inputbuffers[2] = {&procbuf[0 * blocksize], &procbuf[1 * blocksize]};
    float *outputbuffers[2] = {&procbuf[2 * blocksize], &procbuf[3 * blocksize]};
    ctx.audio_inputs_count = 1;
    clap_audio_buffer clapinputbuffer;
    clapinputbuffer.channel_count = 2;
    clapinputbuffer.constant_mask = 0;
    clapinputbuffer.data32 = inputbuffers;
    ctx.audio_inputs = &clapinputbuffer;

    ctx.audio_outputs_count = 1;
    clap_audio_buffer clapoutputbuffer;
    clapoutputbuffer.channel_count = 2;
    clapoutputbuffer.constant_mask = 0;
    clapoutputbuffer.data32 = outputbuffers;
    ctx.audio_outputs = &clapoutputbuffer;

    juce::File outfile(R"(C:\develop\AudioPluginHost_mk2\Source\Experimental\out.wav)");
    outfile.deleteFile();
    auto ostream = outfile.createOutputStream();
    WavAudioFormat wav;
    auto writer = wav.createWriterFor(ostream.release(), sr, 2, 32, {}, 0);

    int outlen = 44100 * 5;
    int outcounter = 0;

    while (outcounter < outlen)
    {
        for (size_t index = 0; index < proc_nodes.size(); ++index)
        {
            auto &node = proc_nodes[index];
            if (index == 0)
            {
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
            }
            if (index == 1)
            {
                float volume = juce::jmap<float>(outcounter, 0, outlen / 2, -36.0, 0.0);
                if (outcounter >= outlen / 2)
                    volume = -12.0;
                xenakios::pushParamEvent(node->inEvents, false, 0,
                                         (clap_id)GainProcessorTest::ParamIds::Volume, volume);
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
