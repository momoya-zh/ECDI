# Phase 24 · DrawLine 线段抗锯齿（line coverage AA）—— 详细设计（v1.1 · 实施规格）

> 来源：初设稿 `phase24-line-antialiasing-preliminary-design.md` **v1.1 ✅ 已评审通过**（「基本通过，可进入 Detailed Design」，3 处修正已吸收）
> 状态：**v1.1**（2026-10-01）——✅ **评审通过，可进入 Implementation**（外部评审结论：「**详设 v1.0 通过，进入 Implementation**」「实施规格已经足够」；★ 附 2 处**非阻塞小修** + 1 条实施注意——**本版已吸收**：T24-6 推理痕迹清理 · 面积诊断宽类型 · T24-8b 期望像素须走**完整合成链辅助函数**；评审另给实施期 8 条「必须严格遵守」清单，与本文盯防 12 条对齐确认）
> 定位：**实施规格**——△1–△7 逐文件改动 + 契约 C1–C12 → 落点 → 测试全映射 + 盯防 14 条（可机检）+ 用例正文 T24-1..T24-11 + 三批顺序。开放项 O1–O4 全部收口（§9）。

---

## 1. 代码基线（B1–B12，全部 2026-10-01 带行号实测）

| # | 基线 | 证据 |
|---|---|---|
| **B1** | `DrawLine` 现实现：`CreatePen(PS_SOLID)` + `lround` 坐标 + 宽度下限 `max(1, lround(width))` | `src/Render/GDIBackend.cpp:402-421` |
| **B2** | `PatchSurface` 现为**正方形**（`int size`，`Ensure(HDC, int requiredSize)`，只增不减） | `GDIBackend.h:88-102` |
| **B3** | 角路径调用形态：`Ensure(m_memoryDC, R)`（`:452`）· `FillPatchFromMask(mask, color, corner)` + `BlendPatch(m_memoryDC, x, y, R)`（`:867-868`） | `GDIBackend.cpp` |
| **B4** | `FillPatchFromMask` 的 stride = **`m_patchSurface.size * 4`**（`RasterizeCornerPatch` 第 2 参；含 2026-09-11 stride 陷阱注释） | `GDIBackend.cpp:882-896` |
| **B5** | `BlendPatch` 现签名 `(HDC, int x, int y, int size)` → `AlphaBlend(..., size, size, ...)` | `GDIBackend.cpp:898-907` |
| **B6** | `RasterizeCornerPatch` 的预乘公式：`RGB = (cb×c+127)/255`、`A = c`（**a==1 特例**） | `src/Render/CoverageRaster.cpp:41-44` |
| **B7** | `GenerateCornerMask` / `CornerMaskCache` 的 samples 契约（≤0 无效、默认 8、`GetSamples()` 存在） | `src/Render/CornerCoverageMask.h:61-103` |
| **B8** | 测试注册点：`RunAllTests.cpp:25-36` 顺序块（Phase 23 行 = `:36`） | `src/Tests/RunAllTests.cpp` |
| **B9** | L2 真窗口装置：`AAWindow` / `CreateAAWindow`（200×200 右上 `SW_SHOWNOACTIVATE`）/ `ReadPixel(HWND,x,y)`；**历史 flaky 已由 `test_main.cpp` 的 `DisableProcessWindowsGhosting()` 根治** | `src/Tests/AntiAliasingTests.cpp:272-321` |
| **B10** | CMake 双 GLOB_RECURSE + CONFIGURE_DEPENDS（框架源 + 测试源）⇒ 新 .cpp **零 CMake 改动** | `CMakeLists.txt:39-42,106-109` |
| **B11** | legacy `lround` 语义 = `std::lround`（**非 int 截断**——谓词必须同式） | `GDIBackend.cpp:407,415-416` |
| **B12** | `BlendAlphaSolid` 每调用建/销 DIB（9.5 遗留，注释明示 YAGNI）——**线段装配不走它**（R6） | `GDIBackend.cpp:30-31` |

## 2. 逐文件改动（△1–△7；新建 3 · 改动 4 · 公共头零触碰）

