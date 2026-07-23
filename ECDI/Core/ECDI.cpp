#include"ECDIAssert.h"
#include"Logger.h"

#include<string>
#include<cassert>
#include<sstream>
#include<Windows.h>

namespace ECDI::Detail
{
	void HandleAssertFailure(
		std::string_view expression,
		std::string_view file,
		int line,
		std::string_view function)
	{
		std::wstring wExpression(expression.begin(), expression.end());
		std::wstring wFile(file.begin(), file.end());
		std::wstring wFunction(function.begin(), function.end());
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
		Logger::Log(LogLevel::Fatal, message);
		MessageBoxW(
			nullptr,
			message.c_str(),
			L"ECDI Framework",
			MB_OK | MB_ICONERROR);
		assert(false);
	}
}