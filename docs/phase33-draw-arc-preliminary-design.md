# Phase 33 圆弧绘制原语（DrawArc）与环形进度控件（RingProgressBar）—— 初步设计

> **状态**：**v1.1**（2026-10-14）——★ **吸收第二轮外部评审（「有条件通过」）**：五项 P1 + 四项补齐项**全部采纳**（端点判据改单一「到弧段距离」式 · 负扫角起点 · 面积式补弧度 · 预过滤变量命名 · 扇形退化与 T33-7 断言 · 共享契约与生成器接线 · 关 AA 取二值化 · 缓存键量化单位 · 批次数订正）；**待复核**
> **输入**：需求稿 `phase33-draw-arc-requirements.md` **v1.2**（C33-1/C33-5 已按本轮评审订正）
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
4. **逐像素超采样**：像素中心**预过滤**（见下 2.2.1 —— ★ 变量必须显式命名，v1.0 初稿的 `dist` 有歧义）→ `samples²` 子采样 → 命中计票 → `lround(255·hit/total)`。
5. ★★ **命中判据 = 唯一的「到弧段距离 ≤ hw」**（见 2.2.2 —— **v1.1 订正：v1.0 写的是 `inBand && inSector`，那把圆帽切成了平头**）。

#### 2.2.1 预过滤的变量定义（★ 评审 §4 订正：v1.0 的 `dist` 照字面实现会跳过整条弧）

```cpp
// ★ 两个量必须分开命名——v1.0 混用 `dist` 会让「半径 100px 的弧全部被跳过」
const float radialDistance           = std::sqrt(sq(px - cx) + sq(py - cy));   // 到**圆心**
const float distanceToArcCenterline  = std::fabs(radialDistance - radius);    // 到**弧中心线**（整圆口径）
if (distanceToArcCenterline > hw + 0.70710678f)   // √½ = 像素半对角线
{
    continue;   // 整格带外 ⇒ coverage 保持 0（不进 samples² 循环）
}
```
★ **语义**：判据里的距离**恒指「到弧中心线」**，`radialDistance` 只是它的中间量。★ 阈值 `hw + √½` 与 `LineCoverage` 的 `skipRadius` 同式（三角不等式：格内任一点距中心 ≤ √½）。

#### 2.2.2 命中判据 = 到**有限弧段**的距离（★ 评审 §1 订正——这是本稿最重要的一处）

```cpp
// 唯一判据：点 p 到「弧段」（有限圆弧曲线，含两端点）的距离 ≤ hw
// ★ 与 LineCoverage 的 DistSqToSegment 逐行同构，只有 t 的解析式从「线段投影」换成「角度投影」
float t = DirectedAngleFraction(p, center, startDeg, sweepDeg);   // 见 2.3：把 p 投影到弧段的参数 ∈ [0,1]
t = std::clamp(t, 0.0f, 1.0f);                                     // ★ 钳到 [0,1] ⇒ 端点外退化为「到端点的距离」
const float qx = cx + radius * cosf(AngleAt(startDeg, sweepDeg, t));   // 弧段上参数 t 处的点
const float qy = cy + radius * sinf(AngleAt(startDeg, sweepDeg, t));
const bool hit = (sq(px - qx) + sq(py - qy)) <= hw * hw;
```

★★ **为什么必须写成这样（v1.0 的 `inBand && inSector` 为什么错）**：

| 写法 | 端点 | 圆心（`radius <= hw`） |
|---|---|---|
| `inBand && inSector`（v1.0） | **平头**——扇区把圆帽切掉，与 T33-6「圆帽」自相矛盾 | 需 `atan2(0,0)` ⇒ **未定义** |
| **到弧段距离 ≤ hw**（v1.1） | **圆帽**——端点处弧段即端点，`t` 被钳到 0/1 ⇒ 距离退化为「到端点」⇒ 球邻域自然是**半圆盘** | **无需特判**——`radius <= hw` 时圆心距弧段 = `radius ≤ hw` ⇒ 自动命中，**全程不调 `atan2`** |

★ **单一判据同时解决评审的 §1（端点）与 §5（圆心归属）两条**——这正是「唯一式子」的价值：**`t` 的钳制把「端点圆帽」与「圆心归属」都变成了同一个式子的自然推论**，不再需要两处特判。
★ **与 `LineCoverage` 的同构性**（条 97）：`DistSqToSegment` 的 `t = clamp(投影, 0, 1)` ⇒ 线段两端天然圆帽；本式只是把「直线投影」换成「角度投影」，**结构逐行对应**，便于对照审阅。
★ **`|sweep| = 360°`**：`t` 的定义域覆盖全周 ⇒ 判据退化为 `|radialDistance − radius| ≤ hw` = 圆环，**无接缝特判**（C33-3② 的机制证明）。

