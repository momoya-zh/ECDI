#pragma once

#include <cstdint>

#include "ECDI/Core/Color.h"
#include "Render/CornerCoverageMask.h"

namespace ECDI{

struct LineCoverageGrid;   // Phase 24：前置声明（本头不依赖生成层——依赖单向：合成层认识网格，反向禁止）

/// @brief 覆盖度 → 像素：把覆盖度掩码按指定角方向合成为**预乘 BGRA** 像素
/// @details
/// Phase 19.1 从 `GDIBackend::FillPatchFromMask` **原样抽出**（算法逐位不变）。
/// 抽出的理由：这段是**纯像素运算**（零 GDI——不碰 HDC/HBITMAP/COLORREF/Windows.h），
/// 而它是「抗锯齿」这条能力的**像素合成点**。放到平台无关处之后，任何后端都能复用
/// 同一套抗锯齿 ⇒ **换后端时不必重写覆盖度合成**（本相位后端可替换性分析的落点之一）。
///
/// 与 `CornerCoverageMask.h` 的分工（同目录、同主题、都不认识任何绘制 API）：
/// - 那个头：**生成覆盖度**（纯几何 · pixel-square coverage 模型）
/// - 本头：  **把覆盖度变成像素**（纯合成）
///
/// ⚠️ 本函数**不认识"圆角"这个语义**——它只认识「一张 R×R 覆盖度掩码 + 一个角方向」。
///    之所以命名为 Corner*，是因为当前唯一的覆盖度来源是四分之一圆盘；
///    将来若出现其他需要覆盖度抗锯齿的形状（例如线段），可在同目录下按同一形态扩展。
///
/// @param dest   目标像素缓冲首地址（**不是行首**——函数内按 stride 逐行定位）
/// @param stride 目标缓冲的**实际每行字节数**（由调用方给；见 GDIBackend 侧的 DIB 行宽说明）
/// @param mask   覆盖度掩码（R×R）；`mask.Empty()` 时不做任何写入
/// @param color  形状颜色；**alpha 视为 1**（与既有实现一致：输出 A = 该像素覆盖度）
/// @param corner 目标角——决定索引变换方向（见 `MaskIndexX` / `MaskIndexY`）
void RasterizeCornerPatch(std::uint8_t* dest, int stride,
                          const CornerCoverageMask& mask, const Color& color, CornerId corner);

/// @brief 覆盖度网格 → 像素：一般化预乘合成（Phase 24 · **无角变换**，Corner 版保留不动）
/// @details 契约 C6（初设 §5.2）：`e = (c*a8+127)/255`（**有效覆盖** = coverage × alpha）；
///          输出 `A = e`、`RGB = (colorByte*e+127)/255`——a8=255 时 e == c，与
///          `RasterizeCornerPatch` **逐位同公式**（B6）。不变量 C7：`RGB ≤ A` 恒成立
///          （colorByte ≤ 255 ⇒ 每通道 (cb*e+127)/255 ≤ e）。
/// @param dest   目标像素缓冲首地址（按 stride 逐行定位）
/// @param stride 目标缓冲的**实际每行字节数**
/// @param grid   覆盖度网格；`grid.Empty()` 时不做任何写入
/// @param color  形状颜色（alpha 一般化消费——D0：a<1 的线段在此获得真半透明）
void RasterizeMask(std::uint8_t* dest, int stride,
                   const LineCoverageGrid& grid, const Color& color);

}
