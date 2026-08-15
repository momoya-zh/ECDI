#pragma once

#include "ECDI/Platform/PlatformWindow.h"
#include "ECDI/Platform/PlatformWindowHost.h"
#include "ECDI/Window/WindowMessageHandler.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // Win32 宏防护：DrawText → DrawTextW 会污染公共头的方法名（与 GDIBackend.h 同款）
#endif

#include <string>

namespace ECDI{

class Application;   // 前置声明（7.1.1 过渡：翻译器构造需 Application*——7.1.2 拆派发后消除）
class WindowClass;   // 前置声明（构造参数引用，P4 定稿——WindowClass 标记 7.1.5）

/// @brief Win32 平台窗口实现（7.1.1 唯一实现）
/// @details 承载全部 Win32：窗口生命周期 / WindowProc / 消息处理 / 翻译器 / IME 平台调用。
/// 平台代码不再出现在框架类（Window）中——Window.h 零 Win32（V2 验收）。
/// 持 Host& 回调框架层（Platform 不认识框架具体类，只认识契约）。
class Win32PlatformWindow final : public PlatformWindow{
public:

	/// @param host     Host 回调（框架契约）
	/// @param app      Application（过渡：转给翻译器构造；7.1.2 拆派发后消除）
	/// @param windowClass 窗口类（Application 持有，此处仅用类名/实例句柄）
	/// @param title    窗口标题（UTF-8，内部转 UTF-16）
	/// @param width    窗口总宽度（含边框和标题栏）
	/// @param height   窗口总高度
	/// @throws std::system_error CreateWindowExW 失败
	Win32PlatformWindow(PlatformWindowHost& host, Application* app,
	                    const WindowClass& windowClass,
	                    const std::string& title, int width, int height);

	~Win32PlatformWindow() override;   ///< Release（幂等）

	void Show() override;
	bool Release() noexcept override;
	void Invalidate() override;
	Size GetClientSize() const override;
	void UpdateTextInputCaret(const Point& clientPos) override;
	void DestroyTextInputCaret() override;

	/// @brief 静态窗口过程（Application 注册 WindowClass 用；GWLP_USERDATA 绑定本实例）
	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	/// @brief 原生句柄（过渡：Window 构造给 GDIBackend::SetHwnd；7.1.4 PlatformRenderContext 替换）
	HWND GetHandle() const noexcept;

private:

	/// @brief 实例级消息处理（WindowProc 路由到这里）
	LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	PlatformWindowHost& m_host;	///< Host 回调（非拥有——Window 实现）
	Application* m_application;	///< 过渡：转给翻译器（7.1.2 消除）
	WindowMessageHandler m_messageHandler;	///< 翻译器（随消息处理整体搬入，结构保持现状）
	HWND m_hwnd = nullptr;	///< 窗口句柄（原 Window::m_handle）
	bool m_caretCreated = false;	///< 系统 caret 是否已创建（5.6 v1.0.3 懒创建标记）

};

}
