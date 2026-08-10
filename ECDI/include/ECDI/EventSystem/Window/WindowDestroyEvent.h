#pragma once

#include "WindowEvent.h"
namespace ECDI
{

/// @brief 窗口销毁事件
/// @details
/// 由 WM_DESTROY 翻译而来。
/// Application 收到后将 Window 从 m_windows 移入 m_deferredDestroy（延迟销毁）。
class WindowDestroyedEvent : public WindowEvent{

public:

	explicit WindowDestroyedEvent(Window* window):WindowEvent(window){

	}

	EventType GetType() const override{

		return StaticType();

	}
	static EventType StaticType(){

		return EventType::WindowDestroyed;

	}
};

}
