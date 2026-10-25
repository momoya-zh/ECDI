#pragma once

#include <string>

#include "ECDI/Core/CursorShape.h"   // #72-②：OnQueryCursor 返回形态（叶子头，零依赖）

namespace ECDI{

class Window;   // 前置声明——GetWindow 返回框架窗口（平台层不 include Window.h，只认此契约）
class Event;    // 前置声明——OnEvent 引用参数（不需要完整定义）

/// @brief 平台窗口宿主（7.1 D2 核心）：Platform 不认识框架具体类，只认识此契约
/// @details Window 实现此接口；Win32PlatformWindow 持 Host& 回调。
/// 平台事件 → Host 回调 → 框架响应（契约语言：平台层"发生了窗口事件"，框架层"响应"）。
///
/// 接口收敛说明（YAGNI，2026-08-15；2026-09-12 按 window-ownership.md §4.5 改写）：
/// - 无 OnDestroyed：**PlatformWindowHost 不负责 Window 对象的生命周期**。
///   Win32 平台层在 WM_DESTROY 后清空 HWND 并置空句柄，同时经既有事件通道上报
///   WindowDestroyedEvent；Window 对象的延迟销毁与注册表维护由 Application 负责。
///   （原文「框架层无需动作」写于 7.1——当时 Application 侧尚无窗口注册表语义，已过期。）
class PlatformWindowHost{
public:
	virtual ~PlatformWindowHost() = default;

	/// @brief 绘制请求（WM_PAINT）→ 帧编排（Window::PaintFrame）
	virtual void OnPaint() = 0;

	/// @brief 客户区尺寸变化（WM_SIZE）→ RootWidget 尺寸同步
	virtual void OnResized(int width, int height) = 0;

	/// @brief 窗口移动/缩放结束（WM_EXITSIZEMOVE）→ 销毁+重建文本插入点（IME 归位）
	virtual void OnExitSizeMove() = 0;

	/// @brief 事件来源窗口（翻译器构造 Event 需 Window*——Event.h:50 绑定）
	/// @details 平台层经此接口拿指针，不 include Window.h——"平台不认识框架具体类"边界守住
	virtual Window* GetWindow() const noexcept = 0;

	/// @brief 事件转发（7.1.2 Dispatch 一级：翻译器 → 框架契约）
	/// @details const 只读（事件不可变数据模型，与 EventRouter::OnEvent 对齐）；
	/// Window 实现为 Transitional adapter（转发应用层入口——7.1.5 可能变）
	virtual void OnEvent(const Event& event) = 0;

	/// @brief IME 组合发生（WM_IME_START/COMPOSITION；7.1.2 方案 B——IME 属输入法子系统，
	/// 由平台层状态同步区上报，非事件系统成员）→ Window::NotifyIMEComposition
	virtual void OnIMEComposition() = 0;

	/// @brief IME 组合串内容更新（8.5.1：平台层状态上报——非 Event 系统成员，同 OnIMEComposition）
	/// @param compositionText 当前组合串（UTF-8；空串 = 组合中无内容——组合仍在，非 Commit）
	/// @details 平台实现（Win32）在 WM_IME_COMPOSITION 且 lParam & GCS_COMPSTR 时提取上报；
	/// 框架层 Window 转发焦点控件更新 Composition 状态（临时编辑，不触发正式编辑语义）。
	virtual void OnIMECompositionUpdate(const std::string& compositionText) = 0;

	/// @brief IME 组合提交（8.5.1：C7 契约——Commit 的唯一可靠来源）
	/// @param resultText 最终结果文本（UTF-8）
	/// @details 平台实现（Win32）在 WM_IME_COMPOSITION 且 lParam & GCS_RESULTSTR 时提取上报；
	/// 框架层 Window 转发焦点控件——组合区间转正式文本（进 Undo 历史）。
	virtual void OnIMECompositionCommit(const std::string& resultText) = 0;

