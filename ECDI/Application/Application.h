#pragma once
#include"WindowClass.h"
#include"Window.h"
#include"EventRouter.h"

#include<memory>
#include<string>
#include<vector>

class WindowCloseRequestedEvent;
class WindowCreatedEvent;
class WindowDestroyedEvent;
class WindowResizedEvent;
class Event;

class Application : public EventRouter
{
public :

	friend class Window;

	Application();

	int Run();

	WindowClass& GetWindowClass();

	Window& Create(const std::wstring&title,int width,int height);

	void Exit();

protected:
	void OnWindowCreated(
		const WindowCreatedEvent& event) override;

	void OnWindowDestroyed(
		const WindowDestroyedEvent& event) override;

	void OnWindowResized(
		const WindowResizedEvent& event) override;

	void OnWindowCloseRequested(
		const WindowCloseRequestedEvent& event) override;

private:
	void ProcessDeferredDestroy();

private:

	WindowClass m_windowClass;

	std::vector<std::unique_ptr<Window>> m_windows;

	std::vector<std::unique_ptr<Window>> m_deferredDestroy;

	bool m_running=true;
};
