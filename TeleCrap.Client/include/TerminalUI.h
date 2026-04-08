#pragma once
#include <string>
#include <ctime>

#include <telecrap/Models.h>
#include <telecrap/Transport.h>

/// <summary>
/// Терминальный интерфейс клиента: рендер, команды и обработка событий ввода.
/// </summary>
class TerminalUI
{
public:
    /// <summary>
    /// Запускает главный цикл UI.
    /// </summary>
    static void run(Transport* transportSocket);

    /// <summary>
    /// Производит авторизацию по имени и паролю
    /// </summary>
    /// <param name="username"></param>
    /// <param name="password"></param>
    static void tryAuth(Transport* transport, std::string& username, std::string& password);

    /// <summary>
    /// Выполняет полную перерисовку интерфейса.
    /// </summary>
    static void drawUI();

    /// <summary>
    /// Обрабатывает текстовую команду пользователя.
    /// </summary>
    static bool processCommand(const std::string& command);

    /// <summary>
    /// Добавляет в UI новое сообщение чата.
    /// </summary>
    static void addMessage(const Message& message);

    /// <summary>
    /// Добавляет в UI системное текстовое сообщение.
    /// </summary>
    static void addMessage(const std::string& content);

    /// <summary>
    /// Обновляет UI после переименования чата.
    /// </summary>
    static void onChatRenamed(const Chat& chat);

    /// <summary>
    /// Обновляет UI после исключения участника из чата.
    /// </summary>
    static void onUserKickedFromChat(const Chat& chat, const User& target);

    /// <summary>
    /// Обновляет UI после бана участника.
    /// </summary>
    static void onUserBannedFromChat(const Chat& chat, const User& target);

    /// <summary>
    /// Обновляет UI после разбана участника.
    /// </summary>
    static void onUserUnbannedFromChat(const Chat& chat, const User& target);

    /// <summary>
    /// Возвращает признак активного UI-цикла.
    /// </summary>
    static bool isRunning();
    /// <summary>
    /// Запрашивает остановку UI-цикла.
    /// </summary>
    static void stopRunning();

    /// <summary>
    /// Хук перерисовки от платформенного слоя.
    /// </summary>
    static void hookRender(Transport* transportSocket);

    /// <summary>
    /// Хук ввода символа.
    /// </summary>
    static void hookInputChar(char c);

    /// <summary>
    /// Хук backspace.
    /// </summary>
    static void hookBackspace();

    /// <summary>
    /// Хук Enter.
    /// </summary>
    static void hookEnter();

    /// <summary>
    /// Хук стрелки вверх.
    /// </summary>
    static void hookArrowUp();

    /// <summary>
    /// Хук стрелки вниз.
    /// </summary>
    static void hookArrowDown();

    /// <summary>
    /// Хук Escape.
    /// </summary>
    static void hookEscape();

    /// <summary>
    /// Платформенно-зависимая запись строки в stdout.
    /// </summary>
    static void platformWriteStdout(const std::string& s);

    /// <summary>
    /// Получает текущий размер терминала.
    /// </summary>
    static void platformGetScreenSize(int& width, int& height);

    /// <summary>
    /// Форматирует timestamp в человекочитаемое время.
    /// </summary>
    static std::string platformGetTimeString(time_t timestamp);
};
