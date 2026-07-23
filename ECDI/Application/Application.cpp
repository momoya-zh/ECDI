#include"Application.h"
#include"Window.h"
#include"WindowResizedEvent.h"
#include"WindowDestroyEvent.h"
#include"WindowCreatedEvent.h"
#include"WindowCloseRequsted.h"
#include"ECDIAssert.h"
#include"Logger.h"

#include<Windows.h>
Application::Application():m_windowClass(L"MyFrameWork",Window::WindowProc) {


}

WindowClass& Application::GetWindowClass() {
	return m_windowClass;
}

int Application::Run() {
	MSG message{};
	while (GetMessageW(&message, nullptr, 0, 0)) {
		TranslateMessage(&message);
		DispatchMessageW(&message);
		ProcessDeferredDestroy();
	}
	return static_cast<int>(message.wParam);
}

Window& Application::Create(const std::wstring& title, int width, int height) {
	m_windows.emplace_back(std::make_unique<Window>(
		this,
		m_windowClass,
		title,
		width,
		height
	));
	Window& window = *m_windows.back();

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
	m_deferredDestroy.clear();
}

void Application::OnWindowCreated(
	const WindowCreatedEvent& event)
{
	Logger::Log(
		LogLevel::Debug,
		L"Window Created Received"
	);
}

void Application::OnWindowDestroyed(
	const WindowDestroyedEvent& event){
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


	if (m_windows.empty()){
		Exit();
	}
}

void Application::OnWindowResized(
	const WindowResizedEvent& event)
{
	Logger::Log(
		LogLevel::Debug,
		L"Window Resize"
	);
}

void Application::OnWindowCloseRequested(
	const WindowCloseRequestedEvent& event
)
{
	Logger::Log(
		LogLevel::Debug,
		L"Window Close Requested"
	);
	event.GetWindow()->Release();
}