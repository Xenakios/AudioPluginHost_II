#include <iostream>
#include "testprocessors.h"
#include <memory>
#include <vector>

#include <clap/helpers/event-list.hh>
#include "clap_xaudioprocessor.h"
#include "juce_xaudioprocessor.h"
#include "fileplayer_xaudioprocessor.h"
#include "xap_utils.h"
#include "xap_notegenerator.h"
#include "xap_modulator.h"
#include "xclapeventlist.h"
#include "noise-plethora/plugins/NoisePlethoraPlugin.hpp"
#include "noise-plethora/plugins/Banks.hpp"
#include "xaudiograph.h"
#include <sst/basic-blocks/dsp/FastMath.h>

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

class XAPPlayer : public juce::AudioIODeviceCallback
{
  public:
    XAPPlayer(xenakios::XAudioProcessor &procToPlay, int subblocksize = 64)
        : m_proc(procToPlay), m_subblocksize(subblocksize)
    {
    }
    void audioDeviceIOCallbackWithContext(const float *const *inputChannelData,
                                          int numInputChannels, float *const *outputChannelData,
                                          int numOutputChannels, int numSamples,
                                          const AudioIODeviceCallbackContext &context) override
    {
        jassert(m_output_ring_buf.size() >= numSamples * numOutputChannels);
        clap_process process;
        juce::zerostruct(process);
        clap_audio_buffer cab[1];
        cab[0].channel_count = 2;
        cab[0].constant_mask = 0;
        cab[0].data64 = nullptr;
        cab[0].latency = 0;
        cab[0].data32 = (float **)outputChannelData;
        process.audio_outputs_count = 1;
        process.audio_outputs = cab;
        process.frames_count = m_subblocksize;
        while (m_output_ring_buf.available() < numSamples * numOutputChannels)
        {
            auto err = m_proc.process(&process);
            if (err == CLAP_PROCESS_ERROR)
            {
                for (int i = 0; i < numOutputChannels; ++i)
                {
                    for (int j = 0; j < m_subblocksize; ++j)
                    {
                        // outputChannelData[i][j] = 0.0f;
                        m_output_ring_buf.push(0.0f);
                    }
                }
            }
            else
            {
                for (int j = 0; j < m_subblocksize; ++j)
                {
                    for (int i = 0; i < numOutputChannels; ++i)
                    {
                        m_output_ring_buf.push(outputChannelData[i][j]);
                    }
                }
            }
        }
        for (int j = 0; j < numSamples; ++j)
        {
            for (int i = 0; i < numOutputChannels; ++i)
            {
                outputChannelData[i][j] = m_output_ring_buf.pop();
            }
        }
        return;
        for (int i = 0; i < numOutputChannels; ++i)
        {
            for (int j = 0; j < numSamples; ++j)
            {
                outputChannelData[i][j] *= 0.01;
            }
        }
    }
    void audioDeviceAboutToStart(AudioIODevice *device) override
    {
        m_proc.activate(device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples(),
                        device->getCurrentBufferSizeSamples());
    }
    void audioDeviceStopped() override {}

  private:
    xenakios::XAudioProcessor &m_proc;
    SimpleRingBuffer<float, 2048> m_input_ring_buf;
    SimpleRingBuffer<float, 2048> m_output_ring_buf;
    int m_subblocksize = 0;
};

inline void test_graph_processor_realtime()
{
    auto g = std::make_unique<XAPGraph>();
    juce::AudioDeviceManager man;
    man.initialiseWithDefaultDevices(0, 2);
    XAPPlayer xplayer(*g);
    man.addAudioCallback(&xplayer);
    double t0 = juce::Time::getMillisecondCounterHiRes();
    while (true)
    {
        double t1 = juce::Time::getMillisecondCounterHiRes();
        if (t1 - t0 > 5000)
            break;
        std::cout << t1 - t0 << "\t" << man.getCpuUsage() * 100.0 << "% CPU" << std::endl;
        juce::Thread::sleep(500);
    }
}

