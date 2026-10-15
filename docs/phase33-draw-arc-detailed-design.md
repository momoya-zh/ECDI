# Phase 33 圆弧绘制原语（DrawArc）与环形进度控件（RingProgressBar）—— 详细设计

> **状态**：v1.0（2026-10-14）——初稿，**待评审**
> **输入**：需求稿 `phase33-draw-arc-requirements.md` **v1.2**（C33-1..10 契约 + R1–R9）· 初设稿 `phase33-draw-arc-preliminary-design.md` **v1.1**（PD33-A..G + T33-1..18 大纲 + O33/L33）
> **模板**：沿 Phase 24 详设的十一节形态（同族最近先例：新覆盖度生成器 + 两后端消费 + 内部头）
> **定位**：把初设的决策落成**逐文件、逐函数的实现规格**与**精确断言**；契约不重开，只在实现层补足初设未定的数值与形态。

---

## 1. 代码基线（B1–B24，全部 2026-10-14 带行号实测）

| # | 事实 | 出处 |
|---|---|---|
| B1 | `RenderingBackend` 6 个绘制原语均为**纯虚**；`SetBackdropSurface` 是唯一带默认实现的虚函数 | `include/ECDI/Render/RenderingBackend.h:38-118` |
| B2 | 命令 variant 全集 = 8 个（Rect/Text/Line/RoundedRect/Image/Clip×2/FocusRect） | `include/ECDI/Render/RenderCommand.h:93-96` |
| B3 | `Renderer::Execute` 用 `std::visit` + 8 个**非模板** `ExecuteCommand` 重载（**漏加即编译错误**） | `src/Render/Renderer.cpp:52-56` |
| B4 | `ScaleRect` / `ScalePoint` / `ScaleLength` 三个折算助手**均在匿名 namespace**；★ 注释明写「**不得**用它折算 `Font::size`」 | `src/Render/Renderer.cpp:11-36` |
| B5 | `DrawLineCommand` 展开时**三个几何量都折**（含 `width`——注释标「盯防③最易漏」） | `src/Render/Renderer.cpp:74-80` |
| B6 | `PaintContext` 的两个剔除构造 + `IsRectVisible`；命令均为一行 `emplace_back` | `src/Render/PaintContext.cpp:25-108` |
| B7 | `LineCoverageGrid` = `{originX, originY, width, height, coverage[]}`；`Empty()` 判 `width<=0 \|\| height<=0 \|\| coverage.empty()` | `src/Render/LineCoverage.h:12-19` |
| B8 | `RasterizeMask(dest, stride, grid, color)` 的形参类型**就是** `LineCoverageGrid` | `src/Render/CoverageRaster.h:44-45` |
| B9 | `GenerateLineCoverage` 五步：samples 守卫 → 退化守卫 → `effW=max(1,lround)` → **bbox 两端各让 `hw`** → 预过滤（`skipRadius = hw+√½`）→ `samples²` 超采样 | `src/Render/LineCoverage.cpp:32-110` |
| B10 | ★ `DistSqToSegment` 的 `t` **钳到 [0,1]** —— 这正是「线段两端天然圆帽」的机制 | `src/Render/LineCoverage.cpp:11-23` |
| B11 | `GDIBackend::DrawLineCoverage` 三步：`GenerateLineCoverage` → `PatchSurface.Ensure` 失败则 **Warning + `DrawLineLegacy` 降级** → `RasterizeMask` + `BlendPatch` | `src/Render/GDIBackend.cpp:865-889` |
| B12 | `DrawLine` 三分支：`!AA` → legacy；`a≥1 且轴对齐` → legacy；其余 → 覆盖度 | `src/Render/GDIBackend.cpp:822` 附近 |
| B13 | `PatchSurface::Ensure(reference, reqW, reqH)` = **两维各自只增不减**；`areaWarned` 是 2M 像素 one-shot | `src/Render/GDIBackend.h:126-142` |
| B14 | `GLRenderer::DrawLine`：`GenerateLineCoverage` → `LineSlot` → ★ **`!slot.valid` 时 `SolidSegment` 实心降级**（不整线消失） | `src/Render/GLRenderer.cpp:292-332` |
| B15 | `LineSlot` 的键 = `(grid.w, grid.h, lround(x0*8), lround(y0*8), lround(x1*8), lround(y1*8), lround(width*8))`；命中即返回，miss 才 `Allocate`+`Upload` | `src/Render/GLRenderer.cpp:756-780` |
| B16 | `SolidSegment` = 以两点为轴、垂直半宽的**实心四边形**（走 `m_solid` 1×1 全亮单元） | `src/Render/GLRenderer.cpp:618-644` |
| B17 | `GLGlyphAtlas::Allocate` 是**纯逻辑**（不碰 GL），`Upload` 需 current context；图集 `kSize = 1024`，**不淘汰** | `src/Render/GLGlyphAtlas.h:46-66` |
| B18 | `TexturedQuad(ax,ay,aw,ah, x,y,w,h)` 走图集 UV + `Snap`（顶点吸附到整数像素） | `src/Render/GLRenderer.cpp:694-708` |
| B19 | `BackendContract.h` 已有 `ImageIsDrawable` / `QuantizeEdge` / `SnapVertex`；头注释确立「**共享代码强于共享断言**」 | `src/Render/BackendContract.h:10-107` |
| B20 | `RecordingBackend` 的记录结构**逐字段对应命令字段**（`LineDraw{start,end,width,color}` 等） | `src/Render/RecordingBackend.h:41-74` |
| B21 | `Theme` 有 **8 个纯虚 getter**；库内实现者 2 个：`DefaultTheme` + 测试替身 `RedButtonTextTheme` | `include/ECDI/Theme/Theme.h:22-29` · `src/Tests/ThemeTests.cpp:177-195` |
| B22 | ★★ **`CMakeLists.txt` 用 `GLOB_RECURSE ... CONFIGURE_DEPENDS`** 收 `ECDI/src/*.cpp`（排除 `/Tests/`）、公共头、测试源 ⇒ **新增文件零 CMake 改动**（初设 §6 把 CMake 列为改动文件**有误**，本稿订正） | `CMakeLists.txt:80-82` · `:136` · `:153` |
| B23 | `RunAllTests.cpp` 逐行 `RegisterXxxTests()`；测试文件自带 `RegisterXxxTests()` 定义 + `RunAllTests.h` 声明 | `src/Tests/RunAllTests.cpp:9-47` · `RunAllTests.h` |
| B24 | `ProgressBar` 的动画形态：`SetProgress` clamp + 同目标 no-op + 无宿主瞬时到位；`AnimateTo` 用 `from = m_displayProgress`（**替换式重启**）；`AnimationToken` RAII | `src/Widget/ProgressBar.cpp:22-90` |

