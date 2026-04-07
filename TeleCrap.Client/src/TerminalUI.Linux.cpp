#ifndef _WIN32

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <ctime>
#include <atomic>

#include "TerminalUI.h"

static std::atomic<bool> needRedraw{ false };

struct TermState { termios orig{}; bool active = false; };
struct Sig { static void winch(int) { needRedraw = true; } };

static void enableRaw(TermState& ts)
{
    if (tcgetattr(STDIN_FILENO, &ts.orig) != 0) return;
    termios raw = ts.orig;
    raw.c_iflag &= static_cast<tcflag_t>(~(BRKINT | ICRNL | INPCK | ISTRIP | IXON));
    raw.c_oflag &= static_cast<tcflag_t>(~(OPOST));
    raw.c_cflag |= static_cast<tcflag_t>(CS8);
    raw.c_lflag &= static_cast<tcflag_t>(~(ECHO | ICANON | IEXTEN | ISIG));
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0) ts.active = true;
};

static void disableRaw(TermState& ts)
{
    if (ts.active) tcsetattr(STDIN_FILENO, TCSAFLUSH, &ts.orig);
    ts.active = false;
};

// --- PLATFORM PROVIDERS (Реализация для Linux) ---

void TerminalUI::platformWriteStdout(const std::string& s)
{
    (void)::write(STDOUT_FILENO, s.data(), s.size());
}

void TerminalUI::platformGetScreenSize(int& width, int& height)
{
    struct winsize ws {};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0)
    {
        width = (std::max)(20, static_cast<int>(ws.ws_col));
        height = (std::max)(8, static_cast<int>(ws.ws_row));
        return;
    }
    width = 80;
    height = 24;
}

std::string TerminalUI::platformGetTimeString(time_t timestamp)
{
    struct tm timeinfo{};
    localtime_r(&timestamp, &timeinfo);
    char buffer[16]{};
    strftime(buffer, sizeof(buffer), "%H:%M:%S", &timeinfo);
    return std::string(buffer);
}

// --- MAIN LOOP (Реализация для Linux) ---

static void handleByteLinux(unsigned char b)
{
    if (b == 27) // ESC
    {
        unsigned char seq[2]{};
        const ssize_t n1 = ::read(STDIN_FILENO, &seq[0], 1);
        if (n1 <= 0) { TerminalUI::hookEscape(); return; }

        if (seq[0] == '[')
        {
            const ssize_t n2 = ::read(STDIN_FILENO, &seq[1], 1);
            if (n2 <= 0) return;
            switch (seq[1])
            {
                case 'A': TerminalUI::hookArrowUp(); break;
                case 'B': TerminalUI::hookArrowDown(); break;
            }
            return;
        }
        TerminalUI::hookEscape();
        return;
    }

    if (b == 127 || b == 8) { TerminalUI::hookBackspace(); return; }
    if (b == '\r' || b == '\n') { TerminalUI::hookEnter(); return; }
    if (b >= 32) { TerminalUI::hookInputChar(static_cast<char>(b)); }
}

void TerminalUI::run(Transport* transportSocket)
{
    TerminalUI::hookRender();

    TermState ts;
    struct sigaction sa {};
    sa.sa_handler = Sig::winch;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGWINCH, &sa, nullptr);

    enableRaw(ts);
    TerminalUI::platformWriteStdout("\x1b[?25l");

    while (TerminalUI::isRunning())
    {
        if (needRedraw.exchange(false))
            TerminalUI::hookRender();

        pollfd pfd{};
        pfd.fd = STDIN_FILENO;
        pfd.events = POLLIN;
        if (poll(&pfd, 1, 50) <= 0) continue;

        unsigned char buf[64]{};
        const ssize_t n = ::read(STDIN_FILENO, buf, sizeof(buf));
        for (ssize_t i = 0; i < n; ++i)
            handleByteLinux(buf[i]);
    }

    TerminalUI::platformWriteStdout("\x1b[0m\x1b[?25h\n");
    disableRaw(ts);
}

#endif // !_WIN32