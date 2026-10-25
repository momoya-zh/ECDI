# Phase 30 · 文本 / 交互快修小轮 —— 详细设计（v1.0）

> 来源：初设 `phase30-interaction-quickfix-preliminary-design.md` **v1.0（评审 PASS → 详设）**——评审**无 blocker**，三个非 blocker 确认点（remaining 负值 / 末位 stretch 伪代码 / 停泊机械步骤）+ 详设展开清单逐项落实（本稿 §1 D30-G..I + §2/§3/§5）。
> 状态：**v1.2**（2026-10-07）——**✅ 全链收口**（两批落地，五链 356/356；A1–A4 全判；§11 实施回填）｜v1.1：**✅ 评审通过（PASS → Implementation，「比较明确的 PASS，非 Conditional」）**（外部评审 2026-10-07：14 项分表全 ✅、无 blocker；两处纯文档修正当场修毕——修正明细见 §9 v1.1；**评审明示「不要再改设计了」⇒ 直接进 Batch 1**）
> 定位：**「实现者照着做不会歧义」**——评审明示**不再增加新架构设计**；本稿 = 精确伪代码 + 精确公式 + 精确断言。

---

## 1. 详设必答（初设评审 3 确认点逐题钉死 + 展开清单）

### D30-G ★ remaining 负值与**乘法守卫**（评审确认①）

- **负值语义 = 沿用既有，不创造新规则**：`remaining` 的钳制已存在——`VerticalLayout.cpp:38-39` / `HorizontalLayout.cpp:38-39` 的 `(std::max)(0, …)`（Phase 17 F4 冻结点「负值钳 0」）。Phase 30 **原样保留**该钳制：stretch 子在空间不足时得 0 尺寸——既有 overflow 行为照旧。
- ★★ **详设期新识别的算术边界（必须守卫）**：`visibleCount == 0`（全隐藏）时，`spacing × (visibleCount − 1)` = `spacing × (−1)` = **−spacing** ⇒ `remaining = parent − … − (−spacing)` = **反而增大 spacing**。既有代码不可达此分支（`count == 0` 早退 + 无隐藏概念），C-VIS 化后**可达** ⇒ 乘法守卫为硬性要求：

```text
gapCount = (visibleCount > 0) ? (visibleCount - 1) : 0
remaining = (std::max)(0, parentAxis - 2 * padding - fixedTotal - spacing * gapCount)
```

（C30-6 的算术化；全隐藏时 `remaining` 照算但不被消费——无分配发生。）

### D30-H ★ 末位可见 stretch——精确伪代码（评审确认②）

```text
// 第二遍循环体内，可见 stretch 子分支：
++stretchSeen;
axis = (stretchSeen == visibleStretchCount)      // ★ 比较对象 = 可见计数（盯防②机检锚——
           ? remaining - allocated                //   旧名 stretchCount 禁止再现于新代码）
           : remaining * child->GetStretch() / totalStretch;   // 整数除法截断（既有 D2 不变）
allocated += axis;
```

- `totalStretch` / `visibleStretchCount` 均来自**第一遍的可见子集合**（隐藏 stretch 子既不进权重也不进计数）。
- **全可见时逐位等价旧式**：`visibleStretchCount == stretchCount`、`visibleCount == count` ⇒ 同一算式（C30-5 的结构性依据）。

### D30-I ★ 停泊机械步骤（评审确认③——写成不可歧义的顺序）

```text
if (!child->IsVisible()):
    const int parkW = child->GetWidth();      // ① 先取**当前**尺寸
    const int parkH = child->GetHeight();
    child->SetPosition(-parkW, -parkH);       // ② 用旧值停泊（自身 bbox 负区——C-VIS-4）
    continue;                                  // ③ 不 SetSize、不参与分配、不吃 spacing
```

- **顺序不可换**：先 SetPosition 后 SetSize 的任何形态都会用新尺寸停泊——虽然仍在负区，但破坏「自身 bbox 负区」语义（初设盯防①升级为机械步骤）。
- 隐藏子**跳过跨轴 fill**（不 SetSize）——这是相对旧行为的唯一变化点，且仅影响隐藏子。

