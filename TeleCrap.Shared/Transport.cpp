#include "pch.h"

#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <thread>

#include "telecrap/Transport.h"
#include "telecrap/Protocol.h"
#include "telecrap/SocketHelper.h"
#include "telecrap/Models.h"
#include "telecrap/Request.h"
#include "telecrap/Responce.h"

static void InitWsad()
{
#ifdef _WIN32
    static WSAData Wsad;
    static bool executed = false;

    if (executed)
        return;

    executed = true;
    if (WSAStartup(0x0202, &Wsad))
        throw std::runtime_error("WSAStartup failed");
#endif
}

static SOCKET ScreamInVoidUntilConnected()
{
    // Кричим в пустоту пока не услышим ответ от сервера
    while (true)
    {
        try
        {
            // функция создаст транспортный сокет, при удачном подключении и рукопожатии с сервером
            return SocketHelper::ConnectHandshake();
        }
        catch (request_error&)
        {
            // "ошибка запроса" означает что соединение с сервером было установлено, но рукопажатие окончилось ошибкой
            // например неверный флаг при нестабильом соединении, или несовпадающая версия протокола
            // продолжать связываться с сервером в таком случае бесполезно, пожтому просто пробрасываем исключение далее
            throw;
        }
        catch (...)
        {
            // если не удалось подключиться, то пробуем снова, возможно сервер ещё не запустился
            // немного откладываем следующий запрос, чтобы не спамить
            std::this_thread::sleep_for(std::chrono::milliseconds(3000));
        }
    }
}

Transport::Transport(const SOCKET transportSocket, const token_t accessToken)
    : TransportSocket(transportSocket), AccessToken(accessToken)
{

}

Transport::~Transport()
{
    // Transport владеет только socket-дескриптором: закрываем его при уничтожении.
    SocketHelper::Close(const_cast<SOCKET*>(&TransportSocket));
    *const_cast<token_t*>(&AccessToken) = 0;
}

void Transport::Init()
{
    InitWsad();
}

Transport* Transport::Server()
{
    SOCKET listenSocket = SocketHelper::OpenHandshake();
    return new Transport(listenSocket, 0);
}

Transport* Transport::Client()
{
    static bool executed = false;
    if (executed)
        throw std::runtime_error("Client endpoint protocol can be created only once per process.");

    SOCKET transportSocket = ScreamInVoidUntilConnected();
    const connflag_t flag = rand();
    Responce responce = Protocol::SendRequest(&transportSocket, Request::CreateHandshake(flag));

    if (responce.Type == ResponceType::Error)
    {
        SocketHelper::Close(&transportSocket);
        throw std::runtime_error("Failed to conect to server : " + std::string(responce.Error.Message.buffer));
    }

    if (responce.Handshake.Flag != flag)
    {
        SocketHelper::Close(&transportSocket);
        throw std::runtime_error("Failed to conect to server : server returned wrong flag");
    }

    // После handshake сервер выдает токен, который клиент прикладывает ко всем дальнейшим запросам.
    executed = true;
    return new Transport(transportSocket, responce.Handshake.Token);
}
