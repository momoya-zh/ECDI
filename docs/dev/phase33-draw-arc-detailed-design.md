# Phase 33 圆弧绘制原语（DrawArc）与环形进度控件（RingProgressBar）—— 详细设计

> **状态**：v1.1（2026-10-16）——★★ **吸收第三轮外部评审（对 v1.0 的「有条件通过」；4×P1 + 3×P2）**：**四项 P1 全部成立、全部采纳**（① **12 行规格**（v1.1 重写为 **15 行**，批一补 **E33-2** 后为 **16 行**）**两处负号配套反了** ⇒ 整条弧沿水平直径镜像 · ② 同一规格**把起点侧圆帽整片切掉** · ③ GDI 二值化数据流不闭合 · ④ GL `ArcSlot` 签名 / 键元数 / 键清单三处互相矛盾）；**P2**：T33-4 配对选错（已改）· 极大有限值的**数值安全带**落地（新 §3.3）· 版本链一项**如实反驳**（评审读到的是过期页面——本机实测声明版本与 `HEAD` 逐位一致）；**✅ 已实现（2026-10-16）——批一–批四全部落盘**：三链（MinGW / Clang / ClangCL）**465/465** 全绿 + **MSVC Release 链 465/465**（= 445 基线 + 20 条设计目标，证据见 `docs/dev/README.md` 的 Phase 33 段与 §10/§11 勘误回填）。★ 原状态行记「**待复核**」——该措辞保留于此，实际已于 2026-10-16 实现收口。
> **输入**：需求稿 `phase33-draw-arc-requirements.md` **v1.3**（C33-1..10 契约 + R1–R9）· 初设稿 `phase33-draw-arc-preliminary-design.md` **v1.2**（PD33-A..G + T33-1..18 大纲 + O33/L33）
> **模板**：沿 Phase 24 详设的十一节形态（同族最近先例：新覆盖度生成器 + 两后端消费 + 内部头）
> **定位**：把初设的决策落成**逐文件、逐函数的实现规格**与**精确断言**；契约不重开，只在实现层补足初设未定的数值与形态。
> ★★ **v1.1 核心变化**：**12 → 16 行规格重写**（两处负号改回契约约定 + `ang` 归约 + **越界时吸附到「圆周角差更小」的端点**——旧规格把**起点侧圆帽整片切掉**，实测误判占带内子采样 `r=10,hw=2,|sweep|=90°` **8.40%** · `|sweep|=20°` **23.85%** · `r=4,hw=4` **25.02%**）· **预过滤补 `lo > 0.0f &&`**（修订一处**假断言**）· **GL 键 10 → 11 元**（补 `originX/originY`）· **GDI 命名 `rasterGrid` / `raster` 并把二值化拷贝喂给光栅化器**（数据流闭合）· **T33-3 / T33-4 / T33-6 / T33-7b / T33-9 断言订正** · **新增 §3.3 数值安全带**（沿 Phase 24 O2 的 2M 像素 one-shot 先例）。

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
    bool oversized = false;     ///< ★ v1.1：仅当因「极大有限值」被拒时为 true（§3.3 N2）
};

/// @brief 规范化（无效判定 + 数值安全带 + 扫角钳制 + 半宽钳制）—— C33-4 全表
/// @details 判定顺序（★ 顺序即契约，逐条对应 C33-4）：
///   ① 任一浮点入参 `!isfinite` ⇒ 不可绘制（**先于一切算术**——防 NaN 传播进 fmod/lround）
///   ② `radius <= 0` ⇒ 不可绘制（**不取绝对值**——不静默改变输入语义）
///   ③ `width <= 0` ⇒ 不可绘制（沿 DrawLine 家族）
///   ④ `sweepDeg == 0` ⇒ 不可绘制
///   ⑤ `sweepDeg = clamp(sweepDeg, -360, 360)`（超一周**钳制**，不是多圈）
///   ⑥ ★★ **数值安全带（v1.1 新增，见 §3.3）**：`radius`/`width` 超 `kMaxArcExtent`、或
///      `|cx|`/`|cy|` 超 `kMaxArcCoord` ⇒ 不可绘制**且** `oversized = true`
///      （**必须先于 ⑦ 的 `lround`**——`lround` 对超范围浮点是「未指定值 + 可能置 FE_INVALID」，
///       且 bbox 的 `floor` 结果要装进 `int`）
///   ⑦ `effW = max(1, lround(width))`；`hw = min(effW * 0.5f, radius)`（★ **内半径非正 ⇒ 退化为扇形**）
///   ⑧ `startDeg` **不归约**（保留原值——归约在 `ArcSegmentDistanceSq` 内部按 mod 360 做；
///      理由：**记录件**要用原值（C33-9）；★ **缓存键**用 `Mod360` 后的值（△7，见 §3.3 N4））
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
 5  a = atan2f(dy, dx) * (180/π)             // ★★ **不取负**：屏幕 y 向下时「顺时针为正」（C33-2）
 6  delta = (p.sweepDeg >= 0) ? (a - p.startDeg) : (p.startDeg - a)     // ★ 正负同一规则
 7  delta = fmodf(delta, 360);  if (delta < 0) delta += 360              // Mod360（禁 int 截断）
 8  sw = fabsf(p.sweepDeg)
 9  if (delta <= sw)
10      s = delta                                     // 落在弧内 ⇒ 取径向投影
11  else
12      s = ((360 - delta) < (delta - sw)) ? 0.0f : sw // ★★ 越界 ⇒ 吸附「圆周角差更小」的端点
13  ang = p.startDeg + sign(p.sweepDeg) * s
14  ang = fmodf(ang, 2π);  if (ang < 0) ang += 2π     // ★★ 归约到 [0,2π)（批一实现期补，见下）
15  qx = p.cx + p.radius * cosf(ang * π/180);  qy = p.cy + p.radius * sinf(ang * π/180)
16  return (x-qx)^2 + (y-qy)^2
```

★★ **与 v1.0 的第一处关键差异（评审第三轮 §2：两处负号必须成对改）**：v1.0 写 `a = atan2f(-dy, dx)` 与 `qy = p.cy - p.radius * sinf(...)`——**两处负号配套反了**。★ 该组合**内部自洽**（中心线往返仍是 `0`）、**形状与面积都不变**，故**镜像类与面积类用例照样通过**，但它实现的是「**逆时针为正**」⇒ **整条弧沿水平直径镜像**（`RingProgressBar` 传 `start=270°`、设计意图 12 点钟，**实测落在 6 点钟**）。★ **为什么既有用例抓不到**：`|sweep| = 360°`（整圆）与 `start = 350°, sweep = 20°`（关于水平直径对称）这两种弧**镜像后等于自己**（实测中心线往返误差 `0.000 px`）⇒ **只有「基本方位真值表」能钉住方向**（C33-2 的 v1.3 收口要求；T33-9 已加该断言）。
★★ **第 9–12 行：越界必须吸附到「圆周角差更小」的端点（v1.1 新增；本轮最重的一处）**：v1.0 只写 `t = clamp(delta / |sweep|, 0, 1)` ⇒ 点落在**起点之前**时 `delta` 绕到 ~355°、`t` 被钳到 **1**，于是返回的是**到终点**的距离 ⇒ **起点侧圆帽被整片切掉**，而终点侧完好。实测（`r=10, hw=2`，与「弧上 4001 点暴力取最小」的独立参考逐子采样比对，格点 107 584 个）：`|sweep|=90°` 误判 **406 / 4831 = 8.40%** · `|sweep|=20°` **406 / 1702 = 23.85%** · `r=4,hw=4,|sweep|=90°` **1614 / 6451 = 25.02%** · `|sweep|=180°` **4.59%** · `|sweep|=360°` **0**（无端点）。★ **修法等价性**：「圆周角差更小」与「到两端点的欧氏距离更小」在圆上**恒等价**（二者只差一个单调的 `cos`），且比较**排在 `sqrt` 之前** ⇒ 仍是零开方风格。★ 修后**六档全部 0 误判**。
★★ **第 14 行的 `ang` 归约（★ 批一实现期补入；E33-2）**：`cos/sin` 以 360° 为周期 ⇒
**几何语义零变化**，但归约让「**同一弧段的等价写法**」**逐位相同**：不归约时 `(270,+90)` 的终点与
`(0,-90)` 的起点分别算出 `ang = 360°` 与 `ang = 0°`，两者 `cos` 的**浮点舍入差**会让 T33-3 的
「同弧别名」断言在**共享端点处的采样点**上偶发失败——那会把「等价类」降级成「近似等价」。
★ 实测：补入后 `(270,+90)` vs `(0,-90)` vs `(-90,+90)` 三写法**逐位相同**（差 0 格）。
★ 本行是**批一落盘时**发现的规格缺口（v1.1 的 15 行未含它），故如实登记为勘误 **E33-2**
（E33-1 = 二值化对图集满无帮助）。
★ **中心线往返（第 5 行与第 14 行必须成对的可机检判据）**：弧段上取参数 `t` 的点，`ArcSegmentDistanceSq` **必须为 0**。实测 v1.0 的两种写法在 `r=10` 上分别偏到 **14.139 px** 与 **20.000 px**，修后 **0.000 px** ⇒ 该断言已写进 T33-9。
★ **第 2–4 行的圆心特判**：`radial ≈ 0` 时 `atan2` 无定义，而几何上「圆心到弧段的距离」**恒等于 `radius`**（弧段上任一点距圆心都是 `radius`）⇒ 直接返回 `radius²`，**圆心归属由距离比较自动决定**（`radius ≤ hw` 时命中），全程不调 `atan2(0,0)`。
★ **`|sweep| = 360°` 无需特判**：`delta ∈ [0,360)` 且 `sw = 360` ⇒ 第 9 行**恒真** ⇒ `s = delta` ⇒ `q` 即 `p` 的径向投影 ⇒ 判据自然成为「到整圆的距离」`|radial − radius|`（C33-3②）。★ 初设 §2.3 纪律④曾建议「`|sweep| == 360°` 时短路为 `|radial − radius| ≤ hw`」——**该短路是冗余的**（本式已等价，实测整圆档 0 误判）；v1.1 采「单一式子」口径，**不设第二条路径**（少一条路径 = 少一处可漂移的语义），**初设稿已同步删除该短路**。

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
          lo     = radius - skipR
          if (radialSq > (radius + skipR)^2 || (lo > 0.0f && radialSq < lo*lo))
              continue;                                             // 整格带外
          hit = 0
          for sj, si: 子采样点 (sx, sy)
              if (BackendContract::ArcSegmentDistanceSq(p, sx, sy) <= hw2) ++hit
          coverage[j*bw + i] = lround(255 * hit / (samples*samples))
7  return grid;
```

