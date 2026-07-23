#include"Logger.h"

#include<string>
#include<stdexcept>
#include<cassert>
#include<Windows.h>

static std::wstring_view ToString(LogLevel level)noexcept {
	switch (level) {
	case LogLevel::Trace:
		return L"Trace";

	case LogLevel::Debug:
		return L"Debug";

	case LogLevel::Info:
		return L"Info";

	case LogLevel::Error:
		return L"Error";

	case LogLevel::Warning:
		return L"Warning";
	case LogLevel::Fatal:
		return L"Fatal";
	}
	assert(false && "Unknown LogLevel");
	return L"";
}

void Logger::Log(LogLevel level, std::wstring_view message)
{
	const std::wstring_view prefix = ToString(level);

	std::wstring log;
	log.reserve(prefix.size() + message.size() + 2);

	log += prefix;
	log += L' ';
	log += L'\n';
	log += message;
	log += L'\n';

	OutputDebugStringW(log.c_str());
}