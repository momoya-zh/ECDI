# Phase 37 · TextBox 垂直对齐——内容不足一屏时居中（deferred #76 ← harness REQ-14）

> 状态：**v1.1**（2026-10-26 外部评审「**修改后通过**」吸收——**PASS ⇒ 可移交初设**；v1.0 同日落盘）｜上游 = `roadmap-deferred.md` **#76**（v2.45 新立 → v2.46 立项）｜来源 = harness `ECDI_fake_harness/docs/ecdi-capability-requests.md` **REQ-14**（v3.8，2026-10-26 新立）
> ★ **v1.1 评审吸收摘要**：**必须修正 1 项**——A4② 表述自相矛盾（「首行视觉中心」与「同一视觉位置」在两配置下不是同一坐标）且缺判别窗口推导 ⇒ 重写为 **A4-1 / A4-2 / A4-3** 三条 + 窗口推导 + 稳定构造参数化 · **初设补清 3 项**——A4 前置钉死 `caret` / `scroll` 状态 · D37-5 调用链核验**并订正 v1.0 的一处事实错误** · A1 基准可复现定义。评审对其余推导（居中偏移不参与 preferred size · `contentH ≤ A` 时固定文本区的 clip 能覆盖偏移后内容 · 超屏偏移归零）确认**自洽**。
> ★ **立项语境**：2026-10-26 用户拍板两条——**① 裁决取 ⓑ**（`TextBoxStyle` 补 `verticalAlign`，缺省 `Top` = 逐位不变）；**② 走正式 Phase 三件套**（下一个空号 = 37）。ⓐ（零 API 无条件居中）被否，理由 = 会改变全部既有 `TextBox` 渲染，破「新能力默认与现状逐位等价」硬线（同 Phase 36 A1 红线）。★ ⓑ 的缺省值即 ⓐ 的开关：将来若判定「居中应无条件默认」，只改缺省值一处，不返工。
> ★ **事实纪律（本稿自约束）**：① 所有行号 = **2026-10-26 对 HEAD 实测**，逐条 `sed`/`grep` 亲核，非转抄账本；② 标注「harness 读数」的数字（四处输入框实测表）来自对方探针，**ECDI 侧未复跑**（条 105 诚实边界），本 Phase 验收不含复跑该项；③ 沿 Phase 31 铁律：**§4 只列决策点与倾向，倾向 ≠ 定案**；④ 规模锚点：`include/ECDI/**/*.h` = **99 件**（本日 `find` 实测；★ **口径待核对**——README 历史锚点链最新为 96，差异不影响本 Phase 的「+1 件」结论）· 用例 **505**（README v2.44 锚点，本日未重跑）· docs **191**（实测）。

---

## §1 输入与问题陈述

### 1.1 缺陷三面（harness REQ-14 取证 + ECDI 侧 2026-10-26 对码全部证实）

| # | 面 | ECDI 侧对码结论（带行号） |
|---|---|---|
| ① | **内容不足一屏时无垂直对齐** | `TextBox::OnPaint` **无单行分支**（`TextBox.cpp:1332-1343`）——单行与多行走同一条顶部路径：`textY = fy + inset`（`:1338`）。⇒ 只有 `H − 2p == 内容高` 时「顶部 + 等分 padding」才**恰好等价**于居中；否则一律偏心 |
| ② | **`padding` 三耦合** | 一个值同时决定 **X 内缩**（`textX = fx + inset`，`:1337`）· **Y 内缩**（`textY`，`:1338`）· **裁切高**（`PushClip` 用 `GetTextAreaHeight() = H − p×2`，`:1343` / `:500-503`）⇒ 想只调一个轴必然动另一个；`p×2 > H − lineH` 时裁切框比行还矮，**字形被上下切掉**（可触发，非假想） |
| ③ | **圆角与内缩互不知道** | `cornerRadius` 只参与**绘制**（`:1281`）；`GetTextLeftInset()` 只返回 `padding`（`:493-498`）⇒ 想让文字躲开圆角只能加大 `padding`，代价是 X/Y 一起变大 |

