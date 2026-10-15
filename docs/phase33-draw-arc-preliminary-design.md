# Phase 33 圆弧绘制原语（DrawArc）与环形进度控件（RingProgressBar）—— 初步设计

> **状态**：v1.0（2026-10-14）——初稿，**待评审**
> **输入**：需求稿 `phase33-draw-arc-requirements.md` **v1.1**（已吸收外部评审「有条件通过」）
> **模板**：沿 Phase 24「线段抗锯齿」初设的十节形态（同族：新覆盖度生成器 + 两后端消费 + 内部头）
> **定位**：把需求的 **C33-1..10 契约**落成**可实现的形态与决策**；实现细节（采样密度/缓存布局/缓冲区尺寸）在此定，界面契约不重开。

---

## 1. 需求 → 初设映射（本稿要闭合什么）

| 需求项 | 初设要定的事 |
|---|---|
| **C33-1** 几何定义（到弧中心线距离 ≤ hw） | 距离函数的**实现式**（含 `\|sweep\| = 360°` 的统一性）；角度判定**不用**升序区间 |
| **C33-2** 有向扫角 | 角度→弧内判定的**符号处理**；`startAngle` 的 `mod 360` 归约点 |
| **C33-3** 整圆与跨 0° | 跨 0° **是否需特判**（倾向：不需要——由 C33-1 单一式子自动覆盖）；接缝**不重不漏的机制证明** |
| **C33-4** 退化与钳制 | 钳制**发生在哪一层**（契约层 / 生成器 / 后端）与判定顺序 |
| **C33-5** 解析面积参照 | 参照式的分段（`< 360°` vs `= 360°`）；容差**来源**（实测冻结，不许推导） |
| **C33-6** 两后端几何一致 | ★ 判据放**哪一份共享件**（`BackendContract.h`？）——避免两端各写一遍 |
| **C33-7** GL 图集满降级 | 两个候选（弦逼近 / 硬边弧）**择一** + 与 `m_solid` 1×1 单元的复用方式 |
| **C33-8** GDI 补丁 bbox | 外扩量取多少；沿用 `patchSurface` 的「只增不减」语义 |
| **C33-9** 记录原样参数 | `RecordingBackend` 的记录结构（沿 `LineDraw` 形态）+ **归一化不下沉到后端**的校验点 |
| **C33-10** 控件最小行为 | 尺寸策略/动画重入/样式字段的**具体形态**；`Theme` 加 getter 的破坏面 |
| **R3** 命令层贯通 | 命令字段、variant 位置、`ExecuteCommand` 的**缩放量**（盯防③：哪些量要折 scale） |
| **R9** `PaintProbe` 交付项 | 初设只登记**接口影响**；跨仓库改动在详设列 step |

---

## 2. 覆盖度生成器的形态（R2 / C33-1 / C33-5）

### 2.1 数据结构：复用 `LineCoverageGrid` 还是另立？

- **决策 PD33-A：复用既有 `LineCoverageGrid`（不另立类型）。**
- **理由**（条 97 先例优先 + 条 22 YAGNI）：
  ① `RasterizeMask` 的形参类型**就是** `LineCoverageGrid`（`CoverageRaster.h:44`）——另立 `ArcCoverageGrid` 会在合成件处产生**第二套同构类型**，而该件是**共享件**（两端同调）；
  ② 该结构的字段名与「线段」**无耦合**（`originX/originY/width/height/coverage` 纯几何）；
  ③ 唯一的代价 = **名字里带 `Line`** ⇒ 用注释记明「本结构承载任意覆盖度源」。
- ★ **命名记账**：若将来第三个形状出现，再评估把类型名中性化（`CoverageGrid`）——**现在不做**（改共享件类型名会波及 `RasterizeMask` 签名与既有用例，属独立改动）。★ 这一条按「**一个概念一个词**」标尺有瑕疵（名字暗示线段），如实登记为**已知命名债**，不掩盖。

### 2.2 生成函数：`GenerateArcCoverage` 的签名与算法

