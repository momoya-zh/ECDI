# Phase 17 · 布局内边距（Layout padding）—— 初步设计

> 状态：**v1.0 待评审**（2026-09-20）
> 输入：需求确认 v1.0（`docs/phase17-layout-padding-requirements.md`）
> 评审前置：外部评审结论「**需求本身成立，技术路线基本合理，可以进入初步设计；Q2 是当前唯一真正需要重点解决的语义问题**」——无阻塞项

---

## 1. 设计输入与基线

### 1.1 需求阶段已定（本稿不再讨论）

| 项 | 已定内容 | 依据 |
|---|---|---|
| **配置方式** | **构造参数**，不提供 setter | 需求 §1.3——Phase 9.7 的 **F3 纪律**（「布局配置非运行时状态，不设 setter」） |
| **形态** | **单值 `int`**（四边等宽） | 需求 D1 |
| **形参位置** | **追加为第 3 个参数**（不重排） | 需求 D3——现有调用全部 ≤2 参数 ⇒ 零破坏 |
| **落点** | `VerticalLayout` / `HorizontalLayout` **各自持有**，`Layout` 基类不动 | 需求 D2（倾向）→ 本稿 §3.1 定案 |
| **不做** | margin · 四边独立 · 完整 Box Model · `Window` 级 API · 控件级 padding | 需求 D4 / D5 / N1–N7 |

### 1.2 代码基线勘察（B1–B6，2026-09-20 复核）

| # | 事实 | 证据 |
|---|---|---|
| **B1** | `Widget::SetSize(int, int)` **无任何钳制**——直接写入 `m_geometry` | `Widget.cpp:210-215`（`m_geometry.width = static_cast<float>(w);`） |
| **B2** | `Widget::SetPosition(int, int)` **无任何钳制** | `Widget.h:83`（inline） |
| **B3** | `ArrangeInternal()`：先对自己调 `m_layout->Arrange(*this)`，**再递归**子节点；`Arrange()` 是其唯一入口 | `Widget.cpp:188-202` ⇒ **每次从头算、不读上一次结果**（幂等性的结构保证） |
| **B4** | `SetStretch` 有**断言先例**：`FRAMEWORK_ASSERT(stretch >= 0)` | `Widget.cpp:217-219` |
| **B5** | 现有 `remaining` 是**单层钳**：`max(0, parent − fixedTotal − spacing·(n−1))` | `VerticalLayout.cpp:37-38` · `HorizontalLayout.cpp:37-38` |
| **B6** | `GetWidth()` / `GetHeight()` 是 `static_cast<int>`（对 `float` 成员**截断**） | `Widget.h:139` / `:141` |

### 1.3 ★ 对评审意见的逐条处置

| # | 评审建议 | 处置 |
|---|---|---|
| 1 | 从 Layout 层解决、不改 AutoSize | ✅ **采纳**（本稿 §1.1 已锁为设计输入） |
| 2 | 复用 Phase 9.7 的 F3 纪律 | ✅ **采纳**——「不是我觉得，而是 ECDI 已有纪律」 |
| 3 | 方案 B 是最小方案 | ✅ **采纳** |
| 4 | D2 取「两个子类各自持有」 | ✅ **采纳**，本稿 §3.1 定案 |
| 5 | **Q2**：`available = max(0, P − 2p)`、`remaining = max(0, available − spacing)` | ⚠️ **采纳目标，但简化形式**——见下 §3.3：**单层 `<code>max</code>` 与两层等价**（`fixedTotal ≥ 0` 恒成立），且单层是**最小 diff**；`remaining ≥ 0` 这一诉求**完全保留** |
| 6 | padding 是**硬 inset**；空间不足时内容区退化为 0，**不反向修改 padding** | ✅ **采纳**——升格为契约 **C2** |
| 7 | R4「逐位退化」是最重要的回归契约 | ✅ **采纳**——升格为契约 **C1**，并指定 T17-1 为守门用例 |
| 8 | 既有 11 条 Layout 用例**不得修改去适应新行为** | ✅ **采纳**——列为 §7 的硬约束 |
| 9 | stretch 分配必须在扣除 `2p + spacing` 之后 | ✅ **采纳**（B5 的写法天然满足） |
| 10 | 嵌套为**累加**语义，非继承/覆盖 | ✅ **采纳**——契约 **C3** |
| 11 | D5 不做 `Window::SetContentPadding` | ✅ **采纳** |
| 12 | Q5 不应成为阻塞项 | ✅ **采纳** |
| 13 | 不得重排参数（追加而非前插） | ✅ **采纳**（D3 已定） |
| 14 | 初设冻结 Q1–Q4 | ✅ **本稿 §3.1 / §3.3 / §6 逐项冻结** |