	/// @brief 客户区可交互命中查询（Phase 13 R2 / D2——NCHITTEST 委托）
	/// @param x,y 窗口局部坐标（Borderless 下客户区 = 整窗，两者坐标系一致）
	/// @return true  = 该点落在**消费鼠标输入的控件**上 → 平台层让本次命中走客户区（HTCLIENT）
	///         false = 交 caption 区语义（HTCAPTION——拖拽 / 双击最大化·还原 / 系统菜单）
	/// @details 平台层**不认识 Widget**（分层律）——只问本契约；实现方（Window）转
	/// RootWidget::HitTest → Widget::ConsumesMouseInput。
	/// **纯虚**：遗漏实现 = 编译期暴露（与 Phase 12 的 7 个纯虚同规格）。
	virtual bool IsClientInteractiveAt(int x, int y) const noexcept = 0;

	/// @brief 系统鼠标捕获已被夺走（★ 审计 #60 / C3：`WM_CAPTURECHANGED` 上报）
	/// @details ★★ **为什么必须上报（缺陷的第二个后果）**：捕获可能被**系统或第三方**
	///   单方面夺走——Alt+Tab 切走、弹出系统菜单、别的窗口调 `SetCapture`、窗口被禁用等，
	///   此时系统发 `WM_CAPTURECHANGED`。**框架若不获知**，`m_captureWidget` 会一直指着
	///   上次 Down 的目标 ⇒ 后续 Move 全部直派它、跳过 HitTest ⇒ **整窗 hover 与命中失效**，
	///   且该控件永远收不到 Up（卡在按下态）。修复前唯一的自愈出口是「用户下一次任意点击」。
	///   ⇒ 平台层收到 `WM_CAPTURECHANGED` 后调本方法，框架据此**清空捕获登记**。
	/// ★ **默认实现 = 空实现**（**非纯虚**——同 `SetWindowBackdrop` 先例）：
	///   既有 4 个实现者（`Window` + `EventTests::FakeHost` / `DropFilesTests::DropHost` /
	///   `DesktopLayerTests::LayerHost`）**零改动**；三个测试 Host 不驱动真实消息循环，
	///   保持空实现即语义正确。
	/// @note ★ **与 `SetCapture(false)` 的区别**：那个是「框架主动释放」，本回调是
	///   「**捕获已被别人拿走**」——此时**不得**再调 `::ReleaseCapture`（会释放别人的捕获
	///   ／或触发二次 `WM_CAPTURECHANGED` 回环），只需清框架侧登记。
	virtual void OnCaptureLost()
	{
	}

	/// @brief 光标形态查询（★ #72-②：`WM_SETCURSOR` 客户区命中时代答）
	/// @param x,y 鼠标位置（窗口局部 **DIP**——与 `IsClientInteractiveAt` 同口径，
	///        物理像素 → DIP 的换算点唯一在平台层）
	/// @return 命中控件自述的光标形态；无命中 / 未实现 = `Arrow`
	/// @details 平台层**不认识 Widget**（分层律，同 `IsClientInteractiveAt`）——只问本契约；
	///          实现方（Window）转 `RootWidget::HitTest` → `Widget::GetCursor`。
	///          ★ **默认实现 = Arrow**（**非纯虚**——同 `OnCaptureLost` 先例）：
	///          既有 4 个实现者（`Window` + 3 个测试 Host）不实现即语义正确（箭头 = 旧行为）。
	[[nodiscard]] virtual CursorShape OnQueryCursor(int x, int y) const noexcept
	{
		(void)x;
		(void)y;
		return CursorShape::Arrow;
	}

	/// @brief 鼠标已离开窗口客户区（★ #72-③：`WM_MOUSELEAVE` 上报）
	/// @details 平台层在每条 `WM_MOUSEMOVE` 上（重）武装 `TrackMouseEvent(TME_LEAVE)`、
	///          系统投递 `WM_MOUSELEAVE` 时调本方法。框架据此清空 hover——修复前
	///          最后一个悬停控件在鼠标**出窗后**永收不到 `OnMouseLeave`（hover 只靠
	///          move 差分，悬停态卡死到下一次 move 才自愈）。
	///          ★ **默认实现 = 空实现**（非纯虚，同上先例）：测试 Host 不驱动真实
	///          鼠标踪迹，空实现即语义正确。
	virtual void OnMouseLeftWindow()
	{
	}
};

}
