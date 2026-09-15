#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 防御性 undef（规范 10）：本文件含 Windows.h——防 DrawTextW 宏污染 ECDI 头声明
#endif

#include "Render/CornerCoverageMask.h"          // L1：Internal 头（纯几何，零 GDI）
#include "Render/GDIBackend.h"                  // L2：AA 开关 + 真实渲染
#include "Platform/Win32/Win32RenderContext.h"
#include "ECDI/Core/Color.h"
#include "ECDI/Core/Point.h"
#include "ECDI/Core/Rect.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace ECDI;

namespace {

constexpr double kPi = 3.14159265358979323846;

// ══════════════════════════════════════════════════════════════════
// L1：覆盖度掩码（零窗口依赖）——数学正确性
// ══════════════════════════════════════════════════════════════════

/// @brief 面积守恒（详设 §6.1 L1-a；容差 tol(S,R) = 0.5·√R/S，§6.4）
/// @details 测试对象 = `GenerateCornerMask(R)` 的掩码本身（**不是** DrawRoundedRect）。
/// 必须打印 Σ(c/255) / πR²/4 / 实际误差 / tol——便于 §6.4d 的实测收紧程序。
void TestMaskAreaConservation()
{
    struct Case { int radius; int samples; };
    const Case cases[] = { { 1, 8 }, { 8, 8 }, { 32, 8 }, { 8, 16 } };

    for (const Case& cs : cases)
    {
        const CornerCoverageMask mask = GenerateCornerMask(cs.radius, cs.samples);
        EXPECT_EQ(mask.radius, cs.radius);
        EXPECT_EQ(static_cast<int>(mask.coverage.size()), cs.radius * cs.radius);

        double sum = 0.0;
        for (std::uint8_t c : mask.coverage)
        {
            sum += static_cast<double>(c) / 255.0;
        }

        const double r = static_cast<double>(cs.radius);
        const double expected = kPi * r * r / 4.0;
        const double tol = 0.5 * std::sqrt(r) / static_cast<double>(cs.samples);
        const double err = std::abs(sum - expected);

        std::printf("[AntiAliasing] R=%2d S=%2d  sum(c/255)=%.6f  pi*R^2/4=%.6f  err=%.6f  tol=%.6f\n",
                    cs.radius, cs.samples, sum, expected, err, tol);

        EXPECT_TRUE(err <= tol);
    }
}

/// @brief 精确字节锚点（详设 §3.4b）——**精确相等、无容差**
/// @details 抓「采样坐标 / 舍入规则 / 判定边界」三类实现细节错误；面积守恒（容差式）
/// 对这类错误不敏感。因 S 取 2 的幂时运算全程为二进制有理数（§3.4c），故可跨平台精确断言。
void TestMaskExactAnchors()
{
    const std::uint8_t kAnchorR1[] = { 207 };
    const std::uint8_t kAnchorR2[] = { 84, 235, 235, 255 };
    const std::uint8_t kAnchorR3[] = { 4, 143, 243, 143, 255, 255, 243, 255, 255 };
    const std::uint8_t kAnchorR4[] = { 0,  40, 179, 247,  40, 243, 255, 255,
                                      179, 255, 255, 255, 247, 255, 255, 255 };

    struct Anchor { int radius; const std::uint8_t* bytes; int count; };
    const Anchor anchors[] = {
        { 1, kAnchorR1, 1  },
        { 2, kAnchorR2, 4  },
        { 3, kAnchorR3, 9  },
        { 4, kAnchorR4, 16 }
    };

    for (const Anchor& a : anchors)
    {
        const CornerCoverageMask mask = GenerateCornerMask(a.radius, 8);
        EXPECT_EQ(mask.radius, a.radius);
        EXPECT_EQ(static_cast<int>(mask.coverage.size()), a.count);
        if (static_cast<int>(mask.coverage.size()) != a.count)
        {
            continue;   // 尺寸不符时不再逐元素比对（避免越界）
        }
        for (int i = 0; i < a.count; ++i)
        {
            EXPECT_EQ(static_cast<int>(mask.coverage[i]), static_cast<int>(a.bytes[i]));
        }
    }
}

/// @brief 值域与「全内 / 全外」像素存在性（详设 §6.1 L1-b）
/// @details 值域由存储类型 `std::uint8_t` 天然保证 [0,255]；此处断言的是存在性。
/// ⚠️ v1.1 修正：全外像素（`c == 0`）自 **R >= 4** 起才存在——R = 2/3 时最外角点像素
/// 仍有部分覆盖（R=2 最小 84、R=3 最小 4），原详设「R = 2,4,8,16 均有 0」有误。
void TestMaskValueRange()
{
    // 全内像素（c == 255）：R >= 2 起存在
    for (int R : { 2, 4, 8, 16 })
    {
        const CornerCoverageMask mask = GenerateCornerMask(R, 8);
        bool hasFull = false;
        for (std::uint8_t c : mask.coverage)
        {
            if (c == 255) { hasFull = true; break; }
        }
        EXPECT_TRUE(hasFull);
    }

    // 全外像素（c == 0）：R >= 4 起存在
    for (int R : { 4, 8, 16 })
    {
        const CornerCoverageMask mask = GenerateCornerMask(R, 8);
        bool hasEmpty = false;
        for (std::uint8_t c : mask.coverage)
        {
            if (c == 0) { hasEmpty = true; break; }
        }
        EXPECT_TRUE(hasEmpty);
    }
}

/// @brief AA 生效判据：存在部分覆盖像素（详设 §6.1 L1-c）
/// @details 硬边掩码只有 0/255 → 此条必失败。
void TestMaskPartialCoverage()
{
    const CornerCoverageMask mask = GenerateCornerMask(8, 8);
    bool hasPartial = false;
    for (std::uint8_t c : mask.coverage)
    {
        if (c > 0 && c < 255) { hasPartial = true; break; }
    }
    EXPECT_TRUE(hasPartial);
}

/// @brief 单调性：沿两轴向圆心方向非递减（详设 §6.1 L1-d 的机械规则）
void TestMaskMonotonic()
{
    const int R = 8;
    const CornerCoverageMask mask = GenerateCornerMask(R, 8);

    // 固定 j：c(i,j) 随 i 增大非递减
    for (int j = 0; j < R; ++j)
    {
        for (int i = 0; i + 1 < R; ++i)
        {
            EXPECT_TRUE(mask.At(i, j) <= mask.At(i + 1, j));
        }
    }
    // 固定 i：c(i,j) 随 j 增大非递减
    for (int i = 0; i < R; ++i)
    {
        for (int j = 0; j + 1 < R; ++j)
        {
            EXPECT_TRUE(mask.At(i, j) <= mask.At(i, j + 1));
        }
    }
}

/// @brief 索引变换（详设 §6.1 L1-e / §5.5）
void TestMaskIndexTransform()
{
    const int R = 8;
    const CornerId corners[4] = { CornerId::TopLeft, CornerId::TopRight,
                                  CornerId::BottomLeft, CornerId::BottomRight };

    // 四角 × 全部局部坐标：返回值恒落在 [0, R)
    for (CornerId corner : corners)
    {
        for (int local = 0; local < R; ++local)
        {
            const int ix = MaskIndexX(local, R, corner);
            const int iy = MaskIndexY(local, R, corner);
            EXPECT_TRUE(ix >= 0 && ix < R);
            EXPECT_TRUE(iy >= 0 && iy < R);
        }
    }

    // 右侧两角水平反向、左侧两角不反向
    for (int local = 0; local < R; ++local)
    {
        EXPECT_EQ(MaskIndexX(local, R, CornerId::TopRight),    R - 1 - local);
        EXPECT_EQ(MaskIndexX(local, R, CornerId::BottomRight), R - 1 - local);
        EXPECT_EQ(MaskIndexX(local, R, CornerId::TopLeft),     local);
        EXPECT_EQ(MaskIndexX(local, R, CornerId::BottomLeft),  local);

        EXPECT_EQ(MaskIndexY(local, R, CornerId::BottomLeft),  R - 1 - local);
        EXPECT_EQ(MaskIndexY(local, R, CornerId::BottomRight), R - 1 - local);
        EXPECT_EQ(MaskIndexY(local, R, CornerId::TopLeft),     local);
        EXPECT_EQ(MaskIndexY(local, R, CornerId::TopRight),    local);
    }
}

/// @brief 确定性：同参数两次调用逐元素相同（详设 §6.1 L1-f）
void TestMaskDeterminism()
{
    const CornerCoverageMask a = GenerateCornerMask(8, 8);
    const CornerCoverageMask b = GenerateCornerMask(8, 8);

    EXPECT_EQ(a.radius, b.radius);
    EXPECT_EQ(a.coverage.size(), b.coverage.size());
    EXPECT_TRUE(a.coverage == b.coverage);
}

/// @brief 缓存命中：同半径二次请求返回同一地址（详设 §6.1 L1-g）
void TestCacheHit()
{
    CornerMaskCache cache;

    const CornerCoverageMask& first = cache.Get(8);
    const std::size_t sizeAfterFirst = cache.Size();
    const CornerCoverageMask& second = cache.Get(8);

    EXPECT_TRUE(&first == &second);              // map 引用稳定
    EXPECT_EQ(cache.Size(), sizeAfterFirst);     // 未新增条目
    EXPECT_EQ(cache.GetSamples(), 8);            // 默认倍率
}

/// @brief SetSamples 清空缓存 + 非法值 no-op（详设 §4 / §6.1 L1-h）
void TestCacheSetSamplesClears()
{
    CornerMaskCache cache;

    cache.Get(8);
    EXPECT_EQ(cache.Size(), std::size_t(1));

    cache.SetSamples(16);
    EXPECT_EQ(cache.GetSamples(), 16);
    EXPECT_EQ(cache.Size(), std::size_t(0));     // ⚠️ 必须清空：不同 S 的量化级别不可混用

    cache.Get(4);
    EXPECT_EQ(cache.Size(), std::size_t(1));

    cache.SetSamples(0);                          // samples <= 0 → no-op
    EXPECT_EQ(cache.GetSamples(), 16);
    EXPECT_EQ(cache.Size(), std::size_t(1));

    cache.SetSamples(-3);                         // 同 no-op
    EXPECT_EQ(cache.GetSamples(), 16);
    EXPECT_EQ(cache.Size(), std::size_t(1));

    cache.SetSamples(16);                         // 与当前值相同 → 幂等 no-op（不清缓存）
    EXPECT_EQ(cache.Size(), std::size_t(1));
}

/// @brief 非法入参 → 空掩码（详设 §6.1 L1-i）
void TestMaskInvalidArgs()
{
    EXPECT_TRUE(GenerateCornerMask(0, 8).Empty());
    EXPECT_TRUE(GenerateCornerMask(-1, 8).Empty());
    EXPECT_TRUE(GenerateCornerMask(8, 0).Empty());
    EXPECT_TRUE(GenerateCornerMask(0, 0).Empty());

    // 奇数 S 合法（仅「推荐 2 的幂」——非契约）
    EXPECT_FALSE(GenerateCornerMask(4, 7).Empty());
    EXPECT_EQ(GenerateCornerMask(4, 7).radius, 4);
}

// ══════════════════════════════════════════════════════════════════
// L2：GDIBackend 集成（真窗口像素读回）——渲染集成正确性
// ══════════════════════════════════════════════════════════════════

/// @brief 测试窗口 RAII（沿用 RendererTests.cpp 既有模式）
struct AAWindow
{
    HWND hwnd = nullptr;
    ~AAWindow() { if (hwnd) DestroyWindow(hwnd); }
};

/// @brief 创建 200×200 测试窗口
/// @details **右上角**短暂显示，`SW_SHOWNOACTIVATE` 不抢焦点。取右上是为了避开任务栏一类常驻遮挡物
///          ——**但注意**：遮挡并**不是**本用例历史失败的成因（真因见下），改位置对当时的症状零改善，
///          保留右上只是因为它无害且更稳。
/// @note **屏幕外窗口的窗口 DC 裁剪区域为空 → `GetPixel` 恒返 `CLR_INVALID`**（`RendererTests.cpp` 已注释该实测坑）。
/// @note **历史 flaky 真因（2026-09-14 定性）**：窗口线程长时间不取消息 ⇒ DWM 判定其无响应 ⇒ 以类名
///       `Ghost` 的替身窗口（同 z-order / 位置 / 大小）替换原窗口 ⇒ 原窗口不再被合成绘制 ⇒ 其 DC 可见区
///       变 `NULLREGION` ⇒ 整帧读不到。修复见 `test_main.cpp` 的 `DisableProcessWindowsGhosting()`。
bool CreateAAWindow(const wchar_t* className, const wchar_t* title, AAWindow& out)
{
    const int screenW = GetSystemMetrics(SM_CXSCREEN);

    const int kTopMargin = 10;   // 顶部避开任务栏（非历史失败成因，见上方 @note）

    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = className;
    if (!RegisterClassW(&wc))
    {
        EXPECT_EQ(GetLastError(), ERROR_CLASS_ALREADY_EXISTS);   // 重复运行忽略
    }

    out.hwnd = CreateWindowExW(0, className, title, WS_POPUP,
                               screenW - 210, kTopMargin, 200, 200,
                               nullptr, nullptr, wc.hInstance, nullptr);
    EXPECT_TRUE(out.hwnd != nullptr);
    if (out.hwnd == nullptr)
    {
        return false;
    }

    ShowWindow(out.hwnd, SW_SHOWNOACTIVATE);
    return true;
}

COLORREF ReadPixel(HWND hwnd, int x, int y)
{
    HDC dc = GetDC(hwnd);
    const COLORREF color = GetPixel(dc, x, y);
    ReleaseDC(hwnd, dc);
    return color;
}

bool IsBackgroundBlue(COLORREF c)
{
    return c != CLR_INVALID && GetRValue(c) <= 3 && GetGValue(c) <= 3 && GetBValue(c) >= 252;
}

bool IsPureRed(COLORREF c)
{
    return c != CLR_INVALID && GetRValue(c) >= 252 && GetGValue(c) <= 3 && GetBValue(c) <= 3;
}

/// @brief 绘制基线：200×200 蓝底 + 被测圆角矩形（每次独立 backend，避免状态串扰）
void DrawOneFrame(HWND hwnd, bool antiAliasing, const Rect& target, float radius,
                  const Color& color)
{
    GDIBackend backend;
    backend.Initialize(Win32RenderContext(hwnd));
    backend.SetAntiAliasing(antiAliasing);

    InvalidateRect(hwnd, nullptr, FALSE);   // 确保 BeginPaint 拿到有效 update region
    backend.BeginFrame();
    backend.DrawRect(Rect{ 0, 0, 200, 200 }, Color::Blue());
    backend.DrawRoundedRect(target, radius, color);
    backend.EndFrame();
}

/// @brief 捕获一帧：绘制后整帧逐像素读出（**单次，无重试**）
/// @details 索引顺序 = y 外层、x 内层（`index = y * 200 + x`）——诊断里的坐标还原依赖此约定。
/// @note 这里**刻意不做重试**：早期（2026-09-14）曾用「重试至整帧可读」去吸收所谓的「瞬时噪声」，
///       后经实测证明真因是 DWM 幽灵窗口（窗口长时间不取消息被系统替换）——重试既消不掉它，反而把
///       阻塞时长推过触发阈值、使偶发失败变成必然失败。修复已移至测试入口
///       （`test_main.cpp` 的 `DisableProcessWindowsGhosting()`），故此处恢复单次读取。
void CaptureFrame(HWND hwnd, bool antiAliasing, const Rect& target, std::vector<COLORREF>& out)
{
    DrawOneFrame(hwnd, antiAliasing, target, 0.0f, Color::Red());

    out.clear();
    out.reserve(200 * 200);

    HDC dc = GetDC(hwnd);
    for (int y = 0; y < 200; ++y)
    {
        for (int x = 0; x < 200; ++x)
        {
            out.push_back(GetPixel(dc, x, y));
        }
    }
    ReleaseDC(hwnd, dc);
}

/// @brief 失败诊断①：打印窗口可见区状态与窗口中心点处**真正**的那个窗口
/// @details 三件套：`IsWindowVisible`（窗口是否可见）· `GetClipBox`（**`NULLREGION=1` 即可见区为空**
///          ——典型成因是 DWM 幽灵窗口或窗口被最小化）· `WindowFromPoint` + `GetClassNameW`（点名中心处是谁）。
/// @warning 中心处是**别的窗口 ≠ 读取会失败**：实测 `SunAwtFrame`（Java 窗口）覆盖时测试仍全绿——DWM
///          redirection 让窗口表面照旧可读。真正的杀手只有 `clipType == NULLREGION`。
/// @note 仅在断言失败时调用——正常路径零输出。
void DumpWindowState(HWND hwnd, const char* where)
{
    HDC dc = GetDC(hwnd);

    RECT clip{};
    const int clipType = GetClipBox(dc, &clip);   // NULLREGION=1 / SIMPLEREGION=2 / COMPLEXREGION=3 / ERROR=0

    ReleaseDC(hwnd, dc);

    RECT wr{};
    GetWindowRect(hwnd, &wr);

    const POINT center{ (wr.left + wr.right) / 2, (wr.top + wr.bottom) / 2 };
    const HWND cover = WindowFromPoint(center);

    wchar_t cls[128] = L"";
    if (cover != nullptr)
    {
        GetClassNameW(cover, cls, 128);
    }

    std::printf("[AA/R0/%s] visible=%d clipType=%d clip=(%ld,%ld,%ld,%ld) 中心窗口=%p[%ls] self=%p rect=(%ld,%ld,%ld,%ld)\n",
                where, IsWindowVisible(hwnd) ? 1 : 0, clipType,
                clip.left, clip.top, clip.right, clip.bottom,
                static_cast<const void*>(cover), cls,
                static_cast<const void*>(hwnd),
                wr.left, wr.top, wr.right, wr.bottom);
}

/// @brief 失败诊断②：两帧差异概览 + 前 5 处不匹配坐标 + 窗口状态
/// @details 判读：① 差异**全是**「真实像素 vs `CLR_INVALID`」⇒ 属读取 / 可见性问题（窗口层面），
///          **不是**渲染差异；② 两帧**都有真实值**却不同 ⇒ 那才是真渲染差异（须查 `GDIBackend`）。
/// @note 仅在断言失败时调用。
void DumpFrameMismatch(HWND hwnd, const std::vector<COLORREF>& off, const std::vector<COLORREF>& on)
{
    std::size_t invalidOff = 0;
    std::size_t invalidOn = 0;
    std::size_t diffCount = 0;
    for (std::size_t i = 0; i < off.size(); ++i)
    {
        if (off[i] == CLR_INVALID) { ++invalidOff; }
    }
    for (std::size_t i = 0; i < on.size(); ++i)
    {
        if (on[i] == CLR_INVALID) { ++invalidOn; }
    }
    for (std::size_t i = 0; i < off.size() && i < on.size(); ++i)
    {
        if (off[i] != on[i]) { ++diffCount; }
    }

    std::printf("[AA/R0] diffCount=%zu / %zu   CLR_INVALID: off=%zu on=%zu\n",
                diffCount, off.size(), invalidOff, invalidOn);

    int shown = 0;
    for (std::size_t i = 0; i < off.size() && i < on.size() && shown < 5; ++i)
    {
        if (off[i] == on[i]) { continue; }

        const int px = static_cast<int>(i % 200);
        const int py = static_cast<int>(i / 200);
        std::printf("[AA/R0] #%d (%3d,%3d) off=%08lX%s on=%08lX%s\n",
                    shown, px, py,
                    static_cast<unsigned long>(off[i]),
                    off[i] == CLR_INVALID ? " [INVALID]" : "",
                    static_cast<unsigned long>(on[i]),
                    on[i] == CLR_INVALID ? " [INVALID]" : "");
        ++shown;
    }

    DumpWindowState(hwnd, "diff");
}

/// @brief AA 开/关在同一角区产生差异（详设 §6.2 L2-a）
void TestGDIAAOnOffDiffers()
{
    AAWindow win;
    if (!CreateAAWindow(L"ECDI_TestAA_OnOff", L"ECDI_AATest_OnOff", win))
    {
        return;
    }

    const Rect target{ 50, 50, 100, 100 };
    const int cornerX = 50;
    const int cornerY = 50;

    // 帧 1：AA 关（走 legacy —— GDI RoundRect 硬边）
    std::vector<COLORREF> offFrame;
    DrawOneFrame(win.hwnd, false, target, 8.0f, Color::Red());
    for (int y = 0; y < 8; ++y)
    {
        for (int x = 0; x < 8; ++x)
        {
            offFrame.push_back(ReadPixel(win.hwnd, cornerX + x, cornerY + y));
        }
    }

    // 帧 2：AA 开（清底重画——DrawOneFrame 内显式重铺不透明蓝底，
    //         确保帧 1 留在角区的部分覆盖像素被完全覆盖）
    std::vector<COLORREF> onFrame;
    DrawOneFrame(win.hwnd, true, target, 8.0f, Color::Red());
    for (int y = 0; y < 8; ++y)
    {
        for (int x = 0; x < 8; ++x)
        {
            onFrame.push_back(ReadPixel(win.hwnd, cornerX + x, cornerY + y));
        }
    }

    EXPECT_EQ(offFrame.size(), std::size_t(64));
    EXPECT_EQ(onFrame.size(), std::size_t(64));

    bool differs = false;
    for (std::size_t i = 0; i < offFrame.size() && i < onFrame.size(); ++i)
    {
        if (offFrame[i] == CLR_INVALID || onFrame[i] == CLR_INVALID)
        {
            continue;
        }
        if (offFrame[i] != onFrame[i])
        {
            differs = true;
            break;
        }
    }
    EXPECT_TRUE(differs);
}

/// @brief 过渡像素存在（详设 §6.2 L2-b）
void TestGDITransitionPixels()
{
    AAWindow win;
    if (!CreateAAWindow(L"ECDI_TestAA_Trans", L"ECDI_AATest_Trans", win))
    {
        return;
    }

    DrawOneFrame(win.hwnd, true, Rect{ 50, 50, 100, 100 }, 8.0f, Color::Red());

    // 左上角补丁区：既非纯红（完全覆盖）也非纯蓝（完全透明）→ 部分覆盖像素
    bool hasTransition = false;
    for (int y = 50; y < 58 && !hasTransition; ++y)
    {
        for (int x = 50; x < 58; ++x)
        {
            const COLORREF c = ReadPixel(win.hwnd, x, y);
            if (c == CLR_INVALID)
            {
                continue;
            }
            if (!IsPureRed(c) && !IsBackgroundBlue(c))
            {
                hasTransition = true;
                break;
            }
        }
    }
    EXPECT_TRUE(hasTransition);
}

/// @brief R == 0 时 AA 开/关逐像素一致（详设 §6.2 L2-c）——不引入 Golden Image
void TestGDIRadiusZeroBitwise()
{
    AAWindow win;
    if (!CreateAAWindow(L"ECDI_TestAA_R0", L"ECDI_AATest_R0", win))
    {
        return;
    }

    const Rect target{ 50, 50, 100, 100 };

    std::vector<COLORREF> offFrame;
    std::vector<COLORREF> onFrame;
    CaptureFrame(win.hwnd, false, target, offFrame);   // AA 关
    CaptureFrame(win.hwnd, true, target, onFrame);     // AA 开

    EXPECT_EQ(offFrame.size(), onFrame.size());

    std::size_t diffCount = 0;
    for (std::size_t i = 0; i < offFrame.size() && i < onFrame.size(); ++i)
    {
        if (offFrame[i] != onFrame[i])
        {
            ++diffCount;
        }
    }

    // 诊断只在失败路径付出成本——正常路径零输出（2026-09-14 收口：原「重试」与「AA-off 复查帧」已移除）
    if (diffCount > 0)
    {
        DumpFrameMismatch(win.hwnd, offFrame, onFrame);
    }

    EXPECT_EQ(diffCount, std::size_t(0));
}

/// @brief 预乘合成链路（详设 §6.2 L2-d）——刻意取**带内点**（覆盖度恒 1）以隔离 coverage 变量
void TestGDIPremultipliedBlend()
{
    AAWindow win;
    if (!CreateAAWindow(L"ECDI_TestAA_Blend", L"ECDI_AATest_Blend", win))
    {
        return;
    }

    DrawOneFrame(win.hwnd, true, Rect{ 50, 50, 100, 100 }, 8.0f,
                 Color::FromRGBA8(255, 0, 0, 128));

    // 带内点 (100, 60)：y = 60 ∈ [58, 142]（中带）→ 覆盖度恒 1
    const COLORREF c = ReadPixel(win.hwnd, 100, 60);
    EXPECT_TRUE(c != CLR_INVALID);
    EXPECT_TRUE(std::abs(GetRValue(c) - 128) <= 3);
    EXPECT_TRUE(GetGValue(c) <= 3);
    EXPECT_TRUE(std::abs(GetBValue(c) - 127) <= 3);
}

/// @brief 三形状装配：圆角矩形 / 胶囊 / 真圆（详设 §6.2 L2-e）
/// @details 后两者是退化压力用例——胶囊 `h == 2R`（中带零高跳过）、真圆 `w == h == 2R`
/// （三条带全跳过，四象限拼成整圆）。
void TestGDIShapeComposition()
{
    // e-1：普通圆角矩形 Rect{50,50,100,100} R=8 —— 包围盒角点被切除
    {
        AAWindow win;
        if (!CreateAAWindow(L"ECDI_TestAA_Shape1", L"ECDI_AATest_Shape1", win))
        {
            return;
        }
        DrawOneFrame(win.hwnd, true, Rect{ 50, 50, 100, 100 }, 8.0f, Color::Red());
        EXPECT_TRUE(IsBackgroundBlue(ReadPixel(win.hwnd, 51, 51)));
    }

    // e-2：胶囊 Rect{50,50,100,20} R=10（h == 2R）
    {
        AAWindow win;
        if (!CreateAAWindow(L"ECDI_TestAA_Shape2", L"ECDI_AATest_Shape2", win))
        {
            return;
        }
        DrawOneFrame(win.hwnd, true, Rect{ 50, 50, 100, 20 }, 10.0f, Color::Red());
        EXPECT_TRUE(IsBackgroundBlue(ReadPixel(win.hwnd, 51, 51)));   // 左半圆外 → 背景
        EXPECT_TRUE(IsPureRed(ReadPixel(win.hwnd, 51, 60)));          // 左半圆内 → 纯红
    }

    // e-3：真圆 Rect{50,50,20,20} R=10（w == h == 2R）
    {
        AAWindow win;
        if (!CreateAAWindow(L"ECDI_TestAA_Shape3", L"ECDI_AATest_Shape3", win))
        {
            return;
        }
        DrawOneFrame(win.hwnd, true, Rect{ 50, 50, 20, 20 }, 10.0f, Color::Red());
        EXPECT_TRUE(IsBackgroundBlue(ReadPixel(win.hwnd, 51, 51)));   // 包围盒角点 → 背景
        EXPECT_TRUE(IsPureRed(ReadPixel(win.hwnd, 60, 60)));          // 圆心 → 纯红
    }
}

} // anonymous namespace

