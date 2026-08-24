
# Phase 8.5.2 文本系统 2.0 详细设计（多行与滚动）

> 状态：v1.2（2026-08-24）｜定稿（GPT 两轮评审通过，可进入实现）
> 前序：Phase 8.5.1 完结 ✅（commit 8ab8300）/ 职责确认 v1.1 / 初步设计 v1.2
> 相关：phase8.5-text-system2.0-preliminary-design.md（B4 多行/B5 滚动/B7 双击/B9 坐标→Caret）/ phase8.5.1-text-system2.0-detailed-design.md（8.5.1 完结）
> 拆分说明：本文件 = 8.5.2 专属详细设计（原 phase8.5-text-system2.0-detailed-design.md §9 拆出）

---

## 9. 8.5.2 详细设计（v1.0 定稿）

### 9.1 文件改动清单

| 文件 | 改动 | 类型 |
|---|---|---|
| `ECDI/include/ECDI/Widget/TextBox.h` | 多行状态（m_lineStarts/m_needsLineRecalc/RecalculateLines）+ 滚动（m_scrollOffsetY/OnMouseWheel/EnsureCaretVisible）+ CaretIndexFromPosition + GetWordBounds + Enter 处理 | 修改 |
| `ECDI/src/Widget/TextBox.cpp` | 全部实现（行缓存/多行绘制/滚动/坐标定位/双击选词） | 修改 |
| `ECDI/include/ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h` | 新增 isDoubleClick 字段（默认 false——现有调用零破坏） | 修改 |
| `ECDI/src/Platform/Win32/WindowMessageHandler.cpp` | WM_LBUTTONDBLCLK → MouseButtonDownEvent(isDoubleClick=true) | 修改 |
| `ECDI/src/Platform/Win32/Win32WindowClass.cpp` | WNDCLASSW 加 CS_DBLCLKS 样式（否则收不到 WM_LBUTTONDBLCLK） | 修改 |
| `ECDI/src/Tests/TextBoxTests.cpp` | 8.5.2 TestCase F16-F25 | 修改 |

无新 .h/.cpp——双工程（vcxproj/CMake）零改动。

### 9.2 多行文本模型（B4）

```cpp
// TextBox.h private 区新增：
	std::vector<size_t> m_lineStarts;      ///< 每行起始码点索引缓存（m_lineStarts[0]=0；行数 = size()-1）
	bool m_needsLineRecalc = true;         ///< 编辑后标记需重算行信息（失效责任见 B4）

	/// @brief 重算行起始缓存（扫描 \n；仅 m_needsLineRecalc 时调用——惰性）
	void RecalculateLines();
```

**RecalculateLines 实现**（码点级扫描——与 B10 索引单位契约一致；GPT 修正：不直接 `m_text[index]` 判断，经 DecodeFirstCodepoint 取码点——避免"m_text[i] 就是第 i 个字符"的误读）：

```cpp
void TextBox::RecalculateLines(){
	m_lineStarts.clear();
	m_lineStarts.push_back(0);   // 首行恒从 0 开始
	const size_t count = GetCodepointCount();
	for (size_t i = 0; i < count; ++i){
		const size_t byte = CodepointIndexToByteOffset(m_text, i);
		const size_t nextByte = CodepointIndexToByteOffset(m_text, i + 1);
		const char32_t cp = DecodeFirstCodepoint(m_text.substr(byte, nextByte - byte));
		if (cp == U'\n')
			m_lineStarts.push_back(i + 1);   // 换行符归属行尾，下一行起始 = i+1
	}
	m_needsLineRecalc = false;
}
```

