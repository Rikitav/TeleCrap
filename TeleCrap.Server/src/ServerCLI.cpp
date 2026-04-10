#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <mutex>
#include <atomic>
#include <algorithm>

#include <telecrap/Log.h>

#include "../include/ServerCLI.h"
#include "../include/Backend.h"
#include "../include/AddonManager.h"
#include "../include/Database.h"

static std::mutex cliMutex;
static std::string inputBuffer;
static std::atomic<bool> running{ true };
static std::vector<std::string> commandHistory;
static int historyIndex = -1;
static std::string historyDraft;

// --- УТИЛИТЫ ---
static void utf8PopBack(std::string& s)
{
    // Backspace для UTF-8: удаляем один codepoint (а не один байт).
    if (s.empty())
        return;

    size_t i = s.size();
    while (i > 0 && (static_cast<unsigned char>(s[i - 1]) & 0xC0) == 0x80) --i;
    
    if (i == 0)
        s.clear();
    else
        s.erase(i - 1);
}

static void renderPromptUnlocked()
{
    // Рендер промпта внизу терминала с сохранением/восстановлением позиции курсора (ANSI save/restore).
    std::cout << "\x1b[\0337";
    std::cout << "\x1b[999;1H\x1b[2K\r\x1b[92mserver> \x1b[0m" << inputBuffer << std::flush;
    std::cout << "\x1b[\0338";
}

bool ServerCLI::isRunning() { return running.load(); }
void ServerCLI::stop() { running = false; }

void ServerCLI::renderPrompt()
{
    if (!running)
        return;

    // Лочим общий Log::logMutex, чтобы вывод логов и prompt не "перемешивались" в stdout.
    std::lock_guard<std::mutex> lk(Log::logMutex);
    renderPromptUnlocked();
}

static void processCommand(const std::string& command)
{
    // Серверная CLI — локальная: команды не идут клиентам, а управляют процессом сервера.
    if (command.empty())
        return;

    std::vector<std::string> tokens;
    std::stringstream ss(command);
    std::string token;
    while (ss >> token) tokens.push_back(token);

    if (tokens[0] == "/help")
    {
        Log::Info("CLI", "Available server commands :");
        Log::Info("CLI", "  /help                    - show this message");
        Log::Info("CLI", "  /list-onlines            - list active connections");
        Log::Info("CLI", "  /halt                    - stop server process");
        Log::Info("CLI", "  /drop-conn [port]        - drop client by remote port");
        Log::Info("CLI", "  /reload-addons           - reload lua addons");
        Log::Info("CLI", "  /global-alert [message]  - broadcast system alert to online users");
        return;
    }

    if (tokens[0] == "/list-onlines" || tokens[0] == "/users")
    {
        const std::vector<OnlineClientInfo> clients = Backend::listOnlineClients();
        Log::Info("CLI", "Online connections: " + std::to_string(clients.size()));

        for (const OnlineClientInfo& c : clients)
        {
            std::string line = "  " + c.PeerAddress + ":" + std::to_string(c.PeerPort);
            if (c.HasUser)
            {
                const std::optional<UserInfo> user = Database::findUserById(c.UserId);
                line += " user=" + std::to_string(c.UserId);
                if (user.has_value())
                    line += "(@" + std::string(user->Name.c_str()) + ")";
            }
            else
            {
                line += " user=<handshake-only>";
            }

            Log::Info("CLI", line);
        }

        return;
    }

    if (tokens[0] == "/drop-conn")
    {
        if (tokens.size() < 2)
        {
            Log::Error("CLI", "Usage: /drop-conn [port]");
            return;
        }

        try
        {
            const unsigned long parsed = std::stoul(tokens[1]);
            if (parsed > 65535UL)
            {
                Log::Error("CLI", "Invalid port range");
                return;
            }

            const bool dropped = Backend::dropConnectionByPeerPort(static_cast<u_short>(parsed));
            if (dropped)
                Log::Info("CLI", "Connection dropped on port " + std::to_string(parsed));
            else
                Log::Error("CLI", "Connection not found for port " + std::to_string(parsed));
        }
        catch (...)
        {
            Log::Error("CLI", "Invalid port");
        }

        return;
    }

    if (tokens[0] == "/reload-addons")
    {
        try
        {
            AddonManager::Init();
            const auto commands = AddonManager::ListRegisteredCommands();
            Log::Info("CLI", "Addons reloaded. Registered commands: " + std::to_string(commands.size()));
        }
        catch (const std::exception& e)
        {
            Log::Error("CLI", std::string("Reload failed: ") + e.what());
        }
        return;
    }

    if (tokens[0] == "/global-alert")
    {
        if (tokens.size() < 2)
        {
            Log::Error("CLI", "Usage: /global-alert [message]");
            return;
        }

        const size_t pos = command.find(' ');
        const std::string text = (pos == std::string::npos) ? std::string() : command.substr(pos + 1);
        if (text.empty())
        {
            Log::Error("CLI", "Alert text cannot be empty");
            return;
        }

        const size_t delivered = Backend::pushGlobalAlert(text);
        Log::Info("CLI", "Global alert delivered to " + std::to_string(delivered) + " online user(s)");
        return;
    }

    if (tokens[0] == "/halt" || tokens[0] == "/stop" || tokens[0] == "/exit")
    {
        Log::Info("CLI", "Server is halting...");
        Backend::stop();
        ServerCLI::stop();
        return;
    }

    Log::Error("CLI", "Неизвестная команда. Введите /help");
}


