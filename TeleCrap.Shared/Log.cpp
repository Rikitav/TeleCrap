#include "pch.h"
#include "telecrap/Log.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>

std::mutex Log::logMutex;
void(*Log::postWriteHook)() = nullptr;

static const char* levelName(Log::Level lvl)
{
	switch (lvl)
	{
		case Log::Level::Error: return "ERROR";
		case Log::Level::Info: return "INFO ";
		case Log::Level::Trace: return "TRACE";
		default: return "?????";
	}
}

static const char* levelColor(Log::Level lvl)
{
	// ANSI colors (work fine on most Linux terminals; harmless elsewhere)
	switch (lvl)
	{
		case Log::Level::Error: return "\x1b[31m";  // red
		case Log::Level::Info:  return "\x1b[32m";  // green
		case Log::Level::Trace: return "\x1b[90m";  // gray
		default: return "\x1b[0m";
	}
}

static std::string nowHHMMSS()
{
	using namespace std::chrono;
	const auto now = system_clock::now();
	const std::time_t t = system_clock::to_time_t(now);

	std::tm tm{};
#ifdef _WIN32
	localtime_s(&tm, &t);
#else
	localtime_r(&t, &tm);
#endif

	std::ostringstream oss;
	oss << std::setfill('0')
		<< std::setw(2) << tm.tm_hour << ':'
		<< std::setw(2) << tm.tm_min << ':'
		<< std::setw(2) << tm.tm_sec;
	return oss.str();
}

void Log::Error(std::string_view facility, std::string_view message)
{
	write(Level::Error, facility, message);
}

void Log::Info(std::string_view facility, std::string_view message)
{
	write(Level::Info, facility, message);
}

void Log::Trace(std::string_view facility, std::string_view message)
{
	write(Level::Trace, facility, message);
}

void Log::SetPostWriteHook(void(*hook)())
{
	std::lock_guard<std::mutex> lk(logMutex);
	postWriteHook = hook;
}

void Log::write(Level level, std::string_view facility, std::string_view message)
{
	void(*hook)() = nullptr;
	{
		std::lock_guard<std::mutex> lk(logMutex);
	std::cout
		<< "\x1b[0m"
		<< "\r\x1b[2K"
		<< "[" << nowHHMMSS() << "] "
		<< levelColor(level) << levelName(level) << "\x1b[0m"
		<< " [" << facility << "] "
		<< message
		<< std::endl;
		hook = postWriteHook;
	}

	if (hook != nullptr)
		hook();
}