### △1 · 新建 `src/Render/LineCoverage.h`（全文）

```cpp
﻿#pragma once

#include <cstdint>
#include <vector>

namespace ECDI {

/// @brief 线段覆盖度网格（Phase 24 · 第一层「覆盖度生成」——纯几何零 GDI）
/// @details 与 `CornerCoverageMask` 同族：行主序、stride = width、coverage ∈ [0,255]。
/// ★ `originX/originY` = 网格 (0,0) 对应的**物理像素坐标**（初设 §5.5 的「单点出参」——
///    生成器计算一次、随网格携带，调用方**零重算**，杜绝三处独立取整的半像素漂移）。
struct LineCoverageGrid {
    int originX = 0;   ///< 网格 (0,0) 的物理像素 x（band bbox 左缘 floor）
    int originY = 0;   ///< 网格 (0,0) 的物理像素 y
    int width = 0;     ///< <= 0 或 coverage 空 ⇒ Empty
    int height = 0;
    std::vector<std::uint8_t> coverage;   ///< width × height，行主序
    bool Empty() const noexcept { return width <= 0 || height <= 0 || coverage.empty(); }
};

/// @brief 分流谓词（初设 D1）：**lround 后共线**（含 round 后重合的退化——legacy 画零像素，探针 A-7）
/// @details 必须用 `std::lround`（与 legacy 路径 `GDIBackend.cpp:415-416` 同式——B11；
///          `int` 截断在负坐标半区与 lround 相差 1，会造成谓词与 legacy 实际绘制不一致）。
bool IsAxisAlignedAfterRound(float x0, float y0, float x1, float y1) noexcept;

/// @brief 生成线段覆盖度（**stadium 带模型：点到线段距离 <= hw**——初设 D3 圆帽）
/// @param x0..y1 物理像素坐标，**float 直入不取整**（R7；取整只发生在谓词与 legacy 路径）
/// @param width  线宽；内部 `effW = max(1, lround(width))`（D4，同 B1 口径）
/// @param samples 超采样倍率（默认 8 = Q2 定案；`<= 0` → 空网格，契约同 B7）
LineCoverageGrid GenerateLineCoverage(float x0, float y0, float x1, float y1,
                                      float width, int samples = 8);

}
```

### △2 · 新建 `src/Render/LineCoverage.cpp`（算法规格 · 约 90 行）

1. **宽度**：`effW = max(1, std::lround(width))`；`hw = effW * 0.5f`。
2. **退化防御**：`(x0==x1 && y0==y1)` → 空网格（理论到不了——谓词已拦，第二层防线）。
3. **bbox**：`minX = floor(min(x0,x1) − hw)`、`maxX = floor(max(x0,x1) + hw)`（y 同理）；`width = maxX−minX+1`。`std::floor` 而非 int 截断（负坐标正确）。
4. **逐像素**（bbox 全扫 + **平方距离预过滤**，O3 定案）：像素中心 `c = (px+0.5, py+0.5)`；若 `distSq(c, seg) > (hw + 0.7071f)²`（`hw` + 半像素对角）⇒ 该像素与带必不相交 ⇒ coverage = 0 跳过 S² 采样。命中者做 S×S 采样：样本 `p = (px + (i+0.5f)/S, py + (j+0.5f)/S)`，stadium 判定 `distSq(p, seg) <= hw²`（**标准点到线段距离**：`t = clamp(dot(ap,ab)/len2, 0, 1)`，天然含两端圆帽）。
5. **量化**：`coverage = lround(255.0f × hit / (S×S))`。
6. **零依赖**：只 include `<cmath>` `<vector>` `<cstdint>` 与自身头——**禁 Windows.h / CoverageRaster.h**（C1 机检）。

### △3 · `src/Render/CoverageRaster.h` 追加声明（既有内容零改动）