```cpp
// src/Render/ArcCoverage.h（新 Internal 头——零 GDI、可无窗口单测）
LineCoverageGrid GenerateArcCoverage(float cx, float cy,
                                     float radius,
                                     float startAngleDeg, float sweepAngleDeg,
                                     float width,
                                     int samples = 8);
```

**算法骨架**（与 `GenerateLineCoverage` 逐段同构，只有距离函数与 bbox 不同）：

1. **入参守卫**（C33-4，全部 return 空网格）：`samples <= 0` · `!(isfinite(所有浮点入参))` · `radius <= 0` · `width <= 0` · `sweepAngle == 0`。
2. **钳制**（C33-4）：`sweep = clamp(sweepAngle, -360, 360)` · `effW = max(1, lround(width))` · `hw = min(effW, 2*radius) / 2`（★ 内半径非正 ⇒ 钳制 ⇒ **退化为扇形**，语义已在需求 C33-4 明写）。
3. **bbox**（C33-8）：`[cx ± (radius + hw + 1), cy ± (radius + hw + 1)]` 与 `floor` 取整 —— **保守外扩**（整圆取满，弧段也取满：**不做按角度的 bbox 收缩**，理由见 2.4）。
4. **逐像素超采样**：像素中心预过滤（`dist > hw + √½` ⇒ 跳过）→ `samples²` 子采样 → 命中计票 → `lround(255·hit/total)`。
5. **命中判据 = C33-1 的单一式子**：
   ```
   d = |dist(p, center) - radius|          // 到「整圆」的距离
   inBand = (d <= hw)
   inSector = angleInside(p, center, start, sweep)   // 见 2.3
   hit = inBand && inSector
   ```
   ★ **`|sweep| = 360°` 时 `inSector ≡ true`**（全周）⇒ 自动得到圆环，**无接缝特判**——这正是 C33-3① 要的机制证明。

### 2.3 角度判定（C33-2 —— 需求里最关键的订正）

```cpp
// 伪代码：点 p 是否落在 [start, start+sweep] 的（带符号）扇区内
float a = atan2(py - cy, px - cx) 转成「度、3 点钟=0、顺时针为正」；然后 mod 360 ⇒ [0, 360)
float rel = mod360(a - mod360(start));
if (sweep > 0) return rel <= sweep;          // 顺时针
else           return (360 - rel) <= -sweep; // 逆时针（等价于 rel >= 360 + sweep）
```

★ **三条纪律**：
① **禁止**写成 `a >= start && a <= start + sweep`（负 sweep 下恒假——需求 v1.0 的矛盾点）；
② `mod360` 用 `fmod` + 补偿负值（**不得**用 `static_cast<int>` 截断，条 103 同族的负坐标教训）；
③ **`sweep` 取 `±360` 时直接短路 `inSector = true`**——避免 `atan2` 在接缝处因浮点舍入把最后一丝扇区判掉（否则整圆会在 0° 处留**一列缝**，这正是 C33-3② 要防的「漏像素」）。

### 2.4 bbox 不做角度收缩（决策 PD33-B）

- **候选**：① 保守整圆 bbox（上述）② 按扇区算真实 bbox ③ 折中（≤180° 用弦包围）
- **取 ①**，理由：**代价可接受、收益不成比例**——`radius` 是半径而非直径量级时，整圆 bbox 与扇区 bbox 的最坏面积比 = 4×（90° 弧），而生成成本已被**像素级预过滤**吸收（带外像素只花一次距离比较，不进 `samples²` 循环）；
  且 ②③ 都要处理**跨 0° 区间的包围盒**（横跨 ±180° 时 `min/max` 不成立）——**新逻辑 = 新错源**。
- ★ 这与 `LineCoverage` 的 bbox 策略同族（那里也是「两端各让出 `hw`」的保守包围）。
- ★ **对 `PatchSurface` 的连带**（C33-8）：GDI 补丁面按 `(grid.width, grid.height)` 复用，保守 bbox 会让补丁面**更大**——但 `PatchSurface` 两维**只增不减**（既有语义），大 bbox 只是让它早一点到达稳定尺寸，**不是**每帧新分配。可接受。

