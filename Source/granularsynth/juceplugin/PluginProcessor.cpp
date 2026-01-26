#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Tunings.h"
//==============================================================================
AudioPluginAudioProcessor::AudioPluginAudioProcessor()
    : AudioProcessor(BusesProperties()
#if !JucePlugin_IsMidiEffect
#if !JucePlugin_IsSynth
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
#endif
                         .withOutput("Output", juce::AudioChannelSet::ambisonic(3), true)

#endif
      )
{
    auto foo = Tunings::evenDivisionOfCentsByM(1200, 12);
    from_gui_fifo.reset(1024);
    to_gui_fifo.reset(1024);
    addParameter(parAmbiOrder = new juce::AudioParameterChoice({"AMBO", 1}, "Ambisonics Order",
                                                               {"STEREO", "1ST", "2ND", "3RD"}, 0));
    addParameter(parOscType = new juce::AudioParameterChoice(
                     {"OSCTY", 1}, "Oscillator Type",
                     {"SINE", "SEMISINE", "TRIANGLE", "SAW", "SQUARE", "FM", "NOISE"}, 0));
    juce::NormalisableRange<float> range;

    addParameter(parGrainRate = new juce::AudioParameterFloat(
                     {"GRATE", 1}, "Grain Rate", {0.0f, 7.0f}, 4.0f, "Hz",
                     juce::AudioProcessorParameter::genericParameter, [](double x, int nd) {
                         double hz = std::pow(2.0, x);
                         return juce::String(hz, 2);
                     }));
    addParameter(parGrainDuration = new juce::AudioParameterFloat({"GDUR", 1}, "Grain Duration",
                                                                  0.002f, 0.5f, 0.05f));
    addParameter(parGrainCenterPitch = new juce::AudioParameterFloat(
                     {"GPITCH", 1}, "Grain Center Pitch", -36.0f, 36.0f, 0.0f));
    addParameter(parGrainFMPitch = new juce::AudioParameterFloat({"GFMPITCH", 1}, "Grain FM Pitch",
                                                                 -48.0f, 48.0f, 0.0f));
    addParameter(parGrainFMDepth = new juce::AudioParameterFloat({"GFMDEPTH", 1}, "Grain FM Depth",
                                                                 0.0f, 1.0f, 0.0f));
    addParameter(parGrainCenterAzimuth = new juce::AudioParameterFloat(
                     {"GAZI", 1}, "Grain Center Azimuth", -180.0f, 180.0f, 0.0f));
    addParameter(parGrainCenterElevation = new juce::AudioParameterFloat(
                     {"GELEV", 1}, "Grain Center Elevation", -180.0f, 180.0f, 0.0f));
    addParameter(parGrainFilter0Cutoff = new juce::AudioParameterFloat(
                     {"GF0C", 1}, "Filter 1 Cutoff", -36.0, 48.0, 48.0f));
    addParameter(parGrainFilter0Reson = new juce::AudioParameterFloat(
                     {"GF0R", 1}, "Filter 1 Resonance", 0.0, 1.0, 0.0f));
}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor() {}

//==============================================================================
const juce::String AudioPluginAudioProcessor::getName() const { return JucePlugin_Name; }

bool AudioPluginAudioProcessor::acceptsMidi() const
{
#if JucePlugin_WantsMidiInput
    return true;
#else
    return false;
#endif
}

bool AudioPluginAudioProcessor::producesMidi() const
{
#if JucePlugin_ProducesMidiOutput
    return true;
#else
    return false;
#endif
}

bool AudioPluginAudioProcessor::isMidiEffect() const
{
#if JucePlugin_IsMidiEffect
    return true;
#else
    return false;
#endif
}

double AudioPluginAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int AudioPluginAudioProcessor::getNumPrograms()
{
    return 1; // NB: some hosts don't cope very well if you tell them there are 0 programs,
              // so this should be at least 1, even if you're not really implementing programs.
}