**框架内部不一致（旁证，本 Phase 的正当性来源）**：同一 `TextWidget` 基类下，`Button` **垂直恒居中**（`TextWidget.cpp:361` `offsetY = (H − lineH)/2`；`ButtonStyle.h:32` 注「垂直恒居中 P7 不动」），`TextBox` 却恒顶部、且**连水平对齐字段都没有**。⇒ 这不是「新需求」，是**已存在的能力没有对称铺开**。

### 1.2 与 `TextBox.h:79` 既有契约的关系（本条的核心争点）

`TextBox.h:79` 原文：单行 → `TextWidget` 内容测量 + `padding×2`（§3.6 验证项：**height = lineH + padding×2 → 自然垂直居中**）。

即框架立场为「**由调用方把尺寸做对**」。本 Phase 的诉求**不是「算错了」**，而是：**该契约在真实 UI 里没有可用解**——

| 输入框（harness 实测，引用） | `H` | `padding` | 圆角 | `H − 2p − lineH` |
|---|---|---|---|---|
| 聊天框 `ComposerBox` | 29 | 6 | 9 | **0**（巧合居中） |
| 设置 BaseURL / API Key / 模型名 | 30 | 0 | 7 | **13** 全在下方 |
| 设置挡位数值框 | 26 | 0 | 7 | **9** |
| 模型浮窗搜索框 | 26 | 0 | 0 | **9** |

⇒ 四处 `H` 全部由**视觉目标**决定（29/30/26/26），其中**三处 `padding = 0`**——按契约必须把 `padding` 抬到 `(H − lineH)/2` 才能居中，但那会让 X 内缩同步变大（面 ②）。**契约本身自洽，只是与 UI 需求冲突**。⇒ 本 Phase 不改契约文本，而是**新增一条与 preferred 高无关的通路**。

### 1.3 ★ ECDI 侧核验新发现（REQ-14 未覆盖，初设必读）

**N1 · 三耦合的代码根因 = 一个函数同时喂两个轴。** `GetTextLeftInset()` 名为 Left，却同时供 Y 使用，Y 侧**三处消费点**：

| 行 | 用途 |
|---|---|
| `TextBox.cpp:1338` | 绘制 `textY`（→ `:1356` `lineY = textY + li×lineH − scrollY`） |
| `TextBox.cpp:676` | `CalculateCaretPosition` → `Point{ GetTextLeftInset() + caretX, GetTextLeftInset() + caretY }`（光标） |
| `TextBox.cpp:753` | `CaretIndexFromPosition` Y 定行：`(localPos.y − GetTextLeftInset() + scrollY) / lineH` |

⇒ **任何加在 Y 上的内缩都必然连带 X**，这就是面 ② 的机理（不是「设计者没想到」，是**单一真相源的副作用**）。⇒ **修法第一步不是加居中，而是把这个函数劈成 X / Y 两个**。`:494` 的注释「单一来源——OnPaint textX / CaretIndexFromPosition innerX 同源防漂移」正是它被设计成共用的原因，劈开时必须保留这个「同源防漂移」属性（两函数各自单一来源）。

**N2 · 居中与裁切互斥是硬约束、不是选择。** `PushClip` 的 y 起点就是 `textY`（`:1343`）⇒ 内容超屏时若居中，**首行直接被裁**。⇒ 居中量必须写成
`offset = max(0, (文本区高 − 内容高) / 2)`
——**一条式子**同时覆盖单行与「多行但不足一屏」，且内容超屏时自动退化为 0。若按 `if (lineCount == 1)` 实现，多行不足一屏仍偏心（打补丁）。

**N3 · 判据应从「单行」升到「内容不足一屏」。** REQ-14 标题写「单行」，但真实需求是「内容不足一屏」。多行 `TextBox`（2 行内容塞进 60px 框）与单行是同一缺口，用上式即可一并覆盖，且不增加任何分支。

