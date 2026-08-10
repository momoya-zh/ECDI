#include"ECDI/Core/Logger.h"

#include<string>
#include<stdexcept>
#include<cassert>
#include<Windows.h>
namespace ECDI{

/// @brief 将 LogLevel 转为可读的宽字符串标签
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
	// 拼接格式："[Level]\nmessage\n"
	const std::wstring_view prefix = ToString(level);

	std::wstring log;
	log.reserve(prefix.size() + message.size() + 2);

	log += prefix;
	log += L' ';
	log += L'\n';
	log += message;
	log += L'\n';

	// 输出到调试器（VS Output 窗口 / DebugView 等）
	OutputDebugStringW(log.c_str());
}

}
