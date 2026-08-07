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

/// @brief 测试用按钮：继承正式的 Button，override OnClick
class TestButton : public Button
{
protected:

	void OnClick() override
	{
		Logger::Log(LogLevel::Info, L"Button Click");
	}
};

/// @brief 测试用面板：继承正式的 Panel，响应鼠标事件用于 Bubbling 验证
class TestPanel : public Panel
{
protected:

	void OnMouseButtonDown(const MouseButtonDownEvent&) override
	{
		Logger::Log(LogLevel::Info, L"Panel MouseDown");
	}
};

/// @brief 测试用 Application
class TestApplication : public Application
{
protected:
	void OnCharInput(const CharInputEvent& event) override
	{
		wchar_t buffer[64]{};
		swprintf_s(
			buffer,
			L"Application CharInput: %c",
			event.GetCharacter()
		);
		Logger::Log(LogLevel::Info, buffer);
	}
};

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow)
{
	TestApplication application;

	// ── Label 状态测试（保留） ─────────────────────────
	auto label1 = std::make_unique<Label>(L"Hello ECDI");
	Logger::Log(LogLevel::Info, L"Label Test1 - Create: " + label1->GetText());

	auto label2 = std::make_unique<Label>(L"First");
	Logger::Log(LogLevel::Info, L"Label Test2 - Initial: " + label2->GetText());
	label2->SetText(L"Second");
	Logger::Log(LogLevel::Info, L"Label Test2 - After SetText: " + label2->GetText());

	std::wstring textCopy = L"Copy Test";
	auto label3 = std::make_unique<Label>(textCopy);
	Logger::Log(LogLevel::Info, L"Label Test3 - Copy: " + label3->GetText());
	Logger::Log(LogLevel::Info, L"Label Test3 - Original: " + textCopy);

	std::wstring textMove = L"Move Test";
	auto label4 = std::make_unique<Label>(std::move(textMove));
	Logger::Log(LogLevel::Info, L"Label Test4 - Move: " + label4->GetText());
	Logger::Log(LogLevel::Info, L"Label Test4 - Moved-from (may be empty): " + textMove);

	// ── Button 文本接口测试（栈对象，不接入树） ─────────
	// 测试1: const& 构造（左值复制）
	std::wstring buttonText = L"OK";
	Button btnConst(buttonText);
	Logger::Log(LogLevel::Info, L"Button Test1 - Const&: " + btnConst.GetText());
	Logger::Log(LogLevel::Info, L"Button Test1 - Original: " + buttonText);

	// 测试2: 移动构造
	std::wstring moveText = L"Move Button";
	Button btnMove(std::move(moveText));
	Logger::Log(LogLevel::Info, L"Button Test2 - Move: " + btnMove.GetText());

	// 测试3: SetText
	btnConst.SetText(L"Cancel");
	Logger::Log(LogLevel::Info, L"Button Test3 - After SetText: " + btnConst.GetText());

	// ── WidgetTree 接入测试 ─────────────────────────
	Window& window = application.Create(L"test", 800, 600);

	// Label 接入 RootWidget
	auto labelTree = std::make_unique<Label>(L"Tree Label");
	labelTree->SetPosition(100, 100);
	labelTree->SetSize(200, 50);
	window.GetRootWidget().AddChild(std::move(labelTree));

	// Panel → Button（测试 Bubbling + Click）
	auto panel = std::make_unique<TestPanel>();
	panel->SetPosition(50, 50);
	panel->SetSize(400, 300);

	auto button = std::make_unique<TestButton>();
	button->SetPosition(50, 50);
	button->SetSize(200, 80);

	panel->AddChild(std::move(button));
	window.GetRootWidget().AddChild(std::move(panel));

	// ── Layout 测试（T1/T2/T3 自动验证，T4 需手动拖窗口）──────

	// 辅助：打印 Widget 几何信息
	auto logGeo = [](const wchar_t* name, const Widget& w)
	{
		wchar_t buffer[128]{};
		swprintf_s(
			buffer,
			L"  %ls: x=%d y=%d w=%d h=%d",
			name,
			w.GetX(),
			w.GetY(),
			w.GetWidth(),
			w.GetHeight()
		);
		Logger::Log(LogLevel::Info, buffer);
	};

	// T1: 单层 VerticalLayout
	auto layoutPanel = std::make_unique<Widget>();
	layoutPanel->SetSize(400, 300);
	layoutPanel->SetLayout(std::make_unique<VerticalLayout>());

	auto layoutChild1 = std::make_unique<Widget>();
	layoutChild1->SetSize(100, 50);
	auto layoutChild2 = std::make_unique<Widget>();
	layoutChild2->SetSize(100, 30);
	auto layoutChild3 = std::make_unique<Widget>();
	layoutChild3->SetSize(100, 20);

	Widget* child1Ptr = layoutChild1.get();
	Widget* child2Ptr = layoutChild2.get();
	Widget* child3Ptr = layoutChild3.get();

	layoutPanel->AddChild(std::move(layoutChild1));
	layoutPanel->AddChild(std::move(layoutChild2));
	layoutPanel->AddChild(std::move(layoutChild3));

	Widget& root = window.GetRootWidget();
	root.AddChild(std::move(layoutPanel));

	root.Arrange();
	Logger::Log(LogLevel::Info, L"[T1] 单层 VerticalLayout，期望 x=0 y=0/50/80:");
	logGeo(L"child1", *child1Ptr);
	logGeo(L"child2", *child2Ptr);
	logGeo(L"child3", *child3Ptr);

	// T2: 幂等性（再 Arrange 一次，结果应不变）
	root.Arrange();
	Logger::Log(LogLevel::Info, L"[T2] 再次 Arrange（幂等），child2 应仍为 x=0 y=50:");
	logGeo(L"child2", *child2Ptr);

	// T3: 嵌套递归（Panel2 内部再挂 VerticalLayout）
	auto innerPanel = std::make_unique<Widget>();
	innerPanel->SetSize(200, 100);
	innerPanel->SetLayout(std::make_unique<VerticalLayout>());

	auto innerChild1 = std::make_unique<Widget>();
	innerChild1->SetSize(100, 20);
	auto innerChild2 = std::make_unique<Widget>();
	innerChild2->SetSize(100, 30);

	Widget* innerChild1Ptr = innerChild1.get();
	Widget* innerChild2Ptr = innerChild2.get();

	innerPanel->AddChild(std::move(innerChild1));
	innerPanel->AddChild(std::move(innerChild2));

	// 取到 layoutPanel 指针（AddChild 转移所有权后仍可通过 root 访问）
	Widget* layoutPanelPtr = root.GetChildAt(root.GetChildCount() - 1);
	layoutPanelPtr->AddChild(std::move(innerPanel));

	root.Arrange();
	Logger::Log(LogLevel::Info, L"[T3] 嵌套递归：外层 inner y=100（50+30+20），内层 y=0/20:");
	Widget* innerPanelPtr = layoutPanelPtr->GetChildAt(layoutPanelPtr->GetChildCount() - 1);
	logGeo(L"innerPanel", *innerPanelPtr);
	logGeo(L"innerChild1", *innerChild1Ptr);
	logGeo(L"innerChild2", *innerChild2Ptr);

	Logger::Log(LogLevel::Info, L"[T4] 手动拖动窗口大小，应触发 OnWindowResized → Arrange（观察日志无变化属正常，布局只依赖子尺寸）");

	// ── Paint System 测试（Phase3：WidgetTree → 像素链路）──

	// [Paint T1/T2] 单 Widget 绘制 + 嵌套坐标转换
	// 结构：Panel(50,50, 200x100) 内嵌 Button(20,20, 100x50)
	// 预期：灰色 Panel 在屏幕 (50,50)，蓝色 Button 在屏幕 (70,70)（offset 累加 50+20）
	Logger::Log(LogLevel::Info, L"[Paint T1/T2] Panel(50,50)+Button(20,20)，Button 应出现在屏幕 (70,70)");

	auto paintPanel = std::make_unique<Panel>();
	paintPanel->SetPosition(50, 50);
	paintPanel->SetSize(200, 100);

	auto paintButton = std::make_unique<Button>(L"Paint");
	paintButton->SetPosition(20, 20);
	paintButton->SetSize(100, 50);

	paintPanel->AddChild(std::move(paintButton));
	root.AddChild(std::move(paintPanel));

	// [Paint T3] Z-Order：后添加的 Button 覆盖先添加的
	// 结构：A(270,10, 80x60) + B(310,40, 80x60) 部分重叠，B 后添加应在顶层
	Logger::Log(LogLevel::Info, L"[Paint T3] Z-Order：两个重叠 Button，B(310,40) 应覆盖 A(270,10)");

	auto zButtonA = std::make_unique<Button>(L"A");
	zButtonA->SetPosition(270, 10);
	zButtonA->SetSize(80, 60);

	auto zButtonB = std::make_unique<Button>(L"B");
	zButtonB->SetPosition(310, 40);
	zButtonB->SetSize(80, 60);

	root.AddChild(std::move(zButtonA));
	root.AddChild(std::move(zButtonB));

	// [Paint T4] Visibility：隐藏的 Widget 不绘制
	// 预期：屏幕 (350,250) 处无蓝色按钮（若可见性失效则会显示 = 测试失败）
	Logger::Log(LogLevel::Info, L"[Paint T4] Visibility：隐藏 Button，屏幕 (350,250) 处不应出现");

	auto hiddenButton = std::make_unique<Button>(L"HIDDEN");
	hiddenButton->SetPosition(350, 250);
	hiddenButton->SetSize(100, 50);
	hiddenButton->SetVisible(false);
	root.AddChild(std::move(hiddenButton));

	// [Paint T5] Resize：拖动窗口应无残影（WM_PAINT 每帧 FillRect 白底 + 全树重绘，天然通过）
	Logger::Log(LogLevel::Info, L"[Paint T5] 拖动窗口大小，观察无残影（全量重绘）");

	Logger::Log(LogLevel::Info, L"All tests setup completed. Click button to test OnClick...");

	window.Show();

	return application.Run();
}
