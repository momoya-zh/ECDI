# Phase 15 滚动容器（ScrollView + 滚动条）需求确认

> 状态：v1.1（2026-09-18）｜需求确认——🚧 待拍板（D1–D12 全部给倾向；**v1.0 外部评审「方向通过，可进初设」**，三项硬要求已核实并回写）
> 前序：Phase 9.5 R1 ✅（Clip pipeline——`PushClip/PopClip` + `TextBox` 内部"偏移+裁切+换算"三件套）/ Phase 9.7 ✅（自适应布局——`SetStretch` + `Arrange`）/ Phase 9.8 ✅（`GetPreferredSize`）/ Phase 13 ✅（`CaptionBar`）/ Phase 14 ✅（托盘与拖入）
> 相关：phase9.5-r1-clip-detailed-design.md（**L102 记账「不做水平滚动条（v1.0 记账）」——本阶段兑现**）/ phase9.6-panel-container-semantics-detailed-design.md（Panel 输入透传定案）/ roadmap-deferred.md（延期项总表）/ MEMORY.md（分层不变量）

---

## 1. 动机与现状勘察

### 1.1 动机

框架目前**没有任何可复用的滚动容器**。所有"内容比视口大"的场景都在各自手搓：

- **已有实例（唯一）**：`examples/ModelProbe/ModelProbe.cpp:50-85` 的 `ModelListPanel`——35 行 `Panel` 子类，靠 `m_offset` 手动 `SetPosition` 逐行移位 + 滚轮 + `clamp` + `Invalidate`，**无滚动条**（行位置 = `i * rowHeight - offset`）。
- **相邻形态（不同源）**：`TextBox` 内部已实现"偏移 + 裁切 + 坐标换算"（`m_scrollOffsetY` / `PushClip` / `EnsureCaretVisible`），但它的滚动是 **caret 驱动**（保证光标可见），与"内容范围（extent）驱动"是**两条不同的驱动源**——不能直接复用其状态，只能复用其**做法**。

本阶段要把这个做法**从控件私有提升为 Widget 层的接缝**（第二次用例出现再抽象——Phase 9.5 R1 的 `TextBox` 是第一次，`ModelListPanel` 是第二次）。

### 1.2 现状勘察（代码事实，全部带行号）

