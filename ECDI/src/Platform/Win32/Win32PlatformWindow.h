#pragma once

#include "ECDI/Platform/PlatformWindow.h"
#include "ECDI/Platform/PlatformWindowHost.h"
#include "ECDI/Window/ChromeMode.h"
#include "ECDI/Window/WindowLayer.h"
#include "ECDI/Window/WindowState.h"
#include "Platform/Win32/Win32RenderContext.h"
#include "Platform/Win32/WindowMessageHandler.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // Win32 宏防护：DrawText → DrawTextW 会污染公共头的方法名（与 GDIBackend.h 同款）
#endif

#include <string>

namespace ECDI{

/// @brief Win32 平台窗口实现（7.1.1 唯一实现）
/// @details 承载全部 Win32：窗口生命周期 / WindowProc / 消息处理 / 翻译器 / IME 平台调用。
/// 平台代码不再出现在框架类（Window）中——Window.h 零 Win32（V2 验收）。
/// 持 Host& 回调框架层（Platform 不认识框架具体类，只认识契约）。
/// 7.1.5：窗口类注册下沉（WindowClass::Instance()——窗口系统资源归窗口类自身）。
class Win32PlatformWindow final : public PlatformWindow{
public:

	/// @param host     Host 回调（框架契约）
	/// @param title    窗口标题（UTF-8，内部转 UTF-16）
	/// @param width    窗口总宽度（含边框和标题栏）
	/// @param height   窗口总高度
	/// @throws std::system_error CreateWindowExW 失败
	Win32PlatformWindow(PlatformWindowHost& host,
	                    const std::string& title, int width, int height);

	~Win32PlatformWindow() override;   ///< Release（幂等）

	void Show() override;
	bool Release() noexcept override;
	void Invalidate() override;
	Size GetClientSize() const override;
	const PlatformRenderContext& GetRenderContext() const override;   ///< 7.1.4：后端经此拿平台句柄
	void UpdateTextInputCaret(const CaretGeometry& geometry) override;
	void DestroyTextInputCaret() override;
	std::string GetClipboardText() const override;   ///< 8.5.1：剪贴板 capability（CF_UNICODETEXT + 转换封装）
	void SetClipboardText(const std::string& text) override;
	void StartTimer(int timerId, unsigned int intervalMs) override;   ///< 8.5.1：通用定时器（SetTimer）
	void StopTimer(int timerId) override;   ///< 8.5.1：StopTimer（KillTimer，幂等）

	// ── Phase 12：WindowChrome / 窗口层级（配置期——Window 构造后 / Show() 前）──
	void SetChromeMode(ChromeMode mode) override;
	void SetCaptionHeight(int height) override;
	void SetResizeInset(int inset) override;
	void SetWindowLayer(WindowLayer layer) override;

	// ── Phase 12：窗口状态（运行期——Show() 之后才有效）──────────
	void Minimize() override;
	void Maximize() override;
	void Restore() override;

	/// @brief 静态窗口过程（应用层注册 WindowClass 用；GWLP_USERDATA 绑定本实例）
	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:

	/// @brief 实例级消息处理（WindowProc 路由到这里）
	LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	/// @brief 最大化客户区校正（R4——rcWork 唯一基准，D-COMP-1：不引入补偿）
	void AdjustMaximizedClientRect(HWND hwnd, RECT& rcClient);

	/// @brief DIP → 物理像素（D-DPI-1：窗口 DPI——非鼠标所在显示器）
	static int DipToPixels(int dip, HWND hwnd);

	/// @brief DWM 增强（R6——系统阴影 + Win11 圆角；失败容忍，仅日志不中断）
	void ApplyDwmEnhancements(HWND hwnd);

	PlatformWindowHost& m_host;	///< Host 回调（非拥有——Window 实现）
	WindowMessageHandler m_messageHandler;	///< 翻译器（7.1.2：构造传 m_host——不再认识应用层）
	HWND m_hwnd = nullptr;	///< 窗口句柄（原 Window::m_handle）
	bool m_caretCreated = false;	///< 系统 caret 是否已创建（5.6 v1.0.3 懒创建标记）
	Win32RenderContext m_renderContext;	///< 渲染上下文（7.1.4：构造体 CreateWindowExW 成功后 SetHandle）

	// ── Phase 12：chrome / 层级 / 生命周期状态 ─────────────────────

	ChromeMode m_chromeMode = ChromeMode::Normal;	///< chrome 形态（R1——配置期生效，Show 后拒绝切换）
	int m_captionHeight = 32;	///< 标题栏命中高度（R3；逻辑坐标 DIP——默认 32）
	int m_resizeInset = 8;	///< 缩放热区宽度（R3；逻辑坐标 DIP——默认 8）
	WindowLayer m_windowLayer = WindowLayer::Normal;	///< 层级档位（R10——语义状态，不随实现路径降级）

	bool m_shown = false;	///< 是否已调用过 Show()（配置期/运行期判据——「API 调用事实」，非「系统当前可见」）
	bool m_chromeConfigured = false;	///< ChromeMode 是否已配置（D-CHROME-1：一次确定——重复调用一律 Warning + 忽略）
	WindowState m_lastWindowState = WindowState::restored;	///< 状态事件去重锚（零值 = 窗口初始态）

	int m_imeResultPendingChars = 0;	///< 待吞掉的 IME 结果 WM_CHAR 数（8.5.1：GCS_RESULTSTR 已提交框架——
	///< 系统随后仍发结果 WM_CHAR（DefWindowProc 通道），吞掉防双写；UTF-16 码元计数 = WM_CHAR 消息数）

};

}