int AudioPluginAudioProcessor::getCurrentProgram() { return 0; }

void AudioPluginAudioProcessor::setCurrentProgram(int index) { juce::ignoreUnused(index); }

const juce::String AudioPluginAudioProcessor::getProgramName(int index)
{
    juce::ignoreUnused(index);
    return {};
}

void AudioPluginAudioProcessor::changeProgramName(int index, const juce::String &newName)
{
    juce::ignoreUnused(index, newName);
}

//==============================================================================
void AudioPluginAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(sampleRate, samplesPerBlock);
    workBuffer.resize(samplesPerBlock * 32);
    granulator.prepare(sampleRate, {}, 3, 0, 0.001f, 0.001f);
}

void AudioPluginAudioProcessor::releaseResources() {}

bool AudioPluginAudioProcessor::isBusesLayoutSupported(const BusesLayout &layouts) const
{
    return true;
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono() &&
        layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo() &&
        layouts.getMainOutputChannelSet() != juce::AudioChannelSet::discreteChannels(16))
        return false;

    // This checks if the input layout matches the output layout

    return true;
}

void AudioPluginAudioProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                             juce::MidiBuffer &midiMessages)
{
    juce::ignoreUnused(midiMessages);

    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());
    /*
    for (const auto mm : midiMessages)
    {
        const auto msg = mm.getMessage();
        if (msg.isController())
        {
            if (msg.getControllerNumber() == 21)
                *parGrainRate = juce::jmap<float>(msg.getControllerValue(), 0, 127, 0.0, 6.0);
            if (msg.getControllerNumber() == 22)
                *parGrainDuration = juce::jmap<float>(msg.getControllerValue(), 0, 127, 0.002, 0.2);
            if (msg.getControllerNumber() == 23)
                *parGrainCenterPitch =
                    juce::jmap<float>(msg.getControllerValue(), 0, 127, -36.0, 36.0);
        }
    }
    */
    ThreadMessage msg;
    while (from_gui_fifo.pop(msg))
    {
        if (msg.opcode == 100 && msg.filterindex >= 0 && msg.filterindex < 2)
        {
            granulator.set_filter(msg.filterindex, msg.filtermodel, msg.filterconfig);
        }
        if (msg.opcode == 2 && msg.lfoindex >= 0)
        {
            granulator.modmatrix.lfo_shapes[msg.lfoindex] = msg.lfoshape;
            granulator.modmatrix.lfo_rates[msg.lfoindex] = msg.lforate;
            granulator.modmatrix.lfo_deforms[msg.lfoindex] = msg.lfodeform;
        }
        if (msg.opcode == 1)
        {
            auto &mm = granulator.modmatrix;
            if (msg.moddest >= 1)
            {
                msg.moddest -= 1;
                if (msg.moddest == 1)
                    msg.depth *= 24.0f;
                if (msg.moddest == 2)
                    msg.depth *= 30.0f;
                if (msg.moddest == 3)
                    msg.depth *= 24.0f;
                mm.rt.updateActiveAt(msg.modslot, true);
                // DBG(msg.modslot << " " << msg.modsource << " " << msg.depth << " " <<
                // msg.moddest);
                mm.rt.updateRoutingAt(msg.modslot, mm.sourceIds[msg.modsource],
                                      mm.sourceIds[msg.modvia], {}, mm.targetIds[msg.moddest],
                                      msg.depth);
                if (msg.modvia == 0)
                {
                    mm.rt.routes[msg.modslot].sourceVia = std::nullopt;
                }
                mm.m.prepare(mm.rt, granulator.m_sr, granul_block_size);
            }
            else
            {
                // DBG("deactivated slot " << msg.modslot);
                mm.rt.updateActiveAt(msg.modslot, false);
                mm.m.prepare(mm.rt, granulator.m_sr, granul_block_size);
            }
        }
    }
    if (prior_ambi_order != parAmbiOrder->getIndex())
    {
        /*
        prior_ambi_order = parAmbiOrder->getIndex();
        if (prior_ambi_order >= 1)
            granulator.set_ambisonics_order(prior_ambi_order);
        if (prior_ambi_order == 0)
            granulator.set_ambisonics_order(1);
        */
    }
    granulator.grain_rate_oct = parGrainRate->get();
    granulator.pitch_center = parGrainCenterPitch->get();
    granulator.grain_dur = parGrainDuration->get();
    granulator.azi_center = parGrainCenterAzimuth->get();
    granulator.ele_center = parGrainCenterElevation->get();
    granulator.filt_cut_off = parGrainFilter0Cutoff->get();
    granulator.filt_reso = parGrainFilter0Reson->get();
    granulator.osc_type = parOscType->getIndex();
    granulator.fm_pitch = parGrainFMPitch->get();
    granulator.fm_depth = parGrainFMDepth->get();
    granulator.process_block(workBuffer.data(), buffer.getNumSamples());

    int procnumoutchs = granulator.num_out_chans;
    auto channelDatas = buffer.getArrayOfWritePointers();
    if (totalNumOutputChannels == 2)
    {
        for (int j = 0; j < buffer.getNumSamples(); ++j)
        {
            float m = workBuffer[j * procnumoutchs + 0];
            float s = workBuffer[j * procnumoutchs + 1];
            channelDatas[0][j] = std::clamp(m + s, -1.0f, 1.0f);
            channelDatas[1][j] = std::clamp(m - s, -1.0f, 1.0f);
        }
    }
    if (totalNumOutputChannels == 16)
    {
        for (int i = 0; i < 16; ++i)
        {
            for (int j = 0; j < buffer.getNumSamples(); ++j)
            {
                float s = workBuffer[j * procnumoutchs + i];
                channelDatas[i][j] = 0.25 * std::clamp(s, -1.0f, 1.0f);
            }
        }
    }
}

