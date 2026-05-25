module;

#include <cctype>
#include <stdexcept>
#include <string>

#ifdef _WIN32

    // Windows
    #include <WinSock2.h>
    #include <Windows.h>
    #include <ws2tcpip.h>
#else

    // Linux (POSIX)
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <cerrno>
    #include <netdb.h>
#endif

#undef SOCKET_ERROR

module telecrap;

using namespace telecrap;

static sockhandle_t openSocket()
{
    return static_cast<sockhandle_t>(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
}

static sockaddr_in createAddress(const u_long ipAddress, const u_short port)
{
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = ipAddress;

    return addr;
}

static ErrorCode bindSocket(const sockhandle_t socket, sockaddr_in& addr)
{
    return static_cast<ErrorCode>(bind(socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)));
}

static ErrorCode transportSocket(const sockhandle_t socket, int backlog)
{
    return static_cast<ErrorCode>(listen(socket, backlog));
}

static ErrorCode connectSocket(const sockhandle_t socket, sockaddr_in& addr)
{
    return static_cast<ErrorCode>(connect(socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)));
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

sockhandle_t SocketHelper::OpenHandshake()
{
    sockhandle_t listener = openSocket();
    if (listener < 0)
        throw std::runtime_error("Failed to create Handshake listener socket. Socket constructor returned negative descriptor.");

    // Listener bind'ится на INADDR_ANY: сервер слушает на всех интерфейсах.
    sockaddr_in addr = createAddress(htonl(INADDR_ANY), ServerPort);
    ErrorCode err = bindSocket(listener, addr);
    if (err != ErrorCode::OK)
    {
        Close(&listener);
        throw std::runtime_error("Failed to bind Handshake listener socket");
    }

    err = transportSocket(listener, SOMAXCONN);
    if (err != ErrorCode::OK)
    {
        Close(&listener);
        throw std::runtime_error("Failed to listen Handshake listener socket");
    }

    return listener;
}

sockhandle_t SocketHelper::ConnectHandshake()
{
    sockhandle_t transport = openSocket();
    if (transport < 0)
        throw std::runtime_error("Failed to create Handshake listener socket. Socket constructor returned negative descriptor.");

    // Клиент подключается к ServerAddress:ServerPort (конфигурируется в клиенте).
    sockaddr_in addr = createAddress(ServerAddress, ServerPort);
    ErrorCode err = connectSocket(transport, addr);
    if (err != ErrorCode::OK)
    {
#ifdef _WIN32
        int lastError = WSAGetLastError();
#else
        int lastError = errno;
#endif
        Close(&transport);

#ifdef _WIN32
        if (lastError == WSAECONNREFUSED)
#else
        if (lastError == ECONNREFUSED)
#endif
            throw connection_refused_error();

        throw request_error("Failed to connect Handshake listener socket.", err);
    }

    return transport;
}

