#pragma once

#include <cstdint>
#include <string_view>

#include "Update.h"
#include "Models.h"

#define ERR_OK 0
#define ERR_EXPIRED 1
#define ERR_UNAUTHORIZED 2

#pragma pack(push, 1)

struct ErrorResponce
{
	uint32_t Code;
	fixed_string<ERRORMESSAGE_MAXLENGTH> Message;
};

struct HandshakeResponce
{
	connflag_t Flag;
	token_t Token;
	std::uint8_t SecureMode;
	std::uint8_t Reserved[3];
	std::uint64_t IntegrityTag;
	std::uint8_t ServerPublicKey[32];
};

struct AuthResponce
{
	User UserModel;
};

struct GetUpdateResponce
{
	std::int32_t RemainingUpdates;
	Update CurrentUpdate;
};

struct GetChatInfoResponce
{
	Chat ChatModel;
};

struct GetChatHistoryResponce
{
	std::int32_t RemainingMessages;
	Message CurrentMessage;
};

struct GetChatMembersResponce
{
	std::int32_t RemainingUsers;
	User CurrentUser;
};

struct GetChatListResponce
{
	std::int32_t RemainingChats;
	Chat CurrentChat;
};

struct GetAddonCommandsResponce
{
	std::int32_t RemainingCommands;
	fixed_string<CHATNAME_MAXLENGTH> CurrentCommand;
};

struct CreateChatResponce
{
	Chat ChatModel;
};

struct JoinChatResponce
{
	Chat ChatModel;
};

struct CommitMessageResponce
{
	Message MessageModel;
};

enum class ResponceType : std::uint32_t
{
	Error,
	HandshakeSuccessfull,
	LoginSuccessfull,
	GetUpdates,

	GetChatInfo,
	GetChatMembers,
	GetChatHistory,
	GetChatList,
	GetAddonCommands,

	CreateChat,
	JoinChat,
	CommitMessage,
};

struct Responce
{
	ResponceType Type;

	union
	{
		HandshakeResponce Handshake;
		ErrorResponce Error;
		AuthResponce Auth;
		GetUpdateResponce GetUpdates;

		GetChatInfoResponce GetChatInfo;
		GetChatMembersResponce GetChatMembers;
		GetChatHistoryResponce GetChatHistory;
		GetChatListResponce GetChatList;
		GetAddonCommandsResponce GetAddonCommands;

		CreateChatResponce CreateChat;
		JoinChatResponce JoinChat;
		CommitMessageResponce CommitMessage;
	};

	/// <summary>
	/// Создает error-ответ с текстом и кодом ошибки.
	/// </summary>
	static Responce CreateError(const char* message, uint32_t errorCode = 0);
	
	/// <summary>
	/// Создает error-ответ из string_view.
	/// </summary>
	static Responce CreateError(const std::string_view& message, uint32_t errorCode = 0);
	
	/// <summary>
	/// Создает handshake-ответ с echo-флагом и access token.
	/// </summary>
	static Responce CreateHandshake(const connflag_t& flag, const token_t token);
	static Responce CreateHandshakeSecure(const connflag_t& flag, const token_t token, std::uint64_t integrityTag, const std::uint8_t serverPublicKey[32]);
	
	/// <summary>
	/// Создает ответ успешной авторизации.
	/// </summary>
	static Responce CreateLogin(const User& user);
	
	/// <summary>
	/// Создает ответ с информацией о чате.
	/// </summary>
	static Responce CreateGetChatInfo(const Chat& chat);
	
	/// <summary>
	/// Создает элемент списка участников чата.
	/// </summary>
	static Responce CreateGetChatMembers(const User& user, int32_t remainingUsers);
	
	/// <summary>
	/// Создает элемент списка чатов пользователя.
	/// </summary>
	static Responce CreateGetChatList(const Chat& chat, int32_t remainingChats);

	/// <summary>
	/// Создает элемент списка серверных команд, зарегистрированных аддонами.
	/// </summary>
	static Responce CreateGetAddonCommand(const std::string_view& command, int32_t remainingCommands);
	
	/// <summary>
	/// Создает ответ после создания чата.
	/// </summary>
	static Responce CreateCreateChat(const Chat& chat);
	
	/// <summary>
	/// Создает ответ после присоединения к чату.
	/// </summary>
	static Responce CreateJoinChat(const Chat& chat);
	
	/// <summary>
	/// Создает ответ с подтверждением отправленного сообщения.
	/// </summary>
	static Responce CreateCommitMessage(const Message& message);
};

#pragma pack(pop)