### D30-C′ 两遍算法全文（VerticalLayout；HorizontalLayout 轴互换同构）

```text
Arrange(parent):
  count = parent.GetChildCount()
  if (count == 0) return                                  // 既有早退保留

  // ── 第一遍：可见子集合统计 ──
  visibleCount = 0; visibleStretchCount = 0; totalStretch = 0; fixedTotal = 0
  for child in children:
      if (!child->IsVisible()) continue
      ++visibleCount
      if (child->GetStretch() > 0):
          totalStretch += child->GetStretch(); ++visibleStretchCount
      else:
          fixedTotal += child->GetHeight()                 // V 取高 / H 取宽

  // ── remaining（D30-G：既有钳制 + 乘法守卫）──
  gapCount = (visibleCount > 0) ? (visibleCount - 1) : 0
  remaining = (std::max)(0, parentAxis - 2 * padding - fixedTotal - spacing * gapCount)
  cross     = (std::max)(0, parentCross - 2 * padding)     // 既有 △4 钳制保留

  // ── 第二遍：分配 + 定位 ──
  cursor = padding; allocated = 0; stretchSeen = 0; prevVisible = false
  for child in children:
      if (!child->IsVisible()):                            // D30-I 三步
          parkW = GetWidth(); parkH = GetHeight()
          child->SetPosition(-parkW, -parkH); continue

      if (prevVisible) cursor += spacing                   // ★ 间隙只落在可见对之间
      if (child->GetStretch() > 0):                        // D30-H
          ++stretchSeen
          axis = (stretchSeen == visibleStretchCount) ? remaining - allocated
                                                      : remaining * stretch / totalStretch
          allocated += axis
          child->SetSize(fillCrossAxis ? cross : child->GetWidth(), axis)      // V
      else if (fillCrossAxis):
          child->SetSize(cross, child->GetHeight())        // 跨轴强制填充（既有 F2）
      child->SetPosition(padding, cursor)
      cursor += child->GetHeight()
      prevVisible = true
```

- **全可见逐位等价证明**：`visibleCount == count` ⇒ `gapCount` 同值、求和集合同、`cursor` 累进与旧「加 spacing 于尾」逐点等价（首子前无 spacing、末子后无 spacing）、停泊分支不可达。（C30-5；T30-4 手算锚。）
- **跨轴注意**：隐藏子**不参与 fillCrossAxis**（不 SetSize）——旧行为会给隐藏子也刷跨轴尺寸，新行为保持其当前尺寸（唯一变化点，仅隐藏子）。
- **HorizontalLayout 明确映射**（评审修正①——照做无需再推导）：`axis = 宽`（第一遍 fixedTotal 取 `GetWidth()`；stretch 分支 `SetSize(axis, fillCrossAxis ? cross : child->GetHeight())`）；`cross = 高`；`cursor` 作用于 **X**、`SetPosition(cursor, padding)`（**padding 作用于 Y**——跨轴坐标 = padding 的既有契约不变）；跨轴 fill 分支 `SetSize(child->GetWidth(), cross)`。其余（gapCount/remaining/prevVisible/停泊）逐字同构。

### D30-B′ CalculateTextPosition 精确落码（Button.cpp:92-99 替换）

```cpp
Point Button::CalculateTextPosition(int x, int y, float textWidth, float lineHeight) const{
	float offsetX = 0.0f;                                            // Left：内容区 = 全控件矩形（B4：无 padding 概念）
	switch (m_style.textAlignment.value){
		case TextAlignment::Right:  offsetX = static_cast<float>(GetWidth()) - textWidth; break;
		case TextAlignment::Center: offsetX = (static_cast<float>(GetWidth()) - textWidth) / 2.0f; break;
		case TextAlignment::Left:   break;                            // 缺省分支 = 显式 Left（防告警 + 防新枚举值漏处理）
	}
	const float offsetY = (static_cast<float>(GetHeight()) - lineHeight) / 2.0f;   // P7 垂直居中不动
	return Point{ static_cast<float>(x) + offsetX, static_cast<float>(y) + offsetY };
}
```

