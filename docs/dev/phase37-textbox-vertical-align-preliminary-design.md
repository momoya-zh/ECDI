# Phase 37 · TextBox 垂直对齐 —— 初步设计

> 状态：**v1.1**（2026-10-26 外部评审「**修改后通过**」吸收——口径收紧 5 项，**方案未变**；v1.0 同日落盘）｜上游 = 需求稿 `phase37-textbox-vertical-align-requirements.md` **v1.1**（外部评审「修改后通过」⇒ 可移交初设）｜台账 = `roadmap-deferred.md` **#76**（v2.48）
> ★ **v1.1 评审吸收摘要（5 项口径收紧，均不改总体方案）**：① **超屏语义**——「与改前逐位相同」**只指绘制原点**，视口外裁切是既有 clip 语义、非本 Phase 保证；超屏不要求整体居中，`EnsureCaretVisible` 与滚动保持现状 · ② **T37-1 基准前置**——源码基准 commit `5adcbedb`（本日核实），**禁止先改源码再回头生成「改前」常量** · ③ **新增 §1.6 `Center` × 滚动组合语义**（`O` 与 `scrollOffsetY` **数学互斥** + 三态表 + 契约 C37-7）· ④ **新增 §1.7 坐标口径表**（四个量逐一定系，T37-4/5/6 取值规则写死）· ⑤ **§1.2 / R37-4 `A < 0` 结论限定**——`O = 0` 只证几何原点不变，负高 clip 行为**未覆盖未验证**，明确挂账。
> ★ **本稿任务**：逐条收口需求 §7 闸门五项，并给出**可实施的逐文件落点**。沿 Phase 31 纪律：**§2 定案须给论证，倾向不等于定案**。
> ★ **事实纪律**：所有行号 = 2026-10-26 对 HEAD 实测；`L = 16`（SimSun@14）为**引用**（Phase 31 双链读数 16.00 + harness `--grow-probe`），**ECDI 侧本 Phase 未重跑**——故 §1.4 的构造一律写成**参数化式**，实测值留实施期填。

---

## §1 初设必答（需求 §7 闸门五项逐题收口）

### 1.1 问一 = 三消费点清单 + `GetTextTopInset()` 全部调用点

**`GetTextLeftInset()` 现有全部 5 个调用点，逐个判归属**（`grep` 全量，`TextBox.cpp`）：

| 行 | 所在函数 | 用途 | 改后 |
|---|---|---|---|
| `:676`（第 1 个） | `CalculateCaretPosition` | 光标 **X**：`GetTextLeftInset() + caretX` | **不变**（X 语义） |
| `:676`（第 2 个） | 同上 | 光标 **Y**：`GetTextLeftInset() + caretY` | ★ 改 **`GetTextTopInset() + caretY`** |
| `:753` | `CaretIndexFromPosition` | **Y 定行**：`(localPos.y − GetTextLeftInset() + scrollY) / L` | ★ 改 **`GetTextTopInset()`** |
| `:757` | 同上 | `innerX = localPos.x − GetTextLeftInset() + scrollX` | **不变**（X 语义） |
| `:798` | `ResetPreferredColumn` | `m_preferredColumn = ...x − GetTextLeftInset() + scrollX` | **不变**（X 语义） |
| `:1336` | `OnPaint` | `inset` **一个变量同时喂 X 与 Y** | ★ **拆成 `insetX` / `insetY` 两个局部量** |

⇒ **新入口 `GetTextTopInset()` 的调用点 = 恰好 3 处**（`:676` Y / `:753` / `:1338`），与需求 §1.3 N1 完全吻合，**无第四处**。

**为什么 `OnPaint` 内只改一个变量就够（`:1356` 下游全跟随）**：`lineY = textY + li×L − scrollY` 是 `OnPaint` 内**唯一几何基**，三处视觉全部经它——① Selection 高亮 `Rect{viewX + hlMin, lineY, ...}` · ② 行文本 `DrawText(Point{viewX, lineY})` · ③ IME 组合下划线 `lineY + L − 1`。⇒ **改 `textY` 一处，三处自动跟随**，不存在遗漏点。

★ **反例排除（`GetTextAreaWidth()` 的既有让位逻辑）**：`GetTextAreaWidth()` 会因垂直条可见而减去条厚（`:648-650`）——那是 **X** 方向，与本 Phase 正交，不动。

### 1.2 问二 = clip 覆盖性不等式推导（`offset > 0` 时不裁字）