**行查询辅助**（private）：
```cpp
	/// @brief 码点索引 → 行号（0-based；二分 m_lineStarts）
	size_t LineIndexFromCodepoint(size_t cp) const;

	/// @brief 行号 → 该行码点区间 [start, end)（end 不含行尾 \n）
	/// @details 尾部空行契约（GPT 修正——多行易漏边界）：
	///   "ab\ncd"  → line0=[0,2] line1=[3,5]
	///   "ab\n"    → line0=[0,2] line1=[3,3]   ← 以 \n 结尾必须存在空的最后一行
	///   "ab"      → line0=[0,2]（无 \n 时仅一行）
	std::pair<size_t, size_t> LineRange(size_t lineIndex) const;
```

**LineRange 实现伪代码（GPT 锁死——避免实现阶段重新推导）**：

```cpp
std::pair<size_t, size_t> TextBox::LineRange(size_t lineIndex) const{
	const size_t start = m_lineStarts[lineIndex];
	// 非最后一行：下一行起始 - 1 = 本行行尾 \n 的位置（lineStarts 只由 \n 产生，-1 恒排除换行符）
	// 最后一行：行尾 = 文本末尾（无 \n 可排）
	const size_t end = (lineIndex + 1 < m_lineStarts.size())
		? m_lineStarts[lineIndex + 1] - 1
		: GetCodepointCount();
	return { start, end };
}
// 验证：
//   "ab\ncd" {0,3}   count=5 → [0,2] [3,5)
//   "ab\n"   {0,3}   count=3 → [0,2] [3,3)
//   "a\n\nb" {0,2,3} count=4 → [0,1) [2,2) [3,4)
```

**Y 映射契约（GPT 明确化——CaretIndexFromPosition 的行为定义）**：

> **任何 Y 坐标都映射到最近的有效文本行**：点击控件底部下方 → clamp 到最后一行；点击第一行上方 → clamp 到第一行。这是设计契约（非"代码恰好如此"）。

**失效责任（B4 已锁——实施时在编辑操作统一入口）**：InsertText/InsertCodepoint/DeleteBackward/DeleteForward/Cut/Paste/Undo/Redo/IME Commit/Enter → `m_needsLineRecalc = true`；光标/Selection/Scroll/Blink → 不置。

**Enter 键（OnKeyDown 新增 case）**：
```cpp
	case KeyCode::Enter:   InsertText("\n");  break;   // 8.5.2：显式换行
```

### 9.3 垂直滚动（B5）

```cpp
// TextBox.h private 区新增：
	float m_scrollOffsetY = 0.0f;    ///< 垂直滚动偏移（像素；clamp [0, maxScroll]）

	/// @brief 计算最大滚动偏移（内容总高 - 可视高；负值 → 0）
	float GetMaxScrollOffset() const;

	/// @brief 滚动使光标可见（编辑/光标移动后调用——光标跟随）
	void EnsureCaretVisible();

// TextBox.cpp：
void TextBox::OnMouseWheel(const MouseWheelEvent& event){
	// 滚轮：向上（delta>0）内容上移（scrollOffsetY 减小）——自然方向
	m_scrollOffsetY -= static_cast<float>(event.GetDelta()) / 120.0f * kScrollLinePx;
	m_scrollOffsetY = (std::clamp)(m_scrollOffsetY, 0.0f, GetMaxScrollOffset());
	Invalidate();
	SyncTextInputCaret();   // 5.6 债务兑现：候选窗位置与滚动联动
}
```

**常量**（TextBox.cpp 匿名 namespace）：`constexpr float kScrollLinePx = 16.0f;`（一行滚动量——WHEEL_DELTA=120 一行）。

**固定行高契约（GPT 锁死——8.5.2 全控件固定 lineH）**：

> **TextBox 2.0 暂不支持逐行不同字体/字号——整个 TextBox 使用固定 line height**（`GetLineHeight()`）。
> 因此 `Y / lineH` 才成立（行号 = 逻辑 Y ÷ 行高）；未来富文本再升级 `line[i].height`（YAGNI，不提前设计）。

**GetMaxScrollOffset 公式（GPT 锁死）**：

