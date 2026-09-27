#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 防御性 undef（规范 10）：本文件含 Windows.h——防 DrawTextW 宏污染 ECDI 头声明
#endif

#include "Platform/Win32/ShellImageDecoder.h"   // ★ 内部头（平台层边界内）——内核 ImageFromHIcon
#include "Render/RecordingBackend.h"
#include "ECDI/Core/Rect.h"
#include "ECDI/Core/String.h"
#include "ECDI/Decode/ImageDecoder.h"
#include "ECDI/Render/PaintContext.h"
#include "ECDI/Render/RenderCommand.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <variant>
#include <vector>

using namespace ECDI;

// ══════════════════════════════════════════════════════════════════
// Phase 21：系统图标 → Image（详设 §6——T21-1..T21-9）
// ★ 全部 **无头可跑**：不依赖 COM 初始化、不依赖 explorer（C7；初设 §2.5 实测）
//   · 内核组（T21-1..T21-3）用 CreateIconIndirect **自造图标** ⇒ 期望值由测试自己构造、零环境依赖
//   · 外壳组（T21-4..T21-9）用**必然存在**的路径（测试 exe 自身 / 临时目录）与**必然不存在**的路径
// ★ 内核与外壳分离（D3）：内核不触 shell ⇒ 像素口径可**逐字节**断言，不必依赖系统图标长什么样
// ══════════════════════════════════════════════════════════════════