**N4 · 「补一个字段」比想象贵。** `TextAlignment`（`Core/TextAlignment.h:15-19`）是**纯水平枚举**（Left/Center/Right），且挂在 `ButtonStyle` 而非共享 `TextStyle`（`TextStyle.h:13` 仅 foreground + font）⇒ 复用会语义污染；新造垂直枚举是**公共头净增 1 件**。

---

## §2 现状勘察（K1–K8，2026-10-26 逐条带行号实测）

| # | 勘察项 | 事实 |
|---|---|---|
| **K1** | 样式数据模型 | `TextBoxStyle`（`TextBoxStyle.h:10-22`）**10 字段**：background / border / selection / composition / caretWidth / caretColor / padding / cornerRadius / borderWidth / borderColor——**零对齐字段**。`TextStyleOverride` 只有 background…borderColor 同序 optional 覆盖（`:25-36`） |
| **K2** | 绘制路径 | `OnPaint`（`:1332` 起）：`inset = GetTextLeftInset()`（`:1336`）→ `textX`（`:1337`）· `textY`（`:1338`）→ `viewX = textX − scrollOffsetX`（`:1340`）→ `PushClip(Rect{textX, textY, GetTextAreaWidth(), GetTextAreaHeight()})`（`:1343`）→ 循环 `lineY = textY + li×lineH − scrollOffsetY`（`:1356`）。★ **`textY` 在 OnPaint 内只有这 2 个消费点**（clip 的 y + lineY 基） |
| **K3** | Y 侧真相源 | 见 §1.3 N1 三处消费点。★ 关键：`GetTextLeftInset()` 是 `protected`（`TextBox.h:327`），与 `GetTextAreaHeight()`（`:330`）同组，**公开侧无 `GetPadding()`** ⇒ 外部无法断言内缩 |
| **K4** | 文本区与滚动 | `GetTextAreaHeight() = H − padding×2`（`:500-503`）；`GetMaxScrollOffset() = max(0, contentH − viewportH)`，`contentH = m_lineStarts.size() × GetLineHeight()`（`:505-511`）；`EnsureCaretVisible` 上/下界同用 `GetTextAreaHeight()`（`:529-533`）。★ 即**内容高与视口高的真相源都已存在**，居中量无需自造口径 |
| **K5** | preferred 口径 | 单行 = `TextWidget::GetPreferredSize()` + `padding×2`（`:196-199`，**无 ceil**）；多行 = `ceil(行数×lineH + padding×2)`（`:224-226`）。⇒ 「preferred 高」确实是「恰好装下」的唯一来源，但它不参与本 Phase 决策（居中不依赖它） |
| **K6** | 主题注入链（加字段的必改面） | ① `Theme` 抽象 `GetTextBoxStyle()` 纯虚（`Theme.h:25`）；② **唯一实现** `DefaultTheme::GetTextBoxStyle()`（`src/Theme/DefaultTheme.cpp:31-52`，逐字段赋值，`padding = 0` / `cornerRadius = 0`）；③ `TextBox::ApplyTheme` 逐字段 `Apply`（`TextBox.cpp:75-97`）；④ `TextBox::SetStyle(TextBoxStyleOverride)` 逐字段 `if (override.x)`（`:99` 起）。⇒ **加 1 字段 = 4 处同步**（结构体 / DefaultTheme / ApplyTheme / SetStyle），漏任一处 = 字段永不生效或永不覆盖 |
| **K7** | 既有测试面 | `TextBoxTests.cpp` **1708 行**；`grep padding·cornerRadius` 命中 **22 处**；**`grep GetCaretClientGeometry·CalculateCaretPosition` = 0 处** ⇒ **既有测试没有任何「文本/光标 Y 起点」断言**。★ 但命令级断言手法**已具备**：`RecordingBackend` + `std::get_if<DrawTextCommand>` 取 `text->pos.y`（`:1135-1147` 现成样例）⇒ A1「逐位不变」**可自动断言**，不必靠口头承诺 |
| **K8** | 缓存 / 指纹面（**本 Phase 关键结论**） | `grep fingerprint` 于 `TextBox.cpp` / `TextWidget.cpp` **零命中**（样式变更走 `Invalidate()` 直接重绘，不做样式指纹比较）；且**对齐不参与任何测量**（不影响 `GetPreferredSize` / `MeasureText` / `LineHeight`）⇒ **本 Phase 无缓存键风险、无指纹扩展面**。★ 这是与 Phase 36（K8 必改键 7 处）的本质差别，评审不应按 Phase 36 的清单要求本 Phase |

