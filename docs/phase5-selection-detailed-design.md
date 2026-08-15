# Phase 5.5.2 Selection + 修饰键 详细设计

> 状态：v1.0（2026-08-14）｜详细设计完成，进入实现
> 相关：phase5-selection-requirements.md（职责确认 v1.0）/ phase5-selection-preliminary-design.md（初步设计 v1.0）

## 1. 改动清单（9 文件）

| # | 文件 | 改动 |
|---|------|------|
| D1 | **KeyModifier.h**（新头） | 枚举 + `operator\|` 位运算配套（位标志惯例） |
| D2 | **KeyEvent.h** | inline 扩展：`m_modifier` 成员 + HasModifier/IsShiftDown/IsCtrlDown/IsAltDown + 构造加参（**全 inline 无 .cpp**——两成员几个访问器） |
| D3 | **KeyDownEvent.h / KeyUpEvent.h** | 构造加 `KeyModifier modifier` 参数（转发 KeyEvent） |
| D4 | **WindowMessageHandler.cpp** | 匿名 namespace `TranslateModifier()`（GetKeyState 填位——WM_KEYDOWN/WM_KEYUP 两分支共用，非分支内 lambda）；两处构造加第三参 |
| D5 | **Window.cpp** | HandleKeyDown：Tab → `FocusNext(event.IsShiftDown() ? -1 : 1)` |
| D6 | **TextBox.h** | private：`m_selectionAnchor` / `m_mouseDown` + 5 辅助声明（HasSelection/Min/Max/DeleteSelection/ClearSelection）；protected：OnMouseMove/OnMouseButtonUp override |
| D7 | **TextBox.cpp** | 7 项实现（见 §3） |
| D8 | **main.cpp** | TextBox 预填 `"Hello World, this is a very long text."`（20-30 字符——验证拖选/裁切/交互） |
| D9 | **vcxproj** | ClInclude 加 KeyModifier.h |

## 2. 关键实现（D1-D5）

### D1 KeyModifier.h

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

/// @brief 位或（位标志惯例配套——m = m | KeyModifier::Shift 免 static_cast 噪音）
constexpr KeyModifier operator|(KeyModifier lhs, KeyModifier rhs){
	return static_cast<KeyModifier>(static_cast<int>(lhs) | static_cast<int>(rhs));
}

}
```

### D2 KeyEvent.h（全 inline）

```cpp
#include "ECDI/EventSystem/Input/KeyBoard/KeyModifier.h"

class KeyEvent : public InputEvent{
public:
	KeyCode GetKeyCode() const noexcept{ return m_keyCode; }
	/// @brief 是否按下指定修饰键组合（位与判断——HasModifier(Ctrl | Shift) 可组合查询）
	bool HasModifier(KeyModifier modifier) const noexcept{
		return (static_cast<int>(m_modifier) & static_cast<int>(modifier)) == static_cast<int>(modifier);
	}
	bool IsShiftDown() const noexcept{ return HasModifier(KeyModifier::Shift); }
	bool IsCtrlDown() const noexcept{ return HasModifier(KeyModifier::Ctrl); }
	bool IsAltDown() const noexcept{ return HasModifier(KeyModifier::Alt); }
protected:
	KeyEvent(Window* window, KeyCode keyCode, KeyModifier modifier)
		: InputEvent(window), m_keyCode(keyCode), m_modifier(modifier){}
private:
	KeyCode m_keyCode;
	KeyModifier m_modifier;
};
```

### D3 KeyDown/KeyUpEvent 构造

```cpp
KeyDownEvent(Window* window, KeyCode keyCode, KeyModifier modifier)
	: KeyEvent(window, keyCode, modifier){}
