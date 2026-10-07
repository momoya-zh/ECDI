# Phase 30 · 文本 / 交互快修小轮 —— 初步设计（v1.0）

> 来源：需求 `phase30-interaction-quickfix-requirements.md` **v1.1（评审 PASS → 初设）**——评审明示的**初设必钉 5 件事**逐题钉死（本稿 §1 D30-A..E），另加 D30-F（caretColor 契约化）。
> 状态：**v1.0**（2026-10-07，待评审）
> 定位：**实施规格前的语义冻结**——三项改动各钉到「公式/映射/接线点」级；快修 Phase 不做架构择型（消费点与既有契约都已存在）。

---

## 1. 初设必答（评审 5 件事 + 1，逐题钉死）

### D30-A ★ TextAlignment 独立公共头（评审必钉①——定案）

- **落位 = `include/ECDI/Core/TextAlignment.h`**（新建，**零依赖**——纯 `enum class : std::uint8_t`，公共头自包含纪律）：
  ```cpp
  enum class TextAlignment : std::uint8_t{ Left = 0, Center = 1, Right = 2 };
  ```
- **依赖方向论证**（评审原话的结构化）：塞 `ButtonStyle.h` 会让未来 multiline 对齐产生 `TextWidget → ButtonStyle.h → TextAlignment` 的怪方向；独立头则 `ButtonStyle` 与 `TextWidget`（O1 未来）**并列**依赖它。**Phase 30 不实现 O1**（D30-E）。
- 公共头 **94 → 95**；枚举成员稳定排序（Left=0 缺省值语义——`StyleField<TextAlignment>` 默认构造 `value{}` = Left，**注意**：Button 的缺省对齐必须显式由主题注入 Center，不能依赖枚举零值——见 D30-B/C30-4）。

### D30-B ★★ Button 内容区公式冻结（评审必钉②——先摊清现状再定公式）

**现状实测（B4）**：`Button::CalculateTextPosition`（`Button.cpp:92-99`）用**裸 `GetWidth()`**——`offsetX = (GetWidth() - textWidth) / 2`，**没有任何 padding/inset 参与**；`ButtonStyle::borderWidth` 的「内框尺寸」逻辑**已随旧实现移除**（`Button.cpp:232` 注释「焦点框不再需要内缩计算——YAGNI」），borderWidth 现只作边框绘制语义、**不参与文本定位**。⇒ **评审担心的「Left/Right 有 padding、Center 用全宽」的不一致，在现状下不存在输入**：文本定位从来就没有内容区概念。

**冻结公式（三模式共用同一内容区 = 全控件矩形，x 为控件原点）**：

```text
Left   : x + 0
Center : x + (GetWidth() - textWidth) / 2        // ★ 与现状逐位相同（默认零变化）
Right  : x + GetWidth() - textWidth
垂直（三模式共用）: y + (GetHeight() - lineHeight) / 2   // P7 契约不动
```

- **不引入 Button padding**（无消费者证据——YAGNI；将来若引入，属新 Phase 且三模式同步接入）。
- `textWidth` 来源不变：`DrawTextContent` 的 `ctx.MeasureText`（B5）。

### D30-C ★★ H/V C-VIS 四层契约**逐项映射** ListLayout（评审必钉③）

Phase 25 C-VIS 是**结果契约**（测行为不测坐标技巧）；H/V 与 ListLayout 的宿主语义不同，映射必须**如实分层**，不硬套：

| 层 | ListLayout（Phase 25 既有） | VerticalLayout / HorizontalLayout（本 Phase） |
|---|---|---|
| **C-VIS-1 排列语义** | 可见子连续槽位（`slot` 计数） | ✅ **适用**：可见子连续排列；**间隙 = 可见数 − 1**；stretch 求和/固定尺寸求和**排除隐藏子**；**末位可见 stretch 子吃余数**（`stretchSeen == visibleStretchCount`） |
| **C-VIS-2 extent 结果** | 不可见不增宽高（`UpdateContentExtent` 遍历全量 ⇒ 靠停泊负区贡献经负向钳 0 归零） | **不直接适用**——H/V 不产 extent；但停泊负区后，若宿主在 ScrollContent 内，其 extent 贡献**经同一负向钳 0 机制自动归零**（Phase 25 机制复用，零新代码） |
| **C-VIS-3 hit-test 结果** | 可见区不可命中 | **既有**——`Widget` 入口门控（`Widget.cpp:114-121`）对停泊在负区的子天然不可命中；**布局层零新增命中逻辑**（Phase 25 纪律：本布局不承担命中语义） |
| **C-VIS-4 实现策略** | 停泊自身 bbox 负区 `(−w, −h)` | ✅ **照搬原文**（`SetPosition(-child->GetWidth(), -child->GetHeight())`）——**不在 Phase 30 重新讨论停泊位**（评审第 7 节） |