| # | 事实 | 出处 | 对本阶段的意义 |
|---|---|---|---|
| **K1** | `Widget::Paint(ctx, offsetX, offsetY)` **已有偏移累加线程**：`x = offsetX + static_cast<int>(m_geometry.x)` 逐层下传 | `Widget.cpp:230-253` | 偏移通道**天然存在**，容器无需新渲染机制 |
| **K2** | `Paint` 中**每个 Widget 自推自身边界入栈**（`ctx.PushClip(Rect{x,y,w,h})`），子控件超出父边界即被**自动裁切**（嵌套交集） | `Widget.cpp:238-241` | **滚动切割零新渲染需求**——视口裁切已由 9.5 R1 能力层提供 |
| **K3** | `Widget::HitTest(x,y)` **不感知任何内容偏移**：子局部坐标 = `x - child->m_geometry.x` | `Widget.cpp:126-134` | 偏移若不进 HitTest，**滚动后点不中**（命中区域与视觉位置错位） |
| **K4** | **★ `HitTest` 也不把子节点约束在父边界内**：**先递归全部子节点、最后才判自身 `ContainsPoint`** | `Widget.cpp:121-141` | 滚出可视区的子控件**依然可被命中**（可点击 / 可 hover / 可收拖入）——对滚动容器是**系统性问题** |
| **K5** | 上述 HitTest 有 **5 个消费点**：hover / 鼠标按下 / 鼠标移动 / **滚轮** / **拖入** | `Application.cpp:220 / 251 / 287 / 308 / 387` | K4 的缺陷面波及 5 条输入路径，不是单点问题 |
| **K6** | `Panel::ContainsPoint` **恒 false**（纯容器不透传输入）；且 `Application::OnMouseWheel` 在 `target == nullptr` 时直接 return | `Panel.h:45` / `Application.cpp:310-312` | **不能拿 `ContainsPoint` 当"边界门"**（会废掉 Panel 子命中）；ScrollView 语义与 Panel **相反**——必须自身可命中，否则**空白区滚轮无效** |
| **K7** | `TextBox` 已是"偏移 + 裁切 + 坐标换算"三件套（`PushClip` 文本区 / 只画可见行 / 命中把偏移加回） | `TextBox.cpp:1125 / 1128-1138 / 565-569` | 容器版是这个做法的**推广**（见 §2 范围内） |
| **K8** | `VerticalLayout::Arrange` **每次都会重写子位置**（`child->SetPosition(0, y)`），且 `y` 从 0 起算 | `VerticalLayout.cpp:59` | 「滚动 = 移动子控件」方案与布局系统**天然冲突**（`Arrange` 会抹掉偏移） |
| **K9** | 全库**无任何 ScrollBar / Thumb 代码**；Phase 9.5 R1 明确记账「不做水平滚动条（v1.0 记账）」 | `docs/phase9.5-r1-clip-detailed-design.md:102` | 本阶段**兑现**该记账，不是推翻它 |
| **K10** | `Widget::GetAbsolutePosition()` **不感知偏移**（纯父链 `GetX/GetY` 累加）；现有消费者 **5 处**（`Button.cpp:116` · `CaptionButton.cpp:56` · `TextBox.cpp:493 / 896 / 928`）**全部需要视觉坐标** | `Widget.cpp:339-359` | 详见 **D3**——**必须认偏移**（其中 3 处是"事件坐标 − abs"换算链，不认则 TextBox 入 ScrollView 后错位） |
| **K11** | `ModelListPanel` 手搓实例：`m_offset` / `m_rowHeight` / `ApplyLayout()`（移子控件）/ 滚轮 / `clamp` / `Invalidate`，**35 行、无滚动条** | `ModelProbe.cpp:50-85`，使用点 `:371 / :798` | **本阶段的首个真实消费者**（"能无损收回 35 行"是 R1 的最低验收线） |
| **K12** | `MouseWheelEvent::GetDelta()` 返回**原始值**（`WHEEL_DELTA` 的倍数，>0 = 远离用户），**不归一化** | `MouseWheelEvent.h:41-44` | 步长换算（含高精度滚轮）**必须由消费者**负责——框架层不得假设"一次滚一格" |
| **K13** | `TextBox::OnMouseWheel` 是滚轮语义的**既有先例**：`offset -= delta/120.0f * kScrollLinePx` 再 `clamp` 再 `Invalidate` | `TextBox.cpp:447-455` | ScrollView 应**与之一致**（含 `120` 为换算基准、`clamp` 到 `[0, max]`） |
| **K14** | `Layout::Arrange(Widget& parent)` 是纯虚、**无内容尺寸概念**；`Layout` 不知道内容有多长 | `Layout.h:14` / `VerticalLayout.cpp:16-63` | 滚动范围（`extent`）**不能问 Layout 要**——必须由 ScrollView 自身从子控件推导 |
| **K15** | **`WM_MOUSEHWHEEL` 全库零处理**——平台层只翻译 `WM_MOUSEWHEEL` | `WindowMessageHandler.cpp:162`（grep `MOUSEHWHEEL` = 0 处） | **横向滚轮在框架里根本不存在** ⇒ D8 的横向入口被锁死 |
| **K16** | `MouseEvent` **无修饰键信息**（成员仅 `m_mouseX` / `m_mouseY`） | `MouseEvent.h` | **「Shift + 滚轮」不可实现**（除非扩 Event）⇒ 印证 K15 / D8 |
| **K17** | `WM_MOUSEWHEEL` 翻译用 `GET_WHEEL_DELTA_WPARAM`（**有符号**）+ `ScreenToClient` | `WindowMessageHandler.cpp:162-178` | `delta` 符号语义（>0 远离用户）与 K12/K13 一致，ScrollView 可直接对齐 |
| **K18** | `Widget::OnMouseWheel` 的**现有实现者 5 处**：`Widget` 基类虚（`Widget.h:144`）· `TextBox`（`TextBox.h:179`）· `ModelListPanel`（`ModelProbe.cpp:64`）· `EventRouter`（`EventRouter.h:82` + `.cpp:88` 转发）· `Application`（`Application.h:112`） | 全库 grep | **改签名有真实成本** ⇒ D7 降级的依据（skill 条 33） |

### 1.3 结构判断（本阶段的核心问题）

现有 `HitTest` 的语义是「**几何包含**」——它回答"这个点的位置落在谁的矩形里"。滚动容器要求的是「**几何包含 ∧ 在视口内**」，即多一个"可见性约束"。而 K4 表明，**现在连纯几何包含都超出了父边界**（父不裁剪子的命中）。因此本阶段的接缝设计必须同时解决：

1. **偏移要进命中**（K3）——否则滚完点不中；
2. **命中要受视口约束**（K4）——否则滚出去的东西还能点。

这两点合起来说明：**滚动不是"多画一块"的问题，而是"坐标语义 + 可见性约束"的问题**。这也是本阶段不能只加一个 `SetContentOffset()` 就了事的原因。

### 1.4 设计不变量（v1.1 新增——外部评审「钉死」要求）

> 以下三条是**后续所有 D 项的前提**，初设与实施必须保持；任何偏离都要显式说明理由。

**（1）坐标语义三分**

| 概念 | 定义 | 受影响者 |
|---|---|---|
| **布局位置**（layout space） | `m_geometry.x/y`——**永远不随滚动变化** | `GetX/GetY/GetGeometry` · `Arrange` · `GetPreferredSize` |
| **内容偏移**（ContentOffset） | 内容坐标 → 视口坐标的**变换**，只属于容器 | 容器的 `GetContentOffsetX/Y()` |
| **视觉位置**（client space） | 布局位置 **−** 沿途累加的内容偏移 | `Paint` 的子偏移 · `HitTest` · `GetAbsolutePosition` |