//==============================================================================
bool AudioPluginAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor *AudioPluginAudioProcessor::createEditor()
{
    sendExtraStatesToGUI();
    // return new juce::GenericAudioProcessorEditor(*this);
    return new AudioPluginAudioProcessorEditor(*this);
}

//==============================================================================
void AudioPluginAudioProcessor::getStateInformation(juce::MemoryBlock &destData)
{
    auto state = choc::value::createObject("state");
    auto mainparams = choc::value::createObject("params");
    auto &pars = getParameters();
    for (int i = 0; i < pars.size(); ++i)
    {
        auto p = dynamic_cast<juce::RangedAudioParameter *>(pars[i]);
        std::string id = p->getParameterID().toStdString();
        mainparams.setMember(id, p->convertFrom0to1(p->getValue()));
    }
    state.setMember("params", mainparams);
    auto lfostates = choc::value::createEmptyArray();
    for (int i = 0; i < GranulatorModMatrix::numLfos; ++i)
    {
        auto lfostate = choc::value::createObject("lfostate");
        lfostate.setMember("shape", granulator.modmatrix.lfo_shapes[i]);
        lfostate.setMember("rate", granulator.modmatrix.lfo_rates[i]);
        lfostate.setMember("deform", granulator.modmatrix.lfo_deforms[i]);
        lfostates.addArrayElement(lfostate);
    }
    state.setMember("lfostates", lfostates);
    auto modroutings = choc::value::createEmptyArray();
    auto &mm = granulator.modmatrix;
    for (int i = 0; i < GranulatorModConfig::FixedMatrixSize; ++i)
    {
        if (mm.rt.routes[i].active && mm.rt.routes[i].source && mm.rt.routes[i].target)
        {
            auto routingstate = choc::value::createObject("routing");
            routingstate.setMember("slot", i);
            routingstate.setMember("source", (int)(mm.rt.routes[i].source->src));
            routingstate.setMember("depth", mm.rt.routes[i].depth);
            routingstate.setMember("dest", (int)(mm.rt.routes[i].target->baz));
            modroutings.addArrayElement(routingstate);
        }
    }
    state.setMember("modroutings", modroutings);
    auto json = choc::json::toString(state);
    destData.append(json.data(), json.size());
    DBG(json);
}

