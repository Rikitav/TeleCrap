#pragma once

#include <string>
#include <cstdint>

#ifdef _WIN32
    // Windows
    #include <WinSock2.h>
    #include <Windows.h>

    typedef int32_t socklen_t;

    /*
    #if defined(_WIN64)
    typedef uint64_t SockHandle_t;
    typedef int32_t SockLen_t;
    
    #else
    typedef uint32_t SockHandle_t;
    typedef int32_t SockLen_t;
    #endif
    */

#else
    // Linux (POSIX)
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <cerrno>

    typedef int SOCKET;
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR (-1)
    
    typedef uint16_t u_short;
#endif

/// <summary>
/// Утилитный класс для работы с сокетами: установка рукопожатий, отправка/прием данных и получение информации о соединении.
/// </summary>
class SocketHelper
{
public:
    /// <summary>
    /// Статическая переменная типа u_long, содержащая адрес сервера.
    /// </summary>
    static u_long ServerAddress;

    /// <summary>
    /// Статическая переменная, представляющая номер порта сервера.
    /// </summary>
    static u_short ServerPort;

    /// <summary>
    /// Открывает сокет со стороны сервера.
    /// </summary>
    /// <returns>Дескриптор SOCKET сервера, с максимальным колчиеством возможных слушателей</returns>
    static SOCKET OpenHandshake();

    /// <summary>
    /// Устанавливает соединение и выполняет процедуру рукопожатия, возвращая сокет подключения.
    /// </summary>
    /// <returns>SOCKET — дескриптор установленного соединения. В случае ошибки возвращается значение, генерирует исключение request_error.</returns>
    static SOCKET ConnectHandshake();

    /// <summary>
    /// Формирует строковый идентификатор IPv4 адреса SOCKET слушателя.
    /// </summary>
    /// <param name="socketInfo">Указатель на дексриптор SOCKET.</param>
    /// <returns>std::string — конечный IPv4 адрес сокета слушателя в виде строки.</returns>
    static std::string SockIdentificatorOf(const SOCKET* socketInfo);

    /// <summary>
    /// Извлекает адрес сокета из предоставленного SOCKET слушателя.
    /// </summary>
    /// <param name="socketInfo">Указатель на константную структуру SOCKET, содержащую информацию о сокете (например, адрес и порт).</param>
    /// <returns>Адрес сокета в виде u_long (обычно IPv4-адрес, представленный числом; порядок байтов зависит от реализации/контекста).</returns>
    static u_long SockAddressOf(const SOCKET* socketInfo);

    /// <summary>
    /// Возвращает номер порта, связанный с указанным SOCKET слушателя.
    /// </summary>
    /// <param name="socketInfo">Указатель на структуру SOCKET, содержащую информацию о сокете.</param>
    /// <returns>Номер порта (u_short), связанный с указанным сокетом.</returns>
    static u_short SockPortOf(const SOCKET* socketInfo);
    
    /// <summary>
    /// Формирует строковый идентификатор IPv4 адреса SOCKET удалённого клиента.
    /// </summary>
    /// <param name="socketInfo">Указатель на дексриптор SOCKET</param>
    /// <returns>std::string — конечный IPv4 адрес сокета удалённого клиента в виде строки.</returns>
    static std::string PeerIdentificatorOf(const SOCKET* socketInfo);

    /// <summary>
    /// Извлекает адрес сокета из предоставленной структуры SOCKET удалённого клиента.
    /// </summary>
    /// <param name="socketInfo">Указатель на константную структуру SOCKET, содержащую информацию о сокете (например, адрес и порт).</param>
    /// <returns>Адрес сокета в виде u_long (обычно IPv4-адрес, представленный числом; порядок байтов зависит от реализации/контекста).</returns>
    static u_long PeerAddressOf(const SOCKET* socketInfo);

