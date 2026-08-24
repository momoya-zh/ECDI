#pragma once

#include "ECDI/EventSystem/Input/Mouse/MouseButtonEvent.h"

namespace ECDI{

/// @brief 鼠标按键按下事件
/// @details
/// 由 WM_LBUTTONDOWN / WM_RBUTTONDOWN / WM_MBUTTONDOWN / WM_XBUTTONDOWN 翻译而来；
/// 8.5.2：WM_LBUTTONDBLCLK（系统双击）也翻译为本事件 + isDoubleClick=true——
/// "双击"是平台已判定的事实（翻译层如实上报，语义判断归消费者）。
class MouseButtonDownEvent : public MouseButtonEvent{

public:

	MouseButtonDownEvent(
		Window* window,
		int mouseX,
		int mouseY,
		MouseButton button,
		bool isDoubleClick = false
	):MouseButtonEvent(window,mouseX,mouseY,button), m_isDoubleClick(isDoubleClick){

	}

	/// @brief 是否系统判定的双击（8.5.2：WM_LBUTTONDBLCLK 翻译——平台层事实，非框架内计时）
	bool IsDoubleClick() const noexcept{

		return m_isDoubleClick;

	}

	static EventType StaticType() noexcept{

		return EventType::MouseButtonDown;

	}


	EventType GetType() const noexcept override{

		return StaticType();

	}

private:

	bool m_isDoubleClick = false;	///< 双击标志（默认 false——普通单 Down；8.5.2）

};

}