★ **基线核对**：B2/B3 共同支撑「加命令 = 必须加 `std::visit` 重载，漏加编译错误」；B7/B8 支撑「复用 `LineCoverageGrid` 零新类型」；B10 是 C33-1「圆帽是几何后果」的**既有代码证据**；B22 **订正了初设的一处影响面错误**。

---

## 2. 逐文件改动（△1–△13；新建 5 · 改动 8 · ★ CMake 零改动）

### △1 `ECDI/src/Render/BackendContract.h`（改 · 追加三段）

**a) `ArcParams` + `NormalizeArc`（C33-4 的唯一实现）**

```cpp
/// @brief 弧的规范化参数（★ Phase 33：C33-4「退化与钳制表」的唯一实现）
/// @details `drawable == false` ⇒ 调用方**必须**直接跳过（不得再进几何判定）。
struct ArcParams
{
    float cx = 0.0f, cy = 0.0f;
    float radius = 0.0f;
    float startDeg = 0.0f;
    float sweepDeg = 0.0f;      ///< 已钳制到 [-360, 360]
    float halfWidth = 0.0f;     ///< 已钳制：hw = min(effW, 2*radius) / 2
    bool drawable = false;
};

/// @brief 规范化（无效判定 + 角度归约 + 扫角钳制 + 半宽钳制）—— C33-4 全表
/// @details 判定顺序（★ 顺序即契约，逐条对应 C33-4）：
///   ① 任一浮点入参 `!isfinite` ⇒ 不可绘制（**先于一切算术**——防 NaN 传播进 fmod/lround）
///   ② `radius <= 0` ⇒ 不可绘制（**不取绝对值**——不静默改变输入语义）
///   ③ `width <= 0` ⇒ 不可绘制（沿 DrawLine 家族）
///   ④ `sweepDeg == 0` ⇒ 不可绘制
///   ⑤ `sweepDeg = clamp(sweepDeg, -360, 360)`（超一周**钳制**，不是多圈）
///   ⑥ `effW = max(1, lround(width))`；`hw = min(effW * 0.5f, radius)`（★ **内半径非正 ⇒ 退化为扇形**）
///   ⑦ `startDeg` **不归约**（保留原值——归约在 `ArcSegmentDistanceSq` 内部按 mod 360 做；
///      理由：记录/缓存键要用**原值**，见 C33-9 与 △7）
inline ArcParams NormalizeArc(float cx, float cy, float radius,
                              float startDeg, float sweepDeg, float width) noexcept;
```

★ **实现注记（三条）**：
① `std::isfinite` 逐参检查（`<cmath>` 已在 `BackendContract.h` 头内 include，B19）；
② 钳制用 `(std::min)` / `(std::max)` **加括号**（Windows.h 宏防护，条 10 同族）；
③ `hw` 的表达式**必须是** `min(effW * 0.5f, radius)` —— **不是** `min(effW, 2*radius) * 0.5f`（两者在 `effW` 为奇数时不同：前者以 float 半宽为准，后者会先把 `2*radius` 截断；★ 取前者是因为**半宽才是几何量**，与 C33-4 的 `hw` 定义逐字对应）。

**b) `ArcSegmentDistanceSq`（C33-1 + C33-2 的唯一实现）**

```cpp
/// @brief 点 (x,y) 到**有限弧段**的距离平方（★ C33-1 的唯一实现；C33-2 的有向扫角在此落地）
/// @details 与 `LineCoverage.cpp` 的 `DistSqToSegment` **结构逐行同构**（B10）：
///          `t = clamp(投影, 0, 1)` ⇒ **端点处退化为「到端点的距离」⇒ 球邻域成半圆盘 = 圆帽**
///          （圆帽是几何后果，**不是**额外特性）。
///          ★★ **本函数是全框架弧几何的唯一判据**：GDI / GL / 生成器**都只经它**判定。
///          ★ 角度处理：正负扫角用**同一条「从起点沿扫描方向的有向角差」**规则 ⇒
///            起点恒给 `t = 0`（正负对称——详设 §3.2 的负扫角订正）。
/// @pre `p.drawable == true`（调用方保证；本函数不做守卫，不再判 NaN）
/// @note 返回**距离平方**（与 `DistSqToSegment` 同口径——bool 判定会掩盖几何量，
///       且平方根对比较无意义）
inline float ArcSegmentDistanceSq(const ArcParams& p, float x, float y) noexcept;
```

**实现规格**（逐行）：

```
1  dx = x - p.cx;  dy = y - p.cy
2  radial = sqrtf(dx*dx + dy*dy)
3  if (radial <= 1e-6f)                     // ★ 圆心本身：极角无定义 ⇒ 特判
4      return p.radius * p.radius;          //   到弧段距离 = radius（几何上正确，且不调 atan2）
5  a = atan2f(-dy, dx) * (180/π)            // ★ y 取负 ⇒ 顺时针为正（C33-2）
6  delta = (p.sweepDeg >= 0) ? (a - p.startDeg) : (p.startDeg - a)     // ★ 正负同一规则
7  delta = fmodf(delta, 360);  if (delta < 0) delta += 360              // Mod360（禁 int 截断）
8  t = delta / fabsf(p.sweepDeg)            // |sweep| = 360 ⇒ delta/360 ∈ [0,1) 覆盖全周
9  t = clamp(t, 0, 1)                       // ★★ 圆帽的关键一步（同 DistSqToSegment）
10 ang = p.startDeg + sign(p.sweepDeg) * t * fabsf(p.sweepDeg)   // 弧段上参数 t 处的方向角
11 qx = p.cx + p.radius * cosf(ang * π/180);  qy = p.cy - p.radius * sinf(ang * π/180)
12 return (x-qx)^2 + (y-qy)^2
```

