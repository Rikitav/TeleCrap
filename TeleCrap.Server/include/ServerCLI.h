#pragma once
#include <telecrap/Transport.h>

/// <summary>
/// Командная оболочка сервера для локального администрирования через терминал.
/// </summary>
class ServerCLI
{
public:
    /// <summary>
    /// Запускает цикл чтения пользовательского ввода в консоли сервера.
    /// </summary>
    static void run(Transport* transportSocket);

    /// <summary>
    /// Останавливает цикл CLI.
    /// </summary>
    static void stop();

    /// <summary>
    /// Возвращает признак активного цикла CLI.
    /// </summary>
    static bool isRunning();

    /// <summary>
    /// Перерисовывает prompt текущего ввода.
    /// </summary>
    static void renderPrompt();

    /// <summary>
    /// Обрабатывает введенный символ.
    /// </summary>
    static void hookInputChar(char c);

    /// <summary>
    /// Обрабатывает удаление символа.
    /// </summary>
    static void hookBackspace();

    /// <summary>
    /// Обрабатывает подтверждение команды (Enter).
    /// </summary>
    static void hookEnter();

    /// <summary>
    /// Обрабатывает Escape.
    /// </summary>
    static void hookEscape();

    /// <summary>
    /// Обрабатывает стрелку вверх.
    /// </summary>
    static void hookArrowUp() {}

    /// <summary>
    /// Обрабатывает стрелку вниз.
    /// </summary>
    static void hookArrowDown() {}
};