### 2.3 角度投影 `DirectedAngleFraction`（C33-2 —— ★ 评审 §2 订正：v1.0 的负扫角漏掉起点）

```cpp
// 输入：点 p、圆心、起始角、有向扫角（度）；输出：p 在弧段上的参数 t（可超出 [0,1]，由调用方钳制）
// ★★ 关键：正负扫角必须用**同一条「从起点沿扫描方向的有向角差」规则**——v1.0 的
//    (360 - rel) <= -sweep 在 rel == 0（点恰在起始角上）时给出 360 <= |sweep| ⇒ **起点被排除**，
//    而正扫角的 rel <= sweep 却包含起点 ⇒ 正负不对称、镜像对称测试会挂。
float DirectedAngleFraction(float px, float py, float cx, float cy,
                            float startDeg, float sweepDeg)
{
    // ① 点相对圆心的极角（度、3 点钟 = 0、顺时针为正）
    float a = atan2f(-(py - cy), px - cx) * 180.0f / kPi;   // ★ y 取负 ⇒ 顺时针为正（与契约 C33-2 一致）
    // ② 沿**扫描方向**度量的有向角差，归约到 [0, 360)
    float delta = (sweepDeg >= 0.0f) ? (a - startDeg) : (startDeg - a);
    delta = Mod360(delta);                                   // fmod + 补偿负值（禁 int 截断）
    // ③ 归一化到参数域：|sweep| 为分母 ⇒ t ∈ [0,1] 即落在弧段内，>1 即超出端点
    return delta / std::fabs(sweepDeg);
}
```
★ **纪律四条**：
① **禁止**写成 `a >= start && a <= start + sweep`（负 sweep 下恒假——需求 v1.0 的矛盾点）；
② **正负对称**：`sweep < 0` 时用 `(start − a)` 而非 `(a − start)`，**起点处两式都给 `delta = 0 ⇒ t = 0`** ⇒ 起点**恒在弧段内**（评审 §2 的修法）；
③ `Mod360` 用 `fmod` + 补偿负值（**不得**用 `static_cast<int>` 截断，条 103 同族的负坐标教训）；
④ **`|sweep| = 360°` 时 `t` 的分母为 360** ⇒ 全周映射到 `[0,1]`；★ 为避免 `atan2` 在接缝处的浮点舍入把最后一丝判掉（整圆在 0° 处留一列缝），**`|sweep| == 360°` 时直接短路为 `hit = (|radialDistance − radius| ≤ hw)`**——即 C33-3② 的接缝防线（此短路是**纯优化与浮点保险**，不改变判据语义：`t` 公式在整圆下已等价）。
★ **容差**（评审 §2 末句要求）：角度的浮点容差**统一在共享件里规定**——命中判据本身用**距离比较**（`≤ hw*hw`）而不是角度比较，故**角度容差只影响 `t` 的边界归属**（±1 个采样点），对覆盖率的影响随 `samples` 增大而收敛；⇒ **不额外引入角度 epsilon**，与 `LineCoverage` 的「不做 epsilon、靠距离比较」口径一致。


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
- **`m_antiAliasing` 开关**：沿 `DrawLine` 语义——**AA 关闭时怎么画？** ★ 决策 **PD33-C（v1.1 明确取边）**：AA 关闭 ⇒ **仍走覆盖度路径，但把覆盖度网格二值化**（`>0 ⇒ 255`）后合成。
  · **为什么不整条不画**：圆弧**没有** legacy 路径（同上）；若 AA 关就整条不画，会让「关 AA」这个后端级开关**静默吞掉整类绘制**（条 104 的静默失效）。
  · ★★ **评审 §7-1 订正**：v1.0 写「AA 关时不做额外处理」⇒ 实际效果是**该开关对圆弧边缘质量没有任何作用**，调用者会以为全局 AA 开关对所有图元一致生效。**现行取边 = 二值化**，与 GL 图集满降级（PD33-D）**同一机制** ⇒ 开关**真有语义**（AA 开 = 平滑边缘 / AA 关 = 硬边），且两端同一套代码。
  · ⇒ **契约（如实写清）**：`m_antiAliasing` 对圆弧的影响 = **边缘质量**，**不影响是否绘制**；`DrawLine` 的 AA 关仍走 legacy 直画，两者**都保留了「绘制不被吞掉」**这一共同底线。