- **默认逐位**：主题注入 Center（D30-F/C30-3）⇒ 默认路径算式与旧码相同 ⇒ 零行为变化。
- `textWidth` 来源不变（`DrawTextContent` 的 `ctx.MeasureText`——B5）。

### D30-A′ TextAlignment.h 内容与 include 纪律

```cpp
﻿#pragma once

#include <cstdint>

namespace ECDI{

/// @brief 水平文本对齐（★ Phase 30 △1：纯值类型、零依赖——Button 立即消费，
///        Phase 29 O1（wrap 每行对齐，经 Line.width）未来复用同一枚举）
/// @details ★ 独立公共头的依赖方向论证（初设 D30-A）：塞 ButtonStyle.h 会造成
///          TextWidget → ButtonStyle.h → TextAlignment 的怪方向；独立头则并列依赖。
///          ⚠️ 缺省值陷阱：枚举零值 = Left ≠ 任何控件的「缺省对齐」——缺省语义一律由
///          主题显式注入（C30-3），禁止依赖 `TextAlignment{}` 零值。
enum class TextAlignment : std::uint8_t{
	Left   = 0,
	Center = 1,
	Right  = 2,
};

}
```

- include 纪律：**本头不被任何 Core 头反向 include**（叶子头）；`ButtonStyle.h` / 未来 `TextWidget` 直接依赖它。BOM 按 rule 70（MSVC 编译头必须 BOM——仓库既有公共头均带）。

### D30-F′ caretColor / textAlignment 接线四点（精确 diff 形态）

| 点 | caretColor（TextBox） | textAlignment（Button） |
|---|---|---|
| ① Style 声明 | `TextBoxStyle.h:16` caretWidth 邻位 +`StyleField<Color> caretColor;` | `ButtonStyle.h:17` hoverBackground 邻位 +`StyleField<TextAlignment> textAlignment;` |
| ② Override 声明 | `TextBoxStyleOverride` +`std::optional<Color> caretColor;` | `ButtonStyleOverride` +`std::optional<TextAlignment> textAlignment;` |
| ③ 主题注入 | `DefaultTheme.cpp:33` 邻位 +`s.caretColor.value = Color::Black();`（**零行为变化**） | `DefaultTheme.cpp:21` 邻位 +`s.textAlignment.value = TextAlignment::Center;`（**零行为变化**） |
| ④ Widget 接线 | `TextBox::ApplyTheme` :82 邻位 +`m_style.caretColor.Apply(defaults.caretColor.value);`；`SetStyle` :103 邻位 +`if (override.caretColor) m_style.caretColor.Set(*override.caretColor);` | `Button::ApplyTheme` :50 邻位同构；`SetStyle` :85 邻位同构 |
| ⑤ 消费 | `TextBox.cpp:1351` `Color::Black()` → `m_style.caretColor.value` | `CalculateTextPosition`（D30-B′） |

- **零新机制**：`StyleField` 状态机（B3）原样——`Set()` 标记 overridden、`Apply()` 只更新未 Override（D7）。T30-1/3 锁此语义。

---

## 2. 逐文件改动（△1–△8）

| △ | 文件 | 改动 | CMake |
|---|---|---|---|
| △1 | `include/ECDI/Core/TextAlignment.h` | **新建**（D30-A′ 全文）——公共头 94→95 | 0（纯头） |
| △2 | `include/ECDI/Theme/TextBoxStyle.h` | ①②（caretColor Style+Override） | 0 |
| △3 | `include/ECDI/Theme/ButtonStyle.h` | ①②（textAlignment Style+Override）+ include `Core/TextAlignment.h` | 0 |
| △4 | `src/Theme/DefaultTheme.cpp` | ③（两处默认值） | 0 |
| △5 | `src/Widget/TextBox.cpp` | ④⑤（接线 + 消费 :1351） | 0 |
| △6 | `src/Widget/Button.cpp` | ④⑤（接线 + CalculateTextPosition 三态化 D30-B′） | 0 |
| △7 | `src/Layout/VerticalLayout.cpp` + `HorizontalLayout.cpp` | D30-C′ 两遍算法（V/H 轴互换同构） | 0 |
| △8 | 测试 ×4 文件 | T30-1（TextBoxTests）/ T30-2、3（WidgetTests）/ T30-4、5、6（LayoutTests）/ T30-7（CollapsiblePanelTests） | 0 |

