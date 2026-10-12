#include "ECDI/Core/Logger.h"
#include "ECDI/Core/ECDIAssert.h"

#include <Windows.h>

#include <string>
#include <stdexcept>

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
	// ★ 审计 M23：`NDEBUG` 下裸 `assert(false)` 展开为空操作 ⇒ 函数继续走到
	//   `return L""` ⇒ 空标签静默进日志（无诊断价值）。改用 FRAMEWORK_ASSERT 与全库统一：
	//   Debug 走 HandleAssertFailure（日志 + MessageBox + 终止），Release 仍静默——
	//   **但那是全库一致的既有口径**（本项目的「契约靠断言维护」纪律见 ECDIAssert.h）。
	// ⚠️ 此处**不是**为 Release 补运行时分支：枚举 switch 已穷尽，到达此处意味着
	//   内存被写坏（强类型 enum 不可能有其它值）——那是不可恢复的框架级错误。
	FRAMEWORK_ASSERT(false && "Unknown LogLevel");
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