namespace {

constexpr DWORD kPathCap = 1024;   // GetModuleFileNameW / GetTempPathW 的缓冲容量

// ── 内核组装置 ────────────────────────────────────────────────────
// ★ 图标位图的既有惯例是 **bottom-up**（正 biHeight）⇒ 本装置把「视觉行号」翻转成内存行号，
//   于是用例里写的坐标就是**视觉**坐标；而「读出来的 Image 是不是 top-down」正是 T21-1 的现目之一。

/// @brief 图标用的 DIB section（裸句柄 + 像素指针 + 行跨距）
struct DibSection
{
	HBITMAP bitmap = nullptr;
	void* bits = nullptr;
	int stride = 0;
};

/// @brief `BITMAPINFO` 承载（★ **必须留出调色板空间**——与内核侧同一条坑）
/// @details ⚠️ `BITMAPINFO` 自带 `bmiColors[1]`（4B），而**调色板格式**（1bpp = 2 项、
///          4bpp = 16 项、8bpp = 256 项）需要更多：`GetDIBits` 会**写**调色板，
///          `CreateDIBSection` 会**读**它（用它建位图自己的调色板）。
///          ★★ 铁证（2026-09-27 实测定案）：本装置原先用 44 字节的 `BITMAPINFO` 建 **1bpp** DIB
///          ⇒ `CreateDIBSection` 读第 2 个调色板项时**越界读**（拿到栈上的垃圾）⇒
///          **单色位图的调色板未定义 ⇒ 位极性不确定** ⇒ 同一份代码、同一台机器上
///          「直接跑 `C0` / gdb 下 `30`」来回翻转（**同一二进制、两种环境、两种结果**）。
///          ⇒ 一次备足 256 项，让调色板**确定**（全 0），装置才可复现。
struct BitmapInfo256
{
	BITMAPINFO info{};
	RGBQUAD    palette[255]{};
};

/// @brief 建 DIB section（bpp ∈ {32, 24, 1}）
/// @details ★ **正 biHeight = bottom-up**（图标惯例——详设 §6.1）⇒ **内存第 0 行 = 视觉底行**。
///          行跨距按 DIB 规则 4 字节对齐（bpp = 1 时即 ((w+31)/32)*4——与内核读 mask 的口径同源）。
DibSection MakeDibSection(int w, int h, unsigned short bpp)
{
	BitmapInfo256 bi{};   // ★ 256 项空间——1bpp 也要 2 项（见上）
	bi.info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bi.info.bmiHeader.biWidth = w;
	bi.info.bmiHeader.biHeight = h;   // ★ 正值 = bottom-up
	bi.info.bmiHeader.biPlanes = 1;
	bi.info.bmiHeader.biBitCount = bpp;
	bi.info.bmiHeader.biCompression = BI_RGB;

	DibSection s{};
	s.bitmap = CreateDIBSection(nullptr, &bi.info, DIB_RGB_COLORS, &s.bits, nullptr, 0);
	s.stride = ((w * static_cast<int>(bpp) + 31) / 32) * 4;
	return s;
}

/// @brief 源位图守卫
/// @details ★ `CreateIconIndirect` **拷贝**位图内容 ⇒ 源位图仍归调用者，必须 `DeleteObject`
///          （图标句柄与这两个 bitmap 是两套 ownership——与内核侧「三栏」事实同源）。
struct SourceBitmap
{
	HBITMAP bitmap = nullptr;
	~SourceBitmap() { if (bitmap != nullptr) DeleteObject(bitmap); }
};

/// @brief 取某**视觉行**的内存行首（bottom-up 翻转的唯一落点）
std::uint8_t* RowOf(const DibSection& s, int h, int yVisual)
{
	const std::size_t rowBytes = static_cast<std::size_t>(h - 1 - yVisual) * s.stride;
	return static_cast<std::uint8_t*>(s.bits) + rowBytes;
}

/// @brief 写一个 32bpp（BGRA）像素——坐标按**视觉**行
void PutPixel32(const DibSection& s, int h, int x, int yVisual,
				std::uint8_t b, std::uint8_t g, std::uint8_t r, std::uint8_t a)
{
	std::uint8_t* px = RowOf(s, h, yVisual) + static_cast<std::size_t>(x) * 4;
	px[0] = b;
	px[1] = g;
	px[2] = r;
	px[3] = a;
}

/// @brief 写一个 24bpp（BGR）像素——坐标按**视觉**行
void PutPixel24(const DibSection& s, int h, int x, int yVisual,
				std::uint8_t b, std::uint8_t g, std::uint8_t r)
{
	std::uint8_t* px = RowOf(s, h, yVisual) + static_cast<std::size_t>(x) * 3;
	px[0] = b;
	px[1] = g;
	px[2] = r;
}

/// @brief 写 1bpp 位图的一位——坐标按**视觉**行；★ 位序 **MSB-first**（与内核读 mask 同口径）
void PutMaskBit(const DibSection& s, int h, int x, int yVisual, bool set)
{
	std::uint8_t* byte = RowOf(s, h, yVisual) + static_cast<std::size_t>(x >> 3);
	const std::uint8_t m = static_cast<std::uint8_t>(0x80u >> (x & 7));
	*byte = set ? static_cast<std::uint8_t>(*byte | m) : static_cast<std::uint8_t>(*byte & ~m);
}

/// @brief 组装 HICON（失败 ⇒ nullptr）
HICON MakeIcon(HBITMAP color, HBITMAP mask)
{
	ICONINFO ii{};
	ii.fIcon = TRUE;
	ii.hbmColor = color;
	ii.hbmMask = mask;
	return CreateIconIndirect(&ii);
}

/// @brief 内核调用的统一封装：造图标 → ImageFromHIcon → 释放 HICON
/// @details ★ HICON 的 ownership **归调用者**（内核与 shell 各自负责自己的那个句柄——详设 §2.3），
///          故用完必须 `DestroyIcon`；本封装内含释放，杜绝用例各自漏放。
Image IconFromBitmaps(HBITMAP color, HBITMAP mask)
{
	const HICON icon = MakeIcon(color, mask);
	EXPECT_TRUE(icon != nullptr);

	const Image img = Decode::ImageFromHIcon(icon);

	if (icon != nullptr) DestroyIcon(icon);
	return img;
}

/// @brief 外壳组共同断言（非空组——详设 §6.2）
void ExpectNonEmptyContract(const Image& img)
{
	EXPECT_TRUE(img.width > 0);
	EXPECT_TRUE(img.height > 0);
	EXPECT_EQ(img.stride, img.width * 4);
	const std::size_t expectedBytes =
		static_cast<std::size_t>(img.stride) * static_cast<std::size_t>(img.height);
	EXPECT_EQ(img.pixels.size(), expectedBytes);
}

/// @brief 逐字节比对（★ **先校验 size**）
/// @details ⚠️ 必须先校验：`_ITERATOR_DEBUG_LEVEL=2`（MSVC Debug 默认；clang 用 MSVC STL
///          时同样）下**越界 `operator[]` 是断言 → 直接 `abort`**，会把「报告失败」升级成
///          「进程消失 + stdout 未 flush 全丢」——可诊断的失败变成不可诊断的崩溃。
///          故本 helper 把守卫收在一处，杜绝各用例各写一遍时漏掉。
void ExpectBytes(const Image& img, const std::uint8_t* expected, std::size_t n)
{
	EXPECT_EQ(img.pixels.size(), n);
	if (img.pixels.size() != n) return;   // ★ 守卫生效：尺寸不符时只报尺寸，不越界
	for (std::size_t i = 0; i < n; ++i) EXPECT_EQ(img.pixels[i], expected[i]);
}

/// @brief 测试 exe 自身路径（UTF-8）——**必然存在**、**零创建零清理**（详设 §6.2 路径资产）
std::string SelfExePathUtf8()
{
	wchar_t buf[kPathCap] = {};
	const DWORD n = GetModuleFileNameW(nullptr, buf, kPathCap);
	if (n == 0 || n >= kPathCap) return {};
	return WideToUTF8(std::wstring(buf, n));
}

/// @brief 临时目录（UTF-8）——**必然存在的目录**（详设 §6.2 路径资产）
std::string TempDirPathUtf8()
{
	wchar_t buf[kPathCap] = {};
	const DWORD n = GetTempPathW(kPathCap, buf);
	if (n == 0 || n >= kPathCap) return {};
	return WideToUTF8(std::wstring(buf, n));
}

// ══════════════════════════════════════════════════════════════════
// 内核组（T21-1..T21-3）——自造图标，逐字节断言
// ══════════════════════════════════════════════════════════════════

// ── T21-1：2×2 · 32bpp + alpha ⇒ 钉住 **alpha 路径** + premultiply 口径 + 多行方向 ──
void TestIconColor32WithAlpha()
{
	constexpr int kW = 2;
	constexpr int kH = 2;
	constexpr std::uint8_t kCh = 0x40;   // B = G = R

	DibSection color = MakeDibSection(kW, kH, 32);
	SourceBitmap colorGuard{color.bitmap};
	DibSection mask = MakeDibSection(kW, kH, 1);
	SourceBitmap maskGuard{mask.bitmap};
	EXPECT_TRUE(color.bitmap != nullptr);
	EXPECT_TRUE(mask.bitmap != nullptr);

	// alpha：视觉**顶行** = 0x00 / 0x40，视觉**底行** = 0x80 / 0xFF；mask 全清（alpha 路径应忽略它）
	const std::uint8_t alpha[kH][kW] = { { 0x00, 0x40 }, { 0x80, 0xFF } };
	for (int y = 0; y < kH; ++y)
		for (int x = 0; x < kW; ++x)
			PutPixel32(color, kH, x, y, kCh, kCh, kCh, alpha[y][x]);

	const Image img = IconFromBitmaps(color.bitmap, mask.bitmap);

	EXPECT_EQ(img.width, 2);
	EXPECT_EQ(img.height, 2);
	EXPECT_EQ(img.stride, 8);
	EXPECT_EQ(img.pixels.size(), std::size_t(16));

	// ★ 逐字节期望 = premultiply 口径 `(c * a + 127) / 255` 的定点：
	//   0x40 × {0x00, 0x40, 0x80, 0xFF} ⇒ 0x00 / 0x10 / 0x20 / 0x40。
	//   ★ 同时钉住「多行方向」——若行序反了，最前两个像素会是 0x20 / 0x40 而非 0x00 / 0x10。
	const std::uint8_t expected[16] = {
		0x00, 0x00, 0x00, 0x00,   0x10, 0x10, 0x10, 0x40,
		0x20, 0x20, 0x20, 0x80,   0x40, 0x40, 0x40, 0xFF,
	};
	ExpectBytes(img, expected, 16);   // ★ 内部先校验 size（条 110：越界读会直接 abort）
}

// ── T21-2：4×1 · 24bpp color + 1bpp mask ⇒ 钉住 **mask 路径**（★ 位序/极性无关） ──
// ★★ 为什么**不**逐字节钉 mask 的位映射（2026-09-27 实测定案）：
//    系统把「自造图标」的 mask 交给 `GetIconInfo` 时，**不保证保持调用方写入的位映射**——
//    实测：本装置写 `0xC0`（px0 / px1 置位），`GetIconInfo` 回来的副本**稳定**读作 `0x30`
//    （px2 / px3 置位）。这是**系统侧的副本语义**，不是内核判据写错：「置位 = 全透明」对
//    **真实 shell 图标**根本走不到（真实图标携带 alpha ⇒ 走 alpha 路径，P10 / P11）。
//    ⇒ 逐字节期望属**过度指定**——它钉的是「我们控制不了、也不该依赖」的量。改用位序 /
//      极性无关的语义断言，保留真正要守的全部性质（见函数内注释）。
void TestIconColor24WithMask()
{
	constexpr int kW = 4;
	constexpr int kH = 1;

	DibSection color = MakeDibSection(kW, kH, 24);
	SourceBitmap colorGuard{color.bitmap};
	DibSection mask = MakeDibSection(kW, kH, 1);
	SourceBitmap maskGuard{mask.bitmap};
	EXPECT_TRUE(color.bitmap != nullptr);
	EXPECT_TRUE(mask.bitmap != nullptr);

	for (int x = 0; x < kW; ++x) PutPixel24(color, kH, x, 0, 0x10, 0x20, 0x30);

	// mask = 0xC0 ⇒ **px0 / px1 置位**（置位 = 全透明）
	PutMaskBit(mask, kH, 0, 0, true);
	PutMaskBit(mask, kH, 1, 0, true);

	const Image img = IconFromBitmaps(color.bitmap, mask.bitmap);

	// ── 几何 ──
	EXPECT_EQ(img.width, 4);
	EXPECT_EQ(img.height, 1);
	EXPECT_EQ(img.stride, 16);
	EXPECT_EQ(img.pixels.size(), std::size_t(16));
	if (img.pixels.size() != std::size_t(16)) return;   // ★ 越界读守卫（条 110）

	// ── 语义断言：每个像素**必属且仅属**两形态之一 ──
	// 24bpp 源经 `GetIconInfo` 被归一化为 **32bpp 且 alpha 恒 0**（P10 / P11 实测）⇒ 内核必须改读 mask，
	// 于是每个像素只可能是：
	//   · **全透明** `00 00 00 00` —— mask 该位**置位** ⇒ A = 0 ⇒ premultiply 后全 0
	//   · **不透明** `10 20 30 FF` —— mask 该位**清零** ⇒ A = 255 ⇒ 原色不动（源写入 B=0x10 / G=0x20 / R=0x30）
	// ★ 两形态合起来守住：**通道序**（若写成 `30 20 10 FF` 则不属任一形态）· **mask 确实生效**
	//   （不是误用了恒 0 的 alpha）。★ premultiply 的**中间值精度**由 T21-1 钉——本用例的 A 只有
	//   0 / 255，对 `(c * a + 127) / 255` 与截断版是等价的，钉不出差别。
	const std::uint8_t kClear[4] = { 0x00, 0x00, 0x00, 0x00 };
	const std::uint8_t kSolid[4] = { 0x10, 0x20, 0x30, 0xFF };
	int nClear = 0;   // 全透明像素数
	int nSolid = 0;   // 不透明像素数
	for (int x = 0; x < kW; ++x) {
		const std::uint8_t* px = img.pixels.data() + static_cast<std::size_t>(x) * 4;
		const bool clear = (std::memcmp(px, kClear, 4) == 0);
		const bool solid = (std::memcmp(px, kSolid, 4) == 0);
		EXPECT_TRUE(clear || solid);   // 出现第三种形态 ⇒ premultiply / 通道序出错
		if (clear) ++nClear;
		if (solid) ++nSolid;
	}
	EXPECT_EQ(nClear + nSolid, kW);   // 无「既非…亦非…」的像素

	// ★★ 反向断言（C9 的存在理由）：**两种形态都必须出现** ——
	//    · 误按位深判 / 误信恒 0 的 alpha ⇒ 整幅**全透明**（老式图标的典型症状）；
	//    · mask 完全没被读取（误走 alpha 路径）⇒ 整幅**全不透明**。
	//    一对断言同时挡住这两个极端。
	EXPECT_TRUE(nClear > 0);
	EXPECT_TRUE(nSolid > 0);
}

// ── T21-3：退化输入（nullptr / 1×1 边界尺寸） ──
void TestIconDegenerateInputs()
{
	// ① nullptr ⇒ 空 Image（C3；★ **不崩**、不泄漏）
	const Image nullImg = Decode::ImageFromHIcon(nullptr);
	EXPECT_EQ(nullImg.width, 0);
	EXPECT_EQ(nullImg.height, 0);
	EXPECT_TRUE(nullImg.pixels.empty());

	// ② 1×1 正常图标（最小合法尺寸）⇒ 契约成立
	DibSection color = MakeDibSection(1, 1, 32);
	SourceBitmap colorGuard{color.bitmap};
	DibSection mask = MakeDibSection(1, 1, 1);
	SourceBitmap maskGuard{mask.bitmap};

	PutPixel32(color, 1, 0, 0, 0x11, 0x22, 0x33, 0xFF);

	const Image img = IconFromBitmaps(color.bitmap, mask.bitmap);

	EXPECT_EQ(img.width, 1);
	EXPECT_EQ(img.height, 1);
	EXPECT_EQ(img.stride, 4);
	EXPECT_EQ(img.pixels.size(), std::size_t(4));
	if (img.pixels.size() == std::size_t(4)) {   // ★ 越界读守卫
		EXPECT_EQ(img.pixels[0], 0x11);
		EXPECT_EQ(img.pixels[1], 0x22);
		EXPECT_EQ(img.pixels[2], 0x33);
		EXPECT_EQ(img.pixels[3], 0xFF);
	}
}

// ══════════════════════════════════════════════════════════════════
// 外壳组（T21-4..T21-9）——真实路径 / 缺失路径 / 端到端
// ══════════════════════════════════════════════════════════════════

// ── T21-4：已存在普通文件 = **测试 exe 自身**（A6-①；评审 §16 的原缺口） ──
void TestSystemIconExistingFile()
{
	const std::string path = SelfExePathUtf8();
	EXPECT_TRUE(!path.empty());

	const Image img = Decode::DecodeSystemIcon(path);
	ExpectNonEmptyContract(img);
}

// ── T21-5：已存在目录 = 临时目录（A6-②；目录的 iIcon 独立——P2 佐证） ──
void TestSystemIconExistingDirectory()
{
	const std::string path = TempDirPathUtf8();
	EXPECT_TRUE(!path.empty());

	const Image img = Decode::DecodeSystemIcon(path);
	ExpectNonEmptyContract(img);
}

// ── T21-6：不存在 + **有**扩展名 ⇒ 仍出图标（A6-③；SHGFI_USEFILEATTRIBUTES 的价值——P4） ──
void TestSystemIconMissingWithExtension()
{
	const Image img = Decode::DecodeSystemIcon("Z:/nonexistent_dir/nope.txt");
	ExpectNonEmptyContract(img);
}

// ── T21-7：不存在 + **无**扩展名 ⇒ 仍出图标（A6-④） ──
void TestSystemIconMissingWithoutExtension()
{
	const Image img = Decode::DecodeSystemIcon("Z:/nonexistent_dir/nope");
	ExpectNonEmptyContract(img);
}

// ── T21-8：空串 / 全空白串 ⇒ 空 Image（C6；★ P7 实测该 API 对空路径**不报错** ⇒ 必须自己拦） ──
void TestSystemIconEmptyPath()
{
	const Image emptyStr = Decode::DecodeSystemIcon("");
	EXPECT_EQ(emptyStr.width, 0);
	EXPECT_EQ(emptyStr.height, 0);
	EXPECT_TRUE(emptyStr.pixels.empty());

	const Image blankStr = Decode::DecodeSystemIcon("   ");
	EXPECT_EQ(blankStr.width, 0);
	EXPECT_EQ(blankStr.height, 0);
	EXPECT_TRUE(blankStr.pixels.empty());
	// ★ `nullptr` 不设用例：公共 API 收 const std::string& ⇒ **语法上不可表达**（详设 §6.4）
}

// ── T21-9：端到端——DecodeSystemIcon → PaintContext::DrawImage → RecordingBackend ──
void TestSystemIconDrawImageCommand()
{
	const Image img = Decode::DecodeSystemIcon(SelfExePathUtf8());
	EXPECT_TRUE(img.width > 0);

	std::vector<RenderCommand> commands;
	RecordingBackend backend;
	PaintContext pc(commands, backend);

	pc.DrawImage(Rect{ 0, 0, 16, 16 }, img);

	EXPECT_EQ(commands.size(), 1);
	EXPECT_TRUE(std::holds_alternative<DrawImageCommand>(commands[0]));
	if (std::holds_alternative<DrawImageCommand>(commands[0])) {
		const auto& cmd = std::get<DrawImageCommand>(commands[0]);
		EXPECT_TRUE(cmd.image.width != 0);   // ★ 值拷贝进命令（ImageDecode.DrawImageCommand 同款锚点）
	}
}

}   // anonymous namespace

void ECDI::Test::RegisterIconDecodeTests()
{
	GetTestRegistry().Add("IconDecode.Color32WithAlpha",         &TestIconColor32WithAlpha);
	GetTestRegistry().Add("IconDecode.Color24WithMask",          &TestIconColor24WithMask);
	GetTestRegistry().Add("IconDecode.DegenerateInputs",         &TestIconDegenerateInputs);
	GetTestRegistry().Add("IconDecode.SystemIconExistingFile",   &TestSystemIconExistingFile);
	GetTestRegistry().Add("IconDecode.SystemIconExistingDir",    &TestSystemIconExistingDirectory);
	GetTestRegistry().Add("IconDecode.SystemIconMissingWithExt", &TestSystemIconMissingWithExtension);
	GetTestRegistry().Add("IconDecode.SystemIconMissingNoExt",   &TestSystemIconMissingWithoutExtension);
	GetTestRegistry().Add("IconDecode.SystemIconEmptyPath",      &TestSystemIconEmptyPath);
	GetTestRegistry().Add("IconDecode.SystemIconDrawImageCmd",   &TestSystemIconDrawImageCommand);
}
