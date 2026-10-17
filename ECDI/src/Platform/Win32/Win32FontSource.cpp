#include "Platform/Win32/Win32FontSource.h"

#include "ECDI/Core/String.h"   // UTF8ToWide（family 是 UTF-8）

#include <Windows.h>

#include <string>

namespace ECDI{

namespace{

/// @brief 宽串 → UTF-8（★ 注册表键名/数据是 UTF-16，族名比较需统一到 UTF-8 再比）
std::string WideToUtf8(const wchar_t* wide)
{
	if (!wide || !*wide)
	{
		return {};
	}
	const int needed = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
	if (needed <= 1)
	{
		return {};
	}
	std::string out(static_cast<std::size_t>(needed - 1), '\0');
	const int written = WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), needed, nullptr, nullptr);
	if (written <= 1)
	{
		return {};
	}
	out.resize(static_cast<std::size_t>(written - 1));
	return out;
}

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

	// ★ 路径 ①：**family 视作文件名**直查（如 "consola.ttf"）——Phase 26 起的既有行为，
	//   **保留且优先**：既有调用点与 T26-6 ③ 的期望因此逐位不变（条 95⑤）。
	//   family 是 UTF-8 ⇒ 先转宽再拼路径（探测用宽 API，不受代码页限制）。
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

	// ★★ 路径 ②：**族名反查**（REQ-09）——GDI 链把同一个串当**族名**用
	//   （`LOGFONTW.lfFaceName`），此前本链只认文件名 ⇒ 写族名时 GDI 命中、GL **静默回退默认 face**。
	//   ⇒ 补上族名维度后，同一个 `Font::family` 才能让两条链落到同一张 face。
	const std::string byFamily = FamilyToFileName(family);
	if (!byFamily.empty())
	{
		const std::wstring resolved = dir + L"\\" + UTF8ToWide(byFamily);
		if (FileExistsWide(resolved))
		{
			return WideToAnsi(resolved);
		}
	}

	// ★ 两条路径都不中 ⇒ **空串**，由 FontEngine 回退默认 face + 告警（**不静默降级**——详设 △2）
	return {};
}

std::string Win32FontSource::FamilyToFileName(const std::string& familyU8)
{
	// ★★ REQ-09 的落点：注册表 `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts`
	//   是「(复合)族名 + (TrueType)" → 文件名」的表。本机实测（探针 `t69_family_map.cpp`）：
	//     `Consolas (TrueType)` → `consola.ttf` · `SimSun & NSimSun (TrueType)` → `simsun.ttc`
	//     `Microsoft YaHei & Microsoft YaHei UI (TrueType)` → `msyh.ttc`
	//   ★ **只用注册表、不用 `EnumFontFamiliesExW`**：后者能枚举族名但**拿不到文件名**，
	//     而本类要交回的是**可 `FT_New_Face` 的路径**。
	const wchar_t* const kKey =
		L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts";

	HKEY key = nullptr;
	if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
	{
		return {};   // 读不到注册表 ⇒ 空串（由调用方回退 + 告警，不静默换字体）
	}

	// ★ 匹配强度：3 = 完全相等 · 2 = `<family> ` 前缀（样式变体）· 1 = `<family> & `（复合族名首项）
	//   ★ **禁止** `find()` 式子串匹配——实测反例：`宋体` 会命中 **`华文宋体`**（另一个字体）。
	int bestStrength = 0;
	std::string bestFile;
	bool bestIsRegular = false;

	DWORD index = 0;
	wchar_t nameBuf[512]{};
	BYTE valueBuf[1024]{};
	for (;;)
	{
		DWORD nameLen = 512;
		DWORD valueLen = 1024;
		DWORD type = 0;
		const LONG rc = RegEnumValueW(key, index++, nameBuf, &nameLen, nullptr, &type,
		                              valueBuf, &valueLen);
		if (rc != ERROR_SUCCESS)
		{
			break;
		}
		if (type != REG_SZ)
		{
			continue;
		}

		// ★★ `RegEnumValueW` 的 `REG_SZ` 数据是 **UTF-16**（不是窄串）——首版探针按 `char*`
		//    直读只得到首字符（"consola.ttf" → "c"）。此处按 `wchar_t*` 读再转 UTF-8。
		const std::string keyU8 = WideToUtf8(nameBuf);
		const std::string fileU8 = WideToUtf8(reinterpret_cast<const wchar_t*>(valueBuf));
		if (fileU8.empty())
		{
			continue;
		}

		// 去掉键名的 ` (TrueType)` / ` (OpenType)` 等后缀 ⇒ 得到「(复合)族名」
		std::string base = keyU8;
		const std::size_t suffix = base.find(" (");
		if (suffix != std::string::npos)
		{
			base.erase(suffix);
		}

		int strength = 0;
		if (base == familyU8)
		{
			strength = 3;
		}
		else if (base.compare(0, familyU8.size() + 1, familyU8 + " ") == 0)
		{
			strength = 2;   // `Consolas Bold` / `Segoe UI Light` …（样式变体）
		}
		else if (base.compare(0, familyU8.size() + 3, familyU8 + " & ") == 0)
		{
			strength = 1;   // `SimSun & NSimSun` / `Microsoft YaHei & Microsoft YaHei UI`（复合首项）
		}
		if (strength == 0)
		{
			continue;
		}

		// ★ 常规体优先：请求**纯族名**（`Consolas`）时不得拿到样式变体（`consolab.ttf`）。
		//   ★ 判据 = 对**去掉族名前缀后的余部**判样式词（而不是对整串 `base` 判）——
		//     因为强度 3（`base == family`）的余部为空 ⇒ 天然算常规体；强度 2 的余部才是
		//     ` Bold` / ` Light` 这类样式后缀。⇒ 这同时保证了：
		//       · 请求 `Consolas`      ⇒ 只有 `Consolas`(强度3/常规) 胜出 ⇒ `consola.ttf` ✅
		//       · 请求 `Consolas Bold` ⇒ `Consolas Bold`(强度3/常规) 胜出 ⇒ `consolab.ttf` ✅（**期望行为**）
		std::string stylePart;
		if (base.size() > familyU8.size())
		{
			stylePart = base.substr(familyU8.size());
		}
		const bool isRegular =
			(stylePart.find("Bold") == std::string::npos) &&
			(stylePart.find("Italic") == std::string::npos) &&
			(stylePart.find("Light") == std::string::npos) &&
			(stylePart.find("Semibold") == std::string::npos) &&
			(stylePart.find("Semilight") == std::string::npos) &&
			(stylePart.find("Black") == std::string::npos) &&
			(stylePart.find("Regular") == std::string::npos) &&
			(stylePart.find("Thin") == std::string::npos) &&
			(stylePart.find("Medium") == std::string::npos);

		// 择优：强度高者优先；同强度下常规体优先；再同则**保留先出现者**（注册表顺序稳定）
		const bool better =
			(strength > bestStrength) ||
			(strength == bestStrength && isRegular && !bestIsRegular);
		if (better)
		{
			bestStrength = strength;
			bestFile = fileU8;
			bestIsRegular = isRegular;
		}
	}

	RegCloseKey(key);
	return bestFile;
}

}