```cpp
float TextBox::GetMaxScrollOffset() const{
	// 内容总高 = 行数 × 固定行高（**含最后一行的完整行高**——5 行 viewport 3 行 → maxScroll = 2*lineH，
	// 最后一行可恰好滚到可视区底部）；可视高 = 文本区域高度
	const float contentHeight = static_cast<float>(m_lineStarts.size()) * GetLineHeight();
	const float viewportHeight = GetTextAreaHeight();
	return (std::max)(0.0f, contentHeight - viewportHeight);
}
// 辅助：float GetTextAreaHeight() const;（与 GetTextAreaWidth 对称——垂直可视区，含焦点框内缩）
```

**EnsureCaretVisible 边界语义（GPT 锁死）**：

```cpp
void TextBox::EnsureCaretVisible(){
	// 光标行逻辑 Y（不含 scrollOffset）：
	const float caretTop = static_cast<float>(LineIndexFromCodepoint(m_caret)) * GetLineHeight();
	const float caretBottom = caretTop + GetLineHeight();
	const float viewportTop = m_scrollOffsetY;
	const float viewportBottom = m_scrollOffsetY + GetTextAreaHeight();
	// 上边界：光标在可视区上方 → 滚到光标顶部
	if (caretTop < viewportTop)
		m_scrollOffsetY = caretTop;
	// 下边界：光标在可视区下方 → 滚到光标底部
	else if (caretBottom > viewportBottom)
		m_scrollOffsetY = caretBottom - GetTextAreaHeight();
	m_scrollOffsetY = (std::clamp)(m_scrollOffsetY, 0.0f, GetMaxScrollOffset());
	Invalidate();
	SyncTextInputCaret();
}
// 统一调用点：键盘移动/Enter/Backspace/Delete/Paste/IME Commit（8.5.2 基础设施）
```

**光标跟随**：编辑操作/光标移动后调 `EnsureCaretVisible()`——光标行 y 超出可视区时滚动到可见（上/下边界语义见上）。

**滚动与光标/IME 联动**：CalculateCaretPosition（见 9.5）y 减 scrollOffsetY → 光标/系统 caret/IME 候选窗全部跟随滚动（5.6 债务表"候选位置与滚动偏移联动"兑现）。

### 9.4 坐标→Caret（B9：CaretIndexFromX → CaretIndexFromPosition）

```cpp
// TextBox.h private（替代原单行 CaretIndexFromX）：
	/// @brief 相对文本框左上角的客户区坐标 → 最近码点索引（多行：Y 定行 → X 定行内 → 全局）
	/// @param localPos 相对控件原点（OnMouseButtonDown/OnMouseMove 经 GetAbsolutePosition 换算）
	size_t CaretIndexFromPosition(Point localPos) const;
```

**定位链**（scrollOffset 参与——点击定位与绘制同源）：

