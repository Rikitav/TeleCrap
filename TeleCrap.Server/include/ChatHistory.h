#pragma once

#include <telecrap/Models.h>

/// <summary>
/// Серверная модель пользователя с полем хеша пароля.
/// </summary>
struct UserInfo
{
	userid_t Id = 0;
	fixed_string<CHATNAME_MAXLENGTH> Name;
	size_t PasswordHash = 0;
};

/// <summary>
/// Серверная модель группового/системного чата с владельцем.
/// </summary>
struct ChatInfo
{
	chatid_t Id = 0;
	fixed_string<CHATNAME_MAXLENGTH> Name;
	userid_t OwnerId;
};

/// <summary>
/// Расширение ChatInfo для direct-чата с двумя участниками.
/// </summary>
struct DirectChatInfo : public ChatInfo
{
	userid_t User1Id;
	userid_t User2Id;
};

/// <summary>
/// Внутренняя серверная модель сообщения в БД.
/// </summary>
struct MessageInfo
{
	msgid_t Id = 0;
	chatid_t ChatId;
	userid_t SenderId;
	timestamp_t Timestamp = 0;
	fixed_string<MESSAGETEXT_MAXLENGTH> Text;
};
