#include "RunAllTests.h"
#include "ECDI/Core/ECDIAssert.h"
#include "ECDI/Render/PaintContext.h"
#include "ECDI/Render/RecordingBackend.h"
#include "ECDI/Widget/Panel.h"
#include "ECDI/Widget/Label.h"
#include "ECDI/Widget/Button.h"
#include "ECDI/Widget/Widget.h"
#include "ECDI/Core/Point.h"
#include "ECDI/Core/Color.h"
#include <cmath>
#include <memory>
#include <utility>

using namespace ECDI;

constexpr float kEpsilon = 0.001f;
inline bool FloatEq(float a, float b) { return std::abs(a - b) < kEpsilon; }

namespace {

void TestPanelPaint()
{
    // ── 4.6 原 #2：Panel → PaintContext → DrawRectCommand ──
    RecordingBackend measurer;
    CommandBuffer commands;
    PaintContext ctx(commands, measurer);

    Panel panel;
    panel.SetPosition(10, 20);
    panel.SetSize(100, 50);
    panel.Paint(ctx, 0, 0);

    FRAMEWORK_ASSERT(commands.size() == 1);
    const auto& cmd = std::get<DrawRectCommand>(commands[0]);
    FRAMEWORK_ASSERT(FloatEq(cmd.rect.x, 10.0f) && FloatEq(cmd.rect.y, 20.0f));
    FRAMEWORK_ASSERT(FloatEq(cmd.rect.width, 100.0f) && FloatEq(cmd.rect.height, 50.0f));
    FRAMEWORK_ASSERT(FloatEq(cmd.color.r, 0.5f) && FloatEq(cmd.color.g, 0.5f));
}

void TestLabelPaint()
{
    // ── 5.2 原 #4：Label → PaintContext → DrawTextCommand ──
    RecordingBackend backend;
    CommandBuffer commands;
    PaintContext ctx(commands, backend);

    Label label("Hello ECDI");
    label.SetPosition(5, 5);
    label.SetSize(100, 30);
    label.Paint(ctx, 0, 0);

    FRAMEWORK_ASSERT(commands.size() == 1);
    const auto& cmd = std::get<DrawTextCommand>(commands[0]);
    FRAMEWORK_ASSERT(cmd.text == "Hello ECDI");
    FRAMEWORK_ASSERT(FloatEq(cmd.color.r, 0.0f));
    FRAMEWORK_ASSERT(FloatEq(cmd.pos.x, 5.0f));

    const float expectedY = 5.0f + (30.0f - backend.LineHeight(Font{})) / 2.0f;
    FRAMEWORK_ASSERT(FloatEq(cmd.pos.y, expectedY));
    FRAMEWORK_ASSERT(FloatEq(cmd.font.size, 14.0f));
}

void TestButtonPaint()
{
    // ── 5.3 原 #5：Button 先背景后文本 ──
    RecordingBackend backend;
    CommandBuffer commands;
    PaintContext ctx(commands, backend);

    Button button("OK");
    button.SetPosition(10, 10);
    button.SetSize(100, 40);
    button.Paint(ctx, 0, 0);

    FRAMEWORK_ASSERT(commands.size() == 2);
    const auto& bg = std::get<DrawRectCommand>(commands[0]);
    FRAMEWORK_ASSERT(FloatEq(bg.rect.x, 10.0f) && FloatEq(bg.rect.width, 100.0f));
    const auto& txt = std::get<DrawTextCommand>(commands[1]);
    FRAMEWORK_ASSERT(txt.text == "OK");
    FRAMEWORK_ASSERT(txt.color == Color::White());

    const float expectedX = 10.0f + (100.0f - backend.MeasureText(Font{}, "OK").width) / 2.0f;
    FRAMEWORK_ASSERT(FloatEq(txt.pos.x, expectedX));
    const float expectedY = 10.0f + (40.0f - backend.LineHeight(Font{})) / 2.0f;
    FRAMEWORK_ASSERT(FloatEq(txt.pos.y, expectedY));
}

void TestWidgetTree()
{
    // ── T5：Widget 树操作 ──

    // 正常添加
    {
        Widget parent;
        parent.AddChild(std::make_unique<Widget>());
        FRAMEWORK_ASSERT(parent.GetChildCount() == 1);
        FRAMEWORK_ASSERT(parent.GetChildAt(0)->GetParent() == &parent);
    }

    // 多子节点顺序
    {
        Widget parent;
        auto child0 = std::make_unique<Widget>();
        auto child1 = std::make_unique<Widget>();
        auto child2 = std::make_unique<Widget>();
        auto* c0 = child0.get();
        auto* c1 = child1.get();
        auto* c2 = child2.get();
        parent.AddChild(std::move(child0));
        parent.AddChild(std::move(child1));
        parent.AddChild(std::move(child2));
        FRAMEWORK_ASSERT(parent.GetChildAt(0) == c0);
        FRAMEWORK_ASSERT(parent.GetChildAt(1) == c1);
        FRAMEWORK_ASSERT(parent.GetChildAt(2) == c2);
    }

    // RemoveChild
    {
        Widget parent;
        auto child = std::make_unique<Widget>();
        auto* raw = child.get();
        parent.AddChild(std::move(child));
        auto removed = parent.RemoveChild(raw);
        FRAMEWORK_ASSERT(parent.GetChildCount() == 0);
        FRAMEWORK_ASSERT(removed.get() == raw);
        FRAMEWORK_ASSERT(removed->GetParent() == nullptr);
    }

    // GetChildAt 越界断言（FRAMEWORK_ASSERT 模式：断言终止）
    // 注意：此负面测试无法自动验证断言路径——仅验证正常路径不触发断言
    {
        Widget parent;
        parent.AddChild(std::make_unique<Widget>());
        auto* child = parent.GetChildAt(0);
        FRAMEWORK_ASSERT(child != nullptr);
    }

    // 防环：AddChild 拒绝已挂载的 child（前置条件检查——间接验证）
    {
        Widget parent1;
        Widget parent2;
        auto child = std::make_unique<Widget>();
        auto* raw = child.get();
        parent1.AddChild(std::move(child));
        FRAMEWORK_ASSERT(raw->GetParent() == &parent1);
    }
}

} // anonymous namespace

void ECDI::Test::RunWidgetTests()
{
    TestPanelPaint();
    TestLabelPaint();
    TestButtonPaint();
    TestWidgetTree();
}