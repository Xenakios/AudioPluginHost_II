#include <windows.h>
#include <handleapi.h>
#include "../Common/xap_ipc.h"

#include "clap/clap.h"
#include "clap/events.h"
#include "clap/ext/note-ports.h"
#include "clap/helpers/plugin.hh"
#include "clap/helpers/plugin.hxx"
#include "clap/helpers/host-proxy.hh"
#include "clap/helpers/host-proxy.hxx"
#include "clap/plugin-features.h"
#include "sst/basic-blocks/params/ParamMetadata.h"
#include "containers/choc_SingleReaderSingleWriterFIFO.h"
#include "../Common/xap_utils.h"
#include <unordered_map>

using ParamDesc = sst::basic_blocks::params::ParamMetaData;

struct xen_pipereceiver
    : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                   clap::helpers::CheckingLevel::Maximal>
{
    std::vector<ParamDesc> paramDescs;
    HANDLE m_pipe = INVALID_HANDLE_VALUE;
    const void *extension(const char *id) noexcept override
    {
        /*
        if (!strcmp(id, CLAP_EXT_PARAM_ORIGIN))
        {
            return &ext_parameter_origin;
        }
        */
        return nullptr;
    }

    xen_pipereceiver(const clap_host *host, const clap_plugin_descriptor *desc)
        : clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                clap::helpers::CheckingLevel::Maximal>(desc, host)
    {
        // bool(CLAP_ABI *get)(const clap_plugin_t *plugin, clap_id param_id, double *out_value);
        /*
        ext_parameter_origin.get = [](const clap_plugin_t *plugin, clap_id param_id,
                                      double *out_value) {
            auto myplugin = (xen_fileplayer *)plugin->plugin_data;
            auto it = myplugin->paramOrigins.find(param_id);
            if (it != myplugin->paramOrigins.end())
            {
                *out_value = it->second;
                return true;
            }
            return false;
        };
        */
    }
    std::unique_ptr<std::thread> m_pipe_thread;
    ~xen_pipereceiver()
    {
        m_stop_pipe_thread = true;
        if (m_pipe_thread)
        {
            m_pipe_thread->join();
        }
        if (m_pipe != INVALID_HANDLE_VALUE)
        {
            CloseHandle(m_pipe);
        }
    }
    void pipeThreadRun()
    {
        std::cout << "starting xenakios pipe receiver thread..." << std::endl;
        char buffer[maxPipeMessageLen];
        while (!m_stop_pipe_thread)
        {
            DWORD numBytesRead = 0;
            BOOL result = ReadFile(m_pipe,
                                   buffer, // the data from the pipe will be put here
                                   sizeof(uint64_t) + sizeof(clap_event_header),
                                   &numBytesRead, // this will store number of bytes actually read
                                   NULL           // not using overlapped IO
            );

            if (result)
            {
                uint64_t magic = 0;
                memcpy(&magic, buffer, sizeof(uint64_t));
                if (magic != messageMagicClap)
                {
                    std::cout << "message magic check failed!\n";
                    // CloseHandle(pipe);
                    return;
                }

                clap_event_header *hdr = (clap_event_header *)(buffer + sizeof(uint64_t));
                if (hdr->space_id == CLAP_CORE_EVENT_SPACE_ID)
                {
                    int bytestoread = hdr->size - sizeof(clap_event_header);
                    result =
                        ReadFile(m_pipe,
                                 buffer + sizeof(clap_event_header) +
                                     sizeof(uint64_t), // the data from the pipe will be put here
                                 bytestoread,          // number of bytes allocated
                                 &numBytesRead, // this will store number of bytes actually read
                                 NULL);         // not using overlapped IO
                    // std::cout << "bytes to read " << bytestoread << " got " << numBytesRead <<
                    // "\n";
                    if (result && bytestoread == numBytesRead)
                    {
                        //*(clap_multi_event *)e
                        m_from_pipe_fifo.push(*(ClapMultiEvent *)hdr);
                        if (hdr->type == CLAP_EVENT_NOTE_ON || hdr->type == CLAP_EVENT_NOTE_OFF)
                        {
                            auto nev = (clap_event_note *)hdr;
                            //m_from_pipe_fifo.push(*(ClapMultiEvent *)&nev);
                        }
                    }
                    else
                    {
                        std::cout << "failed to read concrete clap event from pipe\n";
                    }
                }
            }
            else
            {
                //std::cout << "Failed to read clap header from the pipe/end of messages\n";
                // break;
            }
            Sleep(10);
        }
        std::cout << "xenakios pipe receiver thread ended" << std::endl;
    }
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_stop_pipe_thread{false};
    union ClapMultiEvent
    {
        clap_event_header_t hdr;
        clap_event_note_t note;
        clap_event_note_expression_t note_exp;
        clap_event_param_value_t par_value;
    };
    choc::fifo::SingleReaderSingleWriterFIFO<ClapMultiEvent> m_from_pipe_fifo;
    bool activate(double sampleRate_, uint32_t minFrameCount,
                  uint32_t maxFrameCount) noexcept override
    {
        if (m_pipe == INVALID_HANDLE_VALUE)
        {
            HANDLE pipe = CreateFileA(g_pipename,
                                      GENERIC_READ, // only need read access
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL, NULL);

            m_pipe = pipe;
            if (pipe == INVALID_HANDLE_VALUE)
            {
                std::cout << "Failed to connect to pipe.\n";
            }
            else
            {
                m_from_pipe_fifo.reset(128);
                m_connected = true;
                m_pipe_thread = std::make_unique<std::thread>([this] { pipeThreadRun(); });
            }
        }
        return true;
    }
    bool implementsParams() const noexcept override { return false; }
    bool isValidParamId(clap_id paramId) const noexcept override { return false; }
    uint32_t paramsCount() const noexcept override { return 0; }
    bool paramsInfo(uint32_t paramIndex, clap_param_info *info) const noexcept override
    {
        return false;
        if (paramIndex >= paramDescs.size())
            return false;
        paramDescs[paramIndex].toClapParamInfo<CLAP_NAME_SIZE>(info);
        return true;
    }
    bool paramsValue(clap_id paramId, double *value) noexcept override { return false; }
    bool paramsValueToText(clap_id paramId, double value, char *display,
                           uint32_t size) noexcept override
    {
        return false;
    }
    bool implementsAudioPorts() const noexcept override { return false; }
    uint32_t audioPortsCount(bool isInput) const noexcept override { return 0; }
    bool audioPortsInfo(uint32_t index, bool isInput,
                        clap_audio_port_info *info) const noexcept override
    {
        return false;
    }
    bool implementsNotePorts() const noexcept override { return true; }
    uint32_t notePortsCount(bool isInput) const noexcept override
    {
        if (isInput)
            return 1;
        return 1;
    }
    bool notePortsInfo(uint32_t index, bool isInput,
                       clap_note_port_info *info) const noexcept override
    {
        if (isInput)
        {
            info->id = 100;
            strcpy_s(info->name, "Note Input");
            info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
            info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
        }
        if (!isInput)
        {
            info->id = 101;
            strcpy_s(info->name, "Note Output");
            info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
            info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
        }
        return true;
    }
    clap_process_status process(const clap_process *process) noexcept override
    {
        // add evebts from the pipe to output events (will be at time stamp 0)
        ClapMultiEvent msg;
        while (m_from_pipe_fifo.pop(msg))
        {
            process->out_events->try_push(process->out_events, (clap_event_header *)&msg);
        }

        int numEvents = process->in_events->size(process->in_events);
        for (int i = 0; i < numEvents; ++i)
        {
            auto ev = process->in_events->get(process->in_events, i);
            process->out_events->try_push(process->out_events, ev);
        }

        return CLAP_PROCESS_CONTINUE;
    }
};

