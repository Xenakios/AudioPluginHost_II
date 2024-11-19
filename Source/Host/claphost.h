#pragma once

#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <map>
#include <iostream>

#include "audio/choc_AudioFileFormat.h"
#include "clap/audio-buffer.h"
#include "clap/ext/audio-ports.h"
#include "clap/helpers/event-list.hh"
#include "containers/choc_SingleReaderSingleWriterFIFO.h"
#include "memory/choc_Base64.h"
#include "audio/choc_SampleBuffers.h"
#include "audio/choc_AudioFileFormat_WAV.h"
#include "../Common/xapdsp.h"
#include "../Common/xap_utils.h"
#include "../Xaps/clap_xaudioprocessor.h"
#include "containers/choc_Span.h"

#include "gui/choc_DesktopWindow.h"
#include "gui/choc_MessageLoop.h"
#include "RtAudio.h"
#include "../Common/clap_eventsequence.h"
#include "../Common/xap_breakpoint_envelope.h"
#include "sst/basic-blocks/dsp/FollowSlewAndSmooth.h"
#include "BS_thread_pool.hpp"

inline void generateNoteExpressionsFromEnvelope(ClapEventSequence &targetSeq,
                                                xenakios::Envelope<64> &sourceEnvelope,
                                                double eventsStartTime, double duration,
                                                double granularity, int net, int port, int chan,
                                                int key, int note_id)
{
    double t = eventsStartTime;
    while (t < duration + granularity + eventsStartTime)
    {
        double v = sourceEnvelope.getValueAtPosition(t - eventsStartTime);
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
        double v = sourceEnvelope.getValueAtPosition(t - eventsStartTime);
        targetSeq.addParameterEvent(is_mod, t, port, chan, key, note_id, parid, v);
        t += granularity;
    }
}

// super primitive initial attempt
class TempoMap
{
  public:
    TempoMap() { m_beat_to_secs_map.reserve(65536); }
    double beatPosToSeconds(double beatpos)
    {
        if (!isEnvelopeActive())
            return 60.0 / m_static_bpm * beatpos;
        int index = beatpos * 4;
        if (index >= 0 && index < m_beat_to_secs_map.size())
            return m_beat_to_secs_map[index];
        return 60.0 / m_static_bpm * beatpos;
    }
    bool isEnvelopeActive() { return m_bpm_envelope.getNumPoints() > 0; }
    void setStaticBPM(double bpm) { m_static_bpm = std::clamp(bpm, 5.0, 1000.0); }
    xenakios::Envelope<64> m_bpm_envelope;
    void updateMapping()
    {
        double lastbeat = m_bpm_envelope.getPointSafe(m_bpm_envelope.getNumPoints() - 1).getX();
        int num_to_eval = 4 * std::round(lastbeat);
        double beat = 0.0;
        double t = 0.0;
        m_beat_to_secs_map.clear();
        while (beat < lastbeat + 1.0)
        {
            double bpm = m_bpm_envelope.getValueAtPosition(beat);
            m_beat_to_secs_map.push_back(t);
            t += 60.0 / bpm * 0.25;
            beat += 0.25;
        }
    }

  private:
    double m_static_bpm = 120.0;

    std::vector<double> m_beat_to_secs_map;
};

struct ProcessorEntry
{
    std::unique_ptr<xenakios::XAudioProcessor> m_proc;
    ClapEventSequence m_seq;
    std::optional<ClapEventSequence::IteratorSampleTime> m_eviter;
    std::string name;
    std::unique_ptr<choc::ui::DesktopWindow> guiWindow;
    std::unordered_map<clap_id, std::string> idToStringMap;
    std::unordered_map<std::string, clap_id> stringToIdMap;
    struct Msg
    {
        enum class Opcode
        {
            None,
            SetParam
        };
        Opcode opcode = Opcode::None;
        int64_t i0 = 0;
        double d0 = 0.0;
    };
    choc::fifo::SingleReaderSingleWriterFIFO<Msg> from_ui_fifo;
};

class ProcessorChain
{
  public:
    ProcessorChain(int64_t _id) : id(_id) {}
    ProcessorChain(std::vector<std::pair<std::string, int>> plugins);
    ~ProcessorChain();
    int64_t id;
    std::vector<std::unique_ptr<ProcessorEntry>> m_processors;
    // this should really take in a Clap plugin id instead
    void addProcessor(std::string plugfilename, int pluginindex);

    ClapEventSequence &getSequence(size_t pluginIndex) { return m_processors[pluginIndex]->m_seq; }
    ProcessorEntry &getProcessor(int pluginIndex);
    void activate(double sampleRate, int maxBlockSize);
    std::vector<std::tuple<int, int, int>> inputRouting;
    std::vector<std::tuple<int, int, int>> outputRouting;
    int highestInputPort = 0;
    int highestOutputChannel = 0;
    std::vector<float> chainAudioOutputData;
    void setInputRouting(std::vector<std::tuple<int, int, int>> routing);
    void setOutputRouting(std::vector<std::tuple<int, int, int>> routing);
    int processAudio(choc::buffer::ChannelArrayView<float> inputBuffer,
                     choc::buffer::ChannelArrayView<float> outputBuffer);

