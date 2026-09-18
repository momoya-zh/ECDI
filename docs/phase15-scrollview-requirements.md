# Phase 15 滚动容器（ScrollView + 滚动条）需求确认

> 状态：v1.0（2026-09-17）｜需求确认——🚧 待评审（D1–D12 全部给倾向待拍板）
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
| **K10** | `Widget::GetAbsolutePosition()` **不感知偏移**（纯父链 `GetX/GetY` 累加） | `Widget.cpp:339-359` | 有偏移的容器若被"绝对坐标"消费（未来 Popup/Tooltip/IME），会**错位** |
| **K11** | `ModelListPanel` 手搓实例：`m_offset` / `m_rowHeight` / `ApplyLayout()`（移子控件）/ 滚轮 / `clamp` / `Invalidate`，**35 行、无滚动条** | `ModelProbe.cpp:50-85`，使用点 `:371 / :798` | **本阶段的首个真实消费者**（"能无损收回 35 行"是 R1 的最低验收线） |
| **K12** | `MouseWheelEvent::GetDelta()` 返回**原始值**（`WHEEL_DELTA` 的倍数，>0 = 远离用户），**不归一化** | `MouseWheelEvent.h:41-44` | 步长换算（含高精度滚轮）**必须由消费者**负责——框架层不得假设"一次滚一格" |
| **K13** | `TextBox::OnMouseWheel` 是滚轮语义的**既有先例**：`offset -= delta/120.0f * kScrollLinePx` 再 `clamp` 再 `Invalidate` | `TextBox.cpp:447-455` | ScrollView 应**与之一致**（含 `120` 为换算基准、`clamp` 到 `[0, max]`） |
| **K14** | `Layout::Arrange(Widget& parent)` 是纯虚、**无内容尺寸概念**；`Layout` 不知道内容有多长 | `Layout.h:14` / `VerticalLayout.cpp:16-63` | 滚动范围（`extent`）**不能问 Layout 要**——必须由 ScrollView 自身从子控件推导 |

### 1.3 结构判断（本阶段的核心问题）

现有 `HitTest` 的语义是「**几何包含**」——它回答"这个点的位置落在谁的矩形里"。滚动容器要求的是「**几何包含 ∧ 在视口内**」，即多一个"可见性约束"。而 K4 表明，**现在连纯几何包含都超出了父边界**（父不裁剪子的命中）。因此本阶段的接缝设计必须同时解决：

1. **偏移要进命中**（K3）——否则滚完点不中；
2. **命中要受视口约束**（K4）——否则滚出去的东西还能点。

这两点合起来说明：**滚动不是"多画一块"的问题，而是"坐标语义 + 可见性约束"的问题**。这也是本阶段不能只加一个 `SetContentOffset()` 就了事的原因。

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

> 全部 12 项均给出**倾向 + 理由**，待用户与外部评审逐条拍板。**D1–D4 是地基（不定则 R1 无法动笔）**，D5–D8 是行为语义，D9–D12 是范围与账户。

### D1 接缝形态：视口偏移（A）vs 移动子控件（B）？

- **A（视口偏移）**：`Widget` 新增 `virtual int GetContentOffsetX/Y()`（默认 0），在 `Paint`（只移位子控件、自身 `PushClip` 仍用**视口**矩形）/ `HitTest`（子局部坐标多减一个偏移）/ `GetAbsolutePosition`（累加时多减偏移）三处消费。
- **B（移动子控件）**：容器在滚动时对每个子控件 `SetPosition(0, y - offset)`（= `ModelListPanel::ApplyLayout` 的直搬）。
- **倾向：A**。理由：① **B 与 K8 直接冲突**——`VerticalLayout::Arrange` 每次都会重写 `SetPosition`，偏移会被下一次 `Arrange` 抹掉（除非容器与 Layout 之间建立"谁后写"的隐式约定，脆弱）；② B 让**布局结果依赖滚动状态**（同一份子控件在不同 `offset` 下 `GetY()` 不同），使 `GetPreferredSize` / `Arrange` 的语义变得状态相关；③ A 是 K7（TextBox）做法的**容器化推广**，且默认返回 0 ⇒ **零回归**（所有既有控件与测试不受影响）。
- **待拍板细节**：偏移的类型用 `int` 还是 `float`？倾向前者（`Paint`/`HitTest` 全链路已是 `int`，K1/K3），**滚动位置为整数像素**（避免 B 曾有的 float→int 截断抖动）。若未来要亚像素平滑滚动，再单独立项。

