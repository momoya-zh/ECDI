#include "RunAllTests.h"
#include "TestFramework.h"

#include "Render/ArcCoverage.h"        // Internal 头（纯几何、零 GDI、零窗口）
#include "Render/BackendContract.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <utility>
#include <vector>

using namespace ECDI;

namespace {

constexpr double kPiD = 3.14159265358979323846;

// ══════════════════════════════════════════════════════════════════════
// 测试本地 oracle（独立于生产实现——真值参照）
// ══════════════════════════════════════════════════════════════════════
// ★★ 刻意用**另一种算法**：在弧上密采样、取最小距离，**完全不用角度投影**。
//    若 oracle 与生产同写「投影 + 钳制」，同一个理解错误会被复制两遍 ⇒ oracle 失效。
double RefDistSqToArc(double px, double py, double cx, double cy,
                      double radius, double startDeg, double sweepDeg, int n = 4001)
{
    double best = 1e300;
    for (int k = 0; k < n; ++k)
    {
        const double s = sweepDeg * double(k) / double(n - 1);   // 有向：负扫角自然覆盖
        const double ang = (startDeg + s) * (kPiD / 180.0);
        const double qx = cx + radius * std::cos(ang);
        const double qy = cy + radius * std::sin(ang);
        const double dx = px - qx;
        const double dy = py - qy;
        best = (std::min)(best, dx * dx + dy * dy);
    }
    return best;
}

double SumCoverage(const LineCoverageGrid& g)
{
    double sum = 0.0;
    for (std::size_t i = 0; i < g.coverage.size(); ++i)
    {
        sum += double(g.coverage[i]);
    }
    return sum / 255.0;
}

int CoverageAt(const LineCoverageGrid& g, int px, int py)
{
    const int i = px - g.originX;
    const int j = py - g.originY;
    if (i < 0 || j < 0 || i >= g.width || j >= g.height)
    {
        return 0;
    }
    return g.coverage[static_cast<std::size_t>(j) * static_cast<std::size_t>(g.width)
                      + static_cast<std::size_t>(i)];
}

/// @brief 圆上某角度处的点所在像素的覆盖度
int CoverageAtAngle(const LineCoverageGrid& g, double cx, double cy, double radius, double deg)
{
    const double ang = deg * (kPiD / 180.0);
    const double px = cx + radius * std::cos(ang);
    const double py = cy + radius * std::sin(ang);
    return CoverageAt(g, static_cast<int>(std::floor(px)), static_cast<int>(std::floor(py)));
}

/// @brief 两点在圆上的**弦距**对应的圆心角（度）——用于构造「距端点恰好 d 像素」的探针
double ChordAngleDeg(double d, double radius)
{
    const double half = (std::min)(d / (2.0 * radius), 1.0);
    return 2.0 * std::asin(half) * 180.0 / kPiD;
}

// ══════════════════════════════════════════════════════════════════════
// T33-1：值域与网格完整性（C33-8 的 bbox 契约）
// ══════════════════════════════════════════════════════════════════════
void Test33_1CoverageValueRangeAndBounds()
{
    const float cx = 20.5f, cy = 20.5f, radius = 10.0f;
    const float start = 30.0f, sweep = 120.0f, width = 3.0f;
    const LineCoverageGrid g = GenerateArcCoverage(cx, cy, radius, start, sweep, width, 8);
    EXPECT_FALSE(g.Empty());

    // bbox 公式：originX == floor(cx - pad)；width == floor(cx + pad) - originX + 1
    const BackendContract::ArcParams p =
        BackendContract::NormalizeArc(cx, cy, radius, start, sweep, width);
    EXPECT_TRUE(p.drawable);
    const float pad = p.radius + p.halfWidth + 1.0f;
    EXPECT_EQ(g.originX, static_cast<int>(std::floor(double(cx) - double(pad))));
    EXPECT_EQ(g.originY, static_cast<int>(std::floor(double(cy) - double(pad))));
    EXPECT_EQ(g.width, static_cast<int>(std::floor(double(cx) + double(pad))) - g.originX + 1);
    EXPECT_EQ(g.height, static_cast<int>(std::floor(double(cy) + double(pad))) - g.originY + 1);
    EXPECT_EQ(g.coverage.size(), std::size_t(g.width) * std::size_t(g.height));

    // 值域两端都真的出现过（`uint8_t` 的上界是结构性的，但「全 0」与「全 255」是两种真缺陷）
    int lo = 255, hi = 0;
    for (std::size_t i = 0; i < g.coverage.size(); ++i)
    {
        const int v = static_cast<int>(g.coverage[i]);
        lo = (std::min)(lo, v);
        hi = (std::max)(hi, v);
    }
    EXPECT_EQ(lo, 0);      // bbox 是保守整圆 ⇒ 四角必在带外
    EXPECT_EQ(hi, 255);    // 弧中心线上必有整格带内
}

// ══════════════════════════════════════════════════════════════════════
// T33-2：面积守恒·四档解析参照（C33-5）
// ══════════════════════════════════════════════════════════════════════
// ★ `tol` 由本轮实测冻结（见用例内 printf 的四个相对误差），**不由公式推导**
//   ——沿 Phase 24 的 `relRef <= 0.02 / relAna <= 0.03` 形态。
constexpr double kArcAreaTol = 0.005;   // 0.5%  —— T33-2 档（r=100，网格 ~201x201）

/// @brief 扇形退化档的容差（★ 实测冻结——比 T33-2 宽，理由是**网格更小**）
/// @details 依据 = 本轮实测（samples 8 → 64 的收敛序列，`Arc.FanDegenerate`）：
///          `r=4`：`0.2910% → 0.1154% → -0.0250% → 0.0023%`
///          `r=2`：`0.7191% → 0.3290% → 0.0637% → -0.0611%`
///          ⇒ 残差**随采样密度单调收敛**（格点量化误差的指纹，几何错不会这样）
///          ⇒ 冻结 `1.5%`（实测最差 `0.72%` 的 ~2 倍余量）。
///          ★ 判别力仍在：v1.0 那个「扇形」式在此档**恰为真值的 0.500×**（差 50%），
///          本容差对它有 **33×** 的余量 ⇒ 公式一旦退回去，用例必红。
constexpr double kArcFanAreaTol = 0.015;   // 1.5%

void Test33_2AreaConservationFourTiers()
{
    const float cx = 100.5f, cy = 100.5f, radius = 100.0f, width = 10.0f;
    const double r = 100.0, hw = 5.0;   // effW = lround(10) = 10 ⇒ hw = min(5, 100) = 5
    const double sweeps[4] = { 90.0, 180.0, 270.0, 360.0 };

    for (int k = 0; k < 4; ++k)
    {
        const double sw = sweeps[k];
        const LineCoverageGrid g =
            GenerateArcCoverage(cx, cy, radius, 0.0f, static_cast<float>(sw), width, 8);
        EXPECT_FALSE(g.Empty());

        const double measured = SumCoverage(g);
        double analytic = 0.0;
        if (sw >= 360.0)
        {
            // 整圆：弧段闭合、无端点 ⇒ 圆帽不追加 ⇒ 环带
            analytic = kPiD * ((r + hw) * (r + hw) - (r - hw) * (r - hw));
        }
        else
        {
            analytic = 2.0 * r * hw * (sw * kPiD / 180.0) + kPiD * hw * hw;
        }
        const double rel = std::fabs(measured - analytic) / analytic;
        std::printf("[T33-2] |sweep|=%.0f measured=%.3f analytic=%.3f rel=%.5f%%\n",
                    sw, measured, analytic, rel * 100.0);
        EXPECT_TRUE(rel <= kArcAreaTol);
        // 反例锚：整圆档精确等于 4*pi*r*hw（**不是** 2*pi*r*hw——后者差 2×）
        if (sw >= 360.0)
        {
            EXPECT_NEAR(analytic, 4.0 * kPiD * r * hw, 1e-6);
        }
    }
}

// ══════════════════════════════════════════════════════════════════════
// T33-3：镜像对称 + 同弧别名（★★ 起点侧/终点侧吸附不对称的回归锚）
// ══════════════════════════════════════════════════════════════════════
// ★ 中心取**半整数** ⇒ bbox 关于圆心严格对称 ⇒ 行翻转是格到格的双射
void Test33_3MirrorSymmetryAndSameArcAliasing()
{
    const float cx = 20.5f, cy = 20.5f, radius = 10.0f, width = 4.0f;   // hw = 2

    const LineCoverageGrid a = GenerateArcCoverage(cx, cy, radius, 0.0f, 90.0f, width, 8);
    const LineCoverageGrid mirror = GenerateArcCoverage(cx, cy, radius, 270.0f, 90.0f, width, 8);
    const LineCoverageGrid sameArcNeg = GenerateArcCoverage(cx, cy, radius, 0.0f, -90.0f, width, 8);
    const LineCoverageGrid sameArc180 = GenerateArcCoverage(cx, cy, radius, -90.0f, 90.0f, width, 8);
    const LineCoverageGrid rot180 = GenerateArcCoverage(cx, cy, radius, 270.0f, -90.0f, width, 8);

    EXPECT_EQ(a.width, mirror.width);
    EXPECT_EQ(a.height, mirror.height);
    EXPECT_EQ(a.originX, mirror.originX);
    EXPECT_EQ(a.originY, mirror.originY);

    // ① 镜像对：(0,+90) 与 (270,+90) 关于水平直径互为镜像 ⇒ 行翻转后逐像素相等
    int mirrorDiff = 0;
    for (int j = 0; j < a.height; ++j)
    {
        for (int i = 0; i < a.width; ++i)
        {
            const std::size_t src = static_cast<std::size_t>(a.height - 1 - j)
                                    * static_cast<std::size_t>(a.width) + static_cast<std::size_t>(i);
            const std::size_t dst = static_cast<std::size_t>(j)
                                    * static_cast<std::size_t>(a.width) + static_cast<std::size_t>(i);
            if (a.coverage[src] != mirror.coverage[dst])
            {
                ++mirrorDiff;
            }
        }
    }
    std::printf("[T33-3] mirror-pair differing pixels = %d\n", mirrorDiff);
    EXPECT_EQ(mirrorDiff, 0);

    // ①-bis 反例锚：(270,-90) 是 **180° 旋转**、不是镜像 ⇒ 镜像比对必然**不同**
    int notMirrorDiff = 0;
    for (int j = 0; j < a.height; ++j)
    {
        for (int i = 0; i < a.width; ++i)
        {
            const std::size_t src = static_cast<std::size_t>(a.height - 1 - j)
                                    * static_cast<std::size_t>(a.width) + static_cast<std::size_t>(i);
            const std::size_t dst = static_cast<std::size_t>(j)
                                    * static_cast<std::size_t>(a.width) + static_cast<std::size_t>(i);
            if (a.coverage[src] != rot180.coverage[dst])
            {
                ++notMirrorDiff;
            }
        }
    }
    EXPECT_TRUE(notMirrorDiff > 100);   // 旧稿把它当镜像 ⇒ 这条断言证明「不是」

    // ② 同一弧段的三种写法必须逐位相同（旧规格下 (270,+90) 与 (0,-90) 相差数十格）
    int aliasDiff1 = 0, aliasDiff2 = 0;
    for (std::size_t i = 0; i < a.coverage.size(); ++i)
    {
        if (mirror.coverage[i] != sameArcNeg.coverage[i]) { ++aliasDiff1; }
        if (mirror.coverage[i] != sameArc180.coverage[i]) { ++aliasDiff2; }
    }
    std::printf("[T33-3] alias differing pixels: (270,+90)vs(0,-90)=%d  vs(-90,+90)=%d\n",
                aliasDiff1, aliasDiff2);
    EXPECT_EQ(aliasDiff1, 0);
    EXPECT_EQ(aliasDiff2, 0);
}

// ══════════════════════════════════════════════════════════════════════
// T33-4：跨 0° 的同弧别名（C33-3①）
// ══════════════════════════════════════════════════════════════════════
void Test33_4CrossZeroAliasing()
{
    const float cx = 20.5f, cy = 20.5f, radius = 10.0f, width = 4.0f;

    const LineCoverageGrid p350 = GenerateArcCoverage(cx, cy, radius, 350.0f, 20.0f, width, 8);
    const LineCoverageGrid m10 = GenerateArcCoverage(cx, cy, radius, -10.0f, 20.0f, width, 8);
    const LineCoverageGrid p10 = GenerateArcCoverage(cx, cy, radius, 10.0f, 20.0f, width, 8);
    EXPECT_FALSE(p350.Empty());

    // ① 同一弧段的两种写法逐位相同
    int sameDiff = 0, otherDiff = 0;
    for (std::size_t i = 0; i < p350.coverage.size(); ++i)
    {
        if (p350.coverage[i] != m10.coverage[i]) { ++sameDiff; }
        if (p350.coverage[i] != p10.coverage[i]) { ++otherDiff; }
    }
    std::printf("[T33-4] (350,+20)vs(-10,+20)=%d  (350,+20)vs(10,+20)=%d\n", sameDiff, otherDiff);
    EXPECT_EQ(sameDiff, 0);
    EXPECT_TRUE(otherDiff > 20);   // 反例锚：旧稿的配对**不是**同一弧段

    // ② 跨 0° 未被截断：350°–360° 与 0°–10° 两段都真的有覆盖
    EXPECT_TRUE(CoverageAtAngle(p350, cx, cy, radius, 355.0) > 0);
    EXPECT_TRUE(CoverageAtAngle(p350, cx, cy, radius, 5.0) > 0);
}

// ══════════════════════════════════════════════════════════════════════
// T33-5：整圆接缝与符号（C33-3②③）
// ══════════════════════════════════════════════════════════════════════
void Test33_5FullCircleSeamAndSign()
{
    const float cx = 20.5f, cy = 20.5f, radius = 10.0f, width = 4.0f;

    const LineCoverageGrid pos = GenerateArcCoverage(cx, cy, radius, 0.0f, 360.0f, width, 8);
    const LineCoverageGrid neg = GenerateArcCoverage(cx, cy, radius, 0.0f, -360.0f, width, 8);
    EXPECT_FALSE(pos.Empty());

    // ① 0° 邻域无零覆盖率列（整圆不得在接缝处漏一列）
    for (double deg = 0.0; deg < 360.0; deg += 5.0)
    {
        EXPECT_TRUE(CoverageAtAngle(pos, cx, cy, radius, deg) > 0);
    }

    // ② +360 与 -360 逐位相同（C33-3③）
    int diff = 0;
    for (std::size_t i = 0; i < pos.coverage.size(); ++i)
    {
        if (pos.coverage[i] != neg.coverage[i]) { ++diff; }
    }
    std::printf("[T33-5] (+360)vs(-360) differing pixels = %d\n", diff);
    EXPECT_EQ(diff, 0);

    // ③ 与「到整圆距离 <= hw」的独立 oracle 逐像素一致（判据退化的机制证明）
    int oracleDiff = 0;
    for (int j = 0; j < pos.height; ++j)
    {
        for (int i = 0; i < pos.width; ++i)
        {
            const double px = pos.originX + i + 0.5;
            const double py = pos.originY + j + 0.5;
            const double radial = std::sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
            const bool oracleIn = std::fabs(radial - radius) <= 2.0;
            const bool gridFull = pos.coverage[static_cast<std::size_t>(j)
                                               * static_cast<std::size_t>(pos.width)
                                               + static_cast<std::size_t>(i)] == 255;
            // 只对「整格在带内」的像素做严格断言（部分覆盖的边界像素两类判据本就不同）
            if (oracleIn && std::fabs(std::fabs(radial - radius) - 2.0) > 0.70710678)
            {
                if (!gridFull) { ++oracleDiff; }
            }
        }
    }
    EXPECT_EQ(oracleDiff, 0);
}

// ══════════════════════════════════════════════════════════════════════
// T33-6：圆帽判别点——★★ 两端各一（起点侧正是旧规格丢失的那一半）
// ══════════════════════════════════════════════════════════════════════
void Test33_6RoundCapDiscriminantBothEnds()
{
    const double cx = 20.5, cy = 20.5, radius = 10.0, hw = 2.0;
    const LineCoverageGrid g = GenerateArcCoverage(static_cast<float>(cx), static_cast<float>(cy),
                                                   static_cast<float>(radius),
                                                   0.0f, 90.0f, static_cast<float>(hw * 2.0), 8);
    EXPECT_FALSE(g.Empty());

    const double insideD = hw * 0.5;        // 距端点 1.0 px ⇒ 必在圆帽内
    const double outsideD = hw + 1.0;       // 距端点 3.0 px ⇒ 必在圆帽外
    const double aIn = ChordAngleDeg(insideD, radius);
    const double aOut = ChordAngleDeg(outsideD, radius);

    // 起点侧（角度 < 0）：旧规格正是在这里返回「到终点」的距离 ⇒ 覆盖率恒 0
    EXPECT_TRUE(CoverageAtAngle(g, cx, cy, radius, -aIn) > 0);
    EXPECT_EQ(CoverageAtAngle(g, cx, cy, radius, -aOut), 0);

    // 终点侧（角度 > 90）
    EXPECT_TRUE(CoverageAtAngle(g, cx, cy, radius, 90.0 + aIn) > 0);
    EXPECT_EQ(CoverageAtAngle(g, cx, cy, radius, 90.0 + aOut), 0);

    // 判别性：这两个探针点在「径向带 ∩ 扇区」之外 —— 用 oracle 证明它们只可能被圆帽覆盖
    const double angIn = -aIn * (kPiD / 180.0);
    const double pxIn = cx + radius * std::cos(angIn);
    const double pyIn = cy + radius * std::sin(angIn);
    EXPECT_TRUE(std::fabs(std::sqrt((pxIn - cx) * (pxIn - cx) + (pyIn - cy) * (pyIn - cy)) - radius)
                < 1e-9);   // 在圆周上 ⇒ 半径带内
    EXPECT_TRUE(pyIn < cy);    // 但在扇区**之外**（0..90 度在 y >= cy 一侧）
}

// ══════════════════════════════════════════════════════════════════════
// T33-7：退化全表 + 超一周钳制（C33-4）
// ══════════════════════════════════════════════════════════════════════
void Test33_7DegenerateEmptyTable()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    EXPECT_TRUE(GenerateArcCoverage(10.0f, 10.0f, 0.0f, 0.0f, 90.0f, 2.0f).Empty());    // radius <= 0
    EXPECT_TRUE(GenerateArcCoverage(10.0f, 10.0f, -5.0f, 0.0f, 90.0f, 2.0f).Empty());   // radius < 0
    EXPECT_TRUE(GenerateArcCoverage(10.0f, 10.0f, 10.0f, 0.0f, 90.0f, 0.0f).Empty());   // width <= 0
    EXPECT_TRUE(GenerateArcCoverage(10.0f, 10.0f, 10.0f, 0.0f, 0.0f, 2.0f).Empty());    // sweep == 0
    EXPECT_TRUE(GenerateArcCoverage(10.0f, 10.0f, 10.0f, 0.0f, nan, 2.0f).Empty());     // NaN
    EXPECT_TRUE(GenerateArcCoverage(10.0f, 10.0f, 10.0f, 0.0f, inf, 2.0f).Empty());     // +Inf
    EXPECT_TRUE(GenerateArcCoverage(10.0f, 10.0f, 10.0f, 0.0f, -inf, 2.0f).Empty());    // -Inf
    EXPECT_TRUE(GenerateArcCoverage(nan, 10.0f, 10.0f, 0.0f, 90.0f, 2.0f).Empty());     // 圆心 NaN
    EXPECT_TRUE(GenerateArcCoverage(10.0f, 10.0f, 10.0f, 0.0f, 90.0f, 2.0f, 0).Empty());  // samples <= 0