```cpp
struct LineCoverageGrid;   // 前置声明（本头不 include LineCoverage.h——避免生成层↔合成层循环）

/// @brief 覆盖度网格 → 预乘 BGRA（一般化 alpha；**无角变换**——Corner 版保留不动）
/// 契约 C6：e = (c*a8+127)/255（有效覆盖）；A = e；RGB = (cb*e+127)/255。
/// a8=255 时 e == c ⇒ 与 RasterizeCornerPatch 逐位同公式（B6）。
void RasterizeMask(std::uint8_t* dest, int stride,
                   const LineCoverageGrid& grid, const Color& color);
```

### △4 · `src/Render/CoverageRaster.cpp` 追加实现（约 35 行）

```cpp
void RasterizeMask(std::uint8_t* dest, int stride,
                   const LineCoverageGrid& grid, const Color& color)
{
    if (dest == nullptr || stride <= 0 || grid.Empty()) return;
    const int a8 = static_cast<int>(std::lround(std::clamp(color.a, 0.0f, 1.0f) * 255.0f));
    const int cb = /* 同 RasterizeCornerPatch 的 ToByte(color.b) */;  // cg / cr 同
    for (int j = 0; j < grid.height; ++j) {
        std::uint8_t* line = dest + std::size_t(j) * std::size_t(stride);
        const std::uint8_t* src = grid.coverage.data() + std::size_t(j) * std::size_t(grid.width);
        for (int i = 0; i < grid.width; ++i) {
            const int c = src[i];
            const int e = (c * a8 + 127) / 255;              // C6：有效覆盖
            line[i*4 + 0] = static_cast<std::uint8_t>((cb * e + 127) / 255);
            line[i*4 + 1] = static_cast<std::uint8_t>((cg * e + 127) / 255);
            line[i*4 + 2] = static_cast<std::uint8_t>((cr * e + 127) / 255);
            line[i*4 + 3] = static_cast<std::uint8_t>(e);
        }
    }
}
```

★ include `Render/LineCoverage.h`（此处允许——依赖单向：合成层认识生成层的网格类型，反向禁止）。

### △5 · `src/Render/GDIBackend.h`（diff）

- `PatchSurface`：`int size = 0;` → **`int width = 0; int height = 0;`**；`bool Ensure(HDC reference, int requiredSize);` → **`bool Ensure(HDC reference, int requiredWidth, int requiredHeight);`**（注释「正方形」→「矩形；两维各自只增不减」）。
- `void BlendPatch(HDC target, int x, int y, int size);` → **`(HDC target, int x, int y, int width, int height);`**。
- 新增私有声明：`void DrawLineCoverage(const Point& start, const Point& end, float width, const Color& color);` 与 `static void DrawLineLegacy(const Point& start, const Point& end, float width, const Color& color);`（★ 后者不必 static——需访问 m_memoryDC ⇒ 非静态私有成员函数）。

### △6 · `src/Render/GDIBackend.cpp`（diff · 本阶段核心）

1. **`DrawLine` 重写**（`:402-421` 原体**原样搬入** `DrawLineLegacy`，一字不改）：

```cpp
void GDIBackend::DrawLine(const Point& start, const Point& end,
                          float width, const Color& color)
{
    // 分支序即语义序（初设 §5.3 / 契约 C8）：
    // ① AA 关 → legacy 逐位现状（R3——含 a<1 丢 alpha 的现状语义）
    // ② a≥1 且 round 后轴对齐（含退化）→ legacy 逐位现状（R4 / D1；A-2/A-3 的足迹原样保留）
    // ③ 其余 → 覆盖度路径（斜线 a=1、一切 a<1——D0；L3：a<1 轴对齐在此获得 AA 边缘）
    if (!m_antiAliasing ||
        (color.a >= 1.0f && IsAxisAlignedAfterRound(start.x, start.y, end.x, end.y)))
    {
        DrawLineLegacy(start, end, width, color);
        return;
    }
    DrawLineCoverage(start, end, width, color);
}
```

2. **`DrawLineCoverage` 新增**：