**记号**：`p` = padding · `H` = 控件高 · `A = H − 2p`（文本区高）· `n` = 行数（`m_lineStarts.size()`）· `L` = 行高 · `C = nL` · `O = max(0, (A − C)/2)`。

**clip 取固定文本区**（D37-4 定案 a）：y 区间 = `[fy + p, fy + p + A]`。
**内容绘制区间**：`[fy + p + O, fy + p + O + C]`。

覆盖条件 = 上界 `O ≥ 0` **且** 下界 `O + C ≤ A`：

| 情形 | 结论 |
|---|---|
| **C ≤ A**（内容不足一屏） | `O = (A−C)/2` ⇒ `O + C = (A+C)/2`，由 `C ≤ A` 得 `(A+C)/2 ≤ A` ✓ **完整覆盖**。★ `C = A` 时 `O = 0`、`O + C = A` 取等号——内容底恰在 clip 底，**与改前同源**（改前 `p` 时也可能恰等） |
| **C > A**（超屏） | `O = 0` ⇒ `O + C = C > A` ✗ 下界不成立——但这是**既有语义**：`PushClip` 的本职就是裁掉视口外的行。★★ **v1.1 口径收紧**：此处「与改前逐位相同」**只指绘制原点**（`lineY` 基 = `fy + p`），**不**指整体视觉相同；视口外内容被裁切是**既有 clip 语义，不是本 Phase 的新保证**。⇒ **超屏不要求整体居中**：`EnsureCaretVisible`（`:529-533`）与滚动行为（`GetMaxScrollOffset` / `m_scrollOffsetY` / `SyncScrollBar`）**全部保持现状**，本 Phase 一行不改（A3 判据亦按此口径改写） |
| **A < 0**（`p×2 > H`，`padding` 过大） | `A − C < 0` ⇒ `O = max(0, 负数) = 0` ✓ **本方案不改变该路径的几何原点**（`lineY` 基仍 = `fy + p`）。★★ **v1.1 结论范围订正**：`O = 0` **只**证明偏移在此路径不介入，**不能**据此推断「负高度 clip 的完整行为安全」——负高 `PushClip` 的行为属**既有 M11 路径**（`GLRenderer.cpp:907` 注释自述可达），**本 Phase 未覆盖、未验证**，见 §7 R37-4（明确残余风险条目） |

⇒ **结论（v1.1 限定适用范围）：在 `C ≤ A` 的适用域内，`O` 不引入任何新的裁剪面**——因为 `O` 含 `max(0, ·)`，在「内容装不下」时**恰好归零**，居中的适用域与裁切的失效域是同一条件的两侧、天然互斥。★★ **本结论不外推到三处**：① 超屏期的裁切语义（**既有** clip 行为，非本 Phase 保证）② 负高文本区（**既有 M11 路径**，未覆盖未验证）③ 滚动范围与 `EnsureCaretVisible`（**本 Phase 一行不改**）。

### 1.3 问三 = C30-3 同型风险评估

**C30-3 的实质**（`ButtonStyle.h:32` 原文）：「缺省 **Center** = Phase 30 前行为逐位一致——⚠️ **不得依赖枚举零值 Left**，缺省必须显式注入」。⇒ 该纪律触发条件是 **「主题缺省值 ≠ 枚举零值」**：Button 缺省 `Center = 1` 而零值 = `Left = 0`，若依赖零值则缺省变成 Left，与历史行为（居中）相反 ⇒ 必须显式注入。

**对 `verticalAlign` 的判定**：缺省 = `Top` = 枚举零值 `0` ⇒ **零值与缺省一致** ⇒ **不构成同型风险**。双保险：

| 层 | 保证 |
|---|---|
| ① 枚举 | `Top = 0`（`VerticalAlignment`） |
| ② `StyleField<T>::value{}` | **值初始化**（`StyleField.h:10` `T value{};`）⇒ `VerticalAlignment{}` = `Top` |

⇒ 即使 `DefaultTheme` 漏注入、`SetStyle` 从未被调用，字段仍为 `Top`（逐位不变）。**本 Phase 仍会显式注入**（与既有 10 字段同构，`DefaultTheme.cpp:31-52` 逐字段赋值风格），但不把它当正确性依赖。

★★ **前瞻契约（必须写进 `VerticalAlignment` 头注释）**：若将来翻案为 ⓐ（把缺省改为 `Center`），**C30-3 立即触发**——那时必须同时(a)改 `DefaultTheme` 注入、(b)确认既有调用点未依赖零值。⇒ 本 Phase 的 `Top = 0` 设计是"翻案友好"的，但翻案本身不得只改缺省值了事。

