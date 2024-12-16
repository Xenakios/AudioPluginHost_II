

#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS

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
// #include <namedpipeapi.h>

const char *g_pipename = "\\\\.\\pipe\\my_pipe";

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

    // The read operation will block until there is data to read
    std::vector<int> resultbuffer;
    std::vector<char> buffer;
    buffer.resize(1024);
    while (true)
    {

        DWORD numBytesRead = 0;
        BOOL result = ReadFile(pipe,
                               buffer.data(), // the data from the pipe will be put here
                               buffer.size(), // number of bytes allocated
                               &numBytesRead, // this will store number of bytes actually read
                               NULL           // not using overlapped IO
        );

        if (result)
        {
            // buffer[numBytesRead / sizeof(wchar_t)] = '\0'; // null terminate the string
            std::cout << "Number of bytes read: " << numBytesRead << "\n";
            int numInts = numBytesRead / sizeof(int);
            for (int i = 0; i < numInts; ++i)
            {
                int *iptr = (int *)&buffer[i * sizeof(int)];
                resultbuffer.push_back(*iptr);
            }
            // std::cout << "Message: " << buffer << "\n";
        }
        else
        {
            std::cout << "Failed to read data from the pipe.\n";
            break;
        }
    }
    std::cout << "result buffer size is " << resultbuffer.size() << "\n";
    choc::hash::xxHash32 hash;
    hash.hash(resultbuffer.data(), sizeof(int) * resultbuffer.size());
    std::cout << "test data hash on receiver side is " << hash.getHash() << "\n";
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
        // sender
        std::vector<int> outputdata;
        std::mt19937 rng;
        std::uniform_int_distribution<int> dist(0, 10);
        int outdatalen = 4096;
        for (int i = 0; i < outdatalen; ++i)
            outputdata.push_back(dist(rng));
        choc::hash::xxHash32 hash;
        hash.hash(outputdata.data(), sizeof(int) * outputdata.size());
        std::cout << "test data hash is " << hash.getHash() << "\n";
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

        // This call blocks until a client process reads all the data

        DWORD numBytesWritten = 0;
        result = WriteFile(pipe,                            // handle to our outbound pipe
                           outputdata.data(),               // data to send
                           outputdata.size() * sizeof(int), // length of data to send (bytes)
                           &numBytesWritten,                // will store actual amount of data sent
                           NULL                             // not using overlapped IO
        );

        if (result)
        {
            std::cout << "Number of bytes sent: " << numBytesWritten << "\n";
        }
        else
        {
            std::cout << "Failed to send data.\n";
            // look up error code here using GetLastError()
        }

        // Close the pipe (automatically disconnects client too)
        CloseHandle(pipe);
    }
    else
    {
        // receiver
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