### D2 命中可见性约束：在哪一层收口（K4/R3）？

- 候选：① 在 `HitTest` 里加"子节点须落在父边界内"的**通用**判定（影响全部 Widget）；② 只在 `ScrollView` override 一个 `HitTest`（局部收口）；③ 新增虚方法 `virtual bool ClipsChildren()`（默认 false，容器 override 返回 true），`HitTest` 据此决定是否约束子节点。
- **倾向：③（`ClipsChildren()` 门控）**。理由：① 方案① 会**改变所有现有容器的命中语义**（Panel 当前不裁剪子命中——虽 5 个消费点都受 K4 影响，但既有控件树普遍"子控件不越界"，改动面大且风险未评估）；② 方案② 把 `HitTest` 的递归逻辑复制一份到 `ScrollView`，与 K5 的 5 条路径不同源（滚轮/拖入仍走基类）⇒ 约束只在部分路径生效；③ 方案③ 把"是否裁剪子命中"变成**容器的公开声明**，`HitTest` 单点消费、5 条路径自动一致。
- **⚠️ 待核实的风险**：`ClipsChildren()` 与 `CaptionBar` 的交互——**Phase 13 明确依赖"标题栏越界由自身 `PushClip` 裁切"**（详设 P2），若 `CaptionBar` 也被视为裁剪容器，其内部按钮的命中可能被"视口约束"误伤。**初步设计必须先核实 `CaptionBar` 的树结构（按钮是否越界）**，否则为零回归风险点。

### D3 `GetAbsolutePosition` 是否认偏移？（K10）

- 场景：有偏移的容器内的控件，其"绝对坐标"应反映**视觉位置**（含偏移）还是**布局位置**（不含偏移）？
- **倾向：认偏移**（返回视觉位置）。理由：`GetAbsolutePosition` 的既有用途（`Widget.h:222` 注释）是"TextBox 光标 / ScrollBar / Popup / Tooltip 未来用"——这些消费者要的都是**屏幕上真实的位置**；`ScrollBar`（R2）恰好就是它的第一个消费者（拖动时要知道轨道绝对坐标）。若返回布局位置，每个消费者都得自己再减一次偏移（重复且易错）。
- **⚠️ 待核实**：`GetAbsolutePosition` 的**现有调用点**须全库枚举（含 `src/Tests/` 与 `examples/`），确认"认偏移"不会破坏既有消费者——**这是初步设计的第一项任务**（skill 条 33：加/改语义前先列全消费者）。

### D4 `ScrollView` 自身可命中吗？（K6）

- **必须可命中**——否则**空白区滚轮无效**（`Application::OnMouseWheel` 在 `target == nullptr` 时直接 return，K6）。这与 `Panel`（恒 false）**语义相反**。
- **倾向**：`ScrollView` 继承自 `Widget`（**不是** `Panel`），`ContainsPoint` 用基类默认矩形判定（自身可命中）；**不继承 `Panel`**——`Panel` 的"输入完全透传"是它的定案语义（`Panel.h:42-45`），复用会让两个相反语义纠缠（YAGNI：不为省一个基类而制造概念绑架）。
- **待拍板**：`ScrollView` 是否 override `ConsumesMouseInput()`（Phase 13 D9 语义）返回 `true`？——若返回 true，则落在 ScrollView 上的鼠标在 `--borderless` 自绘标题栏场景**不再阻止系统拖拽**。**倾向：不 override**（保持 false）——因为 `ScrollView` 是**容器**，其空白区（如标题栏下方的空白）仍应允许拖动窗口；真正消费鼠标的是其**内部的交互控件**（Button/TextBox），它们自己已 override。**需在初步设计用 `CaptionBar` 场景复核**。

### D5 滚轮步长与高精度滚轮？（K12）

