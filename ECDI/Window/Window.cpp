#include"Window.h"
#include"WindowClass.h"
#include"Application.h"

#include<string>
#include<system_error>
#include<Windows.h>

Window::Window(Application* app,WindowClass &windowClass,const std::wstring& title, int width, int height)
	: m_application(app)
	, m_messageHandler(m_application)
{
	m_handle = CreateWindowExW(
		0,
		windowClass.GetClassName(),
		title.c_str(),
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		width,
		height,
		nullptr,
		nullptr,
		windowClass.GetInstance(),
		this
	);
	if (m_handle == nullptr)
	{
		throw std::system_error(
			std::error_code(static_cast<int>(GetLastError()), std::system_category()),
			"CreateWindowExW failed");
	}
}

void Window::Show() {
	if (m_handle != nullptr) {
		ShowWindow(m_handle, SW_SHOW);
		UpdateWindow(m_handle);
	}
}

LRESULT CALLBACK Window::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	Window* window = nullptr;
	if (msg == WM_NCCREATE) {
		CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
		window = static_cast<Window*>(create->lpCreateParams);
		window->m_handle = hwnd;
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));

	}
	else {
		window = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
	}
	if (window) {
		return window->HandleMessage(hwnd,msg, wParam, lParam);
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT Window::HandleMessage(HWND hwnd,UINT msg, WPARAM wParam, LPARAM lParam) {
	// WM_DESTROY: 清理 m_handle（Window 资源管理，不是消息翻译职责）
	if (msg == WM_DESTROY) {
		m_handle = nullptr;
	}

	auto result = m_messageHandler.Handle(this, hwnd, msg, wParam, lParam);
	if (result) {
		return *result;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool Window::Release() noexcept {
	if (m_handle==nullptr) {
		return true;
	}
	return DestroyWindow(m_handle) != FALSE;
}

Window::~Window()noexcept {
	Release();
}