inline void test_graph_processor_offline()
{
    auto g = std::make_unique<XAPGraph>();
    g->addProcessorAsNode(std::make_unique<ToneProcessorTest>(), "Tone 1");
    g->addProcessorAsNode(std::make_unique<ToneProcessorTest>(), "Tone 2");
    g->addProcessorAsNode(std::make_unique<ClapPluginFormatProcessor>(
                              R"(C:\Program Files\Common Files\CLAP\Conduit.clap)", 3),
                          "Ring Mod");
    std::string pathprefix = R"(C:\Program Files\Common Files\)";
    g->addProcessorAsNode(std::make_unique<ClapPluginFormatProcessor>(
                              pathprefix + R"(CLAP\Surge Synth Team\Surge XT.clap)", 0),
                          "Surge XT 1");
    // g->connectAudio("Tone 1", 0, 0, "Ring Mod", 0, 0);
    // g->connectAudio("Tone 1", 0, 0, "Ring Mod", 0, 1);
    // g->connectAudio("Tone 2", 0, 0, "Ring Mod", 1, 0);
    // g->connectAudio("Tone 2", 0, 0, "Ring Mod", 1, 1);
    g->addProcessorAsNode(std::make_unique<ClapEventSequencerProcessor>(3), "Note Gen");
    connectEventPorts(g->findNodeByName("Note Gen"), 0, g->findNodeByName("Surge XT 1"), 0);
    g->outputNodeId = "Surge XT 1";
    double sr = 44100;
    int blocksize = 128;
    std::vector<float> procbuf(blocksize * 2);
    clap_audio_buffer cab[1];
    cab[0].channel_count = 2;
    cab[0].constant_mask = 0;
    cab[0].data64 = nullptr;
    cab[0].latency = 0;
    float *chandatas[2] = {&procbuf[blocksize * 0], &procbuf[blocksize * 1]};
    cab[0].data32 = chandatas;
    clap_process process;
    memset(&process, 0, sizeof(clap_process));
    process.audio_outputs_count = 1;
    process.audio_outputs = cab;
    process.frames_count = blocksize;
    g->activate(sr, blocksize, blocksize);

    g->findProcessor("Tone 1")->enqueueParameterChange(
        xenakios::CrossThreadMessage().asParamChange(ToneProcessorTest::ParamIds::Pitch, 72.0));
    g->findProcessor("Tone 1")->enqueueParameterChange(
        {(clap_id)ToneProcessorTest::ParamIds::Distortion, CLAP_EVENT_PARAM_VALUE, 0.1});
    g->findProcessor("Tone 2")->enqueueParameterChange(
        {(clap_id)ToneProcessorTest::ParamIds::Pitch, CLAP_EVENT_PARAM_VALUE, 35.5});
    g->findProcessor("Tone 2")->enqueueParameterChange(
        xenakios::CrossThreadMessage().asParamChange(ToneProcessorTest::ParamIds::Distortion, 0.0));
    // ids taken from Conduit source, 842 ring mod mix level, 712 other source is internal
    // osc/sidechain
    g->findNodeByName("Ring Mod")
        ->processor->enqueueParameterChange({(clap_id)842, CLAP_EVENT_PARAM_VALUE, 1.0});
    g->findNodeByName("Ring Mod")
        ->processor->enqueueParameterChange({(clap_id)712, CLAP_EVENT_PARAM_VALUE, 1.0});
    int outlen = 120 * sr;
    int outcounter = 0;
    juce::File outfile(
        R"(C:\develop\AudioPluginHost_mk2\Source\Experimental\audio\graph_out_05.wav)");
    outfile.deleteFile();
    auto ostream = outfile.createOutputStream();
    WavAudioFormat wav;
    auto writer = wav.createWriterFor(ostream.release(), sr, 2, 32, {}, 0);
    while (outcounter < outlen)
    {
        g->process(&process);
        writer->writeFromFloatArrays(chandatas, 2, blocksize);
        outcounter += blocksize;
    }
    delete writer;
}

