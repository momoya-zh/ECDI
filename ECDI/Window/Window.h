#pragma once
#include<string>
#include<Windows.h>
#include"WindowMessageHandler.h"

class WindowClass;
class Application;
class Window {
	public:


		Window(Application* app,
			WindowClass &windowClass,
			const std::wstring&title,
			int width,
			int height);

		Window(const Window&) = delete;

		Window& operator=(const Window&) = delete;

		Window(Window&&) = delete;

		Window& operator=(Window&&) = delete;

		~Window()noexcept;

		void Show();

		static LRESULT CALLBACK WindowProc(
		HWND hwnd,
		UINT msg,
		WPARAM wParam,
		LPARAM lParam
		);

		bool Release()noexcept;
private:
		LRESULT HandleMessage(
		HWND hwnd,
		UINT msg,
		WPARAM wParam,
		LPARAM lParam
		);

		HWND m_handle=nullptr;

		Application* m_application = nullptr;

		WindowMessageHandler m_messageHandler;
};