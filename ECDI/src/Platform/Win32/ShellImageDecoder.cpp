#include "Platform/Win32/ShellImageDecoder.h"

#include "ECDI/Core/Logger.h"
#include "ECDI/Core/String.h"
#include "ECDI/Decode/ImageDecoder.h"

#include <windows.h>
#include <shellapi.h>          // SHGetFileInfoW / SHFILEINFOW（★ shell32 已链——CMakeLists.txt:75）

#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <string>
#include <vector>

// Phase 21：系统图标 → Image（内核 = HICON → Image；外壳 = 路径 → HICON）
// ★ 内核与外壳分离：内核不触 shell ⇒ 可用 CreateIconIndirect 自造图标在无头环境下逐字节断言。
// ★ 三条 ownership 事实（详设 §2.3）：GetIconInfo 的两个 bitmap 与 HICON ownership 无关（各自
//   DeleteObject）；屏幕 DC 的 ReleaseDC 首参是 nullptr；每个资源「取得即进入作用域 owner」。

namespace ECDI::Decode
{

namespace {

constexpr int kMaxIconDim = 4096;
constexpr std::uint64_t kMaxBytes = 256ull * 1024ull * 1024ull;   // 与 WIC 同源上限（详设 §2.4）

/// @brief 记一条图标解码错误（详设 §3.3 日志口径）
/// @details code == 0 ⇒ "ImageDecoder: <step> failed"；否则带 (code=%lu)。
///          ★ 只在 API 明确提供错误码处传参——GDI 的 GetIconInfo / GetDIBits / GetObject
///          不保证设置 last error（其返回值本身即诊断量）⇒ 一律用默认 0，不伪造 0x00000000。
void LogIconError(const wchar_t* step, unsigned long code = 0)
{
	wchar_t buf[160];
	if (code == 0) {
		std::swprintf(buf, 160, L"ImageDecoder: %s failed", step);
	} else {
		std::swprintf(buf, 160, L"ImageDecoder: %s failed (code=%lu)", step, code);
	}
	Logger::Log(LogLevel::Error, std::wstring_view{buf});
}

/// @brief 尺寸校验（详设 §2.4——刻意不照抄 WIC 的四域检查）
/// @details 尺寸来自 GetObject 的 BITMAP（LONG 域，现实上界极小）⇒ 一个维度上限加一个
///          64 位中间量即可覆盖溢出与资源滥用；256MB 与 WIC 同源（不新造第三种上限口径）。
bool ValidIconSize(int w, int h)
{
	return w >= 1 && h >= 1
	    && w <= kMaxIconDim && h <= kMaxIconDim
	    && static_cast<std::uint64_t>(w) * 4ull * static_cast<std::uint64_t>(h) <= kMaxBytes;
}

/// @brief ★★ 不透明度来源判据：该位图的 alpha 是否携带信息
/// @details ★ 不得按位深判——实测 GetIconInfo 对「无 alpha 的源」一律把 color 归一化为
///          32bpp（即便传入 24bpp / 1bpp），而这类位图的 alpha 恒为 0 ⇒ 按位深判会得到
///          「全透明、不可见」的老式图标。
bool AnyAlphaNonZero(const std::uint8_t* bgra, std::size_t pixelCount)
{
	for (std::size_t i = 0; i < pixelCount; ++i) {
		if (bgra[i * 4 + 3] != 0) return true;
	}
	return false;
}

/// @brief 直通 BGRA → 预乘 BGRA（Image 契约要求）
/// @details 舍入口径与既有像素合成同族（Render/CoverageRaster.cpp:41-43）。
///          ★ mask 路径下 a ∈ {0, 255} ⇒ 该式恒等、无误差。
void PremultiplyInPlace(std::uint8_t* bgra, std::size_t pixelCount)
{
	for (std::size_t i = 0; i < pixelCount; ++i) {
		std::uint8_t* px = bgra + i * 4;
		const unsigned a = px[3];
		for (int ch = 0; ch < 3; ++ch) {
			const unsigned c = px[ch];
			px[ch] = static_cast<std::uint8_t>((c * a + 127) / 255);
		}
	}
}

/// @brief `GetDIBits` 的 BITMAPINFO 承载（★ **必须留出调色板空间**）
/// @details ⚠️ 经典坑（Phase 21 实测定案）：`BITMAPINFO` 自带 `bmiColors[1]`（4B），
///          而 **`GetDIBits` 在目标为调色板格式时会写入完整调色板**（1bpp = 2 项 = 8B）
///          ⇒ 只给 1 项就是**越界写 4 字节**。★ 实测（纯 GDI 探针）：32bpp 请求
///          越界 **0** 字节，**1bpp 请求越界 4 字节**。
///          ★ 后果按工具链分裂：gcc / clang / clang-cl **无 `/RTC`** ⇒ 静默（落在栈
///          填充或相邻局部上，可能扰动别的量，极难定位）；**MSVC Debug 默认 `/RTC1`**
///          （= `/RTCs` + `/RTCu`）⇒ 立刻 `Run-Time Check Failure #2 - Stack around the
///          variable ... was corrupted` 并中止（无调试器时对话框 + `abort`：退出码 3
///          且 stdout 不 flush）。⇒ 本结构一次备足 256 项（8bpp 上限也够）。
/// @note ★ `CreateDIBSection` **只读**该结构 ⇒ 创建 DIB 不必补空间；**只有 `GetDIBits` 需要**。
struct BitmapInfo256
{
	BITMAPINFO info{};          // bmiHeader + bmiColors[1]
	RGBQUAD    palette[255]{};  // 补齐到 256 项（8bpp 上限也够）
};

/// @brief 以 top-down（biHeight = -h）填充 BITMAPINFO 头部
void FillBiTopDown(BITMAPINFO& bi, int w, int h, unsigned short bpp)
{
	bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.bmiHeader.biWidth = w;
	bi.bmiHeader.biHeight = -h;              // ★ 负值 = top-down（row 0 = 视觉顶行）
	bi.bmiHeader.biPlanes = 1;
	bi.bmiHeader.biBitCount = bpp;
	bi.bmiHeader.biCompression = BI_RGB;
	bi.bmiHeader.biSizeImage = 0;
	bi.bmiHeader.biXPelsPerMeter = 0;
	bi.bmiHeader.biYPelsPerMeter = 0;
	bi.bmiHeader.biClrUsed = 0;
	bi.bmiHeader.biClrImportant = 0;
}

// ── 三个 RAII 守卫（详设 §2.3）——不泛化成通用 Win32 句柄包装（评审赞成）──

struct IconBitmaps
{
	HBITMAP color = nullptr;
	HBITMAP mask = nullptr;
	~IconBitmaps();
};

struct ScreenDc
{
	HDC dc = nullptr;
	~ScreenDc();
};

struct UniqueIcon
{
	HICON icon = nullptr;
	~UniqueIcon();
};

IconBitmaps::~IconBitmaps()
{
	if (color != nullptr) DeleteObject(color);
	if (mask != nullptr) DeleteObject(mask);
}

ScreenDc::~ScreenDc()
{
	if (dc != nullptr) ReleaseDC(nullptr, dc);
}

UniqueIcon::~UniqueIcon()
{
	if (icon != nullptr) DestroyIcon(icon);
}

} // anonymous namespace

Image ImageFromHIcon(HICON icon)
{
	// 1) 取 ICONINFO
	ICONINFO ii{};
	if (!GetIconInfo(icon, &ii)) {
		LogIconError(L"GetIconInfo");
		return {};
	}

	// 2) ★ RAII ①：取得即接管（两个 bitmap 的 ownership 与 HICON 无关 ⇒ 必须各自 DeleteObject）
	IconBitmaps bitmaps{ii.hbmColor, ii.hbmMask};

	// 3) ★ C13 支持边界：真 monochrome HICON（hbmColor == nullptr，hbmMask 自带 AND/XOR 双段）
	//    不在本阶段范围 ⇒ 显式、可观测地拒绝（不复用下一支 GetObject 失败的日志）
	if (ii.hbmColor == nullptr) {
		LogIconError(L"monochrome HICON unsupported");
		return {};
	}

	BITMAP bc{};
	if (GetObject(ii.hbmColor, sizeof(bc), &bc) != static_cast<int>(sizeof(bc))) {
		LogIconError(L"GetObject");
		return {};
	}

	// 4) 尺寸校验
	const int w = bc.bmWidth;
	const int h = bc.bmHeight;
	if (!ValidIconSize(w, h)) {
		LogIconError(L"invalid icon size");
		return {};
	}

	// 5) 输出几何（恒 32bpp ⇒ stride == width*4）
	const int stride = w * 4;
	const std::size_t pixelCount = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);

