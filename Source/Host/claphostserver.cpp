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
                    eng->postNoteMessage(darg0, darg1, iarg0, darg2);
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
