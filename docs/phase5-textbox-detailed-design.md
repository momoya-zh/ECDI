# Phase 5.5 TextBox 详细设计

> 状态：v1.0（2026-08-13）｜5.5.1.1 部分定稿，5.5.1.2-5.5.1.4 随进度补充
> 相关：phase5-textbox-requirements.md（职责确认 v1.0）/ phase5-textbox-preliminary-design.md（初步设计 v1.0）

## 1. 实施结构（4 个 commit）

```
5.5.1.1  Window::GetTextMeasurer() + Core/UTF8.h/.cpp（3 函数）+ main.cpp AppendUTF8 迁移  ← 本篇
5.5.1.2  TextBox 骨架（类定义 + 事件 override 空实现 + CanFocus + 焦点可见性）
5.5.1.3  编辑逻辑（Insert/DeleteBackward/DeleteForward/MoveCaret/Home/End + 断言段）
5.5.1.4  Paint（白底/焦点框/文本/光标同源流程）+ 鼠标点击定位 + main.cpp 控件接入
```

## 2. 5.5.1.1 详细设计

### D1 Core/UTF8.h（新文件，K&R + BOM + include 规范）

```cpp
#pragma once

#include <cstddef>
#include <string>

namespace ECDI{

/// @brief UTF-8 编码与码点索引工具（5.5 P6：第二消费者必然出现——IME/剪贴板/多行）
/// @details UTF-8 变长：ASCII 1 字节 / 中文 3 字节 / emoji 4 字节——
/// 码点索引 ≠ 字节偏移，索引转换是 TextBox 光标/删除的正确性前提。

/// @brief 码点 → UTF-8 编码（返回 1-4 字节字符串）
/// @pre codepoint 合法（≤ 0x10FFFF 且非代理区 0xD800-0xDFFF）——调用方保证
///       （TextBox 输入来自 CharInputEvent，翻译器已组合合法码点）
std::string EncodeUTF8(char32_t codepoint);

/// @brief 码点索引 → 字节偏移（遍历跳过 index 个码点）
/// @return 对应字节偏移；index ≥ 码点数时返回 text.size()（自然钳制到末尾）
size_t CodepointIndexToByteOffset(const std::string& text, size_t codepointIndex);

/// @brief 字节偏移 → 码点索引（0 到 byteOffset 之间完整经过的码点数）
/// @pre byteOffset 必须位于 UTF-8 码点边界（非边界输入属未定义行为）
///      —— 鼠标点击定位可能产生非边界偏移，调用方需自行钳制
size_t ByteOffsetToCodepointIndex(const std::string& text, size_t byteOffset);

}
```

- **命名**：ECDI 顶层函数（与 Core/String.h 的 UTF8ToWide/WideToUTF8 风格一致，不引入 namespace UTF8 嵌套）
- **DecodeUTF8 已删**（GPT YAGNI 修正）：当前零消费者；5.6 IME 逐码点遍历时再加（DecodeUTF8 / IsLeadByte / IsContinuationByte）
- include `<cstddef>`（size_t）+ `<string>`——绝对路径、带空格、标准库在后

### D2 Core/UTF8.cpp（实现 + 匿名 namespace 辅助）

```cpp
#include "ECDI/Core/UTF8.h"

namespace ECDI{

namespace{   // 匿名 namespace：内部辅助（不暴露）

/// @brief 按前导字节判定 UTF-8 序列长度（非法前导按 1 处理——避免遍历死循环）
/// @param lead unsigned char：char 在 MSVC 有符号，0xF0 等高位字节会变负、整数提升隐患
size_t SequenceLength(unsigned char lead) noexcept{
	if ((lead & 0x80) == 0)      return 1;
	if ((lead & 0xE0) == 0xC0)   return 2;
	if ((lead & 0xF0) == 0xE0)   return 3;
	if ((lead & 0xF8) == 0xF0)   return 4;
	return 1;   // 连续字节 10xxxxxx / 5-6 字节 11111xxx：非法，按 1 跳过
}

}

std::string EncodeUTF8(char32_t codepoint){
	if (codepoint <= 0x7F)
		return std::string(1, static_cast<char>(codepoint));
	if (codepoint <= 0x7FF)
		return { static_cast<char>(0xC0 | (codepoint >> 6)),
		         static_cast<char>(0x80 | (codepoint & 0x3F)) };
	if (codepoint <= 0xFFFF)
		return { static_cast<char>(0xE0 | (codepoint >> 12)),
		         static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)),
		         static_cast<char>(0x80 | (codepoint & 0x3F)) };
	return { static_cast<char>(0xF0 | (codepoint >> 18)),
	         static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)),
	         static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)),
	         static_cast<char>(0x80 | (codepoint & 0x3F)) };
}

size_t CodepointIndexToByteOffset(const std::string& text, size_t codepointIndex){
	size_t byteOffset = 0;
	size_t cpIndex = 0;
	while (byteOffset < text.size() && cpIndex < codepointIndex){
		byteOffset += SequenceLength(static_cast<unsigned char>(text[byteOffset]));
		++cpIndex;
	}
	return byteOffset;   // index 超界 → 停在 text.size()（自然钳制）
}

size_t ByteOffsetToCodepointIndex(const std::string& text, size_t byteOffset){
	size_t cpIndex = 0;
	size_t pos = 0;
	while (pos < text.size() && pos < byteOffset){
		pos += SequenceLength(static_cast<unsigned char>(text[pos]));
		++cpIndex;
	}
	return cpIndex;
}

}
```

