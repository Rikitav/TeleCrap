#include <optional>
#include <mutex>
#include <algorithm>
#include <vector>
#include <map>

#include <telecrap/Models.h>

#include "../include/MemoryCache.h"

static std::recursive_mutex chatsMutex;
static std::map<chatid_t, ChatMemory*> chatsCache;

std::optional<ChatMemory*> MemoryCache::getChatMemory(const chatid_t chatId)
{
	// Кэш используется UI-потоком и обработчиком update'ов, поэтому защищен mutex'ом.
	std::lock_guard<std::recursive_mutex> lk(chatsMutex);
	const auto find = chatsCache.find(chatId);
	
	if (find == chatsCache.end())
		return std::nullopt;

	return find->second;
}

ChatMemory* MemoryCache::createChatMemory(const Chat& chat)
{
	// Создание происходит при первом появлении чата в списке или при открытии чата.
	std::lock_guard<std::recursive_mutex> lk(chatsMutex);
	ChatMemory* chatMemory = new ChatMemory{
	    .ChatInfo = chat,
	    .Members = {},
	    .Messages = {},
	    .MessagesLoaded = false,
	};
	chatsCache[chat.Id] = chatMemory;
	return chatMemory;
}

void MemoryCache::removeChatMemory(const chatid_t chatId)
{
	// Важно: освобождаем выделенную ChatMemory, иначе будет утечка.
	std::lock_guard<std::recursive_mutex> lk(chatsMutex);
	std::optional<ChatMemory*> chatOpt = getChatMemory(chatId);
	
	if (!chatOpt.has_value())
		return;

	chatsCache.erase(chatId);
	delete chatOpt.value();
	return;
}

void MemoryCache::addMemberToChat(const chatid_t chatId, const User& user)
{
	std::lock_guard<std::recursive_mutex> lk(chatsMutex);
	std::optional<ChatMemory*> chatOpt = getChatMemory(chatId);
	
	if (!chatOpt.has_value())
		return;

	chatOpt.value()->Members.push_back(user);
	return;
}

void MemoryCache::removeMemberFromChat(const chatid_t chatId, userid_t userId)
{
	std::lock_guard<std::recursive_mutex> lk(chatsMutex);
	std::optional<ChatMemory*> chatOpt = getChatMemory(chatId);

	if (!chatOpt.has_value())
		return;

	std::vector<User>& members = chatOpt.value()->Members;
	members.erase(std::remove_if(members.begin(), members.end(),
		[&](const User& user) { return user.Id == userId; }));

	return;
}

void MemoryCache::renameChatInCache(const chatid_t chatId, fixed_string<CHATNAME_MAXLENGTH>& newName)
{
	std::lock_guard<std::recursive_mutex> lk(chatsMutex);
	std::optional<ChatMemory*> chatOpt = getChatMemory(chatId);

	if (!chatOpt.has_value())
		return;

	chatOpt.value()->ChatInfo.Name = newName;
	return;
}

void MemoryCache::storeMessageToChat(const chatid_t chatId, const Message& message, bool addLast)
{
	std::lock_guard<std::recursive_mutex> lk(chatsMutex);
	std::optional<ChatMemory*> chatOpt = getChatMemory(chatId);

	if (!chatOpt.has_value())
		return;

	std::vector<Message>& messages = chatOpt.value()->Messages;
	// Дедупликация: update'ы могут приходить повторно, а история может загружаться пачкой.
	auto searchDupl = std::find_if(messages.begin(), messages.end(),
		[&](const Message& storedMsg) { return message.Id == storedMsg.Id; });

	if (searchDupl != messages.end())
		return;

	if (addLast)
	{
		messages.push_back(message);
	}
	else
	{
		messages.insert(messages.begin(), message);
	}
}

void MemoryCache::appendMessageToChat(const chatid_t chatId, const Message& message)
{
	std::lock_guard<std::recursive_mutex> lk(chatsMutex);
	std::optional<ChatMemory*> chatOpt = getChatMemory(chatId);

	if (!chatOpt.has_value())
		return;

	std::vector<Message>& messages = chatOpt.value()->Messages;
	messages.push_back(message);
	return;
}