void ECDI::Test::RegisterAntiAliasingTests()
{
    // L1：覆盖度掩码（零窗口依赖）
    GetTestRegistry().Add("AntiAliasing.MaskAreaConservation",   &TestMaskAreaConservation);
    GetTestRegistry().Add("AntiAliasing.MaskExactAnchors",       &TestMaskExactAnchors);
    GetTestRegistry().Add("AntiAliasing.MaskValueRange",         &TestMaskValueRange);
    GetTestRegistry().Add("AntiAliasing.MaskPartialCoverage",    &TestMaskPartialCoverage);
    GetTestRegistry().Add("AntiAliasing.MaskMonotonic",          &TestMaskMonotonic);
    GetTestRegistry().Add("AntiAliasing.MaskIndexTransform",     &TestMaskIndexTransform);
    GetTestRegistry().Add("AntiAliasing.MaskDeterminism",        &TestMaskDeterminism);
    GetTestRegistry().Add("AntiAliasing.CacheHit",               &TestCacheHit);
    GetTestRegistry().Add("AntiAliasing.CacheSetSamplesClears",  &TestCacheSetSamplesClears);
    GetTestRegistry().Add("AntiAliasing.MaskInvalidArgs",        &TestMaskInvalidArgs);

    // L2：GDIBackend 集成（真窗口像素读回）
    GetTestRegistry().Add("AntiAliasing.GDIAAOnOffDiffers",      &TestGDIAAOnOffDiffers);
    GetTestRegistry().Add("AntiAliasing.GDITransitionPixels",    &TestGDITransitionPixels);
    GetTestRegistry().Add("AntiAliasing.GDIRadiusZeroBitwise",   &TestGDIRadiusZeroBitwise);
    GetTestRegistry().Add("AntiAliasing.GDIPremultipliedBlend",  &TestGDIPremultipliedBlend);
    GetTestRegistry().Add("AntiAliasing.GDIShapeComposition",    &TestGDIShapeComposition);
}
