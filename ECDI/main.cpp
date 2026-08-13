#include "ECDI/Window/Window.h"

#include "ECDI/Application/Application.h"
#include "ECDI/Core/Logger.h"
#include "ECDI/EventSystem/Input/KeyBoard/CharInputEvent.h"
#include "ECDI/Widget/Panel.h"
#include "ECDI/Widget/Label.h"
#include "ECDI/Widget/Button.h"
#include "ECDI/Layout/VerticalLayout.h"
#include "ECDI/Core/String.h"
#include "ECDI/Core/Point.h"
#include "ECDI/Core/ECDIAssert.h"
#include "ECDI/Render/PaintContext.h"
#include "ECDI/Render/Renderer.h"
#include "ECDI/Render/RecordingBackend.h"

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
	// ── 4.5 第二层测试：Command → Renderer → RecordingBackend（不碰 GDI）──
	// 验证：Renderer 是否正确把命令转换成 Backend 的 DrawRect() 调用（决策 12）
	{
		ECDI::RecordingBackend backend;
		ECDI::Renderer renderer(backend);

		ECDI::CommandBuffer commands;
		commands.emplace_back(ECDI::DrawRectCommand{ ECDI::Rect{ 0, 0, 100, 100 }, ECDI::Color::Red() });
		commands.emplace_back(ECDI::DrawRectCommand{ ECDI::Rect{ 10, 20, 30, 40 }, ECDI::Color::Gray() });

		renderer.Execute(commands);

		// 断言：2 条命令 → 2 次 DrawRect 调用，内容逐字段匹配
		FRAMEWORK_ASSERT(backend.draws.size() == 2);
		FRAMEWORK_ASSERT(backend.draws[0].rect.x == 0.0f && backend.draws[0].rect.width == 100.0f);
		FRAMEWORK_ASSERT(backend.draws[0].color.r == 1.0f && backend.draws[0].color.a == 1.0f);
		FRAMEWORK_ASSERT(backend.draws[1].rect.x == 10.0f && backend.draws[1].rect.y == 20.0f);
		FRAMEWORK_ASSERT(backend.draws[1].color.g == 0.5f);
	}

	// ── 4.6 第一层测试：Widget → PaintContext → CommandBuffer（命令断言，不碰 GDI）──
	// 验证：Panel::OnPaint 是否正确产生 DrawRectCommand（Rect/Color/顺序）
	{
		ECDI::RecordingBackend measurer;             // 兼 TextMeasurer（固定测量值，测试便利）
		ECDI::CommandBuffer commands;
		ECDI::PaintContext ctx(commands, measurer);  // 路线 X：构造注入 TextMeasurer&

		ECDI::Panel panel;
		panel.SetPosition(10, 20);
		panel.SetSize(100, 50);
		panel.Paint(ctx, 0, 0);

		FRAMEWORK_ASSERT(commands.size() == 1);
		const auto& cmd = std::get<ECDI::DrawRectCommand>(commands[0]);
		FRAMEWORK_ASSERT(cmd.rect.x == 10.0f && cmd.rect.y == 20.0f);
		FRAMEWORK_ASSERT(cmd.rect.width == 100.0f && cmd.rect.height == 50.0f);
		FRAMEWORK_ASSERT(cmd.color.r == 0.5f && cmd.color.g == 0.5f);   // Color::Gray()
	}

	// ── 5.1 文本链路测试：DrawTextCommand → Renderer → RecordingBackend（转发原样性）──
	// 验证：文本命令经 Renderer 展开转发后，Backend 收到的参数原样（D4 约束 2 / D5）
	{
		ECDI::RecordingBackend backend;
		ECDI::Renderer renderer(backend);

		ECDI::CommandBuffer commands;
		commands.emplace_back(ECDI::DrawTextCommand{
			ECDI::Point{ 30.0f, 40.0f },
			"Hello ECDI",
			ECDI::Color::Black(),
			ECDI::Font{ 14.0f, "Arial" }
		});
		renderer.Execute(commands);

		// 断言：1 条文本命令 → 1 次 DrawText 调用，参数原样转发
		FRAMEWORK_ASSERT(backend.textDraws.size() == 1);
		FRAMEWORK_ASSERT(backend.textDraws[0].pos.x == 30.0f && backend.textDraws[0].pos.y == 40.0f);
		FRAMEWORK_ASSERT(backend.textDraws[0].text == "Hello ECDI");
		FRAMEWORK_ASSERT(backend.textDraws[0].color.r == 0.0f);
		FRAMEWORK_ASSERT(backend.textDraws[0].font.size == 14.0f);
	}

	// ── 5.2 Label 文本链路：Label → PaintContext → DrawTextCommand（命令断言）──
	// 验证：第一个文本消费者——Label::OnPaint 正确产出 DrawTextCommand（text/pos/color/font）
	{
		ECDI::RecordingBackend backend;
		ECDI::CommandBuffer commands;
		ECDI::PaintContext ctx(commands, backend);

		ECDI::Label label("Hello ECDI");
		label.SetPosition(5, 5);
		label.SetSize(100, 30);
		label.Paint(ctx, 0, 0);

		FRAMEWORK_ASSERT(commands.size() == 1);        // OnPaint 确实被调用（经 Widget::Paint 分发）
		const auto& cmd = std::get<ECDI::DrawTextCommand>(commands[0]);
		FRAMEWORK_ASSERT(cmd.text == "Hello ECDI");
		FRAMEWORK_ASSERT(cmd.color.r == 0.0f);          // Color::Black()
		FRAMEWORK_ASSERT(cmd.pos.x == 5.0f);

		const float expectedY = 5.0f + (30.0f - backend.LineHeight(ECDI::Font{})) / 2.0f;
		FRAMEWORK_ASSERT(cmd.pos.y == expectedY);       // 验证"用了垂直居中公式"，不耦合模拟值
		FRAMEWORK_ASSERT(cmd.font.size == 14.0f);       // 默认 Font()
	}

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

	auto label2 = std::make_unique<ECDI::Label>("Second Window");
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
