module;

#include <string>
#include <cstdint>
#include <vector>

#ifdef _WIN32

    // Windows
    #include <WinSock2.h>
    #include <Windows.h>
    #include <ws2tcpip.h>

    #pragma comment(lib, "ws2_32.lib")
#else

    // Linux (POSIX)
    #include <sys/types.h>
    #include <sys/sockhandle_t.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <cerrno>
#endif

export module telecrap:SocketHelper;

import :Models;

export namespace telecrap
{
    #ifdef _WIN32
        using socklen_t = int32_t;
        using sockhandle_t = SOCKET;
        using wsa_data_t = WSAData;
    
    #else
        using sockhandle_t = int;
        using u_short = uint16_t;
    
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
        /// Разрешает IPv4-адрес сервера по доменному имени или числовой записи (getaddrinfo). Windows и Linux.
        /// При успехе обновляет ServerAddress (сетевой порядок байт).
        /// </summary>
        static bool ResolveServerHost(const std::string& hostNameOrIPv4);
    
        /// <summary>
        /// Открывает сокет со стороны сервера.
        /// </summary>
        /// <returns>Дескриптор sockhandle_t сервера, с максимальным колчиеством возможных слушателей</returns>
        static sockhandle_t OpenHandshake();
    
        /// <summary>
        /// Устанавливает соединение и выполняет процедуру рукопожатия, возвращая сокет подключения.
        /// </summary>
        /// <returns>sockhandle_t — дескриптор установленного соединения. В случае ошибки возвращается значение, генерирует исключение request_error.</returns>
        static sockhandle_t ConnectHandshake();
    
        /// <summary>
        /// Формирует строковый идентификатор IPv4 адреса sockhandle_t слушателя.
        /// </summary>
        /// <param name="sockhandle_tInfo">Указатель на дексриптор sockhandle_t.</param>
        /// <returns>std::string — конечный IPv4 адрес сокета слушателя в виде строки.</returns>
        static std::string SockIdentificatorOf(const sockhandle_t* sockhandle_tInfo);
    
        /// <summary>
        /// Извлекает адрес сокета из предоставленного sockhandle_t слушателя.
        /// </summary>
        /// <param name="sockhandle_tInfo">Указатель на константную структуру sockhandle_t, содержащую информацию о сокете (например, адрес и порт).</param>
        /// <returns>Адрес сокета в виде u_long (обычно IPv4-адрес, представленный числом; порядок байтов зависит от реализации/контекста).</returns>
        static u_long SockAddressOf(const sockhandle_t* sockhandle_tInfo);
    
        /// <summary>
        /// Возвращает номер порта, связанный с указанным sockhandle_t слушателя.
        /// </summary>
        /// <param name="sockhandle_tInfo">Указатель на структуру sockhandle_t, содержащую информацию о сокете.</param>
        /// <returns>Номер порта (u_short), связанный с указанным сокетом.</returns>
        static u_short SockPortOf(const sockhandle_t* sockhandle_tInfo);
        
        /// <summary>
        /// Формирует строковый идентификатор IPv4 адреса sockhandle_t удалённого клиента.
        /// </summary>
        /// <param name="sockhandle_tInfo">Указатель на дексриптор sockhandle_t</param>
        /// <returns>std::string — конечный IPv4 адрес сокета удалённого клиента в виде строки.</returns>
        static std::string PeerIdentificatorOf(const sockhandle_t* sockhandle_tInfo);
    
        /// <summary>
        /// Извлекает адрес сокета из предоставленной структуры sockhandle_t удалённого клиента.
        /// </summary>
        /// <param name="sockhandle_tInfo">Указатель на константную структуру sockhandle_t, содержащую информацию о сокете (например, адрес и порт).</param>
        /// <returns>Адрес сокета в виде u_long (обычно IPv4-адрес, представленный числом; порядок байтов зависит от реализации/контекста).</returns>
        static u_long PeerAddressOf(const sockhandle_t* sockhandle_tInfo);
    
        /// <summary>
        /// Возвращает номер порта, связанный с указанным sockhandle_t удалённого клиента.
        /// </summary>
        /// <param name="sockhandle_tInfo">Указатель на структуру sockhandle_t, содержащую информацию о сокете.</param>
        /// <returns>Номер порта (u_short), связанный с указанным сокетом.</returns>
        static u_short PeerPortOf(const sockhandle_t* sockhandle_tInfo);
    