```cpp
size_t TextBox::CaretIndexFromPosition(Point localPos) const{
	if (m_text.empty())
		return 0;
	if (m_needsLineRecalc)
		RecalculateLines();   // 惰性重算（点击定位首次触发）
	// 1. Y 定行：可视 Y + scrollOffsetY → 逻辑 Y → 行号（clamp 到有效行）
	const float lineH = GetLineHeight();
	const int logicalLine = (std::max)(0, static_cast<int>((localPos.y + m_scrollOffsetY) / lineH));
	const size_t lineIndex = (std::min)(static_cast<size_t>(logicalLine), m_lineStarts.size() - 1);
	// 2. 行起始码点索引 + 行内文本（取行内容——不含 \n）
	const auto [lineStartCp, lineEndCp] = LineRange(lineIndex);
	const size_t startByte = CodepointIndexToByteOffset(m_text, lineStartCp);
	const size_t endByte   = CodepointIndexToByteOffset(m_text, lineEndCp);
	const std::string lineText = m_text.substr(startByte, endByte - startByte);
	// 3. X 定行内码点（复用单行前缀测量逻辑——相对行文本起点）
	TextMeasurer& measurer = GetWindow()->GetTextMeasurer();
	float innerX = localPos.x - GetTextLeftInset();   // 文本起点 x（同源——9.6 绘制）
	// 行内线性扫描（同原 CaretIndexFromX 算法——O(行长²) 短行够用）
	size_t lineInnerCp = 0;
	const size_t lineCpCount = ByteOffsetToCodepointIndex(lineText, lineText.size());
	for (size_t i = 0; i < lineCpCount; ++i){
		const size_t byte = CodepointIndexToByteOffset(lineText, i + 1);
		const Size prefix = measurer.MeasureText(m_font, lineText.substr(0, byte));
		if (innerX <= prefix.width){
			const Size prevPrefix = (i == 0) ? Size{}
				: measurer.MeasureText(m_font, lineText.substr(0, CodepointIndexToByteOffset(lineText, i)));
			const float charWidth = prefix.width - prevPrefix.width;
			lineInnerCp = (innerX >= prevPrefix.width + charWidth / 2.0f) ? i + 1 : i;
			break;
		}
		lineInnerCp = i + 1;   // 未 break → 点在行尾
	}
	// 4. 全局索引 = 行起始 + 行内
	return lineStartCp + lineInnerCp;
}
```

**辅助**（GetLineHeight / GetTextLeftInset——绘制与定位共享，防漂移）：
```cpp
	/// @brief 单行行高（TextMeasurer::LineHeight(m_font)；全系统唯一行高来源——
	/// OnPaint / CaretIndexFromPosition / CalculateCaretPosition / EnsureCaretVisible / Scroll 共用）
	float GetLineHeight() const;

	/// @brief 文本实际绘制起点相对 TextBox client origin 的 X 坐标（单一来源——
	/// 不是"若干视觉偏移相加"；OnPaint 用 textX = GetTextLeftInset()，
	/// CaretIndexFromPosition 用 innerX = localPos.x - GetTextLeftInset()——同源防漂移）
	float GetTextLeftInset() const;
```

**OnMouseButtonDown/OnMouseMove 改造**：`CaretIndexFromX(measurer, innerX)` → `CaretIndexFromPosition(Point{ mouseX - abs.x, mouseY - abs.y })`（相对控件坐标）。

### 9.5 光标/IME 位置多行（CalculateCaretPosition 升级）

```cpp
Point TextBox::CalculateCaretPosition(TextMeasurer& measurer) const{
	// 多行：行号定 y（- scrollOffsetY），行内前缀定 x——与绘制/点击定位同源
	if (m_needsLineRecalc)
		RecalculateLines();   // const 方法内 mutable 语义：m_needsLineRecalc 标记 mutable（缓存本就该 mutable）
	const size_t lineIndex = LineIndexFromCodepoint(m_caret);
	const size_t startByte = CodepointIndexToByteOffset(m_text, m_lineStarts[lineIndex]);
	const size_t caretByte = CodepointIndexToByteOffset(m_text, m_caret);
	// GPT 修正：统一 GetLineHeight()——不用整段文本 MeasureText 的高度（那是整段多行文本的高度，
	// 非单行行高）；全系统唯一行高来源（OnPaint/CaretIndexFromPosition/EnsureCaretVisible/Scroll 共用）
	const float lineH = GetLineHeight();
	const Size prefixSize = measurer.MeasureText(m_font, m_text.substr(startByte, caretByte - startByte));
	const float caretX = (std::min)(prefixSize.width, GetTextAreaWidth());
	const float caretY = static_cast<float>(lineIndex) * lineH - m_scrollOffsetY;
	return Point{ caretX, caretY };   // 相对控件原点（GetCaretClientGeometry 加绝对偏移）
}
```

### 9.6 双击选词（B7/C5：平台层翻译双击事实）

