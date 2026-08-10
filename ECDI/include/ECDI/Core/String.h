#pragma once

#include <string>

namespace ECDI
{

/// @brief UTF-8（std::string）→ UTF-16（std::wstring）
/// @details 字符串边界转换工具（Win32 平台层使用）。
/// 公共 API 统一 UTF-8（std::string），仅在 Win32 边界转换到 UTF-16
/// （Windows 上 wchar_t 为 16 位 UTF-16）。转库/跨平台时此函数留在平台层。
std::wstring UTF8ToWide(const std::string& utf8);

/// @brief UTF-16（std::wstring）→ UTF-8（std::string）
std::string WideToUTF8(const std::wstring& wide);

}
