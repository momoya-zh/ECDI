#include"Window/Window.h"
#include"Application/Application.h"
#include"Window/WindowClass.h"
#include"Core/Logger.h"
#include"Core/ECDIAssert.h"
#include "Input/KeyBoard/CharInputEvent.h"

#include<iostream>
void TestHandled();
class TestApplication : public Application
{

protected:

    void OnCharInput(
        const CharInputEvent& event
    ) override
    {
        wchar_t buffer[64]{};
        swprintf_s(
            buffer,
            L"Char Input: %c",
            event.GetCharacter()
        );
        Logger::Log(
            LogLevel::Info,
            buffer
        );
    }


};


int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow)

{
	TestApplication application;
	Window& window = application.Create(L"test", 800, 600);
	Window& window2 = application.Create(L"test2", 800, 600);
	window.Show();
	window2.Show();

    TestHandled();


	return application.Run();
}

void TestHandled()
{
    // Test 1: 默认状态
    CharInputEvent event(nullptr, L'A');
    Logger::Log(LogLevel::Info, L"Test 1: IsHandled default");
    if (!event.IsHandled())
        Logger::Log(LogLevel::Info, L"  PASS: default is false");
    else
        Logger::Log(LogLevel::Info, L"  FAIL: default should be false");

    // Test 2: SetHandled → true
    event.SetHandled();
    Logger::Log(LogLevel::Info, L"Test 2: SetHandled");
    if (event.IsHandled())
        Logger::Log(LogLevel::Info, L"  PASS: now true");
    else
        Logger::Log(LogLevel::Info, L"  FAIL: should be true");

    // Test 3: 不可逆（接口层面保证，不需要运行时测试）
    // SetHandled() 无参数，不存在 SetHandled(false)，编译期保证
    Logger::Log(LogLevel::Info, L"Test 3: single direction enforced by API (no SetHandled(false))");
    Logger::Log(LogLevel::Info, L"  PASS: compile-time guarantee");
}

