#include "pch.h"

#include <iostream>

#ifdef _WIN32
#include <Windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#include <clocale>
#endif

#include <telecrap/Console.h>

#ifdef _WIN32
static const HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
#endif

void Console::Init()
{
#ifdef _WIN32
    // Устанавливаем кодовую страницу UTF-8 для поддержки русского
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);

    // Включаем поддержку виртуального терминала для ANSI кодов
    DWORD dwMode = 0;
    GetConsoleMode(hConsole, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hConsole, dwMode);

    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD inMode = 0;
    if (GetConsoleMode(hIn, &inMode))
        SetConsoleMode(hIn, inMode | ENABLE_WINDOW_INPUT);

    // Скрываем курсор
    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(hConsole, &cursorInfo);
    cursorInfo.bVisible = FALSE;
    SetConsoleCursorInfo(hConsole, &cursorInfo);
#else
    std::setlocale(LC_ALL, "");
    std::cout << "\033[?25l";
    std::cout.flush();
#endif
}

void Console::setColor(Color color)
{
#ifdef _WIN32
    SetConsoleTextAttribute(hConsole, static_cast<int>(color));
#else
    // Маппинг стандартных DOS-цветов (0-15) в ANSI-коды.
    // Если ваш enum Color устроен иначе, таблицу ansi_map нужно будет адаптировать.
    int c = static_cast<int>(color);
    static const int ansi_map[] = { 30, 34, 32, 36, 31, 35, 33, 37 };

    int ansiColor = ansi_map[c % 8];
    bool isBright = c > 7;

    std::cout << "\033[" << (isBright ? "1;" : "0;") << ansiColor << "m";
#endif
}

void Console::setCursorPosition(int x, int y)
{
#ifdef _WIN32
    COORD coord{};
    coord.X = x;
    coord.Y = y;
    SetConsoleCursorPosition(hConsole, coord);
#else
    // ANSI использует индексацию с 1, поэтому прибавляем 1 к x и y
    std::cout << "\033[" << (y + 1) << ";" << (x + 1) << "H";
    std::cout.flush();
#endif
}

void Console::clearScreen()
{
#ifdef _WIN32
    COORD tl = { 0,0 };
    CONSOLE_SCREEN_BUFFER_INFO s;
    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleScreenBufferInfo(console, &s);
    DWORD written, cells = s.dwSize.X * s.dwSize.Y;
    FillConsoleOutputCharacter(console, ' ', cells, tl, &written);
    FillConsoleOutputAttribute(console, s.wAttributes, cells, tl, &written);
    SetConsoleCursorPosition(console, tl);
#else
    // \033[2J очищает экран
    // \033[H возвращает курсор в (0, 0)
    std::cout << "\033[2J\033[H";
    std::cout.flush();
#endif
}

int Console::getWidth()
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    return csbi.srWindow.Right - csbi.srWindow.Left + 1;
#else
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    return w.ws_col;
#endif
}

int Console::getHeight()
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    return csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
#else
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    return w.ws_row;
#endif
}

void Console::drawBox(int x, int y, int width, int height, Color color)
{
    setColor(color);
    for (int i = 0; i < width; i++)
    {
        setCursorPosition(x + i, y);
        std::wcout << L"─";
        setCursorPosition(x + i, y + height - 1);
        std::wcout << L"─";
    }

    for (int i = 0; i < height; i++)
    {
        setCursorPosition(x, y + i);
        std::wcout << L"│";
        setCursorPosition(x + width - 1, y + i);
        std::wcout << L"│";
    }

    setCursorPosition(x, y);
    std::wcout << L"┌";
    setCursorPosition(x + width - 1, y);
    std::wcout << L"┐";
    setCursorPosition(x, y + height - 1);
    std::wcout << L"└";
    setCursorPosition(x + width - 1, y + height - 1);
    std::wcout << L"┘";

    // Форсируем вывод буфера, так как мы не используем std::endl
    std::wcout.flush();
}