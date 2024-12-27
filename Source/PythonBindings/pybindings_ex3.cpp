#include <cstdint>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <stdexcept>
#include <windows.h>
#include <handleapi.h>
#include <winbase.h>
#include <print>
#include "../Common/xap_utils.h"
#include "clap/events.h"

namespace py = pybind11;

static const char *g_pipename = "\\\\.\\pipe\\clap_pipe";

// pipe messages should start with these byte patterns,
// if they don't, something has went wrong somewhere and should abort processing
static const uint64_t messageMagicClap = 0xFFFFFFFF00EE0000;
static const uint64_t messageMagicCustom = 0xFFFFFFFF00EF0000;

static const uint32_t maxPipeMessageLen = 512;

template <typename EventType>
inline int writeClapEventToPipe(HANDLE pipe, double timeDelay, EventType *ch)
{
    char msgbuf[maxPipeMessageLen * 2];
    // if things are working correctly, the memset redundant, but keeping this around
    // for debugging/testing for now
    memset(msgbuf, 0, maxPipeMessageLen * 2);

    auto magic = messageMagicClap;
    memcpy(msgbuf, &magic, sizeof(uint64_t));
    memcpy(msgbuf + sizeof(uint64_t), &timeDelay, sizeof(double));
    memcpy(msgbuf + sizeof(uint64_t) + sizeof(double), ch, ch->header.size);
    size_t messagelen = sizeof(uint64_t) + sizeof(double) + ch->header.size;
    DWORD numBytesWritten = 0;
    // This call blocks until a client process reads all the data
    auto r = WriteFile(pipe,             // handle to our outbound pipe
                       msgbuf,           // data to send
                       messagelen,       // length of data to send (bytes)
                       &numBytesWritten, // will store actual amount of data sent
                       NULL);            // not using overlapped IO
    if (!r)
    {
        return 0;
    }
    return numBytesWritten;
}

class ClapPipeSender
{
  public:
    const size_t pipeBufferSize = 1024 * 1024;
    ClapPipeSender()
    {

        m_pipe = CreateNamedPipeA(g_pipename,           // name of the pipe
                                  PIPE_ACCESS_OUTBOUND, // 1-way pipe -- send only
                                  PIPE_TYPE_BYTE,       // send data as a byte stream
                                  1,                    // only allow 1 instance of this pipe
                                  pipeBufferSize,       // outbound buffer
                                  0,                    // no inbound buffer
                                  0,                    // use default wait time
                                  NULL                  // use default security attributes
        );

        if (m_pipe == NULL || m_pipe == INVALID_HANDLE_VALUE)
        {
            throw std::runtime_error("Failed to create outbound pipe instance");
        }

        std::print("Waiting for a client to connect to the pipe...\n");

        // This call blocks until a client process connects to the pipe
        BOOL result = ConnectNamedPipe(m_pipe, NULL);
        if (!result)
        {
            m_pipe = INVALID_HANDLE_VALUE;
            throw std::runtime_error("Failed to make connection on named pipe");
        }
    }
    ~ClapPipeSender()
    {
        if (m_pipe != INVALID_HANDLE_VALUE && m_pipe != NULL)
        {
            CloseHandle(m_pipe);
        }
    }
    void sendNoteMessages(const std::vector<std::tuple<double, int, double>> &notes)
    {
        if (m_pipe == INVALID_HANDLE_VALUE || m_pipe == NULL)
            throw std::runtime_error("Pipe is not available");

        for (int i = 0; i < notes.size(); ++i)
        {
            double t = std::get<0>(notes[i]);
            int key = std::get<1>(notes[i]);
            double dur = std::get<2>(notes[i]);
            auto nev = xenakios::make_event_note(0, CLAP_EVENT_NOTE_ON, 0, 0, key, -1, 0.8);
            writeClapEventToPipe(m_pipe, t, &nev);
            nev = xenakios::make_event_note(0, CLAP_EVENT_NOTE_OFF, 0, 0, key, -1, 0.0);
            writeClapEventToPipe(m_pipe, t + dur, &nev);
        }
    }

  private:
    HANDLE m_pipe = INVALID_HANDLE_VALUE;
};

void init_py3(py::module_ &m, py::module_ &m_const)
{
    py::class_<ClapPipeSender>(m, "ClapPipeSender")
        .def(py::init<>())
        .def("send_notes", &ClapPipeSender::sendNoteMessages);
}
