#pragma once

#include <optional>
#include <Windows.h>

#include "Input/Mouse/MouseButton.h"
#include "Input/KeyBoard/KeyCode.h"

class Window;
class Application;

class WindowMessageHandler
{
public:
	explicit WindowMessageHandler(Application* app) noexcept;

	// nullopt = 调用方走 DefWindowProc
	// 有值    = 调用方用该值返回

	std::optional<LRESULT> Handle(
		Window* window,
		HWND hwnd,
		UINT msg,
		WPARAM wParam,
		LPARAM lParam
	);

private:
	Application* m_application;

	static MouseButton TranslateMouseButton(UINT message, WPARAM wParam);
	static KeyCode TranslateKeyCode(WPARAM wParam, LPARAM lParam);
};


