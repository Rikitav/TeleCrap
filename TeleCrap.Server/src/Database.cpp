#include <iostream>
#include <fstream>
#include <string>
#include <mutex>
#include <exception>
#include <memory>
#include <optional>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <algorithm>

#ifdef _WIN32
#include <Windows.h>
#endif

#include <telecrap/Models.h>
#include <telecrap/Request.h>
#include <telecrap/Log.h>
#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>
#include <SQLiteCpp/Transaction.h>

#include "../include/Database.h"
#include "../include/ChatHistory.h"

static std::recursive_mutex DbMutex;
static std::unique_ptr<SQLite::Database> DbSql;

// Соль привязана к машине: хеши паролей в telecrap.db не переносятся на другой хост без сброса паролей.
static std::string readMachineBoundSalt()
{
#ifdef _WIN32
    HKEY key = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, R"(SOFTWARE\Microsoft\Cryptography)", 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
        return {};

    char buffer[256]{};
    DWORD bufferSize = sizeof(buffer);
    DWORD valueType = 0;
    const LSTATUS st = RegQueryValueExA(key, "MachineGuid", nullptr, &valueType,reinterpret_cast<LPBYTE>(buffer), &bufferSize);
    RegCloseKey(key);

    if (st != ERROR_SUCCESS || (valueType != REG_SZ && valueType != REG_EXPAND_SZ))
        return {};

    return std::string(buffer);
#else
    auto readOneLineFile = [](const char* path) -> std::string
        {
            std::ifstream file(path);
            if (!file)
                return {};

            std::string line;
            if (!std::getline(file, line))
                return {};
            
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
                line.pop_back();
            
            return line;
        };

    std::string id = readOneLineFile("/etc/machine-id");
    if (!id.empty())
        return id;

    id = readOneLineFile("/var/lib/dbus/machine-id");
    if (!id.empty())
        return id;

    return readOneLineFile("/sys/class/dmi/id/product_uuid");
#endif
}

static const std::string& passwordSalt()
{
    static std::string value;
    static std::once_flag once;
    std::call_once(once, []
    {
        std::string machine = readMachineBoundSalt();
        if (machine.empty())
        {
            Log::Info("Database", "WARN: не удалось прочитать machine-id (реестр / machine-id / DMI); резервная соль — слабее привязка к машине.");
            machine = "MEDIC GAMING!";
        }
        
        value = machine + "|TeleCrap-password-pepper-v1";
    });

    return value;
}

static size_t hashPassword(const std::string& salt, const std::string& password)
{
    // Пароль в БД не хранится. Сохраняется std::hash(salt + password).
    // Это не криптографический хеш, но достаточен для учебного/локального проекта.
    std::string saltedPassword = salt + password;
    std::hash<std::string> hasher;
    return hasher(saltedPassword);
}