std::string SocketHelper::PeerIdentificatorOf(const sockhandle_t* socketInfo)
{
    sockaddr_storage addr;
    socklen_t addrLen = sizeof(addr);

    if (static_cast<ErrorCode>(getpeername(*socketInfo, reinterpret_cast<sockaddr*>(&addr), &addrLen)) == ErrorCode::SOCKET_ERROR)
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

u_long SocketHelper::PeerAddressOf(const sockhandle_t* socketInfo)
{
    sockaddr_in peerAddr{};
    socklen_t addrSize = sizeof(peerAddr);

    ErrorCode err = static_cast<ErrorCode>(getpeername(*socketInfo, reinterpret_cast<sockaddr*>(&peerAddr), &addrSize));
    if (err != ErrorCode::OK)
        return 0;

    return peerAddr.sin_addr.s_addr;
}

u_short SocketHelper::PeerPortOf(const sockhandle_t* socketInfo)
{
    sockaddr_in peerAddr{};
    socklen_t addrSize = sizeof(peerAddr);

    ErrorCode err = static_cast<ErrorCode>(getpeername(*socketInfo, reinterpret_cast<sockaddr*>(&peerAddr), &addrSize));
    if (err != ErrorCode::OK)
        return 0;

    return peerAddr.sin_port;
}

std::string SocketHelper::SockIdentificatorOf(const sockhandle_t* socketInfo)
{
    sockaddr_storage addr;
    socklen_t addrLen = sizeof(addr);

    if (static_cast<ErrorCode>(getsockname(*socketInfo, reinterpret_cast<sockaddr*>(&addr), &addrLen)) == ErrorCode::SOCKET_ERROR)
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

u_long SocketHelper::SockAddressOf(const sockhandle_t* socketInfo)
{
    sockaddr_in peerAddr{};
    socklen_t addrSize = sizeof(peerAddr);

    ErrorCode err = static_cast<ErrorCode>(getsockname(*socketInfo, reinterpret_cast<sockaddr*>(&peerAddr), &addrSize));
    if (err != ErrorCode::OK)
        return 0;

    return peerAddr.sin_addr.s_addr;
}

u_short SocketHelper::SockPortOf(const sockhandle_t* socketInfo)
{
    sockaddr_in peerAddr{};
    socklen_t addrSize = sizeof(peerAddr);

    ErrorCode err = static_cast<ErrorCode>(getsockname(*socketInfo, reinterpret_cast<sockaddr*>(&peerAddr), &addrSize));
    if (err != ErrorCode::OK)
        return 0;

    return peerAddr.sin_port;
}

template<typename T>
ErrorCode SocketHelper::SendData(const sockhandle_t* transportSocket, const T& buffer)
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

        if (static_cast<ErrorCode>(bytesSent) == ErrorCode::SOCKET_ERROR)
        {
#ifdef _WIN32
            ErrorCode lastError = static_cast<ErrorCode>(WSAGetLastError());
            if (lastError == ErrorCode::CONNABORTED || lastError == ErrorCode::CONNRESET)
                throw disconnected_error();
#else
            int lastError = errno;
            if (lastError == ECONNABORTED || lastError == ECONNRESET || lastError == EPIPE)
                throw disconnected_error();
#endif
            return ErrorCode::SOCKET_ERROR;
        }

        if (bytesSent == 0)
            throw disconnected_error();

        toBeSent -= bytesSent;
        continue;
    }

    return ErrorCode::OK;
}

template ErrorCode SocketHelper::SendData<Request>(const sockhandle_t* transportSocket, const Request& buffer);
template ErrorCode SocketHelper::SendData<Responce>(const sockhandle_t* transportSocket, const Responce& buffer);

