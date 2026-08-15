#pragma once

namespace ECDI{

class Window;   // 前置声明——GetWindow 返回框架窗口（平台层不 include Window.h，只认此契约）

/// @brief 平台窗口宿主（7.1 D2 核心）：Platform 不认识框架具体类，只认识此契约
/// @details Window 实现此接口；Win32PlatformWindow 持 Host& 回调。
/// 平台事件 → Host 回调 → 框架响应（契约语言：平台层"发生了窗口事件"，框架层"响应"）。
///
/// 接口收敛说明（YAGNI，2026-08-15）：
/// - 无 OnEvent：7.1.1 翻译器保持现状直派 Application（F1），7.1.2 拆派发时再加
/// - 无 OnIMEComposition：翻译器 WM_IME_* case 保持现状直调 window->NotifyIMEComposition()
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
};

}