★★ **与初稿的关键差异（评审 §5 的 `atan2(0,0)` 顾虑）**：**第 3-4 行的特判**取代了「圆心是否在扇区内」的角度判定——`radial ≈ 0` 时 `atan2` 无定义，而几何上「圆心到弧段的距离」**恒等于 `radius`**（弧段上任一点距圆心都是 `radius`），故**直接返回 `radius²`**。⇒ **圆心归属由距离比较自动决定**（`radius ≤ hw` 时命中），**全程不依赖 `atan2(0,0)`**。
★ **第 8 行的 `|sweep| = 360` 情形**：`t = delta/360`，`delta ∈ [0,360)` ⇒ `t ∈ [0,1)` ⇒ `t` 的钳制**不再产生端点圆帽的退化**，判据自然成为「到整圆的距离」= `|radial − radius|` —— **整圆无需特判**（C33-3②）；端点圆帽的**重合**问题在**面积参照**里处理（C33-5，见 T33-2）。
★ **第 11 行的 y 取负**：与第 5 行的角度约定配套（屏幕坐标 y 向下 ⇒ 顺时针为正）。

**c) 头注释追加一段**：说明本件新增弧几何的理由（沿用 B19 的「共享代码强于共享断言」论证）+ **可机检声明**：「弧几何的唯一判据 = `ArcSegmentDistanceSq`；`ArcCoverage.cpp` **不得**出现独立的扇区判定」。

### △2 `ECDI/src/Render/ArcCoverage.h`（🆕 新建）

```cpp
#pragma once
#include "Render/LineCoverage.h"   // ★ 复用 LineCoverageGrid（初设 PD33-A）——零新类型
namespace ECDI {
/// @brief 生成圆弧覆盖度网格（Phase 33 · 第一层「覆盖度生成」——纯几何零 GDI）
/// @details 与 `GenerateLineCoverage` **逐段同构**（B9），只有两点不同：
///          ① 距离函数 = `BackendContract::ArcSegmentDistanceSq`（到**有限弧段**）
///          ② bbox = **保守整圆外接方**（初设 PD33-B：不做按角度的收缩——代价被
///             像素级预过滤吸收，而扇区包围盒要处理跨 0° 的 wrap）
///          ★ 几何判定**全部**委托 `ArcSegmentDistanceSq`（不在此文件内写角度逻辑——
///            详设盯防②的可机检判据）。
/// @param cx,cy    圆心（物理像素，float 直入零取整——同 LineCoverage 的 R7）
/// @param radius   半径（<= 0 ⇒ 空网格）
/// @param startDeg 起始角（度；3 点钟 = 0、顺时针为正）
/// @param sweepDeg 有向扫角（度；正 = 顺时针、负 = 逆时针；0 ⇒ 空网格）
/// @param width    线宽（<= 0 ⇒ 空网格；内部 effW = max(1, lround(width))）
/// @param samples  超采样倍率（默认 8 = 全框架 coverage AA 单一精度；<= 0 ⇒ 空网格）
/// @return 覆盖度网格；退化/无效输入 ⇒ `Empty() == true`
LineCoverageGrid GenerateArcCoverage(float cx, float cy, float radius,
                                     float startDeg, float sweepDeg,
                                     float width, int samples = 8);
}
```

### △3 `ECDI/src/Render/ArcCoverage.cpp`（🆕 新建）

**实现规格**（七步，逐条对应 C33-x）：

```
1  if (samples <= 0) return {};                       // 契约：同 GenerateCornerMask
2  ArcParams p = BackendContract::NormalizeArc(cx, cy, radius, startDeg, sweepDeg, width);
3  if (!p.drawable) return {};                        // ★ C33-4 全表在 NormalizeArc 内已判完
4  bbox（★ 保守整圆）：pad = p.radius + p.halfWidth + 1
      bx0 = floor(cx - pad);  by0 = floor(cy - pad)
      bw  = floor(cx + pad) - bx0 + 1;  bh = floor(cy + pad) - by0 + 1
      if (bw <= 0 || bh <= 0) return {};
5  grid.originX/Y/width/height 赋值 + coverage.assign(bw*bh, 0)
6  逐像素：
      skipR  = p.halfWidth + 0.70710678f;  skipR2 = skipR*skipR     // 同 B9 的 √½
      hw2    = p.halfWidth * p.halfWidth
      for j, i:
          // ★ 预过滤：以「到圆心距离」与 radius 的**径向差**为判据（详设 §3.1 的变量命名订正）
          radialSq = (px-cx)^2 + (py-cy)^2
          // 保守预过滤（不开方）：|sqrt(radialSq) - radius| > skipR ⇔ 两边分别判
          if (radialSq > (radius + skipR)^2 || radialSq < (radius - skipR)^2) 且不满足扇形内
              continue;                                             // 整格带外
          hit = 0
          for sj, si: 子采样点 (sx, sy)
              if (BackendContract::ArcSegmentDistanceSq(p, sx, sy) <= hw2) ++hit
          coverage[j*bw + i] = lround(255 * hit / (samples*samples))
7  return grid;
```

★ **第 6 步的预过滤必须支持「内半径非正」的扇形退化**：当 `radius ≤ halfWidth` 时 `(radius − skipR) < 0` ⇒ 平方后 `radialSq < (radius−skipR)²` 恒假 ⇒ **不会误跳过圆盘内部**——★ 但「进入采样」的条件也随之放宽到「整圆盘」，**正确**（扇形本就覆盖圆心附近）。★ 该式**不改写**为 `|radial − radius| > skipR`（那要开方）；用**两侧平方比较**保持与 B9 同族的零开方风格，且**天然处理负下界**。
★ **`ArcSegmentDistanceSq` 做唯一几何判定** ⇒ 本文件**无** `atan2`/扇区逻辑（盯防②）。

### △4 `ECDI/include/ECDI/Render/RenderingBackend.h`（改 · +1 纯虚）

