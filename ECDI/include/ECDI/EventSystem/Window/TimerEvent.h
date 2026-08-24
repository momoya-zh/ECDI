#pragma once

#include "ECDI/EventSystem/Event.h"

namespace ECDI{

/// @brief 周期定时器触发事件（8.5.1；由平台 WM_TIMER 翻译）
/// @details 轻量事实：某 TimerId 触发。语义（光标闪烁/动画）由消费者解释——
/// Event 原则（"已发生的事实"），平台不知道 timerId 属于谁。
class TimerEvent: public Event{

public:

	static EventType StaticType()noexcept{
		return EventType::Timer;
	}

	EventType GetType()const noexcept override{
		return StaticType();
	}

	/// @param window  事件来源窗口
	/// @param timerId 定时器标识（StartTimer 传入的 id）
	TimerEvent(Window* window, int timerId)noexcept
		: Event(window), m_timerId(timerId){}

	/// @brief 获取定时器标识
	int GetTimerId()const noexcept{
		return m_timerId;
	}

private:
	int m_timerId;
};

}
