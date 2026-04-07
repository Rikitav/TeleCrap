// File note: Windows-адаптер серверной CLI: чтение WinAPI событий консоли и маршрутизация в ServerCLI hooks.

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include <Windows.h>
#include <ctime>

#include <telecrap/Transport.h>
#include <telecrap/Console.h>

#include "../include/ServerCLI.h"

static void handleKeyEventWin(const KEY_EVENT_RECORD& ke)
{
    const UINT vk = ke.wVirtualKeyCode;
    const UINT ctrl = ke.dwControlKeyState;

    if (vk == VK_UP)
    {
        ServerCLI::hookArrowUp();
        return;
    }

    if (vk == VK_DOWN)
    {
        ServerCLI::hookArrowDown();
        return;
    }

    if (vk == VK_ESCAPE)
    {
        ServerCLI::hookEscape();
        return;
    }

    if (vk == VK_RETURN) 
    {
        ServerCLI::hookEnter();
        return;
    }

    if (vk == VK_BACK)
    {
        ServerCLI::hookBackspace();
        return;
    }

    if (ke.uChar.UnicodeChar != 0 && !(ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)))
    {
        wchar_t wc = ke.uChar.UnicodeChar;
        if (wc >= 32u && wc != 127u)
        {
            char utf8[8]{};
            int n = WideCharToMultiByte(CP_UTF8, 0, &wc, 1, utf8, sizeof(utf8), nullptr, nullptr);

            for (int i = 0; i < n; ++i)
                ServerCLI::hookInputChar(utf8[i]);
        }
    }
}

void ServerCLI::run(Transport* transportSocket)
{
    ServerCLI::renderPrompt(); // Первый рендер
    const HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    INPUT_RECORD records[32]{};

    while (ServerCLI::isRunning())
    {
        DWORD n = 0;
        if (!ReadConsoleInputW(hIn, records, 32, &n)) continue;

        for (DWORD i = 0; i < n; ++i)
        {
            INPUT_RECORD& rec = records[i];
            if (rec.EventType == WINDOW_BUFFER_SIZE_EVENT)
            {
                ServerCLI::renderPrompt();
                continue;
            }

            if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown)
            {
                handleKeyEventWin(rec.Event.KeyEvent);
            }
        }
    }

    //Console::setColor(Color::WHITE);
    //Console::setCursorPosition(0, Console::getHeight() - 1);
}

#endif // _WIN32