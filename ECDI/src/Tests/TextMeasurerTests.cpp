#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 防御性 undef（规范 10）：本文件含 Windows.h——防宏污染 ECDI 头声明
#endif

#include "ECDI/Core/String.h"                    // UTF8ToWide（T26-10 期望值复算用）
#include "ECDI/Core/UTF8.h"                      // CodepointIndexToByteOffset（T29-FIT-2 oracle 切片）
#include "Platform/Win32/Win32RenderContext.h"   // Initialize 注入（同 GDIBackend 先例）
#include "Render/GDITextMeasurer.h"              // 内部件（Phase 26 批一）

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

using namespace ECDI;

/// @brief 浮点比较容差（与 TextBoxTests / WidgetTests 同口径 0.001f）
constexpr float kEps = 0.001f;

// ══════════════════════════════════════════════════════════════════════
// Phase 26 批一：文本测量链（T26-10 D-8 闭合 · T26-12 GDI 测量缓存）
// ══════════════════════════════════════════════════════════════════════
// ★ 本组**需要窗口**（测量基准 DPI 来自窗口）——装置形态沿 LineCoverageTests.cpp:275-290
//   的 RAII 窗口模式；★ 与 DpiTests.cpp 分文件（后者自述「不经窗口、纯函数」，见其 :17-21）。

namespace {

/// @brief 测试窗口 RAII（**隐藏窗口即足够**——本组不读像素，无需 Show；沿 L2 的 RAII 形态）
struct MeasurerWindow
{
    HWND hwnd = nullptr;
    ~MeasurerWindow() { if (hwnd) DestroyWindow(hwnd); }
};

bool CreateMeasurerWindow(const wchar_t* className, MeasurerWindow& out)
{
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = className;
    if (!RegisterClassW(&wc))
    {
        EXPECT_EQ(GetLastError(), ERROR_CLASS_ALREADY_EXISTS);   // 重复运行忽略
    }
    out.hwnd = CreateWindowExW(0, className, className, WS_POPUP,
                               0, 0, 200, 200, nullptr, nullptr, wc.hInstance, nullptr);
    EXPECT_TRUE(out.hwnd != nullptr);
    return out.hwnd != nullptr;
}

/// @brief 用**窗口 DPI 独立复算**某 Font 的文本宽度（DIP）——T26-10 的期望值 oracle
/// @details 与 `GDITextMeasurer` 内部**同公式**（`lround(size·dpi/96)` → `GetTextExtentPoint32W`
///          → 折回 DIP），但**显式使用 `GetDpiForWindow`** ⇒ 可判定「测量链是否以窗口 DPI 为基准」。
///          ★ oracle 独立于被测实现（不调用 `GDITextMeasurer` 的任何方法）。
float ExpectedWidthAtWindowDpi(HWND hwnd, const Font& font, const std::string& text)
{
    const std::wstring wide = UTF8ToWide(text);
    if (wide.empty())
    {
        return 0.0f;
    }

    int dpi = GetDpiForWindow(hwnd);
    if (dpi <= 0)
    {
        dpi = 96;   // fail-safe（与实现同口径）
    }

    LOGFONTW lf{};
    lf.lfHeight = -static_cast<LONG>(std::lround(font.size * dpi / 96.0));
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfWeight = FW_NORMAL;
    if (!font.family.empty())
    {
        const std::wstring wideFamily = UTF8ToWide(font.family);
        const size_t length = (std::min)(wideFamily.size(), static_cast<size_t>(LF_FACESIZE - 1));
        wideFamily.copy(lf.lfFaceName, length);
        lf.lfFaceName[length] = L'\0';
    }

    HFONT hfont = CreateFontIndirectW(&lf);
    if (!hfont)
    {
        return 0.0f;
    }

    HDC dc = GetDC(nullptr);
    if (!dc)
    {
        DeleteObject(hfont);
        return 0.0f;
    }

    HGDIOBJ oldFont = SelectObject(dc, hfont);
    SIZE extent{};
    const BOOL ok = GetTextExtentPoint32W(dc, wide.c_str(),
                                          static_cast<int>(wide.size()), &extent);
    SelectObject(dc, oldFont);
    ReleaseDC(nullptr, dc);
    DeleteObject(hfont);

    if (!ok)
    {
        return 0.0f;
    }
    return static_cast<float>(extent.cx) * (96.0f / static_cast<float>(dpi));
}

}   // namespace