**实现形态**（两遍结构保序）：

```text
第一遍（求和）：
  visibleStretchCount / totalStretch / fixedTotal —— 隐藏子全部跳过
  visibleCount 单独计数
第二遍（分配 + 定位）：
  if (!child->IsVisible()) { 停泊 (−w, −h); continue; }        // ★ 先于任何 SetSize（用当前尺寸）
  spacing 只在「可见子之间」累加（前一个可见子存在时 + spacing）
  末位可见 stretch 子吃余数（allocated 对齐 visibleStretch 求和）
remaining 公式：parent 轴长 − 2·padding − fixedTotal − spacing·(visibleCount − 1)
```

- **全隐藏**：`visibleCount == 0` ⇒ 无分配、无间隙、全员停泊、不 panic（`remaining` 计算照跑但结果不被消费——实现须保证除零不可能：`totalStretch == 0` 时走非 stretch 分支）。
- **动画中隐藏**：H/V 无动画语义，不在范围（需求 §3.2）。

### D30-D ★ 全可见逐位不变红线 + **专测**（评审必钉④）

- 结构性保证：`visibleCount == count` ⇒ `spacing·(visibleCount−1)` 退化为现式、求和集合不变、分配序不变、停泊分支不可达 ⇒ **结果逐位相同**。
- ★ **不只靠 349 条存量**：新增**专测** `Layout.HVAllVisibleBitIdentity`（T30-4）——V/H 两布局各构造一组含 stretch 的子树，期望值**手算旧公式**逐项比对（评审 A2 补刀的落实）。

### D30-E ★ 范围围栏：枚举 ≠ O1 实现（评审必钉⑤）

**Phase 30 只创建 `TextAlignment` + Button 消费；`TextWidget` multiline 对齐（Phase 29 O1）保持未实现**——禁止「枚举已经有了顺便把 TextWidget 接上」。落点：O1 状态在台账保持不动；`TextWidget::CalculateTextPosition` **零改动**（基类仍左对齐 + 垂直居中）；盯防⑤机检。

### D30-F caretColor 契约化（评审第 2 节建议——写成契约）

- `DefaultTheme::GetTextBoxStyle` +`s.caretColor.value = Color::Black()`（**零行为变化**——现状 :1351 就是 Black）。
- Override 语义沿 `StyleField` 既有状态机（**B3 实测**：`Set()` 标记 overridden ⇒ 后续 `Apply()` 被忽略；`ApplyTheme` 只更新未 Override 字段——D7 契约的代码级保证，无新机制）。
- 接线四点（与 `caretWidth` 完全同构）：`TextBoxStyle.h` 声明 → `TextBoxStyleOverride` 声明 → `TextBox::ApplyTheme` Apply（:82 `caretWidth.Apply` 邻位）→ `TextBox::SetStyle` Set（:103 邻位）→ 消费点 `:1351`（`Color::Black()` → `m_style.caretColor.value`）。

---

## 2. 现状事实（B1–B8，全部带行号实测 2026-10-07）

- **B1** 光标唯一消费点 = `TextBox.cpp:1351`（`DrawRect(..., Color::Black())`）；全文件无第二处光标色。选区（`selection`）/组合串（`composition`）均已样式化——唯独光标漏网。
- **B2** `TextBoxStyle` 9 字段（`TextBoxStyle.h:8-19`）+ `TextBoxStyleOverride` 9 项（:23-33）——加字段即同族扩展。
- **B3** `StyleField<T>` 状态机（`StyleField.h`）：`Set()` → overridden=true；`Apply()` 只在 !overridden 时更新——**ApplyTheme/SetStyle 无新机制可写**。
- **B4** `Button::CalculateTextPosition`（`Button.cpp:92-99`）裸 `GetWidth()`；**无 padding/inset 概念**；`borderWidth` 内框逻辑已移除（:232）⇒ 评审「内容区不一致」风险不存在输入。
- **B5** `Button::OnPaint` 经 `DrawTextContent(ctx, x, y)`（:261）→ 基类用 `ctx.MeasureText` 得 `textWidth` 传入 `CalculateTextPosition`——改对齐**不动测量路径**。
- **B6** Button 接线点：`ApplyTheme`（:37-50，`theme.GetButtonStyle()` 于 `DefaultTheme.cpp:14-23`）+ `SetStyle`（:66-85）——textAlignment 加两行。
- **B7** TextBox 接线点：`ApplyTheme`（:74-86，caretWidth.Apply 在 :82）+ `SetStyle`（:97-107，caretWidth.Set 在 :103）。
- **B8** 测试落位：`LayoutTests.cpp`（1159 行注册区，既有 Stretch/Spacing/CrossFill 七用例——T30-4..6 落此）；`CollapsiblePanelTests.cpp`（:284-287 四用例——T30-7 落此）；Button 对齐落 `WidgetTests.cpp`（既有 TestButtonPaint 同区）；caret 落 `TextBoxTests.cpp`（TestableTextBox 装置现成）。

