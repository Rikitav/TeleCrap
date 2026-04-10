#include <exception>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <csignal>
#include <thread>
#include <cstdlib>
#include <chrono>
#include <vector>
#include <ctime>

#ifdef _WIN32
#include "WinSock2.h"
#include "Windows.h"
#endif

#include <telecrap/Models.h>
#include <telecrap/Request.h>
#include <telecrap/Responce.h>
#include <telecrap/Update.h>
#include <telecrap/Transport.h>
#include <telecrap/SocketHelper.h>
#include <telecrap/Protocol.h>
#include <telecrap/Console.h>
#include <telecrap/Log.h>

#include "../include/ini.h"
#include "../include/TerminalUI.h"
#include "../include/MemoryCache.h"

struct ClientSettings
{
    std::string ServerIP;
    uint16_t ServerPort;
    std::string Username;
    std::string Password;
};

static std::string CONFIG_FILE = "settings.cfg";
static ClientSettings Settings{};
static Transport* transport = nullptr;

static bool runningUnderCmd()
{
#ifdef _WIN32
    DWORD processList[2];
    DWORD processCount = GetConsoleProcessList(processList, 2);
    return processCount > 1;
#else
    return true;
#endif
}

static void sigintHandler(int signum)
{
    std::cout << "\n[Main] Получен сигнал прерывания, завершаем работу..." << std::endl;
    delete transport;
    exit(0);
}

static void processUpdate(const Update& update)
{
    // Update'ы приходят от сервера через периодический GetUpdates (polling).
    // Здесь происходит развилка на UI-реакции и обновление локального кэша.
    switch (update.Type)
    {
        case UpdateType::Message:
        {
            const Message& m = update.MessageSent;
            const bool serverBroadcast = (m.DestChat.Id == 0) || (std::strcmp(m.DestChat.Name.c_str(), "server_alert") == 0);

            // Глобальное объявление сервера: не кладём в кэш чата (иначе коллизия id=1 с реальным чатом и optional::value() в UI).
            if (serverBroadcast)
            {
                const std::string line = std::string("[") + m.From.Name.c_str() + "] " + m.Text.c_str();
                TerminalUI::addMessage(line);
                break;
            }

            MemoryCache::storeMessageToChat(m.DestChat.Id, m, true);
            TerminalUI::addMessage(m);
            break;
        }

        case UpdateType::UserJoined:
        {
            MemoryCache::addMemberToChat(update.UserJoined.ChatModel.Id, update.UserJoined.UserModel);
            TerminalUI::addMessage(std::string(update.UserJoined.UserModel.Name.c_str()) + " присоединился к чату");
            break;
        }

        case UpdateType::UserKicked:
        {
            TerminalUI::onUserKickedFromChat(update.MemberEvent.ChatModel, update.MemberEvent.TargetUser);
            break;
        }

        case UpdateType::UserBanned:
        {
            TerminalUI::onUserBannedFromChat(update.MemberEvent.ChatModel, update.MemberEvent.TargetUser);
            break;
        }

        case UpdateType::UserUnbanned:
        {
            TerminalUI::onUserUnbannedFromChat(update.MemberEvent.ChatModel, update.MemberEvent.TargetUser);
            break;
        }

        case UpdateType::ChatRenamed:
        {
            TerminalUI::onChatRenamed(update.ChatRenamed.ChatModel);
            break;
        }
	}
}

