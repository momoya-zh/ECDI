#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 防御性 undef（规范 10）：本文件含 Windows.h——防宏污染 ECDI 头声明
#endif

#include "Platform/Win32/Win32RenderContext.h"
#include "Render/GDITextMeasurer.h"

#include <string>

using namespace ECDI;   // 装置与被测类型同域（TextMeasurerTests 同款）

// ════════════════════════════════════════════════════════════════════════
// Phase 36 批一：Font::weight 红线批（T36-2 —— A2 库内载体）
// ════════════════════════════════════════════════════════════════════════
// 装置 = TextMeasurerTests 同款 RAII 隐藏窗口（测量基准 DPI 来自窗口）。
// 判据 = 详设 §5 T36-2：SimSun（**无 Bold 字面的族**）@700 vs @400 的 GDI **合成加粗**
// 串宽比 ∈ [1.05, 1.10]——探针读数 183/170 = 1.076 的回归带
// （`.workbuddy/spike/p36/p36a2_readout.txt` [1]）。该 advance 变化即「lfWeight 接线生效」
// 的端到端证据；默认零回归红线（T36-1）由全量回归 + 既有测试文件零 diff 承担。

namespace {

/// @brief 测试窗口 RAII（隐藏窗口即足够——本组不读像素，无需 Show）
struct WeightWindow
{
    HWND hwnd = nullptr;
    ~WeightWindow() { if (hwnd) DestroyWindow(hwnd); }
};

bool CreateWeightWindow(const wchar_t* className, WeightWindow& out)
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

}   // namespace

/// T36-2：SimSun 合成加粗串宽回归带（A2 库内载体；P0-3 后唯一宽度判据）。
static void Test36SimSunSyntheticBoldWidthBand()
{
    WeightWindow w;
    if (!CreateWeightWindow(L"ECDI_T36_Weight", w))
    {
        return;
    }
    GDITextMeasurer measurer;
    measurer.Initialize(Win32RenderContext(w.hwnd));

    const Font regular{ .size = 20.0f, .family = "SimSun" };              // 默认 weight = 400（C36-1）
    const Font bold{ .size = 20.0f, .family = "SimSun", .weight = 700 };  // C7560：weight 声明于 family 后
    EXPECT_TRUE(regular.weight == 400);   // 默认值 = 既有行为（红线）

    const std::string text = "测试文本 test 123";   // 与探针 p36a2 同串（读数 170 / 183）
    const Size w4 = measurer.MeasureText(regular, text);
    const Size w7 = measurer.MeasureText(bold, text);

    EXPECT_TRUE(w7.width > w4.width);   // 方向判据（A2：合成加粗 advance 增大）
    const double ratio = static_cast<double>(w7.width) / static_cast<double>(w4.width);
    EXPECT_TRUE(ratio >= 1.05 && ratio <= 1.10);   // 详设 §5 T36-2 回归带（探针 1.076）
}

void ECDI::Test::RegisterFontWeightTests()
{
    GetTestRegistry().Add("FontWeight.SimSunSyntheticBoldWidthBand", &Test36SimSunSyntheticBoldWidthBand);   // T36-2
}
