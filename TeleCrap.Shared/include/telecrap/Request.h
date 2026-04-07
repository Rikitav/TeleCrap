#pragma once

#include <cstdint>
#include <string>

#include "Models.h"

#pragma pack(push, 1)

struct HandshakeRequest
{
	connflag_t Flag;
	version_t Version;
};

struct AuthRequest
{
	token_t UserToken;
	fixed_string<CHATNAME_MAXLENGTH> Username;
	fixed_string<PASSWORD_MAXLENGTH> Password;
};

struct GetUpdatesRequest
{
	token_t UserToken;
	std::uint8_t DropPending;
	std::uint8_t Reserved[3];
};

struct GetChatInfoRequest
{
	token_t UserToken;
	fixed_string<CHATNAME_MAXLENGTH> ChatQuery;
};

struct GetChatHistoryRequest
{
	token_t UserToken;
	chatid_t ChatId;
};

struct GetChatMembersRequest
{
	token_t UserToken;
	chatid_t ChatId;
};

struct GetChatListRequest
{
	token_t UserToken;
};

struct GetAddonCommandsRequest
{
	token_t UserToken;
};

struct CommitMessageRequest
{
	token_t UserToken;
	timestamp_t Timestamp;
	fixed_string<MESSAGETEXT_MAXLENGTH> Content;
	chatid_t ToChat;
};

struct SpawnGroupChatRequest
{
	token_t UserToken;
	fixed_string<CHATNAME_MAXLENGTH> Chatname;
};

struct JoinGroupChatRequest
{
	token_t UserToken;
	fixed_string<CHATNAME_MAXLENGTH> ChatQuery;
};

enum class RequestType : std::uint32_t
{
	Handshake,
	Register, Login,
	GetUpdates,

	GetChatInfo,
	GetChatHistory,
	GetChatMembers,
	GetChatList,
	GetAddonCommands,

	SpawnGroupChat,
	JoinGroupChat,
	CommitMessage,

	KickMember,
	BanMember,
	UnbanMember,
};

struct Request
{
	RequestType Type;

	union
	{
		HandshakeRequest Handshake;
		AuthRequest Auth;
		GetUpdatesRequest GetUpdates;

		GetChatInfoRequest GetChatInfo;
		GetChatHistoryRequest GetChatHistory;
		GetChatMembersRequest GetChatMembers;
		GetChatListRequest GetChatList;
		GetAddonCommandsRequest GetAddonCommands;

		SpawnGroupChatRequest SpawnGroupChat;
		JoinGroupChatRequest JoinGroupChat;
		CommitMessageRequest CommitMessage;
	};

	/// <summary>
	/// Создает handshake-запрос для установки протокольной сессии.
	/// </summary>
	static Request CreateHandshake(const connflag_t& flag);

	/// <summary>
	/// Создает запрос логина пользователя.
	/// </summary>
	static Request CreateLogin(const token_t& token, const std::string& username, const std::string& password);

	/// <summary>
	/// Создает запрос регистрации пользователя.
	/// </summary>
	static Request CreateRegister(const token_t& token, const std::string& username, const std::string& password);
	
	/// <summary>
	/// Создает запрос получения серверных update-событий.
	/// </summary>
	static Request CreateGetUpdates(const token_t& token, bool dropPending);

	/// <summary>
	/// Создает запрос получения информации о чате по строке поиска.
	/// </summary>
	static Request CreateGetChatInfo(const token_t& token, const std::string& chatQuery);
	
	/// <summary>
	/// Создает запрос списка участников чата.
	/// </summary>
	static Request CreateGetChatMembers(const token_t& token, const chatid_t chatId);
	
	/// <summary>
	/// Создает запрос истории сообщений чата.
	/// </summary>
	static Request CreateGetChatHistory(const token_t& token, const chatid_t chatId);
	
	/// <summary>
	/// Создает запрос списка чатов пользователя.
	/// </summary>
	static Request CreateGetChatList(const token_t& token);

	/// <summary>
	/// Создает запрос списка команд, зарегистрированных аддонами на сервере.
	/// </summary>
	static Request CreateGetAddonCommands(const token_t& token);

	/// <summary>
	/// Создает запрос на создание группового чата.
	/// </summary>
	static Request CreateSpawnGroupChat(const token_t& token, const std::string& chatName);
	
	/// <summary>
	/// Создает запрос на вступление в групповой чат.
	/// </summary>
	static Request CreateJoinGroupChat(const token_t& token, const std::string& chatQuery);
	
	/// <summary>
	/// Создает запрос отправки сообщения в чат.
	/// </summary>
	static Request CreateCommitMessage(const token_t& token, const chatid_t chatId, const std::string& content);
};

#pragma pack(pop)