**架构修正（草案→定稿）**：草案写"框架内两次 Down 间隔判定（不依赖 WM_LBUTTONDBLCLK 样式）"——**修正为平台层翻译**：
- 理由：① Win32 有专用 WM_LBUTTONDBLCLK 消息 + 系统双击时间语义（标准）；② 框架内判定需时钟（平台依赖）且与系统双击语义不一致；③ 符合"Win32 负责翻译"分层
- **前置**：WindowClass 注册加 `CS_DBLCLKS` 样式（已核实当前 wc.style = 0——不加收不到双击消息）

```cpp
// MouseButtonDownEvent.h：构造加可选参数（默认 false——现有调用零破坏）
	MouseButtonDownEvent(Window* window, int mouseX, int mouseY, MouseButton button,
	                     bool isDoubleClick = false)
		: MouseButtonEvent(window, mouseX, mouseY, button), m_isDoubleClick(isDoubleClick){}

	bool IsDoubleClick() const noexcept{ return m_isDoubleClick; }
	// private: bool m_isDoubleClick;

// WindowMessageHandler.cpp：WM_LBUTTONDBLCLK 与 WM_LBUTTONDOWN 同构翻译（isDoubleClick=true）
	case WM_LBUTTONDBLCLK:
		m_host.OnEvent(MouseButtonDownEvent(window, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam),
			MouseButton::Left, true));
		return std::nullopt;
```

**TextBox 双击处理**（OnMouseButtonDown 开头）：

```cpp
	// 8.5.2：双击（平台层系统判定——WM_LBUTTONDBLCLK）→ 选词
	if (event.IsDoubleClick()){
		const Point abs = GetAbsolutePosition();
		const size_t clickIndex = CaretIndexFromPosition(
			Point{ static_cast<float>(event.GetMouseX()) - abs.x,
			       static_cast<float>(event.GetMouseY()) - abs.y });
		const auto [startCp, endCp] = GetWordBounds(clickIndex);
		m_selectionAnchor = startCp;
		m_caret = endCp;
		m_mouseDown = false;   // 双击选词后不进入拖选（拖选由后续单 Down 重新触发）
		Invalidate();
		SyncTextInputCaret();
		return;
	}
```

**GetWordBounds（分词规则锁死——GPT 修正：non-word 行为必须明确）**：

```
word 规则（C5 契约）：
  ASCII letter/digit         → 连续组成一个 word（空格/标点/\n 分隔）
  空格 / 标点 / \n           → non-word
  中文 / Emoji / 其他非 ASCII → 每个 code point 独立一个 word

行为锁死（GPT 推荐方案——用户双击非 word 字符也有稳定反馈）：
  word 字符上双击     → 选中整个 word
  non-word 字符上双击 → 选中该单个 non-word code point（如双击空格 → {5,5}？不——选中该字符本身）
  → 双击空格/标点 → 选中该空格/标点单个码点 {click, click+1}（非空选区，有视觉反馈）
  → 双击文本末尾（clickIndex == count）→ clamp 到 {count-1, count}（选最后一个字符）或 {count,count} 空——实现取 {count,count} 前先 clamp clickIndex 到 [0, count]

Emoji 说明（GPT 明确——复杂度取舍）：
  8.5.2 双击选词以 code point 为最小单位，不保证 Unicode grapheme cluster 整体选择
  （👨‍👩‍👧‍👦 = 多 code point + ZWJ，双击可能只选其中一个 code point）；
  grapheme cluster 支持留待后续文本系统增强阶段——非 bug，是明确取舍
```

