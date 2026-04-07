#pragma once
#include <string_view>
#include <mutex>

/// <summary>
/// Потокобезопасный логгер проекта с уровнями Error/Info/Trace.
/// </summary>
class Log
{
public:
	enum class Level
	{
		Error,
		Info,
		Trace,
	};

	static std::mutex logMutex;

	/// <summary>
	/// Пишет сообщение уровня ошибки.
	/// </summary>
	static void Error(std::string_view facility, std::string_view message);

	/// <summary>
	/// Пишет информационное сообщение.
	/// </summary>
	static void Info(std::string_view facility, std::string_view message);
	
	/// <summary>
	/// Пишет трассировочное сообщение.
	/// </summary>
	static void Trace(std::string_view facility, std::string_view message);

private:
	static void write(Level level, std::string_view facility, std::string_view message);
};

