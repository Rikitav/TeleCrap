#pragma once

#include <string.h>
#include <cstring>
#include <string_view>
#include <iostream>
#include <cstdint>
#include <algorithm>

#define TELECRAP_VERSION 5

#define SYSTEM_FROMID 1
#define CHATNAME_MAXLENGTH 64
#define PASSWORD_MAXLENGTH 32
#define MESSAGETEXT_MAXLENGTH 512
#define ERRORMESSAGE_MAXLENGTH 512

typedef int32_t sockerr_t;
typedef uint16_t version_t;
typedef uint16_t connflag_t;
typedef uint16_t port_t;
typedef uint32_t token_t;

typedef uint32_t userid_t;
typedef uint32_t chatid_t;
typedef int64_t msgid_t;
typedef int64_t timestamp_t;

static_assert(sizeof(token_t) == 4 && sizeof(chatid_t) == 4 && sizeof(userid_t) == 4, "wire sizes");

#pragma pack(push, 1)

/// <summary>
/// Тип чата, используемый в wire-модели.
/// </summary>
enum class ChatType : std::uint8_t
{
    Group = 0,
    Direct = 1,
};

template <size_t Capacity>
/// <summary>
/// Строка фиксированной емкости для бинарного протокола (null-terminated).
/// </summary>
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
        size_t copy_size = (((Capacity) < (view.size())) ? (Capacity) : (view.size()));
        if (copy_size > 0)
            memcpy(buffer, view.data(), copy_size);

        buffer[copy_size] = '\0';
    }

    /// <summary>
    /// Максимальная емкость строки (без учета терминатора).
    /// </summary>
    size_t capacity() const   { return Capacity; }
    /// <summary>
    /// Длина строки до первого '\\0'.
    /// </summary>
    size_t size() const       { return strlen(buffer); }
    /// <summary>
    /// Проверка на пустую строку.
    /// </summary>
    bool empty() const        { return buffer[0] == '\0'; }
    /// <summary>
    /// C-строка, лежащая в буфере.
    /// </summary>
    const char* c_str() const { return buffer; }

    template <size_t other_size>
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

template <size_t N>
std::ostream& operator<<(std::ostream& os, const fixed_string<N>& str)
{
    return os << str.c_str();
}
