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
#include "telecrap/SecureChannel.h"
#include "telecrap/Log.h"

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
            Log::Info("Main", "Попытка подключиться к серверу...");
            return SocketHelper::ConnectHandshake();
        }
        catch (connection_refused_error&)
        {
            // если не удалось подключиться, то пробуем снова, возможно сервер ещё не запустился
            // немного откладываем следующий запрос, чтобы не спамить
            std::this_thread::sleep_for(std::chrono::milliseconds(3000));
        }
        catch (...)
        {
            // "ошибка запроса" означает что соединение с сервером было установлено, но рукопажатие окончилось ошибкой
            // например неверный флаг при нестабильом соединении, или несовпадающая версия протокола
            // продолжать связываться с сервером в таком случае бесполезно, пожтому просто пробрасываем исключение далее
            throw;
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
    Protocol::UnregisterSecureSession(this);
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
    const std::uint64_t integrityTag = (static_cast<std::uint64_t>(rand()) << 32) ^ static_cast<std::uint64_t>(rand());
    const SecureChannel::KeyPair keyPair = SecureChannel::GenerateX25519KeyPair();

    Responce responce = Protocol::SendRequest(&transportSocket,
        Request::CreateHandshakeSecure(flag, integrityTag, keyPair.PublicKey.data()));

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

    Transport* out = new Transport(transportSocket, responce.Handshake.Token);
    if (responce.Handshake.SecureMode != 1 || responce.Handshake.IntegrityTag != integrityTag)
    {
        delete out;
        throw std::runtime_error("Failed to connect to server : secure handshake integrity check failed");
    }

    const std::array<std::uint8_t, 32> sharedSecret =
        SecureChannel::DeriveSharedSecret(keyPair.PrivateKey, responce.Handshake.ServerPublicKey);

    SecureChannel::DeriveDirectionalKeys(
        sharedSecret,
        integrityTag,
        false,
        out->Secure.TxKey,
        out->Secure.RxKey);

    out->Secure.Enabled = true;
    out->Secure.TxCounter = 0;
    out->Secure.RxCounter = 0;
    Protocol::RegisterSecureSession(out);

    // После handshake сервер выдает токен, который клиент прикладывает ко всем дальнейшим запросам.
    executed = true;
    return out;
}