static void execTableCreation(SQLite::Database* DbSql)
{
    // Схема создается лениво при старте сервера. foreign_keys включаем в Init().
    // 1. Users
    DbSql->exec(R"(
        CREATE TABLE IF NOT EXISTS Users (
            Id INTEGER PRIMARY KEY AUTOINCREMENT,
            Name TEXT UNIQUE NOT NULL,
            PasswordHash INTEGER NOT NULL
        );
    )");

    // 2. Chats
    DbSql->exec(R"(
        CREATE TABLE IF NOT EXISTS Chats (
            Id INTEGER PRIMARY KEY AUTOINCREMENT,
            OwnerId INTEGER,
            Name TEXT,
            FOREIGN KEY(OwnerId) REFERENCES Users(Id) ON DELETE CASCADE
        );
    )");

    // 3. DirectChats (Личные переписки)
    DbSql->exec(R"(
        CREATE TABLE IF NOT EXISTS DirectChats (
            ChatId INTEGER PRIMARY KEY,
            User1Id INTEGER NOT NULL,
            User2Id INTEGER NOT NULL,
            UNIQUE(User1Id, User2Id),
            FOREIGN KEY(ChatId) REFERENCES Chats(Id) ON DELETE CASCADE,
            FOREIGN KEY(User1Id) REFERENCES Users(Id) ON DELETE CASCADE,
            FOREIGN KEY(User2Id) REFERENCES Users(Id) ON DELETE CASCADE
        );
    )");

    // 4. Messages
    DbSql->exec(R"(
        CREATE TABLE IF NOT EXISTS Messages (
            Id INTEGER PRIMARY KEY AUTOINCREMENT,
            ChatId INTEGER NOT NULL,
            SenderId INTEGER NOT NULL,
            Timestamp INTEGER NOT NULL,
            Text TEXT,
            FOREIGN KEY(ChatId) REFERENCES Chats(Id) ON DELETE CASCADE,
            FOREIGN KEY(SenderId) REFERENCES Users(Id) ON DELETE CASCADE
        );
    )");

    // 5. Users_has_Chats (Участники групповых чатов)
    DbSql->exec(R"(
        CREATE TABLE IF NOT EXISTS Users_has_Chats (
            UserId INTEGER NOT NULL,
            ChatId INTEGER NOT NULL,
            PRIMARY KEY (UserId, ChatId),
            FOREIGN KEY(UserId) REFERENCES Users(Id) ON DELETE CASCADE,
            FOREIGN KEY(ChatId) REFERENCES Chats(Id) ON DELETE CASCADE
        );
    )");

    DbSql->exec(R"(
        CREATE TABLE IF NOT EXISTS Chats_has_BannedUsers (
            ChatId INTEGER NOT NULL,
            UserId INTEGER NOT NULL,
            PRIMARY KEY (ChatId, UserId),
            FOREIGN KEY(ChatId) REFERENCES Chats(Id) ON DELETE CASCADE,
            FOREIGN KEY(UserId) REFERENCES Users(Id) ON DELETE CASCADE
        );
    )");
}

std::string Database::DbConnection;