**（2）自身的几何与裁剪不受自身偏移影响**：`ScrollView` 自己的 `m_geometry` 与 `PushClip` 用的是**视口矩形**，**绝不叠加自己的 offset**（否则容器会跟着内容一起移出视口）。

**（3）三处消费必须用同一个变换**（自洽的**充要条件**）：`Paint`（子偏移）/ `HitTest`（子局部坐标）/ `GetAbsolutePosition`（父链累加）三处若有一处漏用，就产生"看得见点不到""点得到看不见""Popup/IME 错位"三类不一致。

**（4）职责四层——禁止 `ScrollView` 变成万能容器**

| 层 | 职责 | 边界（明确不做） |
|---|---|---|
| `Widget` | 坐标变换 + 裁剪接缝 | 提供 offset 接缝与裁剪声明，**不知道**"滚动"这个概念 |
| `Layout` | 排布 | **不参与偏移**（D1-A 的核心理由）；不知道 extent |
| `ScrollView` | viewport + offset + extent + 滚动交互 | **不接管** `Arrange`（D1） |
| `ScrollBar` | 展示 + 输入 | **不持有**偏移权威（D11） |

---

## 2. 范围内

本阶段拆为 **R1（内核）与 R2（滚动条）两个 R，同一 Phase 内交付**（用户 2026-09-17 拍板）。

| # | 内容 | 优先级 | 备注 |
|---|---|---|---|
| **R1** | **ScrollView 内核（滚动接缝）**：`Widget` 层新增**视口偏移接缝**（默认 0）+ 在 `Paint` / `HitTest` / `GetAbsolutePosition` 三处消费；`ScrollView` 容器类（偏移状态 + 滚轮 + `clamp` + 内容范围推导） | **P0** | **本阶段的地基**；单独即可无损收回 K11 的 35 行手搓代码 |
| **R2** | **滚动条（ScrollBar + Thumb）**：可视滚动条控件（轨道 / 滑块 / 拖拽 / 点击翻页 / 轨道空白跳转）+ 与 R1 的双向绑定 | **P1** | **R1 的第一个消费者**（非投机抽象）；逼迫出显式的范围模型（内容长度 / 视口长度 / 偏移） |
| **R3** | **命中可见性约束**（K4 收口）：滚出视口的子控件不再被命中（hover / 按下 / 移动 / 滚轮 / 拖入 5 条路径一致） | **P0** | 与 R1 同批落地——否则滚动容器一用就暴露"点到看不见的东西" |
| **R4** | **ModelProbe 接入**：`ModelListPanel`（手搓）替换为框架 `ScrollView` + 滚动条，**行为零回归**（滚轮一行一行滚、行高不变、圆角/边框样式保持） | P1 | **首个真实消费者**；同时是 R1/R2 的端到端验收载体 |
| **R5** | **测试承载**：R1/R2/R3 的用例纳入既有 `ecdi_tests`（Phase 7.2 框架，`RecordingBackend` 命令断言 + 几何断言） | P0 | 遵循 7.2 既有设施，不新建测试框架 |

**范围外**（见 §4）：不做 ListBox/ListView/TreeView；不做 TextBox 的可见滚动条（其滚动是 caret 驱动，见 K13/§4）；不做平滑/惯性滚动；不做框选/键盘导航滚动（YAGNI，第二次用例出现再补）。

---

## 3. 关键决策点（含倾向）

> 全部 12 项均给出**倾向 + 理由**，待用户与外部评审逐条拍板。**D1–D4 是地基（不定则 R1 无法动笔）**，D5–D8 是行为语义，D9–D12 是范围与账户。所有 D 项均须与 **§1.4 设计不变量**一致。

### D1 接缝形态：视口偏移（A）vs 移动子控件（B）？

- **A（视口偏移）**：`Widget` 新增 `virtual int GetContentOffsetX/Y()`（默认 0），在 `Paint`（只移位子控件、自身 `PushClip` 仍用**视口**矩形，见 §1.4(2)）/ `HitTest`（子局部坐标多减一个偏移）/ `GetAbsolutePosition`（累加时多减偏移）三处消费。
- **B（移动子控件）**：容器在滚动时对每个子控件 `SetPosition(0, y - offset)`（= `ModelListPanel::ApplyLayout` 的直搬）。
- **倾向：A**。理由：① **B 与 K8 直接冲突**——`VerticalLayout::Arrange` 每次都会重写 `SetPosition`，偏移会被下一次 `Arrange` 抹掉（除非容器与 Layout 之间建立"谁后写"的隐式约定，脆弱）；② B 让**布局结果依赖滚动状态**（同一份子控件在不同 `offset` 下 `GetY()` 不同），使 `GetPreferredSize` / `Arrange` 的语义变得状态相关；③ A 是 K7（TextBox）做法的**容器化推广**，且默认返回 0 ⇒ **零回归**（所有既有控件与测试不受影响）。
- **★ 硬约束（v1.1 补）**：A 的三处消费必须**用同一个变换**——这是自洽的**充要条件**（§1.4(3)）；实施时三处缺一不可，验收须有对应用例。
- **待拍板细节**：偏移的类型用 `int` 还是 `float`？倾向前者（`Paint`/`HitTest` 全链路已是 `int`，K1/K3），**滚动位置为整数像素**（避免 float→int 截断抖动）。若未来要亚像素平滑滚动，再单独立项。