static void listenUpdates(Transport* transport)
{
    /*
    try
    {
        Protocol::SendRequest(*transport, Request::CreateGetUpdates(transport->AccessToken, true));
    }
    catch (disconnected_error)
    {
        std::cout << "[Updates] Disconnected from server" << std::endl;
        return;
    }
    catch (const std::runtime_error& err)
    {
        std::cout << "[Updates] Runtime error : " << err.what() << std::endl;
        return;
	}
    */

    while (true)
    {
        try
        {
            // Простая модель доставки: раз в ~1s спрашиваем сервер о pending update'ах.
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            std::vector<Responce> updates = Protocol::SendRequestList(
                *transport, Request::CreateGetUpdates(transport->AccessToken, false),
                [](Responce& responce) { return responce.GetUpdates.RemainingUpdates; });

            for (Responce& responce : updates)
				processUpdate(responce.GetUpdates.CurrentUpdate);
        }
        catch (const request_error& err)
        {
            if (err.which() == ERR_UNAUTHORIZED)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(3000));
                continue;
            }

            std::cout << "[Updates] Runtime error : " << err.what() << std::endl;
            abort();
            break;
        }
        catch (disconnected_error&)
        {
            std::cout << "[Updates] Disconnected from server" << std::endl;
            abort();
            break;
        }
        catch (const std::runtime_error& err)
        {
            std::cout << "[Updates] Runtime error : " << err.what() << std::endl;
            continue;
        }
        catch (const std::exception& err)
        {
            std::cout << "[Updates] Error : " << err.what() << std::endl;
            continue;
        }
    }
}

static void configSave()
{
    mINI::INIFile file(CONFIG_FILE);
    mINI::INIStructure ini;

    ini["Network"]["ServerIP"] = Settings.ServerIP;
    ini["Network"]["ServerPort"] = std::to_string(Settings.ServerPort);
    ini["User"]["Username"] = Settings.Username;
    ini["User"]["Password"] = Settings.Password;

    if (!file.generate(ini, true))
        throw std::runtime_error("Could not save config file");
}

static void configLoad()
{
    mINI::INIFile file(CONFIG_FILE);
    mINI::INIStructure ini;

    if (file.read(ini))
    {
        Settings.ServerIP = ini["Network"]["ServerIP"];

        try
        {
            Settings.ServerPort = static_cast<uint16_t>(std::stoi(ini["Network"]["ServerPort"]));
        }
        catch (...)
        {
            Settings.ServerPort = 7777;
        }

        Settings.Username = ini["User"]["Username"];
        Settings.Password = ini["User"]["Password"];

        Log::Info("Config", "Настройки успешно загружены из " + CONFIG_FILE);
    }
    else
    {
        // Файла нет. Заполняем дефолтными значениями
        Log::Info("Config", "Файл не найден. Создаем конфиг по умолчанию...");

        Settings.ServerIP = "127.0.0.1";
        Settings.ServerPort = 7777;
        Settings.Username = "";
        Settings.Password = "";

        configSave();
    }
}

int main(int argc, char** argv)
{
    try
    {
		signal(SIGINT, sigintHandler);
        signal(SIGABRT, sigintHandler);
        srand(static_cast<unsigned int>(time(0)));

#ifdef _WIN32
        SetConsoleTitleA("TeleCrap Messenger v0.5");
#else
        std::cout << "\033]0;" << "TeleCrap Messenger v0.5" << "\007";
#endif

		Console::Init();
        configLoad();

        Log::Info("Main", "Инициализация сервисов...");
        Transport::Init();
        
        Log::Info("Main", "Подключение к серверу : " + Settings.ServerIP + ":" + std::to_string(Settings.ServerPort));
        SocketHelper::ServerPort = Settings.ServerPort;
        if (!SocketHelper::ResolveServerHost(Settings.ServerIP))
            throw std::runtime_error("Не удалось разрешить адрес сервера (DNS / IPv4): \"" + Settings.ServerIP + "\"");
        
        transport = Transport::Client();
		std::thread(listenUpdates, transport).detach();

        if (Settings.Username != "" && Settings.Password != "")
        {
            Log::Info("Main", "Попытка автоматической авторизации...");
            TerminalUI::tryAuth(transport, Settings.Username, Settings.Password);
        }

        Log::Info("Main", "TeleCrap клиент активен!");
        TerminalUI::run(transport);
        
        if (!runningUnderCmd())
        {
            Log::Trace("Main", "Нажмите ENTER для выхода...");
            std::cin.get();
        }
        
        delete transport;
        return 0;
    }
    catch (const std::runtime_error& err)
    {
		Console::clearScreen();
        Log::Error("Main", "Runtime error : " + std::string(err.what()));

        if (transport != nullptr)
            delete transport;

        if (!runningUnderCmd())
        {
            Log::Trace("Main", "Нажмите ENTER для выхода...");
            std::cin.get();
        }

        return 1;
    }
}