> ⚠️ **一处算术勘误（评审第 8 节的示例）**：原文以「`parent=500 / padding=20 / spacing=10 / 3 个 stretch`」举例，给出 `20 + 150 + 10 + 150 + 10 + 150 + 20 = 510`，并据此提示「还要注意扣除顺序」。**该例的加数与结论互相矛盾**（510 ≠ 500）。正确的分配是：`available = 500 − 40 = 460` → `remaining = 460 − 10×2 = 440` → 按整数截断 + 末位吃余数得 `146 / 146 / 148` ⇒ `20 + 146 + 10 + 146 + 10 + 148 + 20 = 500` ✓ **结论（必须先扣 `2p + spacing`）是对的，示例算式需更正**——本稿 §7 的 T17-4 采用更正后的数值。

### 1.4 需求稿 §8 的 Q1–Q5 → 本稿答案索引

| 需求稿问题 | 本稿落点 | 结论 |
|---|---|---|
| **Q1** D2 落点终裁 | §3.1 | **两个子类各自持有**，`Layout` 保持纯接口 |
| **Q2** `remaining` 扣 padding 后为负 | §3.3 | **单层钳**，结构上保证 `remaining >= 0`（与需求 §3.2 R5 的不变式关系见 §3.4） |
| **Q3** 幂等性是否受影响 | §6 | **不受影响**——B3 结构保证；padding 是常量输入 |
| **Q4** `SetSize(0, h)` 语义 | §3.3 | **直接允许**——B1 无钳制，不为 padding 增设特殊语义 |
| **Q5** 是否有第二个消费者 | §5 | **保持观察**，不扩大范围（评审第 12 条） |

---

## 2. 头文件改动

### 2.1 公共头：**净增 0**（逐头确认）

| 头 | 改动 | 性质 |
|---|---|---|
| `include/ECDI/Layout/VerticalLayout.h` | 构造签名 +1 可选形参；成员 +1（`int m_padding = 0;`）；构造注释同步 | 仅既有头 |
| `include/ECDI/Layout/HorizontalLayout.h` | 同上（对称） | 仅既有头 |
| `include/ECDI/Layout/Layout.h` | **零改动** | D2 定案——保持纯接口 |
| 新增头 | **0** | — |

**Public 头 92 → 92**（需求 §7 的预期得到确认）。

### 2.2 零改动区（显式声明）

`Layout.h` · `Widget.h` / `.cpp` · `Window.h` / `.cpp` · `Panel` / `CollapsiblePanel` / `ScrollView` 等所有容器控件（它们只是「有 Layout 的 Widget」，padding 语义全部由布局实现）· 全部 41 处既有调用点（需求 §7）。

---

## 3. 实现分解

### 3.1 构造与成员（两种布局对称）

```cpp
// VerticalLayout.h / HorizontalLayout.h
/// @param spacing       主轴相邻子间隙 px（默认 0 = 现状；>= 0 debug assert）
/// @param fillCrossAxis 跨轴填充开关（默认 false = 现状）
/// @param padding       四边内边距 px（默认 0 = 现状；>= 0 debug assert）
explicit VerticalLayout(int spacing = 0, bool fillCrossAxis = false, int padding = 0);

private:
    int m_spacing = 0;
    bool m_fillCrossAxis = false;
    int m_padding = 0;
```