```cpp
// TextBox.h private：
	/// @brief 双击位置 → 选中范围 [start, end)（C5：code point 级，不引入 grapheme cluster）
	/// @details word 字符 → 整个 word；non-word 字符（空格/标点/中文/Emoji）→ 单个 code point
	std::pair<size_t, size_t> GetWordBounds(size_t clickIndex) const;

// TextBox.cpp：
std::pair<size_t, size_t> TextBox::GetWordBounds(size_t clickIndex) const{
	const size_t count = GetCodepointCount();
	if (count == 0)
		return { 0, 0 };
	if (clickIndex >= count)
		clickIndex = count - 1;   // 点击末尾 → clamp 到最后一个字符（GPT 修正）
	const auto IsWordChar = [](char32_t cp){
		// 英文词字符 = ASCII 字母/数字；其余（空格/标点/中文/Emoji/\n）→ non-word
		return (cp >= U'a' && cp <= U'z') || (cp >= U'A' && cp <= U'Z') || (cp >= U'0' && cp <= U'9');
	};
	// 点击字符本身是否 word——决定扩展方向
	const size_t clickByte = CodepointIndexToByteOffset(m_text, clickIndex);
	const size_t clickNextByte = CodepointIndexToByteOffset(m_text, clickIndex + 1);
	const char32_t clickCp = DecodeFirstCodepoint(m_text.substr(clickByte, clickNextByte - clickByte));
	if (!IsWordChar(clickCp))
		return { clickIndex, clickIndex + 1 };   // non-word → 选中该单个 code point（GPT 锁死）
	// word 字符：向前/向后扩展到 word 边界
	size_t start = clickIndex;
	while (start > 0){
		const size_t prevByte = CodepointIndexToByteOffset(m_text, start - 1);
		const size_t curByte  = CodepointIndexToByteOffset(m_text, start);
		const char32_t prevCp = DecodeFirstCodepoint(m_text.substr(prevByte, curByte - prevByte));
		if (!IsWordChar(prevCp)) break;
		--start;
	}
	size_t end = clickIndex;
	while (end < count){
		const size_t curByte  = CodepointIndexToByteOffset(m_text, end);
		const size_t nextByte = CodepointIndexToByteOffset(m_text, end + 1);
		const char32_t curCp = DecodeFirstCodepoint(m_text.substr(curByte, nextByte - curByte));
		if (!IsWordChar(curCp)) break;
		++end;
	}
	return { start, end };
}
```

（DecodeFirstCodepoint：解码 UTF-8 首码点的小工具——**Core/UTF8.h 新增**（5.5.1.1 已删 DecodeUTF8，YAGNI 时零消费者；现在有消费者了——第二个消费者出现，正式加回）。或 GetWordBounds 内联按前导字节判定。倾向：UTF8.h 加 `DecodeFirstCodepoint(const std::string&)`——第二个消费者触发（B10 注释已预告"第二消费者必然出现"）。）

### 9.7 OnPaint 多行绘制（逐行 + Selection 逐行 + 光标 + 组合串下划线）

```cpp
// OnPaint 文本段改造（替换单行 DrawText 逻辑）：
	if (m_needsLineRecalc)
		RecalculateLines();
	const float lineH = GetLineHeight();
	const size_t lineCount = m_lineStarts.size();
	// 可视行范围（滚动裁剪——不画屏幕外的行）
	const int firstVisible = (std::max)(0, static_cast<int>(m_scrollOffsetY / lineH));
	const int lastVisible  = (std::min)(static_cast<int>(lineCount) - 1,
		static_cast<int>((m_scrollOffsetY + fh) / lineH) + 1);
	for (int li = firstVisible; li <= lastVisible; ++li){
		const size_t lineIndex = static_cast<size_t>(li);
		const auto [startCp, endCp] = LineRange(lineIndex);
		const size_t startByte = CodepointIndexToByteOffset(m_text, startCp);
		const size_t endByte   = CodepointIndexToByteOffset(m_text, endCp);
		const std::string lineText = m_text.substr(startByte, endByte - startByte);
		const float lineY = textPos.y + static_cast<float>(li) * lineH - m_scrollOffsetY;
		// ① 行内 Selection 高亮（与 8.5.1 同款：前缀测量 + clamp 可视宽）
		// ② 行文本 DrawText（textPos.x, lineY）
		// ③ 组合串下划线（8.5.1 欠账顺带补：m_isComposing 且本行含组合区间 →
		//    组合区间前缀测量 → DrawRect 底线——视觉区分临时编辑）
	}
	// 光标竖线：CalculateCaretPosition 多行版（9.5）——已含 scrollOffsetY
```

