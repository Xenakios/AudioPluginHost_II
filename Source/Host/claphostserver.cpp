
#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <exception>
#include "text/choc_StringUtilities.h"
#include "clap/events.h"
#include <random>
#include <windows.h>
#include <winbase.h>
#include <fileapi.h>
#include <memory>
#include <string>
#include <iostream>
#include <format>
#include "claphost.h"
#include "oscpkt.hh"
#include "udp.hh"
#include "memory/choc_xxHash.h"
#include <print>
#include "../Common/xap_ipc.h"

template <typename EventType> inline int writeClapEventToPipe(HANDLE pipe, EventType *ch)
{
    // This call blocks until a client process reads all the data
    char msgbuf[maxPipeMessageLen];
    // if things are working correctly, the memset redundant, but keeping this around
    // for debugging/testing for now
    memset(msgbuf, 0, maxPipeMessageLen);

    auto magic = messageMagicClap;
    memcpy(msgbuf, &magic, sizeof(uint64_t));
    memcpy(msgbuf + sizeof(uint64_t), ch, ch->header.size);
    DWORD numBytesWritten = 0;
    auto r = WriteFile(pipe,                               // handle to our outbound pipe
                       msgbuf,                             // data to send
                       sizeof(uint64_t) + ch->header.size, // length of data to send (bytes)
                       &numBytesWritten,                   // will store actual amount of data sent
                       NULL);                              // not using overlapped IO
    if (!r)
    {
        return 0;
    }
    return numBytesWritten;
}

inline void runInteractiveMode(HANDLE pipe)
{
    while (true)
    {
        try
        {
            std::string input;
            std::getline(std::cin, input);
            auto tokens = choc::text::splitString(input, ' ', false);
            if (tokens.size() == 0)
            {
                std::cout << "no input given!\n";
                continue;
            }
            tokens[0] = choc::text::toUpperCase(tokens[0]);
            if (tokens[0] == "QUIT")
            {
                std::cout << "finishing interactive mode\n";
                break;
            }

            if (tokens[0] == "ON" && tokens.size() >= 2)
            {
                int key = std::stoi(tokens[1]);
                if (key >= 0 && key < 128)
                {
                    double velo = 1.0;
                    if (tokens.size() >= 3)
                        velo = std::stod(tokens[2]);
                    auto nev =
                        xenakios::make_event_note(0, CLAP_EVENT_NOTE_ON, 0, 0, key, -1, velo);
                    writeClapEventToPipe(pipe, &nev);
                }
            }
            if (tokens[0] == "OFF" && tokens.size() >= 2)
            {
                int key = std::stoi(tokens[1]);
                if (key >= 0 && key < 128)
                {
                    auto nev =
                        xenakios::make_event_note(0, CLAP_EVENT_NOTE_OFF, 0, 0, key, -1, 0.0);
                    writeClapEventToPipe(pipe, &nev);
                }
            }
            if (tokens[0] == "TUNE" && tokens.size() >= 3)
            {
                int key = std::stoi(tokens[1]);
                if (key >= 0 && key < 128)
                {
                    double retune = std::stod(tokens[2]);
                    auto nexp = xenakios::make_event_note_expression(0, CLAP_NOTE_EXPRESSION_TUNING,
                                                                     0, 0, key, -1, retune);
                    writeClapEventToPipe(pipe, &nexp);
                }
            }
        }
        catch (std::exception &excep)
        {
            std::print("error parsing input : {}\n", excep.what());
        }
    }
}