---

## 3. 两后端的实现（R4 / C33-6 / C33-7）

### 3.1 GDI：完全照搬 `DrawLineCoverage` 三步

```
GenerateArcCoverage(...) → grid.Empty()? return
  → m_patchSurface.Ensure(m_memoryDC, grid.width, grid.height)
  → RasterizeMask(bits, m_patchSurface.width*4, grid, color)
  → BlendPatch(m_memoryDC, grid.originX, grid.originY, grid.width, grid.height)
```
- **失败降级沿既有先例**：`Ensure` 失败 ⇒ Warning + **不画**（★ 与 `DrawLine` 的「宁可无 AA，不可缺绘制」**不同**：那里有 legacy GDI 路径可退（`DrawLineLegacy`），而**圆弧没有 legacy 原语**（GDI `Arc`/`AngleArc` 是描边设备接口，与「覆盖度带」几何不同、且 W 版参数语义另有坑）⇒ **本 Phase 不为 GDI 造 legacy 弧**，失败即跳过并记 Warning。★ 这是**有意的能力边界**，如实登记为局限 L33-1。
- **`m_antiAliasing` 开关**：沿 `DrawLine` 语义——**AA 关闭时怎么画？** ★ 决策 **PD33-C**：AA 关闭 ⇒ **仍走覆盖度路径**（不降级为硬边）。理由：圆弧**没有** legacy 路径（同上）；若 AA 关就整条不画，会让「关 AA」这个后端级开关**静默吞掉整类绘制**（条 104 的静默失效）。⇒ 契约 = **AA 开关对圆弧只影响「无 AA 可用」的语义，不影响是否绘制**；而在覆盖度路径内部，AA 关时**不做预过滤以外的额外处理**（保持逐位一致，便于断言）。

### 3.2 GL：`ArcSlot` + 图集满降级（C33-7）

- **`ArcSlot` 形态**：镜像 `LineSlot`（`GLRenderer.cpp:756-780`）——缓存键 = 几何量化到 1/8 px 的元组；`Allocate` → `Upload(grid.coverage)` → `TexturedQuad(slot, originX, originY, grid.width, grid.height)`。
- **缓存键必须含哪些量**（★ 漏一个就退化成「每帧新分配 ⇒ 图集迅速填满」——`LineSlot` 的 spike 教训原文：~89 帧即满 ⇒ 堆越界崩）：
  `(grid.width, grid.height, lround(cx*8), lround(cy*8), lround(radius*8), lround(start*8), lround(sweep*8), lround(width*8))`
  ★ **`start`/`sweep` 必须入键**——这是与 `LineSlot` 的关键差异（线段的键是端点；弧的几何由角度决定，**端点相同但角度不同 = 不同覆盖度**）。
- ★★ **图集满降级（决策 PD33-D）：取「硬边弧」而非「弦多段逼近」。**
  - **候选①「弦多段 `SolidSegment`」被否**：`SolidSegment` 是**实心四边形**（`GLRenderer.cpp:618-644`），用它逼近弧需要**自适应段数**（段数少 ⇒ 明显多边形；段数多 ⇒ 每段一次 `glBegin/End`，与「命令数不爆炸」的 Phase 27/28 目标冲突），且**段的接缝处会重复覆盖**（半透明色叠加变深）。
  - **候选②「硬边弧」= 取 ①**：复用**同一份覆盖度网格**，把 coverage **二值化**（`>0 ⇒ 255`）后走**同一个 `ArcSlot` + `TexturedQuad`**。⇒ **几何语义与 AA 路径完全一致**（同网格、同形状），只是边缘硬；新增代码 ≈ 0（一个布尔参数）；**必无接缝问题**（同一张图）。
  - ★ 这与 GDI 侧「AA 关也走覆盖度」的 PD33-C **同构**——两端同哲学：**弧的几何只有一个来源，AA 只改边缘质量**。
- ★ **降级路径的既有先例对照**：`DrawLine` 的降级之所以能用 `SolidSegment`，是因为**线段本身就是四边形**（形状等价）；**弧不是**。⇒ C33-7 的契约（「降级必须仍是弧」）在此落地为**二值化同网格**。