### 3.2 GL：`ArcSlot` + 图集满降级（C33-7）

- **`ArcSlot` 形态**：镜像 `LineSlot`（`GLRenderer.cpp:756-780`）——缓存键 = 几何量化元组；`Allocate` → `Upload(grid.coverage)` → `TexturedQuad(slot, originX, originY, grid.width, grid.height)`。
- **缓存键必须含哪些量**（★ 漏一个就退化成「每帧新分配 ⇒ 图集迅速填满」——`LineSlot` 的 spike 教训原文：~89 帧即满 ⇒ 堆越界崩）：
  ```
  key = (grid.width, grid.height,
         Q8(cx), Q8(cy), Q8(radius),          // ★ 长度类：1/8 px
         QA(start), QA(sweep),                // ★ 角度类：**独立量纲**（见下）
         Q8(width))
  ```
  ★★ **评审 §7-2 订正：像素量化与角度量化必须分开写**（v1.0 把两者混成「都 ×8」，会让读者以为角度也是 1/8 px）：
  · **长度类 `Q8(v) = lround(v * 8)`** ⇒ `1/8 px` 粒度；
  · **角度类 `QA(v) = lround(v * 8)`** ⇒ 算式同形，但**量纲是「1/8 度」**；★ 取同一系数 = **复用同一套「量化后作键」的既有手法**，不引入第二套量化常量（条 95① 的变体：一个量化手法只用一个系数）。
  ★ **量化后的参数是否参与栅格化？** ⇒ **不参与**。**键用量化值、栅格化用原值**——与 `LineSlot` 的既有做法**逐字一致**（它的键量化到 1/8 px，而 `GenerateLineCoverage` 收原始 float）。
  ⇒ ★ **几何偏差上界如实写明**：同一槽位内两次调用的几何可差 **≤ 1/16 px 位置 / ≤ 1/16° 角度**（量化步长之半）；覆盖度已按 1/8 粒度归并 ⇒ **属既有的、`LineSlot` 已接受的精度口径**，非本 Phase 新引入的误差项。
- ★★ **图集满降级（决策 PD33-D）：取「硬边弧」而非「弦多段逼近」。**
  - **候选①「弦多段 `SolidSegment`」被否**：`SolidSegment` 是**实心四边形**（`GLRenderer.cpp:618-644`），用它逼近弧需要**自适应段数**（段数少 ⇒ 明显多边形；段数多 ⇒ 每段一次 `glBegin/End`，与「命令数不爆炸」的 Phase 27/28 目标冲突），且**段的接缝处会重复覆盖**（半透明色叠加变深）。
  - **候选②「硬边弧」= 取 ①**：复用**同一份覆盖度网格**，把 coverage **二值化**（`>0 ⇒ 255`）后走**同一个 `ArcSlot` + `TexturedQuad`**。⇒ **几何语义与 AA 路径完全一致**（同网格、同形状），只是边缘硬；新增代码 ≈ 0（一个布尔参数）；**必无接缝问题**（同一张图）。
  - ★ 这与 GDI 侧「AA 关也走覆盖度」的 PD33-C **同构**——两端同哲学：**弧的几何只有一个来源，AA 只改边缘质量**。
- ★ **降级路径的既有先例对照**：`DrawLine` 的降级之所以能用 `SolidSegment`，是因为**线段本身就是四边形**（形状等价）；**弧不是**。⇒ C33-7 的契约（「降级必须仍是弧」）在此落地为**二值化同网格**。

### 3.3 两后端几何一致性（C33-6 的落点）★ 评审 §6：必须与生成器**实际接线**

