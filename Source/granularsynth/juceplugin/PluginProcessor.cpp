#include "PluginProcessor.h"
#include "PluginEditor.h"
// #include "Tunings.h"

// .withInput("Input", juce::AudioChannelSet::stereo(), true)

AudioPluginAudioProcessor::AudioPluginAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::ambisonic(3), true))
{
    snapshots.resize(64);
    for (int i = 0; i < 64; ++i)
    {
        try
        {
            auto fname = std::format(
                R"(C:\develop\AudioPluginHost_mk2\audio\granulatorpresets\{}.json)", i + 1);
            if (std::filesystem::exists(fname))
            {
                auto jsontxt = choc::file::loadFileAsString(fname);
                auto state = choc::json::parseValue(jsontxt);
                state.setMember(StateIgnoreStrings::masterVolume, true);
                state.setMember(StateIgnoreStrings::dashboardsettings, true);
                snapshots[i] = state;
            }
        }
        catch (std::exception &ex)
        {
            // DBG(i << " error loading state : " << ex.what());
        }
    }
    directMidiMappings[21] = ToneGranulator::PAR_MAINVOLUME;
    directMidiMappings[22] = ToneGranulator::PAR_DENSITY;
    directMidiMappings[23] = ToneGranulator::PAR_PITCH;
    directMidiMappings[24] = ToneGranulator::PAR_AZIMUTH;
    sliceThread.startThread();
    buffer_adapter.reset(1024);
    from_gui_fifo.reset(1024);
    params_from_gui_fifo.reset(2048);
    params_to_gui_fifo.reset(2048);
    to_gui_fifo.reset(1024);
    dirtyStateParam =
        new juce::AudioParameterFloat({"dirtystateparam", 1}, "Internal parameter", 0.0, 1.0, 0.0);
    addParameter(dirtyStateParam);
    for (int i = 0; i < 16; ++i)
    {
        juce::String id = "MACRO" + juce::String(i);
        juce::String name = "MACRO " + juce::String(i);
        auto par = new juce::AudioParameterFloat({id, 1}, name, -1.0, 1.0, 0.0);
        addParameter(par);
    }
    // to be decided if we want to actually have the full mirrored paraneters
    /*
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
    */
}

AudioPluginAudioProcessor::~AudioPluginAudioProcessor() {}

void AudioPluginAudioProcessor::saveSnapShot(int index, choc::value::ValueView state)
{
    if (index >= 0 && index < snapshots.size())
    {
        std::lock_guard<choc::threading::SpinLock> locker(stateLock);
        snapshots[index] = state;
    }
}

void AudioPluginAudioProcessor::loadSnapShot(int index)
{
    if (index >= 0 && index < snapshots.size())
    {
        if (!snapshots[index].isVoid())
        {
            setState(snapshots[index]);
        }
        granulator.currentSnapShot = index;
    }
}

void AudioPluginAudioProcessor::startRecording()
{
    isRecording = false;
    juce::WavAudioFormat wavformat;
    juce::File outfile{R"(C:\MusicAudio\Dome\granulatorlivebounces)"};
    outfile = outfile.getNonexistentChildFile("recording", ".wav");
    std::unique_ptr<juce::OutputStream> ostream = std::make_unique<juce::FileOutputStream>(outfile);
    auto writer = wavformat.createWriterFor(ostream, juce::AudioFormatWriterOptions()
                                                         .withSampleRate(44100)
                                                         .withBitsPerSample(32)
                                                         .withNumChannels(16));
    if (writer)
    {
        threadedWriter = std::make_unique<juce::AudioFormatWriter::ThreadedWriter>(
            writer.release(), sliceThread, 65536);
        isRecording = true;
    }
}

void AudioPluginAudioProcessor::stopRecording()
{
    isRecording = false;
    threadedWriter = nullptr;
}

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
    DBG("prepareToPlay");
    recordBuffer.setSize(16, samplesPerBlock);
    perfMeasurer.reset(sampleRate, samplesPerBlock);
    workBuffer.resize(samplesPerBlock * 32);
    granulator.prepare(sampleRate, {}, 3, GranulatorVoice::FR_ALLSERIAL, 0.002f, 0.002f);
}

