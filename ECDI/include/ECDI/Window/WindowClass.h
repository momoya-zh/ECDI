#pragma once

#include<Windows.h>
#include<string>
namespace ECDI
{

/// @brief Win32 窗口类（WNDCLASS）的 RAII 包装
/// @details
/// 构造时 RegisterClassW，析构时 UnregisterClassW。
/// 禁止拷贝，支持移动（转移注册所有权）。
class WindowClass
{
public:

	/// @param className 窗口类名（注册到系统）
	/// @param windowProc 窗口消息回调函数
	WindowClass(const std::string& className,WNDPROC windowProc);

	// 禁止拷贝
	WindowClass(const WindowClass&) = delete;

	WindowClass& operator=(const WindowClass&) = delete;

	// 支持移动（转移注册所有权）
	WindowClass(WindowClass&& other) noexcept;

	WindowClass& operator=(WindowClass&& other) noexcept;

	~WindowClass() noexcept;

	/// @brief 获取窗口类名
	const wchar_t* GetClassName() const;

	/// @brief 获取当前模块实例句柄
	HINSTANCE GetInstance() const;

	/// @brief 手动释放（反注册窗口类）
	bool Release() noexcept;

private:

	std::wstring m_className;	///< 窗口类名

	HINSTANCE m_instance=nullptr;	///< 模块实例句柄

	WNDPROC m_windowProc=nullptr;	///< 窗口消息回调

	bool m_registered = false;	///< 是否已注册

};

}