struct PluginContentComponent : public juce::Component
{
    xenakios::XAudioProcessor &m_proc;
    juce::Label infoLabel;
    PluginContentComponent(xenakios::XAudioProcessor &proc) : m_proc(proc)
    {
        addAndMakeVisible(infoLabel);
        infoLabel.setText("Info label", juce::dontSendNotification);
        juce::MessageManager::callAsync([this]() {
            clap_plugin_descriptor desc;
            auto m_test_proc = &m_proc;
            if (m_test_proc->getDescriptor(&desc))
            {
                if (getParentComponent())
                    getParentComponent()->setName(juce::String(desc.vendor) + " : " + desc.name);
            }
            m_test_proc->guiCreate("", false);
            clap_window win;
            win.api = "JUCECOMPONENT";
            win.ptr = this;

            m_test_proc->guiSetParent(&win);

            uint32 w = 0;
            uint32_t h = 0;
            if (m_test_proc->guiGetSize(&w, &h))
            {
                setSize(w, h + m_info_area_margin);
            }
            m_test_proc->guiShow();
            m_test_proc->OnPluginRequestedResize = [this](uint32_t neww, uint32_t newh) {
                // setSize is a synchronous call, so we can toggle the flag like this(?)
                m_plugin_requested_resize = true;
                setSize(neww, newh + m_info_area_margin);
                m_plugin_requested_resize = false;
            };
        });
        setSize(10, 10);
    }
    void resized() override
    {
        auto m_test_proc = &m_proc;
        uint32_t w = 0;
        uint32_t h = 0;
        m_test_proc->guiGetSize(&w, &h);
        infoLabel.setBounds(0, getHeight() - m_info_area_margin, getWidth(), m_info_area_margin);

        if (!m_plugin_requested_resize && m_test_proc->guiCanResize())
            m_test_proc->guiSetSize(getWidth(), getHeight() - m_info_area_margin);
    }

    int m_info_area_margin = 25;
    bool m_plugin_requested_resize = false;
};

class XapWindow : public juce::DocumentWindow
{
  public:
    xenakios::XAudioProcessor &m_proc;
    PluginContentComponent m_content;
    XapWindow(xenakios::XAudioProcessor &proc)
        : juce::DocumentWindow("XAP", juce::Colours::darkgrey, 4, true), m_proc(proc),
          m_content(proc)
    {
        setUsingNativeTitleBar(true);
        setVisible(true);
        // setAlwaysOnTop(true);
        setWantsKeyboardFocus(true);
        setContentNonOwned(&m_content, true);
        setResizable(true, false);
    }
    ~XapWindow() override
    {
        m_proc.guiHide();
        m_proc.guiDestroy();
    }
    void resized() override { m_content.setBounds(0, 0, getWidth(), getHeight()); }
    void closeButtonPressed() override
    {
        if (OnRequestDelete)
        {
            OnRequestDelete(this);
        }
    }
    std::function<void(XapWindow *)> OnRequestDelete;
    bool m_plugin_requested_resize = false;
    int m_info_area_margin = 25;
    juce::Label m_info_label;
};

