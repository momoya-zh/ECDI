#pragma once

#include <string>

namespace ECDI{

class Window;   // 前置声明——GetWindow 返回框架窗口（平台层不 include Window.h，只认此契约）
class Event;    // 前置声明——OnEvent 引用参数（不需要完整定义）

/// @brief 平台窗口宿主（7.1 D2 核心）：Platform 不认识框架具体类，只认识此契约
/// @details Window 实现此接口；Win32PlatformWindow 持 Host& 回调。
/// 平台事件 → Host 回调 → 框架响应（契约语言：平台层"发生了窗口事件"，框架层"响应"）。
///
/// 接口收敛说明（YAGNI，2026-08-15）：
/// - 无 OnDestroyed：WM_DESTROY 后 Win32PlatformWindow 内部置空句柄，框架层无需动作
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
};

}
