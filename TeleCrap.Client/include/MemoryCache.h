#pragma once
#include <optional>
#include <vector>

#include <telecrap/Models.h>

/// <summary>
/// In-memory кэш клиента для чат-моделей, участников и истории сообщений.
/// </summary>
struct ChatMemory
{
	Chat ChatInfo;
	std::vector<User> Members;
	std::vector<Message> Messages;
	bool MessagesLoaded = false;
};

/// <summary>
/// Статический API для работы с клиентским кэшем чатов.
/// </summary>
class MemoryCache
{
public:
	/// <summary>
	/// Возвращает кэш чата по id, если он уже создан.
	/// </summary>
	static std::optional<ChatMemory*> getChatMemory(const chatid_t chatId);

	/// <summary>
	/// Создает новый кэш-объект для чата.
	/// </summary>
	static ChatMemory* createChatMemory(const Chat& chat);

	/// <summary>
	/// Удаляет кэш чата по id.
	/// </summary>
	static void removeChatMemory(const chatid_t chatId);

	/// <summary>
	/// Добавляет участника в кэш чата.
	/// </summary>
	static void addMemberToChat(const chatid_t chatId, const User& user);

	/// <summary>
	/// Удаляет участника из кэша чата.
	/// </summary>
	static void removeMemberFromChat(const chatid_t chatId, userid_t userId);

	/// <summary>
	/// Обновляет имя чата в кэше.
	/// </summary>
	static void renameChatInCache(const chatid_t chatId, fixed_string<CHATNAME_MAXLENGTH>& newName);

	/// <summary>
	/// Добавляет сообщение в кэш чата (в начало или в конец).
	/// </summary>
	static void storeMessageToChat(const chatid_t chatId, const Message& message, bool addLast);

	/// <summary>
	/// Добавляет сообщение в конец истории чата.
	/// </summary>
	static void appendMessageToChat(const chatid_t chatId, const Message& message);
};