void Database::Init()
{
    try
    {
        // Файл БД создается рядом с бинарником.
        DbSql = std::make_unique<SQLite::Database>(DbConnection, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
        DbSql->exec("PRAGMA foreign_keys = ON;");
        execTableCreation(DbSql.get());

        SQLite::Statement query(*DbSql, "SELECT Id, ChatId, SenderId, Timestamp, Text FROM Messages");

        // creating system user
        if (!findUserById(SYSTEM_FROMID).has_value())
        {
            // SYSTEM пользователь нужен для системных сообщений и direct-чатов (OwnerId = SYSTEM_FROMID).
            std::lock_guard<std::recursive_mutex> dbLock(DbMutex);
            SQLite::Transaction transaction(*DbSql);
            SQLite::Statement queryChat(*DbSql, "INSERT INTO Users (Name, PasswordHash) VALUES (?, ?)");

            queryChat.bind(1, "SYSTEM");
            queryChat.bind(2, static_cast<int64_t>(0));
            queryChat.exec();
            transaction.commit();
        }

        Log::Info("Database", std::string("SQLite DB initialized successfully. Version: ") + std::to_string(DbSql->getHeaderInfo().sqliteVersion));
    }
    catch (const std::exception& exc)
    {
        Log::Error("Database", std::string("SQLite Exception: ") + exc.what());
        throw;
    }
}

std::optional<UserInfo> Database::findUserById(const std::optional<userid_t> userId)
{
    if (!userId.has_value())
        return std::nullopt;

    std::lock_guard<std::recursive_mutex> dbLock(DbMutex);
    SQLite::Statement query(*DbSql, "SELECT Id, Name, PasswordHash FROM Users WHERE Id = ?");
    query.bind(1, userId.value());

    if (query.executeStep()) // Если нашли строку
    {
        return UserInfo
        {
            .Id = query.getColumn(0).getUInt(),
            .Name = std::string_view(query.getColumn(1).getString()),
            .PasswordHash = static_cast<size_t>(query.getColumn(2).getInt64()),
        };
    }

    return std::nullopt;
}

std::optional<UserInfo> Database::findUserByName(const fixed_string<CHATNAME_MAXLENGTH>& name)
{
    std::lock_guard<std::recursive_mutex> dbLock(DbMutex);
    SQLite::Statement query(*DbSql, "SELECT Id, Name, PasswordHash FROM Users WHERE Name = ?");
    query.bind(1, name.c_str());

    if (query.executeStep()) // Если нашли строку
    {
        return UserInfo
        {
            .Id = query.getColumn(0).getUInt(),
            .Name = std::string_view(query.getColumn(1).getString()),
            .PasswordHash = static_cast<size_t>(query.getColumn(2).getInt64()),
        };
    }

    return std::nullopt;
}

std::optional<ChatInfo> Database::findChatById(const std::optional<chatid_t> chatId)
{
    if (!chatId.has_value())
        return std::nullopt;

    std::lock_guard<std::recursive_mutex> dbLock(DbMutex);
    SQLite::Statement query(*DbSql, "SELECT Id, Name, OwnerId FROM Chats WHERE Id = ?");
    query.bind(1, chatId.value());

    if (query.executeStep()) // Если нашли строку
    {
        return ChatInfo
        {
            .Id = query.getColumn(0).getUInt(),
            .Name = std::string_view(query.getColumn(1).getString()),
            .OwnerId = query.getColumn(2).getUInt(),
        };
    }

    return std::nullopt;
}

std::optional<ChatInfo> Database::findChatByName(const fixed_string<CHATNAME_MAXLENGTH>& name)
{
    std::lock_guard<std::recursive_mutex> dbLock(DbMutex);
    SQLite::Statement query(*DbSql, "SELECT Id, Name, OwnerId FROM Chats WHERE Name = ?");
    query.bind(1, name.c_str());

    if (query.executeStep()) // Если нашли строку
    {
        return ChatInfo
        {
            .Id = query.getColumn(0).getUInt(),
            .Name = std::string_view(query.getColumn(1).getString()),
            .OwnerId = query.getColumn(2).getUInt(),
        };
    }

    return std::nullopt;
}

std::optional<DirectChatInfo> Database::findDirectChat(const userid_t user1Id, const userid_t user2Id)
{
    std::lock_guard<std::recursive_mutex> dbLock(DbMutex);
    SQLite::Statement query(*DbSql, "SELECT ChatId, User1Id, User2Id FROM DirectChats WHERE User1Id = ? AND User2Id = ?");
    query.bind(1, (std::min)(user1Id, user2Id));
    query.bind(2, (std::max)(user1Id, user2Id));

    if (query.executeStep()) // Если нашли строку
    {
        DirectChatInfo direct{};
        direct.Id = query.getColumn(0).getUInt();
        direct.User1Id = query.getColumn(1).getUInt();
        direct.User2Id = query.getColumn(2).getUInt();

        return direct;
    }

    return std::nullopt;
}

std::optional<UserInfo> Database::findDirectChatPeer(const chatid_t chatId, const userid_t requestorId)
{
    std::lock_guard<std::recursive_mutex> dbLock(DbMutex);
    SQLite::Statement query(*DbSql, "SELECT User1Id, User2Id FROM DirectChats WHERE ChatId = ?");
    query.bind(1, static_cast<int64_t>(chatId));

    if (!query.executeStep())
        return std::nullopt;

    const userid_t u1 = query.getColumn(0).getUInt();
    const userid_t u2 = query.getColumn(1).getUInt();

    if (requestorId != u1 && requestorId != u2)
        return std::nullopt;

    userid_t peer = requestorId == u1 ? u2 : u1;
    return findUserById(peer);
}

std::vector<ChatInfo> Database::findChatsByUser(const UserInfo& user)
{
    std::lock_guard<std::recursive_mutex> dbLock(DbMutex);

    try
    {
        SQLite::Statement query(*DbSql, "SELECT c.Id, c.Name, c.OwnerId FROM Chats c JOIN Users_has_Chats uhc ON c.Id = uhc.ChatId WHERE uhc.UserId = ?");
        query.bind(1, user.Id);

        std::vector<ChatInfo> chats;
        while (query.executeStep())
        {
            ChatInfo chat{};
            chat.Id = query.getColumn(0).getUInt();
            chat.Name = query.getColumn(1).getString();
            chat.OwnerId = query.getColumn(2).getUInt();

            chats.push_back(chat);
        }

        return chats;
    }
    catch (std::exception& exc)
    {
        Log::Error("Database", std::string("findChatsByUser failed: ") + exc.what());
        throw;
    }
}

std::vector<UserInfo> Database::findMembersByChat(const ChatInfo& chat)
{
    std::lock_guard<std::recursive_mutex> dbLock(DbMutex);

    try
    {
        SQLite::Statement query(*DbSql, "SELECT u.Id, u.Name, u.PasswordHash, uhc.UserId, uhc.ChatId FROM Users u JOIN Users_has_Chats uhc ON u.Id = uhc.UserId WHERE uhc.ChatId = ?");
        query.bind(1, chat.Id);

        std::vector<UserInfo> users;
        while (query.executeStep())
        {
            UserInfo user{};
            user.Id = query.getColumn(0).getUInt();
            user.Name = query.getColumn(1).getString();
            user.PasswordHash = query.getColumn(2).getUInt();

            users.push_back(user);
        }

        return users;
    }
    catch (std::exception& exc)
    {
        Log::Error("Database", std::string("findMembersByChat failed: ") + exc.what());
        throw;
    }
}

std::vector<Message> Database::findMessagesByChat(const ChatInfo& chat)
{
    std::lock_guard<std::recursive_mutex> dbLock(DbMutex);

    try
    {
        SQLite::Statement query(*DbSql, "SELECT Id, ChatId, SenderId, Timestamp, Text FROM Messages WHERE ChatId = ?");
        query.bind(1, chat.Id);

        std::vector<Message> messages;
        while (query.executeStep())
        {
            userid_t senderId = query.getColumn(2).getUInt();
            std::optional<UserInfo> sender = findUserById(senderId);

            Message message{};
            message.Id = query.getColumn(0).getUInt();
            message.DestChat = Chat{ .Id = chat.Id, .Name = chat.Name };
            message.From = User{ .Id = sender->Id, .Name = sender->Name };
            message.Timestamp = query.getColumn(3).getUInt();
            message.Text = std::string_view(query.getColumn(4).getString());

            messages.push_back(message);
        }

        return messages;
    }
    catch (std::exception& exc)
    {
        Log::Error("Database", std::string("findMessagesByChat failed: ") + exc.what());
        throw;
    }
}

bool Database::verifyPassword(const std::string& password, const std::size_t& salted_hash)
{
    return hashPassword(passwordSalt(), password) == salted_hash;
}

UserInfo Database::createUser(const fixed_string<CHATNAME_MAXLENGTH>& username, const fixed_string<PASSWORD_MAXLENGTH>& password)
{
    std::lock_guard<std::recursive_mutex> dbLock(DbMutex);

    try
    {
        SQLite::Transaction transaction(*DbSql);
        size_t passwordHash = hashPassword(passwordSalt(), password.c_str());

        SQLite::Statement queryChat(*DbSql, "INSERT INTO Users (Name, PasswordHash) VALUES (?, ?)");
        queryChat.bind(1, username.c_str());
        queryChat.bind(2, static_cast<int64_t>(passwordHash));
        queryChat.exec();

        chatid_t newUserId = static_cast<chatid_t>(DbSql->getLastInsertRowid());
        transaction.commit();

        return UserInfo
        {
            .Id = newUserId,
            .Name = username,
            .PasswordHash = passwordHash,
        };
    }
    catch (std::exception& exc)
    {
        Log::Error("Database", std::string("createUser failed: ") + exc.what());
        throw;
    }
}

ChatInfo Database::createGroupChat(UserInfo& ownerInfo, const fixed_string<CHATNAME_MAXLENGTH>& chatname)
{
    std::lock_guard<std::recursive_mutex> dbLock(DbMutex);

    try
    {
        SQLite::Transaction transaction(*DbSql);
        SQLite::Statement queryChat(*DbSql, "INSERT INTO Chats (Name, OwnerId) VALUES (?, ?)");
        queryChat.bind(1, chatname.c_str());
        queryChat.bind(2, ownerInfo.Id);
        queryChat.exec();

        chatid_t newChatId = static_cast<chatid_t>(DbSql->getLastInsertRowid());

        SQLite::Statement queryMember(*DbSql, "INSERT INTO Users_has_Chats (ChatId, UserId) VALUES (?, ?)");
        queryMember.bind(1, newChatId);
        queryMember.bind(2, ownerInfo.Id);
        queryMember.exec();

        transaction.commit();
        return ChatInfo
        {
            .Id = newChatId,
            .Name = chatname,
            .OwnerId = ownerInfo.Id,
        };
    }
    catch (std::exception& exc)
    {
        Log::Error("Database", std::string("createGroupChat failed: ") + exc.what());
        throw;
    }
}

DirectChatInfo Database::createDirectChat(const UserInfo& user1, const UserInfo& user2)
{
    std::lock_guard<std::recursive_mutex> dbLock(DbMutex);

    UserInfo owner{ .Id = SYSTEM_FROMID, .Name = {}, .PasswordHash = 0 };
    ChatInfo directChat = createGroupChat(owner, std::string_view("direct_chat_" + std::string(user1.Name) + "_and_" + std::string(user2.Name)));

    try
    {
        SQLite::Transaction transaction(*DbSql);

        SQLite::Statement queryMember1(*DbSql, "INSERT INTO Users_has_Chats (ChatId, UserId) VALUES (?, ?)");
        queryMember1.bind(1, directChat.Id);
        queryMember1.bind(2, user1.Id);
        queryMember1.exec();

        SQLite::Statement queryMember2(*DbSql, "INSERT INTO Users_has_Chats (ChatId, UserId) VALUES (?, ?)");
        queryMember2.bind(1, directChat.Id);
        queryMember2.bind(2, user2.Id);
        queryMember2.exec();

        SQLite::Statement queryDirect(*DbSql, "INSERT INTO DirectChats (ChatId, User1Id, User2Id) VALUES (?, ?, ?)");
        queryDirect.bind(1, directChat.Id);
        queryDirect.bind(2, (std::min)(user1.Id, user2.Id));
        queryDirect.bind(3, (std::max)(user1.Id, user2.Id));
        queryDirect.exec();

        transaction.commit();
        DirectChatInfo direct{};
        direct.Id = directChat.Id;
        direct.Name = directChat.Name;
        direct.OwnerId = directChat.OwnerId;
        direct.User1Id = user1.Id;
        direct.User2Id = user2.Id;
        return direct;
    }
    catch (std::exception& exc)
    {
        Log::Error("Database", std::string("createDirectChat failed: ") + exc.what());
        throw;
    }
}

void Database::joinChatMember(const ChatInfo& chatInfo, const UserInfo& userInfo)
{
    try
    {
        std::lock_guard<std::recursive_mutex> dbLock(DbMutex);
        SQLite::Statement q(*DbSql, "INSERT OR IGNORE INTO Users_has_Chats (UserId, ChatId) VALUES (?, ?)");
        q.bind(1, static_cast<int64_t>(userInfo.Id));
        q.bind(2, static_cast<int64_t>(chatInfo.Id));
        q.exec();
    }
    catch (std::exception& exc)
    {
        Log::Error("Database", std::string("joinChatMember failed: ") + exc.what());
        throw;
    }
}

Message Database::commitMessage(UserInfo& senderInfo, const CommitMessageRequest& message)
{
    try
    {
        std::optional<ChatInfo> chatOpt = findChatById(message.ToChat);
        if (chatOpt == std::nullopt)
            throw std::runtime_error("Chat not found after message commit");

        std::optional<UserInfo> senderOpt = findUserById(senderInfo.Id);
        if (senderOpt == std::nullopt)
            throw std::runtime_error("Sender not found after message commit");

        std::lock_guard<std::recursive_mutex> dbLock(DbMutex);
        SQLite::Transaction transaction(*DbSql);
        SQLite::Statement query(*DbSql, "INSERT INTO Messages (Timestamp, Text, ChatId, SenderId) VALUES (?, ?, ?, ?)");

        query.bind(1, message.Timestamp);
        query.bind(2, message.Content.c_str());
        query.bind(3, message.ToChat);
        query.bind(4, senderInfo.Id);
        query.exec();

        transaction.commit();
        msgid_t newMessageId = static_cast<msgid_t>(DbSql->getLastInsertRowid());

        return Message
        {
            .Id = newMessageId,
            .DestChat = Chat{ .Id = chatOpt->Id, .Name = chatOpt->Name },
            .From = User{ .Id = senderOpt->Id, .Name = senderOpt->Name },
            .Timestamp = message.Timestamp,
            .Text = message.Content,
        };
    }
    catch (std::exception& exc)
    {
        Log::Error("Database", std::string("commitMessage failed: ") + exc.what());
        throw;
    }
}

Message Database::commitSystemMessage(const ChatInfo& chat, timestamp_t ts, const std::string& text)
{
    try
    {
        std::optional<UserInfo> systemOpt = findUserById(SYSTEM_FROMID);
        if (systemOpt == std::nullopt)
            throw std::runtime_error("System user missing");

        std::lock_guard<std::recursive_mutex> dbLock(DbMutex);
        SQLite::Transaction transaction(*DbSql);
        SQLite::Statement query(*DbSql, "INSERT INTO Messages (Timestamp, Text, ChatId, SenderId) VALUES (?, ?, ?, ?)");

        query.bind(1, ts);
        query.bind(2, text);
        query.bind(3, chat.Id);
        query.bind(4, static_cast<int64_t>(SYSTEM_FROMID));
        query.exec();

        transaction.commit();
        msgid_t newMessageId = static_cast<msgid_t>(DbSql->getLastInsertRowid());

        return Message
        {
            .Id = newMessageId,
            .DestChat = Chat{ .Id = chat.Id, .Name = chat.Name },
            .From = User{ .Id = systemOpt->Id, .Name = systemOpt->Name },
            .Timestamp = ts,
            .Text = std::string_view(text.c_str()),
        };
    }
    catch (std::exception& exc)
    {
        Log::Error("Database", std::string("commitSystemMessage failed: ") + exc.what());
        throw;
    }
}

bool Database::isUserBannedFromChat(chatid_t chatId, userid_t userId)
{
    try
    {
        std::lock_guard<std::recursive_mutex> dbLock(DbMutex);
        SQLite::Statement q(*DbSql, "SELECT 1 FROM Chats_has_BannedUsers WHERE ChatId = ? AND UserId = ? LIMIT 1");
        q.bind(1, static_cast<int64_t>(chatId));
        q.bind(2, static_cast<int64_t>(userId));
        return q.executeStep();
    }
    catch (std::exception& exc)
    {
        Log::Error("Database", std::string("commitMessage failed: ") + exc.what());
        throw;
    }
}

void Database::banUserInChat(const ChatInfo& chat, const UserInfo& target)
{
    try
    {
        std::lock_guard<std::recursive_mutex> dbLock(DbMutex);
        SQLite::Statement q(*DbSql, "INSERT OR IGNORE INTO Chats_has_BannedUsers (ChatId, UserId) VALUES (?, ?)");
        q.bind(1, static_cast<int64_t>(chat.Id));
        q.bind(2, static_cast<int64_t>(target.Id));
        q.exec();
    }
    catch (std::exception& exc)
    {
        Log::Error("Database", std::string("banUserInChat failed: ") + exc.what());
        throw;
    }
}

void Database::unbanUserInChat(chatid_t chatId, userid_t userId)
{
    try
    {
        std::lock_guard<std::recursive_mutex> dbLock(DbMutex);
        SQLite::Statement q(*DbSql, "DELETE FROM Chats_has_BannedUsers WHERE ChatId = ? AND UserId = ?");
        q.bind(1, static_cast<int64_t>(chatId));
        q.bind(2, static_cast<int64_t>(userId));
        q.exec();
    }
    catch (std::exception& exc)
    {
        Log::Error("Database", std::string("unbanUserInChat failed: ") + exc.what());
        throw;
    }
}

void Database::removeMemberFromChat(chatid_t chatId, userid_t userId)
{
    try
    {
        std::lock_guard<std::recursive_mutex> dbLock(DbMutex);
        SQLite::Statement q(*DbSql, "DELETE FROM Users_has_Chats WHERE ChatId = ? AND UserId = ?");
        q.bind(1, static_cast<int64_t>(chatId));
        q.bind(2, static_cast<int64_t>(userId));
        q.exec();
    }
    catch (std::exception& exc)
    {
        Log::Error("Database", std::string("removeMemberFromChat failed: ") + exc.what());
        throw;
    }
}

void Database::renameChat(const ChatInfo& chat, const fixed_string<CHATNAME_MAXLENGTH>& newName)
{
    try
    {
        std::lock_guard<std::recursive_mutex> dbLock(DbMutex);
        SQLite::Statement q(*DbSql, "UPDATE Chats SET Name = ? WHERE Id = ?");
        q.bind(1, newName.c_str());
        q.bind(2, static_cast<int64_t>(chat.Id));
        q.exec();
    }
    catch (std::exception& exc)
    {
        Log::Error("Database", std::string("renameChat failed: ") + exc.what());
        throw;
    }
}

