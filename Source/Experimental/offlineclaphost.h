#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <map>
#include <iostream>

#include "audio/choc_AudioFileFormat.h"
#include "containers/choc_SingleReaderSingleWriterFIFO.h"
#include "memory/choc_Base64.h"
#include "audio/choc_SampleBuffers.h"
#include "audio/choc_AudioFileFormat_WAV.h"
#include "xapdsp.h"
#include "xap_utils.h"
#include "xaps/clap_xaudioprocessor.h"
#include "containers/choc_Span.h"

#include "gui/choc_DesktopWindow.h"
#include "gui/choc_MessageLoop.h"
#include "RtAudio.h"
#include "testsinesynth.h"
#include "clap_eventsequence.h"

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

class ClapProcessingEngine
{

  public:
    struct ProcessorEntry
    {
        std::unique_ptr<xenakios::XAudioProcessor> m_proc;
        ClapEventSequence m_seq;
        std::optional<ClapEventSequence::IteratorSampleTime> m_eviter;
        std::string name;
        std::unique_ptr<choc::ui::DesktopWindow> guiWindow;
    };
    std::vector<std::unique_ptr<ProcessorEntry>> m_chain;

    void setSequence(int targetProcessorIndex, ClapEventSequence seq);
    static std::vector<std::filesystem::path> scanPluginDirectories();
    static std::string scanPluginFile(std::filesystem::path plugfilename);
    ClapProcessingEngine();
    ~ClapProcessingEngine();

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

    void saveStateToJSONFile(size_t chainIndex, const std::filesystem::path &filepath);
    void loadStateFromJSONFile(size_t chainIndex, const std::filesystem::path &filepath);
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

    void openPluginGUIBlocking(size_t chainIndex, bool closeImmediately);

    void openPersistentWindow(int chainIndex);
    void closePersistentWindow(int chainIndex);

    std::unique_ptr<RtAudio> m_rtaudio;
    std::vector<std::string> getDeviceNames();
    void prepareToPlay(double sampleRate, int maxBufferSize);
    void startStreaming(unsigned int deviceId, double sampleRate, int preferredBufferSize,
                        bool blockExecution);
    void wait(double seconds);
    void stopStreaming();
    void postNoteMessage(double delay, double duration, int key, double velo);
    void processAudio(choc::buffer::ChannelArrayView<float> inputBuffer,
                      choc::buffer::ChannelArrayView<float> outputBuffer);
    void runMainThreadTasks();
    void setSuspended(bool b);
    choc::fifo::SingleReaderSingleWriterFIFO<ClapEventSequence::Event> m_to_test_tone_fifo;
    std::vector<ClapEventSequence::Event> m_delayed_messages;

    int64_t m_samplePlayPos = 0;

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
