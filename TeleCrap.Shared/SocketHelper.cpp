#include "pch.h"

#include <cctype>
#include <stdexcept>
#include <string>

#ifndef _WIN32
#include <netdb.h>
#endif

/*
#ifdef _WIN32
    // Windows
    #include <WinSock2.h>
    #include <Windows.h>

#else
    // Linux (POSIX)
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <cerrno>
#endif
*/

#include "telecrap/SocketHelper.h"
#include "telecrap/Request.h"
#include "telecrap/Responce.h"
#include "telecrap/Protocol.h"

static SOCKET openSocket()
{
    // сокет для прослушки (ожидания) клиента и сокет для работы с сеансом
    // описываем (создаём) сокет для прослушки
    //      AF_INET - тип адреса в виде IP+порт
    //      SOCK_STREAM - тип сокета потоковый
    //      IPPROTO_TCP - протокол TCP
    return socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
}

static sockaddr_in createAddress(const u_long ipAddress, const u_short port)
{
    // заполняем структуру с адресом
    // !!! вот тут х.з. нужно ли под ARM вызывать функции htons() и htonl() - Host-To-Net-Short и Host-To-Net-Long
    // !!! которые переставляют байты из "host" в "net" поледовательность (младший-старший)

    // адрес, на котром  будем "слушать", тут сразу будет и IP, и порт
    sockaddr_in addr{};
    addr.sin_family = AF_INET;          // тип нашего адреса в виде IP+порт
    addr.sin_port = port;               // порт
    addr.sin_addr.s_addr = ipAddress;   // адрес

    return addr;
}

