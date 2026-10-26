# Phase 37 · TextBox 垂直对齐 —— 详细设计

> 状态：**v1.2**（2026-10-26 实施收口回写——三批落地 + C37 机检全过 + 收尾四项；v1.1 = 详设评审「修改后通过」吸收 **P0×3 + P1×1 + P2×1 + P3×5**）｜上游 = 初设 `phase37-textbox-vertical-align-preliminary-design.md` **v1.1**（外部评审「修改后通过」）｜台账 = `roadmap-deferred.md` **#76 已关闭**（v2.52）
> ★★ **实施前置（硬门，未过不得动源码）**：**T37-1 基准采集必须先于任何 `TextBox.cpp` 改动完成**。可机检判据 = `git diff --stat -- ECDI/src/Widget/TextBox.cpp` **为空**（详见 §3）。
> ★ **事实纪律**：所有行号 = 2026-10-26 `grep -n` 亲核（**不从上下文推算**——本 Phase 已因此错两次）；`L = 16.0f` 在无头环境由 `FakeTextMeasurer`（`TextBoxTests.cpp:32-42`）确定性提供、生产环境由 `GetLineHeight()` 兜底 `16.0f`（`TextBox.cpp:489-491`），二者同值 ⇒ 本稿几何算式均可无头精确验算。

---

## §1 接口与结构定稿

### 1.1 新公共头 `ECDI/include/ECDI/Core/VerticalAlignment.h`（全文，△1）

```cpp
#pragma once

#include <cstdint>

namespace ECDI{

/// @brief 文本垂直对齐（内容不足一屏时行箱的起始位置）
/// @note 缺省 Top = 枚举零值 ⇒ 依赖零值不会改变任何既有行为。
///       ★ 与 ButtonStyle::textAlignment 的 C30-3 情形**不同**：那里缺省 Center(1) ≠ 零值 Left(0)，
///         故必须显式注入主题缺省；本枚举缺省与零值一致，显式注入只是纪律、不承担正确性。
/// ★★ 前瞻契约：若将来把主题缺省改为 Center，**C30-3 立即触发** —— 必须同时改
///   DefaultTheme::GetTextBoxStyle() 的注入，不得只改缺省值了事。
enum class VerticalAlignment : std::uint8_t{
	Top    = 0,   ///< 顶对齐（缺省 = Phase 37 前行为，逐位不变）
	Center = 1,   ///< 垂直居中（仅当内容高 ≤ 文本区高时生效；超屏自动退化为 Top）
};

}
```

### 1.2 `TextBoxStyle` 字段（△2 / △3）

位置 = **`borderColor` 之后（末尾）**，规避 C7560 designated-initializer 顺序风险（同 Phase 36 D36-5 教训）：

```cpp
// TextBoxStyle（TextBoxStyle.h:21 之后）
	StyleField<VerticalAlignment> verticalAlign;   ///< ★ Phase 37：垂直对齐（缺省 Top = 逐位不变；Center 仅在内容不足一屏时生效）

// TextBoxStyleOverride（TextBoxStyle.h:35 之后）
	std::optional<VerticalAlignment> verticalAlign;
```

★ 需在 `TextBoxStyle.h` 顶部追加 `#include "ECDI/Core/VerticalAlignment.h"`。

### 1.3 `GetTextTopInset()` 声明（△5，v1.1 落点订正 = P0-2）

★ **v1.0 落点（`:327` 后）作废**：`GetTextLeftInset` 声明在 **private 区**（访问区实测：public `:26` / protected `:196` / **private `:280`**；需求稿 K3「protected」系误标，沿需求 → 初设 → 详设传播，本版订正）。private 成员**不能被派生类 `using` 暴露**（C++ 访问规则；`TestableTextBox` 既有注释「CaretIndexFromX 为 private、派生类不可访问」即此约束），而 T37-2 依赖 `using TextBox::GetTextTopInset;`——v1.0 落点下该测试**编译不过**。

**v1.1 定案：落 protected 区「多行与滚动」组**（`RecalculateLines` 声明之前，语义同组——Center 路径自查的就是行缓存重算；同区先例 = IME 组注释明写「测试经 TestableTextBox using 暴露」）。「不扩公开面」承诺保持（protected 对外部不可见），T37-2 可行：

