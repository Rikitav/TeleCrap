#pragma once
#include <stdexcept>
#include <vector>
#include <cstdint>

#include "Models.h"
#include "Responce.h"
#include "Request.h"
#include "SocketHelper.h"
#include "Transport.h"

typedef int32_t(*getRemainingFunc)(Responce&);

/// <summary>
/// Исключение, сигнализирующее о закрытии соединения удаленной стороной.
/// </summary>
class disconnected_error : public std::runtime_error
{
public:
	disconnected_error() : std::runtime_error("Connection was closed by remote host") {}
};

/// <summary>
/// Исключение ошибки протокольного обмена/сокета с кодом и текстом ошибки.
/// </summary>
class request_error : public std::runtime_error
{
    fixed_string<ERRORMESSAGE_MAXLENGTH> Message;
    sockerr_t ErrorCode;

public:
    request_error(fixed_string<ERRORMESSAGE_MAXLENGTH> message, sockerr_t errorCode)
        : std::runtime_error(Message.c_str()), Message(message), ErrorCode(errorCode) {}

    request_error(const char* message, sockerr_t errorCode)
        : std::runtime_error(Message.c_str()), Message(message), ErrorCode(errorCode) {}

    request_error(Responce& errorResponce)
        : std::runtime_error(Message.c_str()), Message(errorResponce.Error.Message), ErrorCode(errorResponce.Error.Code) {}

    sockerr_t which() const
    {
        return ErrorCode;
    }
};

/// <summary>
/// API бинарного протокола CrapOverTCP: handshake, request/response и выдача списков.
/// </summary>
class Protocol
{
public:
    /// <summary>
    /// Принимает входящее handshake-подключение и возвращает транспорт клиента.
    /// </summary>
    static Transport* ListenHandshake(const SOCKET* transportSocket);

    /// <summary>
    /// Читает один Request из сокета.
    /// </summary>
    static Request GetRequest(const SOCKET* transportSocket);

    /// <summary>
    /// Читает один Responce из сокета.
    /// </summary>
    static Responce GetResponce(const SOCKET* transportSocket);

    /// <summary>
    /// Отправляет Request и возвращает одиночный Responce.
    /// </summary>
    static Responce SendRequest(const SOCKET* transportSocket, const Request& request);

    /// <summary>
    /// Отправляет Responce в сокет.
    /// </summary>
    static void SendResponce(const SOCKET* transportSocket, const Responce& responce);
    
    /// <summary>
    /// Отправляет Request и читает последовательность Responce до исчерпания remaining-счетчика.
    /// </summary>
    static std::vector<Responce> SendRequestList(const SOCKET* transportSocket, const Request& request, getRemainingFunc getRemaining);
};