static sockerr_t bindSocket(const SOCKET socket, sockaddr_in& addr)
{
    // связываем ("биндим") сокет прослушки с адресом,
    // т.е. иными словами открываем адрес для ожидания клиента
    return bind(socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
}

static sockerr_t transportSocket(const SOCKET socket, int backlog)
{
    // переводим сокет в режим ожидания клиента
    // замечание - тут ещё не происходит блокировки, а только задаётся режим сокета
    //       backlog - количество ождидаемых клиентов
    return listen(socket, backlog);
}

static sockerr_t connectSocket(const SOCKET socket, sockaddr_in& addr)
{
    // подключаемся к серверу
    return connect(socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
}

u_long SocketHelper::ServerAddress = inet_addr("127.0.0.1");
u_short SocketHelper::ServerPort = 7777;

bool SocketHelper::ResolveServerHost(const std::string& hostNameOrIPv4)
{
    std::string host = hostNameOrIPv4;
    while (!host.empty() && std::isspace(static_cast<unsigned char>(host.front())))
        host.erase(host.begin());
    
    while (!host.empty() && std::isspace(static_cast<unsigned char>(host.back())))
        host.pop_back();

    if (host.empty())
        return false;

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* resolved = nullptr;
    const int rc = getaddrinfo(host.c_str(), nullptr, &hints, &resolved);
    if (rc != 0 || resolved == nullptr)
        return false;

    bool ok = false;
    for (const addrinfo* p = resolved; p != nullptr; p = p->ai_next)
    {
        if (p->ai_family != AF_INET || p->ai_addr == nullptr)
            continue;

        const auto* sin = reinterpret_cast<const sockaddr_in*>(p->ai_addr);
        ServerAddress = sin->sin_addr.s_addr;
        ok = true;
        break;
    }

    freeaddrinfo(resolved);
    return ok;
}

SOCKET SocketHelper::OpenHandshake()
{
    SOCKET listener = openSocket();
    if (listener < 0)
        throw std::runtime_error("Failed to create Handshake listener socket. Socket constructor returned negative descriptor.");

    // Listener bind'ится на INADDR_ANY: сервер слушает на всех интерфейсах.
    sockaddr_in addr = createAddress(htonl(INADDR_ANY), ServerPort);
    sockerr_t err = bindSocket(listener, addr);
    if (err != ERR_OK)
    {
        Close(&listener);
        throw std::runtime_error("Failed to bind Handshake listener socket");
    }

    err = transportSocket(listener, SOMAXCONN);
    if (err != ERR_OK)
    {
        Close(&listener);
        throw std::runtime_error("Failed to listen Handshake listener socket");
    }

    return listener;
}

SOCKET SocketHelper::ConnectHandshake()
{
    SOCKET transport = openSocket();
    if (transport < 0)
        throw std::runtime_error("Failed to create Handshake listener socket. Socket constructor returned negative descriptor.");

    // Клиент подключается к ServerAddress:ServerPort (конфигурируется в клиенте).
    sockaddr_in addr = createAddress(ServerAddress, ServerPort);
    sockerr_t err = connectSocket(transport, addr);
    if (err != ERR_OK)
    {
        int lastError = WSAGetLastError();
        Close(&transport);
        
        if (lastError == WSAECONNREFUSED)
            throw connection_refused_error();

        throw request_error("Failed to connect Handshake listener socket.", err);
    }

    return transport;
}

std::string SocketHelper::PeerIdentificatorOf(const SOCKET* socketInfo)
{
    sockaddr_storage addr;
    socklen_t addrLen = sizeof(addr);

    if (getpeername(*socketInfo, reinterpret_cast<sockaddr*>(&addr), &addrLen) == SOCKET_ERROR)
        return "Unknown";

    char ipString[INET6_ADDRSTRLEN];
    if (addr.ss_family == AF_INET)
    {
        // Это IPv4
        sockaddr_in* ipv4 = reinterpret_cast<sockaddr_in*>(&addr);
        inet_ntop(AF_INET, &(ipv4->sin_addr), ipString, sizeof(ipString));
    }
    else if (addr.ss_family == AF_INET6)
    {
        // Это IPv6
        sockaddr_in6* ipv6 = reinterpret_cast<sockaddr_in6*>(&addr);
        inet_ntop(AF_INET6, &(ipv6->sin6_addr), ipString, sizeof(ipString));
    }
    else
    {
        return "Unsupported Protocol";
    }

    return std::string(ipString);
}

u_long SocketHelper::PeerAddressOf(const SOCKET* socketInfo)
{
    sockaddr_in peerAddr{};
    socklen_t addrSize = sizeof(peerAddr);

    sockerr_t err = getpeername(*socketInfo, reinterpret_cast<sockaddr*>(&peerAddr), &addrSize);
    if (err != ERR_OK)
        return 0;

    return peerAddr.sin_addr.s_addr;
}

u_short SocketHelper::PeerPortOf(const SOCKET* socketInfo)
{
    sockaddr_in peerAddr{};
    socklen_t addrSize = sizeof(peerAddr);

    sockerr_t err = getpeername(*socketInfo, reinterpret_cast<sockaddr*>(&peerAddr), &addrSize);
    if (err != ERR_OK)
        return 0;

    return peerAddr.sin_port;
}

std::string SocketHelper::SockIdentificatorOf(const SOCKET* socketInfo)
{
    sockaddr_storage addr;
    socklen_t addrLen = sizeof(addr);

    if (getsockname(*socketInfo, reinterpret_cast<sockaddr*>(&addr), &addrLen) == SOCKET_ERROR)
        return "Unknown";

    char ipString[INET6_ADDRSTRLEN];
    if (addr.ss_family == AF_INET)
    {
        // Это IPv4
        sockaddr_in* ipv4 = reinterpret_cast<sockaddr_in*>(&addr);
        inet_ntop(AF_INET, &(ipv4->sin_addr), ipString, sizeof(ipString));
    }
    else if (addr.ss_family == AF_INET6)
    {
        // Это IPv6
        sockaddr_in6* ipv6 = reinterpret_cast<sockaddr_in6*>(&addr);
        inet_ntop(AF_INET6, &(ipv6->sin6_addr), ipString, sizeof(ipString));
    }
    else
    {
        return "Unsupported Protocol";
    }

    return std::string(ipString);
}

u_long SocketHelper::SockAddressOf(const SOCKET* socketInfo)
{
    sockaddr_in peerAddr{};
    socklen_t addrSize = sizeof(peerAddr);

    sockerr_t err = getsockname(*socketInfo, reinterpret_cast<sockaddr*>(&peerAddr), &addrSize);
    if (err != ERR_OK)
        return 0;

    return peerAddr.sin_addr.s_addr;
}

u_short SocketHelper::SockPortOf(const SOCKET* socketInfo)
{
    sockaddr_in peerAddr{};
    socklen_t addrSize = sizeof(peerAddr);

    sockerr_t err = getsockname(*socketInfo, reinterpret_cast<sockaddr*>(&peerAddr), &addrSize);
    if (err != ERR_OK)
        return 0;

    return peerAddr.sin_port;
}

template<typename T>
sockerr_t SocketHelper::SendData(const SOCKET* transportSocket, const T& buffer)
{
    // Надежная отправка: докидываем, пока не отправим sizeof(T) байт.
    // Это важно, потому что TCP send может отправить меньше запрошенного.
    int toBeSent = sizeof(T);
    const char* bufferPtr = reinterpret_cast<const char*>(&buffer);

    while (toBeSent > 0)
    {
        int bytesSent = send(
            *transportSocket,
            bufferPtr + (sizeof(T) - toBeSent),
            toBeSent, 0);

        if (bytesSent == SOCKET_ERROR)
        {
#ifdef _WIN32
            int lastError = WSAGetLastError();
            if (lastError == WSAECONNABORTED || lastError == WSAECONNRESET)
                throw disconnected_error();
#else
            int lastError = errno;
            if (lastError == ECONNABORTED || lastError == ECONNRESET || lastError == EPIPE)
                throw disconnected_error();
#endif
            return SOCKET_ERROR;
        }

        if (bytesSent == 0)
            throw disconnected_error();

        toBeSent -= bytesSent;
        continue;
    }

    return ERR_OK;
}

template sockerr_t SocketHelper::SendData<Request>(const SOCKET* transportSocket, const Request& buffer);
template sockerr_t SocketHelper::SendData<Responce>(const SOCKET* transportSocket, const Responce& buffer);

template<typename T>
sockerr_t SocketHelper::SendExactly(const SOCKET* transportSocket, const T& buffer)
{
    int sentBytes = send(
        *transportSocket,
        reinterpret_cast<const char*>(&buffer), 
        sizeof(T), 0);

    if (sentBytes != SOCKET_ERROR)
    {
        if (sentBytes == 0)
            throw disconnected_error();

        if (sentBytes < 0)
            throw std::runtime_error("Failed to receive responce. Server returned 0 or negative amount of data.");

        if (sentBytes != sizeof(T))
            throw std::runtime_error("sentBytes amount mismatch (expected - " + std::to_string(sizeof(T)) + ", got - " + std::to_string(sentBytes) + ")");

        return ERR_OK;
    }

#ifdef _WIN32
    int lastError = WSAGetLastError();
    if (lastError == WSAECONNABORTED || lastError == WSAECONNRESET)
        throw disconnected_error();
#else
    int lastError = errno;
    if (lastError == ECONNABORTED || lastError == ECONNRESET || lastError == EPIPE)
        throw disconnected_error();
#endif

    return SOCKET_ERROR;
}

template sockerr_t SocketHelper::SendExactly<Request>(const SOCKET* transportSocket, const Request& buffer);
template sockerr_t SocketHelper::SendExactly<Responce>(const SOCKET* transportSocket, const Responce& buffer);

template<typename T>
sockerr_t SocketHelper::ReceiveData(const SOCKET* transportSocket, T& buffer)
{
	int toBeReaded = sizeof(T);
	char* bufferPtr = reinterpret_cast<char*>(&buffer);

    while (toBeReaded > 0)
    {
        int bytesRead = recv(
            *transportSocket,
            bufferPtr + (sizeof(T) - toBeReaded),
            toBeReaded, 0);

        if (bytesRead == SOCKET_ERROR)
        {
#ifdef _WIN32
            int lastError = WSAGetLastError();
            if (lastError == WSAECONNABORTED || lastError == WSAECONNRESET)
                throw disconnected_error();
#else
            int lastError = errno;
            if (lastError == ECONNABORTED || lastError == ECONNRESET || lastError == EPIPE)
                throw disconnected_error();
#endif
            return SOCKET_ERROR;
        }

        if (bytesRead == 0)
            throw disconnected_error();

        toBeReaded -= bytesRead;
        continue;
    }

    return ERR_OK;
}

template sockerr_t SocketHelper::ReceiveData<Request>(const SOCKET* transportSocket, Request& buffer);
template sockerr_t SocketHelper::ReceiveData<Responce>(const SOCKET* transportSocket, Responce& buffer);

template<typename T>
sockerr_t SocketHelper::ReceiveExactly(const SOCKET* transportSocket, T& buffer)
{
    int bytesRead = recv(
        *transportSocket,
        reinterpret_cast<char*>(&buffer),
        sizeof(T), 0); // 0 - режим работы по умолчанию, есть и другие

    if (bytesRead != SOCKET_ERROR)
    {
        if (bytesRead == 0)
            throw disconnected_error();

        if (bytesRead < 0)
            throw std::runtime_error("Failed to receive responce. Server returned 0 or negative amount of data.");

        if (bytesRead != sizeof(T))
            throw std::runtime_error("sentBytes amount mismatch (expected - " + std::to_string(sizeof(T)) + ", got - " + std::to_string(bytesRead) + ")");

        return ERR_OK;
    }

#ifdef _WIN32
    int lastError = WSAGetLastError();
    if (lastError == WSAECONNABORTED || lastError == WSAECONNRESET)
        throw disconnected_error();
#else
    int lastError = errno;
    if (lastError == ECONNABORTED || lastError == ECONNRESET || lastError == EPIPE)
        throw disconnected_error();
#endif

    return SOCKET_ERROR;
}

template sockerr_t SocketHelper::ReceiveExactly<Request>(const SOCKET* transportSocket, Request& buffer);
template sockerr_t SocketHelper::ReceiveExactly<Responce>(const SOCKET* transportSocket, Responce& buffer);

sockerr_t SocketHelper::SendBuffer(const SOCKET* transportSocket, const uint8_t* data, size_t size)
{
    size_t sent = 0;
    while (sent < size)
    {
        const int chunk = static_cast<int>(size - sent);
        int bytesSent = send(
            *transportSocket,
            reinterpret_cast<const char*>(data + sent),
            chunk, 0);

        if (bytesSent == SOCKET_ERROR)
        {
#ifdef _WIN32
            int lastError = WSAGetLastError();
            if (lastError == WSAECONNABORTED || lastError == WSAECONNRESET)
                throw disconnected_error();
#else
            int lastError = errno;
            if (lastError == ECONNABORTED || lastError == ECONNRESET || lastError == EPIPE)
                throw disconnected_error();
#endif
            return SOCKET_ERROR;
        }

        if (bytesSent == 0)
            throw disconnected_error();

        sent += static_cast<size_t>(bytesSent);
    }

    return ERR_OK;
}

sockerr_t SocketHelper::ReceiveBuffer(const SOCKET* transportSocket, uint8_t* data, size_t size)
{
    size_t received = 0;
    while (received < size)
    {
        const int chunk = static_cast<int>(size - received);
        int bytesRead = recv(
            *transportSocket,
            reinterpret_cast<char*>(data + received),
            chunk, 0);

        if (bytesRead == SOCKET_ERROR)
        {
#ifdef _WIN32
            int lastError = WSAGetLastError();
            if (lastError == WSAECONNABORTED || lastError == WSAECONNRESET)
                throw disconnected_error();
#else
            int lastError = errno;
            if (lastError == ECONNABORTED || lastError == ECONNRESET || lastError == EPIPE)
                throw disconnected_error();
#endif
            return SOCKET_ERROR;
        }

        if (bytesRead == 0)
            throw disconnected_error();

        received += static_cast<size_t>(bytesRead);
    }

    return ERR_OK;
}

void SocketHelper::Close(SOCKET* socketInfo)
{
    if (*socketInfo == 0)
        return;

    // если попали сюда, значит клиент "отпал" или долго ничего не передаёт
    // закрываем сокет этого клиента и уходим на очередной круг для ожидания нового сеанса
    shutdown(*socketInfo, 0);

#ifdef _WIN32
    closesocket(*socketInfo);
#else
    close(*socketInfo);
#endif

    *socketInfo = 0;
    return;
}