### D2 命中可见性约束：在哪一层收口（K4/R3）？

- 候选：① 在 `HitTest` 里加"子节点须落在父边界内"的**通用**判定（影响全部 Widget）；② 只在 `ScrollView` override 一个 `HitTest`（局部收口）；③ 新增虚方法 `virtual bool ClipsChildren()`（默认 false，容器 override 返回 true），`HitTest` 据此决定是否约束子树。
- **倾向：③（`ClipsChildren()` 门控）**。理由：① 方案① 会**改变所有现有容器的命中语义**（Panel 当前不裁剪子命中——虽 5 个消费点都受 K4 影响，但既有控件树普遍"子控件不越界"，改动面大且风险未评估）；② 方案② 把 `HitTest` 的递归逻辑复制一份到 `ScrollView`，与 K5 的 5 条路径不同源（滚轮/拖入仍走基类）⇒ 约束只在部分路径生效；③ 方案③ 把"是否裁剪子命中"变成**容器的公开声明**，`HitTest` 单点消费、5 条路径自动一致。
- **★ 语义定案（v1.1——回答外部评审「限制子 Widget 几何 还是 限制最终 HitTest 点」）**：③ 的正确语义 = **「命中点必须落在此容器矩形内，否则不递归其子树」**——**检查点**，既不是"限制子控件几何"，也不是"逐层传递裁剪域"。
  - **单层检查即充分**：逐层拦截**自动等价于**"所有祖先裁剪域的交集"——与 `Paint` 的嵌套 `PushClip` **同构**。示例：`ScrollView` 视口高 200，点在容器局部 `y = 250` ⇒ 点不在容器内 ⇒ 直接不递归子树 ✓（无论子树多深、几何如何嵌套）。
  - **必须沿子树传递的是「偏移」**（坐标变换，与 `Paint` 的 `offsetX/offsetY` 参数累加同构），**不是裁剪域**——两者不要混淆。
  - ⚠️ **不可用 `ContainsPoint` 做门控**：`Panel::ContainsPoint` **恒 false**（K6）⇒ 拿它当门会**拦死 Panel 自己的整棵子树**（Panel 的所有子控件全部不可命中）。须用**独立的矩形判定**（`x ∈ [0, GetWidth())` ∧ `y ∈ [0, GetHeight())`）。
- **★ `CaptionBar` 风险：已核实（v1.1）**——**不会被误伤**。`ClipsChildren()` 是 **opt-in**（默认 false），`CaptionBar` **不 override** ⇒ 零影响；v1.0 的担心前提不成立（外部评审的该项风险方向需修正）。
  - **反过来，`CaptionBar` 自身是 K4 的受害者**：`RelayoutChildren` 的 `minX = w - 3 × 46 = w − 138`（`CaptionBar.cpp:86-114`），而全库**无 `WM_GETMINMAXINFO` / `ptMinTrackSize`**（实测 0 处）⇒ **窗口宽 < 138px 时 `minButton.x < 0`**（越界）：视觉上被自身 `PushClip` 裁掉、**命中却仍生效**。触发条件极端，**本阶段不处理**（记入 `roadmap-deferred.md`）。
- **待拍板**：是否确认"同一个 `HitTest` 在不同容器下有不同约束语义"这一**有意的不一致**（D12 的必然结果）。

### D3 `GetAbsolutePosition` 是否认偏移？（K10）

- 场景：有偏移的容器内的控件，其"绝对坐标"应反映**视觉位置**（含偏移）还是**布局位置**（不含偏移）？
- **★ 结论（v1.1 已核实）：必须认偏移**——返回**视觉位置**。（v1.0 的"倾向 + 待核实"已由本次全库枚举收敛。）
- **证据：全库 5 个生产调用点，全部需要视觉坐标**

| 调用点 | 用途 | 需要 |
|---|---|---|
| `Button.cpp:116` | `OnMouseButtonUp`：`event.GetMouseX()` vs `abs` 判"Up 时鼠标是否仍在自身内"（I6 修正） | 视觉 |
| `CaptionButton.cpp:56` | 同上（CaptionBar 的 I6 同款） | 视觉 |
| `TextBox.cpp:493` | `GetCaretClientGeometry`：caret 客户区绝对坐标（喂 IME `SetCaretPos`） | 视觉 |
| `TextBox.cpp:896` | `OnMouseButtonDown`：**事件坐标 − abs = 控件局部坐标**（点击定位） | 视觉 |
| `TextBox.cpp:928` | `OnMouseMove` 拖选：同上（active 端跟随） | 视觉 |