---

## §3 范围与非目标

**范围（三项，最终取舍由评审/初设定）**：

1. **`TextBoxStyle` 增加 `verticalAlign`**（主线，= 裁决 ⓑ）——`VerticalAlignment{Top, Center}`，缺省 `Top` = 逐位不变；`Center` 时按 §1.3 N2 的式子居中。
2. **劈开 X / Y 内缩真相源**——新增 Y 侧入口（名待定，倾向 `GetTextTopInset()`），把 `:676` / `:753` / `:1338` 三处 Y 消费点切过去；`GetTextLeftInset()` 保留 X 语义（`textX` / `innerX` / `preferredColumn` 不动）。
3. **裁切框与居动量解耦**——`PushClip` 的 y 固定为 `fy + inset`（文本区原点），不随居动量下移。

**非目标（明确不做）**：

| 项 | 理由 |
|---|---|
| **水平对齐**（`TextBox` 的 Left/Center/Right） | 需动 `TextAlignment`（纯水平枚举）的归属或复制一个——**独立决策**，本 Phase 不夹带。REQ-14 记了这项缺口，保留在账本 |
| **圆角内缩联动**（文字不撞圆角） | REQ-14 的**第二半**，与对齐正交（改的是 X 内缩来源，不是 Y）。`LayoutScrollBar` 的 `max(padding, 2)` 是先例但那个 `2` 是常数、非从半径推 ⇒ 需单独设计 |
| **无条件居中（ⓐ）** | 用户 2026-10-26 已否（破「逐位不变」红线）。★ 但 ⓑ 的缺省值 = 将来 ⓐ 的开关，不封死 |
| **改 `TextBox.h:79` 契约文本** | 契约自洽（§1.2），本 Phase 只是**增加一条旁路**。契约仍在 preferred 高路径上成立 |
| **多行 wrap / preferred 语义 / 滚动逻辑** | 一律不动（K4 / K5 已自洽） |
| **公开 `GetPadding()` 观测缝** | REQ-14 提到它（为了让 harness 断言「内缩 ≥ 圆角」），但那属**圆角那半**的判据需求。本 Phase 的居中 A2/A4 可用命令级断言覆盖，不需要它 |

---

## §4 决策点 D37-1..6（倾向 ≠ 定案；每项初设必须给论证）

