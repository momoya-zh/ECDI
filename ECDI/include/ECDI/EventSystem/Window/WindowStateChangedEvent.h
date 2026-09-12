#pragma once

#include "ECDI/EventSystem/Window/WindowEvent.h"
#include "ECDI/Window/WindowState.h"

namespace ECDI
{

/// @brief 窗口状态变化事件（R7）
/// @details 由窗口**系统实际状态**变化回流（源 = WM_SIZE + IsIconic/IsZoomed），
/// 消费者（应用自绘标题栏按钮 / 未来的 CaptionBar 控件）据此切换按钮形态。
///
/// ⚠️ 语义边界：本事件表示「**窗口状态已经是** X」这一既成事实——
/// 不是「请把窗口变成 X」的请求。触发源固定为系统状态变化，
/// 因此鼠标拖拽标题栏到屏幕顶部触发的最大化、Win+↑、Aero Snap 等
/// **非 API 路径**同样产生本事件（这是刻意的——状态同步必须覆盖全部来源）。
class WindowStateChangedEvent : public WindowEvent{

public:

	WindowStateChangedEvent(
		Window* window,
		WindowState state):
		WindowEvent(window),
		m_state(state){

	}

	static EventType StaticType(){

		return EventType::WindowStateChanged;

	}

	EventType GetType() const override{

		return StaticType();

	}

	/// @brief 获取变化后的窗口状态
	WindowState GetState() const noexcept{

		return m_state;

	}

private:

	WindowState m_state;
};

}