### 1.4 问四 = A4 构造的可测性复核

**① 装置事实（决定 A4-1 能不能自动化）**：`GetCaretClientGeometry()` **无条件解引用 `GetWindow()`**（`:682` `GetWindow()->GetTextMeasurer()`）⇒ **无窗口会空解引用**。故 A4-1 **必须真窗口**，不能走无头路径。

**② 现成装置（不必新造）**：`TextBoxTests.cpp:1598-1640` 的 `CaretProbe`（T30-1 引入）已经具备全部要素——`Application app` + `app.Create(..., std::move(services))` 真窗口 · `window->GetRootWidget().AddChild(...)` · `PaintContext ctx(commands, window->GetTextMeasurer())` + `box->Paint(ctx, x, y)` 取命令 · `OnFocusGained()` 使 caret 可见。⇒ **扩展该装置**（加 `SetStyle(Center)` + 取 `DrawTextCommand.pos.y`），不新造第二套。

**③ 坐标系对齐（否则 `==` 断言无意义）**：`GetCaretClientGeometry()` 返回 `abs + local`（`abs = GetAbsolutePosition()`，客户区绝对坐标）；`recorded` 命令坐标则取决于 `Paint(ctx, x, y)` 传入的 x/y。⇒ **扩展装置必须传 `abs.x, abs.y`**（而非 T30-1 原样的 `0, 0`），使两侧同为**客户区绝对坐标**，`A4-1` 才能写成直接相等。

**④ `L` 的取值**：`L = 16.00`（SimSun@14）来自 Phase 31 双链读数（README「harness REQ-05 已消除：SimSun@14 GDI 16.00 / FT 16.00，差 0.00」）+ harness `--grow-probe` baseline。★ **ECDI 侧本 Phase 未重跑** ⇒ §4 用例一律**运行时读 `GetLineHeight()` 构造点击坐标**，不硬编码 16 或 22。

**⑤ 判别力自证（A4-2 与 A4-3 的关系）**——
- A4-2（单配置）：`Center` 下点 `y = p + O + L/2`，期望命中**行 0**。若命中未跟随 ⇒ 得**行 1**。
- A4-2 **可能假通过**：若坐标不幸落在非判别窗口内，两种实现都得同一行。
- ⇒ A4-3 的作用 = **证明该坐标确实在窗口内**：加跑 `Top` 配置，同坐标必须得**行 1**。两条一起才排除了「坐标选错」。
- **窗口参数化**：`O ≥ L/2` 为前置（保证 `Top` 侧 `floor((O+L/2)/L) ≥ 1`）。

**⑥ 兜底**：若实施期实测发现 `O ≥ L/2` 的夹具在该控件的滚动/边框状态下不可稳定构造，则 A4-2/A4-3 降级为「A4-1 光标同源」单条 + **详设登记降级理由**（需求 §5 兜底条款已授权此路径）。

### 1.5 问五 = 勿重开 ✓

水平对齐 · 圆角内缩联动 · `TextBox.h:79` 契约改写 · 无条件居中（ⓐ）· `GetPadding()` 观测缝——**五项全部维持需求 §3 非目标**，本稿未夹带任何一项。★ 特别声明：`GetTextTopInset()` 是 `protected`（不扩公开面），**不**顺带提供公开的 `GetPadding()`。

### 1.6 问六（v1.1 新增，评审要求）= `Center` 与滚动的组合语义

**★ 核心事实：`O > 0` 与 `scrollOffsetY > 0` 在数学上互斥，不可能同时成立。**

`GetMaxScrollOffset() = max(0, C − A)`（`:505-511`）与 `O = max(0, (A − C)/2)` 互为 0：

- `C ≤ A` ⇒ `O = (A−C)/2 ≥ 0`，同时 `maxScrollOffset = 0` ⇒ `scrollOffsetY` **恒 0**
- `C > A` ⇒ `O = 0`，同时 `maxScrollOffset = C − A > 0` ⇒ 可滚动

⇒ 不存在「`O` 与滚动偏移同时非零」的状态，实现上**不可能**产生双重修正——但契约仍须显式禁止，防将来改动打破该不变量。

**契约 C37-7（v1.1 新增）**：`O` **只**进入三个坐标消费点（绘制 `textY` / 光标 `caretY` / 命中定行）；**禁止**进入——`GetMaxScrollOffset()` 的 `contentH`（`:509`）· `GetTextAreaHeight()`（`:500-503`）· `EnsureCaretVisible()` 上/下界（`:529-533`）· `SyncScrollBar()` 的 range / extent（`:594-595`）。