- `MouseWheelEvent::GetDelta()` **原始值不归一化**（K12），且 `TextBox` 既有先例是 `delta / 120.0f * kScrollLinePx`（K13）。
- **倾向**：与 `TextBox` 语义一致——`offset -= delta / 120.0f * lineStep`，其中 `lineStep` 是 **可配置的"一滚一行/一滚若干像素"**；`clamp` 到 `[0, maxOffset]`。**框架不假设一次滚一格**（`delta` 可能是 30/60/240 等高精度值——`/120.0f` 天然支持分数步长）。
- **待拍板**：`lineStep` 的默认值与暴露形态（`SetScrollStep(int px)`？默认多少像素？）——倾向默认 **与 K11 手搓版一致**（`ModelListPanel` 用 `m_rowHeight = 28`，即"一滚一行"）以保 R4 零回归；但**框架默认值**应是通用值（如 28px 或字体行高倍数），由初步设计定。

### D6 内容范围（extent）从哪来？（K14）

- `Layout` 不知道内容有多长（K14），`GetPreferredSize`（9.8）是**单控件自报**，不是"所有子控件总范围"。
- **倾向**：`ScrollView` **自身从直接子控件推导**——`extent = Σ(子控件在主轴方向的占用)` 或 `max(child.GetY() + child.GetHeight())`（取更稳健者）。**不引入"内容尺寸"新 API**（YAGNI：`TextWidget`/`TextBox` 已证明"自报尺寸"够用；容器以子控件实况为准即可）。
- **待拍板**：若子控件自身尺寸在运行期变化（如 `AutoSize` 后），`extent` 是否需要**显式失效通知**？倾向 v1 **不自动挂钩**（与 9.8 的"不挂钩"原则一致）——由调用方在内容变化后调 `ScrollView::UpdateContentExtent()`（或直接 `Invalidate` 时重算）。

### D7 嵌套滚动：事件是否被内层消费后不再上冒？

- 既有 `Application::OnMouseWheel`（`Application.cpp:306-322`）是 **target → parent 冒泡**（与鼠标事件同族），**没有"消费即停"机制**。
- **倾向**：v1 **引入"消费即停"**——`OnMouseWheel` 改为**返回 bool**或在 ScrollView **恰好滚到边界**（`offset` 已到 0 / max）时才继续上冒。理由：嵌套滚动（如 ScrollView 内套 ScrollView）若不做停递，内外层会**同时滚动**（体验错误）；而"边界才上冒"是业界通行语义。
- **⚠️ 这是本阶段**唯一**触碰既有事件派发契约的改动**（改 `Application::OnMouseWheel` + `Widget::OnMouseWheel` 签名或加状态），**零回归风险最高**——初步设计须给"如何在不破坏既有 `TextBox` 滚轮的前提下引入停递"的完整方案（如加一个 `bool m_wheelConsumed` 的上下文，或改虚函数返回 `bool`）。
- **待拍板**：是否值得为嵌套场景在 v1 就引入？（可选：v1 先不做嵌套、单层滚动，把"消费即停"记账到 R2 之后——**倾向仍做**，因为 R2 的 ScrollBar 会让嵌套很快出现）。

### D8 横向滚动是否同批做？

- Phase 9.5 R1 记账"不做水平滚动条"（K9），但那是**针对 `TextBox`** 的记账——**不是**针对通用容器。
- **倾向**：**同批做**。理由：① R1 的接缝（D1）本身就是 `GetContentOffsetX/Y` **成对**设计，只做垂直会留下半截对称（实施时容易只写 `Y` 分支）；② `ModelListPanel` 是垂直的，但 R2 的 ScrollBar 一写就必然面对"垂直/水平"两种朝向（`Orientation` 参数），拆开做等于把同一个控件设计两遍；③ 增量成本低（偏移接缝已对称）。
- **待拍板**：横向的**滚轮映射**（`Shift+滚轮`？还是只靠拖动滚动条？）——倾向 v1 **横向只支持拖动滚动条 + 显式 API**，`Shift+滚轮` 作为可选（`MouseWheelEvent` 是否带修饰键信息需初设核实）。

