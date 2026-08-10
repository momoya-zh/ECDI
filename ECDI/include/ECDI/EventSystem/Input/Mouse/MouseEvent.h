#pragma once

#include"ECDI/EventSystem/Input/InputEvent.h"
namespace ECDI
{

/// @brief 鼠标事件基类
/// @details
/// 所有鼠标事件（Move/ButtonDown/ButtonUp/Wheel）的公共基类。
/// 携带鼠标在窗口客户区中的坐标 (x, y)。
class MouseEvent :public InputEvent {

protected:

	/// @param window 事件来源窗口
	/// @param mouseX 鼠标 X 坐标（客户区坐标）
	/// @param mouseY 鼠标 Y 坐标（客户区坐标）
	MouseEvent(
		Window* window,
		int mouseX,
		int mouseY): InputEvent(window),m_mouseX(mouseX),m_mouseY(mouseY){

	}

public:

	/// @brief 获取鼠标 X 坐标（客户区坐标）
	int GetMouseX() const noexcept{

		return m_mouseX;

	}

	/// @brief 获取鼠标 Y 坐标（客户区坐标）
	int GetMouseY() const noexcept{

		return m_mouseY;

	}

private:

	int m_mouseX;
	int m_mouseY;


 };

}
