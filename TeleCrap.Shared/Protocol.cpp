#include "pch.h"

#include <clocale>
#include <mutex>
#include <ctime>
#include <stdexcept>
#include <iostream>
#include <cstdlib>
#include <string>
#include <random>
#include <map>
#include <vector>

#include "telecrap/Models.h"
#include "telecrap/Protocol.h"
#include "telecrap/Request.h"
#include "telecrap/Responce.h"
#include "telecrap/SocketHelper.h"
#include "telecrap/Transport.h"
#include "telecrap/Log.h"
#include "telecrap/SecureChannel.h"

static std::map<SOCKET, std::mutex> requestMutex;
static std::map<SOCKET, const Transport*> secureSessionBySocket;

struct SecureFrameHeader
{
    std::uint32_t CiphertextSize;
    std::uint64_t Counter;
    std::uint8_t Tag[16];
};

static const Transport* getSecureTransport(const SOCKET* transportSocket)
{
    const auto it = secureSessionBySocket.find(*transportSocket);
    if (it == secureSessionBySocket.end())
        return nullptr;

    const Transport* transport = it->second;
    if (transport == nullptr || !transport->Secure.Enabled)
        return nullptr;

    return transport;
}

template <typename T>
static void sendSecureObject(const Transport* transport, const T& object)
{
    std::array<std::uint8_t, 16> tag{};
    const std::uint64_t counter = transport->Secure.TxCounter++;
    const std::vector<std::uint8_t> ciphertext = SecureChannel::EncryptAead(
        transport->Secure.TxKey,
        counter,
        reinterpret_cast<const std::uint8_t*>(&object),
        sizeof(T),
        tag);

    SecureFrameHeader header{};
    header.CiphertextSize = static_cast<std::uint32_t>(ciphertext.size());
    header.Counter = counter;
    std::memcpy(header.Tag, tag.data(), tag.size());

    if (SocketHelper::SendBuffer(&transport->TransportSocket, reinterpret_cast<const std::uint8_t*>(&header), sizeof(header)) != ERR_OK)
        throw request_error("Failed to send secure frame header", SOCKET_ERROR);

    if (SocketHelper::SendBuffer(&transport->TransportSocket, ciphertext.data(), ciphertext.size()) != ERR_OK)
        throw request_error("Failed to send secure frame payload", SOCKET_ERROR);
}

template <typename T>
static T recvSecureObject(const Transport* transport)
{
    SecureFrameHeader header{};
    if (SocketHelper::ReceiveBuffer(&transport->TransportSocket, reinterpret_cast<std::uint8_t*>(&header), sizeof(header)) != ERR_OK)
        throw request_error("Failed to receive secure frame header", SOCKET_ERROR);

    if (header.CiphertextSize == 0 || header.CiphertextSize > 1u << 20)
        throw request_error("Invalid secure frame size", SOCKET_ERROR);

    std::vector<std::uint8_t> ciphertext(header.CiphertextSize);
    if (SocketHelper::ReceiveBuffer(&transport->TransportSocket, ciphertext.data(), ciphertext.size()) != ERR_OK)
        throw request_error("Failed to receive secure frame payload", SOCKET_ERROR);

    if (header.Counter != transport->Secure.RxCounter)
        throw request_error("Secure counter mismatch", SOCKET_ERROR);

    const std::vector<std::uint8_t> plaintext = SecureChannel::DecryptAead(
        transport->Secure.RxKey,
        transport->Secure.RxCounter,
        ciphertext.data(),
        ciphertext.size(),
        header.Tag);

    transport->Secure.RxCounter++;
    if (plaintext.size() != sizeof(T))
        throw request_error("Secure plaintext size mismatch", SOCKET_ERROR);

    T out{};
    std::memcpy(&out, plaintext.data(), sizeof(T));
    return out;
}

void Protocol::RegisterSecureSession(const Transport* transport)
{
    if (transport == nullptr || !transport->Secure.Enabled)
        return;
    secureSessionBySocket[transport->TransportSocket] = transport;
}

void Protocol::UnregisterSecureSession(const Transport* transport)
{
    if (transport == nullptr)
        return;
    secureSessionBySocket.erase(transport->TransportSocket);
}

