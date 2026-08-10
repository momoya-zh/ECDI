#pragma once

#include "ECDI/EventSystem/Event.h"
namespace ECDI
{

/// @brief 窗口事件基类
/// @details 所有窗口生命周期事件（Created/Destroyed/Resized/CloseRequested）的公共基类。
class WindowEvent : public Event{

public:

protected:

	/// @param window 事件来源窗口
	explicit WindowEvent(Window* window): Event(window) {
	
	};
};

}