```cpp
VerticalLayout::VerticalLayout(int spacing, bool fillCrossAxis, int padding)
    : m_spacing(spacing), m_fillCrossAxis(fillCrossAxis), m_padding(padding)
{
    FRAMEWORK_ASSERT(spacing >= 0);
    FRAMEWORK_ASSERT(padding >= 0);   // 与 B4 的 SetStretch 同型
}
```

**D2 定案理由**：`Layout` 目前是**无状态纯接口**（`Layout.h` 全文 17 行）；为一个 `int` 把它变成带状态基类，等于**为 DRY 提前改变抽象层级**，而消费者只有 2 个。**第三次出现时再上提**（与需求 D2 的倾向一致）。

### 3.2 `Arrange` 的四处改动（最小 diff）

以 `VerticalLayout::Arrange` 为例（`HorizontalLayout` 为 x↔y / width↔height 的同构镜像）：

```cpp
// ① 跨轴尺寸提前算一次（循环外）——新增
const int cross = (std::max)(0, parent.GetWidth() - 2 * m_padding);

// ② remaining：把 2·padding 并入被减项——单层钳
- const int remaining = (std::max)(0, parent.GetHeight() - fixedTotal
-                                         - m_spacing * static_cast<int>(count - 1));
+ const int remaining = (std::max)(0, parent.GetHeight() - 2 * m_padding - fixedTotal
+                                         - m_spacing * static_cast<int>(count - 1));

// ③ 主轴起点：0 → padding
- int y = 0;
+ int y = m_padding;

// ④ 跨轴尺寸（两处）与跨轴坐标（一处）
- child->SetSize(m_fillCrossAxis ? parent.GetWidth() : child->GetWidth(), height);
+ child->SetSize(m_fillCrossAxis ? cross : child->GetWidth(), height);
...
- child->SetSize(parent.GetWidth(), child->GetHeight());
+ child->SetSize(cross, child->GetHeight());
...
- child->SetPosition(0, y);
+ child->SetPosition(m_padding, y);   // 跨轴坐标 = padding（契约 4 的推广）
```

**改动点数**：`VerticalLayout.cpp` **4 处**（其中跨轴尺寸涉及 2 行，合计 5 行）；`HorizontalLayout.cpp` 对称。

### 3.3 取值口径的三个定案（Q2 / Q4 + 硬 inset）

| # | 定案 | 理由 |
|---|---|---|
| **①** | `remaining = max(0, P_主轴 − 2p − fixedTotal − spacing·(n−1))` —— **单层钳**，不拆成 `available` / `remaining` 两级 | **与两层写法等价**：因 `fixedTotal >= 0`（尺寸非负）且 `spacing >= 0`，当 `P − 2p < 0` 时两层写法同样得到 `max(0, 0 − fixedTotal − sp) = 0`。**单层是 B5 的最小 diff**，且诉求（`remaining >= 0`）完全保留 |
| **②** | **跨轴尺寸** `cross = max(0, P_跨轴 − 2p)` | B1 虽允许负值，但负尺寸会让 `HitTest` / 绘制进入无意义区间；**钳 0** 是这一项的正确落点 |
| **③** | **主轴起点 / 跨轴坐标不钳**（`= padding`，即使 `padding > P`） | **硬 inset 语义**（评审第 6 条）：空间不足时内容区退化为 `0`，**坐标仍落在 padding 处**——不「反向修改 padding」，也不做「中心化」等另一套语义 |

**关于 Q4**：`Widget::SetSize` **本身无钳制**（B1）⇒ 本相位**不为 padding 引入任何额外约束**：布局传给 `SetSize` 的尺寸已是钳过的 `max(0, …)`，至于 `SetSize(0, h)` 之后的行为，**沿用既有语义**，不新增特例。

### 3.4 与既有不变式的关系（评审第 9 条的精确化）

原不变式（Phase 9.7）：`Σ 主轴尺寸 + spacing·(n−1) == parent主轴`（末位吃余数保证 Σ == remaining）

加 padding 后：`Σ 主轴尺寸 + spacing·(n−1) + 2p == parent主轴` —— **当且仅当 `remaining` 未触发钳制时成立**。