- **决策 PD33-E：把「弧的几何核」抽成 `BackendContract.h` 里的纯函数**，且 ★★ **生成器直接调用它**（不是只让测试调）：
  ```cpp
  namespace BackendContract{
      /// 入参归一化（无效判定 + 角度归约 + 扫角钳制 + 半宽钳制）——C33-4 的唯一实现
      struct ArcParams{ float cx, cy, radius, startDeg, sweepDeg, halfWidth; bool drawable; };
      ArcParams NormalizeArc(float cx, float cy, float radius,
                             float startDeg, float sweepDeg, float width) noexcept;

      /// ★ 几何核：点 p 到**有限弧段**的**距离平方**（C33-1 的唯一实现；含 2.2.2 的 t 钳制）
      float ArcSegmentDistanceSq(const ArcParams& p, float x, float y) noexcept;
  }
  ```
  ★★ **v1.1 订正（评审 §6 的测试盲区）**：v1.0 把 `ArcContainsPoint` 定义为共享件、却在 §2.2 伪代码里另写 `angleInside(...)` ⇒ **真值表测一个生产代码不调用的函数**。
  ⇒ **现行依赖链（单一真相源，逐级向下）**：
  ```
  BackendContract::NormalizeArc        ← C33-4 钳制/守卫的唯一实现
        ↓
  GenerateArcCoverage                  ← 逐像素调 ArcSegmentDistanceSq（不自己写角度逻辑）
        ↓
  GDI / GL                             ← 只消费生成结果（覆盖度网格），不再做几何判定
  ```
  ★ **可机检判据**（详设盯点项）：`ArcCoverage.cpp` **不得出现独立的扇区判定**（几何判定只能来自 `ArcSegmentDistanceSq`）——把「测试靶子 = 生产判据」变成**结构性事实**而非纪律。
- **理由**（`BackendContract.h` 头注释的既有判据，条 104 同族）：**共享代码强于共享断言**——两条断言各写一遍仍可能漂移；**同一份代码不可能漂移**。★ 且该件正是 #62「§7.2 后端契约线」为此而建的落点。
- ★ **可测性**：这两个函数是**纯函数** ⇒ 真值表用例可无头跑（不需要 GL context），与 `ImageIsDrawable` / `QuantizeEdge` 的验收形态一致。★ 且因**生成器直接调用**，真值表覆盖的**就是生产路径**（评审 §6 要的闭环）。

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
| T33-2 | **面积守恒·解析参照**（C33-5）★★ **评审 §3 订正：v1.0 写 `sweep·r·w` 而 `sweep` 是「度」——漏度→弧度换算，整圆会算成 `360·r·hw·2` 而非 `2π·r·hw`——★ **实测比值 `360/(2π) ≈ 57.3×`**（不是「差一点」）**。**现行参照式**（`θ_deg` = `\|sweep\|`、`θ_rad = θ_deg·π/180`、`hw` 见 C33-4 钳制）：<br>· **弧段**：`A_arc = θ_rad · r · hw · 2`（= 环带面积 `θ_rad·(r+hw)²/2 − θ_rad·(r−hw)²/2` 化简）<br>· **两端圆帽**（各半圆盘，合一个整圆）：`A_cap = π·hw²`<br>· ★ **`\|sweep\| = 360°` 时弧段已闭合 ⇒ 圆帽不再追加**：`A = θ_rad·r·hw·2`（整圆 = `2π·r·hw·2/2`… 即 `2π·r·hw`；见下方手算核对）<br>· **扇形退化**（`radius <= hw`，C33-4）：`A = θ_rad·R_eff²/2`，其中 `R_eff = radius + hw`（外半径；内半径已钳到 0）<br>★ **手算核对表**（写进用例，防公式再错）：`\|sweep\|=90/180/270` 三分档用「弧段 + 圆帽」；`360` 档用「仅弧段」；四档**都必须与公式手算值相合**，再谈容差<br>判据：`\|Σcov/255 − A_analytic\| / A_analytic ≤ 实测冻结容差`（★ **容差实测后冻结，不由公式推导**——沿 Phase 24 的 `relRef ≤ 0.02 / relAna ≤ 0.03` 形态） | ①合法性 |
| T33-3 | **镜像对称**：`start=0,sweep=90` 与其关于 x 轴的镜像（`start=270,sweep=-90` 等效）逐像素对应 | ②对称 |
| T33-4 | **跨 0°**：`start=350,sweep=20` 与 `start=10,sweep=20` 的**重叠部分**一致；且**不出现空弧** | ③端点接缝 |
| T33-5 | **整圆接缝**：`sweep=360` 在 `0°` 邻域**无零覆盖率列**（C33-3②的漏像素防线）；与 `sweep=-360` **逐位相同** | ③端点接缝 |
| T33-6 | **圆帽端点**（★ v1.1 与判据对齐）：`sweep=90` 时，**沿端点半径向外 `hw` 处**（即端点圆帽的球邻域内、但在「径向带 ∩ 扇区」之外的点）**覆盖率 > 0**——★ 该点**只可能被圆帽覆盖**，故它是「圆帽 vs 平头」的**判别性采样点**（平头在此恒 0）；★ 反向对照：同点沿**切向**外移 `hw + 1`（圆帽外）⇒ 覆盖率 0 | ③端点接缝 |
| T33-7 | **退化全表**（C33-4）：`radius<=0` / `width<=0` / `sweep==0` / NaN / ±Inf / `\|sweep\|>360`（钳制后**逐位等于** 360 档）——以上全部 `Empty()` 或等价；★ **扇形退化单独一档**（见下） | ④退化 |
| T33-7b | ★★ **扇形退化**（`radius <= width/2`，评审 §5 订正）：**v1.0 断言「中心像素覆盖度 = 255」是错的**——`\|sweep\| < 360°` 时中心像素同时含扇区内外的采样点，覆盖率**必然 < 255**。**现行断言两条**：① **几何归属**：`ArcSegmentDistanceSq(圆心)` ≤ `hw²`（`radius <= hw` ⇒ 必成立）⇒ **圆心必在弧带内**，且**全程不调 `atan2`**（避开 `atan2(0,0)` 未定义）；② **面积对照**：`A = θ_rad·(radius+hw)²/2`（扇形）——用**解析面积**而非「中心像素二值」验证退化，覆盖 `\|sweep\| = 90/360` 两档（后者 = 整圆盘） | ④退化 |
| T33-8 | **确定性**：同输入两次逐位相同（沿 `LineCoverage.Determinism`） | ④退化 |
| T33-9 | **`ArcSegmentDistanceSq` 真值表**（`BackendContract`，★ **v1.1 更名**——返回**距离平方**而非 bool，与 `DistSqToSegment` 同口径）：含**负 sweep 的起点归属**（`t = 0`，评审 §2 的回归锚）· 跨 0° · **整圆短路** · **圆心归属**（`radius ≤ hw`）· 端点圆帽邻域——**纯函数无头可跑**，且**生成器直接调用它**（§3.3 的依赖链 ⇒ 测的就是生产判据） | ①合法性 |
| T33-9b | ★★ **面积参照的手算核对表**（写进用例，防公式再错）——`r=100, w=10`（`hw=5`）实测已验证：<br>`\|sweep\|=90` ⇒ `A = θ_rad·r·hw·2 + π·hw²` = `1570.796 + 78.540` = **`1649.336`**<br>`\|sweep\|=180` ⇒ `3141.593 + 78.540` = **`3220.132`**<br>`\|sweep\|=270` ⇒ `4712.389 + 78.540` = **`4790.929`**<br>`\|sweep\|=360` ⇒ **`6283.185`**（★ 圆帽**不**追加；且**精确等于** `2π·r·hw` = `2π·100·5`）<br>★ 旁证：弧段项与「两个整扇形相减」`θ_rad·((r+hw)²−(r−hw)²)/2` **逐位相同**（两种独立推导互证） | ①合法性 |
| ★ **反例锚** | ★★ **v1.1 的错式**（无度→弧度换算）`\|sweep\|·r·hw·2` 在 `\|sweep\|=360` 下给 **`360000`**，而正确值 `6283.185` ⇒ **比值 `360/(2π) ≈ 57.3×`**。★ 该反例**写进用例注释**：若有人把换算删掉，断言会以 **57 倍**的差距立刻失败（不是"差一点"的容差问题） | ①合法性 |

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

