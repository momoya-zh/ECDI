#pragma once

#include"ECDI/EventSystem/Event.h"
namespace ECDI
{

/// @brief 输入事件基类（鼠标和键盘事件的公共基类）
/// @details 继承自 Event，标记所有与用户输入相关的事件。
class InputEvent :public Event {

protected:

	/// @param window 事件来源窗口
	explicit InputEvent(Window* window):Event(window){

	}

};

}
