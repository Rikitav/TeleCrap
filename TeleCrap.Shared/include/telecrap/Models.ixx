module;

#include <string.h>
#include <cstring>
#include <string_view>
#include <iostream>
#include <cstdint>
#include <algorithm>

export module telecrap:Models;

export namespace telecrap
{
    constexpr uint16_t TELECRAP_VERSION = 6;
    constexpr uint16_t SYSTEM_FROMID = 1;
    constexpr uint16_t CHATNAME_MAXLENGTH = 64;
    constexpr uint16_t PASSWORD_MAXLENGTH = 32;
    constexpr uint16_t MESSAGETEXT_MAXLENGTH = 512;
    constexpr uint16_t ERRORMESSAGE_MAXLENGTH = 512;
    
    using version_t = uint16_t;
    using connflag_t = uint16_t;
    using port_t = uint16_t;
    using token_t = uint32_t;
    
    using userid_t = uint32_t;
    using chatid_t = uint32_t;
    using msgid_t = int64_t;
    using timestamp_t = int64_t;
    
    static_assert(
        sizeof(token_t) == 4 &&
        sizeof(chatid_t) == 4 &&
        sizeof(userid_t) == 4,
        "wire sizes");
    
    #pragma pack(push, 1)
    enum class ErrorCode : int32_t
    {
        OK = 0,                         // No error occurred.
		SOCKET_ERROR = -1,              // Incorrect function.
        INVALID_HANDLE = 6,             // Specified event object handle is invalid.
        NOT_ENOUGH_MEMORY = 8,          // Insufficient memory available.
        INVALID_PARAMETER = 87,         // One or more parameters are invalid.
        OPERATION_ABORTED = 995,        // Overlapped operation aborted.
        IO_INCOMPLETE = 996,            // Overlapped I/O event object not in signaled state.
        IO_PENDING = 997,               // Overlapped operations will complete later.
        INTR = 10004,                   // Interrupted function call.
        BADF = 10009,                   // File handle is not valid.
        ACCES = 10013,                  // Permission denied.
        FAULT = 10014,                  // Bad address.
        INVAL = 10022,                  // Invalid argument.
        MFILE = 10024,                  // Too many open files.
        WOULDBLOCK = 10035,             // Resource temporarily unavailable.
        INPROGRESS = 10036,             // Operation now in progress.
        ALREADY = 10037,                // Operation already in progress.
        NOTSOCK = 10038,                // Socket operation on nonsocket.
        DESTADDRREQ = 10039,            // Destination address required.
        MSGSIZE = 10040,                // Message too long.
        PROTOTYPE = 10041,              // Protocol wrong type for socket.
        NOPROTOOPT = 10042,             // Bad protocol option.
        PROTONOSUPPORT = 10043,         // Protocol not supported.
        SOCKTNOSUPPORT = 10044,         // Socket type not supported.
        OPNOTSUPP = 10045,              // Operation not supported.
        PFNOSUPPORT = 10046,            // Protocol family not supported.
        AFNOSUPPORT = 10047,            // Address family not supported by protocol family.
        ADDRINUSE = 10048,              // Address already in use.
        ADDRNOTAVAIL = 10049,           // Cannot assign requested address.
        NETDOWN = 10050,                // Network is down.
        NETUNREACH = 10051,             // Network is unreachable.
        NETRESET = 10052,               // Network dropped connection on reset.
        CONNABORTED = 10053,            // Software caused connection abort.
        CONNRESET = 10054,              // Connection reset by peer.
        NOBUFS = 10055,                 // No buffer space available.
        ISCONN = 10056,                 // Socket is already connected.
        NOTCONN = 10057,                // Socket is not connected.
        SHUTDOWN = 10058,               // Cannot send after socket shutdown.
        TOOMANYREFS = 10059,            // Too many references.
        TIMEDOUT = 10060,               // Connection timed out.
        CONNREFUSED = 10061,            // Connection refused.
        LOOP = 10062,                   // Cannot translate name.
        NAMETOOLONG = 10063,            // Name too long.
        HOSTDOWN = 10064,               // Host is down.
        HOSTUNREACH = 10065,            // No route to host.
        NOTEMPTY = 10066,               // Directory not empty.
        PROCLIM = 10067,                // Too many processes.
        USERS = 10068,                  // User quota exceeded.
        DQUOT = 10069,                  // Disk quota exceeded.
        STALE = 10070,                  // Stale file handle reference.
        REMOTE = 10071,                 // Item is remote.
        SYSNOTREADY = 10091,            // Network subsystem is unavailable.
        VERNOTSUPPORTED = 10092,        // Winsock.dll version out of range.
        NOTINITIALISED = 10093,         // Successful WSAStartup not yet performed.
        DISCON = 10101,                 // Graceful shutdown in progress.
        NOMORE = 10102,                 // No more results.
        CANCELLED = 10103,              // Call has been canceled.
        INVALIDPROCTABLE = 10104,       // Procedure call table is invalid.
        INVALIDPROVIDER = 10105,        // Service provider is invalid.
        PROVIDERFAILEDINIT = 10106,     // Service provider failed to initialize.
        SYSCALLFAILURE = 10107,         // System call failure.
        SERVICE_NOT_FOUND = 10108,      // Service not found.
        TYPE_NOT_FOUND = 10109,         // Class type not found.
        NO_MORE = 10110,                // No more results.
        REFUSED = 10112,                // Database query was refused.
        HOSTNOTFOUND = 11001,           // Host not found.
        TRYAGAIN = 11002,               // Nonauthoritative host not found.
        NORECOVERY = 11003,             // This is a nonrecoverable error.
        NODATA = 11004,                 // Valid name, no data record of requested type.
        RECEIVERS = 11005,              // QoS receivers.
        SENDERS = 11006,                // QoS senders.
        NO_SENDERS = 11007,             // No QoS senders.
        NO_RECEIVERS = 11008,           // QoS no receivers.
        REQUEST_CONFIRMED = 11009,      // QoS request confirmed.
        ADMISSION_FAILURE = 11010,      // QoS admission error.
        POLICY_FAILURE = 11011,         // QoS policy failure.
        BAD_STYLE = 11012,              // QoS bad style.
        BAD_OBJECT = 11013,             // QoS bad object.
        TRAFFIC_CTRL_ERROR = 11014,     // QoS traffic control error.
        GENERIC_ERROR = 11015,          // QoS generic error.
        ESERVICETYPE = 11016,           // QoS service type error.
        EFLOWSPEC = 11017,              // QoS flowspec error.
        EPROVSPECBUF = 11018,           // Invalid QoS provider buffer.
        EFILTERSTYLE = 11019,           // Invalid QoS filter style.
        EFILTERTYPE = 11020,            // Invalid QoS filter type.
        EFILTERCOUNT = 11021,           // Incorrect QoS filter count.
        EOBJLENGTH = 11022,             // Invalid QoS object length.
        EFLOWCOUNT = 11023,             // Incorrect QoS flow count.
        EUNKOWNPSOBJ = 11024,           // Unrecognized QoS object.
        EPOLICYOBJ = 11025,             // Invalid QoS policy object.
        EFLOWDESC = 11026,              // Invalid QoS flow descriptor.
        EPSFLOWSPEC = 11027,            // Invalid QoS provider-specific flowspec.
        EPSFILTERSPEC = 11028,          // Invalid QoS provider-specific filterspec.
        ESDMODEOBJ = 11029,             // Invalid QoS shape discard mode object.
        ESHAPERATEOBJ = 11030,          // Invalid QoS shaping rate object.
        RESERVED_PETYPE = 11031         // Reserved policy QoS element type.
    };

