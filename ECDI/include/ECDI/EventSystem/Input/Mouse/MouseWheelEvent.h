#pragma once

#include"MouseEvent.h"

/// @brief 鼠标滚轮事件
/// @details
/// 由 WM_MOUSEWHEEL 翻译而来。
/// delta > 0 表示向前滚动（远离用户），delta < 0 表示向后滚动（朝向用户）。
/// 原始值不归一化，语义判断推迟到消费者。
class MouseWheelEvent :public MouseEvent {
	
public:

	static EventType StaticType()noexcept {

		return EventType::MouseWheel;

	}

	EventType GetType()const noexcept override {

		return StaticType();

	}

	/// @param window 事件来源窗口
	/// @param mouseX 鼠标 X 坐标（已从屏幕坐标转为客户区坐标）
	/// @param mouseY 鼠标 Y 坐标
	/// @param delta  滚轮增量（原始值，WHEEL_DELTA 的倍数）
	MouseWheelEvent(
		Window* window,
		int mouseX,
		int mouseY,
		int delta
	) :MouseEvent(window, mouseX, mouseY), m_delta(delta) {

	}

	/// @brief 获取滚轮增量
	int GetDelta() const noexcept{

		return m_delta;
	
	}

private:
	int m_delta;
};