> ⚠️ **钳制触发时（`2p + fixedTotal + spacing·(n−1) > parent`），等式不再成立**，正确的不变式退化为**非负可用空间模型**：`Σ 主轴尺寸 == remaining`（其中 `remaining = 0`）。**这是契约边界的刻意选择**，而非缺陷——T17-4 只在非钳制区间断言等式，钳制区间另立 T17-5 断 `remaining == 0` 且不崩。

---

## 4. 契约（C1–C6）

| # | 契约 | 验证方式 |
|---|---|---|
| **C1** | **默认 0 = 逐位退化**：`padding = 0` 时 `Arrange` 的行为与改动前**逐位相同** | T17-1（**守门用例**）+ 既有 11 条 Layout 用例**不改动**仍全绿 |
| **C2** | **padding 是硬 inset**：空间不足时内容区退化为 0，**绝不反向修改 padding**，也不引入中心化 / 自动缩减 | T17-5 |
| **C3** | **嵌套 = 累加**（每级各自生效），**不**做继承 / 覆盖 / 级联 | T17-6 |
| **C4** | **与 `spacing` / `stretch` / `fillCrossAxis` 正交**：padding 先扣减，其余分配逻辑逐字不变 | T17-3 / T17-4 + 既有用例 |
| **C5** | **幂等**：每次 `Arrange` 从 parent 几何 + 子当前尺寸 + spacing + padding 重算，**不读取上一次结果** | B3 结构保证；T17-7（重复 Arrange 结果相同） |
| **C6** | **负值钳 0 + debug 断言**（`FRAMEWORK_ASSERT(padding >= 0)`） | T17-8 |

**表形制**：沿用 Phase 16 契约表（C1–C12）的形制——**每条契约必须绑定验证方式**，不允许出现「有契约、无验证手段」的条目。

---

## 5. 影响面

| 项 | 实测 | 影响 |
|---|---|---|
| 改动文件 | **4**（`VerticalLayout.h/.cpp` + `HorizontalLayout.h/.cpp`） | 均为既有文件 |
| 公共头 | 92 → **92** | 净增 0 |
| 新增头 | **0** | — |
| 既有调用点 | 生产示例 **20** + 测试 **20** + README 示例 **1** | **零改动**（追加可选参数） |
| `Layout.h` / `Widget` / `Window` | **零改动** | §2.2 |
| 测试用例 | 218 → **218 + N**（N 归详设） | 新增 T17-1..T17-8 量级 |
| 断言特征串 | **待定**（见 O1） | 若沿用 `FRAMEWORK_ASSERT(padding >= 0)`，特征串 **10 → 11**；若改用测试断言则 **10 → 10**。**倾向沿用**（与 `SetStretch` / `spacing` 同型，且断言本身是契约 C6 的一部分） |
| `examples/ModelProbe/main.cpp` | `:262`（root） | ★ **须用户单独授权**（skill 条 2）——验收用；实现主体不依赖它 |
| `examples/ModelProbe/ModelProbe.cpp` | `:155`（page） | 可选（若不在 root 配，可在此处配；**但 R3 的「全局留白」只有 root 级能做到**） |

---

## 6. 开放决策点（O 系列）

| # | 决策 | 倾向 | 说明 |
|---|---|---|---|
| **O1** | padding 的断言形态：`FRAMEWORK_ASSERT` / 仅测试断言 | ⚠️ **沿用 `FRAMEWORK_ASSERT`** | 与 `spacing`（`VerticalLayout.cpp:13`）和 `SetStretch`（`Widget.cpp:219`）同型；代价是**断言特征串 10 → 11**（A2 类核验需同步）。备选：只靠 T17-8 覆盖 ⇒ 特征串不变 |
| **O2** | 是否提供 `GetPadding()` 访问器 | ❌ **不做** | 测试可直接观察**几何结果**（子坐标/尺寸），无需读回配置值；加 getter 会为「可测性」而无需求地扩 API（YAGNI） |
| **O3** | ModelProbe 的 padding 取值 | 建议 **12 ~ 16 px** | 验收时目视定夺；不改 `main.cpp` 前不落地 |
| **O4** | Root 的 padding 与 `CaptionBar`（Phase 13）的高度是否要联动 | ❌ **不联动** | 两者彼此独立（Phase 13 的 D7 已定「不联动」的同型先例） |