```cpp
	/// @brief 文本绘制原点相对控件原点的 **Y**（= padding + 居中外缩量）。
	/// 逻辑上与 `GetTextLeftInset()` 成对（后者 private：**X 恒用 Left**（≡ padding），
	/// **Y 恒用本函数**（含居中））；本函数落 protected 仅为测试可达（T37-2 `using` 暴露），
	/// X/Y 语义对称性由 C37-6 保证。
	/// ★ 不变量 1（A1 依赖）：`verticalAlign == Top` ⇒ **恒等于** `GetTextLeftInset()`
	///   —— 快路径直接返回 padding，**不读行数**（故无时序依赖、无浮点误差）。
	/// ★ 不变量 2：内容高 > 文本区高 ⇒ **恒等于** padding（超屏退化，与改前逐位同）。
	/// @note 非 const 风险：Center 路径依赖 `m_lineStarts.size()`；若 `m_needsLineRecalc` 未清则自查重算
	///   （与 `CalculateCaretPosition` 同手法 `const_cast`，`TextBox.cpp:657-658`）。
	float GetTextTopInset() const;
```

### 1.4 实现（△6，插入 `TextBox.cpp:498`（`GetTextLeftInset` 函数体）之后）

```cpp
float TextBox::GetTextTopInset() const{

	// ★★ Top 快路径：**不碰 m_lineStarts** —— A1「逐位不变」的结构性保证（D37-C）。
	//    不是「算出来恰好相等」，而是根本没有计算；同时使行数时序风险在默认路径上归零。
	if (m_style.verticalAlign.value == VerticalAlignment::Top)
		return m_style.padding.value;

	// Center：行缓存自查（兜底——正常路径三个调用点均已在重算之后，见 §2 △7..△9 注释）
	if (m_needsLineRecalc)
		const_cast<TextBox*>(this)->RecalculateLines();

	const float lineH = GetLineHeight();
	// 内容高的唯一真相源 = 行缓存 × 行高（与 GetMaxScrollOffset 同源，`TextBox.cpp:505-511`）
	const float contentH = static_cast<float>(m_lineStarts.size()) * lineH;
	// ★ max(0, ·)：内容超屏（含 A < 0 的负文本区）⇒ 偏移归零 ⇒ 与改前逐位同（C37-1 不变量 2）
	const float offset = (std::max)(0.0f, (GetTextAreaHeight() - contentH) / 2.0f);
	return m_style.padding.value + offset;

}
```

---

## §2 逐文件差异 △1..11

| △ | 文件 | 位置 | 动作 |
|---|---|---|---|
| **△1** | `include/ECDI/Core/VerticalAlignment.h` | 新建 | §1.1 全文 |
| **△2** | `include/ECDI/Theme/TextBoxStyle.h` | `:21` 后 | 加 `StyleField<VerticalAlignment> verticalAlign;` |
| **△3** | 同上 | `:35` 后 | 加 `std::optional<VerticalAlignment> verticalAlign;` |
| **△4** | `src/Theme/DefaultTheme.cpp` | `:47` 后（`borderColor` 注入之后、`return s` 之前） | 加 `s.verticalAlign.value = VerticalAlignment::Top;` |
| **△5** | `include/ECDI/Widget/TextBox.h` | **protected 区「多行与滚动」组、`RecalculateLines`（`:251`）声明之前**（v1.1 订正——`:327` 后为 private 区，见 §1.3 P0-2） | §1.3 声明块 |
| **△6** | `src/Widget/TextBox.cpp` | `:498` 后 | §1.4 实现 |
| **△7** | 同上 | `:1336-1343`（`OnPaint`） | 见下「△7 逐行」 |
| **△8** | 同上 | `:676`（`CalculateCaretPosition` 返回） | `GetTextLeftInset() + caretY` → `GetTextTopInset() + caretY`（**X 那半不动**） |
| **△9** | 同上 | `:753`（`CaretIndexFromPosition` Y 定行） | `localPos.y - GetTextLeftInset()` → `localPos.y - GetTextTopInset()` |
| **△10** | 同上 | `:88`（`ApplyTheme`） | 加 `m_style.verticalAlign.Apply(defaults.verticalAlign.value);` |
| **△11** | 同上 | `:110`（`SetStyle`） | 加 `if (override.verticalAlign) m_style.verticalAlign.Set(*override.verticalAlign);` |

### △7 · `OnPaint` 逐行（唯一需要拆变量的地方）

