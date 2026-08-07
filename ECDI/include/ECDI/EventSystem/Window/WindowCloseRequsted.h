#pragma once

#include "WindowEvent.h"

/// @brief 窗口关闭请求事件
/// @details
/// 由 WM_CLOSE 翻译而来（用户点击关闭按钮或 Alt+F4）。
/// Application 收到后调用 Window::Release() 销毁 HWND。
class WindowCloseRequestedEvent : public WindowEvent{

public:

	explicit WindowCloseRequestedEvent(Window* window):WindowEvent(window){

	}

	EventType GetType() const override{

		return StaticType();

	}

	static EventType StaticType(){

		return EventType::WindowCloseRequested;

	}
};
