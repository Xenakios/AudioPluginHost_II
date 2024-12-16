

#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "clap/events.h"
#include <random>
#include <windows.h>
#include <winbase.h>
#include <fileapi.h>
#include <memory>
// #undef min
// #undef max
#include <string>
#include <iostream>
#include <format>
#include "claphost.h"
#include "oscpkt.hh"
#include "udp.hh"
#include "memory/choc_xxHash.h"
#include <print>
// #include <namedpipeapi.h>

const char *g_pipename = "\\\\.\\pipe\\my_pipe";

const uint64_t messageMagic = 0xFFFFFFFF00EE0000;

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
    auto writeClapEventToPipe = [&pipe](auto *ch) {
        // This call blocks until a client process reads all the data
        DWORD numBytesWritten = 0;
        auto r = WriteFile(pipe,             // handle to our outbound pipe
                           ch,               // data to send
                           ch->header.size,  // length of data to send (bytes)
                           &numBytesWritten, // will store actual amount of data sent
                           NULL);            // not using overlapped IO
        if (!r)
        {
            std::cout << "failed to send data\n";
        }
    };
    std::cout << "Sending data to pipe...\n";
    bool interactive = true;
    if (!interactive)
    {

        int numMessagesToSend = 12;
        for (int i = 0; i < numMessagesToSend; ++i)
        {
            auto enote = xenakios::make_event_note(0, CLAP_EVENT_NOTE_ON, 1, 0, 60 + i, -1, 0.8888);

            writeClapEventToPipe(&enote);
            if (i == 5)
            {
                auto automev = xenakios::make_event_param_value(0, 666, 0.42, nullptr);
                writeClapEventToPipe(&automev);
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
                writeClapEventToPipe(&mev);
            }
            Sleep(250);
        }
    }
    else
    {
        while (true)
        {
            int key = -1;
            std::cin >> key;
            if (key >= 0 && key < 128)
            {
                auto nev = xenakios::make_event_note(0, CLAP_EVENT_NOTE_ON, 0, 0, key, -1, 0.5);
                writeClapEventToPipe(&nev);
            }
            if (key == -1)
            {
                break;
            }
        }
    }
    // Close the pipe (automatically disconnects client too)
    CloseHandle(pipe);
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

    std::vector<int> resultbuffer;
    std::vector<char> buffer;
    buffer.resize(1024);
    while (true)
    {
        // The read operation will block until there is data to read
        DWORD numBytesRead = 0;
        BOOL result = ReadFile(pipe,
                               buffer.data(), // the data from the pipe will be put here
                               sizeof(clap_event_header),
                               &numBytesRead, // this will store number of bytes actually read
                               NULL           // not using overlapped IO
        );

        if (result)
        {
            // std::cout << "Number of bytes read: " << numBytesRead << "\n";
            /*
            // we should probably implement the message magic properly, but not now...
            uint64_t magic = 0;
            memcpy(&magic, buffer.data(), sizeof(uint64_t));
            if (magic != messageMagic)
            {
                std::cout << "message magic check failed!\n";
                CloseHandle(pipe);
                return;
            }
            */
            clap_event_header *h = (clap_event_header *)buffer.data();
            if (h->space_id == CLAP_CORE_EVENT_SPACE_ID)
            {
                int bytestoread = h->size - sizeof(clap_event_header);
                result = ReadFile(
                    pipe,
                    buffer.data() +
                        sizeof(clap_event_header), // the data from the pipe will be put here
                    bytestoread,                   // number of bytes allocated
                    &numBytesRead,                 // this will store number of bytes actually read
                    NULL);                         // not using overlapped IO
                if (result && bytestoread == numBytesRead)
                {
                    if (h->type == CLAP_EVENT_NOTE_ON && h->size == sizeof(clap_event_note))
                    {
                        clap_event_note *nev = (clap_event_note *)buffer.data();
                        std::cout << "received clap note on event " << nev->key << " "
                                  << nev->velocity << "\n";
                    }
                    else if (h->type == CLAP_EVENT_PARAM_VALUE &&
                             h->size == sizeof(clap_event_param_value))
                    {
                        clap_event_param_value *pev = (clap_event_param_value *)buffer.data();
                        std::cout << "received clap param value event " << pev->param_id << " "
                                  << pev->value << "\n";
                    }
                    else
                    {
                        std::print("unhandled clap event of type {} size {}\n", h->type, h->size);
                    }
                }
                else
                {
                    std::cout << "failed to read clap concrete event from pipe\n";
                }
            }
        }
        else
        {
            std::cout << "Failed to read clap header from the pipe/end of messages\n";
            break;
        }
    }
    /*
    std::cout << "result buffer size is " << resultbuffer.size() << "\n";
    choc::hash::xxHash32 hash;
    hash.hash(resultbuffer.data(), sizeof(int) * resultbuffer.size());
    std::cout << "test data hash on receiver side is " << hash.getHash() << "\n";
    */
    // Close our pipe handle
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