### 3.3 两后端几何一致性（C33-6 的落点）

- **决策 PD33-E：把「弧的几何判据」抽成 `BackendContract.h` 里的纯函数**，两端**调用同一符号**：
  ```cpp
  namespace BackendContract{
      /// 入参归一化（角度归约 + 钳制）——两端共用，杜绝各自解释 C33-2/C33-4
      struct ArcParams{ float cx, cy, radius, startDeg, sweepDeg, halfWidth; bool drawable; };
      ArcParams NormalizeArc(float cx, float cy, float radius,
                             float startDeg, float sweepDeg, float width) noexcept;

      /// 点是否在弧带内（C33-1 + C33-2 的唯一实现）
      bool ArcContainsPoint(const ArcParams& p, float x, float y) noexcept;
  }
  ```
- **理由**（`BackendContract.h` 头注释的既有判据，条 104 同族）：**共享代码强于共享断言**——两条断言各写一遍仍可能漂移；**同一份代码不可能漂移**。★ 且该件正是 #62「§7.2 后端契约线」为此而建的落点。
- ★ **可测性**：这两个函数是**纯函数** ⇒ 真值表用例可无头跑（不需要 GL context），与 `ImageIsDrawable` / `QuantizeEdge` 的验收形态一致。★ 这是**C33-6「几何语义必须同一」的可机检形态**（比「两端各写测试再人工比对」强）。

---

## 4. 命令层与公共 API（R3 / C33-9）

### 4.1 公共签名（C33-1 的落地）

```cpp
// RenderingBackend.h（纯虚——D5 已裁决）
virtual void DrawArc(const Point& center, float radius,
                     float startAngleDeg, float sweepAngleDeg,
                     float width, const Color& color) = 0;
```
- ★ **`Point center` 而非新类型**：不引入「圆」值类型（YAGNI；全库无此概念，为单个消费者造类型撞条 22）。
- ★ 参数名带 `Deg` 后缀：**让单位在调用点可见**（条 95① 命名即语义；全库首个角度 API，后缀是零成本的单位标注）。

### 4.2 命令与展开（★ 盯防③：缩放量清单）

```cpp
struct DrawArcCommand
{
    Point center;              ///< 圆心（最终坐标）
    float radius = 0.0f;       ///< 半径
    float startAngleDeg = 0.0f;///< 起始角（度；**不随 DPI 缩放**）
    float sweepAngleDeg = 0.0f;///< 有向扫角（度；**不随 DPI 缩放**）
    float width = 1.0f;        ///< 线宽
    Color color;
};
```
`Renderer::ExecuteCommand(const DrawArcCommand&, float scale)`：
- **要折 scale 的量 = `center`（点）、`radius`、`width`**；
- ★★ **不折的量 = 两个角度**——角度是**无量纲**的，折它 = 把 90° 变成 135°（150% 下弧长错得离谱）。这是 `ScaleLength` 家族里**第一个「float 但不可折」的形参**，必须在实现处以注释钉死（沿 `Font::size` 不折的同族纪律，`Renderer.cpp:28-34` 已有先例说明）。

### 4.3 `PaintContext::DrawArc` + 剔除

- `PaintContext` 侧 = 一行 `emplace_back`（沿既有形态）。
- ★ **视口剔除（Phase 27）**：`Widget::Paint` 的 `self` 判据用的是**控件自身矩形**（不由命令类型决定），故 `DrawArc` **不需要**新的剔除逻辑；★ 但**`RingProgressBar` 的 `OnPaint` 必须自己用 `ctx.IsRectVisible(...)` 剪枝**吗？——**不需要**：控件的 bbox 就是整环外接方，`Widget::Paint` 已覆盖。如实登记为「无新增剔除面」。

### 4.4 `RecordingBackend`