void AudioPluginAudioProcessor::releaseResources() {}

juce::String getBusesLayoutDescription(const juce::AudioProcessor::BusesLayout &layout)
{
    juce::String description;

    auto appendBuses = [&description](const juce::Array<juce::AudioChannelSet> &buses,
                                      juce::String type) {
        description << type << " Buses (" << buses.size() << "):\n";

        if (buses.isEmpty())
        {
            description << "  None\n";
            return;
        }

        for (int i = 0; i < buses.size(); ++i)
        {
            const auto &bus = buses.getReference(i);
            description << "  Bus " << i << ": "
                        << bus.getDescription() // e.g., "Stereo" or "5.1 Surround"
                        << " [" << bus.size() << " channels]\n";
        }
    };

    appendBuses(layout.inputBuses, "Input");
    description << "\n";
    appendBuses(layout.outputBuses, "Output");

    return description;
}

bool AudioPluginAudioProcessor::isBusesLayoutSupported(const BusesLayout &layouts) const
{
    // DBG("Host requested : " << getBusesLayoutDescription(layouts));
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

void AudioPluginAudioProcessor::setStateDirtyHack()
{
    dirtyStateParam->beginChangeGesture();
    dirtyStateParam->setValueNotifyingHost(*dirtyStateParam == 0.0f ? 0.001f : 0.0f);
    dirtyStateParam->endChangeGesture();
}

void AudioPluginAudioProcessor::processBlock(juce::AudioBuffer<float> &buffer,
                                             juce::MidiBuffer &midiMessages)
{
    juce::AudioProcessLoadMeasurer::ScopedTimer perftimer(perfMeasurer, buffer.getNumSamples());
    double cpu_bench_t0 = juce::Time::getMillisecondCounterHiRes();

    {
        std::lock_guard<choc::threading::SpinLock> locker(stateLock);
        if (!pendingState.isVoid())
        {
            double t0 = juce::Time::getMillisecondCounterHiRes();
            changeStateImpl(pendingState);
            pendingState = choc::value::Value();
            sendExtraStatesToGUI();
            double t1 = juce::Time::getMillisecondCounterHiRes();
            DBG("state change took " << t1 - t0 << " milliseconds");
        }
    }
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
            if (msg.getControllerNumber() == 50 && msg.getControllerValue() > 64)
            {
                int snapnumber = granulator.currentSnapShot + 1;
                if (snapnumber >= snapshots.size())
                {
                    snapnumber = 0;
                }
                loadSnapShot(snapnumber);
            }
            auto &mm = granulator.modmatrix;
            uint32_t ccnum = msg.getControllerNumber();
            auto dmit = directMidiMappings.find(ccnum);
            if (dmit != directMidiMappings.end())
            {
                const auto &pmd = granulator.idtoparmetadata[dmit->second];
                float val =
                    juce::jmap<float>(msg.getControllerValue(), 0, 127, pmd->minVal, pmd->maxVal);
                *granulator.idtoparvalptr[dmit->second] = val;
                ParameterMessage msg;
                msg.id = dmit->second;
                msg.value = val;
                params_to_gui_fifo.push(msg);
            }
            auto it = granulator.midiCCMap.find(ccnum);
            if (it != granulator.midiCCMap.end())
            {
                granulator.modSourceValues[it->second] =
                    juce::jmap<float>(msg.getControllerValue(), 0, 127, 0.0, 1.0);
            }
        }
        if (msg.isSustainPedalOn())
        {
            granulator.midiNoteModSource.set_sustain(true);
        }
        if (msg.isSustainPedalOff())
        {
            granulator.midiNoteModSource.set_sustain(false);
        }
        if (msg.isNoteOn())
        {
            granulator.midiNoteModSource.activate_note(msg.getNoteNumber(), msg.getVelocity());
            DBG(granulator.midiNoteModSource.getDebugString());
        }
        if (msg.isNoteOff())
        {
            granulator.midiNoteModSource.deactivate_note(msg.getNoteNumber());
            DBG(granulator.midiNoteModSource.getDebugString());
        }
    }
    bool statechanged = false;
    ThreadMessage msg;
    while (from_gui_fifo.pop(msg))
    {
        if (msg.opcode == ThreadMessage::OP_FILTERTYPE && msg.filterindex >= 0 &&
            msg.filterindex < 2)
        {
            granulator.set_filter(msg.filterindex, msg.insertmainmode, msg.awtype, msg.filtermodel,
                                  msg.filterconfig);
            for (size_t i = 0; i < GranulatorVoice::maxParamsPerInsert; ++i)
            {
                ParameterMessage omsg;
                int parid = ToneGranulator::PAR_INSERTAFIRST + 32 * msg.filterindex + i;
                omsg.id = parid;
                omsg.value = *granulator.idtoparvalptr[parid];
                params_to_gui_fifo.push(omsg);
            }
            statechanged = true;
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
            statechanged = true;
        }
    }
    if (statechanged)
    {
        setStateDirtyHack();
        // updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
    }

    ParameterMessage parmsg;
    while (params_from_gui_fifo.pop(parmsg))
    {
        if (parmsg.id > 0)
        {
            *granulator.idtoparvalptr[parmsg.id] = parmsg.value;
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
    const auto &pars = getParameters();
    for (int i = 0; i < 16; ++i)
    {
        auto rpar = dynamic_cast<juce::AudioParameterFloat *>(pars[i + 1]);
        granulator.modSourceValues[ToneGranulator::MACROSTART + i] = *rpar;
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

    std::array<float, 16> adapter_block;
    std::fill(adapter_block.begin(), adapter_block.end(), 0.0f);
    int procnumoutchs = granulator.num_out_chans;
    while (buffer_adapter.getUsedSlots() < buffer.getNumSamples())
    {
        granulator.process_block(workBuffer.data(), granul_block_size);
        for (int j = 0; j < granul_block_size; ++j)
        {
            for (int i = 0; i < procnumoutchs; ++i)
            {
                adapter_block[i] = workBuffer[j * procnumoutchs + i];
            }
            buffer_adapter.push(adapter_block);
        }
    }

    buffer.clear();
    auto channelDatas = buffer.getArrayOfWritePointers();
    auto recordDatas = recordBuffer.getArrayOfWritePointers();
    if (totalNumOutputChannels == 2)
    {
        const float midGain = 1.414f;
        for (int j = 0; j < buffer.getNumSamples(); ++j)
        {
            buffer_adapter.pop(adapter_block);
            for (int k = 0; k < 16; ++k)
                recordDatas[k][j] = adapter_block[k];
            float m = adapter_block[0] * midGain;
            float s = adapter_block[1];
            channelDatas[0][j] = std::clamp((m + s) * 0.5f, -1.0f, 1.0f);
            channelDatas[1][j] = std::clamp((m - s) * 0.5f, -1.0f, 1.0f);
        }
        if (isRecording && threadedWriter)
        {
            threadedWriter->write(recordDatas, buffer.getNumSamples());
        }
    }
    if (totalNumOutputChannels == 16)
    {
        for (int j = 0; j < buffer.getNumSamples(); ++j)
        {
            buffer_adapter.pop(adapter_block);
            for (int i = 0; i < 16; ++i)
            {
                // float s = workBuffer[j * procnumoutchs + i];
                float s = adapter_block[i];
                channelDatas[i][j] = std::clamp(s, -1.0f, 1.0f);
            }
        }
    }
    jassert(buffer.getNumSamples() > 0);
    double cpu_bench_t1 = juce::Time::getMillisecondCounterHiRes();
    double elapsed_secs = (cpu_bench_t1 - cpu_bench_t0) / 1000.0;
    double max_secs = buffer.getNumSamples() / getSampleRate();
    cpu_load.store(elapsed_secs / max_secs);
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

choc::value::Value AudioPluginAudioProcessor::getState()
{
    auto state = choc::value::createObject("state");
    auto mainparams = choc::value::createObject("params");
    const auto &pmds = granulator.parmetadatas;
    for (int i = 0; i < pmds.size(); ++i)
    {
        std::string id = std::to_string(pmds[i].id);
        float v = *granulator.idtoparvalptr[pmds[i].id];
        mainparams.setMember(id, v);
    }
    state.setMember("params", mainparams);
    state.setMember("gvs_timespan", granulator.gvsettings.timespantoshow);
    auto filterstates = choc::value::createEmptyArray();
    for (int i = 0; i < 2; ++i)
    {
        auto filterstate = choc::value::createObject("filterstate");
        filterstate.setMember("mainmode", (int64_t)granulator.insertsMainModes[i]);
        filterstate.setMember("awtype", (int64_t)granulator.insertsAWTypes[i]);
        filterstate.setMember("model", (int64_t)granulator.filtersModels[i]);
        filterstate.setMember("pt", (int64_t)granulator.filtersConfigs[i].pt);
        filterstate.setMember("st", (int64_t)granulator.filtersConfigs[i].st);
        filterstate.setMember("dt", (int64_t)granulator.filtersConfigs[i].dt);
        filterstate.setMember("mt", (int64_t)granulator.filtersConfigs[i].mt);
        filterstates.addArrayElement(filterstate);
    }
    state.setMember("filterstates", filterstates);

    auto stepseqstates = choc::value::createEmptyArray();
    for (size_t i = 0; i < granulator.stepModSources.size(); ++i)
    {
        auto &ss = granulator.stepModSources[i];
        auto seqstate = choc::value::createObject("seqstate");
        auto seqsteps = choc::value::createEmptyArray();
        for (size_t j = 0; j < 128; ++j)
        {
            seqsteps.addArrayElement(ss.steps[j]);
        }
        seqstate.setMember("steps", seqsteps);
        seqstate.setMember("startstep", ss.loopstartstep);
        seqstate.setMember("looplen", ss.looplen);
        seqstate.setMember("playmode", (int)ss.playmode);
        stepseqstates.addArrayElement(seqstate);
    }
    state.setMember("stepseqstates", stepseqstates);

    auto auxenvstate = granulator.voiceaux_envelope.getState();
    state.setMember("auxenvstate", auxenvstate);

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
    return state;
}

void AudioPluginAudioProcessor::changeStateImpl(choc::value::ValueView state)
{
    if (!state[StateIgnoreStrings::dashboardsettings].getWithDefault(false))
    {
        granulator.gvsettings.timespantoshow = state["gvs_timespan"].getWithDefault(8.0);
    }
    if (state.hasObjectMember("auxenvstate"))
    {
        auto auxenvstate = state["auxenvstate"];
        granulator.set_aux_envelope_interpolation_mode(auxenvstate["interpmode"].getWithDefault(0));
        auto auxenvsteps = auxenvstate["steps"];
        for (int i = 0; i < auxenvsteps.size(); ++i)
        {
            StepModSource::Message msg;
            msg.opcode = StepModSource::Message::OP_SETSTEP;
            msg.fval0 = auxenvsteps[i].getWithDefault(0.0);
            msg.dest = 1000;
            msg.ival0 = i;
            granulator.fifo.push(msg);
        }
    }
    if (state.hasObjectMember("stepseqstates"))
    {
        auto stepseqstate = state["stepseqstates"];
        for (size_t i = 0; i < stepseqstate.size(); ++i)
        {
            if (i >= granulator.stepModSources.size())
                break;
            auto &ss = granulator.stepModSources[i];
            auto seqstate = stepseqstate[(int)i];
            auto steps = seqstate["steps"];
            for (size_t j = 0; j < steps.size(); ++j)
            {
                if (j < 128)
                {
                    StepModSource::Message msg;
                    msg.opcode = StepModSource::Message::OP_SETSTEP;
                    msg.dest = i;
                    msg.ival0 = j;
                    msg.fval0 = steps[(int)j].getWithDefault(0.0f);
                    granulator.fifo.push(msg);
                }
            }
            StepModSource::Message msg;
            msg.opcode = StepModSource::Message::OP_LOOPSTART;
            msg.dest = i;
            msg.ival0 = seqstate["startstep"].getWithDefault(0);
            granulator.fifo.push(msg);
            msg.opcode = StepModSource::Message::OP_LOOPLEN;
            msg.ival0 = seqstate["looplen"].getWithDefault(1);
            granulator.fifo.push(msg);
            msg.opcode = StepModSource::Message::OP_PLAYMODE;
            msg.ival0 = seqstate["playmode"].getWithDefault(0);
            granulator.fifo.push(msg);
        }
    }
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
                int mainmode = filterstate["mainmode"].getWithDefault(0);
                int awtype = filterstate["awtype"].getWithDefault(0);
                granulator.set_filter(i, mainmode, awtype, m, conf);

                ThreadMessage msg;
                msg.opcode = ThreadMessage::OP_FILTERTYPE;
                msg.insertmainmode = mainmode;
                msg.awtype = awtype;
                msg.filterindex = i;
                msg.filtermodel = m;
                msg.filterconfig = conf;
                // from_gui_fifo.push(msg);
            }
        }
    }
    if (state.hasObjectMember("params"))
    {
        auto params = state["params"];
        auto &pars = granulator.parmetadatas;
        bool ignoreMasterVolume = state[StateIgnoreStrings::masterVolume].getWithDefault(false);
        for (int i = 0; i < pars.size(); ++i)
        {
            if (ignoreMasterVolume && pars[i].id == ToneGranulator::PAR_MAINVOLUME)
                continue;
            std::string id = std::to_string(pars[i].id);
            if (params.hasObjectMember(id))
            {
                float v = params[id].getWithDefault(pars[i].defaultVal);
                ParameterMessage parmsg;
                parmsg.id = pars[i].id;
                parmsg.value = v;
                // params_from_gui_fifo.push(parmsg);
                *granulator.idtoparvalptr[pars[i].id] = v;
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

void AudioPluginAudioProcessor::setState(choc::value::ValueView state)
{
    std::lock_guard<choc::threading::SpinLock> locker(stateLock);
    pendingState = state;
}

void AudioPluginAudioProcessor::getStateInformation(juce::MemoryBlock &destData)
{
    auto state = getState();
    auto sdata = state.serialise();
    destData.append(sdata.data.data(), sdata.data.size());
}

void AudioPluginAudioProcessor::setStateInformation(const void *data, int sizeInBytes)
{
    DBG("setStateInformation");
    try
    {
        if (sizeInBytes < 1)
            return;
        choc::value::InputData idata{(const uint8_t *)data, (const uint8_t *)data + sizeInBytes};
        auto state = choc::value::Value::deserialise(idata);
        setState(state.getView());
    }
    catch (std::exception &ex)
    {
        DBG("tonegranulator error restoring state : " << ex.what());
    }

    sendExtraStatesToGUI();
}

void AudioPluginAudioProcessor::sendExtraStatesToGUI()
{
    ThreadMessage msg{ThreadMessage::OP_STEPSEQUENCER};
    to_gui_fifo.push(msg);
    {
        ThreadMessage msg;
        msg.opcode = ThreadMessage::OP_FILTERTYPE;
        for (int i = 0; i < granulator.filtersConfigs.size(); ++i)
        {
            msg.filterindex = i;
            msg.insertmainmode = granulator.insertsMainModes[i];
            msg.awtype = granulator.insertsAWTypes[i];
            msg.filtermodel = granulator.filtersModels[i];
            msg.filterconfig = granulator.filtersConfigs[i];
            to_gui_fifo.push(msg);
        }
    }
    for (auto &p : granulator.parmetadatas)
    {
        ParameterMessage msg;
        msg.id = p.id;
        msg.value = *granulator.idtoparvalptr[msg.id];
        params_to_gui_fifo.push(msg);
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