static sockerr_t acceptSocket(const SOCKET listener, SOCKET& socket)
{
    // получаем сокет для работы с клиентом
    // это работает с блокировкой (синхронно), т.е. accept() завершится когда подключится клиент,
    // ну или по оооочень большому таймауту выйдем с ошибкой, если клиента долго не будет
    //      второй и третий параметры для переменной и её длины, в которую
    //      вернётся адрес подключившегося клиента, но нам это безразлично, потому NULL и NULL
    socket = accept(listener, NULL, NULL);
    return ERR_OK;
}

static token_t tokGen()
{
    // Токен используется как "session-like" маркер для клиента (AccessToken в Transport).
    thread_local std::random_device rd;
    thread_local std::mt19937_64 gen(rd());
    std::uniform_int_distribution<size_t> dist;
    return static_cast<token_t>(dist(gen));
}

Transport* Protocol::ListenHandshake(const SOCKET* transportSocket)
{
    while (true)
    {
        SOCKET clientTransport = 0;
        try
        {
            if (ERR_OK != acceptSocket(*transportSocket, clientTransport))
            {
                Log::Error("Handshake", "Failed to accept client transport socket");
                continue;
            }

            std::string ip = SocketHelper::PeerIdentificatorOf(&clientTransport) + ":" + std::to_string(SocketHelper::PeerPortOf(&clientTransport));
            Log::Trace("Handshake", "Connection established. Peer - " + ip);
            Request request = Protocol::GetRequest(&clientTransport);

            switch (request.Type)
            {
                default:
                {
                    try
                    {
                        Protocol::SendResponce(&clientTransport, Responce::CreateError("unsupported"));
                        SocketHelper::Close(&clientTransport);
                        continue;
                    }
                    catch (const std::runtime_error& err)
                    {
                        SocketHelper::Close(&clientTransport);
                        Log::Error("Handshake", "Runtime error : " + std::string(err.what()));
                        continue;
                    }
                }

                case RequestType::Handshake:
                {
                    try
                    {
                        // Версия протокола проверяется на этапе handshake.
                        if (request.Handshake.Version != TELECRAP_VERSION)
                        {
                            Protocol::SendResponce(&clientTransport, Responce::CreateError("Invalid version"));
                            SocketHelper::Close(&clientTransport);
                            continue;
                        }

                        token_t token = tokGen();
                        if (request.Handshake.SecureMode == 1)
                        {
                            const SecureChannel::KeyPair serverKeyPair = SecureChannel::GenerateX25519KeyPair();
                            const std::array<std::uint8_t, 32> sharedSecret =
                                SecureChannel::DeriveSharedSecret(serverKeyPair.PrivateKey, request.Handshake.ClientPublicKey);

                            Transport* transport = new Transport(clientTransport, token);
                            SecureChannel::DeriveDirectionalKeys(
                                sharedSecret,
                                request.Handshake.IntegrityTag,
                                true,
                                transport->Secure.TxKey,
                                transport->Secure.RxKey);

                            transport->Secure.TxCounter = 0;
                            transport->Secure.RxCounter = 0;
                            
                            // First reply must be plaintext: client has not enabled AEAD yet.
                            Protocol::SendResponce(&clientTransport, Responce::CreateHandshakeSecure(
                                request.Handshake.Flag,
                                token,
                                request.Handshake.IntegrityTag,
                                serverKeyPair.PublicKey.data()));
                            
                            transport->Secure.Enabled = true;
                            RegisterSecureSession(transport);
                            Log::Trace("Handshake", "Secure handshake successfull.");
                            return transport;
                        }
                        else
                        {
                            Protocol::SendResponce(&clientTransport, Responce::CreateHandshake(request.Handshake.Flag, token));
                            Log::Trace("Handshake", "Handshake successfull.");
                            return new Transport(clientTransport, token);
                        }
                    }
                    catch (const std::runtime_error& err)
                    {
                        Log::Error("Handshake", "Runtime error : " + std::string(err.what()));
                        SocketHelper::Close(&clientTransport);
                        continue;
                    }
                }
            }
        }
        catch (const std::runtime_error& err)
        {
            Log::Error("Handshake", "Runtime error : " + std::string(err.what()));
            SocketHelper::Close(&clientTransport);
            continue;
        }    
    }
}

