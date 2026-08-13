#include "ECDI/Window/Window.h"

#include "ECDI/Window/WindowClass.h"
#include "ECDI/Application/Application.h"
#include "ECDI/Widget/Widget.h"
#include "ECDI/Core/ECDIAssert.h"
#include "ECDI/Core/String.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyDownEvent.h"
#include "ECDI/Render/PaintContext.h"

#include <Windows.h>

#include <string>
#include <system_error>
#include <vector>

namespace ECDI{

namespace {

// 5.4.4：DFS 收集可聚焦控件（树前序：父 → 子；匿名 namespace——Window 内部辅助，非公开能力）
void CollectFocusables(Widget* node, std::vector<Widget*>& out){

	if (node->CanFocus()){

		out.push_back(node);

	}

	for (size_t i = 0; i < node->GetChildCount(); ++i){

		CollectFocusables(node->GetChildAt(i), out);

	}

}

}

Window::Window(Application* app,const WindowClass &windowClass,const std::string& title, int width, int height)
	: m_application(app)
	, m_messageHandler(m_application)
	, m_renderer(m_backend){   // 决策 35：m_backend 先构造（默认构造），m_renderer 绑定引用

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

	// 决策 35：hwnd 就绪后注入渲染后端（BeginFrame 才真正使用）
	m_backend.SetHwnd(m_handle);

	// 创建 RootWidget（Widget 树的根节点，代表窗口客户区）
	m_rootWidget = std::make_unique<Widget>();

	// 5.4.1：根与 Window 建立关联（Widget::Invalidate/HasFocus 上溯到根后走它）
	m_rootWidget->SetWindow(this);

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

		return window->HandleMessage(hwnd,msg, wParam, lParam);

	}

	return DefWindowProcW(hwnd, msg, wParam, lParam);

}

LRESULT Window::HandleMessage(HWND hwnd,UINT msg, WPARAM wParam, LPARAM lParam) {

	// ── 内部状态同步（在 Event 翻译前完成，保证 Handler 看到最新状态）──
	switch (msg){

	case WM_PAINT:
		// 决策 39：绘制不走翻译器（不是 Event），Window 编排整帧
		PaintFrame();
		return 0;

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

void Window::PaintFrame()
{
	// 决策 10/13/33：完整编排，严格配对
	m_commands.clear();                              // 决策 4：复用缓冲
	PaintContext ctx(m_commands, m_backend);         // 路线 X：m_backend 兼 TextMeasurer（测量帧无关）
	m_rootWidget->Paint(ctx, 0, 0);                  // 决策 6：根从 (0,0)，offset 累加
	m_renderer.BeginFrame();                         // 决策 13：转发
	m_renderer.Execute(m_commands);
	m_renderer.EndFrame();
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

void Window::Invalidate(){

	// 5.4.1：整个客户区无效 → WM_PAINT → PaintFrame（GDIBackend 双缓冲自动重绘）
	if (m_handle){

		InvalidateRect(m_handle, nullptr, FALSE);

	}

}

void Window::SetCaptureWidget(Widget* widget){

	// 5.4.2：隐式捕获——Down 命中设置、Up 后释放；非拥有指针（生命周期随 Widget 树）
	m_captureWidget = widget;

}

Widget* Window::GetCaptureWidget() const noexcept{

	return m_captureWidget;

}

void Window::SetFocusedWidget(Widget* widget){

	// 5.4.3：同控件短路——避免重复设置触发 Lost+Gained 空转
	if (m_focusedWidget == widget){

		return;

	}

	// 5.4.3：通知旧焦点失去（数据变更前）
	if (m_focusedWidget){

		m_focusedWidget->OnFocusLost();

	}

	m_focusedWidget = widget;

	if (widget != nullptr){

		// 沿 Parent 链回溯到树根，验证 widget 属于当前窗口的 Widget 树
		Widget* current = widget;

		while (current->GetParent()){

			current = current->GetParent();

		}

		// 树根必须是当前窗口的 RootWidget
		FRAMEWORK_ASSERT(current == &GetRootWidget());

		// 5.4.3：通知新焦点获得
		widget->OnFocusGained();

	}

	// 5.4.3：焦点变化重绘（设新/清空都重绘）
	Invalidate();

}

void Window::HandleKeyDown(const KeyDownEvent& event){

	// 5.4.4：Tab 框架拦截（焦点导航是 Window 职责；仅正向——Shift+Tab 留 5.5 与 KeyEvent 修饰键一起）
	if (event.GetKeyCode() == KeyCode::Tab){

		FocusNext();

		return;

	}

	if (m_focusedWidget){

		m_focusedWidget->OnKeyDown(event);

	}

}

void Window::FocusNext(int direction){

	// 5.4.4：树前序收集 CanFocus 控件 → 当前焦点 index + direction → 循环取模
	std::vector<Widget*> focusables;

	CollectFocusables(m_rootWidget.get(), focusables);

	if (focusables.empty()){

		return;

	}

	int index = -1;

	for (size_t i = 0; i < focusables.size(); ++i){

		if (focusables[i] == m_focusedWidget){

			index = static_cast<int>(i);

			break;

		}

	}

	const int size = static_cast<int>(focusables.size());

	const int next = (index < 0) ? 0 : (index + direction + size) % size;

	SetFocusedWidget(focusables[next]);

}

}