```cpp
/// @brief 绘制圆弧描边（★ Phase 33——框架首个曲线原语；D5 裁决 = **纯虚**）
/// @param center       圆心（最终坐标）
/// @param radius       半径（<= 0 ⇒ 不绘制）
/// @param startAngleDeg 起始角（**度**；0° = 3 点钟方向）
/// @param sweepAngleDeg 有向扫角（**度**；正 = 顺时针、负 = 逆时针；0 ⇒ 不绘制；
///                      |sweep| > 360 ⇒ 钳制到 ±360）
/// @param width        线宽（<= 0 ⇒ 不绘制；后端 lround + 下限 1px）
/// @param color        线色
/// @details 绘制区域 = **到「有限弧段」的距离 ≤ hw** 的点集（C33-1）；
///          **端点固定圆头**（该定义的自然推论——C33-1 注）；
///          `radius <= width/2` ⇒ `hw` 钳制为 `radius` ⇒ **几何退化为扇形**（C33-4，已知语义）。
///          ★ 单位后缀 `Deg` 是**有意的**：全库首个角度 API，让单位在调用点可见（条 95①）。
virtual void DrawArc(const Point& center, float radius,
                     float startAngleDeg, float sweepAngleDeg,
                     float width, const Color& color) = 0;
```
★ **位置**：插在 `DrawLine`（`:88-89`）**之后**、`DrawRoundedRect`（`:95`）之前——按「线族相邻」排布（弧是线的曲线化）。

### △5 `ECDI/include/ECDI/Render/RenderCommand.h`（改 · +1 命令 + variant）

```cpp
/// @brief 绘制圆弧命令（纯数据，Phase 33）
/// @details 死数据：圆心 + 半径 + 起始角 + 有向扫角 + 线宽 + 颜色。
/// ★ **角度不随 DPI 缩放**（无量纲——Renderer 展开时**只折 center/radius/width**）。
struct DrawArcCommand
{
    Point center;                 ///< 圆心（最终坐标）
    float radius = 0.0f;          ///< 半径
    float startAngleDeg = 0.0f;   ///< 起始角（度；不缩放）
    float sweepAngleDeg = 0.0f;   ///< 有向扫角（度；不缩放）
    float width = 1.0f;           ///< 线宽
    Color color;                  ///< 线色
};
```
variant 追加 `DrawArcCommand`（`RenderCommand.h:93-96`，★ 加在 `DrawLineCommand` 之后保持「线族相邻」）。

### △6 `ECDI/src/Render/Renderer.h/.cpp`（改 · +1 重载）

```cpp
void Renderer::ExecuteCommand(const DrawArcCommand& cmd, float scale)
{
    // ★★ 盯防①：**两个角度不折 scale**——角度无量纲，150% 下把 90° 折成 135° 会错得离谱。
    //    这是 ScaleLength 家族里第一个「float 但不可折」的形参（同族先例：Font::size 不折，
    //    Renderer.cpp:28-34 的注释已立此纪律）。
    m_backend.DrawArc(ScalePoint(cmd.center, scale),
                      ScaleLength(cmd.radius, scale),
                      cmd.startAngleDeg,      // ← 原样
                      cmd.sweepAngleDeg,      // ← 原样
                      ScaleLength(cmd.width, scale),
                      cmd.color);
}
```

### △7 `ECDI/src/Render/GLRenderer.h/.cpp`（改 · +override + `ArcSlot` + 二值化降级）

**头**：public 区加 `void DrawArc(...) override;`；private 区加
```cpp
/// @brief 弧：取（或生成并上传）覆盖度网格槽位（★ 键含角度——与 LineSlot 的关键差异）
GlyphSlot ArcSlot(const LineCoverageGrid& grid, const BackendContract::ArcParams& p);
```
成员：`std::map<std::tuple<int,int,int,int,int,int,int,int,int,int>, GlyphSlot> m_arcSlots;`
★ **键 = 10 元组**：`(grid.width, grid.height, Q8(cx), Q8(cy), Q8(radius), QA(start), QA(sweep), Q8(width), binarize, colorKey)`。
★ **`binarize` 与 `colorKey` 是否入键？** ⇒ **`binarize` 必须入**（同一几何有两种像素内容：AA 与硬边——不入键会**串味**）；**`colorKey` 不入**（覆盖度是**与颜色无关的几何量**，`Upload` 的是 A8 覆盖度、着色在 `Shade`——与 `LineSlot` 一致）。

**实现（`DrawArc`）**：
```
1  ArcParams p = BackendContract::NormalizeArc(...)
2  if (!p.drawable) return;
3  if (!m_ready) return;                                   // 沿既有 early-return
4  grid = GenerateArcCoverage(...samples = 8)             // ★ GL 侧固定 8（见 §4 的 O33-1 收口）
5  if (grid.Empty()) return;
6  bool binarize = !m_antiAliasing;                       // ★ PD33-C：关 AA ⇒ 二值化
7  GlyphSlot slot = ArcSlot(grid, p, binarize);
8  if (!slot.valid)  { /* ★ 降级见下 */ }
9  Shade(color);
10 TexturedQuad(slot.x, slot.y, grid.width, grid.height,
                originX, originY, grid.width, grid.height);
```
★ **`binarize` 的实现位置**：在 `ArcSlot` 内 `Upload` **之前**把 `grid.coverage` 拷成局部 `std::vector`，逐字节 `>0 ? 255 : 0`。★ **不改 `grid` 本身**（它是值参、但拷贝代价大；且**不可**污染调用方语义）。
★★ **降级（PD33-D）**：`!slot.valid`（**图集满**）⇒ ★ **仍走 `ArcSlot` 但强制 `binarize = true`**：
```
if (!slot.valid) slot = ArcSlot(grid, p, /*binarize=*/true);   // 二值化 ⇒ 体积不变但内容更简
```
★ **若二值化后仍 `!slot.valid`**（图集真的没有空间）⇒ **不画**（`return`），**不得**退化成像 `DrawLine` 那样的 `SolidSegment` 直线（C33-7：降级必须仍是弧）。
★ ★ **本决策对初设 PD33-D 的一处收窄（如实登记）**：初设写「二值化后走**同一个** `ArcSlot`」，隐含「二值化能解决图集满」——但**二值化不改变 `Allocate` 需要的槽位尺寸** ⇒ **对图集满无帮助**。⇒ **现行处置**：`binarize` 的**真实用途 = PD33-C（关 AA）**；图集满时**先试二值化槽位（可能命中已缓存的硬边槽）**，未命中则**不画 + 一次 Warning**（沿 `m_fullWarned` 的 one-shot 形态）。★ **这是初设的一处过度乐观，本稿订正**（详设偏差 E33-1）。

### △8 `ECDI/src/Render/GDIBackend.h/.cpp`（改 · +override）