Responce Protocol::SendRequest(const SOCKET* transportSocket, const Request& request)
{
    // На один сокет допускается параллельная работа нескольких потоков (например, UI + updates),
    // поэтому все send/recv операции сериализуем per-socket mutex'ом.
	std::lock_guard<std::mutex> lock(requestMutex[*transportSocket]);
    if (const Transport* secureTransport = getSecureTransport(transportSocket); secureTransport != nullptr)
    {
        sendSecureObject(secureTransport, request);
        return recvSecureObject<Responce>(secureTransport);
    }
    else
    {
        sockerr_t err = SocketHelper::SendData(transportSocket, request);
        if (err != ERR_OK)
            throw request_error("Failed to send request.", err);

        Responce responce{};
        err = SocketHelper::ReceiveData(transportSocket, responce);
        if (err != ERR_OK)
            throw request_error("Failed to receive responce.", err);

        return responce;
    }
}

std::vector<Responce> Protocol::SendRequestList(const SOCKET* transportSocket, const Request& request, getRemainingFunc getRemaining)
{
    std::lock_guard<std::mutex> lock(requestMutex[*transportSocket]);
    sockerr_t err = ERR_OK;
    Responce responce{};
    if (const Transport* secureTransport = getSecureTransport(transportSocket); secureTransport != nullptr)
    {
        sendSecureObject(secureTransport, request);
        responce = recvSecureObject<Responce>(secureTransport);
    }
    else
    {
        err = SocketHelper::SendData(transportSocket, request);
        if (err != ERR_OK)
            throw request_error("Failed to send request.", err);

        err = SocketHelper::ReceiveData(transportSocket, responce);
        if (err != ERR_OK)
            throw request_error("Failed to receive responce.", err);
    }
    
    if (responce.Type == ResponceType::Error)
        throw request_error(responce);

    // Контракт: remaining == -1 означает "пустой список" (нет элементов).
    std::vector<Responce> responces{};
    if (getRemaining(responce) == -1)
        return responces;

    responces.push_back(responce);
    while (getRemaining(responce) > 0)
    {
        if (const Transport* secureTransport = getSecureTransport(transportSocket); secureTransport != nullptr)
        {
            responce = recvSecureObject<Responce>(secureTransport);
        }
        else
        {
            err = SocketHelper::ReceiveData(transportSocket, responce);
            if (err != ERR_OK)
                throw request_error("Failed to receive responce.", err);
        }

        if (responce.Type == ResponceType::Error)
            throw request_error(responce);

        responces.push_back(responce);
    }

    return responces;
}

void Protocol::SendResponce(const SOCKET* transportSocket, const Responce& responce)
{
    std::lock_guard<std::mutex> lock(requestMutex[*transportSocket]);
    if (const Transport* secureTransport = getSecureTransport(transportSocket); secureTransport != nullptr)
    {
        sendSecureObject(secureTransport, responce);
    }
    else
    {
        sockerr_t err = SocketHelper::SendData(transportSocket, responce);
        if (err != ERR_OK)
            throw request_error("Failed to send responce.", err);
    }
}

Request Protocol::GetRequest(const SOCKET* transportSocket)
{
    std::lock_guard<std::mutex> lock(requestMutex[*transportSocket]);
    if (const Transport* secureTransport = getSecureTransport(transportSocket); secureTransport != nullptr)
        return recvSecureObject<Request>(secureTransport);

    Request request{};
    sockerr_t err = SocketHelper::ReceiveData(transportSocket, request);
    if (err != ERR_OK)
        throw request_error("Failed to receive request.", err);
    return request;
}

Responce Protocol::GetResponce(const SOCKET* transportSocket)
{
    std::lock_guard<std::mutex> lock(requestMutex[*transportSocket]);
    if (const Transport* secureTransport = getSecureTransport(transportSocket); secureTransport != nullptr)
        return recvSecureObject<Responce>(secureTransport);

    Responce responce{};
    sockerr_t err = SocketHelper::ReceiveData(transportSocket, responce);
    if (err != ERR_OK)
        throw request_error("Failed to receive responce.", err);
    return responce;
}