```cpp
// ─── 改前（:1336-1343）───
	const float inset = GetTextLeftInset();
	const float textX = fx + inset;   // 客户区绝对 X
	const float textY = fy + inset;   // 客户区绝对 Y（顶部对齐 + 焦点框内缩）
	const float viewX = textX - m_scrollOffsetX;
	ctx.PushClip(Rect{ textX, textY, GetTextAreaWidth(), GetTextAreaHeight() });

// ─── 改后 ───
	const float insetX = GetTextLeftInset();   // X：恒 padding（textX / viewX / clip 用）
	const float insetY = GetTextTopInset();    // Y：含居中（textY / lineY 用）
	const float textX = fx + insetX;
	const float textY = fy + insetY;
	const float areaTop = fy + insetX;         // ★ clip 的 y = **文本区顶**，不随居中外缩量变化（C37-3 / D37-D）
	const float viewX = textX - m_scrollOffsetX;
	ctx.PushClip(Rect{ textX, areaTop, GetTextAreaWidth(), GetTextAreaHeight() });
```

**`:1356` 下游零改动**（`lineY = textY + li×lineH − scrollOffsetY`）——Selection 高亮（`:1372`）、行文本（`:1378`）、IME 组合下划线（`:1394`）三处**全部经 `lineY`**，故一处改动自动全跟随。

### △8 / △9 的守卫注释（必须一并写入，防 N1 根因复发）

```cpp
// △8（:676）
	// Y 与 X **各自**单一来源：X 恒 GetTextLeftInset（= padding），Y 恒 GetTextTopInset（含居中）
	// ★ 禁止把两者合并回一个函数——那正是 Phase 37 前「padding 三耦合」的根因（N1）
	return Point{ GetTextLeftInset() + caretX, GetTextTopInset() + caretY };

// △9（:753）
	// 1. Y 定行：可视 Y − **Y 原点**（含居中）+ scrollOffsetY → 逻辑 Y → 行号
	// ★ 必须与绘制/光标用同一个 Y 原点，否则「画居中了、点还算顶部」（A4-2/A4-3 的判别对象）
	const int logicalLine = (std::max)(0,
		static_cast<int>((localPos.y - GetTextTopInset() + m_scrollOffsetY) / lineH));
```

### ★ 零改动面（明确列出，防误伤）

`GetPreferredSize` · `MultilinePreferred` · `GetMaxScrollOffset` · `EnsureCaretVisible` · `GetTextAreaWidth` · `GetTextAreaHeight` · `LayoutScrollBar` · `RecalculateLines` · `TextWidget::CalculateTextPosition`（Button 的居中路径）——**全部不动**。⇒ 由 C37-7 反向校验：`grep -n "GetTextTopInset" TextBox.cpp` 的结果**只允许出现在 △6/△7/△8/△9 四处**。

---

## §3 基准导出记录（实施前置，T37-1 的真相源）

### 3.1 硬门前置

```cmd
git diff --stat -- ECDI/src/Widget/TextBox.cpp
```
**输出必须为空**（= `TextBox.cpp` 未被本 Phase 触碰）。源码基准 = **`5adcbedbc62f0432b6e756b8471d26b4d781fb10`**（2026-10-26 `git log -1` 核实）。★ **提交文档本身不改变源码基准**；★ **禁止先改 `TextBox` 再回头生成「改前」常量**——那样 A1 退化为同义反复。

### 3.2 采集环境：**用 `FakeTextMeasurer`，不用真字体**

`FakeTextMeasurer`（`TextBoxTests.cpp:32-42`）确定性给出「每码点 8 宽 / **行高 16**」。⇒ 基准**不依赖 DPI 与字体族**，可进 CI、可跨机复现。

★ 这与评审要求的「记录 DPI / 字体配置」不冲突：**仍需记录**（作为环境声明），但**基准值本身对该环境恒等**——因为 `textY = fy + inset` 式子中**不含任何字体项**，A1 要验的正是这一点。真字体观感由 `ModelProbe` / 真窗口用例（T37-4）另行覆盖。

### 3.3 三组夹具与**可推导的**期望值

设备：`Paint(ctx, 0, 0)`（传 0 ⇒ 命令坐标为**控件局部**，见初设 §1.7）；`L = 16`。

| 组 | `H` | `p` | 文本 | `n` | 期望 `DrawTextCommand.pos.y` 序列（= `p + i×L`） |
|---|---|---|---|---|---|
| **B1** | 30 | 0 | `"abc"` | 1 | **0** |
| **B2** | 60 | 4 | `"abc\ndef"` | 2 | **4, 20** |
| **B3** | 60 | 4 | `"a\nb\nc\nd"` | 4 | **4, 20, 36, 52** |

