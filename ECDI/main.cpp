#include "ECDI/Window/Window.h"
#include "ECDI/Application/Application.h"
#include "ECDI/Core/Logger.h"
#include "ECDI/EventSystem/Input/KeyBoard/CharInputEvent.h"
#include "ECDI/Widget/Panel.h"
#include "ECDI/Widget/Label.h"
#include "ECDI/Widget/Button.h"
#include "ECDI/Layout/VerticalLayout.h"
#include "ECDI/Core/String.h"
#include <iostream>
#include <string>
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

/// @brief 将 Unicode 码点编码为 UTF-8 追加到字符串（演示用；框架不负责 UTF-8 编码）
static void AppendUTF8(std::string& out, char32_t codepoint)
{
	if (codepoint <= 0x7F)
	{
		out += static_cast<char>(codepoint);
	}
	else if (codepoint <= 0x7FF)
	{
		out += static_cast<char>(0xC0 | (codepoint >> 6));
		out += static_cast<char>(0x80 | (codepoint & 0x3F));
	}
	else if (codepoint <= 0xFFFF)
	{
		out += static_cast<char>(0xE0 | (codepoint >> 12));
		out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (codepoint & 0x3F));
	}
	else
	{
		out += static_cast<char>(0xF0 | (codepoint >> 18));
		out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
		out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (codepoint & 0x3F));
	}
}

/// @brief 演示用 Application：override OnCharInput 输出日志（验证键盘分发链路）
class DemoApplication : public ECDI::Application
{
protected:

	void OnCharInput(const ECDI::CharInputEvent& event) override
	{
		// 码点 → UTF-8 → UTF-16（Logger 契约）→ 调试输出；emoji 验证代理对组合
		std::string utf8 = "CharInput: ";

		AppendUTF8(utf8, event.GetCodepoint());

		ECDI::Logger::Log(
			ECDI::LogLevel::Info,
			ECDI::UTF8ToWide(utf8)
		);
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