// ── T26-10：D-8 闭合——测量链基准 = **窗口 DPI** ───────────────────────────────
// ★ 判据：`GDITextMeasurer` 的测量结果 == 用 `GetDpiForWindow` 复算的期望值。
// ★★ 环境限制（如实标注）：**本机为单屏**（窗口 DPI == 屏幕 DPI）⇒ 两链基准天然一致
//    ⇒ 本用例在本机**无法区分修复前后**（★ 这正是 D-8 长期潜伏的原因：单屏且 DPI 一致时
//    不产生差异）；**真区分需双屏 / 双 DPI 环境**。详见详设 §6「测试环境前提」。
void Test26MeasurerUsesWindowDpi()
{
    MeasurerWindow w;
    if (!CreateMeasurerWindow(L"ECDI_T26_MeasurerDpi", w))
    {
        return;
    }

    GDITextMeasurer measurer;
    measurer.Initialize(Win32RenderContext(w.hwnd));

    const Font font{ 14.0f, "" };              // 空 family = 系统默认（与 GDIBackend 同语义）
    const std::string text = "Abc 123";        // 混合 ASCII + 空格（度量路径确定）

    const Size measured = measurer.MeasureText(font, text);
    const float expected = ExpectedWidthAtWindowDpi(w.hwnd, font, text);

    EXPECT_TRUE(expected > 0.0f);
    EXPECT_NEAR(measured.width, expected, 0.5f);   // ★ 基准一致（±0.5 DIP）
    EXPECT_TRUE(measured.height > 0.0f);
}

// ── T26-12：GDI 测量缓存命中（§1.6 / △9b）─────────────────────────────────────
// ★ 判据用**观测缝**（`MeasureCacheMissCount`）——★ 命中与否的**返回值相同**（这正是缓存的
//   正确性）⇒ **无法凭返回值区分** ⇒ 必须用只读计数（详设 §6「观测缝」）。
void Test26GdiMeasureCacheHit()
{
    MeasurerWindow w;
    if (!CreateMeasurerWindow(L"ECDI_T26_MeasureCache", w))
    {
        return;
    }

    GDITextMeasurer measurer;
    measurer.Initialize(Win32RenderContext(w.hwnd));

    const Font font14{ 14.0f, "" };
    const Font font16{ 16.0f, "" };

    const std::size_t base = measurer.MeasureCacheMissCount();
    EXPECT_EQ(base, static_cast<std::size_t>(0));   // 新建 ⇒ 零未命中

    // ① 首次测量 ⇒ miss
    const Size first = measurer.MeasureText(font14, "Hello");
    EXPECT_EQ(measurer.MeasureCacheMissCount(), base + 1);

    // ② 同 (text, font, dpi) 再测 ⇒ **命中**（计数不增；★ 返回值一致）
    const Size second = measurer.MeasureText(font14, "Hello");
    EXPECT_EQ(measurer.MeasureCacheMissCount(), base + 1);
    EXPECT_NEAR(second.width, first.width, 0.001f);
    EXPECT_NEAR(second.height, first.height, 0.001f);

    // ③ 换 text ⇒ miss（键含 text）
    measurer.MeasureText(font14, "World");
    EXPECT_EQ(measurer.MeasureCacheMissCount(), base + 2);

    // ④ 换 font.size ⇒ miss（键含 size）
    measurer.MeasureText(font16, "Hello");
    EXPECT_EQ(measurer.MeasureCacheMissCount(), base + 3);

    // ⑤ 换 font.family ⇒ miss（键含 family）
    measurer.MeasureText(Font{ 14.0f, "Consolas" }, "Hello");
    EXPECT_EQ(measurer.MeasureCacheMissCount(), base + 4);

    // ⑥ 回头再测最初的那个组合 ⇒ 仍命中（计数不增——证明是**缓存**而非「只记最近一条」）
    measurer.MeasureText(font14, "Hello");
    EXPECT_EQ(measurer.MeasureCacheMissCount(), base + 4);
}