★ 期望值**不是猜的**：`Top` 路径 `textY = fy + p`、`lineY = textY + i×L` ⇒ 序列即 `p + i×L`。仍要求实施前实测核对一次（§3.4），**核对不一致即停下查根因**，不许改期望值去迁就实测。

### 3.4 采集与核对规程（路 A 推荐：零新目标）

1. 确认 §3.1 硬门输出为空。
2. 在 `TextBoxTests.cpp` **追加一个临时用例** `Test37BaselineProbe()`：构造 `FakeTextMeasurer` + `RecordingBackend`，按 B1/B2/B3 各 `Paint` 一次，`printf` 出每组全部 `DrawTextCommand.pos.y`。此时 `TextBox.cpp` 仍未改。
3. 跑一次 `ecdi_tests`，把读数与 §3.3 表逐格对。
4. **一致** ⇒ 把该临时用例改写为 T37-1（断言常量表），进入实施；**不一致** ⇒ 停止实施，先查 `lineY` / `inset` 根因（大概率是本机 `lineH` 非 16，或 `Paint` 传入非 0）。
5. **采集记录（2026-10-26 23:09 已完成，逐格一致）**：

| 项 | 值 |
|---|---|
| 来源 commit | `5adcbedbc62f0432b6e756b8471d26b4d781fb10`（采集前后各复检一次，`TextBox.cpp` 均零 diff） |
| 采集装置 | `FakeTextMeasurer`（每码点 8 宽 / `L ≡ 16`）—— **无字体、无 DPI 依赖** |
| 环境声明 | 本机 1920×1080 @ DPI 120（对基准值**无影响**，因该装置不含字体项） |
| B1 实测 | `y0=0.00` ✅ 与 §3.3 推导一致 |
| B2 实测 | `y0=4.00 y1=20.00` ✅ |
| B3 实测 | `y0=4.00 y1=20.00 y2=36.00 y3=52.00` ✅（超屏组未出现 `SyncScrollBar` 主动滚动——R37 首嫌疑点**实测排除**） |
| **结论** | **基准成立**，A1 的常量表即 §3.3 三行，**无需调整** |

★★ **探针暂不删除，有第二个用途**：批一（△1–△6「只加不改」）落地后**复跑同一探针**，读数必须与上表**逐位相同** —— 这是 A1 在改动现场的第一次实证（比只看断言更有说服力，因为它打印的是原始命令坐标而非经过断言的量）。T37-1 断言版落地后，探针连同 `#include <cstdio>` 一并删除。

★ 路 B（备选）：`.workbuddy/spike/p37/` 独立探针 + 手工 `cl`。仅在「临时用例污染 A1 判据」被认为不可接受时使用；本稿认为不可接受性不成立——临时用例**只读打印、无断言**，且 `git diff` 可证 `TextBox.cpp` 未动。

---

## §4 契约 C37-1..7（终稿，附**可机检**方式）

| # | 条款 | 机检方式 |
|---|---|---|
| **C37-1** | `GetTextTopInset() = padding + max(0, (A − C)/2)`，`A = H − 2p`、`C = nL`。★ 不变量 1：`Top` ⇒ **恒等于** `padding`（不读行数）；★ 不变量 2：`C > A` ⇒ **恒等于** `padding` | T37-2 / T37-3 断言；`grep -n` 定位后**行序核对**（Top 快路径 return 行号 < 函数体内首次 `m_lineStarts` 引用行号——grep 只证存在性，先后序需按行号判，P3-5） |
| **C37-2** | **三消费点同源**：绘制（`lineY` 基）· 光标（`CalculateCaretPosition`）· 命中（`CaretIndexFromPosition` 定行）**全部经 `GetTextTopInset()`** | `grep -n "GetTextTopInset" TextBox.cpp` ⇒ 命中集合恰为 △6/△7/△8/△9；T37-4/T37-5/T37-6 |
| **C37-3** | `PushClip` 的 y = `fy + padding`，**不随 O 变化** | `grep -n "PushClip" TextBox.cpp` 检查实参为 `areaTop`（= `fy + insetX`） |
| **C37-4** | 缺省 `Top` ⇒ 渲染 / 光标 / 命中 / 滚动**一律不变** | T37-1（逐位）+ 505 条存量全绿 |
| **C37-5** | 居中**不参与测量**：`GetPreferredSize` / `MeasureText` / `LineHeight` / `GetMaxScrollOffset` 全不变 | 上述函数**零 diff**（`git diff` 直查） |
| **C37-6** | X / Y **各自单一来源**，任一函数内部**禁止**引用另一个 | `grep` 检查 `GetTextTopInset` 实现体内**无** `GetTextLeftInset()`；反之亦然 |
| **C37-7** | `O` **只**进三处坐标；**禁止**进 `GetMaxScrollOffset` contentH · `GetTextAreaHeight` · `EnsureCaretVisible` 上下界 · `SyncScrollBar` range/extent | T37-3-③ 行为等价断言（v1.1 P0-3：`GetMaxScrollOffset` 为 private 不可直调）；`git diff` 确认四个函数零改动（**主判据**） |

