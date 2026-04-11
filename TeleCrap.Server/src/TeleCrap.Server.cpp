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
#include "../include/ini.h"

struct ServerSettings
{
    uint16_t ListenPort;
    std::string DbConnection;
};

static std::string CONFIG_FILE = "settings.cfg";
static ServerSettings Settings{};
static Transport* handshakeTransport = nullptr;

static int safe_stoi(const std::string& str, int defaultValue, size_t* idx = nullptr, int base = 10)
{
    try
    {
        return std::stoi(str);
    }
    catch (...)
    {
        return defaultValue;
    }
}

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
        catch (disconnected_error&)
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
        catch (disconnected_error&)
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

static void sigintHandler(int signum)
{
    Log::Info("Main", "Signal received, shutting down...");
    
    // graceful shutdown: останавливаем backend и закрываем listener.
    Backend::stop();
    if (handshakeTransport != nullptr)
        delete handshakeTransport;

    exit(0);
}

static void configSave()
{
    mINI::INIFile file(CONFIG_FILE);
    mINI::INIStructure ini;

    ini["Server"]["ListenPort"] = std::to_string(Settings.ListenPort);
    ini["Server"]["DbConnection"] = Settings.DbConnection;

    if (!file.generate(ini, true))
        throw std::runtime_error("Could not save config file");
}

static void configLoad()
{
    mINI::INIFile file(CONFIG_FILE);
    mINI::INIStructure ini;

    if (file.read(ini))
    {
        Settings.ListenPort = static_cast<uint16_t>(safe_stoi(ini["Server"]["ListenPort"], 7777));
        Settings.DbConnection = ini["Server"]["DbConnection"];

        Log::Info("Config", "Настройки успешно загружены из " + CONFIG_FILE);
    }
    else
    {
        // Файла нет. Заполняем дефолтными значениями
        Log::Info("Config", "Файл не найден. Создаем конфиг по умолчанию...");

        Settings.ListenPort = 7777;
        Settings.DbConnection = "telecrap.db";

        configSave();
    }
}

static void parseArgs(int argc, char** argv)
{
    bool anyCfgChanges = false;
    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        if (arg == "-p")
        {
            if (argc == i - 1)
            {
                Log::Error("Main", "Specify port to listen");
                abort();
                return;
            }

            Settings.ListenPort = std::stol(argv[i + 1]);
            anyCfgChanges = true;

            argc += 1;
            continue;
        }
    }

    if (anyCfgChanges)
        configSave();
}

int main(int argc, char** argv)
{
    try
    {
        signal(SIGINT, sigintHandler);
        signal(SIGABRT, sigintHandler);
        srand(static_cast<unsigned int>(time(0)));

        if (argc > 1)
            parseArgs(argc, argv);

        configLoad();
        Console::Init();
        Log::SetPostWriteHook(&ServerCLI::postLogRenderPrompt);
        std::cout << "\033]0;" << "TeleCrap Server v0.6" << "\007";

        Log::Info("Main", "TeleCrap Server is starting...");
        SocketHelper::ServerPort = Settings.ListenPort;
        Database::DbConnection = Settings.DbConnection;

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