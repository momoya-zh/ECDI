#include "Render/LineCoverage.h"
#include "Render/BackendContract.h"

#include <algorithm>
#include <cmath>

namespace ECDI {

namespace {

/// @brief 点到线段距离的平方（t 钳到 [0,1] ⇒ 天然含两端圆帽——stadium 判定核心）
float DistSqToSegment(float px, float py, float ax, float ay, float bx, float by)
{
    const float abx = bx - ax;
    const float aby = by - ay;
    const float apx = px - ax;
    const float apy = py - ay;
    const float len2 = abx * abx + aby * aby;
    float t = (len2 > 0.0f) ? (apx * abx + apy * aby) / len2 : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    const float dx = ax + abx * t - px;
    const float dy = ay + aby * t - py;
    return dx * dx + dy * dy;
}

} // namespace

bool IsAxisAlignedAfterRound(float x0, float y0, float x1, float y1) noexcept
{
    if (!std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1))
    {
        return false;
    }
    return std::lround(y0) == std::lround(y1) || std::lround(x0) == std::lround(x1);
}

LineCoverageGrid GenerateLineCoverage(float x0, float y0, float x1, float y1,
                                      float width, int samples)
{
    LineCoverageGrid grid;   // 默认即空网格（Empty() == true）

    // ① 数值安全带：非有限值或非正入参直接拒绝（先于一切算术，防 NaN/Inf 传播入 UB）
    if (!std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1)
        || !std::isfinite(width) || samples <= 0)
    {
        return grid;
    }
    if (x0 == x1 && y0 == y1)
    {
        return grid;   // 退化第二层防线（第一层 = 分流谓词，探针 A-7：GDI 画零像素）
    }

    // 契约 C2：宽度同 legacy 口径（max(1, lround)）；坐标 float 直入零取整（R7）
    const int effW = std::max(1, static_cast<int>(std::lround(width)));
    const float hw = effW * 0.5f;

    // bbox（std::floor 而非 int 截断——负坐标正确）；两端各让出圆帽的鼓出量
    // ★ 宽类型算：防极大有限值 floor / 乘积溢出 int（对齐 ArcCoverage 与 BackendContract::kMaxArcPixels）
    const double minXd = std::min(static_cast<double>(x0), static_cast<double>(x1)) - static_cast<double>(hw);
    const double maxXd = std::max(static_cast<double>(x0), static_cast<double>(x1)) + static_cast<double>(hw);
    const double minYd = std::min(static_cast<double>(y0), static_cast<double>(y1)) - static_cast<double>(hw);
    const double maxYd = std::max(static_cast<double>(y0), static_cast<double>(y1)) + static_cast<double>(hw);
    const double bx0d = std::floor(minXd);
    const double by0d = std::floor(minYd);
    const double bx1d = std::floor(maxXd);
    const double by1d = std::floor(maxYd);
    const long long bwLL = static_cast<long long>(bx1d) - static_cast<long long>(bx0d) + 1;
    const long long bhLL = static_cast<long long>(by1d) - static_cast<long long>(by0d) + 1;
    if (bwLL <= 0 || bhLL <= 0 || bwLL * bhLL > BackendContract::kMaxArcPixels)
    {
        return grid;
    }

    const int bx0 = static_cast<int>(bx0d);
    const int by0 = static_cast<int>(by0d);
    const int bw = static_cast<int>(bwLL);
    const int bh = static_cast<int>(bhLL);

    // 预过滤（O3 定案）：像素中心到线段距离 > hw + √½ ⇒ 整个像素方格在带外
    // （三角不等式：方格内任意点距中心 ≤ √½，故 dist(q) ≥ dist(center) − √½ > hw）
    const float skipRadius = hw + 0.70710678f;
    const float skipRadius2 = skipRadius * skipRadius;
    const float hw2 = hw * hw;

    grid.originX = bx0;
    grid.originY = by0;
    grid.width = bw;
    grid.height = bh;
    grid.coverage.assign(static_cast<std::size_t>(bw) * static_cast<std::size_t>(bh), 0);

    const float invS = 1.0f / static_cast<float>(samples);
    const float total = static_cast<float>(samples) * static_cast<float>(samples);

    for (int j = 0; j < bh; ++j)
    {
        const float py = static_cast<float>(by0 + j);
        for (int i = 0; i < bw; ++i)
        {
            const float px = static_cast<float>(bx0 + i);
            if (DistSqToSegment(px + 0.5f, py + 0.5f, x0, y0, x1, y1) > skipRadius2)
            {
                continue;   // 整格带外，coverage 保持 0
            }

            int hit = 0;
            for (int sj = 0; sj < samples; ++sj)
            {
                const float sy = py + (static_cast<float>(sj) + 0.5f) * invS;
                for (int si = 0; si < samples; ++si)
                {
                    const float sx = px + (static_cast<float>(si) + 0.5f) * invS;
                    if (DistSqToSegment(sx, sy, x0, y0, x1, y1) <= hw2)
                    {
                        ++hit;
                    }
                }
            }
            grid.coverage[static_cast<std::size_t>(j) * static_cast<std::size_t>(bw)
                          + static_cast<std::size_t>(i)]
                = static_cast<std::uint8_t>(std::lround(255.0f * static_cast<float>(hit) / total));
        }
    }

    return grid;
}

}