    /// <summary>
    /// Тип чата, используемый в wire-модели.
    /// </summary>
    enum class ChatType : std::uint8_t
    {
        Group = 0,
        Direct = 1,
    };
    
    /// <summary>
    /// Строка фиксированной емкости для бинарного протокола (null-terminated).
    /// </summary>
    template <std::size_t Capacity>
    struct fixed_string
    {
        char buffer[Capacity + 1]{};
    
        fixed_string() = default;
        fixed_string(std::string_view view) { assign(view); }
    
        /// <summary>
        /// Присваивает содержимое из string_view с обрезкой до Capacity.
        /// </summary>
        fixed_string& operator=(std::string_view view)
        {
            assign(view);
            return *this;
        }
    
        void assign(std::string_view view)
        {
            std::size_t copy_size = (((Capacity) < (view.size())) ? (Capacity) : (view.size()));
            if (copy_size > 0)
                memcpy(buffer, view.data(), copy_size);
    
            buffer[copy_size] = '\0';
        }
    
        /// <summary>
        /// Максимальная емкость строки (без учета терминатора).
        /// </summary>
        std::size_t capacity() const
        {
            return Capacity;
        }

        /// <summary>
        /// Длина строки до первого '\\0'.
        /// </summary>
        std::size_t size() const 
        {
            return strlen(buffer);
        }

        /// <summary>
        /// Проверка на пустую строку.
        /// </summary>
        bool empty() const
        {
            return buffer[0] == '\0';
        }

        /// <summary>
        /// C-строка, лежащая в буфере.
        /// </summary>
        const char* c_str() const
        {
            return buffer;
        }
    
        template <std::size_t other_size>
        friend bool operator==(const fixed_string& lhs, const fixed_string<other_size>& rhs)
        {
            return 0 == strcmp(lhs.buffer, rhs.buffer);
        }
    
        operator std::string_view() const
        {
            return std::string_view(buffer, size());
        }
    };
    
    /// <summary>
    /// Wire-модель чата (id, имя, тип).
    /// </summary>
    struct Chat
    {
        chatid_t Id = 0;
        fixed_string<CHATNAME_MAXLENGTH> Name;
        ChatType Type = ChatType::Group;
    };
    
    /// <summary>
    /// Wire-модель пользователя (id, имя).
    /// </summary>
    struct User
    {
        userid_t Id = 0;
        fixed_string<CHATNAME_MAXLENGTH> Name;
    };
    
    /// <summary>
    /// Wire-модель сообщения (id, чат назначения, отправитель, timestamp, текст).
    /// </summary>
    struct Message
    {
        msgid_t Id = 0;
        Chat DestChat;
        User From;
        timestamp_t Timestamp = 0;
        fixed_string<MESSAGETEXT_MAXLENGTH> Text;
    };
    
    #pragma pack(pop)
    
    template <std::size_t N>
    std::ostream& operator<<(std::ostream& os, const fixed_string<N>& str)
    {
        return os << str.c_str();
    }
}