inline void run_pipe_sender()
{
    HANDLE pipe = CreateNamedPipeA(g_pipename,           // name of the pipe
                                   PIPE_ACCESS_OUTBOUND, // 1-way pipe -- send only
                                   PIPE_TYPE_BYTE,       // send data as a byte stream
                                   1,                    // only allow 1 instance of this pipe
                                   65536,                // no outbound buffer
                                   0,                    // no inbound buffer
                                   0,                    // use default wait time
                                   NULL                  // use default security attributes
    );

    if (pipe == NULL || pipe == INVALID_HANDLE_VALUE)
    {
        std::cout << "Failed to create outbound pipe instance.\n";
        return;
    }

    std::cout << "Waiting for a client to connect to the pipe...\n";

    // This call blocks until a client process connects to the pipe
    BOOL result = ConnectNamedPipe(pipe, NULL);
    if (!result)
    {
        std::cout << "Failed to make connection on named pipe.\n";
        // look up error code here using GetLastError()
        CloseHandle(pipe); // close the pipe
        return;
    }

    std::cout << "Sending data to pipe...\n";
    bool interactive = true;
    if (!interactive)
    {
        int numMessagesToSend = 12;
        for (int i = 0; i < numMessagesToSend; ++i)
        {
            auto enote = xenakios::make_event_note(0, CLAP_EVENT_NOTE_ON, 1, 0, 60 + i, -1, 0.8888);

            writeClapEventToPipe(pipe, &enote);
            if (i == 5)
            {
                auto automev = xenakios::make_event_param_value(0, 666, 0.42, nullptr);
                writeClapEventToPipe(pipe, &automev);
            }
            if (i == 2)
            {
                xclap_string_event sev;
                sev.header.flags = 0;
                sev.header.size = sizeof(xclap_string_event);
                sev.header.type = 666;
                sev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                sev.header.time = 0;
                strcpy(sev.str, "this is a text event");
                writeClapEventToPipe(pipe, &sev);
            }
            if (i == 10)
            {
                clap_event_midi mev;
                mev.header.flags = 0;
                mev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                mev.header.type = CLAP_EVENT_MIDI;
                mev.header.size = sizeof(clap_event_midi);
                mev.header.time = 0;
                mev.port_index = 0;
                mev.data[0] = 0;
                mev.data[1] = 0;
                mev.data[2] = 0;
                writeClapEventToPipe(pipe, &mev);
            }
            Sleep(250);
        }
    }
    else
    {
        runInteractiveMode(pipe);
    }
    // Close the pipe (automatically disconnects client too)
    CloseHandle(pipe);
}

inline void handleClapEvent(clap_event_header *hdr)
{
    if (hdr->type == CLAP_EVENT_NOTE_ON && hdr->size == sizeof(clap_event_note))
    {
        auto *nev = (clap_event_note *)hdr;
        std::print("received clap note on event key={} velo={}\n", nev->key, nev->velocity);
    }
    else if (hdr->type == CLAP_EVENT_PARAM_VALUE && hdr->size == sizeof(clap_event_param_value))
    {
        auto *pev = (clap_event_param_value *)hdr;
        std::print("received clap param value event id={} value={}\n", pev->param_id, pev->value);
    }
    else if (hdr->type == CLAP_EVENT_NOTE_EXPRESSION &&
             hdr->size == sizeof(clap_event_note_expression))
    {
        auto *pexp = (clap_event_note_expression *)hdr;
        std::print("received clap note expression event net={} key={} value={}\n",
                   pexp->expression_id, pexp->key, pexp->value);
    }
    else if (hdr->type == 666 && hdr->size == sizeof(xclap_string_event))
    {
        auto *sev = (xclap_string_event *)hdr;
        std::print("received text event : {}\n", sev->str);
    }
    else
    {
        std::print("unhandled clap event of type {} size {}\n", hdr->type, hdr->size);
    }
}

inline void run_pipe_receiver()
{
    HANDLE pipe = CreateFileA(g_pipename,
                              GENERIC_READ, // only need read access
                              FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);

    if (pipe == INVALID_HANDLE_VALUE)
    {
        std::cout << "Failed to connect to pipe.\n";
        // look up error code here using GetLastError()
        return;
    }

    std::cout << "Reading data from pipe...\n";

    std::vector<char> buffer;
    buffer.resize(maxPipeMessageLen);
    while (true)
    {
        // The read operation will block until there is data to read
        DWORD numBytesRead = 0;
        BOOL result = ReadFile(pipe,
                               buffer.data(), // the data from the pipe will be put here
                               sizeof(uint64_t) + sizeof(clap_event_header),
                               &numBytesRead, // this will store number of bytes actually read
                               NULL           // not using overlapped IO
        );

        if (result)
        {
            uint64_t magic = 0;
            memcpy(&magic, buffer.data(), sizeof(uint64_t));
            if (magic != messageMagicClap)
            {
                std::cout << "message magic check failed!\n";
                CloseHandle(pipe);
                return;
            }

            clap_event_header *hdr = (clap_event_header *)(buffer.data() + sizeof(uint64_t));
            if (hdr->space_id == CLAP_CORE_EVENT_SPACE_ID)
            {
                int bytestoread = hdr->size - sizeof(clap_event_header);
                result = ReadFile(pipe,
                                  buffer.data() + sizeof(clap_event_header) +
                                      sizeof(uint64_t), // the data from the pipe will be put here
                                  bytestoread,          // number of bytes allocated
                                  &numBytesRead, // this will store number of bytes actually read
                                  NULL);         // not using overlapped IO
                // std::cout << "bytes to read " << bytestoread << " got " << numBytesRead << "\n";
                if (result && bytestoread == numBytesRead)
                {
                    handleClapEvent(hdr);
                }
                else
                {
                    std::cout << "failed to read concrete clap event from pipe\n";
                }
            }
        }
        else
        {
            std::cout << "Failed to read clap header from the pipe/end of messages\n";
            break;
        }
    }
    CloseHandle(pipe);
}

