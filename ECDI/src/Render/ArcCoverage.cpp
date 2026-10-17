#include "Render/ArcCoverage.h"

#include "Render/BackendContract.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ECDI {

LineCoverageGrid GenerateArcCoverage(float cx, float cy, float radius,
                                     float startDeg, float sweepDeg,
                                     float width, int samples)
{
    LineCoverageGrid grid;   // 默认即空网格（Empty() == true）

    if (samples <= 0)
    {
        return grid;   // 契约：同 GenerateCornerMask / GenerateLineCoverage 的入参语义
    }

    // ★ C33-4 全表（无效判定 + 数值安全带 + 扫角钳制 + 半宽钳制）在 NormalizeArc 内已判完
    const BackendContract::ArcParams p =
        BackendContract::NormalizeArc(cx, cy, radius, startDeg, sweepDeg, width);
    if (!p.drawable)
    {
        return grid;   // 含「极大有限值」档：不画（后端另记一次 Warning——§3.3 N2）
    }

    // ── bbox（★ 保守整圆：不做按角度的收缩，PD33-B）────────────────────────
    // ★ 宽类型算：`floor` 结果与乘积都可能越界（§3.3 N3——先判后分配）
    const float pad = p.radius + p.halfWidth + 1.0f;
    const double bx0d = std::floor(static_cast<double>(cx) - static_cast<double>(pad));
    const double by0d = std::floor(static_cast<double>(cy) - static_cast<double>(pad));
    const double bx1d = std::floor(static_cast<double>(cx) + static_cast<double>(pad));
    const double by1d = std::floor(static_cast<double>(cy) + static_cast<double>(pad));
    const long long bwLL = static_cast<long long>(bx1d) - static_cast<long long>(bx0d) + 1;
    const long long bhLL = static_cast<long long>(by1d) - static_cast<long long>(by0d) + 1;
    if (bwLL <= 0 || bhLL <= 0
        || bwLL * bhLL > BackendContract::kMaxArcPixels)
    {
        return grid;
    }

    const int bw = static_cast<int>(bwLL);
    const int bh = static_cast<int>(bhLL);
    grid.originX = static_cast<int>(bx0d);
    grid.originY = static_cast<int>(by0d);
    grid.width = bw;
    grid.height = bh;
    grid.coverage.assign(static_cast<std::size_t>(bw) * static_cast<std::size_t>(bh), 0);

    // ── 预过滤（★ 零开方：两侧平方比较）───────────────────────────────────
    // 判据来源：`dist(P, 弧段) >= | |P| - radius |`（三角不等式，对**弧段**同样成立）
    // ⇒ `|radial - radius| > skipR` 是**保守**筛除条件；skipR = hw + √½ 吸收像素方格
    // 半对角线（方格内任意点距中心 <= √½）。
    // ★★ `lo > 0.0f` 守卫是**必须的**：内半径非正（扇形退化）时 `lo < 0`，而 `lo²` 仍是
    //    **正数** ⇒ 不加守卫会把**圆心附近**整片判成带外（详设 §3.1 / 盯防④）。
    const float skipR = p.halfWidth + 0.70710678f;
    const float lo = p.radius - skipR;
    const float hi2 = (p.radius + skipR) * (p.radius + skipR);
    const float lo2 = lo * lo;
    const float hw2 = p.halfWidth * p.halfWidth;

    const float invS = 1.0f / static_cast<float>(samples);
    const float total = static_cast<float>(samples) * static_cast<float>(samples);

    for (int j = 0; j < bh; ++j)
    {
        const float py = static_cast<float>(grid.originY + j);
        for (int i = 0; i < bw; ++i)
        {
            const float px = static_cast<float>(grid.originX + i);

            const float dcx = (px + 0.5f) - cx;
            const float dcy = (py + 0.5f) - cy;
            const float radialSq = dcx * dcx + dcy * dcy;
            if (radialSq > hi2 || (lo > 0.0f && radialSq < lo2))
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
                    if (BackendContract::ArcSegmentDistanceSq(p, sx, sy) <= hw2)
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
