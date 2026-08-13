#include "ECDI/Core/ECDIAssert.h"

#include "ECDI/Core/Logger.h"

#include <Windows.h>

#include <string>
#include <cassert>
#include <sstream>

namespace ECDI::Detail{

	void HandleAssertFailure(
		std::string_view expression,
		std::string_view file,
		int line,
		std::string_view function){

		// 将窄字符串信息转为宽字符串（当前项目源文件均为 ASCII 兼容编码）
		std::wstring wExpression(expression.begin(), expression.end());
		std::wstring wFile(file.begin(), file.end());
		std::wstring wFunction(function.begin(), function.end());

		// 拼接格式化的断言失败报告
		std::wostringstream stream;
		constexpr wchar_t kSeparator[] =
			L"========================================";
		stream << kSeparator << L'\n';
		stream << L"ECDI Assertion Failed\n\n";
		stream << L"Expression : " << wExpression << L'\n';
		stream << L"File       : " << wFile << L'\n';
		stream << L"Function   : " << wFunction << L'\n';
		stream << L"Line       : " << line << L'\n';
		stream << kSeparator;

		std::wstring message = stream.str();

		// 先写日志（保证调试器能看到），再弹 MessageBox
		Logger::Log(LogLevel::Fatal, message);
		MessageBoxW(
			nullptr,
			message.c_str(),
			L"ECDI Framework",
			MB_OK | MB_ICONERROR);

		// 最终触发标准 assert 终止程序
		assert(false);
	}
}