### D9 R2 与 R1 的绑定关系：谁驱动谁？

- 候选：① `ScrollView` **拥有** `ScrollBar`（内部创建、自动同步）；② `ScrollBar` 是**独立控件**，用户自己 `AddChild` 并手动绑定（`SetTarget` / 回调）。
- **倾向：①（`ScrollView` 拥有，默认创建）+ 可关闭**。理由：R4 的 `ModelProbe` 场景（替换手搓容器）要求"零配置即得滚动条"，若走 ② 则每个使用点都要写绑定代码——与"收回 35 行手搓代码"的初衷相悖。**但保留 `SetScrollBarVisible(false)`**（无滚动条纯滚动仍是合法需求，且是 R1 单独的可用形态）。
- **待拍板**：`ScrollBar` 是否**同时**是公共 API（独立可用）？倾向**是**（`include/ECDI/Widget/ScrollBar.h` 公共头）——它是 R1 的消费者也是独立控件，隐藏它没有收益。

### D10 滚动条视觉：自绘 vs 系统？

- **倾向：自绘**，且**走主题**（`ScrollBarStyle` + `DefaultTheme` 默认值，与 `ButtonStyle`/`PanelStyle`/`ProgressBarStyle` 同款）。理由：① 框架已定"客户区零原生控件"（Phase 12/13 的结论）；② 自绘才能与 `--borderless` 自绘标题栏一致；③ Phase 9 主题系统已落地，新增一个 Style 是既有模式（`StyleField::Set` 标记 overridden）而非新抽象。
- **不引入新能力**：滚动条只需 `DrawRect`（轨道/滑块）+ 圆角（`DrawRoundedRect`）+ 可能的 hover 亮色——**全部是 Phase 8 已有能力**，无需新后端 API。

### D11 滚动范围的"夹紧"归属：钳在 `ScrollView` 还是 `ScrollBar`？

- **倾向：钳在 `ScrollView`（单一真相源）**，`ScrollBar` 只做**展示 + 输入**（`SetRange(extent, viewport, offset)` / 拖拽时回调 `SetOffset`）。理由：偏移是滚动状态的**唯一权威**，若 `ScrollBar` 也持有并自行钳制，两处状态会漂移（`ModelListPanel` 的 `clamp` 在容器里，是正确先例，K11）。
- **待拍板**：`ScrollBar` 拖拽的**回调形态**——`std::function` 回调 vs `ScrollView` 直接持有 `ScrollBar*` 并轮询？（倾向"`ScrollView` 持有指针 + `ScrollBar` 在变化时直接通知容器"，避免引入回调体系的新形态；**但要注意生命周期**——`ScrollBar` 是 `ScrollView` 的子控件，父先于子析构，安全）。

### D12 是否顺手修 K4 的通用缺陷（影响面外扩）？

- K4/K5 表明"子节点越界仍可命中"是**全框架既有缺陷**（5 条路径），`ScrollView` 只是让它**变得可见**。
- **倾向：不在本阶段做通用修复**——只通过 D2 的方案③（`ClipsChildren()` 门控）**让需要裁剪的容器声明式开启**。通用行为是否也要改为"父边界约束命中"，**记账到 `roadmap-deferred.md`**，待有第二个非滚动消费者（如未来的 ListBox/裁剪面板）时再评。理由：① 通用修改会触碰全部既有控件树的命中语义，零回归风险无法在无消费者场景下充分验证（YAGNI + 风险控制）；② `ClipsChildren()` 本身**已经是通用机制**，未来要推广只需把默认值从 false 翻成 true（一次性小改）。
- **待拍板**：是否接受"同一个 `HitTest` 在不同容器下有不同约束语义"这一**有意的不一致**？（倾向接受，并在头文件注释写明原因。）

---

## 4. 范围外（YAGNI 明确不做）