| # | 决策点 | 内容与倾向 |
|---|---|---|
| **D37-1** | **枚举形态** | (a) **新 `VerticalAlignment`（`Core/VerticalAlignment.h`，`enum class : uint8_t { Top = 0, Center = 1 }`）**——公共头净增 1 件，`Top = 0` 天然缺省 · (b) 复用 `TextAlignment`（纯水平，语义污染，否）· (c) `bool centerVertically`（丢失将来 `Bottom`，且 `StyleField<bool>` 语义弱）。**倾向 (a)**。★ 初设必答：`Top = 0` 是否足以让「未设值」与「显式 Top」等价（`StyleField` 的 wasSet 语义） |
| **D37-2** | **居中适用条件** | (a) **`Center` 且 `offset = max(0, (areaH − contentH)/2)`**——超屏自动退化 Top（与滚动天然共存） · (b) `Center` 无条件居中（超屏时内容上移出视口 ⇒ 与 `EnsureCaretVisible` 打架，否）。**倾向 (a)**（§1.3 N2）。★ 初设必答：`contentH` 是否严格取 `m_lineStarts.size() × GetLineHeight()`（K4 同源），还是另有口径 |
| **D37-3** | **X/Y 真相源拆分形态** | (a) **新增 `GetTextTopInset()`（private/protected）**，3 处 Y 消费点切换；`GetTextLeftInset()` 保留 X · (b) 给 `GetTextLeftInset()` 加轴参数（调用点语义显式但签名变，且 protected 面扩散）· (c) 居动量只在 `OnPaint` 内局部算（则光标 `:676` 与命中 `:753` **不同源** ⇒ 必然错位，否）。**倾向 (a)**。★ 初设必答：`:676` 的**光标与首行绘制 x 同源**是否会因劈开而破（现 `Point{ LeftInset + caretX, LeftInset + caretY }` 两处同值——劈开后必须仍由各自单一来源保证） |
| **D37-4** | **clip 与居动量关系** | (a) **clip 固定 `{textX, fy + inset, areaW, areaH}`**（文本区语义，不随居动量动） · (b) clip 跟随 `textY`（功能上不裁字——已算：内容底 = 文本区底 − offset，但 clip 底越出控件，语义脏）。**倾向 (a)**。★ 初设必答：`offset > 0` 时 clip 是否仍完整覆盖内容（须给算式，不许「应该没问题」） |
| **D37-5** | **行数时序依赖** | 居动量依赖 `m_lineStarts.size()`，而现有 `GetLineHeight()` **不依赖行数** ⇒ 新入口理论上引入时序约束（`m_needsLineRecalc` 未清时行数滞后）。★★ **v1.1 事实订正（v1.0 写「`CalculateCaretPosition` 不 recalc」= 错）**：三个入口**现状全部**已保证行缓存有效——`OnPaint` 显式重算（`:1331`）· `CaretIndexFromPosition` 显式重算（`:760`）· **`CalculateCaretPosition` 亦重算**（`:657-658` = `if (m_needsLineRecalc) const_cast<TextBox*>(this)->RecalculateLines();`）；v1.0 只读到 `:660` 起的 `m_lineStarts[lineIndex]` 直用，**漏了函数头的惰性重算**（外部评审指出，2026-10-26 已复核属实）。⇒ **本条时序风险低于 v1.0 判断，但非零**（新入口若被三条路径之外的调用方使用仍会读到滞后行数）。候选：(a) 新入口只允许在这三条已保证路径内使用 + 契约注释 · (b) 在 `GetTextTopInset()` 内自查 `m_needsLineRecalc` 并 `const_cast` 重算（与 `:657-658` **同手法、有先例**，不引入新技术）· (c) 把行数改为入参（依赖显式化，但污染签名）。**倾向 (a) + (b) 兜底**：正常路径靠 (a) 零成本，越界调用由 (b) 兜住。★ 初设必答：**新入口的首次调用点清单**（= `GetTextLeftInset()` 现有 5 个调用点中的非 X 专用者），逐点确认位于重算之后 |
| **D37-6** | **`SetStyle` 覆盖与缺省** | `TextBoxStyleOverride` 加 `std::optional<VerticalAlignment> verticalAlign`；`DefaultTheme` 缺省 `Top`。★ 初设必答：`StyleField<VerticalAlignment>::Apply` 与既有 10 字段同构（无特殊处理），以及 `ButtonStyle::textAlignment` 的 C30-3 先例「缺省必须显式注入、不得依赖枚举零值」是否**同样适用**——若适用则缺省注入必须写死 `Top` 而非留空（倾向：适用，但 `Top = 0` 使两者等价，须在初设写明为何不构成 C30-3 同类风险） |

---

## §5 验收 A1–A5

