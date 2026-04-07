#pragma once

#include <string>
#include <vector>

#include <telecrap/Transport.h>
#include <telecrap/Request.h>

#include "ChatHistory.h"

/// <summary>
/// Управляет загрузкой Lua-аддонов и диспетчеризацией пользовательских команд.
/// </summary>
class AddonManager
{
public:
	/// <summary>
	/// Инициализирует Lua runtime и загружает команды аддонов.
	/// </summary>
	static void Init();
	/// <summary>
	/// Пытается выполнить пользовательскую команду через аддоны.
	/// </summary>
	static bool ExecuteCommand(const std::string& cmd, const std::string& args, Transport* transport, CommitMessageRequest& Request, UserInfo& requestor, ChatInfo& chat);

	/// <summary>
	/// Возвращает список команд, зарегистрированных аддонами (без ведущего '/').
	/// </summary>
	static std::vector<std::string> ListRegisteredCommands();
};
