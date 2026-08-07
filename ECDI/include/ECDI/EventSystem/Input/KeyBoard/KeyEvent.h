#pragma once

#include "ECDI/EventSystem/Input/InputEvent.h"

#include "KeyCode.h"

/// @brief 键盘事件基类（KeyDown / KeyUp 的公共基类）
/// @details 携带 KeyCode（物理按键标识），不携带字符码。
class KeyEvent : public InputEvent{

public:

	/// @brief 获取按键的 KeyCode
	KeyCode GetKeyCode() const noexcept{

		return m_keyCode;

	}

protected:

	/// @param window  事件来源窗口
	/// @param keyCode 按键标识
	KeyEvent(
		Window* window,
		KeyCode keyCode
	): InputEvent(window),m_keyCode(keyCode)	{
	
	}

private:

	KeyCode m_keyCode;

};
