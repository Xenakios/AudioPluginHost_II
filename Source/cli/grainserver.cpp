#include "../granularsynth/granularsynth.h"
#include "RtAudio.h"
#include "text/choc_Files.h"
#include "platform/choc_FileWatcher.h"

struct CallbackData
{
    ToneGranulator *granul = nullptr;
    std::vector<float> procbuffer;
    std::array<float, 512> cpuhistory;
    int num_mes = 200;
    int history_pos = 0;
    std::atomic<float> avg_usage;
};

inline int audiocb(void *outputBuffer, void *inputBuffer, unsigned int nFrames, double streamTime,
                   RtAudioStreamStatus status, void *userData)
{
    CallbackData *data = (CallbackData *)userData;
    float *obuf = (float *)outputBuffer;
    double sr = data->granul->m_sr;
    using clock = std::chrono::high_resolution_clock;
    using ns = std::chrono::duration<double, std::nano>;
    const auto start_time = clock::now();
    float *processbuf = data->procbuffer.data();
    data->granul->process_block(processbuf, nFrames);
    int procnumoutchs = data->granul->num_out_chans;
    for (int i = 0; i < nFrames; ++i)
    {
        float m = processbuf[i * procnumoutchs + 0];
        float s = processbuf[i * procnumoutchs + 1];
        obuf[i * 2 + 0] = std::clamp(m + s, -1.0f, 1.0f);
        obuf[i * 2 + 1] = std::clamp(m - s, -1.0f, 1.0f);
    }
    const ns render_duration = clock::now() - start_time;
    double maxelapsed = nFrames / sr * 1000000000.0;
    double curelapsed = render_duration.count();
    double cpuload = curelapsed / maxelapsed;
    data->cpuhistory[data->history_pos] = cpuload;
    ++data->history_pos;
    if (data->history_pos == data->num_mes)
    {
        double avg = 0.0;
        for (int i = 0; i < data->num_mes; ++i)
        {
            avg += data->cpuhistory[i];
        }
        data->avg_usage = avg / data->num_mes;
        data->history_pos = 0;
    }
    return 0;
}

std::atomic<bool> g_quit{false};

BOOL WINAPI CtrlHandler(DWORD fdwCtrlType)
{
    switch (fdwCtrlType)
    {
        // Handle the CTRL-C signal.
    case CTRL_C_EVENT:
        // printf("Ctrl-C event\n\n");
        g_quit = true;
        return TRUE;
    case CTRL_BREAK_EVENT:
        // printf("Ctrl-Break event\n\n");
        g_quit = true;
        return TRUE;
    default:
        return FALSE;
    }
}

inline events_t load_events_file(std::string path)
{
    events_t result;
    result.reserve(4096);
    try
    {
        auto txt = choc::file::loadFileAsString(path);
        auto lines = choc::text::splitIntoLines(txt, false);

        for (const auto &line : lines)
        {
            auto tokens = choc::text::splitAtWhitespace(line);
            if (tokens.size() == 0)
                continue;
            // std::print("{}\n", tokens);
            GrainEvent evt;
            if (tokens.size() >= 1)
                evt.time_position = std::stof(tokens[0]);
            if (tokens.size() >= 2)
                evt.duration = std::stof(tokens[1]);
            if (tokens.size() >= 3)
                evt.frequency_hz = std::stof(tokens[2]);
            if (tokens.size() >= 4)
                evt.volume = std::stof(tokens[3]);
            if (tokens.size() >= 5 && !tokens[4].empty())
                evt.azimuth = std::stof(tokens[4]);
            if (tokens.size() >= 6 && !tokens[5].empty())
                evt.elevation = std::stof(tokens[5]);
            if (tokens.size() >= 7 && !tokens[6].empty())
                evt.envelope_type = std::stof(tokens[6]);
            if (tokens.size() >= 8 && !tokens[7].empty())
                evt.envelope_shape = std::stof(tokens[7]);
            if (tokens.size() >= 9 && !tokens[8].empty())
                evt.sync_ratio = std::stof(tokens[8]);
            if (tokens.size() >= 10)
            {
                evt.filterparams[0][0] = std::stof(tokens[9]);
                evt.filterparams[0][1] = std::stof(tokens[10]);
                evt.filterparams[0][2] = std::stof(tokens[11]);
                evt.filterparams[1][0] = std::stof(tokens[12]);
                evt.filterparams[1][1] = std::stof(tokens[13]);
                evt.filterparams[1][2] = std::stof(tokens[14]);
            }

            result.push_back(evt);
        }
    }
    catch (std::exception &ex)
    {
        std::print("{}\n", ex.what());
    }
    return result;
}

int main()
{
    auto a = xenakios::decibelsToGain(120.0f);
    // auto a = to_clap_id(666);
    // auto b = to_clap_id(666.0);
    // return 0;
    SetConsoleCtrlHandler(CtrlHandler, TRUE);
    double sr = 44100.0;
    double phase = 0.0;
    CallbackData cbdata;
    auto granulator =
        std::make_unique<ToneGranulator>(sr, 0, "fast_svf/lowpass", "none", 0.002, 0.001);
    cbdata.granul = granulator.get();
    granulator->maingain = 0.5;
    auto rtaudio = std::make_unique<RtAudio>();
    RtAudio::StreamParameters spars;
    spars.firstChannel = 0;
    spars.nChannels = 2;
    spars.deviceId = rtaudio->getDefaultOutputDevice();
    std::print("trying to open {}\n", rtaudio->getDeviceInfo(spars.deviceId).name);
    unsigned int bsize = 256;
    std::string grainfile = R"(C:\MusicAudio\sourcesamples\test_signals\tones\grains.txt)";
    choc::file::Watcher watcher{grainfile, [&](const choc::file::Watcher::Event &ev) {
                                    // std::print("file was changed {}\n",ev.time);
                                    auto evlist = load_events_file(grainfile);
                                    if (evlist.size() > 0)
                                    {
                                        granulator->prepare(std::move(evlist), 1);
                                    }
                                }};
    cbdata.procbuffer.resize(16 * 1024);
    if (rtaudio->openStream(&spars, nullptr, RTAUDIO_FLOAT32, sr, &bsize, audiocb, &cbdata) ==
        RTAUDIO_NO_ERROR)
    {
        std::print("opened rtaudio with buffer size {}\n", bsize);
        rtaudio->startStream();
        while (true)
        {
            if (g_quit)
                break;
            // std::print("{:.1f}\r", cbdata.avg_usage * 100.0);
            Sleep(100);
        }
        std::print("\nquit server loop\n");
        rtaudio->stopStream();
        rtaudio->closeStream();
    }
    else
    {
        std::print("could not open rtaudio stream\n");
    }
    return 0;
}
