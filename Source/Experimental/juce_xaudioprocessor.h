#pragma once

#include "xaudioprocessor.h"
#include "JuceHeader.h"

class JucePluginWrapper : public xenakios::XAudioProcessor,
                          public juce::AudioPlayHead,
                          public juce::AudioProcessorParameter::Listener
{
  public:
    std::vector<clap_param_info> m_param_infos;
    std::unique_ptr<juce::AudioPluginInstance> m_internal;
    juce::AudioBuffer<float> m_work_buf;
    juce::MidiBuffer m_midi_buffer;
    clap_plugin_descriptor m_plug_descriptor;
    JucePluginWrapper(juce::String plugfile, int subpluginindex = 0)
    {
        memset(&m_plug_descriptor, 0, sizeof(clap_plugin_descriptor));
        juce::AudioPluginFormatManager plugmana;
        plugmana.addDefaultFormats();
        for (auto &e : plugmana.getFormats())
        {
            std::cout << e->getName() << "\n";
        }
        juce::KnownPluginList klist;
        juce::OwnedArray<juce::PluginDescription> typesFound;
        juce::VST3PluginFormat f;
        klist.scanAndAddFile(plugfile, true, typesFound, f);
        for (auto &e : typesFound)
        {
            // std::cout << e->name << "\n";
        }
        if (typesFound.size() > 0)
        {
            juce::String err;
            m_internal =
                plugmana.createPluginInstance(*typesFound[subpluginindex], 44100, 512, err);
            if (!m_internal)
            {
                std::cout << err << "\n";
            }
            else
            {
                updateParameterInfos();
                m_desc = m_internal->getPluginDescription();
                clap_id_string = "vst3." + std::to_string(subpluginindex) + "." +
                                 std::to_string(m_desc.uniqueId) + "." + m_desc.name.toStdString();
            }
        }
    }
    juce::PluginDescription m_desc;
    std::string clap_id_string;
    bool getDescriptor(clap_plugin_descriptor_t *dec) const override
    {
        memset(dec, 0, sizeof(clap_plugin_descriptor));
        if (m_internal)
        {
            dec->name = m_internal->getName().getCharPointer();
            dec->vendor = m_desc.manufacturerName.getCharPointer();
            dec->id = clap_id_string.c_str();
            return true;
        }
        return false;
    }
    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override
    {
        return m_transport_pos;
    }
    choc::fifo::SingleReaderSingleWriterFIFO<xenakios::CrossThreadMessage> m_param_messages_fifo;
    void parameterValueChanged(int parameterIndex, float newValue) override
    {
        m_param_messages_fifo.push({(clap_id)parameterIndex, CLAP_EVENT_PARAM_VALUE, newValue});
    }
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override
    {
        int et = CLAP_EVENT_PARAM_GESTURE_BEGIN;
        if (!gestureIsStarting)
            et = CLAP_EVENT_PARAM_GESTURE_END;
        m_param_messages_fifo.push({(clap_id)parameterIndex, et, 0.0});
    }
    juce::Optional<juce::AudioPlayHead::PositionInfo> m_transport_pos;
    void updateParameterInfos()
    {
        m_param_infos.clear();
        auto &pars = m_internal->getParameters();
        // We should probably create the clap parameter
        // id in some other way that makes them non-contiguous...
        clap_id parid = 0;
        for (auto &par : pars)
        {
            auto pinfo = makeParamInfo(parid, par->getName(100), 0.0, 1.0, par->getDefaultValue(),
                                       CLAP_PARAM_IS_AUTOMATABLE);
            m_param_infos.push_back(pinfo);
            par->addListener(this);
            // std::cout << parid << "\t" << par->getName(100) << "\t" << par->getDefaultValue()
            //           << "\n";
            ++parid;
        }
    }
    bool activate(double sampleRate, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override
    {
        jassert(m_internal);
        m_internal->setPlayHead(this);

        m_internal->enableAllBuses();
        m_internal->setPlayConfigDetails(2, 2, sampleRate, maxFrameCount);
        int maxchans_needed = std::max(m_internal->getTotalNumInputChannels(),
                                       m_internal->getTotalNumOutputChannels());
        m_work_buf.setSize(maxchans_needed, maxFrameCount);
        m_work_buf.clear();
        m_internal->prepareToPlay(sampleRate, maxFrameCount);

        return true;
    }
    uint32_t paramsCount() const noexcept override { return m_param_infos.size(); }
    bool paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept override
    {
        *info = m_param_infos[paramIndex];
        return true;
    }

    clap_process_status process(const clap_process *process) noexcept override
    {
        jassert(m_internal);
        int maxchans_needed = std::max(m_internal->getTotalNumInputChannels(),
                                       m_internal->getTotalNumOutputChannels());
        if (maxchans_needed > m_work_buf.getNumChannels())
        {
            m_work_buf.setSize(maxchans_needed, m_work_buf.getNumSamples());
            m_work_buf.clear();
            std::cout << "Had to adjust work buffer channels size for Juce plugin "
                      << m_internal->getName() << " to " << maxchans_needed << "\n";
        }

        if (process->transport)
        {
            m_transport_pos = juce::AudioPlayHead::PositionInfo();
            m_transport_pos->setBpm(process->transport->tempo);
            m_transport_pos->setTimeInSamples(process->transport->song_pos_seconds *
                                              m_internal->getSampleRate());
        }

        m_midi_buffer.clear();
        auto numInChans = 0;
        if (process->audio_inputs_count > 0)
            numInChans = process->audio_inputs[0].channel_count;
        auto numOutChans = 0;
        if (process->audio_outputs_count > 0)
            numOutChans = process->audio_outputs[0].channel_count;
        int frames = process->frames_count;
        auto inEvents = process->in_events;
        auto &pars = m_internal->getParameters();
        uint32_t basechunk = 64;
        uint32_t smp = 0;

        const clap_event_header_t *nextEvent{nullptr};
        uint32_t nextEventIndex{0};
        auto sz = inEvents->size(inEvents);
        if (sz != 0)
        {
            nextEvent = inEvents->get(inEvents, nextEventIndex);
        }

        while (smp < frames)
        {
            uint32_t chunk = std::min(basechunk, frames - smp);
            m_midi_buffer.clear();
            while (nextEvent && nextEvent->time < smp + chunk)
            {
                auto ev = inEvents->get(inEvents, nextEventIndex);
                if (ev->space_id == CLAP_CORE_EVENT_SPACE_ID)
                {
                    if (ev->type == CLAP_EVENT_PARAM_VALUE)
                    {
                        auto pvev = reinterpret_cast<const clap_event_param_value *>(ev);
                        if (pvev->param_id >= 0 && pvev->param_id < pars.size())
                        {
                            pars[pvev->param_id]->setValue(pvev->value);
                        }
                    }
                    if (m_internal->acceptsMidi())
                    {
                        // we could do better with the event time stamps, but this shall suffice for
                        // now...
                        if (ev->type == CLAP_EVENT_MIDI)
                        {
                            auto mev = reinterpret_cast<const clap_event_midi *>(ev);
                            juce::MidiMessage midimsg(mev->data[0], mev->data[1], mev->data[2], 0);
                            m_midi_buffer.addEvent(midimsg, smp);
                        }
                        auto mev = reinterpret_cast<const clap_event_note *>(ev);
                        if (ev->type == CLAP_EVENT_NOTE_ON)
                        {
                            m_midi_buffer.addEvent(juce::MidiMessage::noteOn(mev->channel + 1,
                                                                             mev->key,
                                                                             (float)mev->velocity),
                                                   smp);
                        }
                        else if (ev->type == CLAP_EVENT_NOTE_OFF ||
                                 ev->type == CLAP_EVENT_NOTE_CHOKE)
                        {
                            m_midi_buffer.addEvent(juce::MidiMessage::noteOff(mev->channel + 1,
                                                                              mev->key,
                                                                              (float)mev->velocity),
                                                   smp);
                        }
                    }
                }
                nextEventIndex++;
                if (nextEventIndex >= sz)
                    nextEvent = nullptr;
                else
                    nextEvent = inEvents->get(inEvents, nextEventIndex);
            }
            m_work_buf.setSize(m_work_buf.getNumChannels(), chunk, false, false, true);
            auto workbufptrs = m_work_buf.getArrayOfWritePointers();
            for (int i = 0; i < 2; ++i)
            {
                for (int j = 0; j < chunk; ++j)
                {
                    workbufptrs[i][j] = process->audio_inputs[0].data32[i][smp + j];
                }
            }
            m_internal->processBlock(m_work_buf, m_midi_buffer);
            for (int i = 0; i < 2; ++i)
            {
                for (int j = 0; j < chunk; ++j)
                {
                    process->audio_outputs[0].data32[i][smp + j] = workbufptrs[i][j];
                }
            }
            smp += chunk;
        }
        xenakios::CrossThreadMessage msg;
        while (m_param_messages_fifo.pop(msg))
        {
            if (equalsToAny(msg.eventType, CLAP_EVENT_PARAM_GESTURE_BEGIN,
                            CLAP_EVENT_PARAM_GESTURE_END))
            {
                clap_event_param_gesture ge;
                ge.header.flags = 0;
                ge.header.size = sizeof(clap_event_param_gesture);
                ge.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                ge.header.time = 0;
                ge.header.type = msg.eventType;
                ge.param_id = msg.paramId;
                process->out_events->try_push(process->out_events, (clap_event_header *)&ge);
            }
            if (msg.eventType == CLAP_EVENT_PARAM_VALUE)
            {
                clap_event_param_value pe;
                pe.header.flags = 0;
                pe.header.size = sizeof(clap_event_param_value);
                pe.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                pe.header.time = 0;
                pe.header.type = CLAP_EVENT_PARAM_VALUE;
                pe.channel = -1;
                pe.cookie = nullptr;
                pe.key = -1;
                pe.note_id = -1;
                pe.port_index = -1;
                pe.value = msg.value;
                pe.param_id = msg.paramId;
                process->out_events->try_push(process->out_events, (clap_event_header *)&pe);
            }
        }
        return CLAP_PROCESS_CONTINUE;
    }

    bool stateSave(const clap_ostream *stream) noexcept override
    {
        juce::MemoryBlock block;
        m_internal->getStateInformation(block);
        stream->write(stream, block.getData(), block.getSize());
        return true;
    }
    bool stateLoad(const clap_istream *stream) noexcept override
    {
        juce::MemoryBlock block;
        constexpr size_t bufsize = 4096;
        unsigned char buf[bufsize];
        memset(buf, 0, bufsize);
        while (true)
        {
            int read = stream->read(stream, buf, bufsize);
            if (read == 0)
                break;
            block.append(buf, read);
        }
        if (block.getSize() > 0)
        {
            // here we have to trust the hosted plugin does this thread safely...
            m_internal->setStateInformation(block.getData(), block.getSize());
            return true;
        }
        return false;
    }

    uint32_t notePortsCount(bool isInput) const noexcept override
    {
        if (isInput && m_internal->acceptsMidi())
            return 1;
        return 0;
    }
    bool notePortsInfo(uint32_t, bool isInput, clap_note_port_info *info) const noexcept override
    {
        if (m_internal->acceptsMidi() && isInput)
        {
            info->id = 1000;
            strcpy_s(info->name, "JUCE Wrapper MIDI input");
            info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
            info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
            return true;
        }
        if (m_internal->producesMidi() && !isInput)
        {
            info->id = 2000;
            strcpy_s(info->name, "JUCE Wrapper MIDI output");
            info->supported_dialects = CLAP_NOTE_DIALECT_MIDI;
            info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
            return true;
        }
        return false;
    }

    uint32_t audioPortsCount(bool isInput) const noexcept override
    {
        if (m_internal)
            return m_internal->getBusCount(isInput);
        return 0;
    }
    bool audioPortsInfo(uint32_t index, bool isInput,
                        clap_audio_port_info *info) const noexcept override
    {
        if (m_internal)
        {
            info->channel_count = m_internal->getBus(isInput, index)->getNumberOfChannels();
            info->flags = 0;
            info->in_place_pair = CLAP_INVALID_ID;
            if (isInput)
            {
                info->id = 14387;
                info->port_type = "";
                strcpy_s(info->name, "Juce wrapper audio input");
            }
            else
            {
                info->id = 98765;
                info->port_type = "";
                strcpy_s(info->name, "Juce wrapper audio output");
            }

            return true;
        }

        return false;
    }

    bool implementsGui() const noexcept override { return true; }
    // virtual bool guiIsApiSupported(const char *api, bool isFloating) noexcept { return false; }
    // virtual bool guiGetPreferredApi(const char **api, bool *is_floating) noexcept { return false;
    // }

    // virtual bool guiSetScale(double scale) noexcept { return false; }
    bool guiCreate(const char *api, bool isFloating) noexcept override
    {
        if (!m_internal)
            return false;
        if (m_internal->hasEditor())
        {
            m_editor = m_internal->createEditor();
            return true;
        }
        else
        {
            // the generic editor is kind of sucky, but will have to do for now
            m_editor = new juce::GenericAudioProcessorEditor(m_internal.get());
            return true;
        }
        return false;
    }
    void guiDestroy() noexcept override
    {
        delete m_editor;
        m_editor = nullptr;
    }
    bool guiShow() noexcept override
    {
        if (!m_editor)
            return false;
        return true;
    }
    bool guiHide() noexcept override { return false; }
    virtual bool guiGetSize(uint32_t *width, uint32_t *height) noexcept override
    {
        if (!m_editor)
            return false;
        *width = m_editor->getWidth();
        *height = m_editor->getHeight();
        return true;
    }
    // virtual bool guiCanResize() const noexcept { return false; }
    // virtual bool guiGetResizeHints(clap_gui_resize_hints_t *hints) noexcept { return false; }
    // virtual bool guiAdjustSize(uint32_t *width, uint32_t *height) noexcept
    //{
    //     return guiGetSize(width, height);
    // }
    bool guiSetSize(uint32_t width, uint32_t height) noexcept override
    {
        if (!m_editor)
            return false;
        m_editor->setSize(width, height);
        return true;
    }
    // virtual void guiSuggestTitle(const char *title) noexcept {}
    bool guiSetParent(const clap_window *window) noexcept override
    {
        if (!m_editor || std::string(window->api) != "JUCECOMPONENT")
            return false;
        auto parent = (juce::Component *)window->ptr;
        parent->addAndMakeVisible(*m_editor);
        return true;
    }
    juce::AudioProcessorEditor *m_editor = nullptr;
};