template<typename T>
ErrorCode SocketHelper::SendExactly(const sockhandle_t* transportSocket, const T& buffer)
{
    int sentBytes = send(
        *transportSocket,
        reinterpret_cast<const char*>(&buffer), 
        sizeof(T), 0);

    if (static_cast<ErrorCode>(sentBytes) != ErrorCode::SOCKET_ERROR)
    {
        if (sentBytes == 0)
            throw disconnected_error();

        if (sentBytes < 0)
            throw std::runtime_error("Failed to receive responce. Server returned 0 or negative amount of data.");

        if (sentBytes != sizeof(T))
            throw std::runtime_error("sentBytes amount mismatch (expected - " + std::to_string(sizeof(T)) + ", got - " + std::to_string(sentBytes) + ")");

        return ErrorCode::OK;
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

    return ErrorCode::FAULT;
}

template ErrorCode SocketHelper::SendExactly<Request>(const sockhandle_t* transportSocket, const Request& buffer);
template ErrorCode SocketHelper::SendExactly<Responce>(const sockhandle_t* transportSocket, const Responce& buffer);

template<typename T>
ErrorCode SocketHelper::ReceiveData(const sockhandle_t* transportSocket, T& buffer)
{
	int toBeReaded = sizeof(T);
	char* bufferPtr = reinterpret_cast<char*>(&buffer);

    while (toBeReaded > 0)
    {
        int bytesRead = recv(
            *transportSocket,
            bufferPtr + (sizeof(T) - toBeReaded),
            toBeReaded, 0);

        if (static_cast<ErrorCode>(bytesRead) == ErrorCode::SOCKET_ERROR)
        {
#ifdef _WIN32
            ErrorCode lastError = static_cast<ErrorCode>(WSAGetLastError());
            if (lastError == ErrorCode::CONNABORTED || lastError == ErrorCode::CONNRESET)
                throw disconnected_error();
#else
            int lastError = errno;
            if (lastError == ECONNABORTED || lastError == ECONNRESET || lastError == EPIPE)
                throw disconnected_error();
#endif
            return ErrorCode::SOCKET_ERROR;
        }

        if (bytesRead == 0)
            throw disconnected_error();

        toBeReaded -= bytesRead;
        continue;
    }

    return ErrorCode::OK;
}

template ErrorCode SocketHelper::ReceiveData<Request>(const sockhandle_t* transportSocket, Request& buffer);
template ErrorCode SocketHelper::ReceiveData<Responce>(const sockhandle_t* transportSocket, Responce& buffer);

template<typename T>
ErrorCode SocketHelper::ReceiveExactly(const sockhandle_t* transportSocket, T& buffer)
{
    int bytesRead = recv(
        *transportSocket,
        reinterpret_cast<char*>(&buffer),
        sizeof(T), 0);

    if (static_cast<ErrorCode>(bytesRead) != ErrorCode::SOCKET_ERROR)
    {
        if (bytesRead == 0)
            throw disconnected_error();

        if (bytesRead < 0)
            throw std::runtime_error("Failed to receive responce. Server returned 0 or negative amount of data.");

        if (bytesRead != sizeof(T))
            throw std::runtime_error("sentBytes amount mismatch (expected - " + std::to_string(sizeof(T)) + ", got - " + std::to_string(bytesRead) + ")");

        return ErrorCode::OK;
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

    return ErrorCode::FAULT;
}

template ErrorCode SocketHelper::ReceiveExactly<Request>(const sockhandle_t* transportSocket, Request& buffer);
template ErrorCode SocketHelper::ReceiveExactly<Responce>(const sockhandle_t* transportSocket, Responce& buffer);

ErrorCode SocketHelper::SendBuffer(const sockhandle_t* transportSocket, const uint8_t* data, size_t size)
{
    size_t sent = 0;
    while (sent < size)
    {
        const int chunk = static_cast<int>(size - sent);
        int bytesSent = send(
            *transportSocket,
            reinterpret_cast<const char*>(data + sent),
            chunk, 0);

        if (static_cast<ErrorCode>(bytesSent) == ErrorCode::SOCKET_ERROR)
        {
#ifdef _WIN32
            ErrorCode lastError = static_cast<ErrorCode>(WSAGetLastError());
            if (lastError == ErrorCode::CONNABORTED || lastError == ErrorCode::CONNRESET)
                throw disconnected_error();
#else
            int lastError = errno;
            if (lastError == ECONNABORTED || lastError == ECONNRESET || lastError == EPIPE)
                throw disconnected_error();
#endif
            return ErrorCode::SOCKET_ERROR;
        }

        if (bytesSent == 0)
            throw disconnected_error();

        sent += static_cast<size_t>(bytesSent);
    }

    return ErrorCode::OK;
}

ErrorCode SocketHelper::ReceiveBuffer(const sockhandle_t* transportSocket, uint8_t* data, size_t size)
{
    size_t received = 0;
    while (received < size)
    {
        const int chunk = static_cast<int>(size - received);
        int bytesRead = recv(
            *transportSocket,
            reinterpret_cast<char*>(data + received),
            chunk, 0);

        if (static_cast<ErrorCode>(bytesRead) == ErrorCode::SOCKET_ERROR)
        {
#ifdef _WIN32
            ErrorCode lastError = static_cast<ErrorCode>(WSAGetLastError());
            if (lastError == ErrorCode::CONNABORTED || lastError == ErrorCode::CONNRESET)
                throw disconnected_error();
#else
            int lastError = errno;
            if (lastError == ECONNABORTED || lastError == ECONNRESET || lastError == EPIPE)
                throw disconnected_error();
#endif
            return ErrorCode::SOCKET_ERROR;
        }

        if (bytesRead == 0)
            throw disconnected_error();

        received += static_cast<size_t>(bytesRead);
    }

    return ErrorCode::OK;
}

void SocketHelper::Close(sockhandle_t* socketInfo)
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