**三态行为表**（覆盖评审点名的三组）：

| 状态 | `C` vs `A` | `O` | `maxScroll` | `scrollOffsetY` | 绘制原点 | 滚动行为 |
|---|---|---|---|---|---|---|
| 不足一屏 | `C < A` | `> 0` | `0` | 恒 `0` | `fy + p + O` | 无滚动（**不变**） |
| 恰好一屏 | `C = A` | `0` | `0` | 恒 `0` | `fy + p` | 无滚动（**不变**） |
| 超屏 | `C > A` | `0` | `C − A` | 可非零 | `fy + p` | **与改前逐位同**（不变） |

★ `C = A` 是 `O` 的**上边界情形**：`O = 0` 且内容底恰在 clip 底（§1.2 的 `O + C ≤ A` 取等号）。

### 1.7 问七（v1.1 新增，评审要求）= 坐标口径（T37-4 / 5 / 6 的断言前提）

评审指出「同坐标跨 `Top` / `Center` 对比」若坐标系不统一，比较的就不是同一位置。**本日实测四个量的口径**：

| 量 | 坐标系 | 证据（本日 `grep` 亲核） |
|---|---|---|
| `CaretIndexFromPosition(Point localPos)` **入参** | **控件局部**（相对控件左上角） | 三个调用点**全部先换算**：`:1109` / `:1123` / `:1141` 均为 `Point{ mouseX − abs.x, mouseY − abs.y }` |
| `CalculateCaretPosition()` **返回** | **控件局部** | `:676` `Point{ GetTextLeftInset() + caretX, ... }`，不含 `abs` |
| `GetCaretClientGeometry()` **返回** | **客户区绝对** | `:681-690` `Rect{ abs.x + local.x, abs.y + local.y, ... }` |
| `DrawTextCommand.pos` | **= `Paint(ctx, x, y)` 传入的 x / y**（命令层不另加偏移） | `Paint` 内 `fx = x + 局部偏移`：传 `abs` ⇒ 客户区绝对；传 `0` ⇒ 控件局部 |
| 鼠标事件 `GetMouseX / Y` | **客户区绝对** | 调用点减 `abs` 转局部（同上三处） |

⇒ **测试取值规则（写死，避免详设临场发挥）**：

- **T37-4**（光标 vs 绘制）：两侧必须同系 ⇒ `Paint` 传 `abs.x, abs.y`（**不是** T30-1 原样的 `0, 0`），断言 `GetCaretClientGeometry().rect.y == DrawTextCommand.pos.y`。
- **T37-5 / T37-6**（命中）：直接传**控件局部** `Point{ 1.0f, p + O + L/2 }`，**不做任何换算**——`CaretIndexFromPosition` 本就收局部坐标。★ 评审担心的「不是同一位置」在此口径下自动消解：两配置传**同一个局部 y**、且控件绝对位置不变 ⇒ 物理上就是同一点。

---

## §2 决策定案 D37-A..F（对应需求 D37-1..6）

| # | 定案 | 论证 |
|---|---|---|
| **D37-A** | **新枚举 `Core/VerticalAlignment.h`**：`enum class VerticalAlignment : std::uint8_t { Top = 0, Center = 1 }`。**不加 `Bottom`** | 与 `Core/TextAlignment.h` 同目录同形态（对称）；`Top = 0` 使 §1.3 的双保险成立；`Bottom` 无消费者（YAGNI，且枚举加值非破坏性变更，将来可加） |
| **D37-B** | **`GetTextTopInset()` 返回 `padding + O`**（完整绘制原点 Y），**不是**只返回 `O` | 三处消费点要的都是「相对控件原点的绘制原点 Y」，与 `GetTextLeftInset()` 语义对称 ⇒ 调用点替换是 **1:1**（把名字换掉、其余算式不动），最不容易漏改；若只返回 `O`，每个调用点都要自己 `+ padding`，等于把耦合重新散开 |
| **D37-C** | **`Top` 快路径直接 `return padding`，不读行数** | ★ 本 Phase 最关键的决定：使 A1「逐位不变」**有结构性保证**（不是"算出来恰好相等"），同时让 D37-5 的时序风险在默认路径上**归零**——快路径根本不碰 `m_lineStarts` |
| **D37-D** | **clip 固定用 `fy + insetX`**（= `fy + padding`），不随 `O` 动 | §1.2 已证不裁字；语义上 clip 就该是「文本区」而非「内容区」；且超屏时 `O = 0` ⇒ 与改前逐位同 |
| **D37-E** | **`Center` 路径内自查行缓存**：`if (m_needsLineRecalc) const_cast<TextBox*>(this)->RecalculateLines();` | 与 `:657-658` **同一手法、有先例**，不引入 `mutable`（(c) 被否）；使新入口对任何调用顺序都安全。★ 但 §1.1 已证 3 个调用点全部在重算之后 ⇒ 自查是**兜底而非主路径**，正常运行时永不触发 |
| **D37-F** | **缺省 `Top` 且显式注入 `DefaultTheme`**，同时把「翻案即触发 C30-3」写进 `VerticalAlignment.h` 注释 | §1.3 |

