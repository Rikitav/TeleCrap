#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <mutex>
#include <atomic>

#include <telecrap/Log.h>

#include "../include/ServerCLI.h"

static std::mutex cliMutex;
static std::string inputBuffer;
static std::atomic<bool> running{ true };

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
        Log::Info("CLI", "  /help   - sho this mesage");
        Log::Info("CLI", "  /users  - lsit online users");
        Log::Info("CLI", "  /stop   - halt the server");
        return;
    }

    if (tokens[0] == "/stop" || tokens[0] == "/exit")
    {
        Log::Info("CLI", "Server is halting...");
        ServerCLI::stop();
        return;
    }

    Log::Error("CLI", "Неизвестная команда. Введите /help");
}


void ServerCLI::hookInputChar(char c)
{
    std::lock_guard<std::mutex> lk(cliMutex);
    inputBuffer.push_back(c);
    renderPromptUnlocked();
}

void ServerCLI::hookBackspace()
{
    std::lock_guard<std::mutex> lk(cliMutex);
    if (!inputBuffer.empty())
    {
        utf8PopBack(inputBuffer);
        renderPromptUnlocked();
    }
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