**Selection 逐行**：选中区间 [selMin, selMax] 与每行 [startCp, endCp] 求交 → 行内高亮 [max(selMin,startCp)-startCp, min(selMax,endCp)-startCp] → 前缀测量画高亮块。

**组合串下划线（8.5.1 设计承诺——§4.4 "组合串视觉区分：画下划线"当时未实现，8.5.2 随多行绘制补）**：
```cpp
	if (m_isComposing && m_compositionLength > 0){
		// 组合区间 [compositionStart, compositionStart+length) 所在行的下划线
		// 前缀测量组合区间起点/终点 → DrawRect(Rect{ startX, lineBottom - 1, width, 1 }, kCompositionUnderline)
	}
```

### 9.8 8.5.2 TestCase（F16-F29）

| # | 测试 | 断言点 |
|---|---|---|
| F16 | RecalculateLines 行分割 | "ab\ncd\ne" → m_lineStarts == {0,3,6}（码点索引） |
| F17 | Enter 插入换行 | OnKeyDown(Enter) → m_text 含 \n、光标后移 |
| F18 | 多行光标跨行 | MoveCaret(Right) 越过 \n → caret 到下一行起始 |
| F19 | CaretIndexFromPosition 行内 | "ab\ncd" 点第二行 "c" 前 → 码点 3（\n 后） |
| F20 | CaretIndexFromPosition 跨行 | 点 (x, 2*lineH) → 第二行对应码点（Y 定行正确） |
| F21 | 滚动 clamp | SetScrollOffset 越界 → clamp [0, max] |
| F22 | 双击英文选词 | "hello world" 点 "world" 中 → {6, 11} |
| F23 | 双击中文选词 | "你好世界" 点第 2 个码点 → {1, 2}（单码点独立） |
| F24 | 双击 emoji UTF-8 code point 完整选择 | "a😀b" 点 😀 → {1, 2}（😀 = 4 字节 1 码点——不被 UTF-8 byte 拆开） |
| F25 | EnsureCaretVisible | 光标到末尾（多行超可视）→ scrollOffsetY > 0 |
| F26 | 尾部换行空行（GPT） | "ab\n" → lineStarts == {0,3}；LineRange(1) == {3,3}（空尾行存在） |
| F27 | 空文本（GPT） | "" → lineStarts == {0}；lineCount == 1 |
| F28 | 连续换行（GPT） | "a\n\nb" → lineStarts == {0,2,3}（line0="a" line1="" line2="b"） |
| F29 | 双击标点/空格（GPT） | "hello, world" 分别点 hello/逗号/空格/world → word 选整词、逗号/空格选单码点 {5,6}/{6,7} |
| F30 | UTF-8 多字节换行索引（GPT） | "你\n好" → lineStarts == {0,2}（"你" 3 字节但 1 码点——证明 m_lineStarts 是 code point 非 byte） |

**说明**：CaretIndexFromPosition/GetWordBounds/RecalculateLines 涉及测量（GetWindow()->GetTextMeasurer）——无窗口测试环境 GetWindow()==nullptr。**处理**（GPT 修正——不用"固定行高短路"伪造 TextMeasurer，污染真实实现）：
- **纯逻辑层（无窗口可测）**：RecalculateLines（F16/F26-F28/F30）、GetWordBounds（F22-F24/F29）、**Y→LineIndex**（F19a/F20a——CaretIndexFromPosition 的 Y 定行部分，经 m_lineStarts 纯码点可测）
- **测量层（最小窗口集成待办，同 7.2 遗留）**：**X→Caret 行内定位**（F19b/F20b——依赖 TextMeasurer 前缀测量，需窗口）