---

## §3 方案 PD37-1..6（逐文件落点）

**PD37-1 · 新增 `ECDI/include/ECDI/Core/VerticalAlignment.h`**（公共头 99 → 100）

```cpp
#pragma once
#include <cstdint>
namespace ECDI{
/// @brief 文本垂直对齐（TextBox 内容不足一屏时的行箱起始位置）
/// @note 缺省 Top = 枚举零值 ⇒ 依赖零值不会改变任何既有行为（与 ButtonStyle::textAlignment 的 C30-3 情形**不同**：
///       那里缺省 Center ≠ 零值 Left，故必须显式注入）。
/// ★ 前瞻契约：若将来把主题缺省改为 Center，**C30-3 立即触发** —— 必须同时改 DefaultTheme 注入，
///   不得只改缺省值了事。
enum class VerticalAlignment : std::uint8_t{
	Top    = 0,   ///< 顶对齐（缺省 = Phase 37 前行为，逐位不变）
	Center = 1,   ///< 垂直居中（仅在内容高 ≤ 文本区高时生效；超屏自动退化为 Top）
};
}
```

**PD37-2 · `TextBoxStyle.h`** —— 结构体 + override 各加 1 字段（**声明位置 = 末尾**，避开 C7560 designated-initializer 顺序风险，同 Phase 36 D36-5 教训）：

```cpp
	StyleField<VerticalAlignment> verticalAlign;   ///< ★ Phase 37：垂直对齐（缺省 Top = 逐位不变；Center 仅在内容不足一屏时生效）
// TextBoxStyleOverride 内：
	std::optional<VerticalAlignment> verticalAlign;
```

**PD37-3 · `DefaultTheme.cpp`**（`GetTextBoxStyle` 末尾）—— 显式注入（§1.3 双保险的第二层）：

```cpp
	s.verticalAlign.value = VerticalAlignment::Top;   // ★ 与枚举零值同值——显式注入是纪律，不是正确性依赖
```

**PD37-4 · `TextBox.h`**（protected 区，与 `GetTextLeftInset` 相邻）：

```cpp
	/// @brief 文本绘制原点的 Y（相对控件原点）= padding + 居中外缩量。
	/// 与 GetTextLeftInset() 成对：**X 恒用 Left**（= padding），**Y 恒用本函数**（含居中）。
	/// ★ 不变量（A1 依赖）：verticalAlign == Top 时本函数 **逐位等于** GetTextLeftInset()——快路径直接返回 padding，
	///   不读行数（故无时序依赖、无浮点误差）。
	/// ★ 语义边界：Center 仅在「内容高 ≤ 文本区高」时产生非零偏移；超屏时本函数 == padding（与改前逐位同）。
	float GetTextTopInset() const;
```

**PD37-5 · `TextBox.cpp`** —— 四处改动（`GetTextTopInset` 定义 + OnPaint 拆分 + 光标 + 命中）：

```cpp
// ① 新函数（紧邻 GetTextLeftInset，:498 之后）
float TextBox::GetTextTopInset() const{
	// ★ Top 快路径：不碰 m_lineStarts ⇒ A1 逐位不变有结构性保证，且无行数时序依赖（D37-C）
	if (m_style.verticalAlign.value == VerticalAlignment::Top)
		return m_style.padding.value;
	// Center：行缓存自查（与 :657-658 同手法——兜底；正常路径调用方已在重算之后）
	if (m_needsLineRecalc)
		const_cast<TextBox*>(this)->RecalculateLines();
	const float lineH = GetLineHeight();
	const float contentH = static_cast<float>(m_lineStarts.size()) * lineH;
	const float offset = (std::max)(0.0f, (GetTextAreaHeight() - contentH) / 2.0f);
	return m_style.padding.value + offset;
}

// ② OnPaint（:1336-1343）
const float insetX = GetTextLeftInset();   // X：恒 padding（textX / viewX / clip 用）
const float insetY = GetTextTopInset();    // Y：含居中（textY / lineY 用）
const float textX = fx + insetX;
const float textY = fy + insetY;
const float areaTop = fy + insetX;         // ★ clip 的 y = 文本区顶，不随 O 动（D37-D）
ctx.PushClip(Rect{ textX, areaTop, GetTextAreaWidth(), GetTextAreaHeight() });

// ③ CalculateCaretPosition（:676）
return Point{ GetTextLeftInset() + caretX, GetTextTopInset() + caretY };

// ④ CaretIndexFromPosition（:753）
const int logicalLine = (std::max)(0,
	static_cast<int>((localPos.y - GetTextTopInset() + m_scrollOffsetY) / lineH));
```