**头**：public 区加 `void DrawArc(...) override;`
**实现**（沿 B11 三步）：
```
1  ArcParams p = NormalizeArc(...);  if (!p.drawable) return;
2  grid = GenerateArcCoverage(..., samples = m_cornerMaskCache.GetSamples());   // ★ 角缓存同一旋钮
3  if (grid.Empty()) return;
4  // ★ PD33-C：关 AA ⇒ 二值化（与 GL 同机制）
5  if (!m_antiAliasing) { 局部拷贝 → 逐字节 >0?255:0 }
6  if (!m_patchSurface.Ensure(m_memoryDC, grid.width, grid.height)) {
       Logger::Log(Warning, L"GDIBackend: patch surface unavailable - arc skipped");
       return;                       // ★ L33-1：无 legacy 弧降级（有意）
   }
7  RasterizeMask((uint8_t*)m_patchSurface.bits, m_patchSurface.width * 4, grid, color);
8  BlendPatch(m_memoryDC, grid.originX, grid.originY, grid.width, grid.height);
```
★ **第 5 步的二值化拷贝**：与 GL 同理——**不改 `grid`**（值语义但拷贝贵）。★ **注意**：GDI 的 `RasterizeMask` 吃的是**同一份** `grid`，故二值化**必须在拷贝上做**（否则会静默改掉后续消费者的输入）。

### △9 `ECDI/src/Render/RecordingBackend.h/.cpp`（改 · +记录）

```cpp
struct ArcDraw
{
    Point center; float radius = 0.0f;
    float startAngleDeg = 0.0f;   ///< ★ **原样**（不做归约/钳制——C33-9）
    float sweepAngleDeg = 0.0f;   ///< ★ **原样**
    float width = 1.0f; Color color;
};
std::vector<ArcDraw> arcDraws;
```
★ **实现 = 一行 `arcDraws.push_back(...)`，逐字段直拷入参** —— **绝不**调 `NormalizeArc`（若归一化，`sweep=450` 会被记成 `360`，「断言原样转发」即刻失去意义）。

### △10 `ECDI/include/ECDI/Render/PaintContext.h/.cpp`（改 · +1 方法）

```cpp
void PaintContext::DrawArc(const Point& center, float radius,
                           float startAngleDeg, float sweepAngleDeg,
                           float width, const Color& color) {
    // 原样进命令：钳制/归一化封在生成器与后端（契约层只保证「参数被传递」）
    m_commands.emplace_back(DrawArcCommand{ center, radius, startAngleDeg,
                                            sweepAngleDeg, width, color });
}
```
★ **不做剔除**：`Widget::Paint` 的 `self` 判据基于**控件矩形**（B6 同族），`RingProgressBar` 的 bbox 就是整环外接方 ⇒ 无新增剔除面（初设 §4.3 结论维持）。

### △11 `ECDI/include/ECDI/Theme/RingProgressBarStyle.h`（🆕）+ `Theme.h` / `DefaultTheme.h/.cpp`（改）

```cpp
struct RingProgressBarStyle{
    StyleField<Color> trackColor;
    StyleField<Color> fillColor;
    StyleField<float> thickness;   ///< 环宽（px；<= 0 ⇒ 不绘制——沿 C33-4）
};
struct RingProgressBarStyleOverride{
    std::optional<Color> trackColor;
    std::optional<Color> fillColor;
    std::optional<float> thickness;
};
```
- `Theme` 加**第 9 个纯虚** `virtual RingProgressBarStyle GetRingProgressBarStyle() const = 0;`
- `DefaultTheme` 实现（★ **默认值选灰阶，使「不设主题也能看见」**）：`trackColor = FromRGBA8(230,230,235,255)` · `fillColor = FromRGBA8(80,120,220,255)`（**与 `ProgressBarStyle::background` 同值**——同族控件同色，条 95① 的视觉一致性）· `thickness = 6.0f`。
- ★ **破坏面同步**：`src/Tests/ThemeTests.cpp:177-195` 的 `RedButtonTextTheme` 加一行 `RingProgressBarStyle GetRingProgressBarStyle() const override{ return DefaultTheme{}.GetRingProgressBarStyle(); }`。

### △12 `ECDI/include/ECDI/Widget/RingProgressBar.h` + `ECDI/src/Widget/RingProgressBar.cpp`（🆕）

**头**（沿 B24 的 `ProgressBar` 形态）：
```cpp
class RingProgressBar : public Widget{
public:
    RingProgressBar();                                   // 构造体注入 RingProgressBarStyle
    void SetProgress(float progress);                    // clamp [0,1]；NaN 拒绝；同目标 no-op
    void SetPercent(int percent);
    [[nodiscard]] float GetProgress() const noexcept{ return m_progress; }
    void ApplyTheme(const Theme& theme);
    void SetStyle(RingProgressBarStyleOverride override);
protected:
    void OnPaint(PaintContext& ctx, int x, int y) override;
    virtual AnimationManager* ResolveAnimationManager() const noexcept;   // 测试接缝（沿 ProgressBar 先例）
    float m_displayProgress = 0.0f;
    RingProgressBarStyle m_style;
private:
    void AnimateTo(float target);
    static constexpr int kTransitionMs = 200;
    static constexpr float kEpsilon = 1e-6f;
    float m_progress = 0.0f;
    AnimationToken m_animToken;
};
```
**`SetProgress` 的 NaN 处置（C33-10①）**：`if (!std::isfinite(progress)) return;` —— ★ **先于 `clamp`**（`std::clamp(NaN,...)` 的结果是实现定义的传播，必须先拒）。
**`OnPaint`**：
```
D = min(GetWidth(), GetHeight());   if (D <= 0) return;         // 尺寸退化 ⇒ 不发命令
R = D * 0.5f - thickness * 0.5f;    // 环中线半径
cx = x + GetWidth()*0.5f;  cy = y + GetHeight()*0.5f;           // ★ 圆心居中（宽高不等时取 min 边）
// ① 轨道：整环（sweep = 360）
ctx.DrawArc({cx, cy}, R, 270.0f, 360.0f, thickness, m_style.trackColor.value);
// ② 进度：0 进度 ⇒ sweep = 0 ⇒ 后端不画（命令仍发——命令流可断言，沿 ProgressBar 先例）
ctx.DrawArc({cx, cy}, R, 270.0f, m_displayProgress * 360.0f, thickness, m_style.fillColor.value);
```
★ **`R <= 0`（环宽 ≥ 直径）**：★ **不在此处早退** —— 两条命令照发，由 `NormalizeArc` 按 C33-4 判定（`radius <= 0` ⇒ 不画）。★ 理由：`NormalizeArc` 才是 C33-4 的唯一实现（**不在控件层复制退化规则**——否则两处规则会漂移，正是 C33-6 的控件层对应风险）。
★ **`D <= 0` 早退**则是必要的（`GetWidth()==0` 时连圆心都无意义）。