---

## §5 测试 T37-1..8 详设（505 → 513）

**装置分层（本稿关键简化）**：除 **T37-4 必须真窗口**（`GetCaretClientGeometry` 无条件解引用 `GetWindow()`，`TextBox.cpp:682`）外，其余七条**全部无头**——依据：`RecalculateLines` 纯 `\n` 扫描不依赖测量器（`TextBox.cpp:443-456`）· 无窗口下 `CaretIndexFromLineX` 因 `GetWindow() == nullptr` **直接返回行首码点**（`:772` 判断 + `:788` return，v1.1 订正 v1.0 的 `:789` 系闭括号行）⇒ 命中判据只需断言 `GetCaret()` 落在哪一行首，不需要 X 定位精度。

**装置可达面（v1.1 = P0-2 关联调查，逐函数 `grep` 实测）**：`GetCaret()`（public `TextBox.h:125`）· `GetCaretClientGeometry()`（public `:194`）· `SetStyle`（`:89`）/ `ApplyTheme`（`:86`）/ `SetSize`（`:93`）（均 public）——直调 ✓；`m_style`（**protected `:211`**——测试可直读 `box.m_style.verticalAlign.value`）；`GetTextTopInset()`（本 Phase 新增，落 protected ⇒ `using` 暴露 ✓）。★ **`GetMaxScrollOffset()` 为 private（`:333`）不可直调**（与 K3 同源的第三处访问区误标后果）——T37-3 ③ 的同值判据据此改为行为等价（见该条）。★ 事实订正：`CaretIndexFromPosition` 实为 **protected**（`TextBox.h:261`）可 `using` 暴露——v1.0 及台账「private 不可暴露」的说法源自同文件旧注释的单行算法 `CaretIndexFromX`；T37-5/6 仍维持 `GetCaret()` 行首判据（不依赖 `using` 装置，更简）。

