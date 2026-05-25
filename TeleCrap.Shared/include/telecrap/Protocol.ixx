module;

#include <stdexcept>
#include <vector>
#include <cstdint>

export module telecrap:Protocol;

import :Models;
import :Responce;
import :Request;
import :SocketHelper;
import :Transport;

export namespace telecrap
{
	constexpr int32_t NO_MORE_REMAINING = -1;
    typedef int32_t(*getRemainingFunc)(Responce&);
    
    class connection_refused_error : public std::runtime_error
    {
    public:
        connection_refused_error()
            : std::runtime_error("Connection was refused by remote host") { }
    };
    
    /// <summary>
    /// Исключение, сигнализирующее о закрытии соединения удаленной стороной.
    /// </summary>
    class disconnected_error : public std::runtime_error
    {
    public:
    	disconnected_error()
            : std::runtime_error("Connection was closed by remote host") { }
    };
    
    /// <summary>
    /// Исключение ошибки протокольного обмена/сокета с кодом и текстом ошибки.
    /// </summary>
    class request_error : public std::runtime_error
    {
        fixed_string<ERRORMESSAGE_MAXLENGTH> Message;
        ErrorCode ErrCode;
    
    public:
        request_error(fixed_string<ERRORMESSAGE_MAXLENGTH> message, ErrorCode errorCode)
            : std::runtime_error(Message.c_str()), Message(message), ErrCode(errorCode) {}
    
        request_error(const char* message, ErrorCode errorCode)
            : std::runtime_error(Message.c_str()), Message(message), ErrCode(errorCode) {}
    
        request_error(Responce& errorResponce)
            : std::runtime_error(Message.c_str()), Message(errorResponce.Error.Message), ErrCode(errorResponce.Error.Code) {}
    
        ErrorCode which() const
        {
            return ErrCode;
        }
    };
    
    /// <summary>
    /// API бинарного протокола CrapOverTCP: handshake, request/response и выдача списков.
    /// </summary>
    class Protocol
    {
    public:
        static void RegisterSecureSession(const Transport* transport);

        static void UnregisterSecureSession(const Transport* transport);
    
        /// <summary>
        /// Принимает входящее handshake-подключение и возвращает транспорт клиента.
        /// </summary>
        static Transport* ListenHandshake(const sockhandle_t* transportSocket);
    
        /// <summary>
        /// Читает один Request из сокета.
        /// </summary>
        static Request GetRequest(const sockhandle_t* transportSocket);
    
        /// <summary>
        /// Читает один Responce из сокета.
        /// </summary>
        static Responce GetResponce(const sockhandle_t* transportSocket);
    
        /// <summary>
        /// Отправляет Request и возвращает одиночный Responce.
        /// </summary>
        static Responce SendRequest(const sockhandle_t* transportSocket, const Request& request);
    
        /// <summary>
        /// Отправляет Responce в сокет.
        /// </summary>
        static void SendResponce(const sockhandle_t* transportSocket, const Responce& responce);
        
        /// <summary>
        /// Отправляет Request и читает последовательность Responce до исчерпания remaining-счетчика.
        /// </summary>
        static std::vector<Responce> SendRequestList(const sockhandle_t* transportSocket, const Request& request, getRemainingFunc getRemaining);
    };
}
