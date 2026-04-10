#include "pch.h"

#include <string>
#include <ctime>

#include "telecrap/Models.h"
#include "telecrap/Request.h"

Request Request::CreateHandshake(const connflag_t& flag)
{
	// Handshake открывает сессию и позволяет серверу выдать AccessToken.
	Request request{};
	request.Type = RequestType::Handshake;
	request.Handshake.Flag = flag;
	request.Handshake.Version = TELECRAP_VERSION;
	request.Handshake.SecureMode = 0;
	request.Handshake.Reserved = 0;
	request.Handshake.IntegrityTag = 0;
	std::memset(request.Handshake.ClientPublicKey, 0, sizeof(request.Handshake.ClientPublicKey));

	return request;
}

Request Request::CreateHandshakeSecure(const connflag_t& flag, std::uint64_t integrityTag, const std::uint8_t clientPublicKey[32])
{
	Request request{};
	request.Type = RequestType::Handshake;
	request.Handshake.Flag = flag;
	request.Handshake.Version = TELECRAP_VERSION;
	request.Handshake.SecureMode = 1;
	request.Handshake.Reserved = 0;
	request.Handshake.IntegrityTag = integrityTag;
	std::memcpy(request.Handshake.ClientPublicKey, clientPublicKey, sizeof(request.Handshake.ClientPublicKey));
	return request;
}

Request Request::CreateRegister(const token_t& token, const std::string& username, const std::string& password)
{
	Request request{};
	request.Type = RequestType::Register;
	request.Auth.UserToken = token;
	request.Auth.Username = username.c_str();
	request.Auth.Password = password.c_str();

	return request;
}

Request Request::CreateLogin(const token_t& token, const std::string& username, const std::string& password)
{
	Request request{};
	request.Type = RequestType::Login;
	request.Auth.UserToken = token;
	request.Auth.Username = username.c_str();
	request.Auth.Password = password.c_str();

	return request;
}

Request Request::CreateCommitMessage(const token_t& token, const chatid_t chatId, const std::string& content)
{
	// Timestamp проставляется на клиенте (time(0)); сервер сохраняет как есть.
	Request request{};
	request.Type = RequestType::CommitMessage;
	request.CommitMessage.UserToken = token;
	request.CommitMessage.Timestamp = time(0);
	request.CommitMessage.Content = content.c_str();
	request.CommitMessage.ToChat = chatId;

	return request;
}

Request Request::CreateGetChatInfo(const token_t& token, const std::string& chatQuery)
{
	Request request{};
	request.Type = RequestType::GetChatInfo;
	request.GetChatInfo.UserToken = token;
	request.GetChatInfo.ChatQuery = chatQuery.c_str();

	return request;
}

Request Request::CreateGetChatMembers(const token_t& token, const chatid_t chatId)
{
	Request request{};
	request.Type = RequestType::GetChatMembers;
	request.GetChatMembers.UserToken = token;
	request.GetChatMembers.ChatId = chatId;

	return request;
}

Request Request::CreateGetChatHistory(const token_t& token, const chatid_t chatId)
{
	Request request{};
	request.Type = RequestType::GetChatHistory;
	request.GetChatHistory.UserToken = token;
	request.GetChatHistory.ChatId = chatId;

	return request;
}

Request Request::CreateSpawnGroupChat(const token_t& token, const std::string& chatName)
{
	Request request{};
	request.Type = RequestType::SpawnGroupChat;
	request.SpawnGroupChat.UserToken = token;
	request.SpawnGroupChat.Chatname = chatName;

	return request;
}

Request Request::CreateJoinGroupChat(const token_t& token, const std::string& chatQuery)
{
	Request request{};
	request.Type = RequestType::JoinGroupChat;
	request.JoinGroupChat.UserToken = token;
	request.JoinGroupChat.ChatQuery = chatQuery.c_str();

	return request;
}

Request Request::CreateGetUpdates(const token_t& token, bool dropPending)
{
	Request request{};
	request.Type = RequestType::GetUpdates;
	request.GetUpdates.UserToken = token;
	request.GetUpdates.DropPending = static_cast<std::uint8_t>(dropPending ? 1 : 0);
	request.GetUpdates.Reserved[0] = 0;
	request.GetUpdates.Reserved[1] = 0;
	request.GetUpdates.Reserved[2] = 0;

	return request;
}

Request Request::CreateGetChatList(const token_t& token)
{
	Request request{};
	request.Type = RequestType::GetChatList;
	request.GetChatList.UserToken = token;
	return request;
}

Request Request::CreateGetAddonCommands(const token_t& token)
{
	Request request{};
	request.Type = RequestType::GetAddonCommands;
	request.GetAddonCommands.UserToken = token;
	return request;
}

