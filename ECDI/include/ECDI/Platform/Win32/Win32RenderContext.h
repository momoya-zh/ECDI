#pragma once

#include "ECDI/Platform/PlatformRenderContext.h"

#include <Windows.h>

namespace ECDI{

/// @brief Win32 渲染上下文（7.1.4：HWND 的类型安全容器——防 void* 类型擦除；
/// 与 Win32PlatformWindow 同目录同族——GPT 修订：句柄属平台非渲染）
/// @details 极简值类（头内联，无 cpp）——可赋值：平台窗口 CreateWindowExW
/// 成功后 SetHandle(m_hwnd)（构造体内绑定，F9）。
class Win32RenderContext : public PlatformRenderContext{
public:
	Win32RenderContext() noexcept = default;

	explicit Win32RenderContext(HWND hwnd) noexcept : m_hwnd(hwnd){}

	HWND GetHandle() const noexcept{ return m_hwnd; }

	void SetHandle(HWND hwnd) noexcept{ m_hwnd = hwnd; }

private:
	HWND m_hwnd = nullptr;
};

}
