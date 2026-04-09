#ifdef _WIN32
#include <Windows.h>
#include <ctime>
#include <string>

#include <telecrap/Console.h>

#include "../include/TerminalUI.h"

void TerminalUI::platformWriteStdout(const std::string& s)
{
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0;
    WriteFile(hOut, s.data(), static_cast<DWORD>(s.size()), &written, nullptr);
}

void TerminalUI::platformGetScreenSize(int& width, int& height)
{
    width = (std::max)(20, Console::getWidth());
    height = (std::max)(8, Console::getHeight());
}

std::string TerminalUI::platformGetTimeString(time_t timestamp)
{
    struct tm timeinfo{};
    localtime_s(&timeinfo, &timestamp);
    char buffer[16]{};
    strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeinfo);
    return std::string(buffer);
}

static void handleKeyEventWin(const KEY_EVENT_RECORD& ke)
{
    const UINT vk = ke.wVirtualKeyCode;
    const UINT ctrl = ke.dwControlKeyState;

    if (vk == VK_UP)          { TerminalUI::hookArrowUp(); return; }
    if (vk == VK_DOWN)        { TerminalUI::hookArrowDown(); return; }
    if (vk == VK_ESCAPE)      { TerminalUI::hookEscape(); return; }
    if (vk == VK_RETURN)      { TerminalUI::hookEnter(); return; }
    if (vk == VK_BACK)        { TerminalUI::hookBackspace(); return; }

    if (ke.uChar.UnicodeChar != 0 && !(ctrl & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)))
    {
        wchar_t wc = ke.uChar.UnicodeChar;
        if (wc >= 32u && wc != 127u)
        {
            char utf8[8]{};
            int n = WideCharToMultiByte(CP_UTF8, 0, &wc, 1, utf8, sizeof(utf8), nullptr, nullptr);
            for (int i = 0; i < n; ++i)
                TerminalUI::hookInputChar(utf8[i]);
        }
    }
}

void TerminalUI::run(Transport* transportSocket)
{
    TerminalUI::hookRender(transportSocket); // Первый рендер

    const HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    INPUT_RECORD records[32]{};

    while (TerminalUI::isRunning())
    {
        DWORD n = 0;
        if (!ReadConsoleInputW(hIn, records, 32, &n)) continue;

        for (DWORD i = 0; i < n; ++i)
        {
            INPUT_RECORD& rec = records[i];
            if (rec.EventType == WINDOW_BUFFER_SIZE_EVENT)
            {
                TerminalUI::hookRender(transportSocket);
                continue;
            }
            if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown)
            {
                handleKeyEventWin(rec.Event.KeyEvent);
            }
        }
    }

    Console::setColor(Color::WHITE);
    Console::setCursorPosition(0, Console::getHeight() - 1);
}

#endif // _WIN32