```cpp
void GDIBackend::DrawLineCoverage(const Point& start, const Point& end,
                                  float width, const Color& color)
{
    const int samples = m_cornerMaskCache.GetSamples();   // C9：单一 AA 精度旋钮（O1 定案）
    LineCoverageGrid grid = GenerateLineCoverage(start.x, start.y, end.x, end.y, width, samples);
    if (grid.Empty()) return;                             // 第二层退化防线
    if (!m_patchSurface.Ensure(m_memoryDC, grid.width, grid.height))
    {
        Logger::Log(LogLevel::Warning, L"GDIBackend: patch surface unavailable - line AA skipped");
        DrawLineLegacy(start, end, width, color);         // fail-safe 沿 :452-458 先例：宁可无 AA
        return;
    }
    RasterizeMask(static_cast<std::uint8_t*>(m_patchSurface.bits),
                  m_patchSurface.width * 4, grid, color);   // C-B4 同族：stride 用 DIB 实际行宽
    BlendPatch(m_memoryDC, grid.originX, grid.originY, grid.width, grid.height);
}
```

3. **`PatchSurface::Ensure`**（`:909-943`）：参数改 `(HDC, int requiredWidth, int requiredHeight)`；重建判据 `requiredWidth > width || requiredHeight > height`；**重建尺寸 = 两维各自取 max**（只增不减）；其余（先建后替、负 biHeight、32bpp）逐行保留。
4. **`BlendPatch`**（`:898-907`）：`AlphaBlend(target, x, y, width, height, m_patchSurface.dc, 0, 0, width, height, blend)`。
5. **`FillPatchFromMask`**（`:894-895`）：stride 实参 `m_patchSurface.size * 4` → **`m_patchSurface.width * 4`**；stride 陷阱注释同步迁移（历史快照措辞按 O-4 纪律只登记不重写）。
6. **角路径调用点适配**（行为逐位不变）：`:452` `Ensure(m_memoryDC, R)` → `Ensure(m_memoryDC, R, R)`；`:868` `BlendPatch(m_memoryDC, x, y, R)` → `BlendPatch(m_memoryDC, x, y, R, R)`。
7. **O2 定案落点**：`Ensure` 成功且 `width*height > 2'097'152`（2M 像素 ≈ 8MB）且未告警过 ⇒ 一次性 `Warning`（沿 `kDiagnosticThreshold` 的 one-shot 模式，`bool m_areaWarned` 成员）。★★ **面积计算用宽类型**（v1.1 依评审 §十三）：`static_cast<long long>(width) * height`——诊断代码自身不得整数溢出；分配尺寸同检查。

### △7 · 测试承载

- 新建 `src/Tests/LineCoverageTests.cpp`（`RegisterLineCoverageTests()` + T24-1..T24-11 用例正文见 §6）。
- `RunAllTests.cpp:36` 后追加一行：`RegisterLineCoverageTests();   // Phase 24：DrawLine 线段抗锯齿（T24-1..T24-11）`。
- CMake：**零改动**（B10 实证）。

## 3. 契约 C1–C12 → 落点 → 测试全映射

| # | 契约 | 落点 | 测试 |
|---|---|---|---|
| **C1** | 生成层纯几何零 GDI（禁 Windows.h / HDC） | `LineCoverage.{h,cpp}` | 机检①（grep）+ 全部无头用例 |
| **C2** | `effW = max(1, lround(width))`；坐标 float 直入不取整 | △2-1 | T24-9 |
| **C3** | `samples <= 0` → 空网格 | △2 | T24-4 |
| **C4** | stadium 判定 = 点到线段距离 ≤ hw（含两端圆帽） | △2-4 | T24-1 |
| **C5** | grid 原点**单点出参**（originX/originY 随网格返回，调用方零重算） | △1/△2/△6-2 | T24-8b（错位即失败） |
| **C6** | 预乘一般化 `e=(c*a8+127)/255`；a8=255 时与角路径逐位同公式 | △4 | T24-7 |
| **C7** | 不变量 `RGB ≤ A`（逐像素） | △4 | T24-7 |
| **C8** | 分支序：`!AA` → legacy；`a≥1 且轴对齐` → legacy；其余 → 覆盖度 | △6-1 | T24-6 / T24-8b / T24-10 |
| **C9** | 覆盖度 S 来源 = `m_cornerMaskCache.GetSamples()`（单一旋钮） | △6-2 | 代码审查 + 机检 |
| **C10** | `PatchSurface` 两维只增不减 + 先建后替 | △5/△6-3 | T24-11 |
| **C11** | 角路径零回归（Ensure/BlendPatch 调用形态 `(R,R)` 等价改写） | △6-3/4/6 | T24-11 + 既有 AA 全绿 |
| **C12** | **C-SEAM**：接缝 over-blend vs 真并集 ≤ 28/255 且 ≤ 4 像素（★ 已知非零残差，非缺陷） | △2 + 端到端 | T24-8a / T24-8b |