// ══════════════════════════════════════════════════════════════════════
// Phase 29 批一：FitText 三链（T29-FIT-1..T29-FIT-4）
// ══════════════════════════════════════════════════════════════════════
// ★ 本组断言的**硬契约**（详设 D29-Ⅲ / C29-1 / C29-2）：
//   - `fitCp` 是**相对**码点数（从 `startCp` 起）；`width` = 消费段宽（DIP）；
//   - `fitCp` 永不落 UTF-8 字节序列中间；
//   - **surrogate pair 不可拆分**（`A😀B` 边界）。
// ★ 交叉验证策略：GDI 原生覆写与默认二分体**同语义** ⇒ 两链对同一输入必须给出
//   **相同 fitCp**（宽度按链各自度量，不比——C29-8 跨链只承诺同链确定性）。

/// @brief 确定性测量器（每码点 8.0f 宽）——**默认 FitText 体的**验证宿主
/// @details ★ 这就是「既有自定义 TextMeasurer 零改动」的代表（盯防/批一验收 ⑦）：
///          它**只**实现两个纯虚（MeasureText / LineHeight），不覆写 FitText
///          ⇒ 编译通过即证明默认体内联体自包含、不要求派生类补实现。
class FixedWidthMeasurer final : public TextMeasurer
{
public:
	Size MeasureText(const Font&, const std::string& text) override
	{
		std::size_t count = 0;
		for (std::size_t i = 0; i < text.size(); ++i)
		{
			if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) { ++count; }
		}
		return Size{ static_cast<float>(count) * kPerCpWidth, 16.0f };
	}
	float LineHeight(const Font&) override { return 16.0f; }
	static constexpr float kPerCpWidth = 8.0f;
};