```cpp
struct ArcDraw
{
    Point center; float radius; float startAngleDeg; float sweepAngleDeg;
    float width; Color color;
};
std::vector<ArcDraw> arcDraws;
```
- ★ **原样记录**（C33-9）：**不做**角度归约/钳制——归一化发生在**后端执行端**（`BackendContract::NormalizeArc`）。★ 若记录端先归一化，「断言原样转发」就失去意义（K21 同族的既有观测设计）。
- ★ **配套用例义务**（详设 T 清单）：**同一组非法输入**（如 `sweep = 450`）在 `RecordingBackend` 处**必须记录 450 原值**——这条断言正是「归一化没下沉」的**可机检证据**。

---

## 5. `RingProgressBar` 控件（R6 / C33-10）

### 5.1 形态

```cpp
class RingProgressBar : public Widget
{
public:
    RingProgressBar();                                   // 构造体注入 RingProgressBarStyle
    void SetProgress(float progress);                    // clamp [0,1]；同目标 no-op；NaN 拒绝
    void SetPercent(int percent);
    [[nodiscard]] float GetProgress() const noexcept;
    void ApplyTheme(const Theme& theme);
    void SetStyle(RingProgressBarStyleOverride override);
protected:
    void OnPaint(PaintContext& ctx, int x, int y) override;
    virtual AnimationManager* ResolveAnimationManager() const noexcept;   // 测试接缝（沿 ProgressBar 先例）
    float m_displayProgress = 0.0f;
    RingProgressBarStyle m_style;
private:
    void AnimateTo(float target);
    static constexpr int kTransitionMs = 200;            // 沿 ProgressBar（K15）
    static constexpr float kEpsilon = 1e-6f;
    float m_progress = 0.0f;
    AnimationToken m_animToken;
};
```

### 5.2 `OnPaint` 的两条命令（+ 退化）

```
D = min(width, height);  R = D/2 - hw;   // 圆心 = 控件中心；hw = 线宽/2
① 轨道：DrawArc(center, R, 270°, 360°, hw*2, trackColor)      // 整环
② 进度：DrawArc(center, R, 270°, m_displayProgress*360°, hw*2, fillColor)   // 0 进度 ⇒ sweep=0 ⇒ 不画（C33-4）
```
★ **决策 PD33-F：轨道与进度都走 `DrawArc`，同一半径**（同心同宽）——不引入「用 `DrawRoundedRect` 画轨道」的第二机制（否则轨道与进度的半径/宽度会有**两套算法**，正是 C33-6 在控件层的对应风险）。
★ **退化**：`R <= 0`（控件太小）⇒ **两条命令都发但 `DrawArc` 内部按 C33-4 跳过**（与 `ProgressBar` 「0 进度仍发命令」的既有语义同族：**命令流可断言**，后端负责 no-op）。

### 5.3 `Theme` 的破坏面（★ 本稿新勘明，需求未覆盖）

- 现状：`Theme` 有 **8 个纯虚 getter**（`Theme.h:22-29`），**实现者 2 个**：`DefaultTheme`（库内）+ `RedButtonTextTheme`（**测试替身**，`ThemeTests.cpp:177-195` 逐个 override）。
- ⇒ 加第 9 个纯虚 `GetRingProgressBarStyle()` ⇒ **`RedButtonTextTheme` 编译失败，必须同步加一行**。
- ★ **与 `RenderingBackend` 的破坏面不同**：`Theme` 的库外实现者**经查为零**——harness 的 `Theme.h` 只是**颜色常量集合**（不继承 `ECDI::Theme`，实测 grep）⇒ **破坏面仅库内 2 处**（`DefaultTheme` + 1 个测试替身）。
- **决策 PD33-G：取纯虚**（与 `RenderingBackend::DrawArc` 同口径——新增能力由每个主题显式表态；避免「忘了给环配色 ⇒ 静默继承错色」）。★ 代价如实登记：同步 2 处。

### 5.4 样式字段（C33-10④）