- `ButtonStyle.h` include `Core/TextAlignment.h` = 公共→公共 ✓；`DefaultTheme.cpp` / `Button.cpp` 需 include `Core/TextAlignment.h`（私有→公共 ✓）。

---

## 3. 契约（C30-1..7，沿初设 + C30-6 算术化补强）

- **C30-1** caretColor：默认 Black；Override 经 StyleField 既有状态机；ApplyTheme 只更新未 Override 字段——默认态逐位 = 现状。
- **C30-2** Button 三模式共用**全控件矩形**内容区（无 padding 概念——B4）；垂直恒居中（P7 不动）。
- **C30-3** 默认 `Center` 经主题**显式注入**（不依赖枚举零值——`TextAlignment{}` = Left 陷阱）。
- **C30-4** H/V C-VIS：可见子连续槽位、间隙只落可见对、求和排除隐藏、末位**可见** stretch 吃余数、隐藏子停泊 `(−w, −h)`（先取尺寸→停泊→continue）。
- **C30-5** **全可见 ⇒ 逐位不变**（D30-C′ 等价证明 + 专测 T30-4）。
- **C30-6** 全隐藏 ⇒ **不 panic 且算术不漂移**：乘法守卫 `gapCount = max(0, visibleCount−1)`（D30-G——防 `×(−1)` 反向增大）、全员停泊、remaining 照算不消费。
- **C30-7** 范围围栏：**不实现** Phase 29 O1；`TextWidget.cpp` 本 Phase diff = 0。

---

## 4. 盯防（6 条）

1. 停泊三步顺序（D30-I）：先取尺寸 → 停泊 → continue；**任何 SetSize 都不得插在取尺寸与停泊之间**。
2. `stretchSeen == visibleStretchCount`：比较对象必须是**可见**计数；新代码中禁止出现旧名 `stretchCount`（机检：diff grep）。
3. 间隙实现必须用 `prevVisible` 标志（before-add 形式）——禁止「数索引 / after-add 再回退」的等价变体（易错且不可读）。
4. C30-3 缺省值陷阱：任何 `TextAlignment{}` 零值 = Left；主题与字段初始化必须显式 Center。
5. O1 围栏机检：`TextWidget.cpp` / `TextWidget.h` 本 Phase diff = 0（C30-7）。
6. 默认态零变化三锚：caret Black / Button Center / 全可见布局——T30-1/2/4 各自显式断言（不靠存量）。

---

## 5. 用例（T30-1..7，349 → 356——精确断言与装置）

