#include"Window.h"
#include"WindowClass.h"
#include"WindowEvent.h"
#include"Application.h"
#include"Logger.h"
#include"WindowDestroyEvent.h"
#include"WindowResizedEvent.h"
#include"WindowCloseRequsted.h"

#include<string>
#include<system_error>

Window::Window(Application* app,WindowClass &windowClass,const std::wstring& title, int width, int height):m_application(app) {
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
	switch (msg) {
	case WM_CLOSE: {
		WindowCloseRequestedEvent event(this);

		m_application->OnEvent(event);

		return 0;
	}
	case WM_DESTROY:{

		m_handle = nullptr;

		WindowDestroyedEvent event(this);

		m_application->OnEvent(event);

		return 0;
	}
	case WM_SIZE: {
		const int width = LOWORD(lParam);

		const int height = HIWORD(lParam);

		WindowResizedEvent event(
			this,
			width,
			height
		);
		m_application->OnEvent(event);
		break;
	}

	}

	return DefWindowProcW(hwnd,msg,wParam,lParam);
	
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
