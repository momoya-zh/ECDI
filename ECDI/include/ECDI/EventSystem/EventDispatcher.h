#pragma once

#include "Event.h"

#include <utility>

namespace ECDI{

/// @brief 事件类型分派器（模板分派）
/// @details
/// 通过 EventType 比较 + static_cast 将 Event 基类安全地转为具体事件子类，
/// 然后调用用户提供的处理函数。
///
/// 用法：
/// @code
/// EventDispatcher dispatcher(event);
/// dispatcher.Dispatch<MouseMoveEvent>([](const MouseMoveEvent& e) {
///     // 处理鼠标移动
/// });
/// @endcode
class EventDispatcher{
public:

	/// @param event 要分派的事件引用
	explicit EventDispatcher(const Event& event):m_event(event){
	}


	/// @brief 尝试将事件分派为类型 T
	/// @tparam T 目标事件类型（必须有 StaticType() 静态方法）
	/// @tparam Function 处理函数类型
	/// @param function 匹配成功时调用的处理函数
	/// @return true = 类型匹配并已调用；false = 类型不匹配
	template<typename T, typename Function>

	bool Dispatch(Function&& function) const{

		if(m_event.GetType() == T::StaticType()){

			std::forward<Function>(function)(

				static_cast<const T&>(m_event)
			);

			return true;
		}

		return false;
	}


private:

	const Event& m_event;

};

}