★★ **负下界守卫（`lo > 0`）是必须的，且 v1.0 的理由写错了（评审第三轮复核发现）**：v1.0 称「`(radius − skipR) < 0` ⇒ 平方后 `radialSq < (radius−skipR)²` **恒假**」——**该断言是假的**：负数的平方是**正数**，`radialSq < lo²` 对**圆心附近**的格子**成立**，会把扇形内部整片跳过。实测（`lo = −0.7071`、`lo² = 0.5`，格点 107 584）：`r=4,hw=4,|sweep|=90°` 误跳带内子采样 **72** 个 · 同档 `|sweep|=360°` **96** 个 · `r=2,hw=2` **68** 个；加 `lo > 0.0f &&` 后**全部 0**。★ 该守卫正是「内半径非正」在**预过滤层**的表现（与 C33-4 的几何退化**同源**），故本节与 §3.1 与盯防④**三处必须一致**（v1.0 只在 §3.1 写了守卫，△3 的伪码里漏了——本稿补齐）。
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
GlyphSlot ArcSlot(const LineCoverageGrid& grid, const BackendContract::ArcParams& p, bool binarize);
```
成员：`std::map<std::tuple<int,int,int,int,int,int,int,int,int,int,int>, GlyphSlot> m_arcSlots;`
★ **键 = 11 元组**：`(grid.width, grid.height, grid.originX, grid.originY, Q8(cx), Q8(cy), Q8(radius), QA(start), QA(sweep), Q8(width), binarize)`。★★ **v1.1 订正三处（评审第三轮 §4）**：① 签名由 **2 参改 3 参**（v1.0 的声明与自己的调用点 `ArcSlot(grid, p, binarize)` 不一致）② 元组元素数与键清单**统一为 11**（v1.0 声明 10 个 `int`、清单列 10 项又写 `colorKey` 不入 ⇒ 实为 9 项，三处互不相符）③ ★★ **补入 `originX/originY`**——理由：键里的几何量是**量化**的（`Q8` = 1/8 px、`QA` = 1/8°），两次调用**可以同键而 bbox 原点差 1 px**（`floor(cx ± pad)` 在量化窗内跨整数），而位图是**按网格局部坐标**栅格化的 ⇒ **原点不同 = 位图内容不同**，必须进键，否则「同键 ⇒ 同位图」不成立（那会把缓存的弧画偏 1 px）。★ **`LineSlot` 的键没含原点**——那是既有形态，**同类 1 px 风险**如实登记为**范围外观察**（不在本 Phase 处置，见 §11）。
（覆盖度是**与颜色无关的几何量**，`Upload` 的是 A8 覆盖度、着色在 `Shade`——与 `LineSlot` 一致）。★ **`QA(start)` 用 `Mod360(startDeg)` 后的值**：既免疫超范围入参的 `lround`（见 §3.3 N4），又让 `10°` 与 `370°` 命中**同一槽位**（两者几何等价——`Mod360(a−10)` 与 `Mod360(a−370)` 恒等）。

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
                grid.originX, grid.originY, grid.width, grid.height);
```
★ **`binarize` 的实现位置**：在 `ArcSlot` 内 `Upload` **之前**把 `grid.coverage` 拷成局部 `std::vector`，逐字节 `>0 ? 255 : 0`。★ **不改 `grid` 本身**（它是值参、但拷贝代价大；且**不可**污染调用方语义）。★★ **批二实测订正（E33-3）**：**GL 侧根本没有 AA 开关**——`GLRenderer` 既无 `m_antiAliasing` 类成员、也无 `SetAntiAliasing`（grep 全文件零命中）；它**全部绘制都走图集覆盖度**，AA 是它的**唯一**形态。⇒ **PD33-C（关 AA ⇒ 二值化）在 GL 侧无对应物**，GDI 侧那句 `if (!m_antiAliasing)` 照写，GL 侧必须传 `binarize = false`。★ 这与 E33-1 是同一条链上的两段：初设 PD33-C 是把 GDI 的开关语义**推广**到两端，而 GL 端**从来没有这个开关**。⇒ `binarize` 在 GL 的**唯一真实用途 = 图集满重试**（见下）。
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
5  LineCoverageGrid binary;                      // ★ 二值化只在**局部拷贝**上做
6  const LineCoverageGrid* raster = &grid;        //   raster = 「真正要光栅化的那一份」
7  if (!m_antiAliasing) { binary = grid; 逐字节 >0?255:0（作用于 binary）; raster = &binary; }
8  if (!m_patchSurface.Ensure(m_memoryDC, raster->width, raster->height)) {
       Logger::Log(Warning, L"GDIBackend: patch surface unavailable - arc skipped");
       return;                       // ★ L33-1：无 legacy 弧降级（有意）
   }