**PD37-6 · `TextBox::SetStyle`**（`:99` 起）—— 追加一行，与既有 10 条同构：

```cpp
if (override.verticalAlign) m_style.verticalAlign.Set(*override.verticalAlign);
```

★ **零改动面（明确列出，防误伤）**：`GetPreferredSize` / `MultilinePreferred` / `GetMaxScrollOffset` / `EnsureCaretVisible` / `GetTextAreaWidth` / `LayoutScrollBar` / `RecalculateLines` / `TextWidget::CalculateTextPosition`（Button 的居中路径**不碰**）。

---

## §4 测试 T37-1..8（505 → 513，+8）

| # | 用例 | 验收 | 手法 |
|---|---|---|---|
| **T37-1** | `Top` 逐位基准（3 组 `(H,p,n)`） | A1 | `RecordingBackend` 取 `DrawTextCommand.pos.y`，断言**常量表**。★★ **基准采集 = 实施前置（v1.1，评审要求）**：源码基准 = **`5adcbedbc62f0432b6e756b8471d26b4d781fb10`**（本日 `git log -1` 核实，= 文档重组提交；**提交文档本身不改变源码基准**）；采集必须在**动 `TextBox` 之前**完成，并记录来源 commit / 本机 DPI（120）/ 字体族 + `L` 实测值。★ **禁止先改 `TextBox` 再回头生成「改前」常量**——那样基准被污染，A1 退化为同义反复 |
| **T37-2** | `Center` 生效（3 组正交，含 `(29,6,1)` 巧合组 `O` 必须为 0） | A2 | 同装置，断言 `pos.y == fy + p + O`（`O` 由运行时 `GetLineHeight()` 算） |
| **T37-3** | 三态原点（v1.1 扩为 `C = A` / `C > A` / `scrollOffsetY ≠ 0`） | A3 | ① **`C = A`**（取 `H = 2p + nL`，如 `p=4 / n=2 / L=16 ⇒ H=40`）⇒ `O = 0`，`pos.y == fy + p`；② **`C > A` 且 `Center`** ⇒ `pos.y` 与 `Top` 配置**逐位同**（★ 只断言**绘制原点**，不断言整体视觉——§1.2 v1.1 口径）；③ **`C > A` + `scrollOffsetY > 0`** ⇒ 原点仍 `fy + p`，且 `GetMaxScrollOffset()` 与改前**同值**（验证 `O` 未混入滚动范围，C37-7） |
| **T37-4** | 光标同源 | A4-1 | **真窗口**（扩展 `CaretProbe`）：`GetCaretClientGeometry().rect.y == 首行 DrawTextCommand.pos.y`（`caret = 0` / `scroll = 0` / Paint 传 `abs`） |
| **T37-5** | 命中跟随绘制（单配置） | A4-2 | `Center` + `y = p + O + L/2` ⇒ 命中行 0（按码点归属断言） |
| **T37-6** | 跨配置对比（判别力证明） | A4-3 | 同坐标 + `Top` ⇒ 命中行 1；与 T37-5 合起来排除「坐标选错」 |
| **T37-7** | 主题缺省 | A5 | 新控件 `m_style` 取 `Top`；`ApplyTheme` 后未覆盖字段 = `Top` |
| **T37-8** | `SetStyle` 覆盖 | A5 | `override.verticalAlign = Center` 生效；`ApplyTheme` **不覆盖** override（D7 契约，`StyleField::Apply` 语义） |

★ 与需求稿 §6 的 **+6 → +8** 差异说明：需求估 A4 ×2，初设拆为 T37-4/5/6 三条（A4-1 独立 + A4-2/A4-3 成对）；A5 拆为 T37-7/8 两条（缺省 / 覆盖，`StyleField` 状态机两半各一条）。⇒ **用例数上修是判据细化的结果，不是范围膨胀**。

---

## §5 契约 C37-1..6