### 9.8.1 采纳的观察项（GPT 第二轮——非 blocker，明确记录）

- **水平溢出契约**：单行水平溢出**暂不提供水平滚动**；`CalculateCaretPosition` 的 Caret X 与绘制区域右边界 clamp（`min(prefixWidth, GetTextAreaWidth())`）——未来做水平滚动时此契约是起点
- **MouseWheelEvent 原始 delta**：8.5.2 接受 Win32 原始 WHEEL_DELTA（120）；架构上最终应 framework-normalized（`Win32 → MouseWheelEvent(标准化) → TextBox`，TextBox 不应知道 120）——标注待 Event 层演进，当前不改
- **组合串下划线（8.5.1 设计承诺补欠账）**：随多行绘制落地——组合区间前缀测量 + 底线 DrawRect（9.7 ③）

### 9.9 视觉验证计划（用户 VS 编译运行）

1. **多行输入**：Enter 换行、多行显示（行距正常）、光标跨行移动、Backspace 删 \n
2. **滚动**：滚轮上下滚动（内容移动 + 光标跟随）、光标在可视区外时编辑自动滚回
3. **Selection 多行**：拖选跨行高亮正确
4. **双击**：英文双击选词、中文双击单字、emoji 整体
5. **IME 多行**：第二行输入拼音——组合串内嵌 + 候选窗跟随（滚动联动）
6. **回归**：单行文本（无 \n）行为与 8.5.1 一致（滚动无感、双击无感）

### 9.10 8.5.2 修订记录

- v1.2（2026-08-24）GPT 第二轮评审整合（"可进入实现前最终冻结，不需再重新设计"）：**LineRange 实现伪代码**（end = 下一行起始 - 1 或 count，含验证表）；**GetMaxScrollOffset 公式**（行数×固定行高 - 可视高，含最后一行完整行高）+ GetTextAreaHeight 辅助；**EnsureCaretVisible 上下边界语义**（上：滚到光标顶；下：滚到光标底；统一 clamp）；**固定 lineH 契约**（8.5.2 不支持逐行字体——Y/lineH 成立）；F24 措辞改"UTF-8 code point 完整选择"（代理对是 UTF-16 概念）；**F30 UTF-8 多字节换行索引**（"你\n好"→{0,2}）；F19/F20 拆两层（Y→LineIndex 纯逻辑可测 / X→Caret 最小窗口集成——不用固定行高短路污染实现）；新增 §9.8.1 采纳观察项（水平溢出契约/MouseWheel 120 原始 delta/组合串下划线）。
- v1.1（2026-08-24）GPT 评审整合（"基本通过，建议小修后定稿"）：RecalculateLines 改 **DecodeFirstCodepoint** 码点解码（非 m_text[index]）；LineRange **尾部 \n 空行契约**明确（"ab\n" → line1=[3,3]）+ Y 映射契约（"任意 Y → 最近有效行"）；CalculateCaretPosition **统一 GetLineHeight()**（不用整段文本 MeasureText 高度）；GetTextLeftInset 定义**单一来源**（文本绘制起点 X，非偏移相加）；GetWordBounds **non-word → 选单码点**行为锁死 + clickIndex clamp + **Emoji code point 级取舍说明**；TestCase 补 F26（尾部换行）/F27（空文本）/F28（连续换行）/F29（双击标点空格）。
- v1.0（2026-08-24）8.5.2 定稿：多行模型（RecalculateLines/LineRange）+ 滚动（OnMouseWheel/EnsureCaretVisible）+ CaretIndexFromPosition（B9）+ 双击选词（**平台层 WM_LBUTTONDBLCLK 翻译——草案"框架内判定"修正**，WindowClass 需 CS_DBLCLKS）+ 多行绘制（Selection 逐行 + 组合串下划线补欠账）+ F16-F25。
