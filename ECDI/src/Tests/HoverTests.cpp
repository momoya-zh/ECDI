#include "RunAllTests.h"
#include "TestFramework.h"

#include "ECDI/Application/Application.h"
#include "ECDI/Window/Window.h"
#include "ECDI/Widget/Widget.h"
#include "ECDI/EventSystem/Input/Mouse/MouseMoveEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonUpEvent.h"

#include <memory>
#include <string>
#include <vector>

using namespace ECDI;

namespace {

// ── TestApp：继承 Application 暴露 protected 事件入口（main.cpp DemoApplication 同模式）──
class TestApp : public Application
{
public:
    void SimulateMouseMove(Window& window, int x, int y)
    {
        MouseMoveEvent event(&window, x, y);
        OnMouseMove(event);
    }

    void SimulateMouseDown(Window& window, int x, int y)
    {
        MouseButtonDownEvent event(&window, x, y, MouseButton::Left);
        OnMouseButtonDown(event);
    }

    void SimulateMouseUp(Window& window, int x, int y)
    {
        MouseButtonUpEvent event(&window, x, y, MouseButton::Left);
        OnMouseButtonUp(event);
    }
};

// ── TestWidget：记录 OnMouseEnter/Leave 调用序列 ──
class TestWidget : public Widget
{
public:
    std::vector<std::string> events;   ///< 事件序列（"Enter"/"Leave"）

    void OnMouseEnter() override { events.push_back("Enter"); }
    void OnMouseLeave() override { events.push_back("Leave"); }
};

// ── 辅助：创建独立 Window 的测试环境（窗口不 Show——纯逻辑验证）──
// 每测试独立 Window（隔离 m_hoverWidget 状态——测试互不污染）

struct HoverFixture
{
    TestApp app;
    Window& window;

    HoverFixture() : window(app.Create("HoverTest", 400, 300)) {}