    // |sweep| = 450 ⇒ 钳到 360 ⇒ 与 360 档**逐位相同**
    const LineCoverageGrid clamped =
        GenerateArcCoverage(20.5f, 20.5f, 10.0f, 0.0f, 450.0f, 4.0f, 8);
    const LineCoverageGrid full =
        GenerateArcCoverage(20.5f, 20.5f, 10.0f, 0.0f, 360.0f, 4.0f, 8);
    const LineCoverageGrid clampedNeg =
        GenerateArcCoverage(20.5f, 20.5f, 10.0f, 0.0f, -450.0f, 4.0f, 8);
    const LineCoverageGrid fullNeg =
        GenerateArcCoverage(20.5f, 20.5f, 10.0f, 0.0f, -360.0f, 4.0f, 8);
    EXPECT_FALSE(clamped.Empty());
    int d1 = 0, d2 = 0;
    for (std::size_t i = 0; i < clamped.coverage.size(); ++i)
    {
        if (clamped.coverage[i] != full.coverage[i]) { ++d1; }
        if (clampedNeg.coverage[i] != fullNeg.coverage[i]) { ++d2; }
    }
    EXPECT_EQ(d1, 0);
    EXPECT_EQ(d2, 0);
}

// ══════════════════════════════════════════════════════════════════════
// T33-7b：扇形退化（hw == radius）——★★ 与常规档**共用同一面积合式**
// ══════════════════════════════════════════════════════════════════════
void Test33_7bFanDegenerate()
{
    // radius=4, width=10 ⇒ effW=10 ⇒ hw = min(5, 4) = 4（由 radius 钳制，不是 5）
    const BackendContract::ArcParams p =
        BackendContract::NormalizeArc(10.0f, 10.0f, 4.0f, 0.0f, 90.0f, 10.0f);
    EXPECT_TRUE(p.drawable);
    EXPECT_NEAR(p.halfWidth, 4.0, 1e-6);

    // ① 几何归属：圆心到弧段的距离恒等于 radius ⇒ radius == hw 时**恰在带内**
    EXPECT_NEAR(BackendContract::ArcSegmentDistanceSq(p, 10.0f, 10.0f), 16.0, 1e-4);

    // ② 面积按 C33-5 的合式（退化档**不另立公式**）
    struct FanCase { float radius; float hw; float sweep; double expect; };
    const FanCase cases[] = {
        { 4.0f, 4.0f,  90.0f,  100.531 },   // 旧稿「扇形」式给 50.265 —— 恰为真值的 0.500×
        { 4.0f, 4.0f, 360.0f,  201.062 },
        { 2.0f, 2.0f,  90.0f,   25.133 },
        { 2.0f, 2.0f, 360.0f,   50.265 },
    };
    for (const FanCase& c : cases)
    {
        const LineCoverageGrid g = GenerateArcCoverage(20.5f, 20.5f, c.radius, 0.0f, c.sweep,
                                                       c.hw * 2.0f, 8);
        EXPECT_FALSE(g.Empty());
        const double got = SumCoverage(g);
        std::printf("[T33-7b] r=%.0f hw=%.0f sweep=%.0f got=%.4f expect=%.4f rel=%.4f%% grid=%dx%d\n",
                    double(c.radius), double(c.hw), double(c.sweep), got, c.expect,
                    (got - c.expect) / c.expect * 100.0, g.width, g.height);
        EXPECT_NEAR(got, c.expect, c.expect * kArcFanAreaTol);
    }
}