| # | 验收 | 判据 |
|---|---|---|
| **A1** | **存量零回归（红线）** | 缺省（`Top`）路径：505 条既有用例全绿 + **命令级断言证明 `DrawTextCommand.pos.y` 与改前逐位相同**（`RecordingBackend` + `get_if<DrawTextCommand>`，K7 现成手法）。★ 不接受「逻辑上不变」的口头论证——K7 已证明既有测试**无** Y 起点断言，不新增则本条无判别力。★★ **基准必须可复现（v1.1 补，评审要求）**：① 固定输入五元组 `(文本, 字号, DPI, H, p)` 在测试内写成常量；② 期望 `pos.y` **由改前源码导出**——实施前用当前 HEAD（`TextBox.cpp` 未改动态）跑一次 `RecordingBackend` 导出命令序列，存为测试常量表（1 行 / 2 行 / 超屏三组）；③ 详设新增「**基准导出记录**」小节，写明导出时的 commit hash、本机 DPI（120）、字体族与 `lineH` 实测值，使第三方可独立复现「逐位相同」；④ 测试**只断言常量表**，不在测试内重算公式（否则等于用被测逻辑证明被测逻辑） |
| **A2** | **`Center` 生效** | 内容不足一屏时 `lineY` 相对 `Top` 下移 `(areaH − contentH)/2`。≥3 组正交样本：`(H, p, 行数)` = (30, 0, 1) / (29, 6, 1) / (60, 4, 2) —— 其中 (29,6,1) 的 offset 必须为 0（巧合组，验证不会二次下移） |
| **A3** | **超屏退化** | `contentH > areaH` 时 `offset == 0`，绘制/滚动/`GetMaxScrollOffset` 与改前逐位同（多行超屏 + wrap 关 + `scrollOffsetY > 0` 各一组） |
| **A4** | **三消费点同源（本 Phase 最强判据）** | **共同前置（v1.1 补，评审要求）**：`caret = 0`（首行）+ `scrollOffsetY = 0` + `scrollOffsetX = 0`——因 `CalculateCaretPosition` 的 `caretY = lineIndex×lineH − scrollOffsetY`（`TextBox.cpp:675`），不钉死光标行与滚动量则 Y 不可比。**记号**：`p` = padding · `L` = 行高 · `A = H − 2p` · `n` = 行数 · `contentH = nL` · `O = max(0, (A − nL)/2)`。★ **A4-1 光标同源（三消费点之一，最易漏）**：`GetCaretClientGeometry().y` 必须 `==` 首行 `DrawTextCommand.pos.y`（前置已钉 `caret = 0` / `scrollY = 0`）——`:676`（光标）与 `:1338`（绘制）分属两个函数，是 N1 三处里最容易只改一处的。★ **A4-2 命中跟随绘制（单配置可判）**：点击 `y = p + O + L/2` 必须命中的是**首行**码点区间 `[lineStart, lineStart + 首行码点数)`；若命中测试未跟随（仍以 `p` 为原点），该坐标会落到**行 1** ⇒ 判别力成立。★ **A4-3 跨配置对比（加强）**：对**同一绝对坐标** `y`（不随对齐变化）分别断言 `Top` / `Center` 下的命中**行号**，两者必须不同。★★ **窗口推导（v1.1 补，同时订正 v1.0 的表述矛盾）**：`Top` 行号 = `floor((y − p)/L)`，`Center` 行号 = `floor((y − p − O)/L)`；取 `y = p + O + L/2` 且 `O ≥ L/2` ⇒ `Top` = `floor((O + L/2)/L) ≥ 1`（行 1）、`Center` = `floor((L/2)/L) = 0`（行 0）⇒ **必跨一行**。★ **判别窗口宽度 = `O`，不是 `L`**：只要 `O > 0` 就存在可判别坐标，**不需要 `O > L`**——v1.0 的「首行视觉中心」与「同一视觉位置」彼此矛盾（前者在两配置下是两个不同坐标，后者暗示同一坐标），已作废改写。**稳定构造（本机 SimSun@14，`L = 16`，详设复核）**：`H = 60 / p = 4 / n = 2` ⇒ `A = 52`、`contentH = 32`、`O = 10 ≥ L/2 = 8` ✓ ⇒ 点击 `y = 4 + 10 + 8 = 22`：`Top` = `floor(18/16) = 1`（行 1）· `Center` = `floor(8/16) = 0`（行 0）⇒ 判别成立。★ **兜底条款**：**A4-1 必须通过**（光标与绘制同源是硬要求）；A4-2 / A4-3 **至少一条必须通过**（命中跟随绘制的自动证据）。A4-2 / A4-3 两条都不过 ⇒ 「命中消费点同源」无自动证据，须在详设如实登记为已知缺口（沿 Phase 36 A2「方向判据」如实登记先例），**不得以「代码已改」代替验收** |
| **A5** | **主题与覆盖链** | `DefaultTheme` 缺省 = `Top`（零行为变化）；`SetStyle` 显式 `Center` 生效；`ApplyTheme` 后未被 override 的字段回落到 `Top`。★ 四同步处（K6）逐处有用例，防「字段加了但永不生效」 |