class MainComponent : public juce::Component, public juce::Timer
{
  public:
    std::vector<std::unique_ptr<XapWindow>> m_xap_windows;
    juce::AudioDeviceManager m_aman;
    std::unique_ptr<XAPGraph> m_graph;
    std::unique_ptr<XAPPlayer> m_player;
    int m_info_area_margin = 25;
    juce::Label m_infolabel;
    juce::ComboBox m_mod_rout_combo;
    void timerCallback() override
    {
        int usage = m_aman.getCpuUsage() * 100.0;
        m_infolabel.setText("CPU " + juce::String(usage) + "%", juce::dontSendNotification);
    }
    std::string pathprefix = R"(C:\Program Files\Common Files\)";
    void addNoteGeneratorTestNodes()
    {
        m_graph->addProcessorAsNode(std::make_unique<ClapEventSequencerProcessor>(2), "Note Gen");
        // m_graph->addProcessorAsNode(std::make_unique<ClapPluginFormatProcessor>(
        //                                 pathprefix + R"(CLAP\Surge Synth Team\Surge XT.clap)",
        //                                 0),
        //                             "Surge XT 1");

        m_graph->addProcessorAsNode(
            std::make_unique<ClapPluginFormatProcessor>(pathprefix + "/CLAP/Conduit.clap", 0),
            "Surge XT 1");

        // m_graph->addProcessorAsNode(std::make_unique<ClapPluginFormatProcessor>(
        //                                pathprefix + R"(CLAP\Surge Synth Team\Surge XT.clap)", 0),
        //                            "Surge XT 2");
        m_graph->addProcessorAsNode(std::make_unique<GainProcessorTest>(), "Main");
        connectEventPorts(m_graph->findNodeByName("Note Gen"), 0,
                          m_graph->findNodeByName("Surge XT 1"), 0);
        // connectEventPorts(m_graph->findNodeByName("Note Gen"), 1,
        //                  m_graph->findNodeByName("Surge XT 2"), 0);

        m_graph->connectAudio("Surge XT 1", 0, 0, "Main", 0, 0);
        m_graph->connectAudio("Surge XT 1", 0, 1, "Main", 0, 1);
        // m_graph->connectAudio("Surge XT 2", 0, 0, "Main", 0, 0);
        // m_graph->connectAudio("Surge XT 2", 0, 1, "Main", 0, 1);
        m_graph->outputNodeId = "Main";
    }
    void addFilePlayerTestNodes()
    {
        m_graph->addProcessorAsNode(std::make_unique<FilePlayerProcessor>(), "File 1");
        auto fp = m_graph->findProcessorAndCast<FilePlayerProcessor>("File 1");
        m_graph->addProcessorAsNode(std::make_unique<GainProcessorTest>(), "Main");
        m_graph->connectAudio("File 1", 0, 0, "Main", 0, 0);
        m_graph->connectAudio("File 1", 0, 1, "Main", 0, 1);
        m_graph->outputNodeId = "Main";
    }
    void addConduitTestNodes()
    {
        m_graph->addProcessorAsNode(std::make_unique<FilePlayerProcessor>(), "File 1");
        auto fp = m_graph->findProcessorAndCast<FilePlayerProcessor>("File 1");
        m_graph->addProcessorAsNode(
            std::make_unique<ClapPluginFormatProcessor>(pathprefix + "CLAP/Conduit.clap", 3),
            "Main");
        m_graph->connectAudio("File 1", 0, 0, "Main", 0, 0);
        m_graph->connectAudio("File 1", 0, 1, "Main", 0, 1);
        m_graph->outputNodeId = "Main";
    }
    void addToneGeneratorTestNodes()
    {
        m_graph->addProcessorAsNode(std::make_unique<ToneProcessorTest>(), "Tone 1");
        m_graph->addProcessorAsNode(std::make_unique<GainProcessorTest>(), "Main");
        m_graph->addProcessorAsNode(std::make_unique<ModulatorSource>(1, 1.0), "LFO 1");
        m_graph->addProcessorAsNode(std::make_unique<ModulatorSource>(1, 4.0), "LFO 2");
        m_graph->connectAudio("Tone 1", 0, 0, "Main", 0, 0);
        m_graph->connectAudio("Tone 1", 0, 0, "Main", 0, 1);
        m_graph->connectModulationByNames("LFO 1", 0, "Tone 1",
                                          (clap_id)ToneProcessorTest::ParamIds::Pitch, false, 6.0);
        m_graph->connectModulationByNames("LFO 2", 0, "Tone 1",
                                          (clap_id)ToneProcessorTest::ParamIds::Pitch, false, 1.0);
        m_graph->outputNodeId = "Main";
    }
    MainComponent()
    {
        addAndMakeVisible(m_infolabel);
        addAndMakeVisible(m_mod_rout_combo);

        startTimerHz(10);

        m_graph = std::make_unique<XAPGraph>();
        // addNoteGeneratorTestNodes();
        // addFilePlayerTestNodes();
        // addToneGeneratorTestNodes();
        addConduitTestNodes();
        juce::Random rng{7};
        for (auto &n : m_graph->proc_nodes)
        {
            // continue;
            m_xap_windows.emplace_back(std::make_unique<XapWindow>(*n->processor));
            m_xap_windows.back()->setTopLeftPosition(rng.nextInt({10, 600}),
                                                     rng.nextInt({100, 400}));
            m_xap_windows.back()->OnRequestDelete = [this](XapWindow *w) {
                for (int i = 0; i < m_xap_windows.size(); ++i)
                {
                    if (m_xap_windows[i].get() == w)
                    {
                        m_xap_windows.erase(m_xap_windows.begin() + i);
                        break;
                    }
                }
            };
        }

        m_player = std::make_unique<XAPPlayer>(*m_graph);
        m_aman.initialiseWithDefaultDevices(0, 2);
        m_aman.addAudioCallback(m_player.get());
        initModBox();
        setSize(500, 100);
    }
    ~MainComponent() override
    {
        m_xap_windows.clear();
        m_aman.removeAudioCallback(m_player.get());
    }
    void initModBox()
    {
        auto valhnode = m_graph->findNodeByName("Valhalla");
        if (!valhnode)
            return;
        auto valh = valhnode->processor.get();
        for (int i = 0; i < valh->paramsCount(); ++i)
        {
            m_mod_rout_combo.addItem(juce::String(i), i + 1);
        }
        m_mod_rout_combo.setSelectedItemIndex(0, juce::dontSendNotification);
        m_mod_rout_combo.onChange = [this, valhnode]() {
            int destpar = m_mod_rout_combo.getSelectedItemIndex();
            for (auto &conn : valhnode->inputConnections)
            {
                if (conn.type == XAPNode::ConnectionType::Modulation)
                {
                    conn.destinationParameter = destpar;
                    break;
                }
            }
        };
    }
    bool m_plugin_requested_resize = false;
    void resized() override
    {
        m_infolabel.setBounds(0, 0, getWidth(), 25);
        m_mod_rout_combo.setBounds(0, m_infolabel.getBottom() + 1, 50, 25);
    }
};

