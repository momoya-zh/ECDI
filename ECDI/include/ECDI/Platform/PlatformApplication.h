#pragma once

#include "ECDI/Application/TrayIcon.h"
#include "ECDI/EventSystem/Application/TrayEvent.h"

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

	// ── Phase 14：应用级托盘能力（R1–R7 / D1 / D2）──────────────────
	// ★ 应用级能力惯例（本阶段首次建立——与窗口级 R9 三步同构）：
	//   ① PlatformApplication 加能力 virtual（本文件）；
	//   ② Application 加公共方法透传（Application.h——应用层唯一入口）；
	//   ③ Shell 消息在具体实现内消化（Win32PlatformApplication::TrayHostProc）。

	/// @brief 设置（或更新）托盘图标（R1——**幂等配置语义**，D11）
	/// @param options 图标配置（资源 ID + 提示文本）
	/// @details 宽容语义：**未注册 ⇒ 注册；已注册 ⇒ 更新**（不存在"重复 Add 报错"分支）。
	/// @pre 无（Run() 前后均可调用）
	virtual void SetTrayIcon(const TrayIconOptions& options) = 0;

	/// @brief 移除托盘图标（R1——**幂等**，D11）
	/// @details **未注册 ⇒ no-op**（不抛异常、不记错误）。
	/// ⚠️ 移除后收到 `TaskbarCreated` **不得重加**（D9 双态模型）——
	/// 这是本阶段最容易写错的分支。
	virtual void RemoveTrayIcon() = 0;

	/// @brief 弹出托盘菜单并**同步返回**选中项 ID（R5 / D6 / D10）
	/// @param menu 菜单内容（一级 + 纯文本 + ID）
	/// @return 选中的 `TrayMenuItem::id`；**0 = 未选中 / 取消**（含空菜单）
	/// @details **同步语义**：内部 TrackPopupMenu(TPM_RETURNCMD) 期间消息循环由其接管，
	/// 该期间不派发框架事件（系统模态菜单固有行为——D10 已记账）。
	/// 弹出位置 = **最近一次托盘事件的锚点**（R4 携带）；从未收到事件时用托盘图标位置。
	/// @pre 无
	virtual int ShowTrayMenu(const TrayMenu& menu) = 0;

	// ── Phase 20：应用级 DPI 感知声明（★ 沿用上方应用级能力惯例的三步模板）────

	/// @brief 声明进程 DPI 感知级别（Phase 20 △22——**应用级能力惯例第 ① 步**）
	/// @details 由 `Application` 构造期调用，因此**早于任何窗口创建**（窗口只在 `Create` 内建）
	///          ⇒ 使框架既有的「公共 API 语义恒为 DIP」契约真正生效（Phase 13 立 · Phase 20 兑现）。
	///          ★ **失败容忍**：实现者失败时**只记日志**——**不抛异常、不断言、不主动降级尝试**
	///          （「本进程已被声明过」「系统策略拒绝」均属常态，主动降级反而更差）。
	///          ★ 框架**始终读取**当下真实 DPI、**不假定本声明成功**
	///          （初步设计 §2.3.1「读取而非假定」）。
	/// @note ★ 为何走应用级接缝、而**不在** `Application` 里直接调 Win32 API：
	///       核心不变量「**每个 Win32 API 唯一归属**」+「应用级接缝 = `PlatformApplication → Application`」。
	virtual void DeclareDpiAwareness() = 0;

	/// @brief 注册托盘事件上行通道（D3——复刻 SetDeferredCleanup 的注入模式）
	/// @param sink 事件接收器（**非拥有**；调用时 Application 必然存活——O-1 契约 A）
	/// @details 与 SetDeferredCleanup 同款：基类持 std::function，实现者负责调用。
	/// ⚠️ 销毁顺序契约：sink 必须在宿主窗口销毁**之前**清空（初设 §6.3/§6.4）。
	void SetTrayEventSink(const std::function<void(const TrayEvent&)>& sink){
		m_trayEventSink = sink;
	}

protected:

	/// @brief 执行托盘事件上行（实现者在收到 Shell 回调时调用——未注册则空操作）
	void EmitTrayEvent(const TrayEvent& event) const{
		if (m_trayEventSink){
			m_trayEventSink(event);
		}
	}

private:

	std::function<void(const TrayEvent&)> m_trayEventSink;   ///< 托盘事件上行（Application 注册）

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
