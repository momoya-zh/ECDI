#pragma once

#include "ECDI/EventSystem/Input/InputEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyCode.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyModifier.h"

namespace ECDI{

/// @brief 键盘事件基类（KeyDown / KeyUp 的公共基类）
/// @details 携带 KeyCode（物理按键标识）+ KeyModifier（修饰键状态，5.5.2），不携带字符码。
class KeyEvent : public InputEvent{

public:

	/// @brief 获取按键的 KeyCode
	KeyCode GetKeyCode() const noexcept{

		return m_keyCode;

	}

	/// @brief 是否按下指定修饰键组合（位与判断——HasModifier(Ctrl | Shift) 可组合查询）
	bool HasModifier(KeyModifier modifier) const noexcept{

		return (static_cast<int>(m_modifier) & static_cast<int>(modifier)) == static_cast<int>(modifier);

	}

	bool IsShiftDown() const noexcept{ return HasModifier(KeyModifier::Shift); }

	bool IsCtrlDown() const noexcept{ return HasModifier(KeyModifier::Ctrl); }

	bool IsAltDown() const noexcept{ return HasModifier(KeyModifier::Alt); }

protected:

	/// @param window   事件来源窗口
	/// @param keyCode  按键标识
	/// @param modifier 修饰键状态（翻译器填入——平台翻译器内查询，分层允许）
	KeyEvent(
		Window* window,
		KeyCode keyCode,
		KeyModifier modifier
	): InputEvent(window),m_keyCode(keyCode),m_modifier(modifier)	{

	}

private:

	KeyCode m_keyCode;

	KeyModifier m_modifier;

};

}