- **为什么是"必需"而非"安全"**：事件坐标天然是**视觉坐标**（平台层从鼠标消息 lParam 转客户区，**不感知 Widget 树**）。上表后 3 处正是"客户区绝对 → 控件局部"的**换算链** ⇒ 若 `GetAbsolutePosition` 不认偏移，**TextBox 一放进 ScrollView，其点击定位 / 拖选 / caret 位置全部错位**。
- **自洽充要条件** = §1.4(3)：三处用同一个变换。
- ⚠️ **附带发现**：`Widget.h:222` 注释「TextBox 光标 / ScrollBar / Popup / Tooltip **未来用**」**已过期**——TextBox 早已在用（3 处）。初设应顺手更新该注释。

### D4 `ScrollView` 自身可命中吗？（K6）

- **必须可命中**——否则**空白区滚轮无效**（`Application::OnMouseWheel` 在 `target == nullptr` 时直接 return，K6）。这与 `Panel`（恒 false）**语义相反**。
- **倾向**：`ScrollView` 继承自 `Widget`（**不是** `Panel`），`ContainsPoint` 用基类默认矩形判定（自身可命中）；**不继承 `Panel`**——`Panel` 的"输入完全透传"是它的定案语义（`Panel.h:42-45`），复用会让两个相反语义纠缠（YAGNI：不为省一个基类而制造概念绑架）。
- **待拍板**：`ScrollView` 是否 override `ConsumesMouseInput()`（Phase 13 D9 语义）返回 `true`？——若返回 true，则落在 ScrollView 上的鼠标在 `--borderless` 自绘标题栏场景**不再阻止系统拖拽**。**倾向：不 override**（保持 false）——因为 `ScrollView` 是**容器**，其空白区（如标题栏下方的空白）仍应允许拖动窗口；真正消费鼠标的是其**内部的交互控件**（Button/TextBox），它们自己已 override。**需在初步设计用 `CaptionBar` 场景复核**。
- **★ 连带收益（v1.1 补）**：D1 修复后，Phase 13 的 `IsClientInteractiveAt`（NCHITTEST 委托判定）**自动正确**——它走 `HitTest`，坐标与鼠标事件同源。即 R3 让"caption 区拖拽判定"也免疫 K4。

### D5 滚轮步长与高精度滚轮？（K12）

- `MouseWheelEvent::GetDelta()` **原始值不归一化**（K12），且 `TextBox` 既有先例是 `delta / 120.0f * kScrollLinePx`（K13）。
- **倾向**：与 `TextBox` 语义一致——`offset -= delta / 120.0f * step`，其中 `step` 是**可配置的"一滚一行/一滚若干像素"**；`clamp` 到 `[0, maxOffset]`。**框架不假设一次滚一格**（`delta` 可能是 30/60/240 等高精度值——`/120.0f` 天然支持分数步长）。
- **★ 概念分离（v1.1 采纳评审）**：**框架默认 step ≠ 消费者的行高**。`ModelListPanel` 的 `28` 是**它自己的视觉尺寸**，**不得**成为框架默认值——否则"`Button` 默认高 32 是不是也变成 32？"（默认值会被具体 Demo 绑死）。
  - ⇒ 框架给一个**通用默认**（初设定）；**`ModelProbe` 在 R4 里显式 `SetScrollStep(28)`** 以保零回归。
- **待拍板**：`step` 的暴露形态（`SetScrollStep(int px)`？）与框架默认值——由初步设计定。

### D6 内容范围（extent）从哪来？（K14）

- `Layout` 不知道内容有多长（K14），`GetPreferredSize`（9.8）是**单控件自报**，不是"所有子控件总范围"。
- **倾向**：`ScrollView` **自身从直接子控件推导**。**不引入"内容尺寸"新 API**（YAGNI：`TextWidget`/`TextBox` 已证明"自报尺寸"够用；容器以子控件实况为准即可）。
- **★ extent 定义（v1.1 钉死）**：extent = **内容坐标系下的二维包围盒右下角**（相对内容原点 (0,0)）：
  - `contentWidth  = max(0, max over children (child.GetX() + child.GetWidth()))`
  - `contentHeight = max(0, max over children (child.GetY() + child.GetHeight()))`
  - **不是** `Σ(子控件尺寸)`——间隔会漏算：`y=0/h=20` 与 `y=30/h=20` ⇒ 真实下界 **50**，`Σ` 只得 **40**。
  - **必须二维**（否则 D8 横向一加入，本决策重开）。
  - **负向内容（左上超出原点）v1 不支持**（钳 0）——记入 `roadmap-deferred.md`：布局系统（K8）从 0 起算，负坐标不是正常布局产物。
  - 给 R2 的三元关系：`contentExtent` / `viewportExtent` / `offset`。
- **待拍板**：若子控件自身尺寸在运行期变化（如 `AutoSize` 后），`extent` 是否需要**显式失效通知**？倾向 v1 **不自动挂钩**（与 9.8 的"不挂钩"原则一致）——由调用方在内容变化后调 `ScrollView::UpdateContentExtent()`（或直接 `Invalidate` 时重算）。

