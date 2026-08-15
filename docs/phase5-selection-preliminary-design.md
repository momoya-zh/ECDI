# Phase 5.5.2 Selection + 修饰键 初步设计

> 状态：v1.0（2026-08-14）｜初步设计完成，待详细设计
> 相关：phase5-selection-requirements.md（职责确认 v1.0）/ phase5-textbox-*.md（5.5）

## 1. 定稿决策（P1-P8 + GPT 修正）

### P1 KeyModifier 独立头 —— A ✅

**新头** `EventSystem/Input/KeyBoard/KeyModifier.h`（与 KeyCode.h 分离——**KeyCode=按了哪个键 / KeyModifier=修饰状态，两概念不同**，GPT 赞成）：

```cpp
#pragma once

namespace ECDI{

/// @brief 键盘修饰键（位标志，可组合——Ctrl+Shift+A 未来自然支持）
enum class KeyModifier{
	None  = 0,
	Shift = 1,
	Ctrl  = 2,
	Alt   = 4,
};

}
```

**KeyEvent.h** 扩展：

```cpp
class KeyEvent : public InputEvent{
public:
	KeyCode GetKeyCode() const noexcept;
	/// @brief 是否按下指定修饰键组合（位与判断——HasModifier(Ctrl | Shift) 可组合查询）
	bool HasModifier(KeyModifier modifier) const noexcept;
	bool IsShiftDown() const noexcept { return HasModifier(KeyModifier::Shift); }
	bool IsCtrlDown() const noexcept { return HasModifier(KeyModifier::Ctrl); }
	bool IsAltDown() const noexcept { return HasModifier(KeyModifier::Alt); }
protected:
	KeyEvent(Window* window, KeyCode keyCode, KeyModifier modifier);
private:
	KeyCode m_keyCode;
	KeyModifier m_modifier;
};
```

- KeyDownEvent/KeyUpEvent 构造加 modifier 参数
- `HasModifier` 实现：`(static_cast<int>(m_modifier) & static_cast<int>(modifier)) == static_cast<int>(modifier)`

### P2 翻译器填位 —— A ✅（局部 lambda）

```cpp
// WindowMessageHandler.cpp WM_KEYDOWN/WM_KEYUP case 内：
// 平台翻译器内查询修饰状态（GetKeyState）——分层允许（"Win32 负责翻译"本职）；
// 各平台实现不同（X11 XQueryKeymap/Wayland/macOS NSEvent），不抽公共
const auto TranslateModifier = []() -> KeyModifier{
	KeyModifier m = KeyModifier::None;
	const auto AddIfDown = [&m](int vk, KeyModifier mod){
		if (GetKeyState(vk) & 0x8000)
			m = static_cast<KeyModifier>(static_cast<int>(m) | static_cast<int>(mod));
	};
	AddIfDown(VK_SHIFT,   KeyModifier::Shift);
	AddIfDown(VK_CONTROL, KeyModifier::Ctrl);
	AddIfDown(VK_MENU,    KeyModifier::Alt);
	return m;
};
KeyDownEvent event(window, TranslateKeyCode(wParam, lParam), TranslateModifier());
```

### P3 HandleKeyDown Shift+Tab —— A ✅

```cpp
void Window::HandleKeyDown(const KeyDownEvent& event){
	if (event.GetKeyCode() == KeyCode::Tab){
		FocusNext(event.IsShiftDown() ? -1 : 1);   // 5.5.2：Shift+Tab 反向（5.4 债务落地）
		return;
	}
	if (m_focusedWidget) m_focusedWidget->OnKeyDown(event);
}
```

### P4 Selection 数据模型 —— A ✅（GPT ⭐⭐⭐⭐⭐：anchor + caret）

```cpp
// TextBox.h private：
size_t m_selectionAnchor = 0;   ///< 选择锚点（固定端；无选择时无意义）
bool m_mouseDown = false;       ///< 鼠标按下（拖选进行中——Capture ≠ 拖选中，两状态分离）

// 辅助（private，与 5.5.1.4 CaretIndexFromX 同决策——内部算法不暴露）：
bool HasSelection() const noexcept;              // m_selectionAnchor != m_caret
size_t GetSelectionMin() const noexcept;         // min(anchor, caret)——绘制/删除用
size_t GetSelectionMax() const noexcept;         // max(anchor, caret)
size_t DeleteSelection();                        // 删选中区 + 返回新光标位置（min 处）
void ClearSelection() noexcept;                  // anchor = m_caret（无效化）
```

- **语义**：光标 = active 端；Selection 会扩张会收缩（`ab|cdef → Shift+→ → ab[cd]ef → Shift+← → ab[c]def → Shift+← → ab|cdef`）——start/end 表达不了，anchor/caret 自然