// ══════════════════════════════════════════════════════════════════════
// T33-8：确定性
// ══════════════════════════════════════════════════════════════════════
void Test33_8Determinism()
{
    const LineCoverageGrid a = GenerateArcCoverage(13.7f, 21.3f, 9.25f, 17.5f, -123.0f, 3.0f, 8);
    const LineCoverageGrid b = GenerateArcCoverage(13.7f, 21.3f, 9.25f, 17.5f, -123.0f, 3.0f, 8);
    EXPECT_FALSE(a.Empty());
    EXPECT_EQ(a.originX, b.originX);
    EXPECT_EQ(a.originY, b.originY);
    EXPECT_EQ(a.width, b.width);
    EXPECT_EQ(a.height, b.height);
    EXPECT_EQ(a.coverage.size(), b.coverage.size());
    int diff = 0;
    for (std::size_t i = 0; i < a.coverage.size() && i < b.coverage.size(); ++i)
    {
        if (a.coverage[i] != b.coverage[i]) { ++diff; }
    }
    EXPECT_EQ(diff, 0);
}

// ══════════════════════════════════════════════════════════════════════
// T33-9：`ArcSegmentDistanceSq` 真值表（★ 测的就是生产判据）
// ══════════════════════════════════════════════════════════════════════
void Test33_9ArcSegmentDistanceTruthTable()
{
    const float cx = 20.5f, cy = 20.5f, radius = 10.0f;
    const double r = 10.0;
    const double cxd = 20.5, cyd = 20.5;

    auto P = [&](double deg, double rad) {
        const double a = deg * (kPiD / 180.0);
        return std::pair<float, float>(static_cast<float>(cxd + rad * std::cos(a)),
                                       static_cast<float>(cyd + rad * std::sin(a)));
    };

    // ① 起点归属：点恰在起始角 ⇒ dist² ≈ 0（+90 与 -90 **都**成立——负扫角回归锚）
    for (float sweep : { 90.0f, -90.0f })
    {
        for (float start : { 0.0f, 37.5f, 123.0f, 350.0f })
        {
            const BackendContract::ArcParams p =
                BackendContract::NormalizeArc(cx, cy, radius, start, sweep, 4.0f);
            const double a = double(start) * (kPiD / 180.0);
            const float px = static_cast<float>(cxd + r * std::cos(a));
            const float py = static_cast<float>(cyd + r * std::sin(a));
            const float d2 = BackendContract::ArcSegmentDistanceSq(p, px, py);
            EXPECT_TRUE(d2 < 1e-3f);
        }
    }

    // ② 中心线往返：弧上 4001 个参数点 ⇒ dist² < 1e-6（**两处负号成对**的直接判据）
    {
        const BackendContract::ArcParams p =
            BackendContract::NormalizeArc(cx, cy, radius, 30.0f, 120.0f, 4.0f);
        float worst = 0.0f;
        for (int k = 0; k <= 4000; ++k)
        {
            const double t = double(k) / 4000.0;
            const double ang = (30.0 + 120.0 * t) * (kPiD / 180.0);
            const float px = static_cast<float>(cxd + r * std::cos(ang));
            const float py = static_cast<float>(cyd + r * std::sin(ang));
            worst = (std::max)(worst, BackendContract::ArcSegmentDistanceSq(p, px, py));
        }
        std::printf("[T33-9] centreline round-trip worst dist^2 = %.9f px^2\n", double(worst));
        EXPECT_TRUE(worst < 1e-4f);
    }

    // ③ 越界吸附：弧外点必须等于「到圆周角差更小的端点」的距离平方（**起点侧圆帽回归锚**）
    {
        const BackendContract::ArcParams p =
            BackendContract::NormalizeArc(cx, cy, radius, 0.0f, 90.0f, 4.0f);
        for (double d : { 5.0, 10.0, 40.0, 170.0 })
        {
            // 起点**之前** d 度
            const double ad = d * (kPiD / 180.0);
            const float px = static_cast<float>(cxd + r * std::cos(-ad));
            const float py = static_cast<float>(cyd + r * std::sin(-ad));
            const double toStart = 2.0 * r * std::sin(ad / 2.0);
            const double toEnd = 2.0 * r * std::sin((90.0 + d) * (kPiD / 180.0) / 2.0);
            const double want = (std::min)(toStart, toEnd);
            const double got = std::sqrt(double(BackendContract::ArcSegmentDistanceSq(p, px, py)));
            // 与独立 oracle 一致（oracle 密采样、不用角度投影）
            const double ref = std::sqrt(RefDistSqToArc(px, py, cxd, cyd, r, 0.0, 90.0));
            EXPECT_NEAR(got, ref, 1e-3);
            EXPECT_NEAR(got, want, 1e-3);
        }
        // 终点**之后** d 度
        for (double d : { 5.0, 10.0, 40.0 })
        {
            const double ad = d * (kPiD / 180.0);
            const float px = static_cast<float>(cxd + r * std::cos(90.0 * (kPiD / 180.0) + ad));
            const float py = static_cast<float>(cyd + r * std::sin(90.0 * (kPiD / 180.0) + ad));
            const double got = std::sqrt(double(BackendContract::ArcSegmentDistanceSq(p, px, py)));
            const double ref = std::sqrt(RefDistSqToArc(px, py, cxd, cyd, r, 0.0, 90.0));
            EXPECT_NEAR(got, ref, 1e-3);
        }
    }

    // ④ ★★ 方位真值表（C33-2 v1.3 的收口要求）——**镜像类与面积类断言测不出方向**
    //    ★ 必须用**部分扫角**：sweep = 360 时整圆覆盖，四个方位点**恒**命中 ⇒ 测试空转。
    //    判据 = 起点与终点两个方位点命中、而另外两个方位点**不**命中。
    for (double start : { 0.0, 90.0, 180.0, 270.0 })
    {
        const BackendContract::ArcParams p = BackendContract::NormalizeArc(
            cx, cy, radius, static_cast<float>(start), 90.0f, 4.0f);
        const auto f = [&](double deg) {
            const auto pt = P(deg, r);
            return BackendContract::ArcSegmentDistanceSq(p, pt.first, pt.second);
        };
        EXPECT_TRUE(f(start) < 1e-3);                        // 起点方位点：命中
        EXPECT_TRUE(f(start + 90.0) < 1e-3);                 // 终点方位点：命中
        EXPECT_TRUE(std::sqrt(double(f(start + 180.0))) > 0.5 * r);   // 反向：远离
        EXPECT_TRUE(std::sqrt(double(f(start + 270.0))) > 0.5 * r);
    }

    // ⑤ 圆心：dist² 恒等于 radius²（且 radius <= hw 时 <= hw² ⇒ 圆心在带内）
    {
        const BackendContract::ArcParams p =
            BackendContract::NormalizeArc(cx, cy, radius, 0.0f, 90.0f, 4.0f);
        EXPECT_NEAR(BackendContract::ArcSegmentDistanceSq(p, cx, cy), double(radius * radius), 1e-6);
        const BackendContract::ArcParams fan =
            BackendContract::NormalizeArc(cx, cy, 4.0f, 0.0f, 90.0f, 10.0f);
        EXPECT_TRUE(BackendContract::ArcSegmentDistanceSq(fan, cx, cy)
                    <= fan.halfWidth * fan.halfWidth);
    }

    // ⑥ 整圆：dist² = (radial - radius)²
    {
        const BackendContract::ArcParams p =
            BackendContract::NormalizeArc(cx, cy, radius, 123.0f, 360.0f, 4.0f);
        for (double deg = 0.0; deg < 360.0; deg += 30.0)
        {
            for (double rad : { 6.0, 10.0, 13.5 })
            {
                const auto pt = P(deg, rad);
                const double want = (rad - r) * (rad - r);
                EXPECT_NEAR(BackendContract::ArcSegmentDistanceSq(p, pt.first, pt.second), want, 1e-3);
            }
        }
    }

    // ⑦ 跨 0°：与独立 oracle 逐点一致（含负扫角）
    for (double sweep : { 20.0, -20.0, 270.0, -270.0 })
    {
        const BackendContract::ArcParams p = BackendContract::NormalizeArc(
            cx, cy, radius, 350.0f, static_cast<float>(sweep), 4.0f);
        for (double deg = 0.0; deg < 360.0; deg += 17.0)
        {
            for (double rad : { 8.5, 10.0, 11.5 })
            {
                const auto pt = P(deg, rad);
                const double got = std::sqrt(double(
                    BackendContract::ArcSegmentDistanceSq(p, pt.first, pt.second)));
                const double ref = std::sqrt(RefDistSqToArc(
                    pt.first, pt.second, cxd, cyd, r, 350.0, sweep));
                EXPECT_NEAR(got, ref, 1e-2);
            }
        }
    }
}

