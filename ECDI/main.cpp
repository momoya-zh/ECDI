#include "ECDI/Window/Window.h"
#include "ECDI/Application/Application.h"
#include "ECDI/Core/Logger.h"
#include "ECDI/EventSystem/Input/KeyBoard/CharInputEvent.h"
#include "ECDI/Widget/Panel.h"
#include "ECDI/Widget/Label.h"
#include "ECDI/Widget/Button.h"
#include "ECDI/Layout/VerticalLayout.h"
#include <iostream>
#include <utility>

/// @brief 演示用按钮：override OnClick 输出日志（验证事件分发 + Focus 获取）
class DemoButton : public ECDI::Button
{
public:

	using ECDI::Button::Button;	// 继承构造函数（DemoButton(L"xx") 等）

protected:

	void OnClick() override
	{
		ECDI::Logger::Log(ECDI::LogLevel::Info, L"Button Clicked");
	}
};

/// @brief 演示用 Application：override OnCharInput 输出日志（验证键盘分发链路）
class DemoApplication : public ECDI::Application
{
protected:

	void OnCharInput(const ECDI::CharInputEvent& event) override
	{
		wchar_t buffer[64]{};
		swprintf_s(
			buffer,
			L"CharInput: %c",
			event.GetCharacter()
		);
		ECDI::Logger::Log(ECDI::LogLevel::Info, buffer);
	}
};

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow)
{
	DemoApplication application;

	// ── 窗口 1：Widget Demo ────────────────────────────
	// 验证：WidgetTree + VerticalLayout + Paint + Focus/Event
	ECDI::Window& win1 = application.Create("ECDI Widget Demo", 500, 400);

	auto panel1 = std::make_unique<ECDI::Panel>();
	panel1->SetPosition(50, 30);
	panel1->SetSize(300, 250);
	panel1->SetLayout(std::make_unique<ECDI::VerticalLayout>());

	auto label1 = std::make_unique<ECDI::Label>(L"ECDI Widget System");
	label1->SetSize(300, 40);

	auto btn1 = std::make_unique<DemoButton>(L"Click Me");
	btn1->SetSize(200, 50);

	auto btn2 = std::make_unique<DemoButton>(L"Focus Test");
	btn2->SetSize(200, 50);

	panel1->AddChild(std::move(label1));
	panel1->AddChild(std::move(btn1));
	panel1->AddChild(std::move(btn2));

	win1.GetRootWidget().AddChild(std::move(panel1));
	win1.GetRootWidget().Arrange();

	win1.Show();

	// ── 窗口 2：Multi-Window ──────────────────────────
	// 验证：多窗口独立运行（Application 拥有多个 Window）
	ECDI::Window& win2 = application.Create("ECDI Second Window", 400, 300);

	auto panel2 = std::make_unique<ECDI::Panel>();
	panel2->SetPosition(50, 30);
	panel2->SetSize(300, 200);
	panel2->SetLayout(std::make_unique<ECDI::VerticalLayout>());

	auto label2 = std::make_unique<ECDI::Label>(L"Second Window");
	label2->SetSize(300, 40);

	auto btn3 = std::make_unique<DemoButton>(L"Another Button");
	btn3->SetSize(200, 50);

	panel2->AddChild(std::move(label2));
	panel2->AddChild(std::move(btn3));

	win2.GetRootWidget().AddChild(std::move(panel2));
	win2.GetRootWidget().Arrange();

	win2.Show();

	return application.Run();
}
