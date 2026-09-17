#pragma once

#include "ECDI/EventSystem/Event.h"

#include <string>
#include <vector>

namespace ECDI{

/// @brief 文件拖入事件（Phase 14 R9/R10）
/// @details **`HDROP` 绝不出现在公共 API**：平台层已解析为 UTF-8 路径列表，
/// 且 `DragFinish` 已在**事件抛出之前**完成（R10）——消费者拿到的是已脱离系统资源的纯数据，
/// **应用侧零释放责任**。
class DropFilesEvent : public Event{

public:

	/// @param window 来源窗口（拖入是窗口级事件——恒非空）
	/// @param paths  UTF-8 路径列表（可空——空列表表示无有效文件，消费者应忽略）
	/// @param x      落点 X（**客户区坐标**——与 MouseButtonDownEvent 同坐标系）
	/// @param y      落点 Y（客户区坐标）
	DropFilesEvent(Window* window,
		std::vector<std::string> paths,
		int x,
		int y) noexcept
		: Event(window)
		, m_paths(std::move(paths))
		, m_x(x)
		, m_y(y){}

	/// @brief 拖入的文件路径列表（UTF-8；每项为一个绝对路径）
	const std::vector<std::string>& GetPaths() const noexcept{ return m_paths; }

	/// @brief 落点 X（客户区坐标——HitTest 直接可用）
	int GetX() const noexcept{ return m_x; }

	/// @brief 落点 Y（客户区坐标）
	int GetY() const noexcept{ return m_y; }

	static EventType StaticType() noexcept{ return EventType::DropFiles; }

	EventType GetType() const override{ return EventType::DropFiles; }

private:

	std::vector<std::string> m_paths;

	int m_x = 0;

	int m_y = 0;
};

}
