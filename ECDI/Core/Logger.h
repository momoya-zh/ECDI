#pragma once

#include<string_view>

enum class LogLevel
{
	Trace,
	Debug,
	Info,
	Warning,
	Error,
	Fatal
};

class Logger
{
public:
	static void Log(
		LogLevel level,
		std::wstring_view message
	);

	Logger() = delete;

	Logger(const Logger&) = delete;

	Logger& operator=(const Logger&) = delete;

};

