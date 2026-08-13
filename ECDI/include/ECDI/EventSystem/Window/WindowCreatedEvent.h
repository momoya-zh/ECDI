#pragma once

#include "ECDI/EventSystem/Window/WindowEvent.h"

namespace ECDI
{

/// @brief 窗口创建完成事件
/// @details
/// 由 Application::Create() 手动派发（不是 Win32 消息翻译的产物）。
/// 通知外部"一个新窗口已经创建完毕"。
class WindowCreatedEvent : public WindowEvent{

public:

	explicit WindowCreatedEvent(Window* window):WindowEvent(window){

	}


	EventType GetType() const override{

		return StaticType();

	}


	static EventType StaticType(){

		return EventType::WindowCreated;

	}

};

}