### P5 拖选事件 —— A ✅

```cpp
// OnMouseButtonDown（5.5.1.4 既有 + 加锚点/按下）：
	m_caret = CaretIndexFromX(measurer, innerX);
	m_selectionAnchor = m_caret;   // 锚点 = 点击处（准备拖）
	m_mouseDown = true;
	Invalidate();

// TextBox.h 新增 override：
void OnMouseMove(const MouseMoveEvent& event) override;
void OnMouseButtonUp(const MouseButtonUpEvent&) override;

// TextBox.cpp：
void TextBox::OnMouseMove(const MouseMoveEvent& event){
	if (!m_mouseDown) return;   // 非拖选中（普通移动忽略）
	// 同源定位（5.5.1.4 同款）……
	m_caret = CaretIndexFromX(measurer, innerX);   // active 端跟随
	Invalidate();
}
void TextBox::OnMouseButtonUp(const MouseButtonUpEvent&){
	m_mouseDown = false;   // 结束拖选（保留选择）
}
```

### P6 编辑操作内部处理 Selection（GPT 必须保留——编辑操作自包含）—— A ✅

```cpp
// InsertCodepoint：
void TextBox::InsertCodepoint(char32_t codepoint){
	if (HasSelection()) m_caret = DeleteSelection();   // 有选择先删选中区
	const size_t byte = CodepointIndexToByteOffset(m_text, m_caret);
	m_text.insert(byte, EncodeUTF8(codepoint));
	++m_caret;
	Invalidate();
}

// DeleteBackward：
void TextBox::DeleteBackward(){
	if (HasSelection()){ m_caret = DeleteSelection(); Invalidate(); return; }   // 一次删完
	if (m_caret == 0) return;
	// ... 原单字符逻辑
}

// DeleteForward：对称
void TextBox::DeleteForward(){
	if (HasSelection()){ m_caret = DeleteSelection(); Invalidate(); return; }
	// ...
}

// DeleteSelection（私有）：
size_t TextBox::DeleteSelection(){
	const size_t minCp = GetSelectionMin();
	const size_t maxCp = GetSelectionMax();
	const size_t minByte = CodepointIndexToByteOffset(m_text, minCp);
	const size_t maxByte = CodepointIndexToByteOffset(m_text, maxCp);
	m_text.erase(minByte, maxByte - minByte);
	ClearSelection();   // anchor 无效化
	return minCp;       // 新光标 = 删除起始处
}
```

- **为什么在编辑操作内部**（GPT）：未来 IME 上屏 / Ctrl+V / 程序调用 / 脚本 全走编辑操作——编辑操作必须自包含，事件层判断会漏

### P7 高亮绘制 —— A ✅（颜色不写死，GPT 零成本优化）

```cpp
// TextBox.cpp 匿名 namespace：
namespace{
	const Color kSelectionColor = Color::FromRGBA8(173, 216, 230);   ///< 选择高亮浅蓝（主题友好——未来 ThemeSystem 替换）
}

// OnPaint：白底 → 高亮 → 文本 → 光标
if (HasSelection()){
	const size_t minByte = CodepointIndexToByteOffset(m_text, GetSelectionMin());
	const size_t maxByte = CodepointIndexToByteOffset(m_text, GetSelectionMax());
	const Size minSize = ctx.MeasureText(m_font, m_text.substr(0, minByte));
	const Size maxSize = ctx.MeasureText(m_font, m_text.substr(0, maxByte));
	// 与文本裁切共享 maxTextWidth 钳制（高亮不溢出，GPT S5 要求）
	const float hlMin = (std::min)(minSize.width, maxTextWidth);
	const float hlMax = (std::min)(maxSize.width, maxTextWidth);
	ctx.DrawRect(Rect{ textPos.x + hlMin, textPos.y, hlMax - hlMin, lineH }, kSelectionColor);
}
```

### P8 验证 —— A ✅（GPT 要求②：不彻底放弃断言）

- **人工交互验证**（项目一贯策略）：拖选高亮 / Shift+方向键含反向收缩 / 输入替换选中区 / Backspace 删选中区 / 点击取消选择 / Shift+Tab 反向
- **不新增测试 API、不 private 变 public**（GPT 明确）
- **记账（债务）**：Phase 7 建立完整测试体系时补 Selection 单元测试（外部行为：`选中 cd + 输入 中 → ab中e` 等）

## 2. 修订记录

- v1.0（2026-08-14）初步设计：P1-P8 定稿。GPT 全部批准 + 两个要求落地：① P6 编辑操作自包含（必须项）② P8 人工验证 + Phase 7 补测记账；P7 颜色不写死（kSelectionColor 匿名 namespace，主题友好）。
