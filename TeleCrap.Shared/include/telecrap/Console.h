#pragma once
// Цветовые константы
enum class Color
{
    BLACK = 0,
    DARK_BLUE = 1,
    DARK_GREEN = 2,
    DARK_CYAN = 3,
    DARK_RED = 4,
    DARK_MAGENTA = 5,
    DARK_YELLOW = 6,
    LIGHT_GRAY = 7,
    DARK_GRAY = 8,
    BLUE = 9,
    GREEN = 10,
    CYAN = 11,
    RED = 12,
    MAGENTA = 13,
    YELLOW = 14,
    WHITE = 15
};

// Класс для работы с консолью
/// <summary>
/// Кроссплатформенные утилиты для управления терминалом.
/// </summary>
class Console
{
public:
    /// <summary>
    /// Инициализирует режимы консоли и ввод/вывод.
    /// </summary>
    static void Init();

    /// <summary>
    /// Возвращает ширину терминала в символах.
    /// </summary>
    static int getWidth();

    /// <summary>
    /// Возвращает высоту терминала в символах.
    /// </summary>
    static int getHeight();

    /// <summary>
    /// Устанавливает цвет вывода.
    /// </summary>
    static void setColor(Color color);

    /// <summary>
    /// Перемещает курсор в заданную позицию.
    /// </summary>
    static void setCursorPosition(int x, int y);
    
    /// <summary>
    /// Полностью очищает экран терминала.
    /// </summary>
    static void clearScreen();
    
    /// <summary>
    /// Рисует прямоугольную рамку указанного размера и цвета.
    /// </summary>
    static void drawBox(int x, int y, int width, int height, Color color);
};