void AudioPluginAudioProcessor::setStateInformation(const void *data, int sizeInBytes)
{
    std::string json((char *)data, (char *)data + sizeInBytes);
    // DBG(json);
    auto state = choc::json::parse(json);
    if (state.hasObjectMember("params"))
    {
        auto params = state["params"];
        auto &pars = getParameters();
        for (int i = 0; i < pars.size(); ++i)
        {
            auto p = dynamic_cast<juce::RangedAudioParameter *>(pars[i]);
            std::string id = p->getParameterID().toStdString();
            if (params.hasObjectMember(id))
            {
                p->setValue(p->convertTo0to1(params[id].get<float>()));
            }
        }
    }
    suspendProcessing(true);
    if (state.hasObjectMember("lfostates"))
    {
        auto lfostates = state["lfostates"];
        for (int i = 0; i < lfostates.size(); ++i)
        {
            auto lfostate = lfostates[i];
            if (i < granulator.modmatrix.numLfos)
            {
                granulator.modmatrix.lfo_shapes[i] = lfostate["shape"].get<int>();
                granulator.modmatrix.lfo_rates[i] = lfostate["rate"].get<float>();
                granulator.modmatrix.lfo_deforms[i] = lfostate["deform"].get<float>();
            }
        }
    }
    if (state.hasObjectMember("modroutings"))
    {
        auto routings = state["modroutings"];
        auto &mm = granulator.modmatrix;
        for (int i = 0; i < GranulatorModConfig::FixedMatrixSize; ++i)
        {
            mm.rt.updateActiveAt(i, false);
        }
        for (int i = 0; i < routings.size(); ++i)
        {
            auto rstate = routings[i];
            int slot = rstate["slot"].get<int>();
            if (slot >= 0 && slot < GranulatorModConfig::FixedMatrixSize)
            {
                mm.rt.updateActiveAt(slot, true);
                int src = rstate["source"].get<int>();
                float d = rstate["depth"].get<float>();
                int dest = rstate["dest"].get<int>();

                mm.rt.updateRoutingAt(slot, mm.sourceIds[src], mm.targetIds[dest], d);
            }
        }
        mm.m.prepare(mm.rt, granulator.m_sr, granul_block_size);
    }
    suspendProcessing(false);
    sendExtraStatesToGUI();
}

void AudioPluginAudioProcessor::sendExtraStatesToGUI()
{
    for (int i = 0; i < granulator.modmatrix.numLfos; ++i)
    {
        ThreadMessage msg;
        msg.opcode = 2;
        msg.lfoindex = i;
        msg.lforate = granulator.modmatrix.lfo_rates[i];
        msg.lfoshape = granulator.modmatrix.lfo_shapes[i];
        msg.lfodeform = granulator.modmatrix.lfo_deforms[i];
        to_gui_fifo.push(msg);
    }
    auto &mm = granulator.modmatrix;
    for (int i = 0; i < GranulatorModConfig::FixedMatrixSize; ++i)
    {
        if (mm.rt.routes[i].source && mm.rt.routes[i].target)
        {
            ThreadMessage msg;
            msg.opcode = 1;
            msg.modslot = i;
            msg.modsource = mm.rt.routes[i].source->src;
            msg.depth = mm.rt.routes[i].depth;
            msg.moddest = mm.rt.routes[i].target->baz;
            if (msg.moddest == 1)
                msg.depth /= 24.0f;
            if (msg.moddest == 2)
                msg.depth /= 30.0f;
            if (msg.moddest == 3)
                msg.depth /= 24.0f;
            to_gui_fifo.push(msg);
        }
    }
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new AudioPluginAudioProcessor(); }
