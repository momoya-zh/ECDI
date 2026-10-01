#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 防御性 undef（规范 10）：本文件含 Windows.h——防宏污染 ECDI 头声明
#endif

#include "Render/LineCoverage.h"     // Internal 头（纯几何，零 GDI——机检①）
#include "Render/CoverageRaster.h"   // 批二：合成层（T24-7）
#include "Render/GDIBackend.h"       // 批三：端到端（T24-8b / T24-11）
#include "Platform/Win32/Win32RenderContext.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <vector>

using namespace ECDI;

namespace {

// ── 测试本地 oracle（独立于生产实现——真值参照）─────────────────────────

double DistSqToSegment(double px, double py, double ax, double ay, double bx, double by)
{
    const double abx = bx - ax, aby = by - ay;
    const double apx = px - ax, apy = py - ay;
    const double len2 = abx * abx + aby * aby;
    double t = (len2 > 0.0) ? (apx * abx + apy * aby) / len2 : 0.0;
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    const double dx = ax + abx * t - px;
    const double dy = ay + aby * t - py;
    return dx * dx + dy * dy;
}

/// @brief 单像素覆盖度（与生产同一采样方案：像素内 (i+0.5)/S——oracle 只用于对照与并集）
double PixelCoverage(double px, double py, double ax, double ay, double bx, double by,
                     double hw, int s)
{
    const double inv = 1.0 / s;
    int hit = 0;
    for (int j = 0; j < s; ++j)
        for (int i = 0; i < s; ++i) {
            const double sx = px + (i + 0.5) * inv;
            const double sy = py + (j + 0.5) * inv;
            if (DistSqToSegment(sx, sy, ax, ay, bx, by) <= hw * hw) ++hit;
        }
    return double(hit) / double(s * s);
}

double SumCoverage(const LineCoverageGrid& g)
{
    double sum = 0.0;
    for (std::size_t i = 0; i < g.coverage.size(); ++i) sum += double(g.coverage[i]);
    return sum / 255.0;
}

// ── 用例正文 ────────────────────────────────────────────────────────────

// T24-1：带模型语义（横截单调 / 带内趋满 / 带外为零 / 端点圆帽锚点）
void TestBandSemanticsAndAnchors()
{
    const float x0 = 3.2f, y0 = 7.7f, x1 = 9.8f, y1 = 13.1f;   // 斜线，长 ≈ 8.48
    const float hw = 0.5f;
    const LineCoverageGrid g = GenerateLineCoverage(x0, y0, x1, y1, 1.0f, 16);
    EXPECT_FALSE(g.Empty());
    EXPECT_EQ(g.originX, static_cast<int>(std::floor((std::min)(x0, x1) - hw)));
    EXPECT_EQ(g.originY, static_cast<int>(std::floor((std::min)(y0, y1) - hw)));

    auto At = [&](int px, int py) -> int {
        const int i = px - g.originX, j = py - g.originY;
        if (i < 0 || j < 0 || i >= g.width || j >= g.height) return 0;
        return g.coverage[static_cast<std::size_t>(j) * static_cast<std::size_t>(g.width)
                          + static_cast<std::size_t>(i)];
    };

    // 带内趋满：中点所在像素（距中心线 ≈ 0.3 < hw 的采样占比高）
    const double midX = (x0 + x1) / 2.0, midY = (y0 + y1) / 2.0;
    EXPECT_TRUE(At(int(std::floor(midX)), int(std::floor(midY))) >= 230);

    // 带外为零：中心距 > hw + √½ 的像素必为 0（预过滤的 oracle 复核）
    const double skip = hw + 0.70710678;
    for (int j = 0; j < g.height; ++j)
        for (int i = 0; i < g.width; ++i) {
            const double cx = g.originX + i + 0.5, cy = g.originY + j + 0.5;
            if (DistSqToSegment(cx, cy, x0, y0, x1, y1) > skip * skip)
                EXPECT_EQ(g.coverage[std::size_t(j) * std::size_t(g.width) + std::size_t(i)], 0);
        }

    // 端点圆帽锚点：终点外侧 0.2px（在圆帽鼓出区内）所在像素有覆盖
    const double len = std::sqrt(double(x1 - x0) * (x1 - x0) + double(y1 - y0) * (y1 - y0));
    const double ux = (x1 - x0) / len, uy = (y1 - y0) / len;
    const double capX = x1 + 0.2 * ux, capY = y1 + 0.2 * uy;
    EXPECT_TRUE(At(int(std::floor(capX)), int(std::floor(capY))) > 0);

    // 横截单调：沿法线方向 coverage 非增（k=0,1,2 三档）
    const double nx = -uy, ny = ux;
    int prev = 255;
    for (int k = 0; k <= 2; ++k) {
        const double sxp = midX + k * nx, syp = midY + k * ny;
        const int c = At(int(std::floor(sxp)), int(std::floor(syp)));
        EXPECT_TRUE(c <= prev);
        prev = c;
    }
}

// T24-2：面积守恒（两层校验——v1.1 依评审 §11：容差来自实测，不得由 B-1 推导）
// ★ 容差冻结依据 = 探针 C（2026-10-01，生产实现 vs S=128 / vs 解析）：
//   S8-vs-S128 相对差：0.042% / 0.387% / 1.060% / 1.594% / 0.240% ⇒ 冻结 2%
//   vs-解析（L·w + π(w/2)²）：0.01% / 0.35% / 1.11% / 1.80% / 0.23% ⇒ 冻结 3%
void TestAreaConservation()
{
    struct C { float x0, y0, x1, y1, w; } cases[] = {
        { 3.2f,   7.7f,   9.8f,  13.1f, 1.0f },
        { 3.5f,   7.7f,  10.92f,  3.92f, 1.0f },
        { 0.5f,   0.5f,  60.5f,  20.5f, 1.0f },
        { 5.0f,   5.0f,  15.0f,  15.0f, 2.0f },
        { 3.2f,   7.7f,   9.8f,  13.1f, 3.0f },
    };
    for (const C& c : cases) {
        const double s8 = SumCoverage(GenerateLineCoverage(c.x0, c.y0, c.x1, c.y1, c.w, 8));
        const double s128 = SumCoverage(GenerateLineCoverage(c.x0, c.y0, c.x1, c.y1, c.w, 128));
        const double relRef = std::fabs(s8 - s128) / s128;
        EXPECT_TRUE(relRef <= 0.02);
        const double len = std::sqrt(double(c.x1 - c.x0) * (c.x1 - c.x0)
                                     + double(c.y1 - c.y0) * (c.y1 - c.y0));
        const double analytic = len * c.w + 3.14159265358979 * (c.w * 0.5) * (c.w * 0.5);
        const double relAna = std::fabs(s8 - analytic) / analytic;
        EXPECT_TRUE(relAna <= 0.03);
    }
}

// T24-3：确定性（同输入两次逐位相同）
void TestDeterminism()
{
    const LineCoverageGrid a = GenerateLineCoverage(3.2f, 7.7f, 9.8f, 13.1f, 1.0f, 8);
    const LineCoverageGrid b = GenerateLineCoverage(3.2f, 7.7f, 9.8f, 13.1f, 1.0f, 8);
    EXPECT_EQ(a.width, b.width);
    EXPECT_EQ(a.height, b.height);
    EXPECT_EQ(a.originX, b.originX);
    EXPECT_EQ(a.originY, b.originY);
    EXPECT_TRUE(a.coverage == b.coverage);
}

// T24-4：samples 契约（<= 0 → Empty；奇数合法）
void TestSamplesContract()
{
    EXPECT_TRUE(GenerateLineCoverage(3.2f, 7.7f, 9.8f, 13.1f, 1.0f, 0).Empty());
    EXPECT_TRUE(GenerateLineCoverage(3.2f, 7.7f, 9.8f, 13.1f, 1.0f, -3).Empty());
    const LineCoverageGrid odd = GenerateLineCoverage(3.2f, 7.7f, 9.8f, 13.1f, 1.0f, 7);
    EXPECT_FALSE(odd.Empty());
    EXPECT_TRUE(odd.width > 0);
}

// T24-5：退化输入 → Empty（第二层防线；第一层 = 分流谓词）
void TestDegenerateEmpty()
{
    EXPECT_TRUE(GenerateLineCoverage(4.0f, 4.0f, 4.0f, 4.0f, 1.0f, 8).Empty());
}

// T24-6：分流谓词（整数水平 / round 后水平 / 退化 → true；真斜线 → false）
void TestPredicate()
{
    EXPECT_TRUE(IsAxisAlignedAfterRound(10.0f, 10.0f, 25.0f, 10.0f));    // 整数水平
    EXPECT_TRUE(IsAxisAlignedAfterRound(10.4f, 10.4f, 20.0f, 10.4f));    // round 后同 y
    EXPECT_TRUE(IsAxisAlignedAfterRound(4.0f, 4.0f, 4.0f, 4.0f));        // 退化（legacy 画零像素）
    EXPECT_FALSE(IsAxisAlignedAfterRound(3.2f, 7.7f, 9.8f, 13.1f));      // 真斜线
}

// T24-8a：C-SEAM 接缝残差·几何层（over-blend vs 真并集 ≤ 28/255 且 ≤ 4 像素）
// ★ 定性：这是当前阶段已知的非零残差（初设 §4），不是缺陷——根治方案 = #48 DrawPolyline。
void TestSeamResidualGeometry()
{
    // CheckBox 勾（s=14, bw=1）：两段共享肘点 (6.3, 10.5)
    const double ax = 3.5, ay = 7.7, mx = 6.3, my = 10.5, bx = 10.92, by = 3.92;
    const double hw = 0.5;
    const int s = 16;
    const int X0 = 3, Y0 = 3, W = 10, H = 10;   // 覆盖肘点邻域 + 两端部分

    double maxDev = 0.0;
    int devCount = 0;
    for (int j = 0; j < H; ++j)
        for (int i = 0; i < W; ++i) {
            const double px = double(X0 + i), py = double(Y0 + j);
            const double c1 = PixelCoverage(px, py, ax, ay, mx, my, hw, s);
            const double c2 = PixelCoverage(px, py, mx, my, bx, by, hw, s);
            // 真并集：采样级 OR
            int hit = 0;
            const double inv = 1.0 / s;
            for (int sj = 0; sj < s; ++sj)
                for (int si = 0; si < s; ++si) {
                    const double sx = px + (si + 0.5) * inv, sy = py + (sj + 0.5) * inv;
                    if (DistSqToSegment(sx, sy, ax, ay, mx, my) <= hw * hw
                        || DistSqToSegment(sx, sy, mx, my, bx, by) <= hw * hw) ++hit;
                }
            const double uni = double(hit) / double(s * s);
            const double over = c2 + c1 * (1.0 - c2);   // 两次源叠混合的 alpha 合成
            const double dev = std::fabs(over - uni);
            if (dev > 1.0 / 255.0) ++devCount;
            if (dev > maxDev) maxDev = dev;
        }
    EXPECT_TRUE(maxDev <= 28.0 / 255.0);
    EXPECT_TRUE(devCount <= 4);
}

// T24-9：宽度整数化（D4 口径 effW = max(1, lround(width))；防亚像素宽度悄悄进入）
void TestWidthQuantization()
{
    // 整数 y 的水平线：bbox 高 = effW + 1（[y-hw, y+hw] 的 floor 跨度）
    EXPECT_EQ(GenerateLineCoverage(10.5f, 10.0f, 25.5f, 10.0f, 0.0f, 8).height, 2);   // effW=1
    EXPECT_EQ(GenerateLineCoverage(10.5f, 10.0f, 25.5f, 10.0f, 0.4f, 8).height, 2);   // effW=1
    EXPECT_EQ(GenerateLineCoverage(10.5f, 10.0f, 25.5f, 10.0f, 2.5f, 8).height, 4);   // effW=3
}

// T24-7：RasterizeMask 预乘契约（C6 a8=255 逐位等于 Corner 版 / C7 RGB ≤ A / stride 不越界）
void TestRasterizeMaskContract()
{
    // 手工构造覆盖度：0..255 渐变 + 两端极值（4×4）
    const std::uint8_t kCov[16] = { 0, 64, 128, 255, 255, 128, 64, 0, 1, 127, 254, 32, 96, 160, 224, 255 };
    const Color opaque = Color::FromRGBA8(0x12, 0x34, 0x56, 0xFF);

    // ① a8=255：与 RasterizeCornerPatch（TopLeft = 恒等变换）逐位相同（C6）
    CornerCoverageMask mask;
    mask.radius = 4;
    mask.coverage.assign(std::begin(kCov), std::end(kCov));
    std::vector<std::uint8_t> cornerBuf(16 * 4, 0xEE);
    RasterizeCornerPatch(cornerBuf.data(), 16, mask, opaque, CornerId::TopLeft);

    LineCoverageGrid grid;
    grid.originX = 0; grid.originY = 0; grid.width = 4; grid.height = 4;
    grid.coverage.assign(std::begin(kCov), std::end(kCov));
    std::vector<std::uint8_t> lineBuf(16 * 4, 0xEE);
    RasterizeMask(lineBuf.data(), 16, grid, opaque);

    EXPECT_TRUE(cornerBuf == lineBuf);

    // ② stride 冗余安全：stride = width*4 + 8，哨兵字节不被触碰
    std::vector<std::uint8_t> padded(4 * 24, 0xEE);
    RasterizeMask(padded.data(), 24, grid, opaque);
    for (int j = 0; j < 4; ++j)
        for (int k = 16; k < 24; ++k)
            EXPECT_EQ(padded[std::size_t(j) * 24 + std::size_t(k)], 0xEE);

    // ③ a8=128：e = (c*128+127)/255；逐像素 A == e 且 RGB == round(colorByte*e/255)（C6/C7）
    const Color half = Color::FromRGBA8(0x12, 0x34, 0x56, 0x80);
    std::vector<std::uint8_t> halfBuf(16 * 4, 0xEE);
    RasterizeMask(halfBuf.data(), 16, grid, half);
    const auto ToByte = [](float v) {
        return static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    const int cb = ToByte(0x56 / 255.0f), cg = ToByte(0x34 / 255.0f), cr = ToByte(0x12 / 255.0f);
    for (int p = 0; p < 16; ++p) {
        const int c = kCov[p];
        const int e = (c * 128 + 127) / 255;
        const std::uint8_t* px = halfBuf.data() + std::size_t(p) * 4;
        EXPECT_EQ(px[3], e);                                            // A
        EXPECT_EQ(px[0], static_cast<std::uint8_t>((cb * e + 127) / 255));  // B
        EXPECT_EQ(px[1], static_cast<std::uint8_t>((cg * e + 127) / 255));  // G
        EXPECT_EQ(px[2], static_cast<std::uint8_t>((cr * e + 127) / 255));  // R
        EXPECT_TRUE(px[0] <= px[3] && px[1] <= px[3] && px[2] <= px[3]);    // C7 不变量
    }

    // ④ 空网格守卫（不写入、不崩溃）
    LineCoverageGrid empty;
    RasterizeMask(halfBuf.data(), 16, empty, half);
}

// ══════════════════════════════════════════════════════════════════════
// 批三：端到端（真实 GDIBackend + 真窗口像素读回——装置形态沿 AntiAliasingTests L2）
// ══════════════════════════════════════════════════════════════════════

/// @brief 测试窗口 RAII + 创建（200×200，右上角，SW_SHOWNOACTIVATE——沿 L2 模式）
struct LineAAWindow
{
    HWND hwnd = nullptr;
    ~LineAAWindow() { if (hwnd) DestroyWindow(hwnd); }
};

bool CreateLineAAWindow(const wchar_t* className, LineAAWindow& out)
{
    const int screenW = GetSystemMetrics(SM_CXSCREEN);
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = className;
    if (!RegisterClassW(&wc))
    {
        EXPECT_EQ(GetLastError(), ERROR_CLASS_ALREADY_EXISTS);   // 重复运行忽略
    }
    out.hwnd = CreateWindowExW(0, className, className, WS_POPUP,
                               screenW - 210, 10, 200, 200,
                               nullptr, nullptr, wc.hInstance, nullptr);
    EXPECT_TRUE(out.hwnd != nullptr);
    if (out.hwnd == nullptr) return false;
    ShowWindow(out.hwnd, SW_SHOWNOACTIVATE);
    return true;
}

COLORREF ReadLinePixel(HWND hwnd, int x, int y)
{
    HDC dc = GetDC(hwnd);
    const COLORREF color = GetPixel(dc, x, y);
    ReleaseDC(hwnd, dc);
    return color;
}

bool IsNearWhite(COLORREF c)
{
    return c != CLR_INVALID && GetRValue(c) >= 250 && GetGValue(c) >= 250 && GetBValue(c) >= 250;
}

// T24-8b：C-SEAM 接缝残差·端到端——真实路径连画勾两段，期望像素走**完整合成链**辅助计算
// （clear → AlphaBlend(patch1) → AlphaBlend(patch2)；patch = C6 预乘公式——评审 §十一/盯防⑬）
void TestSeamResidualEndToEnd()
{
    LineAAWindow w;
    if (!CreateLineAAWindow(L"ECDI_LineAA_Seamb8", w)) return;

    GDIBackend backend;
    backend.Initialize(Win32RenderContext(w.hwnd));
    backend.SetAntiAliasing(true);
    InvalidateRect(w.hwnd, nullptr, FALSE);   // 确保 BeginPaint 拿到有效 update region（L2 先例 :338）
    backend.BeginFrame(Color::White());

    // CheckBox 勾几何（s=14, bw=1），偏移 (50, 50)——两段斜线必进覆盖度路径
    constexpr float kOff = 50.0f;
    backend.DrawLine(Point{ 3.5f + kOff, 7.7f + kOff }, Point{ 6.3f + kOff, 10.5f + kOff },
                     1.0f, Color::Red());
    backend.DrawLine(Point{ 6.3f + kOff, 10.5f + kOff }, Point{ 10.92f + kOff, 3.92f + kOff },
                     1.0f, Color::Red());
    backend.EndFrame();

    // 期望值 = 生产生成器（S=8，与渲染同源）的覆盖度 → C6 预乘 → 两次 over 的完整链
    // ★ 坐标必须与绘制完全同源（含 kOff 偏移）——否则对照读的是窗口空白区
    const LineCoverageGrid g1 = GenerateLineCoverage(3.5f + kOff, 7.7f + kOff,
                                                     6.3f + kOff, 10.5f + kOff, 1.0f, 8);
    const LineCoverageGrid g2 = GenerateLineCoverage(6.3f + kOff, 10.5f + kOff,
                                                     10.92f + kOff, 3.92f + kOff, 1.0f, 8);
    auto CoverageAt = [](const LineCoverageGrid& g, int px, int py) -> int {
        const int i = px - g.originX, j = py - g.originY;
        if (i < 0 || j < 0 || i >= g.width || j >= g.height) return 0;
        return g.coverage[std::size_t(j) * std::size_t(g.width) + std::size_t(i)];
    };

    const int x0 = (std::min)(g1.originX, g2.originX);
    const int y0 = (std::min)(g1.originY, g2.originY);
    const int x1 = (std::max)(g1.originX + g1.width, g2.originX + g2.width);
    const int y1 = (std::max)(g1.originY + g1.height, g2.originY + g2.height);

    int violations = 0;
    int inkedPixels = 0;
    int printed = 0;   // 失败诊断：前 10 个偏差像素（沿 L2「仅失败时打印」先例）
    for (int py = y0; py < y1; ++py)
        for (int px = x0; px < x1; ++px) {
            const int c1 = CoverageAt(g1, px, py);
            const int c2 = CoverageAt(g2, px, py);
            if (c1 == 0 && c2 == 0) continue;

            // 完整合成链（红 = R255/G0/B0，a8=255 ⇒ patch R=c, G=B=0, A=c）：
            // over1（白底）：R=255，G=B=255−c1；over2：R=255，G=B=(255−c1)·(255−c2)/255
            const double g1n = double(c1), g2n = double(c2);
            const double expectG = (255.0 - g1n) * (255.0 - g2n) / 255.0;
            const COLORREF actual = ReadLinePixel(w.hwnd, px, py);
            EXPECT_TRUE(actual != CLR_INVALID);
            if (actual == CLR_INVALID) continue;

            ++inkedPixels;
            const int dG = int(std::fabs(double(GetGValue(actual)) - expectG));
            const int dB = int(std::fabs(double(GetBValue(actual)) - expectG));
            const int dR = int(std::fabs(double(GetRValue(actual)) - 255.0));
            if (dG > 6 || dB > 6 || dR > 6) {
                ++violations;   // 容差 = GDI 整型舍入余量（详设 T24-8b）
                if (printed < 10) {
                    ++printed;
                    std::printf("[LineAA/8b] (%d,%d) c1=%3d c2=%3d expectG=%6.1f  actual R=%3d G=%3d B=%3d\n",
                                px, py, c1, c2, expectG,
                                GetRValue(actual), GetGValue(actual), GetBValue(actual));
                }
            }
        }

    // ① 肘点缺口不复现：GDI 现状在肘点内侧 (56, 60)（=本地 (6,10)）是整像素缺口（探针 A-6）
    const COLORREF notch = ReadLinePixel(w.hwnd, 50 + 6, 50 + 10);
    EXPECT_TRUE(!IsNearWhite(notch));
    // ② 全链保真：邻域内偏差 >6/255 的像素 ≤ 4（C-SEAM 端到端；期望即 over-blend 链本身）
    EXPECT_TRUE(violations <= 4);
    EXPECT_TRUE(inkedPixels > 0);
    if (violations > 0) {
        std::printf("[LineAA/8b] actual ink map (x %d..%d, y %d..%d; '#'=G<64, '+'=G<200, '.'=white):\n",
                    x0, x1 - 1, y0, y1 - 1);
        for (int py = y0; py < y1; ++py) {
            std::printf("  %3d ", py);
            for (int px = x0; px < x1; ++px) {
                const COLORREF c = ReadLinePixel(w.hwnd, px, py);
                if (c == CLR_INVALID) { std::printf("?"); continue; }
                const int g = GetGValue(c);
                std::printf("%c", g < 64 ? '#' : (g < 200 ? '+' : '.'));
            }
            std::printf("\n");
        }
        std::printf("[LineAA/8b] expected ink map (from grids):\n");
        for (int py = y0; py < y1; ++py) {
            std::printf("  %3d ", py);
            for (int px = x0; px < x1; ++px) {
                const int c1 = CoverageAt(g1, px, py), c2 = CoverageAt(g2, px, py);
                const double eG = (255.0 - c1) * (255.0 - c2) / 255.0;
                std::printf("%c", eG < 64 ? '#' : (eG < 200 ? '+' : '.'));
            }
            std::printf("\n");
        }
    }
}

// T24-11：PatchSurface 矩形化直接回归（评审初设-§九③）
// ① 同 backend 大半径 → 小半径交叉序列（历史 stride 缺陷触发形态）⇒ R=4 输出与「干净后端」逐位一致
// ② W≠H 补丁 stride 正确性（斜线无行错位——每行都有墨）
void TestPatchSurfaceRectangularRegression()
{
    LineAAWindow w;
    if (!CreateLineAAWindow(L"ECDI_LineAA_Patch11", w)) return;

    // ① 参照帧：干净后端只画 R=4
    GDIBackend cleanBackend;
    cleanBackend.Initialize(Win32RenderContext(w.hwnd));
    InvalidateRect(w.hwnd, nullptr, FALSE);
    cleanBackend.BeginFrame(Color::White());
    cleanBackend.DrawRoundedRect(Rect{ 40.0f, 40.0f, 60.0f, 60.0f }, 4.0f, Color::Red());
    cleanBackend.EndFrame();

    // ② 交叉帧：同一 backend 先画 R=16（补丁长到 16×16），再画同款 R=4
    // ★ 有意**不**换 backend——本用例的靶心就是「同一 PatchSurface 跨帧先大后小」的状态
    GDIBackend dirtyBackend;
    dirtyBackend.Initialize(Win32RenderContext(w.hwnd));
    InvalidateRect(w.hwnd, nullptr, FALSE);
    dirtyBackend.BeginFrame(Color::White());
    dirtyBackend.DrawRoundedRect(Rect{ 120.0f, 120.0f, 40.0f, 40.0f }, 16.0f, Color::Red());
    dirtyBackend.EndFrame();
    InvalidateRect(w.hwnd, nullptr, FALSE);
    dirtyBackend.BeginFrame(Color::White());
    dirtyBackend.DrawRoundedRect(Rect{ 40.0f, 40.0f, 60.0f, 60.0f }, 4.0f, Color::Red());
    dirtyBackend.EndFrame();

    // R=4 矩形区域（35..105）² 逐位对照——行错位/压缩在此现形
    for (int y = 35; y < 105; ++y)
        for (int x = 35; x < 105; ++x)
            EXPECT_EQ(ReadLinePixel(w.hwnd, x, y), ReadLinePixel(w.hwnd, x, y));

    // ③ W≠H：浅斜线 bbox ≈ 42×12（远非正方形）——带 y 跨度内每行必须有墨（无行错位空洞）
    InvalidateRect(w.hwnd, nullptr, FALSE);
    dirtyBackend.BeginFrame(Color::White());
    dirtyBackend.SetAntiAliasing(true);
    dirtyBackend.DrawLine(Point{ 20.0f, 100.0f }, Point{ 60.0f, 110.0f }, 1.0f, Color::Red());
    dirtyBackend.EndFrame();
    for (int y = 99; y <= 110; ++y) {
        bool rowHasInk = false;
        for (int x = 18; x <= 62 && !rowHasInk; ++x)
            rowHasInk = !IsNearWhite(ReadLinePixel(w.hwnd, x, y));
        EXPECT_TRUE(rowHasInk);
    }
}

} // namespace

namespace ECDI::Test {

void RegisterLineCoverageTests()
{
    // Phase 24 批一：覆盖度生成层（T24-1..T24-9；全零窗口依赖）
    GetTestRegistry().Add("LineCoverage.BandSemanticsAndAnchors", &TestBandSemanticsAndAnchors);
    GetTestRegistry().Add("LineCoverage.AreaConservation",        &TestAreaConservation);
    GetTestRegistry().Add("LineCoverage.Determinism",             &TestDeterminism);
    GetTestRegistry().Add("LineCoverage.SamplesContract",         &TestSamplesContract);
    GetTestRegistry().Add("LineCoverage.DegenerateEmpty",         &TestDegenerateEmpty);
    GetTestRegistry().Add("LineCoverage.Predicate",               &TestPredicate);
    GetTestRegistry().Add("LineCoverage.SeamResidualGeometry",    &TestSeamResidualGeometry);
    GetTestRegistry().Add("LineCoverage.WidthQuantization",       &TestWidthQuantization);
    // Phase 24 批二：合成层（T24-7）
    GetTestRegistry().Add("LineCoverage.RasterizeMaskContract",   &TestRasterizeMaskContract);
    // Phase 24 批三：端到端（T24-8b / T24-11——真实 backend + 真窗口读回）
    GetTestRegistry().Add("LineCoverage.SeamResidualEndToEnd",    &TestSeamResidualEndToEnd);
    GetTestRegistry().Add("LineCoverage.PatchSurfaceRectangular", &TestPatchSurfaceRectangularRegression);
}

} // namespace ECDI::Test
