#include"ECDI/Application/Application.h"

#include"ECDI/Window/Window.h"
#include"ECDI/EventSystem/Window/WindowResizedEvent.h"
#include"ECDI/EventSystem/Window/WindowDestroyEvent.h"
#include"ECDI/EventSystem/Window/WindowCreatedEvent.h"
#include"ECDI/EventSystem/Window/WindowCloseRequsted.h"

#include"ECDI/EventSystem/Input/Mouse/MouseMoveEvent.h"
#include"ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include"ECDI/EventSystem/Input/Mouse/MouseButtonUpEvent.h"
#include"ECDI/EventSystem/Input/Mouse/MouseWheelEvent.h"

#include"ECDI/EventSystem/Input/KeyBoard/CharInputEvent.h"
#include"ECDI/EventSystem/Input/KeyBoard/KeyUpEvent.h"
#include"ECDI/EventSystem/Input/KeyBoard/KeyDownEvent.h"

#include"ECDI/Widget/Widget.h"
#include"ECDI/Core/ECDIAssert.h"
#include"ECDI/Core/Logger.h"

#include<Windows.h>
#include<algorithm>

namespace ECDI{

Application::Application():m_windowClass("ECDI FrameWork",Window::WindowProc) {

}

WindowClass& Application::GetWindowClass() {
	return m_windowClass;
}

int Application::Run() {


	MSG message{};
	// 标准 Win32 消息循环：GetMessage 返回 0 时退出（收到 WM_QUIT）
	while (GetMessageW(&message, nullptr, 0, 0)) {
		TranslateMessage(&message);
		DispatchMessageW(&message);
		// 每帧末尾处理延迟销毁的窗口（避免在消息处理过程中销毁窗口导致悬空指针）
		ProcessDeferredDestroy();
	}
	return static_cast<int>(message.wParam);
}

Window& Application::Create(const std::string& title, int width, int height) {
	m_windows.emplace_back(std::make_unique<Window>(
		this,
		m_windowClass,
		title,
		width,
		height
	));
	Window& window = *m_windows.back();

	// 手动派发 WindowCreatedEvent（不是 Win32 消息翻译的产物，是框架层语义事件）
	WindowCreatedEvent event(&window);

	OnEvent(event);


	return window;
}

void Application::Exit(){
	if (!m_running)
		return;
	m_running = false;
	PostQuitMessage(0);
}

void Application::ProcessDeferredDestroy() {
	// 清空延迟销毁列表（unique_ptr 析构，Window 资源释放）
	m_deferredDestroy.clear();
}

void Application::OnWindowCreated(
	const WindowCreatedEvent& event)
{
	// 默认空实现，子类可 override 执行初始化逻辑
}

void Application::OnWindowDestroyed(
	const WindowDestroyedEvent& event){
	// 将已销毁的 Window 从活跃列表移到延迟销毁列表
	auto it = std::find_if(
		m_windows.begin(),
		m_windows.end(),
		[&](const auto& window)
		{
			return window.get() ==
				event.GetWindow();
		});

	FRAMEWORK_ASSERT(it != m_windows.end());

	if (it == m_windows.end()){
		return;
	}

	m_deferredDestroy.emplace_back(
		std::move(*it)
	);


	m_windows.erase(it);


	// 所有窗口都关闭了，退出消息循环
	if (m_windows.empty()){
		Exit();
	}
}

void Application::OnWindowResized(
	const WindowResizedEvent& event)
{
	event.GetWindow()
		->GetRootWidget()
		.Arrange();
}

void Application::OnWindowCloseRequested(
	const WindowCloseRequestedEvent& event
)
{
	// 用户请求关闭 → 销毁底层 HWND
	event.GetWindow()->Release();
}

Widget* Application::FindTargetWidget(
	Window& window,
	int x,
	int y)const noexcept {

	return window.GetRootWidget().HitTest(x, y);

}

Widget* Application::FindFocusedWidget(Window& window)const noexcept {

	return window.GetFocusedWidget();

}

// ── 鼠标事件处理（HitTest → Target Dispatch → Bubbling）────────────

void Application::OnMouseMove(const MouseMoveEvent& event){

	// 1. HitTest：通过坐标找到目标 Widget
	Widget* target = FindTargetWidget(*event.GetWindow(), event.GetMouseX(), event.GetMouseY());

	// 2. 无目标则忽略事件
	if (target == nullptr) {

		return;

	}

	// 3. Bubbling：沿 Parent 链逐级调用 OnMouseMove
	Widget* current = target;

	while (current != nullptr) {

		current->OnMouseMove(event);

		current = current->GetParent();
	}

}

void Application::OnMouseButtonDown(const MouseButtonDownEvent& event){

	Widget* target = FindTargetWidget(*event.GetWindow(), event.GetMouseX(), event.GetMouseY());

	if (target == nullptr) {
	
		return;
	
	}
	
	// Focus 获取：命中且可聚焦 → 设置窗口焦点（在派发前，避免回调改树影响焦点）
	if (target->CanFocus()) {

		event.GetWindow()->SetFocusedWidget(target);

	}

	Widget* current = target;

	while (current != nullptr) {

		current->OnMouseButtonDown(event);

		current = current->GetParent();

	}

}

void Application::OnMouseButtonUp(const MouseButtonUpEvent& event){

	Widget* target = FindTargetWidget(*event.GetWindow(), event.GetMouseX(), event.GetMouseY());

	if (target == nullptr) {

		return;
	
	}

	Widget* current = target;

	while (current != nullptr) {

		current->OnMouseButtonUp(event);

		current = current->GetParent();

	}

}

void Application::OnMouseWheel(const MouseWheelEvent& event){

	Widget* target = FindTargetWidget(*event.GetWindow(), event.GetMouseX(), event.GetMouseY());

	if (target == nullptr) {

		return;

	}

	Widget* current = target;

	while (current != nullptr) {

		current->OnMouseWheel(event);

		current = current->GetParent();

	}

}

void Application::OnCharInput(const CharInputEvent&event){

	Widget* target = FindFocusedWidget(*event.GetWindow());

	if (target == nullptr) {

		return;

	}

	target->OnCharInput(event);

}

void Application::OnKeyDown(const KeyDownEvent& event) {

	Widget* target = FindFocusedWidget(*event.GetWindow());

	if (target == nullptr) {
	
		return;

	}

	target->OnKeyDown(event);

}

void Application::OnKeyUp(const KeyUpEvent& event) {

	Widget* target = FindFocusedWidget(*event.GetWindow());

	if (target == nullptr) {

		return;
	
	}

	target->OnKeyUp(event);

}

}
