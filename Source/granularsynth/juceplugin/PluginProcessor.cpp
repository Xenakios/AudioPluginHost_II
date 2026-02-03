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

    from_gui_fifo.reset(1024);
    params_from_gui_fifo.reset(1024);
    params_to_gui_fifo.reset(1024);
    to_gui_fifo.reset(1024);
    for (int i = 0; i < granulator.parmetadatas.size(); ++i)
    {
        const auto &pmd = granulator.parmetadatas[i];
        juce::String id(pmd.id);
        if (pmd.type == ToneGranulator::pmd::FLOAT)
        {
            auto par = new juce::AudioParameterFloat({id, 1}, pmd.name, pmd.minVal, pmd.maxVal,
                                                     pmd.defaultVal);
            addParameter(par);
            jucepartoindex[(juce::AudioProcessorParameter *)par] = pmd.id;
        }
        if (pmd.type == ToneGranulator::pmd::INT && pmd.discreteValues.size())
        {
            juce::StringArray choices;
            for (auto &e : pmd.discreteValues)
            {
                choices.add(e.second);
            }
            auto par = new juce::AudioParameterChoice({id, 1}, pmd.name, choices, 0);
            addParameter(par);
            jucepartoindex[(juce::AudioProcessorParameter *)par] = pmd.id;
        }
    }
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
    perfMeasurer.reset(sampleRate, samplesPerBlock);
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
    juce::AudioProcessLoadMeasurer::ScopedTimer perftimer(perfMeasurer, buffer.getNumSamples());
    juce::ignoreUnused(midiMessages);

    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    for (const auto mm : midiMessages)
    {
        const auto msg = mm.getMessage();
        if (msg.isController())
        {
            auto &mm = granulator.modmatrix;
            uint32_t ccnum = msg.getControllerNumber();
            auto it = granulator.midiCCMap.find(ccnum);
            if (it != granulator.midiCCMap.end())
            {
                granulator.modSourceValues[it->second] =
                    juce::jmap<float>(msg.getControllerValue(), 0, 127, 0.0, 1.0);
            }
        }
    }
    ParameterMessage parmsg;
    while (params_from_gui_fifo.pop(parmsg))
    {
        if (parmsg.id > 0)
        {
            *granulator.idtoparvalptr[parmsg.id] = parmsg.value;
        }
    }
    ThreadMessage msg;
    while (from_gui_fifo.pop(msg))
    {
        if (msg.opcode == ThreadMessage::OP_FILTERTYPE && msg.filterindex >= 0 &&
            msg.filterindex < 2)
        {
            granulator.set_filter(msg.filterindex, msg.filtermodel, msg.filterconfig);
        }
        if (msg.opcode == ThreadMessage::OP_LFOPARAM && msg.lfoindex >= 0)
        {
            granulator.modmatrix.lfo_unipolars[msg.lfoindex] = msg.lfounipolar;
        }
        auto &mm = granulator.modmatrix;
        if (msg.opcode == ThreadMessage::OP_MODROUTING || msg.opcode == ThreadMessage::OP_MODPARAM)
        {
            jassert(msg.moddest >= 1);
            auto it = granulator.modRanges.find(msg.moddest);
            if (it != granulator.modRanges.end())
                msg.depth *= it->second;
            if (msg.opcode == ThreadMessage::OP_MODPARAM)
            {
                mm.rt.updateDepthAt(msg.modslot, msg.depth);
            }
            else
            {
                mm.rt.updateActiveAt(msg.modslot, true);
                // DBG(msg.modslot << " " << msg.modsource << " " << msg.depth << " " <<
                // msg.moddest);
                mm.rt.updateRoutingAt(
                    msg.modslot, GranulatorModConfig::SourceIdentifier{(uint32_t)msg.modsource},
                    GranulatorModConfig::SourceIdentifier{(uint32_t)msg.modvia},
                    GranulatorModConfig::MyCurve{msg.modcurve, msg.modcurvepar0},
                    GranulatorModConfig::TargetIdentifier{msg.moddest}, msg.depth);
                if (msg.modvia == 0)
                {
                    mm.rt.routes[msg.modslot].sourceVia = std::nullopt;
                }
                mm.m.prepare(mm.rt, granulator.m_sr, granul_block_size);
            }
        }
    }
    // if (prior_ambi_order != parAmbiOrder->getIndex())
    {
        /*
        prior_ambi_order = parAmbiOrder->getIndex();
        if (prior_ambi_order >= 1)
            granulator.set_ambisonics_order(prior_ambi_order);
        if (prior_ambi_order == 0)
            granulator.set_ambisonics_order(1);
        */
    }
    /*
    const auto &pars = getParameters();
    for (auto &p : pars)
    {
        auto it = jucepartoindex.find(p);
        if (it != jucepartoindex.end())
        {
            int parid = it->second;
            auto rpar = dynamic_cast<juce::RangedAudioParameter *>(p);
            jassert(rpar);
            *granulator.idtoparvalptr[parid] = rpar->convertFrom0to1(rpar->getValue());
        }
    }
    */
    granulator.osc_type = *granulator.idtoparvalptr[ToneGranulator::PAR_OSCTYPE];

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
                channelDatas[i][j] = std::clamp(s, -1.0f, 1.0f);
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
    auto &pmds = granulator.parmetadatas;
    for (int i = 0; i < pmds.size(); ++i)
    {
        std::string id = std::to_string(pmds[i].id);
        float v = *granulator.idtoparvalptr[pmds[i].id];
        mainparams.setMember(id, v);
    }
    state.setMember("params", mainparams);
    auto filterstates = choc::value::createEmptyArray();
    for (int i = 0; i < 2; ++i)
    {
        auto filterstate = choc::value::createObject("filterstate");
        filterstate.setMember("model", (int64_t)granulator.filtersModels[i]);
        filterstate.setMember("pt", (int64_t)granulator.filtersConfigs[i].pt);
        filterstate.setMember("st", (int64_t)granulator.filtersConfigs[i].st);
        filterstate.setMember("dt", (int64_t)granulator.filtersConfigs[i].dt);
        filterstate.setMember("mt", (int64_t)granulator.filtersConfigs[i].mt);
        filterstates.addArrayElement(filterstate);
    }
    state.setMember("filterstates", filterstates);
    auto lfostates = choc::value::createEmptyArray();
    for (int i = 0; i < GranulatorModMatrix::numLfos; ++i)
    {
        auto lfostate = choc::value::createObject("lfostate");
        lfostate.setMember("uni", granulator.modmatrix.lfo_unipolars[i]);

        lfostates.addArrayElement(lfostate);
    }
    state.setMember("lfostates", lfostates);
    auto modroutings = choc::value::createEmptyArray();
    auto &mm = granulator.modmatrix;
    for (int i = 0; i < GranulatorModConfig::FixedMatrixSize; ++i)
    {
        if (mm.rt.routes[i].active)
        {
            auto routingstate = choc::value::createObject("routing");
            routingstate.setMember("slot", i);
            if (mm.rt.routes[i].source)
                routingstate.setMember("source", (int)(mm.rt.routes[i].source->src));
            if (mm.rt.routes[i].sourceVia)
                routingstate.setMember("via", (int)(mm.rt.routes[i].sourceVia->src));
            routingstate.setMember("depth", mm.rt.routes[i].depth);
            if (mm.rt.routes[i].target)
                routingstate.setMember("dest", (int)(mm.rt.routes[i].target->baz));
            if (mm.rt.routes[i].curve)
                routingstate.setMember("curve", mm.rt.routes[i].curve->id);
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
    try
    {
        std::string json((char *)data, (char *)data + sizeInBytes);
        // DBG(json);
        auto state = choc::json::parse(json);
        if (state.hasObjectMember("params"))
        {
            auto params = state["params"];
            auto &pars = granulator.parmetadatas;
            for (int i = 0; i < pars.size(); ++i)
            {
                std::string id = std::to_string(pars[i].id);
                if (params.hasObjectMember(id))
                {
                    float v = params[i].getWithDefault(pars[i].defaultVal);
                    *granulator.idtoparvalptr[pars[i].id] = v;
                }
            }
        }
        suspendProcessing(true);
        if (state.hasObjectMember("filterstates"))
        {
            auto filterstates = state["filterstates"];
            for (int i = 0; i < filterstates.size(); ++i)
            {
                auto filterstate = filterstates[i];
                if (i < 2)
                {
                    sfpp::FilterModel m = (sfpp::FilterModel)filterstate["model"].getWithDefault(0);
                    sfpp::ModelConfig conf;
                    conf.dt = (decltype(conf.dt))filterstate["dt"].getWithDefault(0);
                    conf.st = (decltype(conf.st))filterstate["st"].getWithDefault(0);
                    conf.mt = (decltype(conf.mt))filterstate["mt"].getWithDefault(0);
                    conf.pt = (decltype(conf.pt))filterstate["pt"].getWithDefault(0);
                    ThreadMessage msg;
                    msg.opcode = ThreadMessage::OP_FILTERTYPE;
                    msg.filterindex = i;
                    msg.filtermodel = m;
                    msg.filterconfig = conf;
                    from_gui_fifo.push(msg);
                }
            }
        }
        if (state.hasObjectMember("lfostates"))
        {
            auto lfostates = state["lfostates"];
            for (int i = 0; i < lfostates.size(); ++i)
            {
                auto lfostate = lfostates[i];
                if (i < granulator.modmatrix.numLfos)
                {
                    granulator.modmatrix.lfo_unipolars[i] = lfostate["uni"].getWithDefault(false);
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
                    uint32_t src = rstate["source"].getWithDefault(0);
                    uint32_t srcvia = rstate["via"].getWithDefault(0);
                    int curve = rstate["curve"].getWithDefault(1);
                    float d = rstate["depth"].get<float>();
                    int dest = rstate["dest"].getWithDefault(1);
                    mm.rt.updateRoutingAt(slot, GranulatorModConfig::SourceIdentifier{src},
                                          GranulatorModConfig::SourceIdentifier{srcvia},
                                          GranulatorModConfig::MyCurve{curve},
                                          GranulatorModConfig::TargetIdentifier{dest}, d);
                    if (srcvia == 0)
                        mm.rt.routes[slot].sourceVia = std::nullopt;
                }
            }
            mm.m.prepare(mm.rt, granulator.m_sr, granul_block_size);
        }
    }
    catch (std::exception &ex)
    {
        DBG(ex.what());
    }
    suspendProcessing(false);
    sendExtraStatesToGUI();
}

void AudioPluginAudioProcessor::sendExtraStatesToGUI()
{
    for (auto &p : granulator.parmetadatas)
    {
        ParameterMessage msg;
        msg.id = p.id;
        msg.value = *granulator.idtoparvalptr[msg.id];
        params_to_gui_fifo.push(msg);
    }
    for (int i = 0; i < granulator.modmatrix.numLfos; ++i)
    {
        ThreadMessage msg;
        msg.opcode = ThreadMessage::OP_LFOPARAM;
        msg.lfoindex = i;
        msg.lfounipolar = granulator.modmatrix.lfo_unipolars[i];
        to_gui_fifo.push(msg);
    }
    auto &mm = granulator.modmatrix;
    for (int i = 0; i < GranulatorModConfig::FixedMatrixSize; ++i)
    {
        if (mm.rt.routes[i].source && mm.rt.routes[i].target)
        {
            ThreadMessage msg;
            msg.opcode = ThreadMessage::OP_MODROUTING;
            msg.modslot = i;
            msg.modsource = mm.rt.routes[i].source->src;
            if (mm.rt.routes[i].sourceVia)
                msg.modvia = mm.rt.routes[i].sourceVia->src;
            msg.depth = mm.rt.routes[i].depth;
            msg.moddest = mm.rt.routes[i].target->baz;
            if (mm.rt.routes[i].curve)
                msg.modcurve = mm.rt.routes[i].curve->id;
            auto it = granulator.modRanges.find(msg.moddest);
            if (it != granulator.modRanges.end())
                msg.depth /= it->second;
            to_gui_fifo.push(msg);
        }
    }
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new AudioPluginAudioProcessor(); }
