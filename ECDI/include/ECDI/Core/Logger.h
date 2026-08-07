#pragma once

#include<string_view>

/// @brief 日志级别，从低到高排列
enum class LogLevel
{
	Trace,		///< 最细粒度的跟踪信息
	Debug,		///< 调试信息
	Info,		///< 一般信息
	Warning,	///< 警告
	Error,		///< 错误
	Fatal		///< 致命错误（通常触发断言后程序终止）
};

/// @brief Framework 统一日志工具
/// @details
/// 所有日志输出通过 OutputDebugStringW 发送到调试器输出窗口。
/// Logger 是纯静态工具类，禁止实例化。
class Logger
{
public:
	/// @brief 输出一条日志
	/// @param level  日志级别
	/// @param message 日志内容（宽字符串视图）
	static void Log(
		LogLevel level,
		std::wstring_view message
	);

	Logger() = delete;

	Logger(const Logger&) = delete;

	Logger& operator=(const Logger&) = delete;

};
