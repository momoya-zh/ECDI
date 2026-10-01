#pragma once

#include <cstdint>
#include <vector>

namespace ECDI {

/// @brief 线段覆盖度网格（Phase 24 · 第一层「覆盖度生成」——纯几何零 GDI）
/// @details 与 `CornerCoverageMask` 同族：行主序、stride = width、coverage ∈ [0,255]。
/// ★ `originX/originY` = 网格 (0,0) 对应的**物理像素坐标**（初设 §5.5 的「单点出参」——
///    生成器计算一次、随网格携带，调用方**零重算**，杜绝多处独立取整的半像素漂移）。
struct LineCoverageGrid {
    int originX = 0;   ///< 网格 (0,0) 的物理像素 x（band bbox 左缘 floor）
    int originY = 0;   ///< 网格 (0,0) 的物理像素 y
    int width = 0;     ///< <= 0 或 coverage 空 ⇒ Empty
    int height = 0;
    std::vector<std::uint8_t> coverage;   ///< width × height，行主序
    bool Empty() const noexcept { return width <= 0 || height <= 0 || coverage.empty(); }
};

/// @brief 分流谓词（初设 D1）：**lround 后共线**（含 round 后重合的退化——legacy 画零像素，探针 A-7）
/// @details 必须用 `std::lround`（与 legacy 路径 `GDIBackend.cpp` 的坐标取整同式——基线 B11；
///          `int` 截断在负坐标半区与 lround 相差 1，会造成谓词与 legacy 实际绘制不一致）。
bool IsAxisAlignedAfterRound(float x0, float y0, float x1, float y1) noexcept;

/// @brief 生成线段覆盖度（**stadium 带模型：点到线段距离 <= hw**——初设 D3 圆帽）
/// @param x0..y1 物理像素坐标，**float 直入不取整**（R7；取整只发生在谓词与 legacy 路径）
/// @param width  线宽；内部 `effW = max(1, lround(width))`（D4，同 legacy 口径）
/// @param samples 超采样倍率（默认 8 = Q2 定案；`<= 0` → 空网格，契约同 `GenerateCornerMask`）
LineCoverageGrid GenerateLineCoverage(float x0, float y0, float x1, float y1,
                                      float width, int samples = 8);

}