### D3 Window::GetTextMeasurer（T1 落地）

**Window.h**：include 区加 `"ECDI/Render/TextMeasurer.h"`（IWYU——直接返回 TextMeasurer&）；公共区（Invalidate 附近）加：

```cpp
/// @brief 获取文本测量器（5.5 T1；GDIBackend 兼 TextMeasurer——返回抽象接口不暴露后端）
/// @details 控件经 protected GetWindow() 获取——非 Paint 时刻测量（点击定位光标等）
TextMeasurer& GetTextMeasurer() noexcept;
```

**Window.cpp**：`TextMeasurer& Window::GetTextMeasurer() noexcept { return m_backend; }`

### D4 main.cpp AppendUTF8 迁移

- **删除** 36-61 行 `AppendUTF8` 函数（4 分支编码逻辑整体移除——被 Core 工具正式化）
- **OnCharInput**（73 行）：`AppendUTF8(utf8, event.GetCodepoint());` → `utf8 += ECDI::EncodeUTF8(event.GetCodepoint());`
- include 区加 `"ECDI/Core/UTF8.h"`（main.cpp 带空格 include 风格）

### D5 构建与断言

- **vcxproj**：ClCompile 加 `src\Core\UTF8.cpp`；ClInclude 加 `Include\ECDI\Core\UTF8.h`
- **CMake**：src GLOB 自动收（CONFIGURE_DEPENDS 已配）
- **断言段**（main.cpp 测试区，UTF-8 工具自测——字节布局：a=1 / 中=3 / 😀=4，总 8）：

```cpp
// ── 5.5.1.1 UTF-8 工具自测（码点↔字节转换正确性）──
{
	FRAMEWORK_ASSERT(ECDI::EncodeUTF8(U'A') == "A");
	FRAMEWORK_ASSERT(ECDI::EncodeUTF8(U'中') == "\xE4\xB8\xAD");     // 3 字节
	FRAMEWORK_ASSERT(ECDI::EncodeUTF8(U'😀') == "\xF0\x9F\x98\x80"); // 4 字节

	const std::string s = "a中😀";
	FRAMEWORK_ASSERT(ECDI::CodepointIndexToByteOffset(s, 0) == 0);
	FRAMEWORK_ASSERT(ECDI::CodepointIndexToByteOffset(s, 1) == 1);   // a=1
	FRAMEWORK_ASSERT(ECDI::CodepointIndexToByteOffset(s, 2) == 4);   // 中=3
	FRAMEWORK_ASSERT(ECDI::CodepointIndexToByteOffset(s, 3) == 8);   // 😀=4 → 总 8
	FRAMEWORK_ASSERT(ECDI::ByteOffsetToCodepointIndex(s, 4) == 2);
	FRAMEWORK_ASSERT(ECDI::ByteOffsetToCodepointIndex(s, 8) == 3);
}
```

## 3. 修订记录

- v1.0（2026-08-13）5.5.1.1 定稿：D1-D5。**DecodeUTF8 删除**（GPT YAGNI 修正——零消费者，5.6 IME 时再加）；**SequenceLength 参数改 unsigned char**（GPT 符号修正——MSVC char 有符号）；**ByteOffsetToCodepointIndex @pre 明确**（非边界 UB，鼠标定位需调用方钳制）；断言字节布局修正（3 → 8，GPT 数学确认）；命名 ECDI 顶层函数（与 UTF8ToWide 风格一致，用户确认）。