| # | 名（注册名） | 文件/装置 | 精确断言 |
|---|---|---|---|
| T30-1 | `TextBox.CaretColorStyle` | TextBoxTests；`TestableTextBox` +`using TextBox::OnFocusGained;`（`TextBox.cpp:212-214` 置 `m_showCaret=true`，窗口访问有判空 ⇒ 无窗口安全）；Paint 进 `PaintContext(CommandBuffer, RecordingBackend)`（ClipTests 形态） | ① 默认：`OnFocusGained` 后 Paint ⇒ caret `DrawRect` 命令的 color == `Color::Black()`（经 `std::get<DrawRectCommand>` 取）；② SetStyle(caretColor=红) ⇒ 色变红；③ 再 ApplyTheme ⇒ **仍红**（D7 Override 保持）；④ 宽 = `m_style.caretWidth.value`（同源回归锚） |
| T30-2 | `Button.TextAlignmentModes` | WidgetTests；RecordingBackend + PaintContext 直 Paint（TestButtonPaint 形态） | SetStyle 三态各 Paint：`DrawTextCommand.pos.x` = 手算（Left: x+0 / Center: x+(w−tw)/2 / Right: x+w−tw，tw = backend.MeasureText 实测宽）；**三态 pos.y 相同**（垂直恒居中）；**默认（不 SetStyle）== Center 现状** |
| T30-3 | `Button.TextAlignmentOverride` | WidgetTests 同装置 | override Right ⇒ ApplyTheme ⇒ **仍 Right**（D7）；override 后 `SetStyle({})` 空结构不重置（StyleField 语义）；无窗口下全路径不 panic |
| T30-4 | `Layout.HVAllVisibleBitIdentity` | LayoutTests（Panel + SetLayout + AddChild + `Arrange()` 既有形态） | **专测**：V/H 各两组——(a) 纯固定子；(b) 固定 + 多 stretch 混合（含 spacing/padding 非零）。期望值 = **手算旧公式**（spacing×(n−1) / stretch 截断 / 末位吃余数）逐项 `EXPECT_EQ(GetX/GetY/GetSize)`；再 `Arrange()` 一次断言幂等 |
| T30-5 | `Layout.VerticalCVis` | LayoutTests 同装置 | 五段：① [A 隐 B 可 C 隐 D 可] ⇒ B、D 连续槽位（间隙 1）、**A、C 停泊** `(−w,−h)`（`EXPECT_EQ(GetX(), -GetWidth())`）；② 隐藏 stretch 子不进权重（visibleStretch 只算可见）；③ 末位**可见** stretch 吃余数；④ 全隐藏 ⇒ Arrange 正常返回 + 全员 `(−w,−h)` + 无 panic；⑤ 全可见子树跑 T30-4 同组数据（本地复核） |
| T30-6 | `Layout.HorizontalCVis` | LayoutTests 同装置 | T30-5 的水平同构 + **fillCrossAxis 与隐藏子交互**：隐藏子跨轴尺寸保持现值（不被刷成 cross） |
| T30-7 | `CollapsiblePanel.CollapseVisibilityPin` | CollapsiblePanelTests（无窗口瞬时路径，既有 4 用例形态；**新增 include** PaintContext/RecordingBackend/Label） | ① 收起 ⇒ `GetContent()->IsVisible()` false + **Paint 全树零内容 DrawText**（label 进 GetContent()）；② `HitTest` 不命中（**既有断言 TestDownCollapse 已覆盖**——本用例不重复注册，引用其锚）；③ SetExpanded(true) ⇒ 内容恢复绘制；④ 双 Toggle 幂等（两轮收起⇄展开后状态与首轮一致）；⑤ **不改码声明**：本用例若红 = 现状回归，非新功能缺陷 |

---

## 6. 批次

| 批 | 内容 | 验收 |
|---|---|---|
| 批一 | △1–△6（caretColor + TextAlignment/Button）+ T30-1/2/3 | 五链全绿（MinGW/Clang/ClangCL Debug + MinGW/MSVC Release）+ 默认态零变化三锚之 T30-1/2 |
| 批二 | △7–△8（H/V C-VIS + 钉住）+ T30-4..7 + 收口 | 五链全绿 + C30-5 专测 + 五处台账 |

---

## 7. 影响面（详设定稿）

| 项 | 预算 |
|---|---|
| 公共头文件数量 | **94 → 95**（+`Core/TextAlignment.h`） |
| 公共 API | **+1 类型**（`TextAlignment`）**+ 4 样式字段**（caretColor / textAlignment × Style+Override——字段族口径，非类型爆炸） |
| 用例 | **349 → 356**（+7） |
| CMake | **0**（新头无 .cpp；GLOB 不受影响） |
| 风险 | **低**——三处默认态全「零变化」；唯一行为变化（H/V 隐藏子）有 Phase 25 契约背书 + C30-5 专测锚 |

---

## 8. 开放项（沿初设）

| # | 项 |
|---|---|
| O1 | Phase 29 O1（wrap 每行对齐）保持未实现——C30-7 围栏 |
| O2 | Button 文本 padding（无消费者证据；将来引入须三模式同步） |
| O3 | 垂直对齐（Top/Middle/Bottom——无消费者证据） |
| O4 | TextBox 颜色族补漏巡检（本 Phase 勘察确认 selection/composition/caret 全样式化、无第四处硬编码色） |