```cpp
struct RingProgressBarStyle{
    StyleField<Color> trackColor;
    StyleField<Color> fillColor;
    StyleField<float> thickness;    ///< 环宽（px）；<= 0 ⇒ 沿 C33-4（不画）
};
```
- **不放**内外边距（无消费者，条 22）；**不放** `startAngle`（D7 已裁决不开放）。
- ★ `thickness` 与 `GetSize()` 的关系：`R = min(w,h)/2 - thickness/2`；若 `thickness > min(w,h)` ⇒ `R <= 0` ⇒ 按 C33-4 不画（**不钳制 `thickness`**——钳制会让「环宽」显示成用户没设过的值，比不画更难诊断）。

---

## 6. 影响面

| 类别 | 文件 | 说明 |
|---|---|---|
| 🆕 新建 | `ECDI/src/Render/ArcCoverage.h/.cpp` | 覆盖度生成器（Internal，零 GDI） |
| 🆕 新建 | `ECDI/include/ECDI/Theme/RingProgressBarStyle.h` | 样式（沿 `ProgressBarStyle.h` 形态） |
| 🆕 新建 | `ECDI/include/ECDI/Widget/RingProgressBar.h` + `ECDI/src/Widget/RingProgressBar.cpp` | 控件 |
| 🆕 新建 | `ECDI/src/Tests/ArcCoverageTests.cpp` | 生成器四类测试（R8） |
| ✏️ 改 | `ECDI/include/ECDI/Render/RenderingBackend.h` | **+1 纯虚**（破坏面：3 库内 + 1 库外 `PaintProbe`） |
| ✏️ 改 | `ECDI/include/ECDI/Render/RenderCommand.h` | +`DrawArcCommand` + variant +1 |
| ✏️ 改 | `ECDI/src/Render/Renderer.h/.cpp` | +1 `ExecuteCommand` 重载（**漏加即编译错误**） |
| ✏️ 改 | `ECDI/include/ECDI/Render/PaintContext.h` + `.cpp` | +`DrawArc` |
| ✏️ 改 | `ECDI/src/Render/GDIBackend.h/.cpp` | +override（覆盖度→补丁面） |
| ✏️ 改 | `ECDI/src/Render/GLRenderer.h/.cpp` | +override + `ArcSlot` + 缓存 map + 二值化降级 |
| ✏️ 改 | `ECDI/src/Render/RecordingBackend.h/.cpp` | +`ArcDraw` + 记录 |
| ✏️ 改 | `ECDI/src/Render/BackendContract.h` | +`ArcParams` / `NormalizeArc` / `ArcContainsPoint` |
| ✏️ 改 | `ECDI/include/ECDI/Theme/Theme.h` + `DefaultTheme.h/.cpp` | **+1 纯虚**（破坏面：`DefaultTheme` + `RedButtonTextTheme`） |
| ✏️ 改 | `ECDI/src/Tests/ThemeTests.cpp` | 替身补 override（破坏面同步） |
| ✏️ 改 | `ECDI/src/Tests/RunAllTests.h/.cpp` | 注册新测试文件 |
| ✏️ 改 | `CMakeLists.txt` | 新源文件入列表（★ 唯一真相源，条 66） |
| ✏️ 改 | `ECDI_fake_harness/gui/src/PaintProbe.h/.cpp` | **R9 交付项**（跨仓库） |

★ **公共头 96 → 98**（`RingProgressBarStyle.h` + `RingProgressBar.h`）· **API 净增**：1 纯虚 + 1 命令 + 1 `PaintContext` 方法 + 1 控件的 5 个方法 + 1 样式类型 + 1 `Theme` 纯虚。
★ **零改动**：`Widget` 基类 / `Layout` / 事件层 / `Window` / `Application`（`RenderingBackend` 的**既有 6 原语语义零变化** = R7①）。

---

## 7. 测试大纲

### 7.1 生成器（T33-1..，无头；★ R8 四类）

