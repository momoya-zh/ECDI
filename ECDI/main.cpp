#include <Windows.h>   // wWinMain 入口（WINAPI/HINSTANCE——7.1.5 前经 Application→WindowClass 传递 include，解耦后显式引入）


#include "ECDI/Window/Window.h"

#include "ECDI/Application/Application.h"
#include "ECDI/Core/Logger.h"
#include "ECDI/EventSystem/Input/KeyBoard/CharInputEvent.h"
#include "ECDI/Widget/Panel.h"
#include "ECDI/Widget/Label.h"
#include "ECDI/Widget/Button.h"
#include "ECDI/Widget/TextBox.h"
#include "ECDI/Layout/VerticalLayout.h"
#include "ECDI/Layout/HorizontalLayout.h"
#include "ECDI/Core/String.h"
#include "ECDI/Core/UTF8.h"

#include <iostream>
#include <string>
#include <utility>

#ifdef _DEBUG
#include "src/Tests/RunAllTests.h"
#endif

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
		// 仅显示可打印字符（退格 0x08/Delete 0x7F 等控制字符不打日志——避免调试器乱码显示）。
		// 控制字符仍正常传递给 Application（下方无条件转发）——不在此处改变事件语义，
		// 过滤是 TextBox 消费者的职责（DemoApplication 只是演示程序，不做语义判断）。
		const char32_t cp = event.GetCodepoint();
		if (cp >= 0x20 && cp != 0x7F){
			// 码点 → UTF-8 → UTF-16（Logger 契约）→ 调试输出；emoji 验证代理对组合
			std::string utf8 = "CharInput: ";
			utf8 += ECDI::EncodeUTF8(cp);
			ECDI::Logger::Log(ECDI::LogLevel::Info, ECDI::UTF8ToWide(utf8));
		}

		// ⚠️ 5.5.1.4 修复：override 会吞掉框架派发——显式转发基类（焦点控件 OnCharInput）
		ECDI::Application::OnCharInput(event);
	}
};

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow)
{
#ifdef _DEBUG
    ECDI::Test::RunAllTests();
#endif

	DemoApplication application;

	// ── 窗口 1：Widget Demo ────────────────────────────
	// 验证：WidgetTree + VerticalLayout + Paint + Focus/Event
	ECDI::Window& win1 = application.Create("ECDI Widget Demo", 500, 400);

	auto panel1 = std::make_unique<ECDI::Panel>();
	panel1->SetPosition(50, 30);
	panel1->SetSize(300, 250);
	panel1->SetLayout(std::make_unique<ECDI::VerticalLayout>());

	auto label1 = std::make_unique<ECDI::Label>("ECDI Widget System");
	label1->SetSize(300, 40);

	auto btn1 = std::make_unique<DemoButton>("Click Me");
	btn1->SetSize(200, 50);

	// 5.5.1-5.5.2：TextBox（预填 20-30 字符——验证拖选/裁切/Selection 交互）
	auto textBox1 = std::make_unique<ECDI::TextBox>("Hello World, this is a very long text.");
	textBox1->SetSize(200, 30);

	auto btn2 = std::make_unique<DemoButton>("Focus Test");
	btn2->SetSize(200, 50);

	panel1->AddChild(std::move(label1));
	panel1->AddChild(std::move(textBox1));
	panel1->AddChild(std::move(btn1));
	panel1->AddChild(std::move(btn2));

	win1.GetRootWidget().AddChild(std::move(panel1));

	// Phase 6：水平布局示例（HorizontalLayout 交互验证——与 panel1 垂直布局同屏对照）
	auto hpanel = std::make_unique<ECDI::Panel>();
	hpanel->SetPosition(50, 300);
	hpanel->SetSize(300, 50);
	hpanel->SetLayout(std::make_unique<ECDI::HorizontalLayout>());

	auto hb1 = std::make_unique<DemoButton>("B1");
	hb1->SetSize(100, 40);
	auto hb2 = std::make_unique<DemoButton>("B2");
	hb2->SetSize(100, 40);
	auto hb3 = std::make_unique<DemoButton>("B3");
	hb3->SetSize(100, 40);

	hpanel->AddChild(std::move(hb1));
	hpanel->AddChild(std::move(hb2));
	hpanel->AddChild(std::move(hb3));

	win1.GetRootWidget().AddChild(std::move(hpanel));
	win1.GetRootWidget().Arrange();   // 覆盖新 panel（与上重复调用，Arrange 幂等无害）

	win1.Show();

	// ── 窗口 2：Multi-Window ──────────────────────────
	// 验证：多窗口独立运行（Application 拥有多个 Window）
	ECDI::Window& win2 = application.Create("ECDI Second Window", 400, 300);

	auto panel2 = std::make_unique<ECDI::Panel>();
	panel2->SetPosition(50, 30);
	panel2->SetSize(300, 200);
	panel2->SetLayout(std::make_unique<ECDI::VerticalLayout>());

	auto label2 = std::make_unique<ECDI::Label>("Second Window");
	label2->SetSize(300, 40);

	auto btn3 = std::make_unique<DemoButton>("Another Button");
	btn3->SetSize(200, 50);

	panel2->AddChild(std::move(label2));
	panel2->AddChild(std::move(btn3));

	win2.GetRootWidget().AddChild(std::move(panel2));
	win2.GetRootWidget().Arrange();

	win2.Show();

	return application.Run();
}