### D7 嵌套滚动：事件是否被内层消费后不再上冒？

- 既有 `Application::OnMouseWheel`（`Application.cpp:306-322`）是 **target → parent 冒泡**（与鼠标事件同族），**没有"消费即停"机制**。
- **★ 倾向（v1.1 改为）：降级为记账，v1 不做**——采纳外部评审，**推翻 v1.0 的"倾向仍做"**。
- **硬依据（v1.1 实测）**：
  1. **嵌套消费者实测 = 0**：全库 grep `ScrollView` / `ScrollBar` = **0 处实现**（仅 `Widget.h:222/276` 注释提及）；真实消费者 `ModelListPanel` 是**单层**。为尚不存在的场景改动**已稳定的事件契约** = 违反 YAGNI。
  2. **改签名有真实成本**（K18 已枚举）：`OnMouseWheel` 现有实现者 **5 处**（基类虚 · `TextBox` · `ModelListPanel` · `EventRouter` 转发 · `Application`）。改返回 `bool` 触及 **EventRouter 链 + 2 个 Widget 实现者**。
  3. **三种替代机制都有代价**（记账备查，避免未来重新推演）：

| 机制 | 形态 | 代价 |
|---|---|---|
| ① 改虚函数签名 | `virtual bool OnMouseWheel(...)` | 触及 K18 全部实现者（含 EventRouter 链） |
| ② Event 带消费状态 | `event.Consume()` / `IsConsumed()` | **污染 Event 原则**（"只表示已发生的事实"，消费属控制流） |
| ③ Application 认识容器 | 派发后查询"偏移是否变化" | **Application 耦合滚动容器**（且需新增查询接口） |

- **v1 行为**：嵌套时内外层**同时滚动**（不做特殊处理）。**重启条件**：出现第二个真实嵌套消费者。
- **待拍板**：确认这一降级（外部评审建议、AI 复核同意）。

### D8 横向滚动是否同批做？

- Phase 9.5 R1 记账"不做水平滚动条"（K9），但那是**针对 `TextBox`** 的记账——**不是**针对通用容器。
- **倾向**：**同批做**。理由：① R1 的接缝（D1）本身就是 `GetContentOffsetX/Y` **成对**设计，只做垂直会留下半截对称（实施时容易只写 `Y` 分支）；② `ModelListPanel` 是垂直的，但 R2 的 ScrollBar 一写就必然面对"垂直/水平"两种朝向（`Orientation` 参数），拆开做等于把同一个控件设计两遍；③ 增量成本低（偏移接缝已对称）。
- **★ 横向入口（v1.1 钉死，基于 K15/K16 实测）**：**横向没有滚轮入口**——`WM_MOUSEHWHEEL` 在框架里**未被翻译**（K15），`MouseEvent` **无修饰键**（K16）⇒ "Shift + 滚轮"**亦不可行**。
  - ⇒ v1 横向 = **拖动滚动条 + 显式 API（`SetContentOffsetX`）二者而已**。
  - 若未来要滚轮横向，须**先扩平台翻译（`WM_MOUSEHWHEEL`）+ Event 维度（轴 / 修饰键）**——属独立议题，本阶段不做（记入 `roadmap-deferred.md`）。

### D9 R2 与 R1 的绑定关系：谁驱动谁？

- 候选：① `ScrollView` **拥有** `ScrollBar`（内部创建、自动同步）；② `ScrollBar` 是**独立控件**，用户自己 `AddChild` 并手动绑定（`SetTarget` / 回调）。
- **倾向：①（`ScrollView` 拥有，默认创建）+ 可关闭**。理由：R4 的 `ModelProbe` 场景（替换手搓容器）要求"零配置即得滚动条"，若走 ② 则每个使用点都要写绑定代码——与"收回 35 行手搓代码"的初衷相悖。**但保留 `SetScrollBarVisible(false)`**（无滚动条纯滚动仍是合法需求，且是 R1 单独的可用形态）。
- **待拍板**：`ScrollBar` 是否**同时**是公共 API（独立可用）？倾向**是**（`include/ECDI/Widget/ScrollBar.h` 公共头）——它是 R1 的消费者也是独立控件，隐藏它没有收益。

### D10 滚动条视觉：自绘 vs 系统？

- **倾向：自绘**，且**走主题**（`ScrollBarStyle` + `DefaultTheme` 默认值，与 `ButtonStyle`/`PanelStyle`/`ProgressBarStyle` 同款）。理由：① 框架已定"客户区零原生控件"（Phase 12/13 的结论）；② 自绘才能与 `--borderless` 自绘标题栏一致；③ Phase 9 主题系统已落地，新增一个 Style 是既有模式（`StyleField::Set` 标记 overridden）而非新抽象。
- **不引入新能力**：滚动条只需 `DrawRect`（轨道/滑块）+ 圆角（`DrawRoundedRect`）+ 可能的 hover 亮色——**全部是 Phase 8 已有能力**，无需新后端 API。