### △13 测试与注册（🆕 `ArcCoverageTests.cpp` + `RingProgressBarTests.cpp`；改 `RunAllTests.h/.cpp`）

- 新建 `ECDI/src/Tests/ArcCoverageTests.cpp`（T33-1..T33-9b，§5 正文）——**自带 `void ECDI::Test::RegisterArcCoverageTests()`**（★ **定义须全限定**，沿 `DesktopNestTests.cpp:652` 的既有教训：写全局作用域会定义出 `::RegisterArcCoverageTests` ⇒ LNK2019）。
- 新建 `ECDI/src/Tests/RingProgressBarTests.cpp`（T33-14..T33-18）。
- `RunAllTests.h` 加两行声明；`RunAllTests.cpp` 在 `:47` 后加两行 `Register...()`。
- ★ **CMake 零改动**（B22）。

---

## 3. 实现期必须解决的两处（初设未定，本稿定）

### 3.1 预过滤的**不开方**写法（△3 第 6 步的精确形态）

初设只写了「`distanceToArcCenterline > hw + √½` ⇒ 跳过」，但 `distanceToArcCenterline = |radial − radius|` 含 `sqrt`。**B9 的既有做法是不开方**（`DistSqToSegment` 返回平方，比较 `> skipRadius2`）。弧的困难：`|radial − radius|` 的平方比较要**开方或两侧判**。
★ **决策 PD33-H：两侧平方比较**（零开方，且天然处理负下界）：
```cpp
const float radialSq = dx*dx + dy*dy;
const float hi = radius + skipR;   const float lo = radius - skipR;
const bool outsideBand = (radialSq > hi*hi) || (lo > 0.0f && radialSq < lo*lo);
if (outsideBand) continue;
```
★★ **`lo > 0.0f &&` 这个短路是必须的**：当 `lo <= 0`（**扇形退化**：`radius ≤ skipR`）时，「`radialSq < lo²`」会把**圆心附近**误判为「带外」（因为 `lo²` 是正数）⇒ **整个扇形内部被跳过**。★ 该守卫正是「内半径非正」在**预过滤层**的表现——与 C33-4 的几何退化**同源**，必须在两处一致处理。★ 本稿新增**盯防④**专钉此点（§4）。

### 3.2 负扫角起点的**正负对称**（△1b 第 6 行的形态）

初设定了「`sweep < 0` 时用 `(start − a)`」，本稿把它落成**单表达式**（第 6 行）并给出**回归锚**：
- `sweep > 0` 且点恰在起始角 ⇒ `delta = 0 ⇒ t = 0` ⇒ **t 钳到 0 ⇒ 命中端点** ✓
- `sweep < 0` 且点恰在起始角 ⇒ `delta = (start − a) = 0 ⇒ t = 0` ⇒ **同样命中** ✓
★ **反例锚**（写进 T33-9 的用例注释）：初设 v1.0 的 `(360 − rel) ≤ −sweep` 在 `sweep = −90`、点恰在起点时给 `360 ≤ 90` = **假** ⇒ 起点被排除。★ 若有人改回那个形式，T33-9 的**起点归属**断言会立刻红。

---

## 4. 盯防清单（可机检）

| # | 盯防项 | 判据形态 |
|---|---|---|
| ① | ★ **两个角度不折 scale** | `Renderer.cpp` 的 `ExecuteCommand(const DrawArcCommand&)` 内**不得**出现对 `startAngleDeg` / `sweepAngleDeg` 的 `ScaleLength` 调用 |
| ② | ★★ **弧几何的唯一判据** | `ArcCoverage.cpp` 内**不得**出现 `atan2` / 独立的扇区比较；几何判定只能来自 `BackendContract::ArcSegmentDistanceSq` |
| ③ | ★ **`ArcSlot` 的键含角度与 `binarize`** | 键元组**必须**含 `QA(start)` / `QA(sweep)` / `binarize`——漏则**每帧新分配**（`LineSlot` 的 ~89 帧填满教训） |
| ④ | ★★ **预过滤的负下界守卫** | `ArcCoverage.cpp` 的两侧比较**必须**含 `lo > 0.0f &&` 短路（否则扇形退化时内部被整体跳过） |
| ⑤ | ★ **`RecordingBackend` 不归一化** | `RecordingBackend.cpp` 的 `DrawArc` 内**不得**调用 `NormalizeArc` |
| ⑥ | ★ **Clamp 宏防护** | 新增代码中 `min`/`max` 一律写 `(std::min)` / `(std::max)` |
| ⑦ | ★ **`Theme` 实现者同步** | `Theme` 加纯虚后，`DefaultTheme` 与 `RedButtonTextTheme` **都必须**有 `GetRingProgressBarStyle`——漏则编译失败（结构性保证） |
| ⑧ | ★ **`Register...Tests` 定义全限定** | 新测试文件的定义写 `void ECDI::Test::RegisterXxxTests()`（沿 `DesktopNestTests.cpp:649-651` 的 LNK2019 教训） |
| ⑨ | ★ **二值化不改 `grid`** | 两后端的二值化都必须在**局部拷贝**上做（`RasterizeMask` 吃同一份 `grid`） |

---

## 5. 用例正文（T33-1..T33-18；★ 自动化 **+18** ⇒ **445 → 463**）

> ★ **计数口径**：T33-1..T33-18 共 18 条（其中 T33-7b/9b 是本稿新增的**并列条目**，已计入编号）；**人工观感项不计数**（沿 Phase 24 T24-10 的口径勘误先例）。

### 5.1 生成器（`ArcCoverageTests.cpp`，全部无头）

