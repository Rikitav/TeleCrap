#pragma once
#include <string>
#include <memory>
#include <optional>
#include <vector>

#include <telecrap/Models.h>
#include <telecrap/Request.h>
#include <SQLiteCpp/Database.h>

#include "ChatHistory.h"

/// <summary>
/// Статический слой доступа к SQLite: хранение пользователей, чатов, членства и сообщений.
/// </summary>
class Database
{
    static std::unique_ptr<SQLite::Database> DbSql;

public:
    /// <summary>
    /// Открывает БД, применяет PRAGMA и создает таблицы при первом запуске.
    /// </summary>
    static void Init();

    /// <summary>
    /// Возвращает пользователя по идентификатору.
    /// </summary>
    static std::optional<UserInfo> findUserById(const std::optional<userid_t> userId);
    /// <summary>
    /// Возвращает пользователя по имени.
    /// </summary>
    static std::optional<UserInfo> findUserByName(const fixed_string<CHATNAME_MAXLENGTH>& name);

    /// <summary>
    /// Возвращает чат по идентификатору.
    /// </summary>
    static std::optional<ChatInfo> findChatById(const std::optional<chatid_t> chatId);
    /// <summary>
    /// Возвращает чат по имени.
    /// </summary>
    static std::optional<ChatInfo> findChatByName(const fixed_string<CHATNAME_MAXLENGTH>& name);
    /// <summary>
    /// Ищет direct-чат между двумя пользователями.
    /// </summary>
    static std::optional<DirectChatInfo> findDirectChat(const userid_t user1Id, const userid_t user2Id);
    /// <summary>
    /// Возвращает собеседника direct-чата относительно requestorId.
    /// </summary>
    static std::optional<UserInfo> findDirectChatPeer(const chatid_t chatId, const userid_t requestorId);

    /// <summary>
    /// Возвращает список чатов, в которых состоит пользователь.
    /// </summary>
    static std::vector<ChatInfo> findChatsByUser(const UserInfo& user);
    /// <summary>
    /// Возвращает участников указанного чата.
    /// </summary>
    static std::vector<UserInfo> findMembersByChat(const ChatInfo& chat);
    /// <summary>
    /// Возвращает историю сообщений чата.
    /// </summary>
    static std::vector<Message> findMessagesByChat(const ChatInfo& chatid);

    /// <summary>
    /// Создает нового пользователя.
    /// </summary>
	static UserInfo createUser(const fixed_string<CHATNAME_MAXLENGTH>& username, const fixed_string<PASSWORD_MAXLENGTH>& password);
    /// <summary>
    /// Создает групповой чат с указанным владельцем.
    /// </summary>
    static ChatInfo createGroupChat(UserInfo& ownerInfo, const fixed_string<CHATNAME_MAXLENGTH>& chatname);
    /// <summary>
    /// Создает direct-чат между двумя пользователями.
    /// </summary>
    static DirectChatInfo createDirectChat(const UserInfo& user1, const UserInfo& user2);

    /// <summary>
    /// Добавляет пользователя в чат.
    /// </summary>
    static void joinChatMember(const ChatInfo& chatInfo, const UserInfo& userInfo);
    /// <summary>
    /// Сохраняет пользовательское сообщение в БД.
    /// </summary>
    static Message commitMessage(UserInfo& senderInfo, const CommitMessageRequest& message);
    /// <summary>
    /// Сохраняет системное сообщение в БД.
    /// </summary>
    static Message commitSystemMessage(const ChatInfo& chat, timestamp_t ts, const std::string& text);

    /// <summary>
    /// Проверяет, забанен ли пользователь в чате.
    /// </summary>
    static bool isUserBannedFromChat(chatid_t chatId, userid_t userId);
    /// <summary>
    /// Банит пользователя в чате.
    /// </summary>
    static void banUserInChat(const ChatInfo& chat, const UserInfo& target);
    /// <summary>
    /// Снимает бан пользователя в чате.
    /// </summary>
    static void unbanUserInChat(chatid_t chatId, userid_t userId);
    /// <summary>
    /// Удаляет участника из чата.
    /// </summary>
    static void removeMemberFromChat(chatid_t chatId, userid_t userId);
    /// <summary>
    /// Переименовывает чат.
    /// </summary>
    static void renameChat(const ChatInfo& chat, const fixed_string<CHATNAME_MAXLENGTH>& newName);

    /// <summary>
    /// Проверяет пароль относительно сохраненного salted hash.
    /// </summary>
    static bool verifyPassword(const std::string& password, const std::size_t& salted_hash);
};