| # | 夹具 | 断言 | 判别力 |
|---|---|---|---|
| **T37-1** | §3.3 的 B1/B2/B3，`FakeTextMeasurer` + `RecordingBackend`，`Paint(ctx,0,0)` | `pos.y` 序列 **逐格等于**常量表（0 / 4,20 / 4,20,36,52） | 缺省路径任何偏移都会打破等式 |
| **T37-2** | `TestableTextBox`（新暴露 `using TextBox::GetTextTopInset;`——**前提 = △5 落 protected，§1.3 P0-2**）+ `FakeTextMeasurer`（`L=16`），三组：`(H,p,n)` = (30,0,1) / (60,4,2) / (40,4,2) | `GetTextTopInset()` 依次 = **7 / 14 / 4**（`O` = **7** / 10 / 0） | ★ v1.1 订正（P0-1）：v1.0 组1 写 0 系**算错**——(30,0,1)：`A=30, C=16, O=(30−16)/2=7`（非 0）；照 v1.0 断言则测试必红，且期望 0 == Top 值对该组实现错误**零判别**。三组实际为 `O=7 / O=10 / O=0`：组1/2 验 `O>0` 生效，组3 `C=A ⇒ O=0`（**上边界**） |
| **T37-3** | 三态：① `C=A`：`H=40,p=4,n=2`（`A=32,C=32`）② `C>A`：`H=40,p=4,n=4`（`C=64`）③ `C>A` + 预置滚动——`OnMouseButtonDown` 点行 3（局部 y = 4+3×16+8 = 60 > 视口底 36）触发 `EnsureCaretVisible`（★ v1.1 P3-4：`scrollOffsetY` 为 private 无写入口，预置手段 = 点击；点击前先 Paint 存基准序列，点击后再 Paint——**序列必须变化** = 滚动确实发生的自证） | ① `GetTextTopInset()==4` ② `==4` ③ `==4` **且** 滚动后绘制命令序列 Top/Center 两配置**逐位同** | ②③ 验 C37-1 不变量 2；③ 验 C37-7（`O` 未混入滚动范围）。★ v1.1（P0-3）：v1.0 的「`GetMaxScrollOffset()` 与 Top 配置同值」**直调断言删除**——该函数 private（`:333`）不可达；同值判据由 C37-7 的「四函数 git diff 零改动」机检承担（更强），③ 的行为等价判据补上运行时证据 |
| **T37-4** | **真窗口**（扩展 `CaretProbe`，`TextBoxTests.cpp:1598-1640`）：`SetStyle(Center)`，`caret=0`，`scroll=0`，`Paint(ctx, abs.x, abs.y)` | `GetCaretClientGeometry().rect.y ==` 首行 `DrawTextCommand.pos.y` | 光标与绘制**必须**同系；Paint 传 0 则此项必然失败（装置自检） |
| **T37-5** | 无头：文本 `"abc\ndef"`、`H=60`、`p=4`、`Center`，`OnMouseButtonDown` 传 `mouse = abs + Point{1, 22}`（`abs` 无父时为 (0,0)） | `GetCaret() == 0`（行 0 首码点） | 若命中未跟随 ⇒ `(22−4)/16 = 1` ⇒ 得 **4**（行 1 首）⇒ 断言失败 |
| **T37-6** | 同 T37-5 夹具，仅把 `verticalAlign` 换 `Top`，同坐标 `y=22` | `GetCaret() == 4`（行 1 首码点） | ★ **证明 `y=22` 确实落在判别窗口内**（排除 T37-5 因坐标选错而假通过）；窗口宽度 = `O` = 10 > 0 |
| **T37-7** | 新 `TextBox`（无窗口），不设 `verticalAlign`；再 `ApplyTheme(DefaultTheme)` | `GetTextTopInset() == m_style.padding.value`（即 `O = 0`；`m_style` protected 直读——v1.1 订正 v1.0 的 `Padding()` 伪码，P3-3）；`DefaultTheme::GetTextBoxStyle().verticalAlign.value == Top` | 缺省 = Top（C37-4 / §1.3 双保险） |
| **T37-8** | `SetStyle` 设 `Center` → `ApplyTheme` → 再 `SetStyle` 设 `Top` | ① `Center` 生效（`== m_style.padding.value + 10.0f`，用 `H=60,p=4` ⇒ `O=10`；v1.1 P3-3 同上）② `ApplyTheme` **不覆盖** override（D7：`StyleField::Apply` 只在 `!overridden` 时写）③ 回 `Top` 后 `== m_style.padding.value` | 覆盖链四同步处（K6）之一失效即失败 |

★ 与需求稿的 **+6 → +8**：A4 拆 3 条（T37-4/5/6）+ A5 拆 2 条（T37-7/8）⇒ 判据细化，非范围膨胀。

★ **T37-5 的 `abs` 说明**：`OnMouseButtonDown` 内 `Point abs = GetAbsolutePosition()`（`TextBox.cpp:1106`）后减 `abs` 转局部；控件无父且 `SetPosition` 未设时 `abs = (0,0)` ⇒ 直接传局部 y = 22。若夹具给控件设了位置，则须加回 `abs`（测试内注释写明）。

---

## §6 影响面与收尾同步

| 项 | 值 |
|---|---|
| 公共头 | **99 → 100**（`Core/VerticalAlignment.h`） |
| 公共 API | +1 字段（`TextBoxStyle::verticalAlign`）+ 1 枚举；**无新方法**（`GetTextTopInset` protected） |
| 用例 | **505 → 513**（+8） |
| CMake | **0**（`GLOB_RECURSE` 自动入库新头）；测试另需手工加 `RunAllTests.h` 声明 + `RunAllTests.cpp` 调用 |
| 源码 | 6 文件：新头 1 · `TextBoxStyle.h` · `DefaultTheme.cpp` · `TextBox.h` · `TextBox.cpp` · `TextBoxTests.cpp` |

**收尾同步清单（实施完成时逐项核）**：

| # | 位置 | 动作 |
|---|---|---|
| 1 | `docs/dev/README.md` | 规模锚点：用例 505 → **513**、公共头 **99 → 100**（★ v1.1 订正 = P1-1：v1.0 说「README 公共头锚点链最新为 96，与实测 99 口径不同」**不成立**——README 规模锚点行现行值就是 99，口径注明 `version.h` 为 CMake 生成头不计、与 `find` 完全同口径；全 README 无「公共头 96」锚点，v1.0 该说法源自需求稿 v1.0 的过时观察，同日 deferred v2.46 已把 README 更新为 99） |
| 2 | `docs/usage.md` | `TextBoxStyle` 字段表（`:406`）与示例注释（`:314`）补 `verticalAlign`；`:402` 的 `ButtonStyle.textAlignment` 保持不动 |
| 3 | `roadmap-deferred.md` | #76 关闭（迁 §7.9「✅ 已完成」）+ 版本号递增 |
| 4 | `EDCI_fake_harness/docs/ecdi-capability-requests.md` | **REQ-14 翻转**（不在本仓改，**列为外部动作**，由 harness 仓自行处理） |

