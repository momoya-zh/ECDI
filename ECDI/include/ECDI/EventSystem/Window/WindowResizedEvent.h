#pragma once
#include "WindowEvent.h"

/// @brief 窗口大小变化事件
/// @details
/// 由 WM_SIZE 翻译而来。
/// 注意：Window 内部会先同步 RootWidget 尺寸，再派发此事件。
/// Application 收到此事件时，RootWidget 已经是最新尺寸。
class WindowResizedEvent : public WindowEvent{

public:

	WindowResizedEvent(
		Window* window,
		int width,
		int height):
		WindowEvent(window),
		m_width(width),
		m_height(height){

	}

	static EventType StaticType(){

		return EventType::WindowResized;

	}

	EventType GetType() const override{

		return StaticType();

	}


	/// @brief 获取新的窗口客户区宽度
	int GetWidth() const noexcept{

		return m_width;

	}

	/// @brief 获取新的窗口客户区高度
	int GetHeight() const noexcept{

		return m_height;

	}


private:

	int m_width;
	int m_height;
};
