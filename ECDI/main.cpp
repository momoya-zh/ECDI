#include"Window/Window.h"
#include"Application/Application.h"
#include"Window/WindowClass.h"
#include"Core/Logger.h"
#include"Core/ECDIAssert.h"
#include<iostream>
#include"EventSystem/Event.h"
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow)

{
	Application application;
	Window& window = application.Create(L"test", 800, 600);
	Window& window2 = application.Create(L"test2", 800, 600);
	window.Show();
	window2.Show();


	return application.Run();
}

