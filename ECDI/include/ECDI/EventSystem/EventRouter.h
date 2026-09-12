#pragma once

#include "ECDI/EventSystem/Event.h"

namespace ECDI{

class WindowCreatedEvent;
class WindowDestroyedEvent;
class WindowResizedEvent;
class WindowStateChangedEvent;
class WindowCloseRequestedEvent;
class TimerEvent;
class MouseMoveEvent;
class MouseButtonUpEvent;
class MouseButtonDownEvent;
class MouseWheelEvent;
class KeyDownEvent;
class KeyUpEvent;
class CharInputEvent;

/// @brief 事件路由器基类
/// @details
/// 提供 OnEvent() 入口，通过 EventDispatcher 将通用 Event 分派到具名虚方法。
/// Application 继承此基类，override 感兴趣的事件处理函数。
///
/// 所有虚方法默认空实现（不处理 = 忽略）。
class EventRouter{
public:

	virtual ~EventRouter() = default;

	/// @brief 事件入口：分派到对应的 OnXxx 虚方法
	void OnEvent(const Event&event);

protected:

	// ── 窗口事件 ────────────────────────────────────

	virtual void OnWindowCreated(
		const WindowCreatedEvent& event
	){}

	virtual void OnWindowDestroyed(
		const WindowDestroyedEvent&event
	){}

	virtual void OnWindowResized(
		const WindowResizedEvent&event
	){}

	/// @brief 窗口状态变化（Phase 12 R7——minimized/maximized/restored）
	/// @details 源 = 平台层 WM_SIZE 的 IsIconic/IsZoomed 判定——即「系统真实状态」，
	/// 非「某个 API 被调用」，故鼠标拖拽 / Win+↑ / Aero Snap 等非 API 路径同样到达。
	virtual void OnWindowStateChanged(
		const WindowStateChangedEvent& event
	){}

	virtual void OnWindowCloseRequested(
		const WindowCloseRequestedEvent&evennt
	){}

	virtual void OnTimer(
		const TimerEvent& event
	){}

	// ── 鼠标事件 ────────────────────────────────────

	virtual void OnMouseMove(
		const MouseMoveEvent& event
	){}

	virtual void OnMouseButtonDown(
		const MouseButtonDownEvent& event
	){}

	virtual void OnMouseButtonUp(
		const MouseButtonUpEvent& event
	){}

	virtual void OnMouseWheel(
		const MouseWheelEvent& event
	){}

	// ── 键盘事件 ────────────────────────────────────

	virtual void OnKeyDown(
		const KeyDownEvent& event
	){}

	virtual void OnKeyUp(
		const KeyUpEvent& event
	){}

	virtual void OnCharInput(
		const CharInputEvent& event
	){}

};

}