	// 6) 目标 Image
	Image img;
	img.width = w;
	img.height = h;
	img.stride = stride;
	img.pixels.resize(static_cast<std::size_t>(stride) * static_cast<std::size_t>(h));

	// 7) ★ RAII ②：屏幕 DC（与 GetDC(nullptr) 配对 ⇒ ReleaseDC 首参是 nullptr）
	ScreenDc screen{GetDC(nullptr)};
	if (screen.dc == nullptr) {
		LogIconError(L"GetDC");
		return {};
	}

	// 8) 读 color——恒请求 32bpp / top-down（GetIconInfo 已把 color 归一化为 32bpp）
	BitmapInfo256 bi32{};   // ★ 256 项空间——32bpp 用不到调色板，但统一用同一承载
	FillBiTopDown(bi32.info, w, h, 32);
	if (GetDIBits(screen.dc, ii.hbmColor, 0, static_cast<UINT>(h),
	              img.pixels.data(), &bi32.info, DIB_RGB_COLORS) != h) {
		LogIconError(L"GetDIBits(color)");
		return {};
	}

	// 9) ★★ 不透明度：alpha 携带信息 ⇒ 用 alpha；alpha 全为 0 ⇒ 改用 mask
	if (!AnyAlphaNonZero(img.pixels.data(), pixelCount)) {
		const int maskStride = ((w + 31) / 32) * 4;
		std::vector<std::uint8_t> mask(static_cast<std::size_t>(maskStride) * static_cast<std::size_t>(h));

		// ★★ 越界现场：1bpp 目标格式 ⇒ GDI 写 2 个调色板项，单槽 BITMAPINFO 会越界 4 字节
		BitmapInfo256 bi1{};
		FillBiTopDown(bi1.info, w, h, 1);
		if (ii.hbmMask == nullptr
		    || GetDIBits(screen.dc, ii.hbmMask, 0, static_cast<UINT>(h),
		                 mask.data(), &bi1.info, DIB_RGB_COLORS) != h) {
			LogIconError(L"GetDIBits(mask)");
			return {};
		}

		for (int y = 0; y < h; ++y) {
			for (int x = 0; x < w; ++x) {
				const std::uint8_t byte = mask[static_cast<std::size_t>(y) * maskStride + (x >> 3)];
				const unsigned bit = (byte >> (7 - (x & 7))) & 1u;
				img.pixels[(static_cast<std::size_t>(y) * w + x) * 4 + 3] = bit ? 0 : 255;   // 置位 = 全透明
			}
		}
	}