## 4. 盯防清单（12 条，全部可机检）

| # | 盯防 | 机检方式 |
|---|---|---|
| ① | `LineCoverage.{h,cpp}` 零 `Windows.h` / `HDC` / `HBITMAP` | grep |
| ② | 覆盖度路径**坐标零取整**（`DrawLineCoverage` 内无对 x0..y1 的 `lround`；唯一 lround 在 effW） | grep |
| ③ | `IsAxisAlignedAfterRound` 用 `std::lround`（**禁 `int` 截断**——B11 负半区差 1） | grep + T24-6 |
| ④ | `BlendPatch` 全部调用点同步 5 参（生产 2 处：角 + 线段） | grep |
| ⑤ | `Ensure` 全部调用点同步 4 参（`:452` 角 + 新 1 处线段） | grep |
| ⑥ | `FillPatchFromMask` stride = `width * 4`（改后 grep `m_patchSurface.size` 应**零命中**） | grep |
| ⑦ | `PatchSurface` 重建先建后替（新资源就绪前不动旧） | 代码审查（决策 38 同款） |
| ⑧ | C7 不变量逐像素断言 | T24-7 |
| ⑨ | 公共头零改动（`git diff include/ECDI` 为空）· CMake 零改动 | git diff |
| ⑩ | 退化线段绝不进覆盖度路径（谓词 true → legacy → 0 像素 = A-7） | T24-6 |
| ⑪ | 批三零回归硬判据：既有 AA 用例（`TestMask*` + L2 系列）+ T24-11 全绿 | ecdi_tests |
| ⑫ | 探针程序不入仓库（`.workbuddy/`）· S=128 参照数据落 T24-2 注释 | git status |
| ⑬ | **T24-8b 期望像素走完整合成链辅助函数**（clear → premultiplied patch ×2 → AlphaBlend over ×2——不只比 coverage） | 代码审查（评审 §十一） |
| ⑭ | **面积诊断/告警用宽类型**（`long long`）——诊断代码自身不得溢出 | 代码审查（评审 §十三） |

## 5. 用例正文（T24-1..T24-11；★ 自动化 **+11** ⇒ **286 → 297**；对初设「+12 ≈ 298」做口径勘误——T24-10 为人工不计数，沿 Phase 19「用例数口径勘误」先例）

