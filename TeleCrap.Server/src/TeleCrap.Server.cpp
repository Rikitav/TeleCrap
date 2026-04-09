#include <exception>
#include <new>
#include <thread>
#include <stdexcept>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <string>

#include <telecrap/Protocol.h>
#include <telecrap/Request.h>
#include <telecrap/Transport.h>
#include <telecrap/Models.h>
#include <telecrap/Console.h>
#include <telecrap/Log.h>

#include "../include/AddonManager.h"
#include "../include/Database.h"
#include "../include/Backend.h"
#include "../include/ServerCLI.h"

static void listenClientRequestsLoop(Transport* transport)
{
    while (Backend::isActive())
    {
        try
        {
            // Цикл обработки запросов одного клиента.
            // Поток блокируется на чтении из TCP, пока клиент не пришлет следующий Request.
            Request request = Protocol::GetRequest(*transport);
            Backend::processRequest(transport, request);
        }
        catch (disconnected_error)
        {
            Log::Info("Conn", "Client disconnected");
            break;
        }
        catch (const std::runtime_error& err)
        {
            Log::Error("Conn", std::string("Client loop runtime error: ") + err.what());
            break;
        }
        catch (...)
        {
            Log::Error("Conn", "Unknown error in request handler");
            break;
        }
    }

    Backend::removeClientSocket(transport);
    delete transport;
}

static void listenHandshakeRequestsLoop(Transport* handshakeTransport)
{
    while (Backend::isActive())
    {
        try
        {
            // Цикл приема новых подключений (handshake listener).
            // accept()+handshake выполняются синхронно; на каждый успешный handshake создается отдельный поток клиента.
            Transport* newUserTransport = Protocol::ListenHandshake(*handshakeTransport);

            std::thread(listenClientRequestsLoop, newUserTransport).detach();
            Backend::addClientSocket(newUserTransport);
        }
        catch (disconnected_error)
        {
            Log::Info("Conn", "Handshake client disconnected");
            continue;
        }
        catch (const std::runtime_error& err)
        {
            Log::Error("Conn", std::string("Handshake loop runtime error: ") + err.what());
            continue;
        }
    }

    Backend::stop();
    return;
}

static Transport* handshakeTransport = nullptr;

static void sigintHandler(int signum)
{
    Log::Info("Main", "Signal received, shutting down...");
    
    // graceful shutdown: останавливаем backend и закрываем listener.
    Backend::stop();
    if (handshakeTransport != nullptr)
        delete handshakeTransport;

    exit(0);
}

int main()
{
    try
    {
        Console::Init();
        Log::Info("Main", "TeleCrap Server is starting...");
        signal(SIGINT, sigintHandler);
        signal(SIGABRT, sigintHandler);
        srand(static_cast<unsigned int>(time(0)));

#ifdef _WIN32
        SetConsoleTitleA("TeleCrap Server v0.3");
#else
        std::cout << "\033]0;" << "TeleCrap Server v0.3" << "\007";
#endif

        Log::Info("Main", "Initializing services...");
        Backend::start();

        Database::Init();
        Transport::Init();
        AddonManager::Init();

        Log::Info("Main", "Starting handshake listener...");
		Transport* handshakeTransport = Transport::Server();
        std::thread(listenHandshakeRequestsLoop, handshakeTransport).detach();

        Log::Info("Main", "TeleCrap Server is now running");
		ServerCLI::run(handshakeTransport);

        delete handshakeTransport;
        return 0;
    }
    catch (const std::runtime_error& err)
    {
        Log::Error("Main", std::string("Critical error: ") + err.what());
        return 1;
    }
}