| # | 条款 |
|---|---|
| **C37-1** | **`GetTextTopInset()` = `padding + max(0, (A − C)/2)`**（`A = H − 2p`、`C = nL`），语义 = 文本绘制原点 Y（相对控件原点）。★ **不变量 1**：`verticalAlign == Top` ⇒ **恒等于** `GetTextLeftInset()`（快路径，不读行数）。★ **不变量 2**：`C > A` ⇒ **恒等于** `padding`（超屏退化） |
| **C37-2** | **三消费点同源**：绘制（`lineY` 基 `textY`）· 光标（`CalculateCaretPosition().y`）· 命中（`CaretIndexFromPosition` 定行）**必须全部经 `GetTextTopInset()`**。`lineY` 是 `OnPaint` 内唯一几何基，Selection / 文本 / 下划线经它自动跟随 |
| **C37-3** | **clip = 文本区**：`PushClip` 的 y 恒为 `fy + padding`，**不随居中外缩量变化**；宽高仍走 `GetTextAreaWidth/Height()` |
| **C37-4** | **缺省 `Top` ⇒ 逐位不变**：公共样式面唯一增量 = `TextBoxStyle::verticalAlign`；不设该字段的全部既有调用方渲染、光标、命中、滚动**一律不变** |
| **C37-5** | **居中不参与测量**：`GetPreferredSize` / `MultilinePreferred` / `MeasureText` / `LineHeight` / `GetMaxScrollOffset` 全部不变 ⇒ 无缓存键、无指纹扩展面 |
| **C37-6** | **X / Y 各自单一来源**：劈开后 `GetTextLeftInset()` 只服务 X（`textX` / `innerX` / `preferredColumn`），`GetTextTopInset()` 只服务 Y。★ **禁止**任一函数内部引用另一个（否则耦合复活，N1 根因重现） |
| **C37-7** | **`O` 的消费面封闭（v1.1 新增，评审要求）**：`O` **只**进三处坐标（绘制 `textY` / 光标 `caretY` / 命中定行）；**禁止**进 `GetMaxScrollOffset()` 的 `contentH`（`:509`）· `GetTextAreaHeight()`（`:500-503`）· `EnsureCaretVisible()` 上/下界（`:529-533`）· `SyncScrollBar()` 的 range / extent（`:594-595`）。★ 数学依据（§1.6）：`O > 0 ⇒ maxScrollOffset = 0 ⇒ scrollOffsetY ≡ 0`，两者互斥——本契约防的是**将来**有人把 `O` 加进滚动范围（届时互斥性被打破，会双重位移） |

---

## §6 影响面

| 项 | 估值 | 说明 |
|---|---|---|
| 公共头 | **99 → 100** | `Core/VerticalAlignment.h` 新增 1 件 |
| 公共 API | **+1 字段 + 1 枚举** | `TextBoxStyle::verticalAlign`（缺省 `Top`）+ `VerticalAlignment`；**无新方法**（`GetTextTopInset()` 在 protected） |
| 用例 | **505 → 513**（+8） | §4 表 |
| CMake | **0** | `GLOB_RECURSE`（新头自动入库）；测试用例需手工加 `RunAllTests.h` 声明 + `RunAllTests.cpp` 调用 |
| 源码改动 | `TextBox.cpp`（+1 函数 / OnPaint 4 行 / 光标 1 行 / 命中 1 行）· `TextBox.h`（+1 声明）· `TextBoxStyle.h`（+2 行）· `DefaultTheme.cpp`（+1 行）· `TextBox::SetStyle`（+1 行）· `Core/VerticalAlignment.h`（新） | **无后端改动**（GDI / GL 均只消费命令，不重算对齐） |
| 风险 | **低** | 纯几何、无跨链一致性面（与 Phase 36 的本质差别）。残余风险见 §7 |

---

## §7 风险 R37-1..4

| # | 风险 | 缓解 |
|---|---|---|
| **R37-1** | **三消费点漏改**（N1 根因重现：只改绘制、忘光标/命中） | T37-4（光标）+ T37-5/6（命中）三条；C37-2 写成契约；code review 检查点 = `grep GetTextLeftInset` 应只剩 X 语义的 3 处 |
| **R37-2** | **`A4-2` 假通过**（坐标落在非判别窗口） | 强制 T37-6 配对（同一坐标 `Top` 侧必须得行 1） |
| **R37-3** | **`L` 实测 ≠ 16**（DPI/字体差异） | 用例运行时读 `GetLineHeight()` 构造坐标，**不硬编码**；若 `O < L/2` 则该夹具不满足前置 ⇒ 换 `H` 或 `n` 重算，不降级判据 |
| **R37-4** | **`A < 0` 负高文本区（M11 既有路径）** | ★★ **v1.1 明确为残余风险**：`O = max(0, ·) = 0` **只**证明本方案不改变该路径的**几何原点**，**不**证明负高 `clip` 的完整行为安全。该路径**本 Phase 无用例、未验证**（`GLRenderer.cpp:907` 自述可达），按「没测到 ≠ 没问题」如实挂账，**不因本 Phase 而免责**；若将来要闭合，需独立立项 |