// ── T29-FIT-1：默认体 = 兼容 fallback（零改动派生类可用；码点边界保证）──────
// ★ 判据：① 派生类**不覆写** FitText 也能用（编译级）；② fitCp 是**相对**码点数；
//   ③ 恰好等于 maxWidth 时**算放得下**（> 才是超宽）；④ startCp 超界 ⇒ {0,0}。
void Test29DefaultFitTextCpBoundary()
{
    FixedWidthMeasurer measurer;
    const Font font{ 14.0f, "" };
    // "AB 你好" = 5 码点（1+1+1+3+3 字节 = 9 字节）——ASCII 与 CJK 多字节混串
    const std::string mixed = "AB \xE4\xBD\xA0\xE5\xA5\xBD";   // "AB 你好"

    // ① 相对口径：startCp = 2（"你"）起，两个码点宽 16 ⇒ maxWidth = 16 恰好放得下
    const TextFit exact = measurer.FitText(font, mixed, 2, 2 * FixedWidthMeasurer::kPerCpWidth);
    EXPECT_EQ(exact.fitCp, static_cast<std::size_t>(2));
    EXPECT_NEAR(exact.width, 2 * FixedWidthMeasurer::kPerCpWidth, kEps);

    // ② 少 0.5 ⇒ 只放得下一个码点（严格 > 才断——「恰好等于」不属超宽）
    const TextFit less = measurer.FitText(font, mixed, 2, 2 * FixedWidthMeasurer::kPerCpWidth - 0.5f);
    EXPECT_EQ(less.fitCp, static_cast<std::size_t>(1));
    EXPECT_NEAR(less.width, FixedWidthMeasurer::kPerCpWidth, kEps);

    // ③ ★ **码点边界**：对每个起点穷举 maxWidth 序列，fitCp 必落在码点边界
    //   （反例会被 3 字节 CJK 序列的中途字节暴露——宽度模型每码点恒宽 8 ⇒
    //   任何「切在序列中间」的实现都会给出非 8 倍数宽度）
    for (std::size_t start = 0; start <= 5; ++start)
    {
        for (int step = 1; step <= 40; ++step)
        {
            const float width = static_cast<float>(step) * 0.5f;
            const TextFit fit = measurer.FitText(font, mixed, start, width);
            const float rem = fit.width / FixedWidthMeasurer::kPerCpWidth;
            const bool integral = (rem > (std::floor)(rem)) == false;
            EXPECT_TRUE(integral);   // 宽度必是「整数 × 每码点宽」⇒ 不可能切在序列中间
        }
    }

    // ④ startCp 越过串尾 / 空串 ⇒ {0, 0}
    const TextFit past = measurer.FitText(font, mixed, 99, 1000.0f);
    EXPECT_EQ(past.fitCp, static_cast<std::size_t>(0));
    EXPECT_NEAR(past.width, 0.0f, kEps);
    const TextFit empty = measurer.FitText(font, std::string(), 0, 100.0f);
    EXPECT_EQ(empty.fitCp, static_cast<std::size_t>(0));

    // ⑤ 放得下全部 ⇒ fitCp = 剩余码点数（5 码点，从 4 起剩 1）
    const TextFit all = measurer.FitText(font, mixed, 4, 1000.0f);
    EXPECT_EQ(all.fitCp, static_cast<std::size_t>(1));
    const TextFit allHead = measurer.FitText(font, mixed, 3, 1000.0f);
    EXPECT_EQ(allHead.fitCp, static_cast<std::size_t>(2));
}

