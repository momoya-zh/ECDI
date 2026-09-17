#pragma once

#include "ECDI/EventSystem/Event.h"

namespace ECDI{

/// @brief 托盘交互事件种类（Phase 14 R4 / D5）
/// @details **平台语义在此收口**——`NOTIFYICON_VERSION_4` 的 `NIN_SELECT` / `NIN_KEYSELECT` /
/// `WM_CONTEXTMENU` 等一律翻译为本枚举，**公共 API 不出现 Win32 消息码**。
enum class TrayEventType{

	/// @brief 鼠标左键单击（v4 的 NIN_SELECT）
	Select,

	/// @brief 键盘激活（v4 的 NIN_KEYSELECT——Enter/Space 聚焦图标后激活）
	KeySelect,

	/// @brief 鼠标左键双击
	DoubleClick,

	/// @brief 右键（v4 的 WM_CONTEXTMENU——用于弹出菜单）
	ContextMenu
};

/// @brief 托盘交互事件（Phase 14 R4）
/// @details **应用级事件**：`GetWindow()` 恒为 `nullptr`（无来源窗口——与 `Window` 解耦，R7）。
/// 锚点坐标用于菜单定位与「就地弹窗」（v4 携带；键盘激活时为图标左上角——MSDN 语义）。
/// ⚠️ **ContextMenu 的锚点由框架兜底**（v1.1 核实 O-5：NOTIFYICON_VERSION_4 下
/// WM_CONTEXTMENU 不在坐标有效列表内——MSDN：「For all other messages, wParam is undefined」）；
/// 平台层以 GetCursorPos() 取右键时刻光标位置（Win32 对 WM_CONTEXTMENU 的标准做法）。
class TrayEvent : public Event{

public:

	/// @param type 事件种类
	/// @param x    锚点 X（**屏幕坐标**——菜单定位直接可用，无需换算）
	/// @param y    锚点 Y（屏幕坐标）
	TrayEvent(TrayEventType type, int x, int y) noexcept
		: Event(nullptr), m_type(type), m_x(x), m_y(y){}

	TrayEventType GetTrayType() const noexcept{ return m_type; }

	/// @brief 锚点 X（**屏幕坐标**——菜单定位直接可用，无需换算）
	int GetX() const noexcept{ return m_x; }

	/// @brief 锚点 Y（屏幕坐标）
	int GetY() const noexcept{ return m_y; }

	static EventType StaticType() noexcept{ return EventType::Tray; }

	EventType GetType() const override{ return EventType::Tray; }

private:

	TrayEventType m_type;

	int m_x = 0;

	int m_y = 0;
};

}