---

## §8 修订记录

- **v1.1（2026-10-26）** 外部评审「**修改后通过**」吸收——**5 项口径收紧，总体方案未变**（评审明确保留：`Top` 快路径 / 固定文本区 clip / 三消费点同源 / 不改测量与滚动范围）。① **超屏语义收紧**（§1.2）：v1.0 的「与改前逐位相同」含义不明 ⇒ 明确**只指绘制原点**（`lineY` 基 = `fy + p`）；视口外裁切是**既有 clip 语义、非本 Phase 保证**；**超屏不要求整体居中**，`EnsureCaretVisible`（`:529-533`）与滚动行为**一行不改**。② **§1.2 结论限定适用范围**：v1.0 写「`O` 不引入任何新的裁剪面」⇒ 限定为「**在 `C ≤ A` 适用域内**」，并列出**不外推的三处**（超屏裁切 / 负高 M11 / 滚动范围）。③ **`A < 0` 结论订正**：v1.0 写「`O = 0` ⇒ 不改变该路径行为」——`O = 0` **只能**证几何原点不变，**不能**证负高 `clip` 完整行为安全 ⇒ §7 **R37-4 改为明确残余风险**（本 Phase 无用例、未验证，不因本 Phase 免责）。④ **新增 §1.6 `Center` × 滚动组合语义**：证 `O > 0 ⇒ C < A ⇒ maxScrollOffset = 0 ⇒ scrollOffsetY ≡ 0` ⇒ `O` 与滚动偏移**数学互斥**；给三态行为表；新增**契约 C37-7** 封闭 `O` 的消费面（禁止进 `GetMaxScrollOffset` / `GetTextAreaHeight` / `EnsureCaretVisible` / `SyncScrollBar`）。⑤ **新增 §1.7 坐标口径表**：本日实测四量定系——`CaretIndexFromPosition` **入参 = 控件局部**（三调用点 `:1109` / `:1123` / `:1141` 均为 `mouse − abs`）· `CalculateCaretPosition` 返回 = 控件局部 · `GetCaretClientGeometry` 返回 = 客户区绝对 · `DrawTextCommand.pos` = `Paint` 传入值；据此写死 T37-4（`Paint` 传 `abs`）/ T37-5·6（直接传局部 `Point{ 1, p + O + L/2 }`、**不换算**）的取值规则。⑥ **T37-1 基准采集列为实施前置**：源码基准 = `5adcbedbc62f0432b6e756b8471d26b4d781fb10`（本日 `git log -1` 核实；文档提交**不改变**源码基准），**禁止先改 `TextBox` 再回头生成「改前」常量**。⑦ **T37-3 扩为三态**（`C = A` / `C > A` / `scrollOffsetY ≠ 0`），`C = A` 给出具体构造（`p=4 / n=2 / L=16 ⇒ H=40`），并显式断言 `GetMaxScrollOffset` 同值（C37-7 的自动判据）。
- **v1.0（2026-10-26）** 初稿落盘。闸门五项逐条收口：§1.1 调用点全表（**新入口恰 3 处**，`OnPaint` 内 `lineY` 为唯一几何基故下游自动跟随）· §1.2 覆盖性不等式（三情形，含 `A < 0` 既有 M11 路径不恶化）· §1.3 C30-3 触发条件辨析（缺省 = 零值 ⇒ 不触发；**前瞻契约**：翻案为 Center 即触发）· §1.4 A4 可测性（`GetCaretClientGeometry` 无窗口会空解引用 ⇒ **必须复用 T30-1 `CaretProbe` 真窗口装置**，且 Paint 须传 `abs` 对齐坐标系；A4-3 = A4-2 的判别力证明）· §1.5 勿重开 ✓。★ 本稿新增的关键决定 = **D37-C「`Top` 快路径不读行数」**：使 A1 的逐位不变有**结构性**保证，并把 D37-5 的时序风险在默认路径上归零——这是对需求稿 D37-5 倾向的**加强**（自查只作 Center 路径兜底）。
