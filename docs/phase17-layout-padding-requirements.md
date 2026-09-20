# Phase 17 · 布局内边距（Layout padding）—— 需求确认

> 状态：**v1.0 待评审**（2026-09-20）
> 立项依据：`docs/roadmap-deferred.md` §7.6 条目 **#38「容器级内边距」**（2026-09-19 新立，候选 Phase 17）
> 方案：**方案 B（框架侧）**——用户 2026-09-20 拍板
> 前置解除：Phase 16 已收口 ⇒ #38 原文「⛔ 不得并入 Phase 16」的约束自动失效

---

## 1. 背景与现状勘察

### 1.1 立项由来

**用户观察**（2026-09-19 登记 · 2026-09-20 复述）：内容四边贴死客户区，**鼠标拖选极易越出 GUI 边界**。用户原话：「autosize 需要修改，不然默认从 (0,0) 开始的话，贴近 GUI 的边框难以使用」。

**⚠️ 一个必须澄清的归因（否则会改错层）**：贴边**不是 AutoSize 的行为缺陷**——AutoSize 的定义就是「窗口尺寸跟随内容」⇒ 内容从 `(0,0)` 铺满是它的**必然结果**。AutoSize 只是让这个问题**变得无余地**（窗口严丝合缝等于内容，一点留白都没有），所以从 AutoSize 入手发现它是合理的路径，但**修复落点在布局层**，不在 `GetPreferredSize()`。

**Phase 9.8 曾明确拒绝过**（需求 §4 非目标 + 初设 §3.2 冻结），原话是「**不要为 AutoSize 顺手搞一套完整 Padding/Style 系统**」——其理由是「**不要顺手搞**」，即**需求未出现**。**现在需求已出现**，拒绝的前提不再成立。

### 1.2 现状勘察（全部带行号，2026-09-20 逐条复核）

| # | 事实 | 证据 |
|---|---|---|
| **K1** | root 被设为**客户区全尺寸** | `Window.cpp:444` `m_rootWidget->SetSize(width, height)` |
| **K2** | `Arrange` 主轴起点**恒 0** | `VerticalLayout.cpp:41` `int y = 0;` · `HorizontalLayout.cpp:41` `int x = 0;` |
| **K3** | `remaining` **只减 `spacing`**，无任何 inset | `VerticalLayout.cpp:37-38` · `HorizontalLayout.cpp:37-38` |
| **K4** | `fillCrossAxis` 时子跨轴 = **父跨轴**（左右/上下贴死） | `VerticalLayout.cpp:53` · `:56`（`parent.GetWidth()`） |
| **K5** | 定位时跨轴坐标**恒 0** | `VerticalLayout.cpp:59` `SetPosition(0, y)`（注释「契约 4，现状不变」） |
| **K6** | 框架**无任何容器级 padding / margin**——`Layout.h` 只有 `Arrange(Widget&)` | `Layout.h:7-15`（全文件仅 17 行） |
| **K7** | 全库唯一的 `padding` 是 `TextBoxStyle::padding`（**控件内文字**内边距，与容器无关） | `TextBoxStyle.h:17` |
| **K8** | 两种布局**完全同构**（x↔y / width↔height），改动可对称 | 两份 `.cpp` 逐行对照（各 65 行） |
| **K9** | 构造签名 `explicit (int spacing = 0, bool fillCrossAxis = false)` | `VerticalLayout.h:17` · `HorizontalLayout.h:16` |
| **K10** | 现有调用点：**生产/示例 20 处 · 测试 20 处 · README 文档示例 1 处**，**全部只传 ≤ 2 个参数** | 全库 grep（2026-09-20） |

### 1.3 一条「会定形态」的事实

**Phase 9.7 的 F3 决策已经定了纪律**（`phase9.7-adaptive-layout-preliminary-design.md:25`）：

> 「spacing 属于 Layout，**构造参数注入**——布局配置**非运行时状态，不设 setter**（未来动态 UI 需求出现再加，不可逆性为零）」

⇒ **padding 必须沿用同一纪律**：**构造参数、不提供 setter**。这一条直接锁定 §4 的 **D0**，无需再议。

---

## 2. 技术路线（一段话定方向）

`VerticalLayout` / `HorizontalLayout` **各加一个 `int padding = 0` 构造形参**，`Arrange` 改 **4 处**：**主轴起点** `= padding` · `remaining` `− 2·padding` · **跨轴尺寸** `− 2·padding` · **跨轴坐标** `= padding`。**root 一级配置即全局留白**（`Window` 的 root 自身也是 `VerticalLayout`）⇒ 应用侧无需包裹容器，ModelProbe 只需改 2 处构造参数。

---

## 3. 需求条目

### 3.1 实现主体 · R1–R3