	// 10) 预乘（Image 契约：premultiplied BGRA）
	PremultiplyInPlace(img.pixels.data(), pixelCount);

	// 11) 成功
	return img;
}

Image DecodeSystemIcon(const std::string& utf8Path)
{
	// 1) ★ C6 入口自校验——实测空路径 API 不报错（SHGetFileInfoW 照样返回图标）
	//    ⇒ 不能依赖它，必须自己拦
	if (utf8Path.empty() || utf8Path.find_first_not_of(" \t\r\n") == std::string::npos) {
		LogIconError(L"empty path");
		return {};
	}

	// 2) UTF-8 → UTF-16（框架公共 API 统一 UTF-8，只在 Win32 边界转换）
	const std::wstring wide = UTF8ToWide(utf8Path);
	if (wide.empty()) {
		LogIconError(L"UTF8ToWide");
		return {};
	}

	// 3) 属性：拿不到 ⇒ fallback 到 FILE_ATTRIBUTE_NORMAL
	//    ★ 语义是「无法取得该路径的属性」，不等于「路径不存在」（成因含权限 / 不可访问等）
	DWORD attrs = GetFileAttributesW(wide.c_str());
	if (attrs == INVALID_FILE_ATTRIBUTES) attrs = FILE_ATTRIBUTE_NORMAL;

	// 4) shell 图标——一律带 SHGFI_USEFILEATTRIBUTES（不加它则不存在路径拿不到图标；
	//    而已存在路径上加与不加结果逐项相同 ⇒ 单一路径）
	SHFILEINFOW sfi{};
	const DWORD_PTR r = SHGetFileInfoW(wide.c_str(), attrs, &sfi, static_cast<UINT>(sizeof(sfi)),
	                                   SHGFI_ICON | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES);
	if (r == 0 || sfi.hIcon == nullptr) {
		LogIconError(L"SHGetFileInfoW", static_cast<unsigned long>(GetLastError()));
		return {};
	}

	// 5) ★ RAII ③：取得即接管
	UniqueIcon guard{sfi.hIcon};

	// 6) 交给内核
	return ImageFromHIcon(guard.icon);
}

} // namespace ECDI::Decode