9  RasterizeMask((uint8_t*)m_patchSurface.bits, m_patchSurface.width * 4, *raster, color);
10 BlendPatch(m_memoryDC, raster->originX, raster->originY, raster->width, raster->height);
```
★★ **第 5–7 步的数据流必须闭合（v1.1 订正，评审第三轮 §3）**：v1.0 第 5 步把二值化做在一份**未命名的局部拷贝**上，第 7 步却仍传**原 `grid`** ⇒ 照字面实现则**二值化被静默丢弃**（关 AA 的语义静默失效）。★ 修法 = **给拷贝命名（`binary`）并让 `raster` 指针指向它**，光栅化与 `BlendPatch` 一律吃 `*raster`。★ **不改 `grid`** 的红线保持不变：与 GL 同理——**不改 `grid`**（值语义但拷贝贵）。

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

### 3.3 数值安全带（v1.1 新增——评审第三轮 P2-6 的落点）

C33-4 末条承诺「极大 `radius`/`width` ⇒ 几何网格超安全尺寸时**不画 + 一次 Warning**」，但 v1.0 的 `NormalizeArc` 与 `ArcCoverage` 七步**都没有落点**（全程只有 `!isfinite` 一关）⇒ 本轮补齐。形态**沿 Phase 24 O2 的既有先例**（`docs/dev/phase24-line-antialiasing-detailed-design.md:166`：2M 像素阈值 · **面积用宽类型算**防诊断自身溢出 · `bool m_areaWarned` one-shot 成员；实现见 `ECDI/src/Render/GDIBackend.cpp:1460-1466`）。

| # | 落点 | 判据 |
|---|---|---|
| **N1** | `BackendContract.h` 三个常量 | `inline constexpr float kMaxArcExtent = 1.0e6f;`（`radius`/`width` 上界）· `kMaxArcCoord = 1.0e6f`（圆心坐标上界）· `inline constexpr long long kMaxArcPixels = 2'097'152;`（沿 O2 的 2M 像素） |
| **N2** | `NormalizeArc` 第 ⑥ 步 | 任一超界 ⇒ `drawable = false;` **且** `oversized = true;`（`ArcParams` 新增该字段）⇒ 后端据此记**一次** Warning（沿 `m_fullWarned` 的 one-shot 形态） |
| **N3** | `GenerateArcCoverage` 第 4 步 | bbox 的 `floor` 结果**先装进 `double` 再判**，`bw`/`bh` 用 **`long long`** 算，`bw * bh > kMaxArcPixels` ⇒ 返回 `{}`（**先判后分配**，绝不让 `coverage.assign` 吃到溢出的尺寸） |
| **N4** | GL 键量化（△7） | `Q8(cx/cy/radius/width)` 与 `QA(sweep)` 的入参已被 N1/N2 限定在安全域；`QA(start)` 用 **`Mod360(startDeg)`** 后的值（既免疫超范围 `lround`，又让 `10°` 与 `370°` 命中同一槽位——两者几何等价） |

★ **为什么 `oversized` 要做成字段、而不是各后端自判**：C33-4 是「唯一实现」契约（△1a），拒绝原因若在两处各判一次就**违反了它**；字段化让「不画 + 一次 Warning」**可测**（T33-9b）。
★ **为什么位置必须在 `lround` 之前**：`lround` 对超出 `long` 表示范围的浮点数是「未指定值 + 可能置 `FE_INVALID`」（C++ [c.math]）⇒ **先钳后取整**；`floor(cx ± pad)` 同理（结果要装进 `int`）。
★ **与 `!isfinite` 的分工**：① 管 NaN/±Inf（**算术之前**），⑥ 管**极大有限值**（**取整之前**）——两者**不可互相替代**。

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
| ⑧ | ★ **`Register...Tests` 定义全限定** | 新测试文件的定义写 `void ECDI::Test::RegisterXxxTests()`（沿 `DesktopNestTests.cpp:649-651` 的 LNK2019 教训）。★★ **同族第二坑（批一实测补入）**：**VS/MSBuild 链新增 `Tests/*.cpp` 后可能只「重新生成项目」而不「编译该文件」**——症状 = `LNK2019: 无法解析的外部符号 "void __cdecl ECDI::Test::RegisterArcCoverageTests(void)"`，而 `.vcxproj` 里**该项存在且良构**（`ClCompile Include=...ArcCoverageTests.cpp`）。**根因** = `ecdi_tests.dir\Debug\ecdi_tests.tlog\**Cl.items.tlog**` 是 MSBuild 的**跨构建项缓存**，它沿用了 CMake 重新生成 `.vcxproj` **之前**的项清单 ⇒ **只链接、不编译**（实测该 tlog 里无 `ArcCoverageTests`、有 `LineCoverageTests`；`ecdi_tests.dir\Debug\` 下 38 个 `*Tests.obj` 独缺它）。**修法** = 删掉该目标的 `Cl.items.tlog`（必要时连 `unsuccessfulbuild`）后重建 ⇒ 该文件立即参与编译、链接通过。★ **判据（一眼分辨「代码错」与「构建目录陈旧」）**：`obj` 文件**根本不存在** ⇒ 陈旧跟踪；`obj` 存在却报符号缺失 ⇒ 才是真符号问题。★ **Ninja 链不受影响**（三个 ninja 目录的 `obj` 均正常生成）——所以这个坑**只在 VS 链出现**，容易误判成「MSVC 才有的代码错」 |
| ⑨ | ★ **二值化拷贝必须被消费** | 两后端的二值化都在**局部拷贝**上做，**且光栅化 / 上传必须吃那份拷贝**（GDI 传 `*raster` · GL 在 `ArcSlot` 内 `Upload` 拷贝）——★ **「只拷贝不消费」= 二值化静默失效**（v1.0 的 △8 伪码就是这样，见该节注） |

---

## 5. 用例正文（T33-1..T33-18；★ 自动化 **+20** ⇒ **445 → 465**）

> ★ **计数口径**（★ **批一落盘后实测订正**，条 29③c「计数类断言必须落盘后实测」）：本表共 **20 行**编号条目 = `T33-1..T33-18`（18 个号）**加**两个并列号 `T33-7b` / `T33-9b`；**「编号数」≠「注册数」也≠「条目数」**（条 99 的口径纪律）。三批的实际注册数 = **批一 11**（`T33-1..T33-9b`：`Arc.*` 9 条 + `ArcSegmentDistance.TruthTable` + `Arc.NormalizeArcTruthTable`）· **批二 4**（`T33-10..13`）· **批三 5**（`T33-14..18`）= **20** ⇒ `445 + 20 = **465**`。★ v1.0 曾写「共 18 条 ⇒ +18 ⇒ 463」——**与本节表格的行数不符**（漏算了两个并列号的注册）。**人工观感项不计数**（沿 Phase 24 T24-10 的口径勘误先例）。

### 5.1 生成器（`ArcCoverageTests.cpp`，全部无头）

| # | 名称 | 判据（精确值/表达式） |
|---|---|---|
| T33-1 | `Arc.CoverageValueRangeAndBounds` | 全像素 `0 ≤ c ≤ 255`；`originX == floor(cx−pad)` · `width == floor(cx+pad) − originX + 1`（`pad = radius + hw + 1`） |
| T33-2 | `Arc.AreaConservationFourTiers` | **四档解析参照**（C33-5；`r=100, w=10` ⇒ `hw=5`）：<br>`\|sweep\|=90` ⇒ `A = 1570.796 + 78.540 = 1649.336`<br>`180` ⇒ `3141.593 + 78.540 = 3220.132`<br>`270` ⇒ `4712.389 + 78.540 = 4790.929`<br>`360` ⇒ `6283.185`（**圆帽不追加**；★ v1.1 订正**标签**：= `4π·radius·hw`，**不是** `2π·radius·hw`——后者 = `3141.593`，**差 2×**）<br>★ 四档**都在 C33-5 的有效域内**（该档界 = `360° − 2·arcsin(0.05) = 354.27°` ≥ `270°`）<br>判据：`\|Σ(c/255) − A\| / A ≤ tol`；★ **`tol` 实测后冻结**（沿 Phase 24 的 `≤0.02 / ≤0.03` 形态），**不由公式推导** |
| T33-3 | `Arc.MirrorSymmetryAndSameArcAliasing` | ★★ **v1.1 订正（评审第三轮附带查出）**：v1.0 拿 `start=270,sweep=-90` 当「关于 x 轴的镜像」——**它不是镜像，是 180° 旋转**（实测：镜像比对差 **8038** 格，而 180° 旋转比对差 **0**）。**现行两条**：<br>① **镜像对**：`start=0,sweep=90` 与 `start=270,sweep=90`（等价写法 `start=0,sweep=-90`）逐像素覆盖率相等（容差 0）<br>② ★★ **同一弧段的三种写法必须逐位相同**：`(270,+90)` ≡ `(0,-90)` ≡ `(-90,+90)`（实测差 **0**）——这是**起点侧 / 终点侧吸附不对称**的**回归锚**（旧规格下 `(270,+90)` 与 `(0,-90)` 实测差 **26** 格） |
| T33-4 | `Arc.CrossZeroAliasing` | ★★ **v1.1 订正（评审第三轮 §5）**：v1.0 拿 `start=10,sweep=20` 与 `start=350,sweep=20` 比「重叠角域」——**二者不是同一弧段**（实测逐像素差 **1789** 格；它们只在 `10°` 这**一个边界点**上相接，v1.0 的括号注「取 350–360 段」更是把两段不同角域混为一谈）。**现行两条**：<br>① ★★ **同一弧段的两种写法必须逐位相同**：`start=350,sweep=20` ≡ `start=-10,sweep=20`（实测差 **0**）<br>② 该弧**非空**，且 `350°–360°` 与 `0°–10°` **两段都有覆盖**（不因跨 0° 被截断） |
| T33-5 | `Arc.FullCircleSeamAndSign` | `sweep=360`：`0°` 邻域**无零覆盖率列**（接缝防线）；★ `sweep=+360` 与 `sweep=-360` **逐位相同** |
| T33-6 | `Arc.RoundCapDiscriminant` | ★★ **两端各取一个判别点（v1.1 订正）**：v1.0 只写「端点圆帽内、径向带 ∩ 扇区之外」，**没指明哪一端**——而**起点侧圆帽的缺失正是旧 12 行规格的缺陷**（见 △1b 注；只测终点会**整片漏判**）。⇒ 对 `start=0,sweep=90`，在**终点圆帽内**与**起点圆帽内**各取一点（都在「径向带 ∩ 扇区」之外），**两处覆盖率都必须 > 0**（平头在两处恒 0）；★ 反向对照：同点沿切向外移 `hw+1` ⇒ **0** |
| T33-7 | `Arc.DegenerateEmptyTable` | `radius<=0` / `width<=0` / `sweep==0` / `NaN` / `+Inf` / `-Inf` ⇒ **全部 `Empty()`**；`\|sweep\|=450` ⇒ **逐位等于** `sweep=360` 档 |
| T33-7b | `Arc.FanDegenerate` | `radius=4, width=10` ⇒ `effW=lround(10)=10` ⇒ **`hw = min(10*0.5, 4) = 4`**（★ 由 `radius` 钳制，**不是** 5）：<br>① `ArcSegmentDistanceSq(圆心) == radius² = 16 ≤ hw² = 16` ⇒ **圆心在带内**（★ 等号成立 ⇒ 本档正好是 `radius == hw` 的**临界**）<br>② **面积按 C33-5 的合式**（★★ **v1.1 订正：退化档与常规档共用同一式**）：`\|sweep\|=90` ⇒ `A = 2·radius·hw·θ_rad + π·hw² = 50.265 + 50.265 = ` **`100.531`**；`360` ⇒ `A = 4π·radius·hw = ` **`201.062`**（**按解析面积**判，**不**断言「中心像素=255」）<br>★ **另取一档更小半径**（`radius=2, width=10` ⇒ `hw=2`）：`\|sweep\|=90` ⇒ `12.566 + 12.566 = ` **`25.133`**；`360` ⇒ **`50.265`**<br>★ **算式核对留痕（两轮都要留）**：初版把半径档写成 `hw=5` 的值（`63.617`/`254.469`）——与本节自己的 `hw = min(effW*0.5, radius)` 规则矛盾；**v1.0 又改用「扇形」式 `θ_rad·(radius+hw)²/2`**（`50.265`/`12.566`）——★★ 该式在 `radius == hw` 时**恒等于无帽带面积**、**系统性漏掉整个圆帽 `π·hw²`**（实测**恰为真值的 `0.500×`**），**只在 `\|sweep\| = 360°` 因巧合正确**。⇒ 现行与常规档**共用同一合式**，不再有独立分档 |
| T33-8 | `Arc.Determinism` | 同输入两次：`width/height/originX/originY/coverage` **逐位相同** |
| T33-9 | `ArcSegmentDistance.TruthTable` | 纯函数真值表（**生成器实际调用它** ⇒ 测即生产判据）：<br>★ **起点归属**：点恰在起始角 ⇒ `dist² ≈ 0`（`sweep = +90` 与 `−90` **都**成立——**负扫角订正的回归锚**）<br>★★ **中心线往返（v1.1 新增）**：弧段上 4001 个参数点 `t ∈ [0,1]` ⇒ `dist² < 1e-6`——这是「第 5 行与第 14 行**两处负号成对**」的**直接**判据（v1.0 的写法实测最大偏 **14.139 px** / **20.000 px**）<br>★★ **越界吸附（v1.1 新增）**：起点**之前**的点与终点**之后**的点 ⇒ `dist²` **必须等于「到圆周角差更小的那个端点」的距离平方**（**起点侧圆帽的回归锚**——旧规格在起点侧返回的是到**终点**的距离）<br>★★ **方位真值表（v1.1 新增，C33-2 的 v1.3 收口要求）**：`start` 取 `0/90/180/270°`、`sweep = 360°` ⇒ 中心线上 **3 点钟 / 6 点钟 / 9 点钟 / 12 点钟** 四个方位点的 `dist²` **都为 0**（★ **镜像类与面积类断言测不出方向**——只有这一条能钉住）<br>★ 跨 0° · ★ 整圆（`dist² = (radial−radius)²`）· ★ **圆心**（`radius²`，`radius ≤ hw` 时 `≤ hw²`）· ★ 端点圆帽邻域 |
| T33-9b | `Arc.NormalizeArcTruthTable` | `NormalizeArc` 全表：`drawable` 的六种 false 条件 · `sweep` 钳制（`450 → 360`、`-450 → -360`）· `hw = min(effW*0.5, radius)`（★ 含 `radius=4, width=10 → hw=4`）· ★★ **数值安全带（v1.1 新增，§3.3）**：`radius = kMaxArcExtent * 2` ⇒ `drawable == false` **且** `oversized == true`；`cx = kMaxArcCoord * 2` ⇒ 同；★ 且 `GenerateArcCoverage` 在 `bw * bh > kMaxArcPixels` 的档上返回 `Empty()`（**不崩、不分配**）· ★ **`QA(start)` 的等价类**：`startDeg = 10°` 与 `370°` 命中同一槽位 |

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
| **批二** | △4 `RenderingBackend` 纯虚 · △5 `RenderCommand` · △6 `Renderer` · △10 `PaintContext` · △9 `RecordingBackend` · **△7/△8 的代码层**（GDI 覆盖度→`PatchSurface`；GL 覆盖度→`ArcSlot`（11 元键）→`TexturedQuad`）+ T33-10..12 | ★ **3 个实现者必须同步**（漏 = 编译失败，结构性）——★ 故 △7/△8 的**代码层**随批二落地（纯虚强制，与 §10 批二行一致）；批三只留**真窗口像素与观感验证** |
| **批三** | △11 样式 + `Theme` + 替身 · △12 控件 + T33-13..18 · △7/△8 的**真窗口像素与观感验证**（代码层已在批二） | 真窗口像素 + 观感 |
| **批四** | `PaintProbe` 加转发 override + 验证（转发/计数/计时） | harness 重建 + 读数 |

---

## 7. 影响面（实施目标）

| 项 | 目标值 |
|---|---|
| 公共头 | **96 → 98**（`RingProgressBarStyle.h` + `RingProgressBar.h`；`RenderingBackend.h`/`RenderCommand.h`/`PaintContext.h`/`Theme.h` 为**既有头改动**） |
| 公共 API | +1 纯虚（`DrawArc`）· +1 命令 · +1 `PaintContext` 方法 · +1 样式类型 +1 override 结构 · +1 `Theme` 纯虚 · +1 控件（5 方法） |
| 新建文件 | **5**（`ArcCoverage.h/.cpp` · `RingProgressBarStyle.h` · `RingProgressBar.h/.cpp`）——★ 测试 2 个另计 |
| 改动文件 | ★ **19 个文件**（v1.0 的「**8**」两种数法都得不到——按文件数 19、按模块条目数 12，批二实测订正，§11 待核项随之关闭）：`BackendContract.h` · `RenderingBackend.h` · `RenderCommand.h` · `Renderer.h/.cpp` · `PaintContext.h/.cpp` · `GDIBackend.h/.cpp` · `GLRenderer.h/.cpp` · `RecordingBackend.h/.cpp` · `Theme.h` · `DefaultTheme.h/.cpp` · `ThemeTests.cpp` · `RunAllTests.h/.cpp`。★ **批一+批二已落盘 = 15 个**（`git status` 实测：4 公共头 + 9 `src/Render` + 2 `src/Tests`）；余 4 个（`Theme.h` · `DefaultTheme.h/.cpp` · `ThemeTests.cpp`）属批三 |
| ★ CMake | **配置 0 改动**（B22 的 `GLOB_RECURSE CONFIGURE_DEPENDS`——★ 初设 §6 列为「改动」**有误**，本稿订正）。★★ **但边界要写清（批二实测，E33-4）**：`CONFIGURE_DEPENDS` 的实际行为是「**构建时发现 glob 失配 ⇒ 重新 configure**」，而**同一次构建里不会把新文件补进编译单元集** ⇒ ★★ **VS/MSBuild 链新增 `Tests/*.cpp` 后，第一次构建只重新 configure**（该文件**不参与编译**）⇒ 链接报 `LNK2019: 无法解析的外部符号 RegisterXxxTests`；**第二次构建才编译它并链接通过**（批一、批二各复现一次，两次都在新增测试文件时命中）。★ 三个 Ninja 链（MinGW / Clang / ClangCL）**一次即过**——它们在同一构建内完成 re-glob + 编译。⇒ **判据**：`obj` **不存在** ⇒ 再跑一次构建即可（**不是**代码缺陷）；`obj` **存在**却报符号缺失 ⇒ 才是真符号问题。★ 因此「零 CMake 改动」的正确表述 = **「不需要人工编辑任何构建文件」**（成立），**不等于「新增文件后一次构建即通过」**（后者是错的）。 |
| 用例 | **445 → 465**（+20） |
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
| **批四**（✅ **2026-10-16 落盘**） | ★ **跨仓库交付项 R9 兑现**（落点 `ECDI_fake_harness`）：`PaintProbe` 加 `DrawArc` 转发 override（**分类计数 + `OnCommand` + 逐参透传**，与其余 8 个 `Draw*` 同形）· `ArcCommands()` 读数 + `m_arcs` 计数位 · `App.cpp` 命令构成 JSON 接 `"arc"` 类。★★ **前置链路（本批实测勘明，是本 Phase 最容易被漏掉的一环）**：harness 经 `find_package(ECDI CONFIG)` 消费 **install 产物**（`third_party/ecdi/`），故「改 ECDI 公共头」**不会**自动传到 harness——必须 **`cmake --install` 刷新前缀**。实测该前缀是 **2026-10-13 的旧快照**（零 `DrawArc`）⇒ 加 override 前它连的是**旧 ECDI**。★ **执行链** = ECDI `cmake-build-release-visual-studio` **重新 configure**（否则新建的 `ArcCoverage.cpp` 不入 `.vcxproj` ⇒ `LNK2019: GenerateArcCoverage` 未解析，**E33-4 机制在 install 链上的第二次实证**）→ `--build --config Release` → `--install --prefix third_party/ecdi` → harness `--build --config Release`。★★ **R9 三项验证（独立探针 `.scratch/p33b4/probe_paintprobe_arc.cpp`，MSVC Release，`PASS: 0 failure(s)`）**：**V1 转发参数 7/7**（6 字段**逐位相同**，含 `sweep=450` **未被钳到 360**、`start=-30` 原样——C33-9 原样语义的可机检证据）· **V2 计数 6/6**（`ArcCommands()==5`、内层收到 5、矩形/线**不串味**、`TotalCommands()==7` 含弧）· **V3 计时 6/6**（包一层探针**不产生数量级劣化**；逐帧序列完好）。★ **端到端读数**：`--cull-probe` 跑 `demo-session.jsonl` ⇒ **`commandMix` 已含 `"arc"` 键**，`frameCount=23` / `avgExecuteUs=6332` / 各类计数正常 ⇒ **探针零行为回归**。★ **ECDI 侧 Release 链补测 465/465**（此前只有三链 Debug 读数，本批补上 Release 配置）。★ **本批实测抓出的三处探针判据坑（均如实登记，E33-11）**：① `NullInner` 空实现被 `/O2` **整体优化掉** ⇒ 基线恒 `0us`（对照组失效，条 92④）——修法 = 加 `sink` 副作用并**断言它被执行**；② `PaintProbe::EndFrame` 是「**先取 now → 再调 `inner->EndFrame()`**」⇒ 把忙等放在内层 `EndFrame` **记不到**（实测 busy inner 仍 `0us`）——负载必须落在 **`DrawArc`**（计时窗口内）；③ 「`TotalExecuteUs() > 0`」在空活下测的是**计时器分辨率**而非探针正确性（每帧各自 `duration_cast<microseconds>` ⇒ 单帧 <1us 时**逐帧截断为 0**，和恒 0）——修法 = 给**能被量到的负载**做正对照（实测 2000 帧 × 3us = **6023us**，量具校准成立）。 |
| **批三**（✅ **2026-10-16 落盘**） | 样式 / Theme / 控件 + 命令层消费收口：△11 `RingProgressBarStyle.h`（🆕：`trackColor`/`fillColor`/`thickness` 三字段 + Override 结构）· `Theme` **第 9 个纯虚** + `DefaultTheme` 实现（track `230,230,235` / fill `80,120,220` 与 `ProgressBarStyle::background` 同值 / `thickness = 6.0f`）· `ThemeTests.cpp` 的 `RedButtonTextTheme` 替身同步（盯防⑦结构性保证）· △12 `RingProgressBar.h/.cpp`（🆕：`SetProgress` **NaN 先于 clamp 拒绝** / `AnimateTo` **替换式重启** from = 呈现值 / `OnPaint` **恒 2 条 `DrawArc`**，`R <= 0` 不在控件层早退——退化判定归 `NormalizeArc` 唯一实现）· `RunAllTests` 注册。**三链实测（Debug）全部 `465 passed / 0 failed / 465 total`、退出码 0**（MinGW · Clang · ClangCL）——= 459 + 批三 6，**Phase 33 的 20 条设计目标全部落盘**；MSVC 链留用户侧。★ **本批实测抓出两处测试断言错误 + 一处详设规格错误（均如实登记）**：**E33-8** = T33-17 写「`SetSize(0,0)` ⇒ 0 条命令」——**详设规格写错了**：`Widget::Paint` 的 clip 对（Push/Pop）在剔除栈为空时**照常发出**（`PaintContext` 双参构造 = 无界，`IsRectVisible` 恒 true）⇒ 实测 2 条 clip + 0 条弧；修法 = 用 **`OnPaint` 直调**断言「控件自身 0 条弧」（经派生类公开转发——protected 虚），**另补 Paint() 对照断言 2 条 clip**；**E33-9** = T33-18 初版**方位判据反了**：start=270° 顺时针 270° ⇒ 覆盖 12→3→6→9 点钟，**3 点钟在进度弧内**（G≈120）、缺口在**左上象限**——初版却断言「3 点钟是轨道色」，实测红；修法 = 弧上点取进度色 G<180 + 缺口点（10:30 方向）取轨道色 G>180，**双点对偶**判据（比单点更强——同时钉住方向与扇区界）。★ **工具链教训（E33-6/E33-7 的延续，本批第 4–6 次命中）**：**`Edit` 工具在部分路径上会剥离被编辑文件的 UTF-8 BOM**（`Theme.h` / `DefaultTheme.h` / 本测试文件多次）⇒ 已固化为**写盘后固定自查**（每次 Edit 后立即核 BOM，剥了就原子补回；`doc_lint` L01 作兜底量具——本轮靠它当场抓到 +2）。★ 另抓出并修复一处 **CRLF 翻倍畸形**（`\r\r\n` ×396——PowerShell here-string 的 `-split "`n"` 残留 `\r` 所致，条 90 纪律四的实况复现；按其修复配方 `re.sub(r'\r*\n','\n')` 吸收后一次性 join 还原，行数守恒 397）。★ **一连接线收益**：`RunAllTests.h` 批二遗留的**注释错拼**（`RegisterArcCommandTests` 行尾粘着两段旧注释）随本批注册一并修掉。 |
| **批二**（✅ **2026-10-16 落盘**） | 命令层贯通 + **纯虚追赶**（`DrawArc` 取纯虚 ⇒ 实现者必须同步，否则编译失败）：△4 `RenderingBackend` 纯虚 · △5 `DrawArcCommand` + variant · △6 `Renderer::ExecuteCommand` 重载（★★ **两个角度不折 scale**）· △9 `RecordingBackend::ArcDraw` + `arcDraws`（★ **不调 `NormalizeArc`**）· △10 `PaintContext::DrawArc` · **△7/△8 的代码层**（GDI 覆盖度→`PatchSurface`；GL 覆盖度→`ArcSlot`（**11 元键**）→`TexturedQuad`）· 新增 `ArcCommandTests.cpp`（T33-10..12）。**四链实测（Debug）全部 `459 passed / 0 failed / 459 total`、退出码 0**（MinGW · Clang · ClangCL · MSVC）——= 批一 456 + 批二 3。★ **本批实测抓出三处设计-实现偏差（均如实登记）**：**E33-3** = GL 侧**没有** AA 开关（`m_antiAliasing` 全文件零命中）⇒ PD33-C 在 GL 无对应物、`binarize` 只能传 `false`；**E33-4** = VS 链新增测试文件**需要构建两次**（第一次仅重新 configure，第二次才编译）——「CMake 零改动」应表述为「不需人工编辑构建文件」，**不是**「一次构建即通过」；**E33-5** = `GLRenderer` 的 `DrawArc` 声明此前**从未存在**（我首轮只加了 `ArcSlot`）⇒ 编译器以`no declaration matches` 拦下——**纯虚的价值正在于此**（漏 override 是编译错误，不是运行期静默丢失）。★ 另修正批一的登记：当时把 VS 链的 `LNK2019` 归因为「`Cl.items.tlog` 陈旧、删了就好」——批二复现证明**真正机制是「新文件需要第二次构建」**（删 tlog 不是必需步骤；那是当时**同时**做了两件事造成的误归因，本条按条 80 追溯订正）。 |
| **批一**（✅ **2026-10-16 落盘**） | **零公共头改动**（公共头 96 保持）· 新增 `ArcCoverage.h/.cpp` + `ArcCoverageTests.cpp` · `BackendContract.h` 追加弧三段（+`ArcParams` / `NormalizeArc` / `ArcSegmentDistanceSq` / 三个数值安全带常量）· `RunAllTests.h/.cpp` 各 +1 行。**四链读数（Debug，全部 `456 passed / 0 failed / 456 total`、退出码 0）**：MinGW（本机 `ninja -C cmake-build-debug-mingw`，`-D_DEBUG` ×252）· Clang（本机 ×258）· ClangCL（本机）· **MSVC / Visual Studio（用户侧实测）**。★ **四链读数逐位一致**：`T33-2` 四档 `1649.165 / 3219.792 / 4790.420 / 6282.510`、`T33-7b` `100.8235 / 201.4706 / 25.3137 / 50.4588`、`T33-9` 往返 `0.000000000 px²` —— **同一组数字在四链上完全相同**（本 Phase 的几何判据是纯浮点、无平台分支，实测印证了这一点）。★ 断言启用旁证：`build.ninja` 的 `-D_DEBUG` 见上；VS 链 `CL.command.1.tlog` 内含 `/D_DEBUG`。★★ **VS 链曾报一次 `LNK2019`**（`RegisterArcCoverageTests` 未解析）——**不是代码缺陷**，是 MSBuild 的 `Cl.items.tlog` 陈旧（只链接不编译）；删该 tlog 后重建即通过，判据与修法已写进 §4 盯防⑧ 的同族第二坑。★★ **容差冻结（实测，非推导）**：`T33-2` 四档相对误差 `0.0104% / 0.0106% / 0.0106% / 0.0108%` ⇒ 冻结 `0.5%`；`T33-7b` 扇形档 `0.291% / 0.203% / 0.719% / 0.386%` ⇒ 冻结 **`1.5%`**（依据 = samples 8→64 的**收敛序列**：`r=4` `0.291%→0.115%→−0.025%→0.002%`、`r=2` `0.719%→0.329%→0.064%→−0.061%`，残差随采样密度**单调收敛** ⇒ 是**格点量化**而非几何错；该容差对 v1.0 的错式（差 50%）仍有 **33×** 判别余量）。★★ **一处计数缺陷（本批实测订正）**：v1.0 写「T33-1..18 共 18 条 ⇒ +18 ⇒ 463」，而本节表格实为 **20 行**（18 个号 + 并列的 `7b`/`9b`）⇒ 正确目标 = **465**，已改（§5 标题 / 计数口径 / §7 / 本行）。★ **实现期一处偏离（已回写设计 = 勘误 E33-2；规格 15 → 16 行）**：增设 **`ang` 归约到 `[0, 2π)`**——`cos/sin` 以 360° 为周期 ⇒ **几何语义零变化**，但归约让「同一弧段的等价写法」**逐位相同**（否则 `(270,+90)` 与 `(0,−90)` 在共享端点分别得 `ang = 360` 与 `0`，浮点舍入差会让 T33-3 的别名断言在边界采样点上偶发失败）。**详设 §2 △1b 尚未写入该行**，记为本批**待回写项**。 |
| **测试** | T33-1..18 结果 + 运行链（MinGW / Clang / ClangCL / MSVC）+ 用例总数（目标 **465**） |
| **验证** | 四链读数 · 真窗口 E2E 读数 · 面积守恒实测容差（冻结值） |
| **限制** | L33-1..6 的最终状态 |
| **台账** | 本文件 §10 + `framework-defect-audit` + `roadmap-deferred` + `docs/dev/README` + 根 README |

---

## 11. 修订记录

- **v1.2**（2026-10-16）**实现落地状态同步（补记）**：原头部状态行记「**待复核**」，实际 **批一–批四已于 2026-10-16 全部实现并验证通过**（三链 465/465 + MSVC Release 465/465，= 445 基线 + 20 条设计目标）。★ **取证**（条 30③：实现日期必须取证，不得用文档头日期倒推）：① `docs/dev/README.md` 的 Phase 33 段已登记「✅ 实现收口（批一–批四全部落盘）」并附三链与 MSVC 读数；② 本文正文已含「批四」3 次、「465」9 次、「收口」13 次；③ 八个新增源文件（`ArcCoverage.h/.cpp` · `RingProgressBar.h/.cpp` · `RingProgressBarStyle.h` · 三个测试文件）全部落盘，`RunAllTests.cpp` 已注册 `RegisterArcCoverageTests` / `RegisterArcCommandTests` / `RegisterRingProgressBarTests`。★ **按条 30①**：原「待复核」措辞**不抹除**，以引用形式保留在状态行末句。★ **未改动任何设计内容**——本条只订正状态载体（头部状态行 + 本条目），§1–§11 正文一字未动。
- **v1.1**（2026-10-16）★★ **吸收第三轮外部评审（对 v1.0 的「有条件通过」；4×P1 + 3×P2）**。★ 按条 121 先做**已覆盖度回扫**，再**用独立探针逐条复现**（沿条 29⑤：不因为对方是评审就跳过验证，也不因为是自己写过的公式就免检）。**四项 P1 全部成立、全部采纳**：
  · **§1 退化档面积式错** ⇒ ★★ **成立，且比评审说的更严重**。评审只说「不能直接推出 `θ_rad·(radius+hw)²/2`」；**实测确认该式在 `radius == hw` 时恒等于无帽带面积 `θ_rad·radius·(2hw)`，系统性漏掉整个圆帽 `π·hw²`**：`radius=hw=4, |sweep|=90°` ⇒ 该式 `50.265` vs 真值 `100.531`（**恰为 `0.500×`**），`radius=hw=2` ⇒ `12.566` vs `25.133`（同）。★ 它**只在 `|sweep| = 360°` 因巧合正确**（此时域 = 整圆盘、两式重合）——这解释了 T33-7b 的 `360°` 档为何一直是对的。**修法 = 删掉独立分档、与常规档共用 `A = θ_rad·radius·hw·2 + π·hw²`**，并**回改需求 C33-5**（根因在需求侧 ⇒ 需求升 v1.3）。
  · ★★ **本轮另发现一项评审未点出的**：该合式**自身有有效域**——`π·hw²` 项假定两端圆帽不重叠，实测精确域 = **`|sweep| ≤ 360° − 2·arcsin(hw/radius)`**（用极坐标求积的独立量具，在三处已知闭式上校准到 `0.00e+00`；`hw/radius = 0.05 / 0.20 / 0.80 / 1.00` ⇒ 界 = `354.27° / 336.93° / 253.74° / 180.00°`；界内误差 `< 1e-9`，界外系统性偏高至 `1.247×`）⇒ **已写进需求 C33-5 的有效域**，并把域外处置写明（不设解析参照）。★ 顺带订正 `|sweep| = 360°` 档的**标签**：值 `6283.185` 正确，但它 **= `4π·radius·hw`**、**不是** `2π·radius·hw`（差 2×）。
  · **§2 角度两处负号反了** ⇒ ★★ **确认；两处负号必须成对改**。`atan2f(-dy, dx)` 与 `qy = cy - radius*sinf(...)` **配套反了** ⇒ 实现的是「逆时针为正」，`RingProgressBar` 的 `start=270°` **实测落在 6 点钟**（设计意图 12 点钟）。★ **评审的附注也成立且已量化**：「镜像对称测试可能仍然通过」——**实测确认**：`(0,+90)` 与 `(270,-90)` 的镜像比对在**正负两种约定下给出同一个差异数**，且**整圆档与 `350°/20°` 档镜像后等于自己**（中心线往返 `0.000 px`）⇒ **镜像类与面积类断言结构上无法判定方向**。**修法 = 两处成对改回 `atan2f(dy, dx)` 与 `cy + radius*sinf(...)`**，并把「**方位真值表**」写成 C33-2 的收口要求（T33-9 已加）。★ **顺带发现评审未点出的一处更重的缺陷**：**同一 12 行规格把「起点侧圆帽」整片切掉**——越界时 `t` 一律钳到 **1**，于是**起点之前**的点返回的是到**终点**的距离（终点侧却完好）。实测误判占带内子采样 `r=10,hw=2,|sweep|=90°` **406/4831 = 8.40%** · `|sweep|=20°` **23.85%** · `r=4,hw=4` **25.02%** · `|sweep|=180°` **4.59%**（`|sweep|=360°` 无端点 ⇒ 0）。**修法 = 越界时吸附到「圆周角差更小」的端点**（与「到两端点欧氏距离更小」恒等价，且比较排在 `sqrt` 前 ⇒ 仍是零开方风格）；**修后六档全部 0 误判**。
  · **§3 GDI 二值化数据流不闭合** ⇒ ★ **确认**（第 5 步做在**未命名的局部拷贝**上、第 7 步仍传原 `grid` ⇒ 照字面实现则关 AA 的语义**静默失效**）。**修法 = 命名 `binary` + `raster` 指针指向它**，光栅化与 `BlendPatch` 一律吃 `*raster`；并把盯防⑨ 从「不改 `grid`」升级为「**拷贝必须被消费**」。
  · **§4 GL `ArcSlot` 签名与键元数矛盾** ⇒ ★ **确认**（声明 **2 参** vs 自己的调用点 **3 参** · 元组声明 **10 个 `int`** vs 键清单 **9 项**）。**修法 = 统一为 3 参 + 11 元键**；★ 并**采纳评审的附加建议**（键与量化几何的关系）：**补入 `originX/originY`**——键里的几何量是量化的，两次调用**可以同键而 bbox 原点差 1 px**，而位图按网格局部坐标栅格化 ⇒ 原点必须进键，否则「同键 ⇒ 同位图」不成立。★ 顺带**实测澄清**评审问的「`r < hw` 边界是否需独立验证」：**不可达**——`hw = min(effW*0.5, radius)` ⇒ **恒有 `hw ≤ radius`**，退化档**只有 `radius == hw` 这一个临界**。
  **三项 P2 的处置**：
  · **§5 T33-4 配对选错** ⇒ ★ **确认**（`(350,+20)` 与 `(10,+20)` **只在 `10°` 一个边界点上相接**，实测逐像素差 **1789** 格）。**修法 = 改「同一弧段的两种写法」`(350,+20)` ≡ `(-10,+20)`（实测差 0）**。
  · **§6 极大有限值缺安全带** ⇒ ★ **确认**（`NormalizeArc` 只有 `!isfinite` 一关；`lround` / bbox 的 `floor→int` / `bw*bh` / 键量化四处均可越界）。**修法 = 新增 §3.3**（N1–N4，沿 Phase 24 O2 先例）。
  · **§7 版本链不符** ⇒ ★★ **如实反驳（本轮唯一不成立项）**：本机实测 `HEAD == origin/main`，`git show HEAD:` 逐字读出**需求稿 v1.2 / 初设稿 v1.1**，与本稿 §1 声明的输入**逐位一致**；`origin/main` reflog 显示该提交已于 `2026-10-15 22:26:55 +0800` 推送 ⇒ 评审读到的是**过期页面**（本机 `git fetch` 因无外网失败，无法再取远端快照比对）。★ 但**评审据此提出的动作照做**：三份文档**同步升版**（需求 v1.3 / 初设 v1.2 / 详设 v1.1），并把「详设版本被引用处」一并核对。
  ★ **评审未点出、由本稿复核抓出的第三处**：`△3` 的预过滤伪码（第 6 步）**漏了 `lo > 0.0f &&` 守卫**，而同文件 §3.1 与盯防④**都要求有它** ⇒ **文档自相矛盾**；且 §3.1 给出的理由（「`lo < 0` ⇒ 平方后 `radialSq < lo²` 恒假」）**是假的**——负数的平方是**正数**，该比较对**圆心附近**成立。实测未加守卫时误跳带内子采样：`r=4,hw=4,|sweep|=90°` **72** 个 · 同档 `|sweep|=360°` **96** 个 · `r=2,hw=2` **68** 个，加守卫后**全部 0**。**三处已统一。**
  ★ **一处范围外观察（如实登记，不在本 Phase 处置）**：`LineSlot` 的键**不含 `originX/originY`**（`GLRenderer.h:120` 的 7 元组），与 §2 △7 对 `ArcSlot` 的分析**同源** ⇒ **同类 1 px 风险**，但属 Phase 24 的既有形态，**本轮不擅自改**。
  ★★ **实施期实测偏差（批一/批二落盘时抓出，追加登记）**：
  · **E33-2**（批一）= 12→15 行规格增设 **`ang` 归约到 `[0, 2π)`**——`cos/sin` 以 360° 为周期 ⇒ 几何语义零变化，但归约让「同一弧段的等价写法」**逐位相同**（否则 T33-3 的别名断言在共享端点处偶发失败）。规格已改为 **16 行**。
  · **E33-3**（批二）= **GL 侧没有 AA 开关**（实测 grep 零命中）⇒ PD33-C 在 GL 无对应物，`binarize = false`。
  · **E33-4**（批二）= **VS 链新增 `Tests/*.cpp` 需构建两次**（第一次仅重新 configure）⇒「CMake 零改动」的正确表述 = 「不需人工编辑构建文件」，**不等于**「一次构建即通过」。
  · **E33-5**（批二）= `GLRenderer::DrawArc` 声明此前不存在 ⇒ 编译器 `no declaration matches` 拦下——**这正是取纯虚的收益**（漏 override = 编译错误，而非运行期静默不画）。
  · ★ **批一登记的一处误归因已订正**：当时把 VS 链 `LNK2019` 记为「`Cl.items.tlog` 陈旧」，批二复现证明真机制是 **E33-4 的「需第二次构建」**（删 tlog 非必需）。
  · **E33-7**（批二收口）= **行尾形态的实测事实订正（条 98 的 docs 分类与实况不符）**：全库实测（逐文件统计）——**源码 = CRLF 压倒性主流**（`ECDI/include` 95:1 · `ECDI/src` 115:11），**`docs/*.md` = LF 为多数**（149 LF : 34 CRLF），**根/`docs` README 与 `roadmap-deferred.md` 均 LF**。★ 条 98 写的「阶段文档 = CRLF+BOM」在 2026-09-24 成立，此后 docs/ 又以 LF 形态增长（含 phase24/phase32 详设实测 LF），**该分类已过时**。★ **据此拍板的处置（用户 2026-10-16）**：① 只把本批改写的 `BackendContract.h` 从 LF 转 **CRLF**（对齐源码主流形态；转换零噪音——`git diff --stat` 仍为 `171 insertions / 0 deletions`，行数 278 守恒、裸 LF/裸 CR 均为 0）；② `README.md` / `docs/dev/README.md` **不转**（条 98 反向纪律：它们自身的既有形态就是 LF）；③ 三份 phase33 文档**不转**（保持 docs/ 现行 LF 多数形态）；④ 其余 11 个 LF 源文件属既有少数派，**不在本批处置**（超授权面）；⑤ skill 条 98 的修订**留给用户**，本稿只登记实况。
  · **E33-8**（批三）= **T33-17 的「`SetSize(0,0)` ⇒ 0 条命令」规格错误**：`Widget::Paint` 的 clip 对在剔除栈为空时**照常发出**（`PaintContext` 双参构造 = 无界 ⇒ `IsRectVisible` 恒 true），实测 2 条 clip + 0 条弧——**「0 条」只对控件自身成立，不对 Paint() 全链成立**。修法 = 测试改用 `OnPaint` 直调（经派生类公开转发，protected 虚）+ 补 Paint() 对照断言；详设 T33-17 判据随批订正。
  · **E33-9**（批三）= **T33-18 初版方位判据反了**：start=270° 顺时针 270° 覆盖 12→3→6→9 点钟，3 点钟在**进度弧内**、缺口在**左上象限**——初版却断言 3 点钟是轨道色（实测红）。修法 = **双点对偶**判据（弧上点 G<180 + 缺口点 G>180），同时钉住方向与扇区界。★ 该错误恰是 C33-2「方位真值表」要防的那类（**镜像/面积类断言测不出方向**）在**测试侧**的重演——判据写错方向时，红灯是唯一救星。
  · **E33-10**（批三）= **`Edit` 工具的 BOM 剥离是系统性行为、必须固化为写盘后固定自查**：批二批三合计命中 **6 次**（`GLRenderer.h` · 详设自身 · `Theme.h` · `DefaultTheme.h` · `RingProgressBarTests.cpp` ×2），全部由「Edit 后立即核 BOM」与 `doc_lint` L01 抓回。★ **判据升级**：① 每次 `Edit` 后立即核 BOM（三个字节，成本近零）；② **`doc_lint` 是兜底量具**——批三的 `Theme.h`/`DefaultTheme.h` 剥离就是 L01 345→347 当场暴露的；③ 条 113 的「Edit 对 BOM+CRLF 安全」结论**不可依赖**（同为 Edit，有的文件保住了、有的剥了——触发条件未查明，按「必剥」假设处理才安全）；④ 同批另抓出一次 **CRLF 翻倍畸形**（`\r\r\n` ×396，PowerShell here-string 残留 `\r` 所致）——条 90 纪律四的修复配方（`re.sub(r'\r*\n','\n')` 吸收后一次性 join）当场可用，行数守恒是硬判据。
  · **E33-11**（批四）= **探针判据的三处自坑（对照组 / 计时窗口 / 量具分辨率）**——R9 三项验证的探针首版**红了三轮**，三处都不是被测对象的问题，而是**判据本身**错：① **对照组被优化掉**：`NullInner` 全空实现 ⇒ `/O2` 把 3200 次空调用整体消除，基线恒 `0us`（条 92④「探针必须带有效对照组」的实况复现）——修法 = 给内层加 `sink` 副作用，**并断言它被执行**（`sink` 计数匹配才算对照组有效）；② **负载放错计时窗口**：`PaintProbe::EndFrame` 是「**先取 `now` → 再调 `inner->EndFrame()`**」⇒ 把忙等放进内层 `EndFrame` 时它**发生在计时窗口之外**（实测 busy inner 仍 `0us`）——负载必须落在窗口内被调用的 `DrawArc`；③ **判据测的是量具而非对象**：「`TotalExecuteUs() > 0`」在空活下**恒假**，因为 `EndFrame` 对**每帧各自** `duration_cast<microseconds>` ⇒ 单帧 <1us 时**逐帧截断为 0**、和恒 0（加帧数无用，实测 20000 帧仍 0）——修法 = 给**能被量到的负载**做正对照（2000 帧 × 3us = **6023us**），先证量具测得准（条 76）。★ **三处的共性**：**红灯指向的是「我写得对不对」，不是「它做得对不对」**——故报「验证通过」前必须先让对照组红一次、或让量具在一处已知量上校准（条 92③ / 条 76 的合并教训）。
  · **E33-6**（批二收口）= **`GLRenderer.h` 的 UTF-8 BOM 在编辑中被剥离**（HEAD 带 BOM / 工作区不带，`doc_lint` L01 由 345 → 346 暴露）——**机械判据**：该文件含中文注释，MSVC 在 CP936 下按双字节读 ⇒ C4819。**修法** = 原子补回 BOM 字节（内容零改动，`bytes` 只 +3）；**修后** L01 回到 345，且**逐文件比对 HEAD** 证明本批**未引入任何新 L01 缺陷**（全部 24 个改动/新增文件的 BOM 状态与 HEAD 一致，含 4 个新建文件均带 BOM）。★ **同族教训**：条 113 已证 `Edit` 工具对 BOM+CRLF 安全，但本批的部分写入走了**脚本/其他通道** ⇒ **BOM 核查必须作为「写盘后固定动作」**，不能只对新文件做（既有文件被改写同样会丢）。
  ★ **一处待核 → 已订正（批二收口）**：§7 影响面的「改动文件 **8**」与其后**枚举的 12 项**不自洽——实测（`git status` 逐项清点）该枚举 = **19 个文件 / 12 个模块条目**，两种数法都得不到 8；§7 已按 19 订正，本项关闭。

- **v1.0**（2026-10-14）初稿。**输入**：需求 **v1.2** · 初设 **v1.1**。**内容**：§1 基线 B1–B24（★ **B22 订正初设的一处影响面错误**：CMake 用 `GLOB_RECURSE CONFIGURE_DEPENDS` ⇒ 新增文件**零 CMake 改动**）· §2 逐文件 △1–△13（新建 5 + 改动 8）· §3 初设未定的两处（**PD33-H 预过滤的开方问题** ⇒ 两侧平方比较 + `lo > 0` 守卫；负扫角的正负对称单表达式 + 反例锚）· §4 盯防九条（全部可机检）· §5 用例 T33-1..18 精确断言（含**面积四档手算值**与**负扫角起点回归锚**）· §6 四批 · §7 影响面 · §8 O33 全收口 · §9 新增 **L33-6** · §10 回填模板。**待评审。**
