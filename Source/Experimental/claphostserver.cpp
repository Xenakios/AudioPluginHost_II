#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <memory>
#include <iostream>
#include <format>
#include "offlineclaphost.h"
#include "oscpkt.hh"
#include "udp.hh"

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
    eng->addProcessorToChain(R"(C:\Program Files\Common Files\CLAP\Surge Synth Team\Surge XT.clap)",
                             0);
    eng->startStreaming(132, 44100, 512, false);
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
                int iarg;
                float darg0;
                float darg1;
                float darg2;
                float darg3;
                bool barg0;
                if (msg->match("/ping").popInt32(iarg).isOkNoMoreArgs())
                {
                    std::cout << "Server: received /ping " << iarg << " from "
                              << sock.packetOrigin() << std::endl;
                    // Message repl;
                    // repl.init("/pong").pushInt32(iarg + 1);
                    // pw.init().addMessage(repl);
                    // sock.sendPacketTo(pw.packetData(), pw.packetSize(), sock.packetOrigin());
                }
                else if (msg->match("/play_note")
                             .popFloat(darg0)
                             .popFloat(darg1)
                             .popInt32(iarg)
                             .popFloat(darg2)
                             .isOkNoMoreArgs())
                {
                    std::cout << std::format("{} {} {} {}", darg0, darg1, iarg, darg2) << std::endl;
                    eng->postNoteMessage(darg0, darg1, iarg, darg2);
                }
                else if (msg->match("/show_gui").popBool(barg0).isOkNoMoreArgs())
                {
                    if (barg0)
                        eng->openPersistentWindow(0);
                    else
                        eng->closePersistentWindow(0);
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

int main()
{
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
