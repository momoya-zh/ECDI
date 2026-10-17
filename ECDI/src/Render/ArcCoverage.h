#pragma once

#include "Render/LineCoverage.h"   // ★ 复用 LineCoverageGrid（初设 PD33-A）——零新类型

namespace ECDI {

/// @brief 生成圆弧覆盖度（★ Phase 33 · 第一层「覆盖度生成」——纯几何、零 GDI、可无窗口单测）
/// @param cx,cy    圆心（物理像素，float 直入零取整——同 LineCoverage 的 R7）
/// @param radius   半径（<= 0 ⇒ 空网格）
/// @param startDeg 起始角（度；**3 点钟 = 0、顺时针为正**——C33-2）
/// @param sweepDeg 有向扫角（度；正 = 顺时针、负 = 逆时针；0 ⇒ 空网格）
/// @param width    线宽（<= 0 ⇒ 空网格；内部 effW = max(1, lround(width))）
/// @param samples  超采样倍率（默认 8 = 全框架 coverage AA 单一精度；<= 0 ⇒ 空网格）
/// @return 覆盖度网格；退化/无效输入 ⇒ Empty() == true
/// @details 绘制区域 = **到「有限弧段」的距离 <= hw** 的点集（C33-1 的单一式子）：
///          · **圆帽端点是该式的自然推论**（端点处弧段即端点 ⇒ 球邻域成半圆盘），不是额外特性；
///          · `|sweep| = 360°` 时弧段闭合 ⇒ 自然得到圆环，**无接缝特判**（C33-3②）；
///          · `radius <= width/2` ⇒ `hw` 被钳到 `radius` ⇒ 退化为扇形（C33-4 的已知语义）。
///          ★ **几何判定全部委托** `BackendContract::ArcSegmentDistanceSq`——本文件**不含**
///            任何角度/扇区逻辑（详设盯防②的可机检判据）。
///          ★ **bbox 是保守整圆**（不做按角度的收缩——初设 PD33-B）：代价被像素级预过滤吸收，
///            而角度收缩要处理跨 0° 包围盒，是新的错源。
/// @note 与 `GenerateLineCoverage` 是同族同构件：区别只在「点到线段的距离」换成
///       「点到弧段的距离」（`LineCoverage.cpp` 的 `DistSqToSegment` ↔ 本件调共享契约）。
LineCoverageGrid GenerateArcCoverage(float cx, float cy, float radius,
                                     float startDeg, float sweepDeg,
                                     float width, int samples = 8);

}
