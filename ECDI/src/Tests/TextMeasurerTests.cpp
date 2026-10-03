#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 防御性 undef（规范 10）：本文件含 Windows.h——防宏污染 ECDI 头声明
#endif

#include "ECDI/Core/String.h"                    // UTF8ToWide（T26-10 期望值复算用）
#include "Platform/Win32/Win32RenderContext.h"   // Initialize 注入（同 GDIBackend 先例）
#include "Render/GDITextMeasurer.h"              // 内部件（Phase 26 批一）

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

using namespace ECDI;

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

void ECDI::Test::RegisterTextMeasurerTests()
{
    GetTestRegistry().Add("TextMeasurer.UsesWindowDpi",      &Test26MeasurerUsesWindowDpi);   // T26-10
    GetTestRegistry().Add("TextMeasurer.GdiMeasureCacheHit", &Test26GdiMeasureCacheHit);      // T26-12
}
