#include <cstdio>
#include <ctime>
#include <string>

#include "../include/TerminalUI.h"

#if defined(_WIN32)
#include <curses.h>
#else
#include <ncurses.h>
#endif

void TerminalUI::platformWriteStdout(const std::string& s)
{
    // Current renderer emits ANSI frame buffers. Keep writing them directly.
    (void)std::fwrite(s.data(), 1, s.size(), stdout);
    std::fflush(stdout);
}

void TerminalUI::platformGetScreenSize(int& width, int& height)
{
    int h = 0;
    int w = 0;
    getmaxyx(stdscr, h, w);
    width = (w < 20) ? 20 : w;
    height = (h < 8) ? 8 : h;
}

std::string TerminalUI::platformGetTimeString(time_t timestamp)
{
    struct tm timeinfo{};
#ifdef _WIN32
    localtime_s(&timeinfo, &timestamp);
#else
    localtime_r(&timestamp, &timeinfo);
#endif
    char buffer[16]{};
    strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeinfo);
    return std::string(buffer);
}

void TerminalUI::run(Transport* transportSocket)
{
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    timeout(50);
    curs_set(1);

    if (has_colors())
    {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_WHITE, -1);
        init_pair(2, COLOR_CYAN, -1);
        init_pair(3, COLOR_BLUE, -1);
        init_pair(4, COLOR_YELLOW, -1);
        init_pair(5, COLOR_GREEN, -1);
    }

    TerminalUI::hookRender(transportSocket);

    while (TerminalUI::isRunning())
    {
        int ch = getch();
        if (ch == ERR)
            continue;

        switch (ch)
        {
            case KEY_RESIZE:
                TerminalUI::hookRender(transportSocket);
                break;
            case KEY_UP:
                TerminalUI::hookArrowUp();
                break;
            case KEY_DOWN:
                TerminalUI::hookArrowDown();
                break;
            case 27:
                TerminalUI::hookEscape();
                break;
            case KEY_BACKSPACE:
            case 127:
            case 8:
                TerminalUI::hookBackspace();
                break;
            case '\n':
            case '\r':
            case KEY_ENTER:
                TerminalUI::hookEnter();
                break;
            default:
                if (ch >= 32 && ch <= 255)
                    TerminalUI::hookInputChar(static_cast<char>(ch));
                break;
        }
    }

    endwin();
}