## 8. 实现顺序（**四批**——★ 评审 §7-3 订正：v1.0 标题写「三批」而表列四批）

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

- **v1.1**（2026-10-14）**吸收第二轮外部评审（对 v1.0 的「有条件通过」）**。★ 按条 121 先做**已覆盖度回扫 + 事实核验**，区分「评审看到的需求 v1.0（GitHub 延迟）」与「已修的需求 v1.1」，再逐条处置。**评审五项「实现前必须解决」全部成立，全部采纳**：
  · **§1 端点几何互斥** ⇒ ★★ **本稿最重的一处**：v1.0 的 `hit = inBand && inSector` **把圆帽切成平头**（`inBand` 是「到**整圆**距离」，再乘扇区门控 ⇒ 端点被角度截断），与 T33-6「圆帽」自相矛盾。**修法 = 单一「到有限弧段距离」判据**（§2.2.2）：`t* = clamp(角度投影, 0, 1)` ⇒ 端点处退化为「到端点距离」⇒ **球邻域自然成半圆盘**。★ 与 `LineCoverage::DistSqToSegment` **逐行同构**。
  · **§2 负扫角漏起点** ⇒ ★ 确认（`rel == 0` 时 `(360−rel) ≤ −sweep` 为假 ⇒ 起点被排除，正负不对称）。**修法 = 正负共用「从起点沿扫描方向的有向角差」**（§2.3 `DirectedAngleFraction`：`sweep<0` 时用 `(start − a)` ⇒ 起点恒给 `t = 0`）。
  · **§3 面积式漏度→弧度** ⇒ ★★ 确认（v1.0 写 `sweep·r·w`，`sweep` 是度 ⇒ 整圆算成 `360` 的量级而非 `2π`，**实测比值 ≈ 57.3×**；数值核对见本稿 §7.1 手算表，`sweep=360` 档已精确等于 `2πrw`）。**修法 = T33-2 改四分档参照式 + 手算核对表**；同时**回改需求 C33-5**（根因在需求侧）。
  · **§4 预过滤 `dist` 歧义** ⇒ ★ 确认（照字面实现：「到圆心距离 > hw+√½」会让半径 > hw 的弧**全部跳过**）。**修法 = 显式双变量命名**（§2.2.1 `radialDistance` / `distanceToArcCenterline`）。
  · **§5 退化扇形缺几何定义 + T33-7 断言错** ⇒ ★★ 确认，且**评审的两条论据都成立**（`atan2(0,0)` 未定义；`|sweep| < 360°` 时中心像素**必然**含扇区内外的采样点 ⇒ 覆盖率必 < 255）。**修法 = 判据不依赖 `atan2`**（圆心归属由「距离 ≤ hw」自动决定）+ **T33-7b 改断言「几何归属 + 解析面积」**（覆盖 90°/360° 两档）。
  **评审四项「建议冻结前补齐」亦全部采纳**：
  · **§6 共享契约未接线** ⇒ ★ 确认（v1.0 定义 `ArcContainsPoint` 却在伪代码里另写 `angleInside` ⇒ **真值表测一个生产不调用的函数**）。**修法 = §3.3 明确依赖链** `NormalizeArc → GenerateArcCoverage → 两端`，并把「**生成器直接调用**」写成结构要求 + 可机检判据（`ArcCoverage.cpp` 不得有独立扇区判定）。★ 函数亦更名为 `ArcSegmentDistanceSq`（返回**距离平方**，与 `DistSqToSegment` 同口径，避免「bool 判定」掩盖几何量）。
  · **§7-1 关 AA 时行为** ⇒ ★ 确认（v1.0「不做额外处理」= 该开关对圆弧边缘质量**无作用**）。**修法 = PD33-C 取「二值化」**，与 PD33-D 同机制 ⇒ 开关**真有语义**。
  · **§7-2 缓存键量化单位** ⇒ ★ 确认（「都 ×8」把 px 与度混为一谈）。**修法 = 分开写 `Q8`(1/8 px) 与 `QA`(1/8°)**，并写明「**键用量化值、栅格化用原值**」+ 偏差上界（≤1/16 px / ≤1/16°）。
  · **§7-3 版本号与批次数** ⇒ ★ **版本号属 GitHub 延迟**（本仓库需求 v1.1 早于本稿落盘）；★ **「§8 标题写三批而表列四批」是我的真错**，已修。
  **如实反驳：无**——本轮五项 P1 与四项补齐项**全部成立**，未发现方法不适用或已覆盖的误报（与上一轮不同）。
- **v1.0**（2026-10-14）初稿。**输入**：需求稿 **v1.1**（C33-1..10 契约 + R1–R9）。**内容**：§1 需求→初设映射 · §2 生成器形态（PD33-A 复用 `LineCoverageGrid` / PD33-B bbox 不收缩 + 算法骨架与角度判定伪码）· §3 两后端（PD33-C GDI 关 AA 仍走覆盖度 / PD33-D GL 图集满取「二值化同网格」**否决弦多段** / PD33-E 几何判据抽入 `BackendContract.h` 两端同符号）· §4 公共签名与命令层（★ 角度**不折 scale** 的盯防）· §5 控件（PD33-F 轨道也走 `DrawArc` / **PD33-G `Theme` 加纯虚的破坏面仅库内 2 处**——★ 本稿新勘明，harness 的 `Theme.h` 只是颜色常量集合、不继承 `ECDI::Theme`）· §6 影响面（公共头 96→98）· §7 测试大纲 T33-1..18（四类 + 人工观感项）· §8 四批（批一零公共头改动）· §9 开放项 O33-1..5 · §10 局限 L33-1..5。**待评审。**