## 4. 5.5.1.3 详细设计（编辑逻辑）

### E1 类定义补充（TextBox.h，GPT 修正）

```cpp
public:
	// ── 光标方向（类型即文档：MoveCaret(-1) 的 -1 是什么？——GPT 可读性修正）──
	enum class CaretDirection{ Left, Right };

	// 编辑操作中：
	void MoveCaret(CaretDirection direction);   // 原 MoveCaret(int)

private:
	/// @brief 码点总数（私有辅助——消除 DeleteForward/MoveCaret/MoveCaretToEnd 三处重复统计）
	size_t GetCodepointCount() const;           // = ByteOffsetToCodepointIndex(m_text, m_text.size())
```

### E2 编辑操作实现（TextBox.cpp 替换占位）

```cpp
void TextBox::InsertCodepoint(char32_t codepoint){
	const size_t byte = CodepointIndexToByteOffset(m_text, m_caret);
	m_text.insert(byte, EncodeUTF8(codepoint));
	++m_caret;
	Invalidate();   // 职责契约：修改了可见状态 → 自身负责请求重绘（GPT 论证，非平台合并）
}

void TextBox::DeleteBackward(){
	if (m_caret == 0) return;                       // 头边界
	const size_t cur = CodepointIndexToByteOffset(m_text, m_caret);
	const size_t prev = CodepointIndexToByteOffset(m_text, m_caret - 1);
	m_text.erase(prev, cur - prev);                 // 删前一个码点的字节区间
	--m_caret;
	Invalidate();
}

void TextBox::DeleteForward(){
	if (m_caret >= GetCodepointCount()) return;     // 尾边界
	const size_t cur = CodepointIndexToByteOffset(m_text, m_caret);
	const size_t next = CodepointIndexToByteOffset(m_text, m_caret + 1);
	m_text.erase(cur, next - cur);
	Invalidate();
}

void TextBox::MoveCaret(CaretDirection direction){
	const size_t count = GetCodepointCount();
	if (direction == CaretDirection::Left){ if (m_caret > 0) --m_caret; }
	else                                  { if (m_caret < count) ++m_caret; }
	Invalidate();
}

void TextBox::MoveCaretToStart(){ m_caret = 0; Invalidate(); }

void TextBox::MoveCaretToEnd(){ m_caret = GetCodepointCount(); Invalidate(); }

size_t TextBox::GetCodepointCount() const{
	return ByteOffsetToCodepointIndex(m_text, m_text.size());
}
```

### E3 事件映射（替换占位）

```cpp
void TextBox::OnKeyDown(const KeyDownEvent& event){
	switch (event.GetKeyCode()){
	case KeyCode::Backspace: DeleteBackward(); break;
	case KeyCode::Delete:    DeleteForward();  break;
	case KeyCode::Left:      MoveCaret(CaretDirection::Left);  break;
	case KeyCode::Right:     MoveCaret(CaretDirection::Right); break;
	case KeyCode::Home:      MoveCaretToStart(); break;
	case KeyCode::End:       MoveCaretToEnd();   break;
	default: break;   // 其他键（含 Tab——被 Window::HandleKeyDown 拦截，到不了这里）
	}
}

void TextBox::OnCharInput(const CharInputEvent& event){
	const char32_t cp = event.GetCodepoint();
	// 过滤控制字符（Event 原则"语义判断推迟给消费者"——TextBox 就是消费者）：
	// Backspace(0x08)/Delete(0x7F) 的 WM_CHAR 通道丢弃（编辑走 OnKeyDown 的 KeyCode 路径）
	if (cp < 0x20 || cp == 0x7F) return;
	InsertCodepoint(cp);
}
```

- include 新增（TextBox.cpp）：KeyDownEvent.h / CharInputEvent.h / KeyCode.h / Core/UTF8.h

### E4 main.cpp 断言段（5.5.1.1 段后）

