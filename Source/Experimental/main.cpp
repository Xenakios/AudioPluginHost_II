#include <iostream>
#include "testprocessors.h"
#include <memory>
#include <vector>

#include <clap/helpers/event-list.hh>

inline void pushParamChange(clap::helpers::EventList &elist, uint32_t timeStamp, clap_id paramId,
                            double value)
{
    clap_event_param_value pv;
    pv.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    pv.header.size = sizeof(clap_event_param_value);
    pv.header.flags = 0;
    pv.header.time = timeStamp;
    pv.header.type = CLAP_EVENT_PARAM_VALUE;
    pv.cookie = nullptr;
    pv.param_id = paramId;
    pv.value = value;
    elist.push(reinterpret_cast<const clap_event_header *>(&pv));
}

int main()
{
    int blocksize = 512;
    int numchans = 2;
    // we need 4 buffers because inputs and outputs must be separate!
    std::vector<float> procbuf(2 * numchans * blocksize);
    auto proc = std::make_unique<GainProcessorTest>();
    clap_param_info pinfo;
    proc->paramsInfo(1, &pinfo);
    double sr = 44100;
    proc->activate(sr, 0, blocksize);
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

    clap::helpers::EventList inevents;
    clap::helpers::EventList outevents;

    ctx.in_events = inevents.clapInputEvents();
    ctx.out_events = outevents.clapOutputEvents();

    juce::File outfile(R"(C:\develop\AudioPluginHost_mk2\Source\Experimental\out.wav)");
    outfile.deleteFile();
    auto ostream = outfile.createOutputStream();
    WavAudioFormat wav;
    auto writer = wav.createWriterFor(ostream.release(), sr, 2, 32, {}, 0);

    int outlen = 44100;
    int outcounter = 0;
    double sinephase = 0.0;
    while (outcounter < outlen)
    {
        for (int i = 0; i < blocksize; ++i)
        {
            float sample = std::sin(2 * 3.141592 / sr * sinephase * 440.0);
            inputbuffers[0][i] = sample;
            inputbuffers[1][i] = sample;
            sinephase += 1.0;
        }
        inevents.clear();
        float volume = juce::jmap<float>(outcounter,0,outlen/2,-36.0,0.0);
        if (outcounter >= outlen / 2)
            volume = -48.0;
        pushParamChange(inevents, 0, (clap_id)GainProcessorTest::ParamIds::Volume, volume);

        proc->process(&ctx);
        writer->writeFromFloatArrays(outputbuffers, 2, blocksize);
        outcounter += blocksize;
    }
    delete writer;
    return 0;
}