| # | 用例 | 类别 |
|---|---|---|
| T33-1 | **值域与网格完整性**：全像素 `0..255`；`originX/Y/width/height` 与 bbox 公式一致 | ①合法性 |
| T33-2 | **面积守恒·解析参照**（C33-5）：`\|sweep\|=90/180/270` 三分档，`\|Σcov/255 − A_analytic\| / A ≤ 实测冻结容差`；★ **`\|sweep\|=360` 单独一档且参照式为 `sweep·r·w`（不追加圆帽）** | ①合法性 |
| T33-3 | **镜像对称**：`start=0,sweep=90` 与其关于 x 轴的镜像（`start=270,sweep=-90` 等效）逐像素对应 | ②对称 |
| T33-4 | **跨 0°**：`start=350,sweep=20` 与 `start=10,sweep=20` 的**重叠部分**一致；且**不出现空弧** | ③端点接缝 |
| T33-5 | **整圆接缝**：`sweep=360` 在 `0°` 邻域**无零覆盖率列**（C33-3②的漏像素防线）；与 `sweep=-360` **逐位相同** | ③端点接缝 |
| T33-6 | **圆帽端点**：`sweep=90` 的两端点处覆盖度 > 0（平头会给出 0 或锐减）；中心线端点外侧 `hw` 处 → 0 | ③端点接缝 |
| T33-7 | **退化全表**（C33-4）：`radius<=0` / `width<=0` / `sweep==0` / NaN / ±Inf / `\|sweep\|>360`（钳制后非空且等于 360 档）/ `radius<=width/2`（**扇形**：中心像素覆盖度 = 255） | ④退化 |
| T33-8 | **确定性**：同输入两次逐位相同（沿 `LineCoverage.Determinism`） | ④退化 |
| T33-9 | **`ArcContainsPoint` 真值表**（`BackendContract`）：含负 sweep、跨 0°、整圆短路——**纯函数无头可跑** | ①合法性 |

### 7.2 命令层（T33-10..，无头）

| # | 用例 |
|---|---|
| T33-10 | `PaintContext::DrawArc` → 命令流 +1 且**字段原样** |
| T33-11 | `RecordingBackend` 记录**原样参数**：传入 `sweep=450` ⇒ 记录 **450**（C33-9 的可机检证据） |
| T33-12 | `Renderer` 展开：scale=1.5 时 **center/radius/width 折、两个角度不折**（★ 盯防③的专用断言） |
| T33-13 | `Theme` 破坏面：`DefaultTheme::GetRingProgressBarStyle()` 返回非空默认值；`RedButtonTextTheme` 编译通过（**回归锚**） |

### 7.3 控件（T33-14..，无头 + 真窗口各半）

| # | 用例 |
|---|---|
| T33-14 | `SetProgress` clamp / 同目标 no-op / **NaN 拒绝**（值不变、不启动动画） |
| T33-15 | 动画重入：过渡中再 `SetProgress` ⇒ **从当前呈现值**重新过渡（替换式重启） |
| T33-16 | `OnPaint` 命令流：2 条 `DrawArc`（轨道 360° + 进度 `p*360°`）；`p=0` ⇒ 仍 2 条 |
| T33-17 | 尺寸策略：宽高不等 ⇒ 直径取 `min`、圆心居中（命令流字段断言） |
| T33-18 | 真窗口：`RingProgressBar` 上屏 + `p=0.75` 时**进度弧可见**（像素级，沿 `LineCoverage` T24-8b 形态） |

★ **人工观感项（不可自动化，如实登记）**：AA 边缘质量 / 圆帽观感 / 环与刻度对齐（**GL 无 WGL context ⇒ 结构上不可自动化**，沿 `BackendContract.h` 头注释②的既有结论）。

---

## 8. 实现顺序（三批）

| 批 | 内容 | 可独立验收 |
|---|---|---|
| **批一** | `BackendContract` 弧原语（`NormalizeArc` / `ArcContainsPoint`）+ `ArcCoverage.h/.cpp` + T33-1..9 | ✅ 纯生成器，无头全绿即过（**不碰任何公共头**） |
| **批二** | 公共 API 贯通：`RenderingBackend` +1 纯虚 · `RenderCommand` · `Renderer` · `PaintContext` · `RecordingBackend` + T33-10..12 | ✅ 命令流断言（3 实现者必须同步——**编译失败即暴露漏改**） |
| **批三** | 两后端真实绘制（GDI 补丁面 / GL `ArcSlot` + 降级）+ `RingProgressBar`（样式 + `Theme` + 控件）+ T33-13..18 | ✅ 真窗口像素 + 观感 |
| **批四（跨仓库）** | `PaintProbe` 加转发 override + 验证（转发/计数/计时） | ✅ harness 重新构建 + 读数 |