| # | 输入 | 期望 |
|---|---|---|
| **T24-1** | 斜线 `(3.2,7.7)→(9.8,13.1)`，w=1，S=16 | 横截单调（沿法线方向 coverage 不增反降无回升）；带内存在 ≥0.9 像素；带外（距中心线 > hw+1）恒 0；两端圆帽区存在覆盖（bbox 比矩形带多出的角像素 > 0） |
| **T24-2** | 同上几何 + **S=128 参照**（同函数不同 samples） | `sum(S=8) 与 sum(S=128) 的相对差 ≤ 冻结阈值 T`（T 由实施时实测回填详设 §7.1——预计 ≤1%，**以实测为准**，不得引用 B-1 推导）；另断言 `sum ≈ L×w + π(w/2)²`（粗校验，容差 5%） |
| **T24-3** | 同输入生成两次 | `coverage` 向量逐位相同 |
| **T24-4** | `samples = 0 / -3` → Empty；`samples = 7`（奇数）→ 非 Empty 且 width 正确 | 契约 C3 |
| **T24-5** | `(4,4)→(4,4)` 直接调 `GenerateLineCoverage` | Empty（第二层防线） |
| **T24-6** | 谓词（四例）：整数水平 `(10,10)-(25,10)` → **true**；round 后水平 `(10.4,10.4)-(20,10.4)`（两端 round 同 y）→ **true**；退化 `(4,4)-(4,4)` → **true**；真斜线 `(3.2,7.7)-(9.8,13.1)` → **false** | 契约 C8 / 盯防⑩ |
| **T24-7** | `RasterizeMask`：a8=255 对照 `RasterizeCornerPatch` 输出（同 c 数组手工构造）；a8=128 检查 `RGB ≤ A` 全像素 | a8=255 逐位相同（C6）；a8=128 不变量成立（C7） |
| **T24-8a** | 勾两段（探针 B1 几何），S=16：逐像素 `c1、c2、真并集` | `over = c2 + c1(1−c2)` vs 并集：偏差 ≤ 28/255 且 >1/255 的像素 ≤ 4（C12 几何层） |
| **T24-8b** | **真实 backend**（AAWindow 装置）：蓝底改白底 200×200，`SetAntiAliasing(true)`，连画勾两段（s=14, bw=1, 色=纯红），EndFrame 后读回肘点邻域（bbox 12×12 全像素）。★★ **期望像素由专用辅助函数计算**（v1.1 依评审 §十一）：`ExpectedOverBlend(bg, color, c1, c2)` 按**完整合成链**推导——`clear → AlphaBlend(patch1) → AlphaBlend(patch2)`，其中 patch_i = RasterizeMask 公式（C6）的预乘值，over = `s + d·(255−sA)/255`（AC_SRC_ALPHA 定义）——**必须覆盖「coverage → premultiplied → AlphaBlend」整条链，不得只比 coverage**（否则 8b 失去存在意义） | ① 肘点缺口不复现（GDI 现状 (6,10) 处读回**非背景色**——并集填补）；② 邻域逐像素 `|readback − ExpectedOverBlend|` ≤ 冻结容差（吸收 GDI AlphaBlend 与数学 over 的整型舍入差，实施时定标 ≤6/255 起步）；③ 偏差 >1/255 的像素 ≤ 4（C12 端到端） |
| **T24-9** | `GenerateLineCoverage(..., width = 0 / 0.4 / 2.5)` | effW = 1 / 1 / 3（以 bbox 尺寸与带宽锚点判定） |
| **T24-10** | 人工（用户侧 @125%）：勾 / 关闭 X / 还原框 | 边缘平滑；接缝无缺口无可见加深；AA 关闭逐位现状；与圆角品质同级 |
| **T24-11** | **真实 backend**：① 同一 backend 依序画 R=16 与 R=4 圆角矩形（历史 stride 缺陷触发形态）读回 R=4 角区断言正确；② 画斜线（W≠H bbox，如 40×20 的浅斜线）读回断言无行错位（每一行都有红色像素、行间不错位）；③ 既有 AA L2 用例全绿 | C10 / C11 |

## 6. 三批实现顺序（检查点制，沿初设 §8）

| 批 | 内容 | 出口判据 |
|---|---|---|
| **批一** | △1 + △2 + △7（LineCoverage + LineCoverageTests 的 T24-1..6, 8a, 9）+ RunAllTests 注册 | 构建 + **286 全绿** + 新 8 用例绿（零后端接线） |
| **批二** | △3 + △4（RasterizeMask）+ T24-7 | **289 全绿** |
| **批三** | △5 + △6（DrawLine 分支 + PatchSurface 矩形化 + O2 诊断）+ T24-8b / T24-11 | 四链 **297 全绿**（★ 角路径零回归硬判据）+ T24-10 人工目视 + 收尾（#48 登记 + 文档回写） |

## 7. 实施记录（实施时回填）

- §7.1 T24-2 的面积容差实测值 T：＿＿（S=128 参照 vs S=8，实施批一时实测回填）
- §7.2 逐条偏离（D-1..）：＿＿

## 8. 影响面（复核初设 §6，详设口径）