```cpp
// ── 5.5.1.3 TextBox 编辑逻辑：Insert/Delete/Move（不依赖窗口；emoji/中文不切字）──
{
	ECDI::TextBox box("abc");
	box.MoveCaretToEnd();
	box.InsertCodepoint(U'😀');                 // emoji 4 字节
	FRAMEWORK_ASSERT(box.GetText() == "abc😀");
	FRAMEWORK_ASSERT(box.GetCaret() == 4);
	box.DeleteBackward();                       // 删 😀（删前一码点字节区间）
	FRAMEWORK_ASSERT(box.GetText() == "abc");
	FRAMEWORK_ASSERT(box.GetCaret() == 3);

	box.MoveCaret(ECDI::TextBox::CaretDirection::Left);  // 光标到 'c' 后
	box.InsertCodepoint(U'中');                 // 中文 3 字节
	FRAMEWORK_ASSERT(box.GetText() == "ab中c");
	FRAMEWORK_ASSERT(box.GetCaret() == 3);

	box.DeleteForward();                        // 删光标后 = 'c'
	FRAMEWORK_ASSERT(box.GetText() == "ab中");
	FRAMEWORK_ASSERT(box.GetCaret() == 3);

	box.MoveCaretToStart();
	FRAMEWORK_ASSERT(box.GetCaret() == 0);
	box.MoveCaret(ECDI::TextBox::CaretDirection::Left);   // 头边界钳制
	FRAMEWORK_ASSERT(box.GetCaret() == 0);
	box.MoveCaretToEnd();
	box.MoveCaret(ECDI::TextBox::CaretDirection::Right);  // 尾边界钳制
	FRAMEWORK_ASSERT(box.GetCaret() == 3);
}
```

### E5 债务记录（Invalidate 内嵌——GPT 最终论证 + 用户分层纪律）

- **设计**：编辑操作内嵌 `Invalidate()`——理由 = **职责契约**（修改可见状态 → 自身负责请求重绘，防职责泄漏），**不依赖平台重绘合并**（第 21 条分层论证纪律）
- **解耦时机 = Phase 7 API 审查**：出现批量编辑（PasteText/ReplaceSelection/SetText 大文本）时改两层结构（`XxxInternal` 无重绘 + 对外 API 负责刷新）——与编辑操作可见性审查同一批
- 当前全是单步操作，YAGNI

- v1.1（2026-08-13）5.5.1.3 定稿：E1-E5。CaretDirection 枚举 + GetCodepointCount 辅助（GPT 可读性/重复统计修正）；Invalidate 内嵌保留（职责契约论证，解耦归 Phase 7）；事件占位改实现（OnKeyDown/OnCharInput）。

## 5. 5.5.1.4 详细设计（Paint 完整版 + 鼠标点击定位）

### F1 OnPaint 完整版（文本 + 光标同源）

```cpp
void TextBox::OnPaint(PaintContext& ctx, int x, int y){
	const float fx = static_cast<float>(x);
	const float fy = static_cast<float>(y);
	const float fw = static_cast<float>(GetWidth());
	const float fh = static_cast<float>(GetHeight());

	// 1. 白底 + 焦点框（骨架已有）
	if (HasFocus()){
		ctx.DrawRect(Rect{ fx, fy, fw, fh }, Color::FromRGBA8(80, 120, 220));
		ctx.DrawRect(Rect{ fx + 2, fy + 2, fw - 4, fh - 4 }, Color::White());
	}
	else{
		ctx.DrawRect(Rect{ fx, fy, fw, fh }, Color::White());
	}

	// 2. 文本 + 光标（同源：一次 MeasureText 宽高 → CalculateTextPosition 起点）
	// ⚠️ 不用 DrawTextContent——TextBox 需要文本起点/尺寸（光标计算），DrawTextContent 不返回中间值
	if (m_text.empty() && !m_showCaret)
		return;   // 空文本且无光标：省测量

	const Size textSize = ctx.MeasureText(m_font, m_text);
	// ⚠️ 空串高度兜底：GDIBackend::MeasureText("") 返回 {0,0}（GetTextExtentPoint32W 空串 cy=0）
	// → CalculateTextPosition 的 offsetY=(fh-0)/2 垂直定位偏下——空文本+焦点时光标竖线用行高
	const float lineH = (textSize.height > 0.0f) ? textSize.height : ctx.LineHeight(m_font);
	const Point textPos = CalculateTextPosition(x, y, textSize.width, lineH);   // 同源

	if (!m_text.empty())
		ctx.DrawText(textPos, m_text, m_textColor, m_font);

	// 3. 光标竖线（与文本起点同源——不直接用 x）
	if (m_showCaret){
		const size_t byteOffset = CodepointIndexToByteOffset(m_text, m_caret);
		const Size prefixSize = ctx.MeasureText(m_font, m_text.substr(0, byteOffset));
		ctx.DrawRect(Rect{ textPos.x + prefixSize.width, textPos.y, 2.0f, lineH }, Color::Black());
	}
}
```

