#include "Platform/Win32/Win32FontSource.h"

#include "ECDI/Core/String.h"   // UTF8ToWide（family 是 UTF-8）

#include <Windows.h>

#include <string>

namespace ECDI{

namespace{

/// @brief UTF-16 → **ANSI（当前代码页）**
/// @details ★★ 为什么是 ANSI 而不是 UTF-8：FreeType 的 Windows 系统接口
///          （`third_party/freetype/builds/windows/ftsystem.c:231`）对 `FT_New_Face`
///          收到的 `char*` 路径执行 **`MultiByteToWideChar(CP_ACP, ...)`**
///          ⇒ 传 UTF-8 会让非 ASCII 路径**解析错误**。
/// @return 窄串；**转换失败（该代码页表达不了）⇒ 空串**
std::string WideToAnsi(const std::wstring& wide)
{
	if (wide.empty())
	{
		return {};
	}

	const int needed = WideCharToMultiByte(CP_ACP, 0, wide.c_str(),
	                                       static_cast<int>(wide.size()),
	                                       nullptr, 0, nullptr, nullptr);
	if (needed <= 0)
	{
		return {};
	}

	std::string out(static_cast<std::size_t>(needed), '\0');
	const int written = WideCharToMultiByte(CP_ACP, 0, wide.c_str(),
	                                        static_cast<int>(wide.size()),
	                                        out.data(), needed, nullptr, nullptr);
	if (written <= 0)
	{
		return {};
	}
	out.resize(static_cast<std::size_t>(written));
	return out;
}

/// @brief 目标是否为**存在的文件**（宽字符路径——探测不受代码页限制）
bool FileExistsWide(const std::wstring& path)
{
	const DWORD attr = GetFileAttributesW(path.c_str());
	return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

/// @brief 系统字体目录（宽字符）——★ 由 `GetWindowsDirectoryW` 推导，**不硬编码盘符**
std::wstring FontsDirectoryWide()
{
	wchar_t buffer[MAX_PATH]{};
	const UINT length = GetWindowsDirectoryW(buffer, MAX_PATH);
	if (length == 0 || length >= MAX_PATH)
	{
		return {};
	}

	std::wstring dir(buffer, length);
	dir += L"\\Fonts";
	return dir;
}

}   // namespace

std::string Win32FontSource::DefaultFontFile()
{
	// ★ 优先级（v1.1 对齐 GDI 默认解析）：GDI 空 family + DEFAULT_CHARSET 在中文系统上
	//   实际落到 **SimSun**（探针实测 tmAscent/tmHeight ≈ 0.86em）——控件的框高/内缩都是
	//   按 GDI 观感调的，FontEngine 默认若解析到行盒大 30% 的 MSYH，单行输入的文字会
	//   下偏并被裁剪（GL 实测）。SimSun 同样含 CJK（详设 L2 的意图保持）。
	static const wchar_t* const kCandidates[] = {
		L"simsun.ttc",    // 宋体（= GDI 空 family 的实际解析结果；拉丁 + CJK）
		L"msyh.ttc",      // 微软雅黑（行盒更大——观感差异见上）
		L"segoeui.ttf",   // Segoe UI（纯拉丁兜底）
		L"arial.ttf",     // Arial
	};

	const std::wstring dir = FontsDirectoryWide();
	if (dir.empty())
	{
		return {};
	}

	for (const wchar_t* name : kCandidates)
	{
		const std::wstring candidate = dir + L"\\" + name;
		if (FileExistsWide(candidate))
		{
			return WideToAnsi(candidate);
		}
	}
	return {};
}

std::string Win32FontSource::ResolveFile(const std::string& family) const
{
	if (family.empty())
	{
		return DefaultFontFile();   // ★ 空 = 系统默认
	}

	const std::wstring dir = FontsDirectoryWide();
	if (dir.empty())
	{
		return {};
	}

	// ★ 本 Phase：**family 视作文件名**直查（如 "consola.ttf"）——不做 family↔文件名解析
	//   （详设 §9 L1）。family 是 UTF-8 ⇒ 先转宽再拼路径（探测用宽 API，不受代码页限制）。
	const std::wstring wideFamily = UTF8ToWide(family);
	if (wideFamily.empty())
	{
		return {};
	}

	const std::wstring candidate = dir + L"\\" + wideFamily;
	if (FileExistsWide(candidate))
	{
		return WideToAnsi(candidate);
	}

	// ★ 找不到 ⇒ **空串**，由 FontEngine 回退默认 face + 告警（**不静默降级**——详设 △2）
	return {};
}

}
