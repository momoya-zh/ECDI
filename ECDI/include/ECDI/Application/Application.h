#pragma once

#include "ECDI/Window/Window.h"
#include "ECDI/EventSystem/EventRouter.h"

#include <memory>
#include <string>
#include <vector>

#include "ECDI/Application/TrayIcon.h"

namespace ECDI{

class PlatformApplication;   // 前置声明（7.1.5：事件循环下沉——组合 unique_ptr 成员）

class WindowCloseRequestedEvent;
class WindowCreatedEvent;
class WindowDestroyedEvent;
class WindowResizedEvent;
class TimerEvent;
class Event;
class MouseMoveEvent;
class MouseButtonDownEvent;
class MouseButtonUpEvent;
class MouseWheelEvent;
class KeyDownEvent;
class KeyUpEvent;
class CharInputEvent;
class Widget;
class DropFilesEvent;
class TrayEvent;

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

	// ── Phase 14：应用级托盘（D1——应用层唯一入口，转发平台应用）────

	/// @brief 设置（或更新）托盘图标（R1；幂等配置语义——D11：未注册 ⇒ 注册；已注册 ⇒ 更新）
	void SetTrayIcon(const TrayIconOptions& options);

	/// @brief 移除托盘图标（R1；幂等——未注册为 no-op）
	void RemoveTrayIcon();

	/// @brief 弹出托盘菜单并同步返回选中项 ID（R5/D10；0 = 未选中/取消）
	/// @details 同步语义：菜单显示期间不派发框架事件（系统模态菜单固有行为）
	int ShowTrayMenu(const TrayMenu& menu);

	/// @brief 设置「最后一个窗口关闭时是否退出应用」（R13；默认 `true` = 零行为变更）
	/// @details 命名直述触发条件（避免双重否定）。只影响隐式退出；显式 Exit() 不受影响。
	/// 与 Hide() 的分工见详设 §6.6：「关窗→隐藏到托盘」用 Hide()（不触发本开关）。
	void SetQuitOnLastWindowClosed(bool enabled);

protected:

	// ── 窗口事件处理 ────────────────────────────────

	void OnWindowCreated(
		const WindowCreatedEvent& event) override;

	void OnWindowDestroyed(
		const WindowDestroyedEvent& event) override;

	void OnWindowResized(
		const WindowResizedEvent& event) override;

	void OnWindowCloseRequested(const WindowCloseRequestedEvent& event) override;

	/// @brief 文件拖入派发（Phase 14 R11——HitTest → Dispatch → Bubbling，与鼠标同族）
	void OnDropFiles(const DropFilesEvent& event) override;

	/// @brief 定时器触发（8.5.1；派发给焦点控件——与 OnCharInput 同路径，非坐标事件）
	/// @details TimerEvent 无 HitTest——FindFocusedWidget → target->OnTimer
	void OnTimer(const TimerEvent& event) override;

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

	bool m_quitOnLastWindowClosed = true;	///< Phase 14 R13：最后窗口关闭是否隐式退出（默认 true = 既有行为）
};

}
