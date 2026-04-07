#include "pch.h"
#include "telecrap/Models.h"
#include "telecrap/Responce.h"

Responce Responce::CreateError(const char* message, uint32_t errorCode)
{
	Responce responce{};
	responce.Type = ResponceType::Error;
	responce.Error.Message = message;
	responce.Error.Code = errorCode;

	return responce;
}

Responce Responce::CreateError(const std::string_view& message, uint32_t errorCode)
{
	Responce responce{};
	responce.Type = ResponceType::Error;
	responce.Error.Message = message;
	responce.Error.Code = errorCode;

	return responce;
}

Responce Responce::CreateHandshake(const connflag_t& flag, const token_t token)
{
	// Сервер echo'ит флаг клиента, чтобы клиент убедился, что ответ относится к его запросу.
	Responce responce{};
	responce.Type = ResponceType::HandshakeSuccessfull;
	responce.Handshake.Flag = flag;
	responce.Handshake.Token = token;

	return responce;
}

Responce Responce::CreateLogin(const User& user)
{
	Responce responce{};
	responce.Type = ResponceType::LoginSuccessfull;
	responce.Auth.UserModel = user;

	return responce;
}

Responce Responce::CreateGetChatInfo(const Chat& chat)
{
	Responce responce{};
	responce.Type = ResponceType::GetChatInfo;
	responce.GetChatInfo.ChatModel = chat;

	return responce;
}

Responce Responce::CreateGetChatMembers(const User& user, int32_t remainingUsers)
{
	Responce responce{};
	responce.Type = ResponceType::GetChatMembers;
	responce.GetChatMembers.CurrentUser = user;
	responce.GetChatMembers.RemainingUsers = remainingUsers;
	return responce;
}

Responce Responce::CreateGetChatList(const Chat& chat, int32_t remainingChats)
{
	Responce responce{};
	responce.Type = ResponceType::GetChatList;
	responce.GetChatList.CurrentChat = chat;
	responce.GetChatList.RemainingChats = remainingChats;
	return responce;
}

Responce Responce::CreateGetAddonCommand(const std::string_view& command, int32_t remainingCommands)
{
	Responce responce{};
	responce.Type = ResponceType::GetAddonCommands;
	responce.GetAddonCommands.CurrentCommand = command;
	responce.GetAddonCommands.RemainingCommands = remainingCommands;
	return responce;
}

Responce Responce::CreateCreateChat(const Chat& chat)
{
	Responce responce{};
	responce.Type = ResponceType::CreateChat;
	responce.CreateChat.ChatModel = chat;
	return responce;
}

Responce Responce::CreateJoinChat(const Chat& chat)
{
	Responce responce{};
	responce.Type = ResponceType::JoinChat;
	responce.JoinChat.ChatModel = chat;
	return responce;
}

Responce Responce::CreateCommitMessage(const Message& message)
{
	Responce responce{};
	responce.Type = ResponceType::CommitMessage;
	responce.CommitMessage.MessageModel = message;

	return responce;
}