const char *features[] = {CLAP_PLUGIN_FEATURE_UTILITY, nullptr};
clap_plugin_descriptor desc = {CLAP_VERSION,
                               "com.xenakios.clappipe_receiver",
                               "xenakios clappipe receiver",
                               "Unsupported",
                               "https://xenakios.com",
                               "",
                               "",
                               "0.0.0",
                               "xenakios clappipe_receiver",
                               features};

static const clap_plugin *clap_create_plugin(const clap_plugin_factory *f, const clap_host *host,
                                             const char *plugin_id)
{
    // I know it looks like a leak right? but the clap-plugin-helpers basically
    // take ownership and destroy the wrapper when the host destroys the
    // underlying plugin (look at Plugin<h, l>::clapDestroy if you don't believe me!)
    auto wr = new xen_pipereceiver(host, &desc);
    return wr->clapPlugin();
}

uint32_t get_plugin_count(const struct clap_plugin_factory *factory) { return 1; }

const clap_plugin_descriptor *get_plugin_descriptor(const clap_plugin_factory *f, uint32_t w)
{
    return &desc;
}

const CLAP_EXPORT struct clap_plugin_factory the_factory = {
    get_plugin_count,
    get_plugin_descriptor,
    clap_create_plugin,
};

static const void *get_factory(const char *factory_id) { return &the_factory; }

// clap_init and clap_deinit are required to be fast, but we have nothing we need to do here
bool clap_init(const char *p) { return true; }

void clap_deinit() {}

extern "C"
{

    // clang-format off
const CLAP_EXPORT struct clap_plugin_entry clap_entry = {
        CLAP_VERSION,
        clap_init,
        clap_deinit,
        get_factory
};
    // clang-format on
}