★ **批一可先行**且**零公共头改动**——这与 Phase 24「批一 `LineCoverage` 独立落地」同构；若批一评审发现问题，代价只有生成器。

---

## 9. 开放项（O33-x，均带倾向）

| # | 项 | 倾向 |
|---|---|---|
| O33-1 | **采样密度**：弧是否也用「角缓存同一旋钮」（`m_cornerMaskCache.GetSamples()`）？ | **是**——沿 `DrawLineCoverage` 的既有做法（契约 C9「全框架 coverage AA 单一精度」） |
| O33-2 | **`ArcSlot` 缓存是否设上限** | **暂不设**——键的量化粒度（1/8 px）+ 角度量化已限制基数；若实测超 32 条再评估（`CornerMaskCache` 有 32 条诊断阈值先例，可移植） |
| O33-3 | **`LineCoverageGrid` 命名中性化** | **不做**（见 2.1 的命名债登记） |
| O33-4 | GDI 是否补 legacy 弧（`AngleArc`） | **不做**（见 3.1 局限 L33-1） |
| O33-5 | `RingProgressBar` 是否支持**多圈/反向**（`SetReversed`） | **不做**（无消费者） |

---

## 10. 局限（L33-x，如实登记）

| # | 局限 | 影响 |
|---|---|---|
| **L33-1** | **GDI 无 legacy 弧降级**：补丁面分配失败 ⇒ 该弧**不画**（仅 Warning）。GL 侧则有二值化降级（PD33-D） | 两端在「资源失败」下行为**不同**（GDI 不画 / GL 画硬边弧）——★ 这是**已知且有意**的不对称（GDI 无等价原语）；若将来出现消费者要求 GDI 也降级，再评估 `AngleArc` 路线 |
| **L33-2** | **`\|sweep\| > 360°` 被钳制**，不是多圈 | 调用方写 450 得到的是一整圈（无重叠），与「画 1.25 圈」的直觉不同——已在 C33-4 明写 |
| **L33-3** | **`radius <= width/2` 退化为扇形** | 与 D1「不做楔形」的边界：此形态**只在此输入下出现**，是几何后果而非 API 能力（C33-4 已明写） |
| **L33-4** | **`LineCoverageGrid` 类型名暗示「线段」** | 命名债（2.1）；纯可读性问题，无行为影响 |
| **L33-5** | **AA 质量无自动化验收**（GL 无 WGL context） | 沿 `BackendContract.h` 头注释②的既有结论：靠**纯函数真值表** + **人工观感项** |

---

## 11. 修订记录

- **v1.0**（2026-10-14）初稿。**输入**：需求稿 **v1.1**（C33-1..10 契约 + R1–R9）。**内容**：§1 需求→初设映射 · §2 生成器形态（PD33-A 复用 `LineCoverageGrid` / PD33-B bbox 不收缩 + 算法骨架与角度判定伪码）· §3 两后端（PD33-C GDI 关 AA 仍走覆盖度 / PD33-D GL 图集满取「二值化同网格」**否决弦多段** / PD33-E 几何判据抽入 `BackendContract.h` 两端同符号）· §4 公共签名与命令层（★ 角度**不折 scale** 的盯防）· §5 控件（PD33-F 轨道也走 `DrawArc` / **PD33-G `Theme` 加纯虚的破坏面仅库内 2 处**——★ 本稿新勘明，harness 的 `Theme.h` 只是颜色常量集合、不继承 `ECDI::Theme`）· §6 影响面（公共头 96→98）· §7 测试大纲 T33-1..18（四类 + 人工观感项）· §8 四批（批一零公共头改动）· §9 开放项 O33-1..5 · §10 局限 L33-1..5。**待评审。**