inline void test_pipe(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cout << "not enough arguments\n";
        return;
    }

    if (atoi(argv[1]) == 0)
    {
        run_pipe_sender();
    }
    else
    {
        run_pipe_receiver();
    }
}

inline void run_host(size_t receivePort)
{
    using namespace oscpkt;
    UdpSocket sock;
    size_t PORT_NUM = receivePort;
    sock.bindTo(PORT_NUM);
    if (!sock.isOk())
    {
        std::cerr << "Error opening port " << PORT_NUM << ": " << sock.errorMessage() << "\n";
        return;
    }
    auto eng = std::make_unique<ClapProcessingEngine>();
    // eng->addProcessorToChain(R"(C:\Program Files\Common Files\CLAP\Surge Synth Team\Surge
    // XT.clap)",
    //                         0);
    // eng->startStreaming(132, 44100, 512, false);
    std::cout << "Server started, will listen to packets on port " << PORT_NUM << std::endl;
    PacketReader pr;
    PacketWriter pw;
    bool shouldQuit = false;
    auto oscProcessFunc = [&pr, &pw, &sock, &eng]() {
        eng->runMainThreadTasks();
        if (!sock.isOk())
        {
            choc::messageloop::stop();
            return false;
        }

        if (sock.receiveNextPacket(30))
        {
            pr.init(sock.packetData(), sock.packetSize());
            oscpkt::Message *msg = nullptr;
            while (pr.isOk() && (msg = pr.popMessage()) != nullptr)
            {
                int iarg0;
                int iarg1;
                int iarg2;
                float darg0;
                float darg1;
                float darg2;
                float darg3;
                bool barg0;
                std::string strarg0;
                if (msg->match("/ping").popInt32(iarg0).isOkNoMoreArgs())
                {
                    std::cout << "Server: received /ping " << iarg0 << " from "
                              << sock.packetOrigin() << std::endl;
                    // Message repl;
                    // repl.init("/pong").pushInt32(iarg + 1);
                    // pw.init().addMessage(repl);
                    // sock.sendPacketTo(pw.packetData(), pw.packetSize(), sock.packetOrigin());
                }
                else if (msg->match("/play_note")
                             .popFloat(darg0)
                             .popFloat(darg1)
                             .popInt32(iarg0)
                             .popFloat(darg2)
                             .isOkNoMoreArgs())
                {
                    // std::cout << std::format("{} {} {} {}", darg0, darg1, iarg, darg2) <<
                    // std::endl;
                    // eng->postNoteMessage(darg0, darg1, iarg0, darg2);
                }
                else if (msg->match("/suspend_processing").popBool(barg0).isOkNoMoreArgs())
                {
                    eng->setSuspended(barg0);
                }
                else if (msg->match("/add_plugin").popStr(strarg0).popInt32(iarg0).isOkNoMoreArgs())
                {
                    eng->setSuspended(true);
                    eng->addProcessorToChain(strarg0, iarg0);
                    eng->setSuspended(false);
                }
                else if (msg->match("/show_gui").popBool(barg0).isOkNoMoreArgs())
                {
                    if (barg0)
                        eng->openPersistentWindow(0);
                    else
                        eng->closePersistentWindow(0);
                }
                else if (msg->match("/start_streaming")
                             .popInt32(iarg0)
                             .popInt32(iarg1)
                             .popInt32(iarg2)
                             .isOkNoMoreArgs())
                {
                    eng->startStreaming(iarg0, iarg1, iarg2, false);
                }
                else if (msg->match("/stop_streaming").isOkNoMoreArgs())
                {
                    choc::messageloop::stop();
                    return false;
                }
                else
                {
                    std::cout << "Server: unhandled message: " << msg->addressPattern() << " "
                              << msg->typeTags() << std::endl;
                }
            }
        }
        return true;
    };
    choc::messageloop::initialise();
    choc::messageloop::Timer timer(10, oscProcessFunc);
    choc::messageloop::run();
    std::cout << "stopped listening to OSC messages\n";
    eng->stopStreaming();
    std::cout << "stopped streaming audio\n";
}

int main(int argc, char **argv)
{
    test_pipe(argc, argv);
    return 0;
    auto mtx = CreateMutex(NULL, TRUE, "XenakiosClapServer");
    if (GetLastError() == ERROR_SUCCESS)
    {
        run_host(7001);
    }
    else
    {
        std::cout << "ClapHostServer already running" << std::endl;
    }
    ReleaseMutex(mtx);
    CloseHandle(mtx);
}