---

## 3. 逐文件改动（△1–△8）

| △ | 文件 | 改动 |
|---|---|---|
| △1 | `include/ECDI/Core/TextAlignment.h` | **新建**：`enum class TextAlignment : std::uint8_t { Left, Center, Right }`（零依赖、自包含——公共头 94→95） |
| △2 | `include/ECDI/Theme/TextBoxStyle.h` | +`StyleField<Color> caretColor`（:16 caretWidth 邻位）+ Override +`std::optional<Color> caretColor` |
| △3 | `include/ECDI/Theme/ButtonStyle.h` | +`StyleField<TextAlignment> textAlignment` + Override +`std::optional<TextAlignment> textAlignment` |
| △4 | `src/Theme/DefaultTheme.cpp` | `GetTextBoxStyle` +`s.caretColor.value = Color::Black()`；`GetButtonStyle` +`s.textAlignment.value = TextAlignment::Center`（默认 = 现行为） |
| △5 | `src/Widget/TextBox.cpp` | `ApplyTheme` +caretColor.Apply（:82 邻位）；`SetStyle` +caretColor.Set（:103 邻位）；消费 `:1351` `Color::Black()` → `m_style.caretColor.value` |
| △6 | `src/Widget/Button.cpp` | `ApplyTheme` +textAlignment.Apply；`SetStyle` +textAlignment.Set；`CalculateTextPosition`（:92-99）按 D30-B 公式三态化（**默认 Center 逐位 = 现状**） |
| △7 | `src/Layout/VerticalLayout.cpp` + `src/Layout/HorizontalLayout.cpp` | D30-C 映射落地：两遍结构 + 停泊 + 间隙只算可见 + 末位可见 stretch 吃余数 + 全隐藏不 panic |
| △8 | 测试 | `TextBoxTests.cpp` +T30-1；`WidgetTests.cpp` +T30-2/3；`LayoutTests.cpp` +T30-4/5/6；`CollapsiblePanelTests.cpp` +T30-7 |

- **新文件 1 个**（△1 公共头，无 .cpp ⇒ CMake 0；FRAMEWORK_SOURCES GLOB 不受影响）；其余扩员。

---

## 4. 契约（C30-1..7）

- **C30-1** caretColor：默认 `Color::Black()`；Override 经 `StyleField` 既有状态机；`ApplyTheme` 只更新未 Override 字段（D7）——**默认态逐位 = 现状**。
- **C30-2** Button 对齐三模式共用**全控件矩形**内容区（无 padding 概念——D30-B 冻结）；垂直恒居中（P7 不动）。
- **C30-3** 默认 `TextAlignment::Center` 经主题显式注入（**不依赖枚举零值**——D30-A 的缺省值陷阱）。
- **C30-4** H/V C-VIS：可见子连续槽位、间隙 = 可见数 − 1、stretch/固定求和排除隐藏、**末位可见 stretch 吃余数**、隐藏子停泊 `(−w, −h)`（先于 SetSize、用当前尺寸）。
- **C30-5** **全可见 ⇒ 逐位不变**（D30-D；专测 T30-4）。
- **C30-6** 全隐藏 ⇒ 不 panic、全员停泊（除零结构性不可能）。
- **C30-7** 范围围栏：本 Phase **不实现** Phase 29 O1；`TextWidget::CalculateTextPosition` 零改动。

---

## 5. 盯防（5 条）

1. 停泊**先于** SetSize（用当前尺寸——与 ListLayout `ListLayout.cpp:24` 同构；顺序错了停泊位会用到分配后的尺寸——虽然仍在负区，但破坏「自身 bbox 负区」语义）。
2. **末位可见 stretch**：`stretchSeen == visibleStretchCount` 的比较对象必须是**可见**计数——照抄旧 `stretchCount` 会把余数给到隐藏 stretch 子（不可达分支）或提前触发。
3. 间隙累加位置：只在「上一子可见 && 当前子可见」时 + spacing（实现用 lastVisible 标志，不数索引）。
4. C30-3 缺省值陷阱：任何 `TextAlignment{}` 零值 = Left——主题/字段初始化必须显式 Center（评审 D2 论证的落地点）。
5. O1 围栏机检：`TextWidget.cpp` 本 Phase diff 为零（C30-7）。