---

## §7 实施批次

| 批 | 内容 | 验证 |
|---|---|---|
| **前置** | §3 基准采集 + 核对（**改任何源码之前**） | §3.4 规程 |
| **批一** | △1–△6（枚举 / 字段 / 注入 / 声明 / 实现）——**只加不改**，此时行为应逐位不变 | 505 条存量全绿 + T37-1 + T37-7 |
| **批二** | △7–△9（OnPaint / 光标 / 命中三处切换）+ △10/△11（主题/覆盖） | T37-2..T37-6 + T37-8 |
| **批三** | 收尾同步（§6 清单 1–3；第 4 项为外部动作） | `doc_lint` + `git diff` 面核对 |

★ 批一后**刻意**跑一次 T37-1：若此时已不绿，说明字段加入本身影响了行为（应为零）——这是"只加不改"的可判性。

---

## §8 风险与盯防

| # | 风险 | 盯防 |
|---|---|---|
| **R37-1** | 三消费点漏改（N1 根因复发） | C37-2 的 `grep` 机检（命中集合必须恰 4 处）+ T37-4/5/6 |
| **R37-2** | T37-5 假通过（坐标不在判别窗口） | 强制 T37-6 配对（同坐标 `Top` 必须得行 1） |
| **R37-3** | `L` 实测 ≠ 16（真窗口/DPI 差异） | 无头用例统一用 `FakeTextMeasurer`（`L ≡ 16`）；真窗口只用于 T37-4，且其断言形式为「光标 y == 绘制 y」（**不硬编码任何 y 值**）⇒ 与 `L` 无关 |
| **R37-4** | `A < 0` 负高文本区（M11 既有路径） | **明确残余风险**：`O = 0` 只证几何原点不变，负高 `clip` 行为本 Phase **无用例、未验证**（按「没测到 ≠ 没问题」挂账，不因本 Phase 免责） |
| **R37-5** | `GetTextTopInset` 被将来调用方在行缓存失效时使用 | 入口内 `const_cast` 自查兜底（同 `:657-658` 手法）；C37-1 注明 |
| **R37-6** | 新公共头 GLOB 入库 × VS 生成器竞态（Phase 36 LNK2019 教训：`GLOB CONFIGURE_DEPENDS` 在 VS 生成器下不自检新增文件，v1.1 = P2-1） | 批一新增 `VerticalAlignment.h` 后，**MSVC（VS 生成器）链构建前显式 reconfigure**（用户既有拍板纪律）；Ninja/MinGW 系自检无此问题 |

---

## §9 修订记录