| # | 需求 | 说明 |
|---|---|---|
| **R1** | **容器级单值内边距** | 布局在四个方向上为子内容保留 `padding` px 的**空间**（不是描边、不是背景） |
| **R2** | **四边等宽** | 一个 `int` 同时作用于上下左右；v1 **不做**四边独立配置 |
| **R3** | **root 一级即全局留白** | `Window` 的 root 配置一次，整个客户区四周留白；这是本项的主要使用姿势 |

### 3.2 语义与边界 · R4–R6

| # | 需求 | 说明 |
|---|---|---|
| **R4** | **默认 0 = 逐位退化** | `padding = 0` 时行为与现状**逐位等价**（本次改动对既有 20+20 处调用零影响） |
| **R5** | **与 spacing / stretch / fillCrossAxis 正交** | padding 先扣减可用空间，其余分配逻辑不变；stretch 末位吃余数的 `Σ == remaining` 不变式在**扣除 padding 后**的坐标系内继续成立 |
| **R6** | **负值钳 0 + debug 断言** | 沿用 `spacing` 的既有做法（`VerticalLayout.cpp:13`）：`FRAMEWORK_ASSERT(padding >= 0)`，构造内钳 0 |

### 3.3 覆盖范围 · R7–R8

| # | 需求 | 说明 |
|---|---|---|
| **R7** | **两种布局对称实现** | `VerticalLayout` / `HorizontalLayout` 同构改动（K8），diff 形状一致 |
| **R8** | **嵌套语义 = 每级各自生效（累加）** | 外层 padding 与内层 padding 叠加，**不做**「子级继承/覆盖父级」这类样式级联——那是另一套语义，YAGNI |

### 3.4 测试与验收 · R9–R10

| # | 需求 | 说明 |
|---|---|---|
| **R9** | **自动化用例**（详见 §6） | 至少覆盖：默认 0 退化 · 单子 padding 定位 · fillCrossAxis + padding 的跨轴宽 · stretch 与 padding 共存 · 空容器 · 嵌套累加 · 负值钳 0 |
| **R10** | **验收** | ModelProbe 目视：内容四边不再贴死客户区 · 鼠标拖选不再越出边界；`ecdi_tests` 全绿（218 → +N） |

---

## 4. 决策点（D 系列——全部给倾向，待拍板）

| # | 决策 | 倾向 | 理由 / 备选 |
|---|---|---|---|
| **D0** | 配置方式：构造参数 / setter | ✅ **构造参数（已由 §1.3 锁定）** | Phase 9.7 F3 已定「布局配置非运行时状态、不设 setter」；**不可逆性为零**（将来要动态再补 setter） |
| **D1** | 单值 / 四边独立 | ✅ **单值 `int padding = 0`** | YAGNI：不对称留白需求**未出现**。「上下 > 左右」的排版偏好到时候再说，加字段不可逆性为零 |
| **D2** | 落点：`Layout` 基类 / 两个子类 | ⚠️ **两个子类各自持有**（待评估） | 基类加则未来新布局自动获得（更 DRY），但基类目前**无构造、无状态**（`Layout.h:7-15`），加成员会把「纯接口」变成「带状态基类」。⇒ 倾向**先各子类各自持有**，第三次出现时再上提 |
| **D3** | 形参位置 | ✅ **追加为第 3 个参数** | K10：现有调用全部 ≤2 参数 ⇒ 追加**零破坏**；不重排参数（重排会静默改变既有调用的语义，是危险的重构） |
| **D4** | 是否同时提供 `margin`（子级外边距） | ❌ **不做** | YAGNI：padding 已能解决用户的问题（窗口四周留白）；margin 是「子级各自的留白」，需求未出现 |
| **D5** | 「窗口四周留白」是否要顺带做成 `Window` 级 API（如 `Window::SetContentPadding`） | ❌ **不做** | root 的 `SetLayout` 已能表达（R3）；多一层 API 反而把「布局概念」泄漏到窗口层 |

---

## 5. 非目标（YAGNI 圈定）

| # | 非目标 | 理由 |
|---|---|---|
| **N1** | 四边独立配置（`padding{top,right,bottom,left}`） | D1——需求未出现 |
| **N2** | `margin`（子级外边距） | D4 |
| **N3** | 每子独立 padding | 「每布局」粒度够用（与 `fillCrossAxis` 同层级的粒度选择，Phase 9.7 已有先例） |
| **N4** | 完整 Padding/Style 系统 / CSS 式盒模型 | Phase 9.8 已明确拒绝过，本次仍拒 |
| **N5** | 为 `Label` / `Button` 补控件级 padding | 那是**控件内文字**余量（与 `TextBoxStyle::padding` 同类），**与容器级内边距是两件事**；需求未出现 |
| **N6** | 把 `LinearLayout` 抽象提出来（合并两种布局） | Phase 9.5 R2 已评估并搁置（YAGNI）——本次**不借机重提** |
| **N7** | 动态修改 padding（setter） | D0 |

---

## 6. 测试 / 验证方向

