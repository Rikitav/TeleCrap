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

#include "telecrap/Models.h"
#include "telecrap/Protocol.h"
#include "telecrap/Request.h"
#include "telecrap/Responce.h"
#include "telecrap/SocketHelper.h"
#include "telecrap/Transport.h"
#include "telecrap/Log.h"

static std::map<SOCKET, std::mutex> requestMutex;

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
                        Protocol::SendResponce(&clientTransport, Responce::CreateHandshake(request.Handshake.Flag, token));
                        Log::Trace("Handshake", "Handshake successfull.");

                        return new Transport(clientTransport, token);
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
    sockerr_t err = SocketHelper::SendData(transportSocket, request);
    if (err != ERR_OK)
        throw request_error("Failed to send request.", err);

    Responce responce{};
    err = SocketHelper::ReceiveData(transportSocket, responce);
    if (err != ERR_OK)
        throw request_error("Failed to receive responce.", err);

    return responce;
}

std::vector<Responce> Protocol::SendRequestList(const SOCKET* transportSocket, const Request& request, getRemainingFunc getRemaining)
{
    std::lock_guard<std::mutex> lock(requestMutex[*transportSocket]);
    sockerr_t err = SocketHelper::SendData(transportSocket, request);
    if (err != ERR_OK)
        throw request_error("Failed to send request.", err);

    Responce responce{};
    err = SocketHelper::ReceiveData(transportSocket, responce);
    if (err != ERR_OK)
        throw request_error("Failed to receive responce.", err);
    
    if (responce.Type == ResponceType::Error)
        throw request_error(responce);

    std::vector<Responce> responces{};
    // Контракт: remaining == -1 означает "пустой список" (нет элементов).
    if (getRemaining(responce) == -1)
        return responces;

    responces.push_back(responce);
    while (getRemaining(responce) > 0)
    {
        err = SocketHelper::ReceiveData(transportSocket, responce);
        if (err != ERR_OK)
            throw request_error("Failed to receive responce.", err);

        if (responce.Type == ResponceType::Error)
            throw request_error(responce);

        responces.push_back(responce);
    }

    return responces;
}

void Protocol::SendResponce(const SOCKET* transportSocket, const Responce& responce)
{
    std::lock_guard<std::mutex> lock(requestMutex[*transportSocket]);
    sockerr_t err = SocketHelper::SendData(transportSocket, responce);
    if (err != ERR_OK)
        throw request_error("Failed to send responce.", err);
}

Request Protocol::GetRequest(const SOCKET* transportSocket)
{
    std::lock_guard<std::mutex> lock(requestMutex[*transportSocket]);
    Request request{};

    sockerr_t err = SocketHelper::ReceiveData(transportSocket, request);
    if (err != ERR_OK)
        throw request_error("Failed to receive request.", err);

    return request;
}

Responce Protocol::GetResponce(const SOCKET* transportSocket)
{
    std::lock_guard<std::mutex> lock(requestMutex[*transportSocket]);
    Responce responce{};

    sockerr_t err = SocketHelper::ReceiveData(transportSocket, responce);
    if (err != ERR_OK)
        throw request_error("Failed to receive responce.", err);

    return responce;
}