    TestWidget* AddWidget(int x, int y, int w, int h)
    {
        auto widget = std::make_unique<TestWidget>();
        widget->SetPosition(x, y);
        widget->SetSize(w, h);
        auto* raw = widget.get();
        window.GetRootWidget().AddChild(std::move(widget));
        return raw;
    }
};

// ── R4-S1：进入（null→A） ──
void TestHoverEnter()
{
    HoverFixture f;
    auto* a = f.AddWidget(10, 10, 100, 50);

    f.app.SimulateMouseMove(f.window, 20, 20);

    EXPECT_EQ(a->events.size(), 1);
    EXPECT_EQ(a->events[0], "Enter");
}

// ── R4-S2：离开（A→null——移动到空白区域） ──
void TestHoverLeave()
{
    HoverFixture f;
    auto* a = f.AddWidget(10, 10, 100, 50);

    f.app.SimulateMouseMove(f.window, 20, 20);   // 进入 A
    EXPECT_EQ(a->events.size(), 1);

    f.app.SimulateMouseMove(f.window, 300, 200);   // 空白区域（无控件）
    EXPECT_EQ(a->events.size(), 2);
    EXPECT_EQ(a->events[1], "Leave");
}

// ── R4-S3：切换（A→B，Leave 先于 Enter——契约 D） ──
void TestHoverSwitch()
{
    HoverFixture f;
    auto* a = f.AddWidget(10, 10, 100, 50);
    auto* b = f.AddWidget(150, 10, 100, 50);

    f.app.SimulateMouseMove(f.window, 20, 20);   // 进入 A
    EXPECT_EQ(a->events.size(), 1);

    f.app.SimulateMouseMove(f.window, 170, 20);   // 切到 B
    EXPECT_EQ(a->events.size(), 2);
    EXPECT_EQ(a->events[1], "Leave");
    EXPECT_EQ(b->events.size(), 1);
    EXPECT_EQ(b->events[0], "Enter");
}

// ── R4-S4：同目标不重复通知 ──
void TestHoverSameTargetNoRepeat()
{
    HoverFixture f;
    auto* a = f.AddWidget(10, 10, 100, 50);

    f.app.SimulateMouseMove(f.window, 20, 20);   // 进入 A
    EXPECT_EQ(a->events.size(), 1);

    f.app.SimulateMouseMove(f.window, 30, 25);   // 仍在 A 内移动
    EXPECT_EQ(a->events.size(), 1);              // 不重复
}

// ── R4-S5：Z 序（重叠时 HitTest 返回最上层） ──
void TestHoverZOrder()
{
    HoverFixture f;
    auto* a = f.AddWidget(10, 10, 100, 50);
    auto* b = f.AddWidget(10, 10, 100, 50);   // B 后添加 = 上层

    f.app.SimulateMouseMove(f.window, 20, 20);   // 重叠区 → 命中 B（Z 序）

    EXPECT_EQ(a->events.size(), 0);   // A 不进入
    EXPECT_EQ(b->events.size(), 1);
    EXPECT_EQ(b->events[0], "Enter");
}

// ── R4-S6：捕获期间冻结（契约 A——Down 后 MouseMove 不更新 hover） ──
void TestHoverCaptureFreeze()
{
    HoverFixture f;
    auto* a = f.AddWidget(10, 10, 100, 50);
    auto* b = f.AddWidget(150, 10, 100, 50);

    f.app.SimulateMouseMove(f.window, 20, 20);   // 进入 A
    f.app.SimulateMouseDown(f.window, 20, 20);   // Down A → 捕获 A

    f.app.SimulateMouseMove(f.window, 170, 20);   // 移到 B（捕获期冻结）

    EXPECT_EQ(a->events.size(), 1);   // A 不 Leave
    EXPECT_EQ(b->events.size(), 0);   // B 不 Enter
}

// ── R4-S7：捕获释放后恢复（契约 B——释放后首次 MouseMove 重新 HitTest） ──
void TestHoverCaptureRelease()
{
    HoverFixture f;
    auto* a = f.AddWidget(10, 10, 100, 50);
    auto* b = f.AddWidget(150, 10, 100, 50);

    f.app.SimulateMouseMove(f.window, 20, 20);   // 进入 A
    f.app.SimulateMouseDown(f.window, 20, 20);   // 捕获 A
    f.app.SimulateMouseMove(f.window, 170, 20);  // 移到 B（冻结——无通知）
    f.app.SimulateMouseUp(f.window, 170, 20);    // 释放捕获

    // 释放本身不重算（契约 B）
    EXPECT_EQ(a->events.size(), 1);
    EXPECT_EQ(b->events.size(), 0);

    f.app.SimulateMouseMove(f.window, 170, 20);   // 释放后首次 MouseMove → 恢复 HitTest

    EXPECT_EQ(a->events.size(), 2);
    EXPECT_EQ(a->events[1], "Leave");
    EXPECT_EQ(b->events.size(), 1);
    EXPECT_EQ(b->events[0], "Enter");
}

// ── R4-S8：脱树不补发 Leave（契约 C——RemoveChild 后目标失效） ──
void TestHoverDetachNoLeave()
{
    HoverFixture f;
    auto* a = f.AddWidget(10, 10, 100, 50);
    auto* b = f.AddWidget(150, 10, 100, 50);

    f.app.SimulateMouseMove(f.window, 20, 20);   // 进入 A
    EXPECT_EQ(a->events.size(), 1);

    // A 脱树——⚠️ 必须持有 RemoveChild 返回值（unique_ptr 丢弃即析构——§4.4 契约：调用者持有期间对象存活）
    auto removedA = f.window.GetRootWidget().RemoveChild(a);

    f.app.SimulateMouseMove(f.window, 170, 20);  // 移到 B

    EXPECT_EQ(a->events.size(), 1);              // A 不补发 Leave（契约 C）
    EXPECT_EQ(b->events.size(), 1);
    EXPECT_EQ(b->events[0], "Enter");
}

// ── R4-S9：脱树后目标位置无通知 ──
void TestHoverDetachHitTestOriginal()
{
    HoverFixture f;
    auto* a = f.AddWidget(10, 10, 100, 50);

    f.app.SimulateMouseMove(f.window, 20, 20);   // 进入 A
    EXPECT_EQ(a->events.size(), 1);

    // A 脱树——持有返回值（同上 §4.4 契约）
    auto removedA = f.window.GetRootWidget().RemoveChild(a);

    f.app.SimulateMouseMove(f.window, 20, 20);   // 移到 A 原位置（HitTest 已无 A）

    EXPECT_EQ(a->events.size(), 1);              // 无 Leave、无 Enter（脱树失效）
}

} // anonymous namespace

void ECDI::Test::RegisterHoverTests()
{
    GetTestRegistry().Add("Hover.Enter", &TestHoverEnter);
    GetTestRegistry().Add("Hover.Leave", &TestHoverLeave);
    GetTestRegistry().Add("Hover.Switch", &TestHoverSwitch);
    GetTestRegistry().Add("Hover.SameTargetNoRepeat", &TestHoverSameTargetNoRepeat);
    GetTestRegistry().Add("Hover.ZOrder", &TestHoverZOrder);
    GetTestRegistry().Add("Hover.CaptureFreeze", &TestHoverCaptureFreeze);
    GetTestRegistry().Add("Hover.CaptureRelease", &TestHoverCaptureRelease);
    GetTestRegistry().Add("Hover.DetachNoLeave", &TestHoverDetachNoLeave);
    GetTestRegistry().Add("Hover.DetachHitTestOriginal", &TestHoverDetachHitTestOriginal);
}
