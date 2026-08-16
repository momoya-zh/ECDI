#pragma once

#include <functional>

namespace ECDI{

/// @brief 平台应用抽象（7.1.5：事件循环下沉——消息泵 + 退出请求 + 延迟清理时机）
/// @details 消息驱动模型（GUI 框架定位，非帧循环）：平台控制消息循环，
/// 框架注册延迟清理逻辑（资源生命周期管理——延迟销毁安全窗口，避免在
/// DispatchMessage 栈上销毁窗口悬空指针）。
/// 唯一实现：Win32PlatformApplication（X11/Wayland 只留接口，YAGNI）。
class PlatformApplication{
public:
	virtual ~PlatformApplication() = default;

	/// @brief 注册延迟清理回调（框架层 ProcessDeferredDestroy——时机平台控制，逻辑框架提供）
	/// @param cleanup 框架清理逻辑（每条消息处理后执行）
	void SetDeferredCleanup(const std::function<void()>& cleanup){ m_deferredCleanup = cleanup; }

	/// @brief 泵消息循环（阻塞直到退出；循环内每条消息处理后调用 PerformDeferredCleanup）
	/// @return 退出码（框架 Run 的返回值）
	virtual int Run() = 0;

	/// @brief 请求退出消息循环（异步——循环在下一轮判断退出）
	virtual void RequestExit() = 0;

protected:
	/// @brief 执行延迟清理（Run 循环内调用点——实现者职责；未注册则空操作）
	void PerformDeferredCleanup() const{
		if (m_deferredCleanup){
			m_deferredCleanup();
		}
	}

private:
	std::function<void()> m_deferredCleanup;   ///< 框架清理逻辑（Application::ProcessDeferredDestroy）
};

}