---

## §6 影响面

| 项 | 估值 | 说明 |
|---|---|---|
| 公共头 | **99 → 100** | 口径 = `include/ECDI/**/*.h` 本日实测；新增 `Core/VerticalAlignment.h`（1 件）；`TextBoxStyle.h` 为既有文件字段扩展 |
| 公共 API | **+1 字段**（`TextBoxStyle::verticalAlign`，缺省 `Top`）+ 1 枚举 | 无新方法；`GetTextTopInset()` 走 protected（不扩公开面） |
| 用例 | **505 → ~511**（估 +6） | A1 ×1 · A2 ×3 · A3 ×2 · A4 ×2 · A5 ×3（初设细化，可能净 +8） |
| CMake | **0** | `GLOB_RECURSE` 自动入库；测试文件另需 `RunAllTests.h` 声明 + `RunAllTests.cpp` 调用（手工，既有纪律） |
| 内部改动面 | `TextBox.cpp`（新增 Y 入口 + 3 消费点 + clip 一处）· `TextBoxStyle.h`（+1 字段 + +1 optional）· `DefaultTheme.cpp`（+1 行）· `TextBox::ApplyTheme`（+1 行）· `TextBox::SetStyle`（+1 行）· `Core/VerticalAlignment.h`（新） | **无缓存/指纹面**（K8） |
| 风险 | **低-中** | 算式简单、无后端参与（纯几何，**GDI/GL 天然一致**——两侧都只消费命令，不重算对齐）。真风险 = **三消费点漏改**（N1）→ 由 A4 兜底；以及 D37-5 的行数时序 |

---

## §7 初设闸门（移交初设的必答项）

1. **三消费点清单逐处定案**——`:676` / `:753` / `:1338` 各自改用什么入口、`textX` 一侧为何不受影响，**逐行写出**；★ **v1.1 订正**：`CalculateCaretPosition` **已有**惰性重算（`:657-658`），故本闸门改为「列出 `GetTextTopInset()` **全部调用点**（= `GetTextLeftInset()` 现有 5 点中的非 X 专用者）并逐点确认位于重算之后，或由入口内 `const_cast` 自查兜底」（D37-5）。
2. **clip 算式**——`offset > 0` 时 clip 仍完整覆盖内容的**不等式推导**（D37-4），不许只写「不会裁字」。
3. **C30-3 同型风险评估**——`ButtonStyle::textAlignment` 的「缺省必须显式注入」纪律是否适用于 `verticalAlign`（D37-6）；若认为不适用，给出与 `Top = 0` 的关系论证。
4. **A4 的可构造性（v1.1 已给构造，闸门转为实测复核）**——`H = 60 / p = 4 / n = 2 / L = 16` ⇒ `O = 10`、点击 `y = 22` 的构造须在详设**实测复核**；`L` 随 DPI / 字体变化 ⇒ 公式必须给**参数化形式**（`y = p + O + L/2`，前置 `O ≥ L/2`），**严禁硬编码 22**。★ A4-1 必须过；A4-2 / A4-3 至少过一条，两条都不过 ⇒ 如实登记降级理由。
5. **勿重开**：水平对齐、圆角内缩、`TextBox.h:79` 契约改写、无条件居中（ⓐ）、`GetPadding()` 观测缝——五项全在 §3 非目标，任何一项要拉回来必须**先改本稿 §3**并重新评审。

