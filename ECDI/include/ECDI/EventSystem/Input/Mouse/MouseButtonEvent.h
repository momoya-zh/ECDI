#pragma once

#include "ECDI/EventSystem/Input/Mouse/MouseEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButton.h"

namespace ECDI{

/// @brief 鼠标按键事件基类（ButtonDown / ButtonUp 的公共基类）
/// @details 在 MouseEvent 基础上增加 MouseButton（哪个按键）。
class MouseButtonEvent : public MouseEvent{

protected:

	/// @param window 事件来源窗口
	/// @param mouseX 鼠标 X 坐标
	/// @param mouseY 鼠标 Y 坐标
	/// @param button 按键标识
	MouseButtonEvent(
		Window* window,
		int mouseX,
		int mouseY,
		MouseButton button
	):MouseEvent(window, mouseX, mouseY),m_button(button){

	}


public:

	/// @brief 获取触发事件的鼠标按键
	MouseButton GetButton() const noexcept{

		return m_button;

	}


private:

	MouseButton m_button;

};

}