---

## 11. 实施回填（收口——2026-10-07）

### 11.1 批次执行表

| 批 | 内容 | 提交 | 验收实绩 |
|---|---|---|---|
| 批一 | △1–△6（TextAlignment.h 新建 + caretColor 接线五点 + textAlignment/Button 三态化）+ T30-1/2/3 | `4409e47` | 五链 **352/352**；默认态零变化（caret Black / Button Center）——**C30-1/C30-3 直接锚定**；★ T30-1 用真窗口探针（caret 绘制读 `GetWindow()->GetTextMeasurer()`——无窗口空解引用，**无窗口不可行**）+ caret 矩形按宽 2.0f 指纹过滤 |
| 批二 | △7–△8（H/V 两遍算法 C-VIS 化 + T30-4..7） | `bea44df` | 五链 **356/356**；C30-5 专测 + 停泊/fillCrossAxis 交互断言全过 |
| 收口 | 本节 + 五处台账 | （本提交） | A1–A4 全判 |

### 11.2 实施记录（三条——**均测试侧，生产零缺陷**）

| # | 记录 | 影响 |
|---|---|---|
| R1 | **T30-7 首版 label 无尺寸**：探针 Label 未 SetSize ⇒ 0×0 Widget 被 Paint 的可见性门控跳过（与 SetVisible 无关）⇒ 「展开后文本出现」恒失败 | ★ **教训：无尺寸 Widget 不可绘——可见性语义的钉住必须先给尺寸**；修正 = label SetSize(120,20) + 改为直接 Paint 内容子树（收起态整面板轴向 0 本就被零面积门控跳过——钉内容子树信号更强） |
| R2 | **T30-1 漏注册**：测试函数落地但注册行漏写 ⇒ 总数 351（预期 352）——**用例计数守恒检查抓到** | 教训：每次加用例后立即对账（registered 数 == 预算数） |
| R3 | H/V 改写采用 **before-add spacing**（`if (prevVisible) cursor += spacing`）而非旧「尾加」——全可见时逐点等价（首子前无、末子后无），部分隐藏时间隙自然只落可见对 | 比旧式更直读；C30-5 等价证明的落点 |

### 11.3 A1–A4 判定表（需求 §5 验收）

| 项 | 判据 | 结果 |
|---|---|---|
| **A1** | 存量 349 全绿（三处默认零变化） | ✅ **356/356 五链**（MinGW/Clang/ClangCL Debug + MinGW/MSVC Release[干净全量重建]）——caret 默认 Black（T30-1①）/ Button 默认 Center（T30-2①）/ 全可见布局逐位（T30-4） |
| **A2** | 行为断言（命令流 / 坐标级） | ✅ T30-1（caret DrawRect 色 = 字段值，宽 2.0f 指纹）/ T30-2（三态 pos.x 手算 0/45/90 + y 恒 13）/ T30-5/6（停泊坐标 = (−w,−h)、连续槽位、间隙数） |
| **A3** | H/V 与 ListLayout 的 C-VIS 结果契约一致 | ✅ T30-5/6（跳过+停泊+连续槽位+间隙可见对）——C-VIS-1/4 同语义；C-VIS-2 经既有负向钳 0 机制（零新代码）；C-VIS-3 既有入口门控 |
| **A4** | 确定性 | ✅ 全部字面常量/整数算术；无 locale/NLS/时间依赖；T30-4 幂等复验 + T30-7 双 Toggle 幂等 |

### 11.4 收口核对（全链）

- 公共头 **94 → 95**（+`Core/TextAlignment.h`，叶子头）；公共 API **+1 类型 +4 样式字段**（字段族口径）；**CMake 0**；用例 **349 → 356**（+7）。
- **批次流水**：需求 v1.0（`27b3ec4`）→ v1.1 评审 PASS → 初设 v1.0（`0bf1164`）→ 评审 PASS → 详设 v1.0（`a5962c9`）→ v1.1 评审 PASS（`82a6643`）→ 批一（`4409e47`）→ 批二（`bea44df`）→ 收口（本提交）。**四轮外部评审全 PASS、零返修循环**。
- **遗留（不阻塞）**：O1 = Phase 29 O1（wrap 每行对齐，`TextAlignment` 已就位待消费）；O2 Button padding；O3 垂直对齐；O4 TextBox 颜色族巡检结论 = 无第四处硬编码色。harness 侧验证：其评估清单 ①②③ 对应项已可复跑核对（④ 已勘误为其仿制模型问题）。