沿用 `LayoutTests.cpp`（现有 11 条 `Layout.*` 用例）的风格，新增用例建议：

| # | 用例（建议名） | 判据 |
|---|---|---|
| T17-1 | `Layout.PaddingDefaultZero` | `padding=0` 时定位/尺寸与改动前**逐位相同**（回归护栏） |
| T17-2 | `Layout.PaddingSingleChild` | 单子 + `padding=p` ⇒ 子位于 `(p, p)`、跨轴尺寸 = `父 − 2p` |
| T17-3 | `Layout.PaddingCrossAxisWidth` | `fillCrossAxis=true` + padding ⇒ 子宽 = `父宽 − 2p`（K4 的修正） |
| T17-4 | `Layout.PaddingWithStretch` | stretch 子 + padding ⇒ `Σ 主轴尺寸 + spacing + 2p == 父主轴`（R5 的不变式） |
| T17-5 | `Layout.PaddingEmptyAndSingle` | 空容器不崩；单子不越界（边界） |
| T17-6 | `Layout.PaddingNested` | 嵌套两级各配 padding ⇒ 内层子坐标 = 两级 padding 之和（R8 累加语义） |
| T17-7 | `Layout.PaddingNegativeClamped` | 负值构造 ⇒ 钳 0（R6） |
| T17-8 | `Layout.PaddingHorizontalSymmetric` | `HorizontalLayout` 的对称面（R7） |

**注**：T17-1 是**最重要的回归护栏**——它把「默认 0 时逐位退化」钉死，保证本次改动不动既有的 11 条用例行为。

---

## 7. 影响面

| 项 | 实测（2026-09-20） | 影响 |
|---|---|---|
| `VerticalLayout.h` / `.cpp` | 各 1 处 | 加形参 + 成员 + `Arrange` 改 4 处 |
| `HorizontalLayout.h` / `.cpp` | 各 1 处 | 同上（对称） |
| `Layout.h` | **不动** | D2——基类保持纯接口 |
| 公共头 | 92 → **92（净增 0）** | 只改既有头的签名，**不新增头** |
| 现有调用点 | 生产/示例 **20** + 测试 **20** + README 示例 **1** | **零改动**（追加可选参数；K10） |
| `examples/ModelProbe/main.cpp` | `:262`（root） | ★ **须单独授权**（skill 条 2——AI 不得自行修改 `main.cpp`）；若只求「ModelProbe 留白」可改为 `ModelProbe.cpp:155` 的 page 一级，**但 root 才是全局留白**（R3） |
| `examples/ModelProbe/ModelProbe.cpp` | `:155`（page） | 可选：可在此处也配 padding |
| 测试用例 | 218 → **218 + N** | 新增 T17-1..T17-8（N 待定） |
| 断言特征串 | 10 → **10** | 若 T17-7 引入新 `FRAMEWORK_ASSERT`，则特征串 +1（**待初设确认**——也可改用测试断言而非框架断言） |

**⚠️ 触角提醒**：R3「root 一级即全局留白」在本项目里**必然碰到 `main.cpp`**（ModelProbe 的 root 在那儿构造）⇒ 该处修改**须用户单独授权**。

---

## 8. 留给初步设计的问题清单

| # | 问题 | 说明 |
|---|---|---|
| **Q1** | D2 的落点最终定：两个子类各自持有 vs 上提基类 | 倾向前者（基类保持纯接口），需初设给最终结论 |
| **Q2** | `remaining` 扣 padding 后的**负数**如何处理 | `max(0, …)` 只钳了 spacing 项；padding 双倍扣减可能让 `remaining` 为负 ⇒ 需明确「子尺寸最小 0」还是「允许负」 |
| **Q3** | padding 与 `Arrange` 的**幂等性**（契约 1：每次从头算）是否受影响 | 预期不影响（padding 是常量输入），需在初设里显式确认 |
| **Q4** | 子尺寸被 padding 压到 0 时 `SetSize(0, h)` 的语义 | 与 `Widget::SetSize` 的既有行为对齐即可，需核实 |
| **Q5** | 是否有需要 padding 的**现有 demo** 之外的消费者 | 目前仅「窗口四周留白」这一个需求；DesktopNest 的框内留白可能成为第二个（待其立项时复用） |

---

## 9. 修订记录

- **v1.0**（2026-09-20）**需求确认初稿**。立项依据 #38；现状勘察 K1–K10 全部带行号复核；澄清「贴边不是 AutoSize 的缺陷」这一归因；技术路线定为方案 B；需求条目 R1–R10；决策点 D0–D5（**D0 已由 Phase 9.7 的 F3 纪律锁定 = 构造参数**）；非目标 N1–N7；测试方向 T17-1..T17-8（含 T17-1「默认 0 逐位退化」这一回归护栏）；影响面已含 `main.cpp` 须单独授权的提醒。**待评审。**
