#pragma once

#include "ECDI/Window/Window.h"
#include "ECDI/EventSystem/EventRouter.h"

#include <memory>
#include <string>
#include <vector>

namespace ECDI{

class PlatformApplication;   // 前置声明（7.1.5：事件循环下沉——组合 unique_ptr 成员）

class WindowCloseRequestedEvent;
class WindowCreatedEvent;
class WindowDestroyedEvent;
class WindowResizedEvent;
class Event;
class MouseMoveEvent;
class MouseButtonDownEvent;
class MouseButtonUpEvent;
class MouseWheelEvent;
class KeyDownEvent;
class KeyUpEvent;
class CharInputEvent;
class Widget;

/// @brief 应用程序主类
/// @details
/// 管理窗口生命周期、事件分发和消息循环。
/// Application 是 Event System 和 Widget System 的桥梁：
/// - Event System：通过继承 EventRouter 接收所有 Framework Event
/// - Widget System：通过 FindTargetWidget / HitTest 将事件分发给 Widget 树
///
/// 事件流控制（HitTest → Target Dispatch → Bubbling）全部在 Application 完成，
/// Widget 只负责事件响应。
class Application : public EventRouter{

public :

	friend class Window;

	Application();

	/// @brief 显式析构（7.1.5：unique_ptr\<PlatformApplication\> 不完整类型成员——
	/// 析构点移到 Application.cpp（PlatformApplication 完整可见处），pimpl 惯用法）
	~Application();

	/// @brief 进入消息循环（阻塞，直到所有窗口关闭）
	int Run();

	/// @brief 创建一个新窗口
	/// @param title  窗口标题
	/// @param width  窗口总宽度
	/// @param height 窗口总高度
	/// @return 新创建窗口的引用
	Window& Create(const std::string&title,int width,int height);

	/// @brief 退出消息循环
	void Exit();

protected:

	// ── 窗口事件处理 ────────────────────────────────

	void OnWindowCreated(
		const WindowCreatedEvent& event) override;

	void OnWindowDestroyed(
		const WindowDestroyedEvent& event) override;

	void OnWindowResized(
		const WindowResizedEvent& event) override;

	void OnWindowCloseRequested(const WindowCloseRequestedEvent& event) override;

	// ── 鼠标事件处理（HitTest → Target Dispatch → Bubbling）──

	void OnMouseMove(const MouseMoveEvent& event)override;

	void OnMouseButtonDown(const MouseButtonDownEvent& event) override;

	void OnMouseButtonUp(const MouseButtonUpEvent& event) override;

	void OnMouseWheel(const MouseWheelEvent& event) override;

	void OnKeyDown(const KeyDownEvent& event)override;

	void OnKeyUp(const KeyUpEvent& event)override;

	void OnCharInput(const CharInputEvent& event) override;

private:

	/// @brief 在消息循环末尾处理延迟销毁的窗口
	void ProcessDeferredDestroy();

	/// @brief 通过坐标在 Widget 树中查找目标 Widget
	/// @param window 目标窗口
	/// @param x 客户区坐标 X
	/// @param y 客户区坐标 Y
	/// @return 目标 Widget 指针，未命中返回 nullptr
	Widget* FindTargetWidget(Window& window,int x,int y)const noexcept;

	Widget* FindFocusedWidget(Window&window) const noexcept;

private:

	// 7.1.5：m_windowClass 已下沉——窗口系统资源归平台层 WindowClass::Instance()（Application 不再认识窗口类）

	std::unique_ptr<PlatformApplication> m_platformApplication;	///< 平台应用（7.1.5：事件循环下沉——cpp 创建 Win32 实现）

	std::vector<std::unique_ptr<Window>> m_windows;	///< 活跃窗口列表

	std::vector<std::unique_ptr<Window>> m_deferredDestroy;	///< 延迟销毁的窗口（本帧结束时释放）

	bool m_running=true;	///< 消息循环是否继续运行
};

}