        /// <summary>
        /// Отправляет данные через указанный транспортный сокет. Поддерживает аккумулирование данных при разрывных пакетах
        /// </summary>
        /// <typeparam name="T">Тип буфера или контейнера с данными, которые будут отправлены.</typeparam>
        /// <param name="transportsockhandle_t">Указатель на транспортный сокет, через который отправляются данные.</param>
        /// <param name="buffer">Константная ссылка на буфер или объект, содержащий данные для отправки.</param>
        /// <returns>Значение типа ErrorCode, обозначающее результат операции (успех или код ошибки).</returns>
        template <typename T>
        static ErrorCode SendData(const sockhandle_t* transportsockhandle_t, const T& buffer);
    
        /// <summary>
        /// Получает данные из транспортного сокета и записывает их в указанный буфер. Поддерживает аккумулирование данных при разрывных пакетах
        /// </summary>
        /// <typeparam name="T">Тип буфера или контейнера, используемого для хранения принятых данных.</typeparam>
        /// <param name="transportsockhandle_t">Указатель на транспортный сокет, из которого будут прочитаны данные.</param>
        /// <param name="buffer">Ссылка на буфер/контейнер типа T, в который будут сохранены полученные данные.</param>
        /// <returns>ErrorCode — код результата операции приема, указывающий на успех или описание ошибки сокета.</returns>
        template <typename T>
        static ErrorCode ReceiveData(const sockhandle_t* transportsockhandle_t, T& buffer);
    
        /// <summary>
        /// Отправляет весь буфер через указанный сокет, пытаясь передать все данные или вернуть ошибку при невозможности отправки.
        /// </summary>
        /// <typeparam name="T">Тип буфера или контейнера с данными для отправки. Ожидается, что он обеспечивает доступ к данным и их длине.</typeparam>
        /// <param name="transportsockhandle_t">Указатель на транспортный сокет, через который выполняется отправка данных.</param>
        /// <param name="buffer">Константная ссылка на буфер с данными для отправки. Тип T должен предоставлять доступ к содержимому и его размеру (например, через методы data()/size() или эквивалент).</param>
        /// <returns>Значение типа ErrorCode, отражающее результат операции отправки. Нулевое значение обычно означает успех, ненулевое — произошла ошибка при отправке.</returns>
        template <typename T>
        static ErrorCode SendExactly(const sockhandle_t* transportsockhandle_t, const T& buffer);
    
        /// <summary>
        /// Считывает ровно столько байт, сколько требует buffer, из указанного сокета, повторяя/блокируя операции чтения до получения всех данных или возникновения ошибки.
        /// </summary>
        /// <typeparam name="T">Тип буфера для приема. Должен предоставлять доступ к памяти и размер/количество байт для заполнения (например, std::vector<char>, std::string, std::span<char> или пользовательская структура с data()/size()).</typeparam>
        /// <param name="transportsockhandle_t">Указатель на sockhandle_t, из которого выполняется прием данных.</param>
        /// <param name="buffer">Ссылка на буфер, в который будут записаны принятые данные. Буфер должен обеспечивать доступ к памяти и информацию о размере (например, контейнер с плотным расположением элементов или структура с методами data() и size()).</param>
        /// <returns>Код ошибки ErrorCode, указывающий на результат операции: успех при успешном приеме всех запрошенных байт или соответствующий код ошибки при сбое.</returns>
        template <typename T>
        static ErrorCode ReceiveExactly(const sockhandle_t* transportsockhandle_t, T& buffer);
    
        /// <summary>
        /// Отправляет произвольный буфер байт.
        /// </summary>
        static ErrorCode SendBuffer(const sockhandle_t* transportsockhandle_t, const uint8_t* data, size_t size);
    
        /// <summary>
        /// Считывает указанное количество байт в буфер.
        /// </summary>
        static ErrorCode ReceiveBuffer(const sockhandle_t* transportsockhandle_t, uint8_t* data, size_t size);
    
        /// <summary>
        /// Закрывает указанный сокет.
        /// </summary>
        /// <param name="sockhandle_tInfo">Указатель на sockhandle_t, который должен быть закрыт. Поведение при передаче nullptr зависит от реализации.</param>
        static void Close(sockhandle_t* sockhandle_tInfo);
    };
}