**O1 是唯一需要在详设前收敛的一项**（它影响 A2 的断言特征串判据）。

---

## 7. 测试方向（T17-1..T17-8 的落地口径）

**硬约束**：**既有 11 条 `Layout.*` 用例一个字都不改**（评审第 8 条）——它们的全绿本身就是 C1 的证据。

| # | 用例 | 判据（精确口径） |
|---|---|---|
| **T17-1** | `Layout.PaddingDefaultZero` | `padding = 0` 下重跑一组**既有场景**（如 `SpacingPositions` + `CrossFill`）的几何结果，与预期值**逐位一致**（守门） |
| **T17-2** | `Layout.PaddingSingleChild` | 单子 + `p` ⇒ 子位于 `(p, p)`、跨轴尺寸 `= max(0, 父跨轴 − 2p)` |
| **T17-3** | `Layout.PaddingCrossAxisWidth` | `fillCrossAxis = true` + `p` ⇒ 每子跨轴 `= 父跨轴 − 2p` |
| **T17-4** | `Layout.PaddingWithStretch` | 非钳制区间断言等式：**用评审示例的正确数值**——`父=500 / p=20 / spacing=10 / 3 个 stretch` ⇒ `available = 460`、`remaining = 440`、分配 `146 / 146 / 148`，验证 `20 + 146 + 10 + 146 + 10 + 148 + 20 == 500` ✓ |
| **T17-5** | `Layout.PaddingOverflow` | `2p + spacing·(n−1) + fixedTotal > 父主轴`（如 `父=20 / p=15`）⇒ **不崩**、`Σ == remaining == 0`、坐标仍为 `padding`（C2） |
| **T17-6** | `Layout.PaddingNested` | 外层 `p=10` + 内层 `p=20` ⇒ 内层子相对外层的坐标 `= 30`（**累加**，非 max / 非继承） |
| **T17-7** | `Layout.PaddingIdempotent` | 连续两次 `Arrange()` ⇒ 几何完全一致（C5） |
| **T17-8** | `Layout.PaddingNegativeClamped` | 负值构造 ⇒ 钳 0（C6）；`HorizontalLayout` 的对称面另测 |

**用例数**：218 → **218 + 8**（暂定；详设可拆并，N 以详设为准）。

---

## 8. 修订记录

- **v1.0**（2026-09-20）**初步设计初稿**。输入 = 需求确认 v1.0（外部评审「可进初步设计」）。① **§1.2 新增代码基线 B1–B6**（全部带行号）——其中 **B1（`SetSize` 无钳制）** 与 **B3（`ArrangeInternal` 递归重算）** 分别给 Q4 与 Q3 提供了**实证答案**，无需再凭推理；② **§1.3 对评审 14 条逐条处置**（全采纳；其中第 5 条**采纳目标但简化形式**——论证单层 `max` 与两层等价，且是最小 diff），并**指出评审示例的一处算术勘误**（示例加数 510 ≠ 500，正确分配为 `146/146/148`）；③ **§1.4 建立 Q1–Q5 → 本稿落点的答案索引**（五问全部收敛）；④ **§3.3 定案取值口径三项**（单层钳 / 跨轴钳 0 / 主轴起点不钳——硬 inset）；⑤ **§3.4 精确化不变式**：等式仅在非钳制区间成立，钳制区间退化为「非负可用空间模型」（这是契约边界的选择而非缺陷）；⑥ **契约 C1–C6**（C1「逐位退化」与 C2「硬 inset」为守护性契约）；⑦ **§7 测试口径**含**更正后的 T17-4 数值**；⑧ **开放点 O1–O4**，其中 **O1（断言形态）是唯一需在详设前收敛的一项**（影响断言特征串 10→11 与否）。**待评审。**