---

## 6. 用例（7 条，349 → 356）

| # | 名（注册名） | 文件 | 断言核心 |
|---|---|---|---|
| T30-1 | `TextBox.CaretColorStyle` | TextBoxTests | 默认 Black（命令流 DrawRect 色 == Black）；override 自定义色生效；ApplyTheme 不覆盖 Override（D7） |
| T30-2 | `Button.TextAlignmentModes` | WidgetTests | Left/Center/Right 三态 pos.x（手算公式）+ 垂直 y 三态相同 + **默认 Center = 现状逐位** |
| T30-3 | `Button.TextAlignmentOverride` | WidgetTests | SetStyle override 生效；ApplyTheme 后 Override 保持；无窗口降级路径不 panic |
| T30-4 | `Layout.HVAllVisibleBitIdentity` | LayoutTests | **全可见专测**：V/H 各含 stretch 组，期望 = **手算旧公式**逐项（评审 A2 补刀） |
| T30-5 | `Layout.VerticalCVis` | LayoutTests | 隐藏子停泊 (−w,−h) + 可见连续槽位 + 间隙 = 可见−1 + 末位可见 stretch 吃余数 + 部分隐藏序（A→C→E）+ **全隐藏不 panic** |
| T30-6 | `Layout.HorizontalCVis` | LayoutTests | T30-5 的水平同构（含跨轴 fill 与隐藏子交互） |
| T30-7 | `CollapsiblePanel.CollapseVisibilityPin` | CollapsiblePanelTests | 收起（含动画路径）⇒ 内容子树**零 DrawText + HitTest false**；展开 ⇒ 恢复；双 Toggle 幂等（**不改码——现状钉住**） |

---

## 7. 批次

| 批 | 内容 | 验收 |
|---|---|---|
| 批一 | △1–△6（caretColor + TextAlignment/Button）+ T30-1/2/3 | 五链全绿 + 默认态零变化（caret Black / Button Center） |
| 批二 | △7–△8（H/V C-VIS + 钉住）+ T30-4..7 + 收口 | 五链全绿 + C30-5 专测 + 五处台账 |

---

## 8. 影响面（初设校准）

| 项 | 预算 |
|---|---|
| 公共头文件数量 | **94 → 95**（+`Core/TextAlignment.h`——D30-A 定案） |
| 公共 API | **+1 类型**（`TextAlignment`）**+ 4 样式字段**（caretColor / textAlignment × Style+Override——与 Phase 29 虚函数计数口径不同族，如实分开记） |
| 用例 | **349 → 356**（+7） |
| CMake | **0**（新头无 .cpp；GLOB 不受影响） |
| 风险 | **低**——默认态三处全「零变化」；唯一行为变化（H/V 隐藏子）有 Phase 25 契约背书 + 专测锚 |

---

## 9. 开放项

| # | 项 |
|---|---|
| O1 | **Phase 29 O1（wrap 每行对齐）保持未实现**——本 Phase 只建枚举（C30-7；D30-E） |
| O2 | Button 文本 padding（无消费者证据——将来引入须三模式同步） |
| O3 | 垂直对齐（Top/Middle/Bottom——无消费者证据） |
| O4 | caretColor 之外的 TextBox 颜色族补漏巡检（selection/composition 已样式化——本 Phase 巡检确认无第四处） |

---

## 10. 修订记录

- **v1.0**（2026-10-07）初稿。**输入**：需求 v1.1（评审 PASS）+ 评审初设必钉 5 件事 + 补充勘察（B1–B8 带行号——**B4 直接消解评审第 4 问**：Button 文本定位从无 padding 概念，borderWidth 内框逻辑已随旧实现移除 [`Button.cpp:232`] ⇒ 三模式内容区 = 全控件矩形、无不一致输入）。**D30-A..F 逐题钉死**：A 独立头（依赖方向论证）· B 内容区公式冻结（Left=x+0 / Center=现状 / Right=x+w−tw；垂直恒居中）· C 四层契约**逐项映射**（C-VIS-2 对 H/V 不直接适用——extent 由停泊负区 + 既有负向钳 0 机制自动归零，如实分层不硬套）· D 全可见红线 + 专测 T30-4 · E 范围围栏（O1 不实现）· F caretColor 契约化（接线四点 + D7）。△1–△8（新文件 1 = 公共头）· C30-1..7 · 盯防 5 · 用例 7 条（349 → **356**）· 两批 · 94→95 / API +1 类型 +4 字段 / CMake 0 / 风险 低。