### D11 滚动范围的"夹紧"归属：钳在 `ScrollView` 还是 `ScrollBar`？

- **倾向：钳在 `ScrollView`（单一真相源）**，`ScrollBar` 只做**展示 + 输入**（`SetRange(contentExtent, viewportExtent, offset)` / 拖拽时回调 `SetOffset`）。理由：偏移是滚动状态的**唯一权威**，若 `ScrollBar` 也持有并自行钳制，两处状态会漂移（`ModelListPanel` 的 `clamp` 在容器里，是正确先例，K11）。
- **待拍板**：`ScrollBar` 拖拽的**通知形态**——`std::function` 回调 vs `ScrollView` 直接持有 `ScrollBar*`？（倾向"`ScrollView` 持有指针 + `ScrollBar` 在变化时直接通知容器"，避免引入回调体系的新形态；**但要注意生命周期**——`ScrollBar` 是 `ScrollView` 的子控件，父先于子析构，安全）。

### D12 是否顺手修 K4 的通用缺陷（影响面外扩）？

- K4/K5 表明"子节点越界仍可命中"是**全框架既有缺陷**（5 条路径），`ScrollView` 只是让它**变得可见**。
- **倾向：不在本阶段做通用修复**——只通过 D2 的方案③（`ClipsChildren()` 门控）**让需要裁剪的容器声明式开启**。通用行为是否也要改为"父边界约束命中"，**记账到 `roadmap-deferred.md`**，待有第二个非滚动消费者（如未来的 ListBox/裁剪面板）时再评。理由：① 通用修改会触碰全部既有控件树的命中语义，零回归风险无法在无消费者场景下充分验证（YAGNI + 风险控制）；② `ClipsChildren()` 本身**已经是通用机制**，未来要推广只需把默认值从 false 翻成 true（一次性小改）。
- **待拍板**：是否接受"同一个 `HitTest` 在不同容器下有不同约束语义"这一**有意的不一致**？（倾向接受，并在头文件注释写明原因。）
- **已发现的真实实例（v1.1）**：`CaptionBar` 在窗口宽 < 138px 时按钮越界（见 D2）——即该缺陷**已经存在**，只是无人踩到。

---

## 4. 范围外（YAGNI 明确不做）

- **ListBox / ListView / TreeView / DataGrid**——`ScrollView` 是它们的**基础设施**，不是终点；控件本身待二次需求
- **TextBox 的可见滚动条**——`TextBox` 的滚动是 **caret 驱动**（K13），与 extent 驱动不同源；Phase 9.5 R1 的"不做水平滚动条"记账**仅指 TextBox**，本阶段不推翻它（若未来要给 TextBox 加条，应让 TextBox 内部视图接入 `ScrollBar`，是本阶段**之后**的独立议题）
- **嵌套滚动停递**（v1.1 从"待拍板"移入范围外——见 D7，附重启条件）
- **横向滚轮**（v1.1 新增——K15/K16：平台无翻译、Event 无修饰键；见 D8）
- **平滑滚动 / 惯性滚动 / 弹性回弹**（Phase 9.6 动画系统可支撑，但属体验增强）
- **键盘导航滚动**（`PageUp/PageDown/Home/End` + 焦点自动滚入视口）——倾向记账到"焦点滚入"独立条目（与 D7/D8 同族但驱动源不同）
- **虚拟化 / 按需生成子控件**（性能优化，需内容模型抽象；YAGNI）
- **框选 / 多选 / 拖拽重排**（list 控件语义，非容器职责）
- **双轴同时滚动的滚动条布局细节**（右下角交叉区）——若 D8 同批做，交叉区**先简单处理**（不做"双轴都出现时缩角"的精细布局）
- **触摸板手势 / 惯性**（平台层输入能力，非本阶段）
- **负向内容**（子控件坐标 < 0 的包围盒，见 D6）
- **`CaptionBar` 窄窗越界**（v1.1 新发现的既有缺陷实例，见 D2/D12）

---

## 5. 与既有约束的对齐