void ServerCLI::hookInputChar(char c)
{
    std::lock_guard<std::mutex> lk(cliMutex);
    historyIndex = -1;
    historyDraft.clear();
    inputBuffer.push_back(c);
    renderPromptUnlocked();
}

void ServerCLI::hookBackspace()
{
    std::lock_guard<std::mutex> lk(cliMutex);
    historyIndex = -1;
    historyDraft.clear();
    if (!inputBuffer.empty())
    {
        utf8PopBack(inputBuffer);
        renderPromptUnlocked();
    }
}

void ServerCLI::hookArrowUp()
{
    std::lock_guard<std::mutex> lk(cliMutex);
    if (commandHistory.empty())
        return;

    if (historyIndex < 0)
    {
        historyDraft = inputBuffer;
        historyIndex = static_cast<int>(commandHistory.size()) - 1;
    }
    else if (historyIndex > 0)
    {
        --historyIndex;
    }

    inputBuffer = commandHistory[static_cast<size_t>(historyIndex)];
    renderPromptUnlocked();
}

void ServerCLI::hookArrowDown()
{
    std::lock_guard<std::mutex> lk(cliMutex);
    if (historyIndex < 0)
        return;

    if (historyIndex < static_cast<int>(commandHistory.size()) - 1)
    {
        ++historyIndex;
        inputBuffer = commandHistory[static_cast<size_t>(historyIndex)];
    }
    else
    {
        historyIndex = -1;
        inputBuffer = historyDraft;
        historyDraft.clear();
    }

    renderPromptUnlocked();
}

void ServerCLI::hookEnter()
{
    std::string line;
    {
        std::lock_guard<std::mutex> lk(cliMutex);
        line = inputBuffer;
        inputBuffer.clear();
    }

    if (!line.empty())
    {
        if (commandHistory.empty() || commandHistory.back() != line)
            commandHistory.push_back(line);
        if (commandHistory.size() > 100)
            commandHistory.erase(commandHistory.begin());
        historyIndex = -1;
        historyDraft.clear();

        std::cout << "\x1b[90m> " << line << "\x1b[0m";
        processCommand(line);
    }

    {
        std::lock_guard<std::mutex> lk(cliMutex);
        if (running) renderPromptUnlocked();
    }
}

void ServerCLI::hookEscape()
{
    std::lock_guard<std::mutex> lk(cliMutex);
    inputBuffer.clear();
    renderPromptUnlocked();
}