### F2 OnMouseButtonDown（P1 测量服务首个消费者 + 同源原则）

```cpp
void TextBox::OnMouseButtonDown(const MouseButtonDownEvent& event){
	// 焦点获取由 Application 前置处理（5.4.2）
	// 坐标系：事件 GetMouseX = 窗口客户区绝对；GetAbsolutePosition = 客户区绝对
	const Point abs = GetAbsolutePosition();
	TextMeasurer& measurer = GetWindow()->GetTextMeasurer();   // T1 首个消费者——非 Paint 时刻测量
	const Size textSize = measurer.MeasureText(m_font, m_text);
	// 与 OnPaint 完全同源：同一个 CalculateTextPosition → 同一个文本起点
	// （点击定位与绘制共享同一坐标系——未来改居中/内边距/滚动不偏，GPT D2 原则）
	const Point textPos = CalculateTextPosition(
		static_cast<int>(abs.x), static_cast<int>(abs.y),
		textSize.width, textSize.height);
	const float innerX = static_cast<float>(event.GetMouseX()) - textPos.x;   // 相对文本起点
	m_caret = CaretIndexFromX(measurer, innerX);
	Invalidate();
}
```

### F3 CaretIndexFromX（private 成员——GPT 最终采纳：5.5.2 Selection 必然复用，是"TextBox 坐标定位算法"）

```cpp
// TextBox.h private：
/// @brief 文本内 x 偏移 → 最近码点索引（点击定位算法——5.5.2 Selection 拖选/双击/Shift+单击复用）
/// @param innerX 相对文本起点的 x（与绘制同源）
size_t CaretIndexFromX(TextMeasurer& measurer, float innerX) const;

// TextBox.cpp：
size_t TextBox::CaretIndexFromX(TextMeasurer& measurer, float innerX) const{
	if (m_text.empty()) return 0;
	const size_t count = GetCodepointCount();
	// MVP：线性扫描 + 前缀测量（O(n²)——单行短文本够用）；
	// 未来长文本/多行/滚动：缓存码点边界或二分查找（性能注释）
	for (size_t i = 0; i < count; ++i){
		const size_t byte = CodepointIndexToByteOffset(m_text, i + 1);
		const Size prefix = measurer.MeasureText(m_font, m_text.substr(0, byte));
		if (innerX <= prefix.width){
			// 落在第 i 个码点区间：中点判断四舍五入（左半 → i，右半 → i+1）
			const Size prevPrefix = (i == 0) ? Size{}
				: measurer.MeasureText(m_font, m_text.substr(0, CodepointIndexToByteOffset(m_text, i)));
			const float charWidth = prefix.width - prevPrefix.width;
			return (innerX >= prevPrefix.width + charWidth / 2.0f) ? i + 1 : i;
		}
	}
	return count;   // 点在末尾之后
}
```

### F4 main.cpp（D4 GPT 建议：删断言改人工交互验证）

- TextBox 预填 `TextBox("Hello")`（文本可见，便于看光标初始位置）
- **人工交互验证**（项目一贯测试策略——Button 点击/Tab/Capture/焦点全如此）：
  - 点击 "H" 左侧 → 光标最左边
  - 点击 "e" 右侧 → 光标在 e 后
  - 点击文本末尾 → 光标在末尾
  - 打字 → 文本实时显示 + 光标跟随
  - Backspace/Delete/←→/Home/End → 光标移动 + 文本变化

### F5 事件占位收尾

- OnMouseButtonDown 的 FRAMEWORK_ASSERT(false) → F2 真实实现（点击不再弹断言——遗留问题自然解决）
- include 新增：MouseButtonDownEvent.h（TextBox.cpp）

- v1.2（2026-08-13）5.5.1.4 定稿：F1-F5。**同源原则**（点击定位与绘制共享 CalculateTextPosition——GPT D2 真问题）；**空串高度兜底**（实测 GDIBackend::MeasureText("") 返回 {0,0}，GPT 担忧属实）；CaretIndexFromX **private 成员**（GPT 最终采纳——5.5.2 Selection 复用）；**删断言改人工验证**（GPT 建议 + private 可测性矛盾解）；性能注释（O(n²) MVP 取舍）。
