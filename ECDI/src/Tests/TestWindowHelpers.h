#pragma once

#include <Windows.h>
#ifdef DrawText
#undef DrawText
#endif

namespace ECDI::Test {

/// @brief 无弹窗不抢焦的隐藏 OpenGL 测试窗口 RAII 封装（Phase 47 WGL 像素测试共用）
class HiddenGLWindow{
public:
	HiddenGLWindow(const wchar_t* className, int width, int height)
	{
		WNDCLASSW wc{};
		wc.lpfnWndProc = DefWindowProcW;
		wc.hInstance = GetModuleHandleW(nullptr);
		wc.lpszClassName = className;
		wc.style = CS_OWNDC;
		RegisterClassW(&wc);
		m_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
		                         className, L"ECDI_HiddenGL", WS_POPUP,
		                         0, 0, width, height,
		                         nullptr, nullptr, wc.hInstance, nullptr);
	}

	~HiddenGLWindow()
	{
		if (m_hwnd != nullptr)
		{
			DestroyWindow(m_hwnd);
			m_hwnd = nullptr;
		}
	}

	HiddenGLWindow(const HiddenGLWindow&) = delete;
	HiddenGLWindow& operator=(const HiddenGLWindow&) = delete;

	HWND Hwnd() const noexcept { return m_hwnd; }

private:
	HWND m_hwnd = nullptr;
};

} // namespace ECDI::Test