// ══════════════════════════════════════════════════════════════════════
// T33-9b：`NormalizeArc` 全表 + 数值安全带
// ══════════════════════════════════════════════════════════════════════
void Test33_9bNormalizeArcTruthTable()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    // drawable == false 的六种条件
    EXPECT_FALSE(BackendContract::NormalizeArc(0, 0, nan, 0, 90, 2).drawable);
    EXPECT_FALSE(BackendContract::NormalizeArc(0, 0, 10, nan, 90, 2).drawable);
    EXPECT_FALSE(BackendContract::NormalizeArc(0, 0, 0.0f, 0, 90, 2).drawable);      // radius <= 0
    EXPECT_FALSE(BackendContract::NormalizeArc(0, 0, 10, 0, 90, 0.0f).drawable);     // width <= 0
    EXPECT_FALSE(BackendContract::NormalizeArc(0, 0, 10, 0, 0.0f, 2).drawable);      // sweep == 0

    // 扫角钳制（含 ±）
    EXPECT_NEAR(BackendContract::NormalizeArc(0, 0, 10, 0, 450.0f, 2).sweepDeg, 360.0, 1e-6);
    EXPECT_NEAR(BackendContract::NormalizeArc(0, 0, 10, 0, -450.0f, 2).sweepDeg, -360.0, 1e-6);

    // 半宽钳制：radius=4, width=10 ⇒ hw = min(5, 4) = 4
    EXPECT_NEAR(BackendContract::NormalizeArc(0, 0, 4.0f, 0, 90, 10.0f).halfWidth, 4.0, 1e-6);
    // 宽 < 2*radius ⇒ hw = effW/2 = 2
    EXPECT_NEAR(BackendContract::NormalizeArc(0, 0, 10.0f, 0, 90, 4.0f).halfWidth, 2.0, 1e-6);
    // width < 1 ⇒ effW 下限 1 ⇒ hw = 0.5
    EXPECT_NEAR(BackendContract::NormalizeArc(0, 0, 10.0f, 0, 90, 0.2f).halfWidth, 0.5, 1e-6);

    // ★★ 数值安全带：极大有限值 ⇒ 不可绘制**且** oversized（可辨原因 ⇒ 后端能记一次 Warning）
    {
        const BackendContract::ArcParams big =
            BackendContract::NormalizeArc(0, 0, BackendContract::kMaxArcExtent * 2.0f, 0, 90, 2);
        EXPECT_FALSE(big.drawable);
        EXPECT_TRUE(big.oversized);
        const BackendContract::ArcParams bigW =
            BackendContract::NormalizeArc(0, 0, 10, 0, 90, BackendContract::kMaxArcExtent * 2.0f);
        EXPECT_FALSE(bigW.drawable);
        EXPECT_TRUE(bigW.oversized);
        const BackendContract::ArcParams bigC =
            BackendContract::NormalizeArc(BackendContract::kMaxArcCoord * 2.0f, 0, 10, 0, 90, 2);
        EXPECT_FALSE(bigC.drawable);
        EXPECT_TRUE(bigC.oversized);
        // 对照：正常输入**不得**被标 oversized（防「一键全拒」的假绿）
        const BackendContract::ArcParams ok = BackendContract::NormalizeArc(0, 0, 10, 0, 90, 2);
        EXPECT_TRUE(ok.drawable);
        EXPECT_FALSE(ok.oversized);
        // 生成器在超尺寸档返回 Empty（不崩、不分配）
        EXPECT_TRUE(GenerateArcCoverage(0.0f, 0.0f, BackendContract::kMaxArcExtent * 2.0f,
                                        0.0f, 90.0f, 2.0f).Empty());
        // 像素数上界：radius 足够大以致 bw*bh 超阈值 ⇒ Empty
        EXPECT_TRUE(GenerateArcCoverage(0.0f, 0.0f, 1000.0f, 0.0f, 360.0f, 2.0f).Empty());
    }

    // ★ startDeg 的**等价类**：10° 与 370° 几何等价（这也是缓存键允许用 Mod360 的依据）
    {
        const BackendContract::ArcParams p10 = BackendContract::NormalizeArc(20.5f, 20.5f, 10, 10.0f, 90, 4);
        const BackendContract::ArcParams p370 = BackendContract::NormalizeArc(20.5f, 20.5f, 10, 370.0f, 90, 4);
        EXPECT_TRUE(p10.drawable);
        EXPECT_TRUE(p370.drawable);
        for (double deg = 0.0; deg < 360.0; deg += 23.0)
        {
            const double a = deg * (kPiD / 180.0);
            const float px = static_cast<float>(20.5 + 10.0 * std::cos(a));
            const float py = static_cast<float>(20.5 + 10.0 * std::sin(a));
            EXPECT_NEAR(BackendContract::ArcSegmentDistanceSq(p10, px, py),
                        BackendContract::ArcSegmentDistanceSq(p370, px, py), 1e-3);
        }
        EXPECT_NEAR(p10.startDeg, 10.0, 1e-6);   // ★ 原值保留（记录件要用原值——C33-9）
        EXPECT_NEAR(p370.startDeg, 370.0, 1e-6);
    }
}

} // namespace

