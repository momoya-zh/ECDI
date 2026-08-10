#pragma once

#include "EventType.h"

namespace ECDI{

class Window;

/// @brief Framework 所有事件的基类
/// @details
/// 事件是轻量的"已发生的事实"，只携带数据，不负责创建/分发/监听/处理。
///
/// 设计原则：
/// - m_window：事件来源窗口
/// - m_handled：传播层状态（由 Application 控制，Widget 只读取）
/// - 虚 GetType()：用于 EventDispatcher 的类型分派
class Event{

public:

	virtual ~Event() = default;

	/// @brief 获取事件来源窗口
	Window* GetWindow() const noexcept{

		return m_window;

	}

	/// @brief 获取事件类型（子类实现，用于 StaticType 分派）
	virtual EventType GetType() const = 0;

	/// @brief 事件是否已被处理
	bool IsHandled() const noexcept{

		return m_handled;

	}

	/// @brief 标记事件为已处理
	void SetHandled() noexcept{

		m_handled = true;

	}

protected:

	/// @param window 事件来源窗口
	explicit Event(Window* window): m_window(window) {

	}

protected:

	Window* m_window=nullptr;	///< 事件来源窗口
	bool m_handled = false;		///< 是否已被处理（传播层状态）
};

}