| 约束（skill 条号） | 对齐方式 |
|---|---|
| **15 分层**（Widget 层零 Win32） | `ScrollView` / `ScrollBar` 纯 Widget 层实现，零 Win32 类型；滚轮走既有 `MouseWheelEvent` |
| **16 Event 原则**（轻量、事实、不归一化） | 不新增事件类型；`GetDelta()` 原始值语义保持（K12），换算在消费者（D5）；**D7 的候选② 因违反本条被否** |
| **18 唯一入口原则**（框架操作从 RootWidget 进） | 偏移是 **Widget 状态**（可直调，与 `SetSize` 同族），不新增"从 RootWidget 进"的操作 |
| **19/36 RenderCommand 死数据** | 滚动**不新增任何 RenderCommand**——复用既有 `DrawRect`/`DrawRoundedRect`/`PushClip`（K2 已证裁切零新需求） |
| **20 能力/决策层正交** | `ScrollBar` 的样式走 `ScrollBarStyle` + `DefaultTheme`（与 `Button`/`ProgressBar` 同款），不硬编码颜色 |
| **22 YAGNI** | 不做 ListBox/虚拟化/平滑滚动/通用 HitTest 修改（D12）；不引入"内容尺寸"新 API（D6）；**D7 因无消费者而降级** |
| **23 分层论证纪律** | 本稿所有论证用**契约语言**（"偏移 = 坐标语义的一部分"），不引用 GDI/Win32 行为作为依据；平台事实（K15–K17）只用于**界定能力边界**，不用于论证公共层设计 |
| **资源类禁拷贝禁移动** | `ScrollView`/`ScrollBar` 继承 `Widget`，天然禁拷贝禁移动；`ScrollBar*` 成员为**非拥有**（子控件，父先析构） |
| **31 契约双向写全** | D4/D5/D6 涉及"何时有效/何时拒绝"的，须在初设给 `@pre` + 实现 + 契约表 + 用例**四处对称** |
| **33 加纯虚先 grep 实现者** | K18 已枚举 `OnMouseWheel` 实现者（D7 依据）；**D1/D2 若确定新增虚方法（`GetContentOffsetX/Y` / `ClipsChildren`），初设必须同样 grep 全库实现者清单**（含 `src/Tests/`、`examples/` 的测试替身） |
| **9.5 R1 Clip 记账** | K9 的"不做水平滚动条"是**兑现对象**（R2 落地），不是推翻 |
| **9.7/9.8 布局与尺寸** | 偏移**不参与** `Arrange`（D1 选 A 的理由 + §1.4(4)）；`extent` 不挂钩 `GetPreferredSize`（D6） |
| **五阶段法** | 本文档 = 需求确认（只记边界与决策点，不展开接口设计） |

---

## 6. 修订记录

- v1.1（2026-09-18）外部评审处置（评审结论：「**方向通过，待拍板后进初设**」；重点 D2/D3/D7）：
  - **核实三项硬要求（全部执行，结论回写）**：
    - **D3** 全库枚举 `GetAbsolutePosition` 消费者 —— **5 处全部需要视觉坐标**（且 3 处是换算链）⇒ 从"倾向认偏移"**升级为「必须认偏移」**；
    - **D2** 核实 `CaptionBar` —— **证实 opt-in 不误伤**（评审该项的风险方向需修正为：`CaptionBar` 自身反而是 K4 受害者，窗口宽 < 138px 时按钮越界）；同时**定案语义** = "命中点须落在容器矩形内"（非几何限制、非裁剪域传递），并补"**禁用 `ContainsPoint` 门控**"的实现级理由；
    - **D7** 枚举 `OnMouseWheel` 实现者（`K18`，5 处）+ 实测嵌套消费者 = 0 ⇒ **支持评审的降级建议**。
  - **新增 §1.4 设计不变量**（评审「钉死」要求）：坐标语义三分 · 自身几何/裁剪不受自身偏移影响 · 三处同变换（充要条件）· **职责四层**（禁止 ScrollView 变万能容器）。
  - **新增勘察事实 K15–K18**：`WM_MOUSEHWHEEL` 全库零处理 · `MouseEvent` 无修饰键 · `WM_MOUSEWHEEL` 有符号翻译 · `OnMouseWheel` 实现者 5 处。
  - **D7 倾向反转**：v1.0「倾向仍做」→ v1.1「**降级为记账，v1 不做**」，附三条硬依据 + 三种替代机制代价表（并移入 §4 范围外）。
  - **D8 钉死横向入口**：按 K15/K16 —— v1 横向仅"滚动条拖动 + 显式 API"，**无滚轮入口**。
  - **D1** 补「三处同一变换」为硬约束；**D2** 补 `CaptionBar` 越界实例与"独立矩形判定"要求；**D4** 补 NCHITTEST 连带收益；**D5** 拆「框架默认 step ≠ 消费者行高」（评审：避免默认值被 Demo 绑死）；**D6** extent 钉为**二维包围盒** `max(0, max(x+w))` / `max(0, max(y+h))`（非 `Σ`），负向内容不支持并记账；**D12** 注 `CaptionBar` 为既有缺陷的真实实例。
  - **范围外新增 4 项**：嵌套滚动停递 · 横向滚轮 · 负向内容 · `CaptionBar` 窄窗越界。
  - **未采纳 0 项**；对评审的 **1 处修正**：D2 的 `CaptionBar` 风险方向（评审假设"会被误伤"，实测为 opt-in 不参与）。
- v1.0（2026-09-17）需求确认初稿：确立 R1–R5 五项范围（**R1 内核与 R2 滚动条同 Phase 交付**——用户拍板）、K1–K14 勘察事实（带行号）、D1–D12 决策点（全部含倾向 + 理由 + 待拍板细节）、非目标 8 项。核心结构判断：滚动容器的真问题不是"多画一块"，而是**坐标语义（K3）+ 可见性约束（K4）**；故接缝倾向 D1-A（视口偏移，零回归）与 D2-③（`ClipsChildren()` 门控，避开通改 `HitTest` 的零回归风险）。