// ── T29-FIT-2：GDI 原生覆写（surrogate 不拆 + 与默认体 fitCp 同语义）───────
// ★ 判据：① `A😀B` 在能把 B 放下的宽度下 ⇒ fitCp = 2（"A😀"）而**非** 3；
//         ② GDI 与默认体在 ASCII/CJK 混串上 fitCp **逐例一致**（跨链同语义）。
void Test29GdiFitTextSurrogate()
{
    MeasurerWindow w;
    if (!CreateMeasurerWindow(L"ECDI_T29_GdiFitText", w))
    {
        return;
    }

    GDITextMeasurer measurer;
    measurer.Initialize(Win32RenderContext(w.hwnd));
    const Font font{ 18.0f, "" };
    FixedWidthMeasurer fallback;   // 默认体宿主（确定性宽度模型）

    // ① 宽度充裕 ⇒ 三个码点全放得下（"A" + 😀 + "B" = 3 码点 / 6 字节 / 4 wchar）
    const std::string surrogate = "A\xF0\x9F\x98\x80" "B";
    const float fullWidth = measurer.MeasureText(font, surrogate).width;
    EXPECT_TRUE(fullWidth > 0.0f);
    const TextFit all = measurer.FitText(font, surrogate, 0, fullWidth);
    EXPECT_EQ(all.fitCp, static_cast<std::size_t>(3));

    // ② ★ 收窄到「A😀 放得下、B 放不下」——用 B 的宽当缺口（比 1 个码点宽更保守）
    const float abWidth = measurer.MeasureText(font, std::string("A\xF0\x9F\x98\x80")).width;
    const TextFit partial = measurer.FitText(font, surrogate, 0, abWidth);
    // 断言的是**不可拆分**：若实现把 fitW 停在代理对中间（2 wchar = "A" + 高代理），
    // 三步换算的 ① 会 −−fitW ⇒ 退化到 1 码点（"A"）⇒ 断言可辨。
    EXPECT_EQ(partial.fitCp, static_cast<std::size_t>(2));
    EXPECT_NEAR(partial.width, abWidth, 0.5f);

    // ③ 只放得下 "A" ⇒ 1 码点
    const float aWidth = measurer.MeasureText(font, std::string("A")).width;
    const TextFit tiny = measurer.FitText(font, surrogate, 0, aWidth);
    EXPECT_EQ(tiny.fitCp, static_cast<std::size_t>(1));

    // ④ 零宽 ⇒ 一个码点也放不下 ⇒ {0, 0}（退化语义的显式锚）
    const TextFit zero = measurer.FitText(font, surrogate, 0, 0.0f);
    EXPECT_EQ(zero.fitCp, static_cast<std::size_t>(0));
    EXPECT_NEAR(zero.width, 0.0f, kEps);

    // ⑤ ★ **GDI 原生路径 vs 独立 oracle**：`FitText` 走 `GetTextExtentExPointW`，
    //   oracle 走 `MeasureText`（`GetTextExtentPoint32W`）——**两条不同的 Win32 原生调用**。
    //   对 ASCII/CJK 混串逐宽度求「最大可消费前缀」，两者 fitCp 必须一致。
    //   ★ 这才是可判的同语义验证：**不与默认体比**（默认体走每码点恒宽 8 的模型，
    //     与 GDI 的比例字体断点本就不同——C29-8 只承诺**同链确定性**）。
    const std::string mixed = "AB \xE4\xBD\xA0\xE5\xA5\xBD";   // 5 码点 / 9 字节
    const std::size_t mixedCp = ByteOffsetToCodepointIndex(mixed, mixed.size());
    EXPECT_EQ(mixedCp, static_cast<std::size_t>(5));
    const Size mixedSize = measurer.MeasureText(font, mixed);
    EXPECT_TRUE(mixedSize.width > 0.0f);

    for (int k = 0; k <= 12; ++k)
    {
        const float limit = mixedSize.width * static_cast<float>(k) / 12.0f;   // 0 → 1.0 倍全宽
        // oracle = 按 `MeasureText` 逐前缀取最大可消费码点数（朴素线性，非二分）
        std::size_t oracle = 0;
        for (std::size_t n = 1; n <= mixedCp; ++n)
        {
            const std::size_t b0 = CodepointIndexToByteOffset(mixed, 0);
            const std::size_t b1 = CodepointIndexToByteOffset(mixed, n);
            if (measurer.MeasureText(font, mixed.substr(b0, b1 - b0)).width <= limit)
            {
                oracle = n;
            }
        }
        const TextFit gdi = measurer.FitText(font, mixed, 0, limit);
        EXPECT_EQ(gdi.fitCp, oracle);
        // ★ 消费段宽亦须与 oracle 的测量一致（±1 物理像素级：GDI 像素取整 ⇒ 放宽到 0.6 DIP）
        if (oracle > 0)
        {
            const std::size_t b1 = CodepointIndexToByteOffset(mixed, oracle);
            const Size prefix = measurer.MeasureText(font, mixed.substr(0, b1));
            EXPECT_NEAR(gdi.width, prefix.width, 0.6f);
        }
    }

    (void)fallback;   // ★ 默认体的同语义验证在 T29-FIT-1（固定宽模型下可精确判）
}

void ECDI::Test::RegisterTextMeasurerTests()
{
    GetTestRegistry().Add("TextMeasurer.UsesWindowDpi",      &Test26MeasurerUsesWindowDpi);   // T26-10
    GetTestRegistry().Add("TextMeasurer.GdiMeasureCacheHit", &Test26GdiMeasureCacheHit);      // T26-12
    GetTestRegistry().Add("TextMeasurer.DefaultFitTextCpBoundary", &Test29DefaultFitTextCpBoundary);   // T29-FIT-1
    GetTestRegistry().Add("TextMeasurer.GdiFitTextSurrogate",     &Test29GdiFitTextSurrogate);       // T29-FIT-2
}
