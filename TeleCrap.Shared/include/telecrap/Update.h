#pragma once

#include <cstdint>

#include "Models.h"

#pragma pack(push, 1)

/// <summary>
/// Событие: пользователь присоединился к чату.
/// </summary>
struct UserJoinedUpdate
{
	Chat ChatModel;
	User UserModel;
};

/// <summary>
/// Событие модерации: целевой пользователь (kick/ban/unban) в контексте чата.
/// </summary>
struct ChatMemberEvent
{
	Chat ChatModel;
	User TargetUser;
};

/// <summary>
/// Событие: чат был переименован.
/// </summary>
struct ChatRenamedUpdate
{
	Chat ChatModel;
};

/// <summary>
/// Тип push-обновления, доставляемого клиенту через GetUpdates.
/// </summary>
enum class UpdateType : std::uint32_t
{
	Message,
	UserJoined,
	UserKicked,
	UserBanned,
	UserUnbanned,
	ChatRenamed,
};

/// <summary>
/// Push-обновление от сервера клиенту (tagged union по UpdateType).
/// </summary>
struct Update
{
	std::int32_t Id;
	UpdateType Type;

	union
	{
		Message MessageSent;
		UserJoinedUpdate UserJoined;
		ChatMemberEvent MemberEvent;
		ChatRenamedUpdate ChatRenamed;
	};
};

#pragma pack(pop)