    void stopProcessing();
    int getNumAudioPorts(size_t pluginIndex, bool isInput);
    clap_audio_port_info getAudioPortInfo(size_t pluginIndex, size_t portIndex, bool isInput);
    std::string getParametersAsJSON(size_t chainIndex);
    std::vector<float> audioInputData;
    float *getInputBuffer(size_t index)
    {
        if (audioInputData.empty())
            return nullptr;
        return &audioInputData[index * blockSize];
    }
    std::vector<clap_audio_buffer> inputBuffers;
    std::vector<float> audioOutputData;
    float *getOutputBuffer(size_t index)
    {
        if (audioOutputData.empty())
            return nullptr;
        return &audioOutputData[index * blockSize];
    }
    std::vector<clap_audio_buffer> outputBuffers;
    std::vector<float *> inChannelPointers;
    std::vector<float *> outChannelPointers;
    clap::helpers::EventList inEventList;
    clap::helpers::EventList outEventList;
    size_t blockSize = 0;
    int64_t samplePosition = 0;
    double currentSampleRate = 0.0;
    std::atomic<bool> isProcessing{false};
    std::atomic<bool> isActivated{false};
    std::thread::id main_thread_id;
    // events specific to the chain like output volume
    ClapEventSequence chainSequence;
    std::optional<ClapEventSequence::IteratorSampleTime> eventIterator;
    double chainGain = 1.0;
    sst::basic_blocks::dsp::SlewLimiter chainGainSmoother;
    bool muted = false;
    enum class ChainParameters
    {
        Volume,
        Mute
    };
    BS::thread_pool thpool{1};
};

class ClapProcessingEngine
{

  public:
    std::vector<std::unique_ptr<ProcessorEntry>> m_chain;
    std::vector<std::unique_ptr<ProcessorChain>> m_chains;
    ProcessorChain &addChain();
    ProcessorChain &getChain(size_t index);
    void setSequence(int targetProcessorIndex, ClapEventSequence seq);
    static std::vector<std::filesystem::path> scanPluginDirectories();
    static std::string scanPluginFile(std::filesystem::path plugfilename);
    ClapProcessingEngine();
    ~ClapProcessingEngine();
    static std::set<ClapProcessingEngine *> &getEngines();

    void addProcessorToChain(std::string plugfilename, int pluginindex);
    void removeProcessorFromChain(int index);

    ClapEventSequence &getSequence(size_t chainIndex);
    std::map<std::string, clap_id> getParameters(size_t chainIndex);
    std::string getParametersAsJSON(size_t chainIndex);
    std::string getParameterValueAsText(size_t chainIndex, clap_id parId, double value);
    size_t getNumParameters(size_t chainIndex)
    {
        return m_chain[chainIndex]->m_proc->paramsCount();
    }
    std::string getParameterInfoString(size_t chainIndex, size_t index);

    void saveStateToBinaryFile(size_t chainIndex, const std::filesystem::path &filepath);
    void loadStateFromBinaryFile(size_t chainIndex, const std::filesystem::path &filepath);
    std::unordered_map<size_t, std::filesystem::path> deferredStateFiles;
    void loadStateFromBinaryFileDeferred(size_t chainIndex, const std::filesystem::path &filepath)
    {
        deferredStateFiles[chainIndex] = filepath;
    }
    choc::audio::AudioFileProperties outfileprops;
    clap_audio_buffer m_clap_inbufs[32];
    clap_audio_buffer m_clap_outbufs[32];
    std::vector<choc::buffer::ChannelArrayBuffer<float>> inputbuffers;
    std::vector<choc::buffer::ChannelArrayBuffer<float>> outputbuffers;

    clap::helpers::EventList list_in;
    clap::helpers::EventList list_out;

    void checkPluginIndex(size_t index);

    void processToFile(std::string filename, double duration, double samplerate, int numoutchans);
    void processToFile2(std::string filename, double duration, double samplerate, int numoutchans);

    void openPluginGUIBlocking(size_t chainIndex, bool closeImmediately);

    void openPersistentWindow(int chainIndex);
    void closePersistentWindow(int chainIndex);

    std::unique_ptr<RtAudio> m_rtaudio;
    std::vector<std::string> getDeviceNames();
    void prepareToPlay(double sampleRate, int maxBufferSize);
    void startStreaming(std::optional<unsigned int> deviceId, double sampleRate,
                        int preferredBufferSize, bool blockExecution);
    void wait(double seconds);
    void stopStreaming();
    void allNotesOff();
    void setMainVolume(double decibels);
    void postNoteMessage(int destination, double delay, double duration, int key, double velo);
    void postParameterMessage(int destination, double delay, clap_id parid, double value);
    void processAudio(choc::buffer::ChannelArrayView<float> inputBuffer,
                      choc::buffer::ChannelArrayView<float> outputBuffer);
    void runMainThreadTasks();
    void setSuspended(bool b);
    choc::fifo::SingleReaderSingleWriterFIFO<ClapEventSequence::Event>
        m_realtime_messages_to_plugins;
    struct EngineMessage
    {
        enum class Opcode
        {
            None,
            AllNotesOff,
            MainVolume
        };
        Opcode opcode = Opcode::None;
        int iarg0 = 0;
        double farg0 = 0.0;
    };
    choc::fifo::SingleReaderSingleWriterFIFO<EngineMessage> m_engineCommandFifo;
    std::vector<ClapEventSequence::Event> m_delayed_messages;
    int64_t m_samplePlayPos = 0;
    double m_mainGain = 1.0;
    choc::buffer::ChannelArrayBuffer<float> outputConversionBuffer;
    choc::buffer::ChannelArrayBuffer<float> inputConversionBuffer;
    double m_samplerate = 0.0;
    int64_t m_transportposSamples = 0;
    int64_t m_timerPosSamples = 0;
    enum class ProcState
    {
        Idle,
        NeedsStarting,
        Started,
        NeedsStopping
    };
    std::atomic<ProcState> m_processorsState{ProcState::Idle};
    std::atomic<bool> m_isSuspended{false};
    // lock this only when *absolutely* necessary
    std::mutex m_mutex;
    std::atomic<bool> m_isPrepared{false};
    clap_process m_clap_process;
    choc::messageloop::Timer m_gui_tasks_timer;
};