// KeyUpEvent 对称
```

### D4 WindowMessageHandler.cpp

```cpp
namespace{   // 匿名 namespace（现有结构内）

/// @brief 翻译当前修饰键状态（平台翻译器内查询——分层允许；各平台实现不同不抽公共）
KeyModifier TranslateModifier(){
	KeyModifier m = KeyModifier::None;
	const auto AddIfDown = [&m](int vk, KeyModifier mod){
		if (GetKeyState(vk) & 0x8000)
			m = m | mod;   // operator| 配套生效
	};
	AddIfDown(VK_SHIFT,   KeyModifier::Shift);
	AddIfDown(VK_CONTROL, KeyModifier::Ctrl);
	AddIfDown(VK_MENU,    KeyModifier::Alt);
	return m;
}

}

// WM_KEYDOWN/WM_KEYUP case：
KeyDownEvent event(window, TranslateKeyCode(wParam, lParam), TranslateModifier());
// KeyUpEvent 对称
```

### D5 Window.cpp

```cpp
void Window::HandleKeyDown(const KeyDownEvent& event){
	if (event.GetKeyCode() == KeyCode::Tab){
		FocusNext(event.IsShiftDown() ? -1 : 1);   // 5.5.2：Shift+Tab 反向（5.4 债务落地）
		return;
	}
	if (m_focusedWidget) m_focusedWidget->OnKeyDown(event);
}
```

## 3. TextBox 实现清单（D7，7 项）

```
1. 辅助实现：
   HasSelection()      = m_selectionAnchor != m_caret
   GetSelectionMin()   = min(anchor, caret)
   GetSelectionMax()   = max(anchor, caret)
   DeleteSelection()   = 删 [min, max) 字节区 + ClearSelection + return minCp（新光标）
   ClearSelection()    = m_selectionAnchor = m_caret（无效化）

2. 编辑操作改判（P6 编辑操作自包含——GPT 必须项）：
   InsertCodepoint：  if (HasSelection()) m_caret = DeleteSelection(); 再插入
   DeleteBackward：   if (HasSelection()){ m_caret = DeleteSelection(); Invalidate(); return; }
   DeleteForward：    对称

3. OnKeyDown 扩展（GPT：最容易漏，明确列入）：
   Shift+Left：   if (m_caret > 0) --m_caret;              （anchor 不动，active 移动）
   无 Shift+Left：ClearSelection(); if (m_caret > 0) --m_caret;
   Shift+Right / 无 Shift+Right：对称
   Shift+Home：   m_caret = 0;
   无 Shift+Home：ClearSelection(); m_caret = 0;
   Shift+End / 无 Shift+End：对称（count）
   （Backspace/Delete 分支不变——编辑操作内部已处理 Selection）

4. OnMouseMove：  if (!m_mouseDown) return; 同源定位 → m_caret = CaretIndexFromX(...); Invalidate();
5. OnMouseButtonUp：m_mouseDown = false;
6. OnMouseButtonDown（既有 + 加）：
   m_selectionAnchor = m_caret;
   m_mouseDown = true;
7. OnPaint 高亮（kSelectionColor 匿名 namespace + maxTextWidth 钳制）：
   白底 → [HasSelection 高亮块] → 文本 → 光标
```

## 4. 验证（人工——P8）

拖选高亮 / Shift+方向键含反向收缩（`ab[c]def → Shift+← → ab|cdef`）/ 输入替换选中区 / Backspace 删选中区 / 点击取消选择 / Shift+Tab 反向导航 / 长文本裁切与 Selection 交互

## 5. 修订记录

- v1.0（2026-08-14）详细设计：D1-D9 定稿。GPT 5 处调整全采纳：D1 operator| 配套；D2 全 inline 确认；D4 TranslateModifier 匿名 namespace 函数（双分支共用非分支内 lambda）；D7 补 OnKeyDown 清单（Shift+方向键/Home/End + 无 Shift 清选择——最容易漏）；D8 预填 20-30 字符长文本；记账 m_mouseDown 未来升级 m_dragSelecting（6.x 双击/三击/选词/选行）。