class GuiAppApplication : public juce::JUCEApplication
{
  public:
    //==============================================================================
    GuiAppApplication() {}

    const juce::String getApplicationName() override { return "FOO"; }
    const juce::String getApplicationVersion() override { return "0.0.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    //==============================================================================
    void initialise(const juce::String &commandLine) override
    {
        juce::ignoreUnused(commandLine);
        mainWindow.reset(new MainWindow(getApplicationName()));
    }

    void shutdown() override
    {
        mainWindow = nullptr; // (deletes our window)
    }

    //==============================================================================
    void systemRequestedQuit() override { quit(); }

    void anotherInstanceStarted(const juce::String &commandLine) override
    {
        juce::ignoreUnused(commandLine);
    }

    class MainWindow : public juce::DocumentWindow
    {

      public:
        explicit MainWindow(juce::String name)
            : DocumentWindow(name,
                             juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                                 ResizableWindow::backgroundColourId),
                             DocumentWindow::allButtons)
        {

            setUsingNativeTitleBar(true);

            setContentOwned(new MainComponent, true);
            setResizable(true, true);
            setTopLeftPosition(50, 50);

            setVisible(true);
        }

        void closeButtonPressed() override
        {

            JUCEApplication::getInstance()->systemRequestedQuit();
        }

      private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

  private:
    std::unique_ptr<MainWindow> mainWindow;
};

#define TESTJUCEGUI 0

#if TESTJUCEGUI

START_JUCE_APPLICATION(GuiAppApplication)

#else

inline clap_event_note makeClapNote(int samplepos, int etype, int port, int chan, int key,
                                    int noteid, double velo)
{
    clap_event_note en;
    en.header.flags = 0;
    en.header.size = sizeof(clap_event_note);
    en.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    en.header.time = samplepos;
    en.header.type = etype;
    en.channel = chan;
    en.key = key;
    en.note_id = noteid;
    en.port_index = port;
    en.velocity = velo;
    return en;
}

inline bool isClapNoteEventType(uint16_t et)
{
    return et >= CLAP_EVENT_NOTE_ON && et <= CLAP_EVENT_NOTE_END;
}

// just a raw unchecked cast, use at your own peril
template <typename EventType> inline const EventType *clapCast(const clap_event_header *e)
{
    return reinterpret_cast<const EventType *>(e);
}

// This has not been benchmarked etc, so depending on intended use pattern,
// might not want to always use this because of the runtime
// conditional checking involved. If new event types are added to Clap, this needs to
// be updated...
// Should probably also use switch case in this instead of the ifs...

template <typename EventType> inline const EventType *clapDynCast(const clap_event_header *e)
{
    if (e->space_id == CLAP_CORE_EVENT_SPACE_ID)
    {
        if (e->type >= CLAP_EVENT_NOTE_ON && e->type <= CLAP_EVENT_NOTE_END)
        {
            if constexpr (std::is_same_v<EventType, clap_event_note>)
                return reinterpret_cast<const clap_event_note *>(e);
        }
        if (e->type == CLAP_EVENT_NOTE_EXPRESSION)
        {
            if constexpr (std::is_same_v<EventType, clap_event_note_expression>)
                return reinterpret_cast<const clap_event_note_expression *>(e);
        }
        if (e->type == CLAP_EVENT_PARAM_VALUE)
        {
            if constexpr (std::is_same_v<EventType, clap_event_param_value>)
                return reinterpret_cast<const clap_event_param_value *>(e);
        }
        if (e->type == CLAP_EVENT_PARAM_MOD)
        {
            if constexpr (std::is_same_v<EventType, clap_event_param_mod>)
                return reinterpret_cast<const clap_event_param_mod *>(e);
        }
        if (e->type == CLAP_EVENT_PARAM_GESTURE_BEGIN || e->type == CLAP_EVENT_PARAM_GESTURE_END)
        {
            if constexpr (std::is_same_v<EventType, clap_event_param_gesture>)
                return reinterpret_cast<const clap_event_param_gesture *>(e);
        }
        if (e->type == CLAP_EVENT_MIDI)
        {
            if constexpr (std::is_same_v<EventType, clap_event_midi>)
                return reinterpret_cast<const clap_event_midi *>(e);
        }
        if (e->type == CLAP_EVENT_TRIGGER)
        {
            if constexpr (std::is_same_v<EventType, clap_event_trigger>)
                return reinterpret_cast<const clap_event_trigger *>(e);
        }
        if (e->type == CLAP_EVENT_TRANSPORT)
        {
            if constexpr (std::is_same_v<EventType, clap_event_transport>)
                return reinterpret_cast<const clap_event_transport *>(e);
        }
        if (e->type == CLAP_EVENT_MIDI2)
        {
            if constexpr (std::is_same_v<EventType, clap_event_midi2>)
                return reinterpret_cast<const clap_event_midi2 *>(e);
        }
        if (e->type == CLAP_EVENT_MIDI_SYSEX)
        {
            if constexpr (std::is_same_v<EventType, clap_event_midi_sysex>)
                return reinterpret_cast<const clap_event_midi_sysex *>(e);
        }
    }
    return nullptr;
}

inline void printXList(const xenakios::ClapEventList &elist)
{
    for (int i = 0; i < elist.size(); ++i)
    {
        auto e = elist.get(i);
        if (auto ne = clapDynCast<clap_event_note>(e))
            std::cout << e->time << "\tNOTE EVENT " << ne->key << "\n";
        else if (auto pe = clapDynCast<clap_event_param_value>(e))
        {
            std::cout << e->time << "\tPAR VALUE  " << pe->param_id << " " << pe->value << "\n";
        }
        else
            std::cout << e->time << "\tUNHANDLED FOR PRINTING\n";
    }
}

inline void testNewEventList()
{
    xenakios::ClapEventList elist;
    clap_process process;
    auto en = makeClapNote(500, CLAP_EVENT_NOTE_ON, 0, 0, 60, -1, 1.0);
    elist.tryPushAs(&en);
    en = makeClapNote(0, CLAP_EVENT_NOTE_ON, 0, 0, 67, -1, 1.0);
    elist.tryPushAs(&en);
    en = makeClapNote(400, CLAP_EVENT_NOTE_OFF, 0, 0, 67, -1, 1.0);
    elist.tryPushAs(&en);
    en = makeClapNote(100, CLAP_EVENT_NOTE_ON, 0, 0, 48, -1, 1.0);
    elist.tryPushAs(&en);
    elist.getLastAs<clap_event_note>()->key = 49;
    auto parev = makeClapParameterValueEvent(333, 42, 0.666, nullptr);

    elist.tryPushAs(&parev);
    clap_event_midi midiev;
    midiev.header.type = CLAP_EVENT_MIDI;
    midiev.header.size = sizeof(CLAP_EVENT_MIDI);
    midiev.header.time = 3;
    elist.tryPushAs(&midiev);
    printXList(elist);
    elist.sort();
    std::cout << "sorted\n";
    printXList(elist);
}

struct StereoSimperSVF // thanks to urs @ u-he and andy simper @ cytomic
{
    __m128 ic1eq{_mm_setzero_ps()}, ic2eq{_mm_setzero_ps()};
    __m128 g, k, gk, a1, a2, a3, ak;