| # | 名称 | 判据（精确值/表达式） |
|---|---|---|
| T33-1 | `Arc.CoverageValueRangeAndBounds` | 全像素 `0 ≤ c ≤ 255`；`originX == floor(cx−pad)` · `width == floor(cx+pad) − originX + 1`（`pad = radius + hw + 1`） |
| T33-2 | `Arc.AreaConservationFourTiers` | **四档解析参照**（C33-5；`r=100, w=10` ⇒ `hw=5`）：<br>`\|sweep\|=90` ⇒ `A = 1570.796 + 78.540 = 1649.336`<br>`180` ⇒ `3141.593 + 78.540 = 3220.132`<br>`270` ⇒ `4712.389 + 78.540 = 4790.929`<br>`360` ⇒ `6283.185`（**圆帽不追加**）<br>判据：`\|Σ(c/255) − A\| / A ≤ tol`；★ **`tol` 实测后冻结**（沿 Phase 24 的 `≤0.02 / ≤0.03` 形态），**不由公式推导** |
| T33-3 | `Arc.MirrorSymmetry` | `start=0,sweep=90` 与 `start=270,sweep=-90`（关于 x 轴镜像）在对应像素上**覆盖率相等**（逐点；容差 = 0） |
| T33-4 | `Arc.CrossZeroOverlap` | `start=350,sweep=20` 与 `start=10,sweep=20` 的**重叠角域**（10°–10°+20° 之外… 取 350–360 段）覆盖率逐点一致；且该弧**非空**（不因跨 0° 而截断） |
| T33-5 | `Arc.FullCircleSeamAndSign` | `sweep=360`：`0°` 邻域**无零覆盖率列**（接缝防线）；★ `sweep=+360` 与 `sweep=-360` **逐位相同** |
| T33-6 | `Arc.RoundCapDiscriminant` | 判别性采样点：`sweep=90` 时，**端点圆帽内、径向带 ∩ 扇区之外**的点覆盖率 **> 0**（平头在此恒 0）；★ 反向对照：同点沿切向外移 `hw+1` ⇒ **0** |
| T33-7 | `Arc.DegenerateEmptyTable` | `radius<=0` / `width<=0` / `sweep==0` / `NaN` / `+Inf` / `-Inf` ⇒ **全部 `Empty()`**；`\|sweep\|=450` ⇒ **逐位等于** `sweep=360` 档 |
| T33-7b | `Arc.FanDegenerate` | `radius=4, width=10` ⇒ `effW=lround(10)=10` ⇒ **`hw = min(10*0.5, 4) = 4`**（★ 由 `radius` 钳制，**不是** 5）：① `ArcSegmentDistanceSq(圆心) == radius² = 16 ≤ hw² = 16` ⇒ **圆心在带内**（★ 等号成立 ⇒ 本档正好是 `radius == hw` 的**临界**）；② `\|sweep\|=90` ⇒ `A = θ_rad·(radius+hw)²/2 = θ_rad·8²/2 = ` **`50.265`**；`360` ⇒ **`201.062`**（**按解析面积**判，**不**断言「中心像素=255」）<br>★ **另取一档严格 `<`**（`radius=2, width=10` ⇒ `hw=2`）：`\|sweep\|=90` ⇒ `θ_rad·4²/2 = ` **`12.566`**；`360` ⇒ **`50.265`**<br>★ **算式核对留痕**：本稿初版把这两档写成 `hw=5` 的值（`63.617`/`254.469`）——**与本节自己的 `hw = min(effW*0.5, radius)` 规则矛盾**，已按 `A = θ_rad·(radius+hw)²/2` 重算；两个极端（`radius == hw` 与 `radius << hw`）都入档 |
| T33-8 | `Arc.Determinism` | 同输入两次：`width/height/originX/originY/coverage` **逐位相同** |
| T33-9 | `ArcSegmentDistance.TruthTable` | 纯函数真值表（**生成器实际调用它** ⇒ 测即生产判据）：<br>★ **起点归属**：点恰在起始角 ⇒ `dist² ≈ 0`（`sweep = +90` 与 `−90` **都**成立——**负扫角订正的回归锚**）<br>★ 跨 0° · ★ 整圆（`dist² = (radial−radius)²`）· ★ **圆心**（`radius²`，`radius ≤ hw` 时 `≤ hw²`）· ★ 端点圆帽邻域 |
| T33-9b | `Arc.NormalizeArcTruthTable` | `NormalizeArc` 全表：`drawable` 的六种 false 条件 · `sweep` 钳制（`450 → 360`、`-450 → -360`）· `hw = min(effW*0.5, radius)`（★ 含 `radius=4, width=10 → hw=4`） |

### 5.2 命令层（无头）

| # | 名称 | 判据 |
|---|---|---|
| T33-10 | `ArcPaint.CommandPassthrough` | `PaintContext::DrawArc` ⇒ 命令流 **+1**，且 6 个字段**逐字段等于入参** |
| T33-11 | `RecordingBackend.ArcRawParams` | 传 `sweep=450` ⇒ 记录**原值 450**（★ C33-9 的可机检证据）；`start=-30` 亦原样 |
| T33-12 | `Renderer.ArcScaleFolding` | `scale=1.5`：`center`/`radius`/`width` **已折**；★ **`startAngleDeg`/`sweepAngleDeg` 逐位未变**（盯防①） |
| T33-13 | `Theme.RingProgressBarStyleContract` | `DefaultTheme::GetRingProgressBarStyle()` 三字段非空默认；★ `RingProgressBarStyleOverride` 的 `Set` 后 `ApplyTheme` 不覆盖（`StyleField` 契约） |

### 5.3 控件（无头 + 真窗口）

| # | 名称 | 判据 |
|---|---|---|
| T33-14 | `RingProgressBar.ValueContract` | `SetProgress`：`-1 → 0` · `2 → 1` · **`NaN ⇒ 值不变且不启动动画`** · 同目标 no-op |
| T33-15 | `RingProgressBar.AnimationReentrant` | 过渡中再 `SetProgress` ⇒ 从**当前呈现值**重新过渡（沿 B24 的替换式重启；用替身 manager 观测 `from`） |
| T33-16 | `RingProgressBar.PaintCommands` | `OnPaint` ⇒ **恒 2 条** `DrawArc`（轨道 `sweep=360` + 进度 `p*360`）；`p=0` ⇒ **仍 2 条**（第二条 `sweep=0`） |
| T33-17 | `RingProgressBar.SizeStrategy` | `SetSize(200,100)` ⇒ 直径 = **100**、圆心在 `(100, 50)`（命令流字段断言）；`SetSize(0,0)` ⇒ **0 条命令** |
| T33-18 | `RingProgressBar.WindowedPaintE2E` | 真窗口 `p=0.75`：进度弧**像素可见**（采样环上 270° 处 `≠` 轨道色；沿 T24-8b 的端到端形态） |

