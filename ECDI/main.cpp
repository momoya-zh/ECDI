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

	// ── 5.3 Button 文本链路：先背景后文本，命令顺序 = 绘制顺序 ──
	// 验证：Button::OnPaint 先画蓝底（DrawRect）再画白字（DrawText，水平垂直居中）
	{
		ECDI::RecordingBackend backend;
		ECDI::CommandBuffer commands;
		ECDI::PaintContext ctx(commands, backend);

		ECDI::Button button("OK");
		button.SetPosition(10, 10);
		button.SetSize(100, 40);
		button.Paint(ctx, 0, 0);

		FRAMEWORK_ASSERT(commands.size() == 2);                       // 背景 + 文本
		const auto& bg = std::get<ECDI::DrawRectCommand>(commands[0]);   // 先背景
		FRAMEWORK_ASSERT(bg.rect.x == 10.0f && bg.rect.width == 100.0f);
		const auto& txt = std::get<ECDI::DrawTextCommand>(commands[1]);  // 后文本
		FRAMEWORK_ASSERT(txt.text == "OK");
		FRAMEWORK_ASSERT(txt.color == ECDI::Color::White());          // 完整比较（operator==）

		const float expectedX = 10.0f + (100.0f - backend.MeasureText(ECDI::Font{}, "OK").width) / 2.0f;
		FRAMEWORK_ASSERT(txt.pos.x == expectedX);                     // 水平居中（动态期望）
		const float expectedY = 10.0f + (40.0f - backend.LineHeight(ECDI::Font{})) / 2.0f;
		FRAMEWORK_ASSERT(txt.pos.y == expectedY);                     // 垂直居中（动态期望）
	}

	// ── 5.5.1.1 UTF-8 工具自测：码点 ↔ 字节转换正确性 ──
	// 字节布局：a=1 / 中=3 / 😀=4，总 8——验证索引转换不切字（TextBox 光标正确性前提）
	{
		FRAMEWORK_ASSERT(ECDI::EncodeUTF8(U'A') == "A");
		FRAMEWORK_ASSERT(ECDI::EncodeUTF8(U'中') == "\xE4\xB8\xAD");     // 3 字节
		FRAMEWORK_ASSERT(ECDI::EncodeUTF8(U'😀') == "\xF0\x9F\x98\x80"); // 4 字节

		const std::string s = "a中😀";
		FRAMEWORK_ASSERT(ECDI::CodepointIndexToByteOffset(s, 0) == 0);
		FRAMEWORK_ASSERT(ECDI::CodepointIndexToByteOffset(s, 1) == 1);   // a=1
		FRAMEWORK_ASSERT(ECDI::CodepointIndexToByteOffset(s, 2) == 4);   // 中=3
		FRAMEWORK_ASSERT(ECDI::CodepointIndexToByteOffset(s, 3) == 8);   // 😀=4 → 总 8
		FRAMEWORK_ASSERT(ECDI::ByteOffsetToCodepointIndex(s, 4) == 2);
		FRAMEWORK_ASSERT(ECDI::ByteOffsetToCodepointIndex(s, 8) == 3);
	}

	// ── 5.5.1.3 TextBox 编辑逻辑：Insert/Delete/Move（不依赖窗口；emoji/中文不切字）──
	{
		ECDI::TextBox box("abc");
		box.MoveCaretToEnd();
		box.InsertCodepoint(U'😀');                 // emoji 4 字节
		FRAMEWORK_ASSERT(box.GetText() == "abc😀");
		FRAMEWORK_ASSERT(box.GetCaret() == 4);
		box.DeleteBackward();                       // 删 😀（删前一码点字节区间）
		FRAMEWORK_ASSERT(box.GetText() == "abc");
		FRAMEWORK_ASSERT(box.GetCaret() == 3);

		box.MoveCaret(ECDI::TextBox::CaretDirection::Left);  // 光标到 'c' 后
		box.InsertCodepoint(U'中');                 // 中文 3 字节
		FRAMEWORK_ASSERT(box.GetText() == "ab中c");
		FRAMEWORK_ASSERT(box.GetCaret() == 3);

		box.DeleteForward();                        // 删光标后 = 'c'
		FRAMEWORK_ASSERT(box.GetText() == "ab中");
		FRAMEWORK_ASSERT(box.GetCaret() == 3);

		box.MoveCaretToStart();
		FRAMEWORK_ASSERT(box.GetCaret() == 0);
		box.MoveCaret(ECDI::TextBox::CaretDirection::Left);   // 头边界钳制
		FRAMEWORK_ASSERT(box.GetCaret() == 0);
		box.MoveCaretToEnd();
		box.MoveCaret(ECDI::TextBox::CaretDirection::Right);  // 尾边界钳制
		FRAMEWORK_ASSERT(box.GetCaret() == 3);
	}

	// ── Phase 6 HorizontalLayout：坐标计算断言（不依赖窗口；测试 1/2/3）──
	{
		// 测试 1：不同宽度累加（验证 x += childWidth，非固定步长）
		ECDI::Panel panel;
		panel.SetSize(300, 50);
		panel.SetLayout(std::make_unique<ECDI::HorizontalLayout>());

		auto box1 = std::make_unique<ECDI::Widget>();
		box1->SetSize(100, 30);
		auto box2 = std::make_unique<ECDI::Widget>();
		box2->SetSize(80, 30);
		auto box3 = std::make_unique<ECDI::Widget>();
		box3->SetSize(60, 30);
		auto* b1 = box1.get();
		auto* b2 = box2.get();
		auto* b3 = box3.get();

		panel.AddChild(std::move(box1));
		panel.AddChild(std::move(box2));
		panel.AddChild(std::move(box3));
		panel.Arrange();

		FRAMEWORK_ASSERT(b1->GetX() == 0    && b1->GetY() == 0);
		FRAMEWORK_ASSERT(b2->GetX() == 100  && b2->GetY() == 0);
		FRAMEWORK_ASSERT(b3->GetX() == 180  && b3->GetY() == 0);

		// 幂等（设计契约 1）：Arrange 重复调用位置一致
		panel.Arrange();
		FRAMEWORK_ASSERT(b1->GetX() == 0 && b2->GetX() == 100 && b3->GetX() == 180);
	}

	{
		// 测试 2：超出父容器——Layout 不裁切不换行（总宽 300 > 父宽 200，第 3 子仍放 x=200）
		ECDI::Panel panel;
		panel.SetSize(200, 50);
		panel.SetLayout(std::make_unique<ECDI::HorizontalLayout>());

		auto box1 = std::make_unique<ECDI::Widget>();
		box1->SetSize(100, 30);
		auto box2 = std::make_unique<ECDI::Widget>();
		box2->SetSize(100, 30);
		auto box3 = std::make_unique<ECDI::Widget>();
		box3->SetSize(100, 30);
		auto* b3 = box3.get();

		panel.AddChild(std::move(box1));
		panel.AddChild(std::move(box2));
		panel.AddChild(std::move(box3));
		panel.Arrange();

		FRAMEWORK_ASSERT(b3->GetX() == 200);
	}

	{
		// 测试 3：边界——0 子控件不崩溃 / 1 子控件归零位
		ECDI::Panel empty;
		empty.SetSize(200, 50);
		empty.SetLayout(std::make_unique<ECDI::HorizontalLayout>());
		empty.Arrange();   // count=0：循环不跑，不崩

		ECDI::Panel single;
		single.SetSize(200, 50);
		single.SetLayout(std::make_unique<ECDI::HorizontalLayout>());
		auto box = std::make_unique<ECDI::Widget>();
		box->SetSize(100, 30);
		auto* b = box.get();
		single.AddChild(std::move(box));
		single.Arrange();
		FRAMEWORK_ASSERT(b->GetX() == 0 && b->GetY() == 0);
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