namespace ECDI::Test {

void RegisterArcCoverageTests()
{
    // Phase 33 批一：弧覆盖度生成层（T33-1..T33-9b；全零窗口依赖）
    GetTestRegistry().Add("Arc.CoverageValueRangeAndBounds",  &Test33_1CoverageValueRangeAndBounds);
    GetTestRegistry().Add("Arc.AreaConservationFourTiers",    &Test33_2AreaConservationFourTiers);
    GetTestRegistry().Add("Arc.MirrorSymmetryAndSameArcAliasing", &Test33_3MirrorSymmetryAndSameArcAliasing);
    GetTestRegistry().Add("Arc.CrossZeroAliasing",            &Test33_4CrossZeroAliasing);
    GetTestRegistry().Add("Arc.FullCircleSeamAndSign",        &Test33_5FullCircleSeamAndSign);
    GetTestRegistry().Add("Arc.RoundCapDiscriminant",         &Test33_6RoundCapDiscriminantBothEnds);
    GetTestRegistry().Add("Arc.DegenerateEmptyTable",         &Test33_7DegenerateEmptyTable);
    GetTestRegistry().Add("Arc.FanDegenerate",                &Test33_7bFanDegenerate);
    GetTestRegistry().Add("Arc.Determinism",                  &Test33_8Determinism);
    GetTestRegistry().Add("ArcSegmentDistance.TruthTable",    &Test33_9ArcSegmentDistanceTruthTable);
    GetTestRegistry().Add("Arc.NormalizeArcTruthTable",       &Test33_9bNormalizeArcTruthTable);
}

} // namespace ECDI::Test