★ **人工观感项（不计数，如实登记）**：AA 边缘平滑度 · 圆帽观感 · 环宽与半径比例 · 跨 DPI 清晰度（**GL 无 WGL context ⇒ 结构上不可自动化**，沿 B19 头注释②的既有结论）。

---

## 6. 四批实现顺序（检查点制，沿初设 §8）

| 批 | 内容 | 检查点 |
|---|---|---|
| **批一** | △1 `BackendContract` 弧原语 · △2/△3 `ArcCoverage` · △13 的 `ArcCoverageTests.cpp` + 注册 | ★ **零公共头改动**；T33-1..9b 全绿（无头） |
| **批二** | △4 `RenderingBackend` 纯虚 · △5 `RenderCommand` · △6 `Renderer` · △10 `PaintContext` · △9 `RecordingBackend` + T33-10..12 | ★ **3 个实现者必须同步**（漏 = 编译失败，结构性） |
| **批三** | △8 GDI · △7 GL · △11 样式 + `Theme` + 替身 · △12 控件 + T33-13..18 | 真窗口像素 + 观感 |
| **批四** | `PaintProbe` 加转发 override + 验证（转发/计数/计时） | harness 重建 + 读数 |

---

## 7. 影响面（实施目标）

| 项 | 目标值 |
|---|---|
| 公共头 | **96 → 98**（`RingProgressBarStyle.h` + `RingProgressBar.h`；`RenderingBackend.h`/`RenderCommand.h`/`PaintContext.h`/`Theme.h` 为**既有头改动**） |
| 公共 API | +1 纯虚（`DrawArc`）· +1 命令 · +1 `PaintContext` 方法 · +1 样式类型 +1 override 结构 · +1 `Theme` 纯虚 · +1 控件（5 方法） |
| 新建文件 | **5**（`ArcCoverage.h/.cpp` · `RingProgressBarStyle.h` · `RingProgressBar.h/.cpp`）——★ 测试 2 个另计 |
| 改动文件 | **8**（`BackendContract.h` · `RenderingBackend.h` · `RenderCommand.h` · `Renderer.h/.cpp` · `PaintContext.h/.cpp` · `GDIBackend.h/.cpp` · `GLRenderer.h/.cpp` · `RecordingBackend.h/.cpp` · `Theme.h` · `DefaultTheme.h/.cpp` · `ThemeTests.cpp` · `RunAllTests.h/.cpp`） |
| ★ CMake | **0 改动**（B22 的 `GLOB_RECURSE CONFIGURE_DEPENDS`——★ 初设 §6 列为「改动」**有误**，本稿订正） |
| 用例 | **445 → 463**（+18） |
| 既有行为 | 6 个既有原语语义**零变化**；`RingProgressBar` 为新控件 ⇒ 无既有场景 |

---

## 8. 开放项收口（O33-1..5）

| # | 收口 |
|---|---|
| **O33-1** 采样密度 | ✅ **收口**：GDI 用 `m_cornerMaskCache.GetSamples()`（△8 第 2 步，与 `DrawLineCoverage` 同源）；★ **GL 用固定 8** —— 因为 `GLRenderer` **没有**角缓存旋钮（`CornerSlot` 直接用 `GenerateCornerMask`），沿其既有形态；★ **两端差异如实登记**（GDI 可调、GL 固定）——与 `DrawLine` 的既有一致性状态**相同**（GL 侧 `GenerateLineCoverage` 亦用默认 8，`GLRenderer.cpp:300`）⇒ **不是本 Phase 新引入的分歧** |
| **O33-2** `ArcSlot` 上限 | ✅ **收口**：暂不设上限；键含角度后基数受限，若实测超 32 条再移植 `CornerMaskCache` 的诊断阈值 |
| **O33-3** 类型名中性化 | ✅ **不做**（L33-4 命名债维持） |
| **O33-4** GDI legacy 弧 | ✅ **不做**（L33-1 维持） |
| **O33-5** `SetReversed` | ✅ **不做**（无消费者） |

---

## 9. 局限（沿初设 L33-1..5 + 本稿新增 L33-6）

| # | 局限 |
|---|---|
| L33-1..5 | 沿初设原文生效（GDI 无 legacy 弧降级 · `\|sweep\|>360` 钳制 · 扇形退化 · 类型命名债 · AA 无自动化验收） |
| **L33-6**（🆕） | ★ **图集满时弧不画**：GL 的 `ArcSlot` 分配失败 ⇒ 二值化重试 ⇒ 仍失败则 **return + 一次 Warning**（★ **不得**退回 `SolidSegment` 直线——C33-7）。⇒ 与 `DrawLine`（会降级画直线）**行为不同**，如实登记为**有意的能力边界** |

---

## 10. 实施回填（模板，实施后逐批填写）

| 项 | 回填内容 |
|---|---|
| **偏差** | 实际实现与本设计的逐条偏差（含勘误编号 E33-x） |
| **测试** | T33-1..18 结果 + 运行链（MinGW / Clang / ClangCL / MSVC）+ 用例总数（目标 463） |
| **验证** | 四链读数 · 真窗口 E2E 读数 · 面积守恒实测容差（冻结值） |
| **限制** | L33-1..6 的最终状态 |
| **台账** | 本文件 §10 + `framework-defect-audit` + `roadmap-deferred` + `docs/README` + 根 README |

---

## 11. 修订记录

- **v1.0**（2026-10-14）初稿。**输入**：需求 **v1.2** · 初设 **v1.1**。**内容**：§1 基线 B1–B24（★ **B22 订正初设的一处影响面错误**：CMake 用 `GLOB_RECURSE CONFIGURE_DEPENDS` ⇒ 新增文件**零 CMake 改动**）· §2 逐文件 △1–△13（新建 5 + 改动 8）· §3 初设未定的两处（**PD33-H 预过滤的开方问题** ⇒ 两侧平方比较 + `lo > 0` 守卫；负扫角的正负对称单表达式 + 反例锚）· §4 盯防九条（全部可机检）· §5 用例 T33-1..18 精确断言（含**面积四档手算值**与**负扫角起点回归锚**）· §6 四批 · §7 影响面 · §8 O33 全收口 · §9 新增 **L33-6** · §10 回填模板。**待评审。**