    __m128 oneSSE{_mm_set1_ps(1.0)};
    __m128 twoSSE{_mm_set1_ps(2.0)};
    enum Mode
    {
        LP,
        HP,
        BP,
        NOTCH,
        PEAK,
        ALL
    };

    void setCoeff(float key, float res, float srInv);

    template <int Mode> static void step(StereoSimperSVF &that, float &L, float &R);
    template <int Mode> static __m128 stepSSE(StereoSimperSVF &that, __m128);

    void init();
};

const float pival = 3.14159265358979323846;

inline void StereoSimperSVF::setCoeff(float key, float res, float srInv)
{
    auto co = 440.0 * pow(2.0, (key - 69.0) / 12);
    co = std::clamp(co, 10.0, 25000.0); // just to be safe/lazy
    res = std::clamp(res, 0.01f, 0.99f);
    g = _mm_set1_ps(sst::basic_blocks::dsp::fasttan(pival * co * srInv));
    k = _mm_set1_ps(2.0 - 2.0 * res);
    gk = _mm_add_ps(g, k);
    a1 = _mm_div_ps(oneSSE, _mm_add_ps(oneSSE, _mm_mul_ps(g, gk)));
    a2 = _mm_mul_ps(g, a1);
    a3 = _mm_mul_ps(g, a2);
    ak = _mm_mul_ps(gk, a1);
}

template <int FilterMode>
inline void StereoSimperSVF::step(StereoSimperSVF &that, float &L, float &R)
{
    auto vin = _mm_set_ps(0, 0, R, L);
    auto res = stepSSE<FilterMode>(that, vin);
    float r4 alignas(16)[4];
    _mm_store_ps(r4, res);
    L = r4[0];
    R = r4[1];
}

template <int FilterMode> inline __m128 StereoSimperSVF::stepSSE(StereoSimperSVF &that, __m128 vin)
{
    // auto v3 = vin[c] - ic2eq[c];
    auto v3 = _mm_sub_ps(vin, that.ic2eq);
    // auto v0 = a1 * v3 - ak * ic1eq[c];
    auto v0 = _mm_sub_ps(_mm_mul_ps(that.a1, v3), _mm_mul_ps(that.ak, that.ic1eq));
    // auto v1 = a2 * v3 + a1 * ic1eq[c];
    auto v1 = _mm_add_ps(_mm_mul_ps(that.a2, v3), _mm_mul_ps(that.a1, that.ic1eq));

    // auto v2 = a3 * v3 + a2 * ic1eq[c] + ic2eq[c];
    auto v2 = _mm_add_ps(_mm_add_ps(_mm_mul_ps(that.a3, v3), _mm_mul_ps(that.a2, that.ic1eq)),
                         that.ic2eq);

    // ic1eq[c] = 2 * v1 - ic1eq[c];
    that.ic1eq = _mm_sub_ps(_mm_mul_ps(that.twoSSE, v1), that.ic1eq);
    // ic2eq[c] = 2 * v2 - ic2eq[c];
    that.ic2eq = _mm_sub_ps(_mm_mul_ps(that.twoSSE, v2), that.ic2eq);

    __m128 res;

    switch (FilterMode)
    {
    case LP:
        res = v2;
        break;
    case BP:
        res = v1;
        break;
    case HP:
        res = v0;
        break;
    case NOTCH:
        res = _mm_add_ps(v2, v0);
        break;
    case PEAK:
        res = _mm_sub_ps(v2, v0);
        break;
    case ALL:
        res = _mm_sub_ps(_mm_add_ps(v2, v0), _mm_mul_ps(that.k, v1));
        break;
    default:
        res = _mm_setzero_ps();
    }

    return res;
}

inline void StereoSimperSVF::init()
{
    ic1eq = _mm_setzero_ps();
    ic2eq = _mm_setzero_ps();
}

void test_np_code()
{
    std::shared_ptr<NoisePlethoraPlugin> plug;
    std::string plugToCreate = "satanWorkout";
    std::unordered_map<int, std::string> availablePlugins;
    int k = 0;
    for (int i = 0; i < numBanks; ++i)
    {
        auto &bank = getBankForIndex(i);
        std::cout << "bank " << i << "\n";
        for (int j = 0; j < programsPerBank; ++j)
        {
            std::cout << "\t" << bank.getProgramName(j) << "\n";
            availablePlugins[k] = bank.getProgramName(j);
            ++k;
        }
    }
    plug = MyFactory::Instance()->Create(availablePlugins[0]);
    if (!plug)
    {
        std::cout << "could not create plugin\n";
        return;
    }

    double sr = 44100;
    StereoSimperSVF filter;
    filter.init();
    StereoSimperSVF dcblocker;
    dcblocker.setCoeff(0.0, 0.01, 1.0 / sr);
    dcblocker.init();
    int outlen = sr * 10;
    juce::File outfile(
        R"(C:\develop\AudioPluginHost_mk2\Source\Experimental\audio\noise_plethora_out_02.wav)");
    outfile.deleteFile();
    auto ostream = outfile.createOutputStream();
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(ostream.release(), sr, 2, 32, {}, 0);
    juce::AudioBuffer<float> buf(2, outlen);
    buf.clear();

    plug->init();
    plug->m_sr = sr;
    std::minstd_rand0 rng;
    std::uniform_real_distribution<float> whitenoise{-1.0f, 1.0f};
    for (int i = 0; i < outlen; ++i)
    {
        float p0 = 0.5 + 0.5 * std::sin(2 * 3.141592653 / sr * i * 0.3);
        float p1 = 0.5 + 0.5 * std::sin(2 * 3.141592653 / sr * i * 0.4);
        float fcutoff = 84.0 + 12.0 * std::sin(2 * 3.141592653 / sr * i * 4.0);
        filter.setCoeff(fcutoff, 0.7, 1.0 / sr);
        plug->process(p0, p1);
        float outL = plug->processGraph();
        float outR = outL;
        dcblocker.step<StereoSimperSVF::HP>(dcblocker, outL, outR);
        filter.step<StereoSimperSVF::LP>(filter, outL, outR);
        buf.setSample(0, i, outL);
        buf.setSample(1, i, outR);
    }
    writer->writeFromAudioSampleBuffer(buf, 0, outlen);
    delete writer;
}

int main()
{
    test_np_code();
    // testNewEventList();
    return 0;
    juce::ScopedJuceInitialiser_GUI gui_init;
    // test_node_connecting();
    test_graph_processor_offline();
    return 0;
    clap::helpers::EventList list;
    auto ev = makeClapParameterValueEvent(1, 666, 0.987);
    list.push((const clap_event_header *)&ev);
    auto mev = makeClapParameterModEvent(46, 78, 0.444);
    list.push((const clap_event_header *)&mev);
    printClapEvents(list);
    return 0;
}
#endif