- **ListBox / ListView / TreeView / DataGrid**——`ScrollView` 是它们的**基础设施**，不是终点；控件本身待二次需求
- **TextBox 的可见滚动条**——`TextBox` 的滚动是 **caret 驱动**（K13），与 extent 驱动不同源；Phase 9.5 R1 的"不做水平滚动条"记账**仅指 TextBox**，本阶段不推翻它（若未来要给 TextBox 加条，应让 TextBox 内部视图接入 `ScrollBar`，是本阶段**之后**的独立议题）
- **平滑滚动 / 惯性滚动 / 弹性回弹**（Phase 9.6 动画系统可支撑，但属体验增强）
- **键盘导航滚动**（`PageUp/PageDown/Home/End` + 焦点自动滚入视口）——倾向记账到"焦点滚入"独立条目（与 D7/D8 同族但驱动源不同）
- **虚拟化 / 按需生成子控件**（性能优化，需内容模型抽象；YAGNI）
- **框选 / 多选 / 拖拽重排**（list 控件语义，非容器职责）
- **双轴同时滚动的滚动条布局细节**（右下角交叉区）——若 D8 同批做，交叉区**先简单处理**（不做"双轴都出现时缩角"的精细布局）
- **触摸板手势 / 惯性**（平台层输入能力，非本阶段）

---

## 5. 与既有约束的对齐

| 约束（skill 条号） | 对齐方式 |
|---|---|
| **15 分层**（Widget 层零 Win32） | `ScrollView` / `ScrollBar` 纯 Widget 层实现，零 Win32 类型；滚轮走既有 `MouseWheelEvent` |
| **16 Event 原则**（轻量、事实、不归一化） | 不新增事件类型；`GetDelta()` 原始值语义保持（K12），换算在消费者（D5） |
| **18 唯一入口原则**（框架操作从 RootWidget 进） | 偏移是 **Widget 状态**（可直调，与 `SetSize` 同族），不新增"从 RootWidget 进"的操作 |
| **19/36 RenderCommand 死数据** | 滚动**不新增任何 RenderCommand**——复用既有 `DrawRect`/`DrawRoundedRect`/`PushClip`（K2 已证裁切零新需求） |
| **20 能力/决策层正交** | `ScrollBar` 的样式走 `ScrollBarStyle` + `DefaultTheme`（与 `Button`/`ProgressBar` 同款），不硬编码颜色 |
| **22 YAGNI** | 不做 ListBox/虚拟化/平滑滚动/通用 HitTest 修改（D12）；不引入"内容尺寸"新 API（D6） |
| **23 分层论证纪律** | 本稿所有论证用**契约语言**（"偏移 = 坐标语义的一部分"），不引用 GDI/Win32 行为作为依据 |
| **资源类禁拷贝禁移动** | `ScrollView`/`ScrollBar` 继承 `Widget`，天然禁拷贝禁移动；`ScrollBar*` 成员为**非拥有**（子控件，父先析构） |
| **31 契约双向写全** | D4/D7/D8 涉及"何时有效/何时拒绝"的，须在初设给 `@pre` + 实现 + 契约表 + 用例**四处对称** |
| **33 加纯虚先 grep 实现者** | 若 D1/D2 确定新增虚方法（`GetContentOffsetX/Y` / `ClipsChildren`），**初设必须全库 grep 实现者清单**（含 `src/Tests/`、`examples/` 的测试替身） |
| **9.5 R1 Clip 记账** | K9 的"不做水平滚动条"是**兑现对象**（R2 落地），不是推翻 |
| **9.7/9.8 布局与尺寸** | 偏移**不参与** `Arrange`（D1 选 A 的理由）；`extent` 不挂钩 `GetPreferredSize`（D6） |
| **五阶段法** | 本文档 = 需求确认（只记边界与决策点，不展开接口设计） |

---

## 6. 修订记录

- v1.0（2026-09-17）需求确认初稿：确立 R1–R5 五项范围（**R1 内核与 R2 滚动条同 Phase 交付**——用户拍板）、K1–K14 勘察事实（带行号）、D1–D12 决策点（全部含倾向 + 理由 + 待拍板细节）、非目标 8 项。核心结构判断：滚动容器的真问题不是"多画一块"，而是**坐标语义（K3）+ 可见性约束（K4）**；故接缝倾向 D1-A（视口偏移，零回归）与 D2-③（`ClipsChildren()` 门控，避开通改 `HitTest` 的零回归风险）。
