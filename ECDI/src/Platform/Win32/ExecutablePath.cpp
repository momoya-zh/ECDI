#include "ECDI/Platform/ExecutablePath.h"

#include "ECDI/Core/String.h"

#include <Windows.h>

#ifdef DrawText
#undef DrawText
#endif

#include <string>

namespace ECDI{
namespace Platform{

std::string GetExecutableDirectory(){
	wchar_t buffer[MAX_PATH]{};
	const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
	if (length == 0 || length >= MAX_PATH){
		return {};
	}
	std::wstring path(buffer, length);
	// 去掉文件名段：截到最后一个 \ 之前（不含分隔符本身——调用方自行拼 `\`）
	const size_t separator = path.find_last_of(L'\\');
	if (separator == std::wstring::npos){
		return {};
	}
	path.resize(separator);
	return WideToUTF8(path);
}

}
}