新建 3（`LineCoverage.h` / `LineCoverage.cpp` / `LineCoverageTests.cpp`）· 改动 4（`CoverageRaster.{h,cpp}` · `GDIBackend.{h,cpp}` · `RunAllTests.cpp`）· 公共头 **92 → 92 / API +0** · CMake **0 改动** · 用例 **286 → 297**。

## 9. 开放项收口（O1–O4 全闭合）

| # | 定案 |
|---|---|
| **O1** | S 来源 = `m_cornerMaskCache.GetSamples()`（△6-2 已落） |
| **O2** | `Ensure` 内一次性面积告警（阈值 2M 像素 ≈ 8MB；`m_areaWarned` one-shot） |
| **O3** | 触及像素 = **bbox 全扫 + 平方距离预过滤**（`hw + √½`）——实现最简、正确性显然；长线成本 ≈ bbox×~10ns + 触及×S²采样（500px 线 ≈ 1.5ms 上界，可接受；再优化待真实消费者出现） |
| **O4** | `#48`（`DrawPolyline`）随本阶段**收口时**登记（含重启条件 R-1 接缝零残差硬需求 / R-2 第三个折线消费者）——**立项须用户拍板** |

## 10. 局限（沿初设 §10：L1–L4 原样生效）

L1 接缝残差 = 已知非零残差（C-SEAM 阈值内，非缺陷）· L2 分数轴对齐亚像素信息被 legacy 丢弃 · L3 `a<1` 轴对齐（AA 开）走覆盖度路径带 AA 边缘 · L4 `PatchSurface` 两维只增不减（O2 告警兜底）。

## 11. 修订记录

- **v1.1**（2026-10-01）**评审处置 —— ✅ 通过，进入 Implementation**。① **评审结论**：「**详设 v1.0 通过，进入 Implementation**」「实施规格已经足够」——`originX/originY` 单点出参、坐标域钉死、stadium 定义、O3 预过滤、RasterizeMask 职责划分、C7 不变量、PatchSurface 矩形化、T24-2 两层校验、8a/8b 分层、三批顺序、297 口径**全部获认可**。② ★ **非阻塞 2 处小修**：**T24-6 推理痕迹清理**（四例明确化：整数水平 / round 后水平 / 退化 → true；真斜线 → false）· **面积诊断宽类型**（`long long`，盯防⑭）。③ ★ **实施注意 1 条升格为盯防⑬**：T24-8b 期望像素必须走**完整合成链辅助函数**（clear → premultiplied ×2 → AlphaBlend over ×2——只比 coverage 则 8b 失去存在意义）。④ ★ 评审的 8 条「必须严格遵守」与本文盯防清单对齐确认（纯几何零 GDI / 覆盖度坐标零取整 / legacy 逐字保留 / origin 单点出参 / stride=width*4 / 角路径逐位回归 / 8b 全链 / 四链跑完）。⑤ 头部 v1.0 → **v1.1**。
- **v1.0**（2026-10-01）初稿（实施规格）。**基线 B1–B12**（全部带行号实测——★ B11 legacy 用 `std::lround` ⇒ 谓词禁 int 截断）。**△1–△7 逐文件规格**（新建 3 + 改动 4；★ **△1 的 `LineCoverageGrid` 增加 `originX/originY` 字段**——初设 §5.5「单点出参」的落点，调用方零重算）。**契约 C1–C12 → 落点 → 测试全映射** · **盯防 12 条**（全部可机检）· **用例正文 T24-1..T24-11**（★ **用例口径勘误：自动化 +11 ⇒ 286 → 297**，初设「+12 ≈ 298」把人工 T24-10 误计入——沿 Phase 19 口径勘误先例）· **T24-8b 的参照实现定案 = 数学期望上界**（两帧分别单画无法复现 over-blend，评审建议的「另画参照位图」不可行，如实改为 8a 几何数据 + 预乘模型推导）· **三批**（批一 8 用例 / 批二 1 / 批三 2 + 人工）· **O1–O4 全收口**（O3 定案 bbox+预过滤）。**待评审。**