---

## 9. 修订记录

- **v1.2**（2026-10-07）**实施回填 + ✅ 全链收口**（两批落地——五链 356/356，A1–A4 全判）。★ **批次流水**：批一 `4409e47`（△1–△6 + T30-1/2/3——caretColor 接线五点 + Button 三态化 + 真窗口 caret 探针）· 批二 `bea44df`（△7–△8 + T30-4..7——H/V 两遍算法 C-VIS 化 + 全可见专测 + CollapsiblePanel 钉住）。★ **实施记录三条（均测试侧、生产零缺陷）**：R1 T30-7 label 无尺寸（0×0 Widget 被 Paint 门控跳过——**可见性钉住必须先给尺寸**）· R2 T30-1 漏注册（计数守恒检查抓到）· R3 before-add spacing（全可见逐点等价）。★ A1–A4 全判（§11.3）。用例 349 → **356**（+7）。
- **v1.1**（2026-10-07）**评审吸收（PASS → Implementation）**。**评审总判**：「比较明确的 PASS，非 Conditional」——14 项分表全 ✅（remaining 负值/全隐藏 ×(−1)/stretch 余数/停泊顺序/H·V 兼容性[有证明 + 专测]/Button 对齐/TextAlignment{} 陷阱/caretColor/O1 围栏/CollapsiblePanel 回归/测试装置/CMake 影响面全过）；**评审亮点认可**：C30-5「不是单纯依赖测试而是有结构等价依据」·「外部报告 → 查版本 → 修或不修」的纪律 · T30-7「红了是现状回归不是新功能缺陷」的语义 · 影响面字段族口径诚实。**两处纯文档修正（当场修毕）**：① D30-C′ 补 **HorizontalLayout 明确 X/Y 映射**（axis=宽/cross=高/cursor 作用 X/padding 作用 Y——照做无需再推导）；② T30-5 「A/D… 停泊」笔误 → **A、C 停泊**。**评审明示「不要再改设计了」⇒ 直接进 Batch 1**（批一 = ①②+T30-1/2/3 → 批二 = ③④+T30-4..7+收口）。
- **v1.0**（2026-10-07）初稿。**输入**：初设 v1.0（评审 **PASS → Detailed Design，无 blocker**）+ 评审 3 个非 blocker 确认点 + 详设展开清单 + 补充勘察（remaining 既有钳制 = `(std::max)(0,…)` F4 冻结点 `VerticalLayout.cpp:38-39`；`OnFocusGained` 无窗口安全 `:212-214`；`HitTest` 公开 `Widget.h:258` 且命中断言已存在于 `TestDownCollapse`）。**D30-G/H/I 逐题钉死**：**G** remaining 负值沿用既有 F4 钳制不创造新规则 + **详设期新识别算术边界 = `visibleCount == 0` 时 `spacing×(−1)` 反向增大** ⇒ 乘法守卫 `gapCount = max(0, visibleCount−1)`（C30-6 算术化）；**H** 末位可见 stretch 伪代码（比较对象 = visibleStretchCount，旧名禁现）；**I** 停泊三步机械顺序（先取尺寸→停泊→continue）。**展开清单全落实**：两遍算法全文（V/H 同构）· CalculateTextPosition 精确落码 · TextAlignment.h 全文 + include 纪律 · 接线五点表格化 · T30-1..7 精确断言与装置（含 T30-7 引用既有命中锚不重复注册）。△1–△8 · C30-1..7 · 盯防 6 · 用例 7 条（349→**356**）· 两批 · 94→95 / API +1 类型 +4 字段 / CMake 0 / 风险 低。