- **v1.2（2026-10-26）** 实施收口回写。① **三批落地**：前置基准两轮逐格一致（改前探针 + 批一后复跑**逐位同** = A1 现场实证）· **批一 `074b8aa` 507/507**（△1–△6 只加不改 + T37-1 + T37-7）· **批二 `ac80645` 513/513**（△7–△11 + T37-2..8）——MSVC Debug（`cmake-build-vs`，VS 2026 生成器，R37-6 reconfigure 纪律已执行）；MinGW/Clang/ClangCL 待用户侧补验（沿 #71 先例如实挂账）。② **C37 机检全过**：C37-2 `grep GetTextTopInset` 恰 4 处功能位置 · C37-6 实现体内零交叉引用 · C37-7 四函数零 diff · C37-3 `PushClip` 实参 = `areaTop`。③ **§3.4 探针按规程退役**：两轮打印验证后删除（连同 `#include <cstdio>`），T37-1 断言常量表接替（基准采集记录保留于本节 v1.0 ① 与 §3.4 第 5 步）。④ **实施期两处文档↔代码偏差订正**（均已在本 v1.1 前的评审吸收中钉死，实施如实照做）：T37-2 组1 期望值 = 7（v1.0 写 0 系算错）· △5 落 protected 区。⑤ **§6 收尾四项完成**：README 规模锚点（用例 505 → **513** · 公共头 **99 → 100** · docs 191 → **194**）· `usage.md`（`:314` 注释 / `:406` 字段表补 `verticalAlign`）· deferred #76 关闭迁 §7.9（台账 **v2.52**）· REQ-14 翻转 = harness 仓**外部动作**。
- **v1.1（2026-10-26）** 详设评审「**修改后通过**」吸收——**P0×3 + P1×1 + P2×1 + P3×5，方案本体（D37-A..F / C37 契约 / 几何算式 / 三批结构）不变**。① **P0-1 T37-2 组1 期望值订正**：v1.0 断言 `0` 系算错——(30,0,1) 在 `L=16` 下 `O=(30−16)/2=7`，正确期望 = **7**（`O` 序列 = 7/10/0）；照 v1.0 则测试必红，且期望 0 == Top 值对该组实现错误零判别。② **P0-2 △5 落点订正**：`GetTextLeftInset` 声明在 **private 区**（访问区实测 public `:26` / protected `:196` / private `:280`；需求稿 K3「protected」误标沿需求 → 初设 → 详设传播，本版订正）——private 成员不能被派生类 `using` 暴露，v1.0 落点下 T37-2 **编译不过**；v1.1 落 **protected 区「多行与滚动」组**（`RecalculateLines` 前），「不扩公开面」承诺保持。③ **P0-3（实施调查新增）T37-3 ③ 判据改写**：`GetMaxScrollOffset()` 为 private（`:333`）不可直调——v1.0 的直调断言不可实施；改为「预置滚动（点击行 3）→ 滚动自证（点击前后 Paint 序列必须变化）→ Top/Center 滚动后序列逐位同」的行为等价判据，同值判据由 C37-7 的 git diff 零改动机检承担。④ **P1-1 收尾清单 #1 订正**：README 规模锚点现行值就是 99（`version.h` 生成头不计口径与 `find` 一致），v1.0「锚点链最新为 96」不成立（源自需求稿 v1.0 过时观察，deferred v2.46 已更新）⇒ 收尾动作 = 公共头 99 → 100 直接改。⑤ **P2-1 新增 R37-6**：批一加新公共头后 MSVC（VS 生成器）链构建前显式 reconfigure（Phase 36 LNK2019 既有拍板）。⑥ **P3 批**：头部台账 v2.49 → v2.51（P3-1）· `:789` → `:788`（P3-2）· T37-7/8 的 `Padding()` 伪码 → `m_style.padding.value` 直读（P3-3，`m_style` protected `:211` 可达）· T37-3 ③ 预置手段写明 = 点击（P3-4）· C37-1 机检「grep 确认之前 return」→「行序核对」（P3-5）。⑦ **事实订正（装置调查副产物）**：`CaretIndexFromPosition` 实为 protected（`TextBox.h:261`）可 `using` 暴露——v1.0/台账「private 不可暴露」源自同文件旧注释的单行算法 `CaretIndexFromX`；T37-5/6 维持 `GetCaret()` 判据不变。⑧ §3.4 第 5 步基准采集记录（并行实施会话完成：三组逐格一致、探针批一后复跑、T37-1 落地时删除）并入本版。
- **v1.0（2026-10-26）** 初稿落盘。① §1 接口定稿（`VerticalAlignment.h` 全文 · 字段位置 = `borderColor` 之后避 C7560 · `GetTextTopInset` 双不变量写入声明注释 · 实现含 `Top` 快路径）。② §2 逐文件 **△1..11** + `OnPaint` 逐行对照 + 守卫注释原文 + **零改动面清单**。③ **§3 基准导出记录**（硬门前置 `git diff` 为空 · 源码基准 `5adcbedb` · **改用 `FakeTextMeasurer` 使基准不依赖 DPI/字体** · 三组可推导常量 · 采集规程路 A/B）。④ §4 契约 **C37-1..7** 每条附**机检方式**。⑤ §5 测试详设：**除 T37-4 外全部无头**——依据 `RecalculateLines` 不依赖测量器 + 无窗口下 `CaretIndexFromLineX` 返回行首码点（`:772`/`:789`）；T37-5/6 用 `GetCaret()` 的行首归属做判据，避开 private `CaretIndexFromPosition` 不可暴露的限制。⑥ §6 收尾同步清单（含 README 公共头锚点口径差异**不擅动**、REQ-14 翻转为**外部动作**）。⑦ §7 三批 + 「只加不改」的可判性设计。⑧ §8 **R37-3 的规避方式**：真窗口断言形式为「光标 y == 绘制 y」，**与 `L` 无关**。