---

## §8 修订记录

- **v1.1（2026-10-26）** 外部评审「**修改后通过**」吸收。① **A4 重写（必须修正项）**：原 A4② 表述**自相矛盾**——「首行视觉中心」在两对齐配置下是**两个不同坐标**，而「同一视觉位置」又暗示**同一坐标**，二者不能同时成立；且未给判别窗口 ⇒ 无法证明可区分（评审按 `(60,4,2)` 算得偏移 10px 遂判「不可能跨行」，实为缺推导所致）。重写为三条：**A4-1 光标同源（硬过）** / **A4-2 命中跟随绘制（单配置可判）** / **A4-3 跨配置行号对比（加强）**，附**窗口推导**（判别窗口宽度 = `O` **不是** `L`；`O > 0` 即有窗口，取 `y = p + O + L/2` 且 `O ≥ L/2` ⇒ `Top` 落行 1、`Center` 落行 0）+ **稳定构造**（`H = 60 / p = 4 / n = 2 / L = 16` ⇒ `O = 10`、`y = 22`）+ 兜底条款（A4-1 硬过；A4-2 / A4-3 至少一条，都不过则**如实登记缺口**，不得以「代码已改」代替验收）。② **A4 前置钉死**：`caret = 0` + `scrollOffsetY = 0` + `scrollOffsetX = 0`（`caretY = lineIndex×lineH − scrollOffsetY`，`:675`）——不钉死则 Y 不可比。③ ★★ **D37-5 事实订正（自我订正：v1.0 写错了）**：v1.0 称「`CalculateCaretPosition` 不 recalc」，实为 **`:657-658` `if (m_needsLineRecalc) const_cast<TextBox*>(this)->RecalculateLines();` 惰性重算**；错因 = 当时只 `sed` 了 `:660` 起的 `m_lineStarts[lineIndex]` 直用，**漏看函数头 7 行**。⇒ 三入口**现状全部**已保证行缓存有效，时序风险**低于** v1.0 判断；倾向由「(a) 契约注释」升级为「**(a) + (b) 入口内 `const_cast` 自查兜底**」（与 `:657-658` 同手法、有先例，不引入新技术）。④ **A1 基准可复现**：五元组常量 `(文本, 字号, DPI, H, p)` + 改前源码导出 `pos.y` 常量表（1 / 2 / 超屏三组）+ 详设「基准导出记录」（commit hash / 本机 DPI 120 / `lineH` 实测）+ **测试只断言常量表、不重算公式**。⑤ §7 闸门第 1 / 4 项同步改写（闸门 1 由「核 `CalculateCaretPosition` 调用链」改为「列 `GetTextTopInset()` 全部调用点」；闸门 4 由「构造可行性」改为「构造实测复核 + 参数化严禁硬编码」）。
- **v1.0（2026-10-26）** 初稿落盘。来源 = `roadmap-deferred` #76（v2.45）+ harness REQ-14（v3.8）。裁决 ⓑ 已由用户拍板并写入 §1 立项语境。§2 K1–K8 全部本日实测；K8「无缓存/指纹面」为**否定性结论**（grep 零命中），刻意写入以防评审按 Phase 36 清单误加压。§1.3 N1–N4 四条为 ECDI 侧新增判据（REQ-14 未覆盖），其中 N1（`GetTextLeftInset()` 同时喂 X/Y = 三耦合代码根因）与 N2（`max(0, …)` 一条式子）直接决定 D37-2 / D37-3 的形态。
