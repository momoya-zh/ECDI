#pragma once

#include "ECDI/Platform/PlatformApplication.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // Win32 宏防护（与本工程其它平台头统一——条 10）
#endif

#include <memory>
#include <string>

namespace ECDI{

class WindowClass;

/// @brief Win32 平台应用（7.1.5：GetMessageW 消息泵 + PostQuitMessage——消息循环唯一归属；
/// Phase 14：应用级托盘宿主——隐藏顶层 HWND + 托盘状态机 + TaskbarCreated 自愈）
/// @details 消息驱动模型：Run 循环内每条消息后 PerformDeferredCleanup
/// （延迟销毁安全时机——框架经 SetDeferredCleanup 注册逻辑，时机平台控制）。
/// 托盘宿主窗口（D2 硬契约）：**内部平台资源，不是 ECDI Window**——永不进入
/// Application::m_windows，不参与窗口枚举/所有权登记；由本类自行创建、持有、销毁。
class Win32PlatformApplication final : public PlatformApplication{
public:

	/// @brief 构造（声明于此、定义于 cpp——pimpl 约束：隐式内联构造会在含 make_unique 的
	/// 翻译单元被实例化，其异常清理路径要求 WindowClass 完整类型，C2027）
	Win32PlatformApplication();

	/// @brief 析构（四步不变量：NIM_DELETE → DestroyIcon → DestroyWindow → UnregisterClassW）
	/// @details 显式声明的原因：成员析构按声明逆序，m_trayHostClass（UnregisterClassW）会先于
	/// m_trayHostHwnd 隐式析构——违反「先销窗口再反注册」（K10）。四步在此显式完成，
	/// 成员析构时窗口已销、类反注册安全。
	~Win32PlatformApplication() override;

	int Run() override;

	void RequestExit() override;

	void SetTrayIcon(const TrayIconOptions& options) override;

	void RemoveTrayIcon() override;

	int ShowTrayMenu(const TrayMenu& menu) override;

	// ── 测试注入/观测（仅内部头——不进 Public API；条 51：seam 不出实现层）──

	/// @brief Shell 调用测试缝类型（v1.1 拍板：函数指针形态——不出实现层、保 final）
	/// @details 声明必须位于首个使用点之前——GCC 对成员函数形参不做延迟名字查找
	/// （放在 private 区会令 MinGW 报 "'NotifyShellFn' has not been declared"）
	using NotifyShellFn = BOOL (*)(UINT action, NOTIFYICONDATAW* nid);
	using DragFinishFn = void (*)(HDROP hDrop);

	void SetShellSeamsForTests(NotifyShellFn notifyShell, DragFinishFn dragFinish){
		m_notifyShell = notifyShell;
		m_dragFinish = dragFinish;
	}

	HWND GetTrayHostHwndForTests() const noexcept{ return m_trayHostHwnd; }

	UINT GetTaskbarCreatedMsgForTests() const noexcept{ return m_taskbarCreatedMsg; }

	/// @brief 托盘回调消息值（Phase 14 A3 修正：测试注入 WM_LBUTTONUP 序列需知道该消息号）
	UINT GetTrayCallbackMsgForTests() const noexcept;

	bool IsTrayRegisteredForTests() const noexcept{ return m_trayRegistered; }

	bool IsTrayDesiredForTests() const noexcept{ return m_trayDesired; }

	/// 锚点缓存观测（修正五回归：ContextMenu 分支曾漏更新 ⇒ 首次直接右键时菜单弹在屏幕左上角）
	int GetLastAnchorXForTests() const noexcept{ return m_lastAnchorX; }
	int GetLastAnchorYForTests() const noexcept{ return m_lastAnchorY; }

private:

	/// @brief 托盘宿主窗口与窗口类的懒创建（D2——首次 SetTrayIcon 时；零消费者零开销）
	void EnsureTrayHost();

	/// @brief 静态窗口过程（GWLP_USERDATA 绑定本实例——与 Win32PlatformWindow::WindowProc 同款）
	static LRESULT CALLBACK TrayHostProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	/// @brief 实例级宿主消息处理（TaskbarCreated 自愈 / 托盘回调 / 其它走 DefWindowProc）
	LRESULT HandleTrayHostMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	/// @brief 托盘回调消息翻译（v4 语义 → TrayEvent，D5 终版规则见详设 §3.3）
	void HandleTrayCallback(WPARAM wParam, LPARAM lParam);

	/// @brief 构造 NOTIFYICONDATAW（v1.1 参数化——SetTrayIcon 传新值、自愈/析构传当前值）
	NOTIFYICONDATAW BuildIconData(HICON icon, const std::wstring& tooltip);

	/// @brief Shell 调用测试缝适配器（类型定义见上方 public 区——默认真实 API）
	static BOOL NotifyShellAdapter(UINT action, NOTIFYICONDATAW* nid);
	static void DragFinishAdapter(HDROP hDrop);

	HWND m_trayHostHwnd = nullptr;         ///< 隐藏顶层宿主（内部资源——永不进 Application::m_windows）
	std::unique_ptr<WindowClass> m_trayHostClass;   ///< 宿主窗口类（独立实例、懒创建——析构顺序见初设 §6.4）
	UINT m_taskbarCreatedMsg = 0;          ///< RegisterWindowMessageW(L"TaskbarCreated")
	HICON m_trayIcon = nullptr;            ///< 当前图标（平台持有；失败降级系统默认——初设 §3.2）
	bool m_trayIconNeedsDestroy = false;   ///< 当前句柄是否 owned（LoadImageW=true / LoadIconW 共享=false）
	bool m_trayDesired = false;            ///< desired state（D9——应用意图）
	bool m_trayRegistered = false;         ///< shell registration state（D9——Shell 实际）
	std::wstring m_trayTooltip;            ///< 提示文本缓存（UTF-16——MODIFY 重建用）
	int m_lastAnchorX = 0;                 ///< 最近有效锚点（菜单定位）
	int m_lastAnchorY = 0;

	// ── Phase 14 A3 修正四（2026-09-17 实测）：双击序列尾部的 UP 吞除 ──
	// 实测双击 = `UP` + `DBLCLK` + `UP`（DBLCLK 与尾部 UP 间隔恒 0 ms）⇒ 尾部 UP 属双击序列，
	// 不吞掉会让双击报 3 个事件、末位多一个 Select。
	// 类型用 unsigned long long 而非 ULONGLONG——本头不引 Windows.h。
	unsigned long long m_swallowNextUpTick = 0;   ///< 武装时标（0 = 未武装；>0 且未超时 ⇒ 吞掉下一个 UP）

	NotifyShellFn m_notifyShell = &NotifyShellAdapter;   ///< Shell 测试缝（默认真实 API——测试注入替身）
	DragFinishFn m_dragFinish = &DragFinishAdapter;      ///< DragFinish 测试缝（同上）

};

}