    /// <summary>
    /// Возвращает номер порта, связанный с указанным SOCKET удалённого клиента.
    /// </summary>
    /// <param name="socketInfo">Указатель на структуру SOCKET, содержащую информацию о сокете.</param>
    /// <returns>Номер порта (u_short), связанный с указанным сокетом.</returns>
    static u_short PeerPortOf(const SOCKET* socketInfo);

    /// <summary>
    /// Отправляет данные через указанный транспортный сокет. Поддерживает аккумулирование данных при разрывных пакетах
    /// </summary>
    /// <typeparam name="T">Тип буфера или контейнера с данными, которые будут отправлены.</typeparam>
    /// <param name="transportSocket">Указатель на транспортный сокет, через который отправляются данные.</param>
    /// <param name="buffer">Константная ссылка на буфер или объект, содержащий данные для отправки.</param>
    /// <returns>Значение типа sockerr_t, обозначающее результат операции (успех или код ошибки).</returns>
    template <typename T>
    static sockerr_t SendData(const SOCKET* transportSocket, const T& buffer);

    /// <summary>
    /// Получает данные из транспортного сокета и записывает их в указанный буфер. Поддерживает аккумулирование данных при разрывных пакетах
    /// </summary>
    /// <typeparam name="T">Тип буфера или контейнера, используемого для хранения принятых данных.</typeparam>
    /// <param name="transportSocket">Указатель на транспортный сокет, из которого будут прочитаны данные.</param>
    /// <param name="buffer">Ссылка на буфер/контейнер типа T, в который будут сохранены полученные данные.</param>
    /// <returns>sockerr_t — код результата операции приема, указывающий на успех или описание ошибки сокета.</returns>
    template <typename T>
    static sockerr_t ReceiveData(const SOCKET* transportSocket, T& buffer);

    /// <summary>
    /// Отправляет весь буфер через указанный сокет, пытаясь передать все данные или вернуть ошибку при невозможности отправки.
    /// </summary>
    /// <typeparam name="T">Тип буфера или контейнера с данными для отправки. Ожидается, что он обеспечивает доступ к данным и их длине.</typeparam>
    /// <param name="transportSocket">Указатель на транспортный сокет, через который выполняется отправка данных.</param>
    /// <param name="buffer">Константная ссылка на буфер с данными для отправки. Тип T должен предоставлять доступ к содержимому и его размеру (например, через методы data()/size() или эквивалент).</param>
    /// <returns>Значение типа sockerr_t, отражающее результат операции отправки. Нулевое значение обычно означает успех, ненулевое — произошла ошибка при отправке.</returns>
    template <typename T>
    static sockerr_t SendExactly(const SOCKET* transportSocket, const T& buffer);

    /// <summary>
    /// Считывает ровно столько байт, сколько требует buffer, из указанного сокета, повторяя/блокируя операции чтения до получения всех данных или возникновения ошибки.
    /// </summary>
    /// <typeparam name="T">Тип буфера для приема. Должен предоставлять доступ к памяти и размер/количество байт для заполнения (например, std::vector<char>, std::string, std::span<char> или пользовательская структура с data()/size()).</typeparam>
    /// <param name="transportSocket">Указатель на SOCKET, из которого выполняется прием данных.</param>
    /// <param name="buffer">Ссылка на буфер, в который будут записаны принятые данные. Буфер должен обеспечивать доступ к памяти и информацию о размере (например, контейнер с плотным расположением элементов или структура с методами data() и size()).</param>
    /// <returns>Код ошибки sockerr_t, указывающий на результат операции: успех при успешном приеме всех запрошенных байт или соответствующий код ошибки при сбое.</returns>
    template <typename T>
    static sockerr_t ReceiveExactly(const SOCKET* transportSocket, T& buffer);

    /// <summary>
    /// Закрывает указанный сокет.
    /// </summary>
    /// <param name="socketInfo">Указатель на SOCKET, который должен быть закрыт. Поведение при передаче nullptr зависит от реализации.</param>
    static void Close(SOCKET* socketInfo);
};
