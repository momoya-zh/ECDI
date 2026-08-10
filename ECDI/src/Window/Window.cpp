#include"ECDI/Window/Window.h"
#include"ECDI/Window/WindowClass.h"
#include"ECDI/Application/Application.h"
#include"ECDI/Widget/Widget.h"
#include"ECDI/Core/ECDIAssert.h"
#include"ECDI/Core/String.h"


#include<string>
#include<system_error>
#include<Windows.h>
namespace ECDI
{

Window::Window(Application* app,const WindowClass &windowClass,const std::string& title, int width, int height)
	: m_application(app)
	, m_messageHandler(m_application){

	// 公共 API 为 UTF-8（std::string），在平台边界转换到 UTF-16（字符串边界划分）
	const std::wstring wideTitle = UTF8ToWide(title);

	// 创建 Win32 窗口（WS_OVERLAPPEDWINDOW = 标题栏 + 边框 + 最小化/最大化/关闭按钮）
	m_handle = CreateWindowExW(
		0,
		windowClass.GetClassName(),
		wideTitle.c_str(),
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		width,
		height,
		nullptr,
		nullptr,
		windowClass.GetInstance(),
		this  // 通过 CREATESTRUCT 传递 this 指针，用于 WindowProc 中 GWLP_USERDATA 绑定

	);

	if (m_handle == nullptr){

		throw std::system_error(
			std::error_code(static_cast<int>(GetLastError()), std::system_category()),
			"CreateWindowExW failed");

	}

	// 创建 RootWidget（Widget 树的根节点，代表窗口客户区）
	m_rootWidget = std::make_unique<Widget>();

	FRAMEWORK_ASSERT(m_rootWidget != nullptr);

	// 同步 RootWidget 尺寸到窗口客户区（不含边框和标题栏）
	RECT rc{};

	if (GetClientRect(m_handle, &rc)){

		m_rootWidget->SetSize(rc.right - rc.left, rc.bottom - rc.top);

	}
}

void Window::Show() {

	if (m_handle != nullptr) {

		ShowWindow(m_handle, SW_SHOW);

		UpdateWindow(m_handle);

	}

}

Widget& Window::GetRootWidget() noexcept {

	return *m_rootWidget;

}

LRESULT CALLBACK Window::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {

	Window* window = nullptr;

	// WM_NCCREATE：窗口创建最早的消息，通过 CREATESTRUCT 绑定 HWND ↔ Window
	if (msg == WM_NCCREATE) {

		CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);

		window = static_cast<Window*>(create->lpCreateParams);

		window->m_handle = hwnd;

		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));

	}

	else {

		// 后续消息：从 GWLP_USERDATA 取回 Window 指针
		window = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

	}

	if (window) {

		if (msg==WM_PAINT){

			PAINTSTRUCT ps;

			HDC hdc = BeginPaint(hwnd, &ps);

			FillRect(hdc, &ps.rcPaint, (HBRUSH)GetStockObject(WHITE_BRUSH));

			window->m_rootWidget->Paint(hdc,0,0);

			EndPaint(hwnd, &ps);

			return 0;
		}

		return window->HandleMessage(hwnd,msg, wParam, lParam);

	}

	return DefWindowProcW(hwnd, msg, wParam, lParam);

}

LRESULT Window::HandleMessage(HWND hwnd,UINT msg, WPARAM wParam, LPARAM lParam) {

	// ── 内部状态同步（在 Event 翻译前完成，保证 Handler 看到最新状态）──
	switch (msg){

	case WM_DESTROY:
		m_handle = nullptr;
		break;

	case WM_SIZE:
		// 窗口大小变化时，同步 RootWidget 尺寸到新的客户区大小
		if (m_rootWidget){

			m_rootWidget->SetSize(LOWORD(lParam), HIWORD(lParam));

		}

		break;

	}

	// 将 Win32 消息翻译为 Framework Event 并派发
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

Widget* Window::GetFocusedWidget() const noexcept{

	return m_focusedWidget;

}

void Window::SetFocusedWidget(Widget* widget){

	// nullptr 表示清除焦点
	if (widget == nullptr){

		m_focusedWidget = nullptr;
		
		return;
	
	}

	// 沿 Parent 链回溯到树根，验证 widget 属于当前窗口的 Widget 树
	Widget* current = widget;

	while (current->GetParent()){

		current = current->GetParent();
	
	}

	// 树根必须是当前窗口的 RootWidget
	FRAMEWORK_ASSERT(current == &GetRootWidget());

	m_focusedWidget = widget;

}

}
