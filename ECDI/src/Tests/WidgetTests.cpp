#include "RunAllTests.h"
#include "TestFramework.h"
#include "ECDI/Render/PaintContext.h"
#include "ECDI/Render/RecordingBackend.h"
#include "ECDI/Render/TextMeasurer.h"
#include "ECDI/Widget/Panel.h"
#include "ECDI/Widget/Label.h"
#include "ECDI/Widget/Button.h"
#include "ECDI/Widget/Widget.h"
#include "ECDI/Theme/DefaultTheme.h"
#include "ECDI/Core/Point.h"
#include "ECDI/Core/Color.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonUpEvent.h"
#include "ECDI/Render/RenderCommand.h"
#include <memory>
#include <utility>

using namespace ECDI;

constexpr float kEpsilon = 0.001f;

namespace {

void TestPanelPaint()
{
    // ── 4.6 原 #2：Panel → PaintContext → DrawRectCommand ──
    // 9.5 R1：命令流 = PushClip(控件边界) → DrawRect(背景) → PopClip
    // 2026-08-30 变更（phase9.6-panel-container-semantics v1.1）：默认背景透明 → a==0 短路无 DrawRect；
    // DrawRect 命令断言移至 SetStyle 设色场景

    // 默认透明：命令流 = PushClip → PopClip（size 2，无 DrawRect）
    {
        RecordingBackend measurer;
        CommandBuffer commands;
        PaintContext ctx(commands, measurer);

        Panel panel;
        panel.SetPosition(10, 20);
        panel.SetSize(100, 50);
        panel.Paint(ctx, 0, 0);

        EXPECT_EQ(commands.size(), 2);
        const auto& clip = std::get<PushClipCommand>(commands[0]);
        EXPECT_NEAR(clip.rect.x, 10.0f, kEpsilon);
        EXPECT_NEAR(clip.rect.y, 20.0f, kEpsilon);
        EXPECT_NEAR(clip.rect.width, 100.0f, kEpsilon);
        EXPECT_NEAR(clip.rect.height, 50.0f, kEpsilon);
        EXPECT_TRUE(std::holds_alternative<PopClipCommand>(commands[1]));   // 严格配对
    }

    // SetStyle 设色后：恢复 PushClip → DrawRect → PopClip（命令管线不变）
    {
        RecordingBackend measurer;
        CommandBuffer commands;
        PaintContext ctx(commands, measurer);

        Panel panel;
        panel.SetPosition(10, 20);
        panel.SetSize(100, 50);
        panel.SetStyle(PanelStyleOverride{ .background = Color::FromRGBA8(10, 200, 30) });
        panel.Paint(ctx, 0, 0);

        EXPECT_EQ(commands.size(), 3);
        const auto& cmd = std::get<DrawRectCommand>(commands[1]);
        EXPECT_NEAR(cmd.rect.x, 10.0f, kEpsilon);
        EXPECT_NEAR(cmd.rect.y, 20.0f, kEpsilon);
        EXPECT_NEAR(cmd.rect.width, 100.0f, kEpsilon);
        EXPECT_NEAR(cmd.rect.height, 50.0f, kEpsilon);
        EXPECT_NEAR(cmd.color.r, 10.0f / 255.0f, kEpsilon);
        EXPECT_NEAR(cmd.color.g, 200.0f / 255.0f, kEpsilon);
        EXPECT_NEAR(cmd.color.b, 30.0f / 255.0f, kEpsilon);
        EXPECT_TRUE(std::holds_alternative<PopClipCommand>(commands[2]));   // 严格配对
    }
}

void TestLabelPaint()
{
    // ── 5.2 原 #4：Label → PaintContext → DrawTextCommand ──
    // 9.5 R1：命令流 = PushClip(控件边界) → DrawText → PopClip
    RecordingBackend backend;
    CommandBuffer commands;
    PaintContext ctx(commands, backend);

    Label label("Hello ECDI");
    label.SetPosition(5, 5);
    label.SetSize(100, 30);
    label.Paint(ctx, 0, 0);

    EXPECT_EQ(commands.size(), 3);
    EXPECT_TRUE(std::holds_alternative<PushClipCommand>(commands[0]));   // 控件边界
    const auto& cmd = std::get<DrawTextCommand>(commands[1]);
    EXPECT_EQ(cmd.text, "Hello ECDI");
    EXPECT_NEAR(cmd.color.r, 0.0f, kEpsilon);
    EXPECT_NEAR(cmd.pos.x, 5.0f, kEpsilon);

    const float expectedY = 5.0f + (30.0f - backend.LineHeight(Font{})) / 2.0f;
    EXPECT_NEAR(cmd.pos.y, expectedY, kEpsilon);
    EXPECT_NEAR(cmd.font.size, 14.0f, kEpsilon);
    EXPECT_TRUE(std::holds_alternative<PopClipCommand>(commands[2]));   // 严格配对
}

void TestButtonPaint()
{
    // ── 5.3 原 #5：Button 先背景后文本 ──
    // 9.5 R1：命令流 = PushClip(控件边界) → DrawRect(背景) → DrawText → PopClip
    // （cornerRadius 默认 0——DefaultTheme.cpp:19，背景仍走 DrawRect 非 DrawRoundedRect）
    RecordingBackend backend;
    CommandBuffer commands;
    PaintContext ctx(commands, backend);

    Button button("OK");
    button.SetPosition(10, 10);
    button.SetSize(100, 40);
    button.Paint(ctx, 0, 0);

    EXPECT_EQ(commands.size(), 4);
    EXPECT_TRUE(std::holds_alternative<PushClipCommand>(commands[0]));   // 控件边界
    const auto& bg = std::get<DrawRectCommand>(commands[1]);
    EXPECT_NEAR(bg.rect.x, 10.0f, kEpsilon);
    EXPECT_NEAR(bg.rect.width, 100.0f, kEpsilon);
    const auto& txt = std::get<DrawTextCommand>(commands[2]);
    EXPECT_EQ(txt.text, "OK");
    EXPECT_EQ(txt.color, Color::White());

    const float expectedX = 10.0f + (100.0f - backend.MeasureText(Font{}, "OK").width) / 2.0f;
    EXPECT_NEAR(txt.pos.x, expectedX, kEpsilon);
    const float expectedY = 10.0f + (40.0f - backend.LineHeight(Font{})) / 2.0f;
    EXPECT_NEAR(txt.pos.y, expectedY, kEpsilon);
    EXPECT_TRUE(std::holds_alternative<PopClipCommand>(commands[3]));   // 严格配对
}

void TestWidgetTree()
{
    // ── T5：Widget 树操作 ──

    // 正常添加
    {
        Widget parent;
        parent.AddChild(std::make_unique<Widget>());
        EXPECT_EQ(parent.GetChildCount(), 1);
        EXPECT_EQ(parent.GetChildAt(0)->GetParent(), &parent);
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
        EXPECT_EQ(parent.GetChildAt(0), c0);
        EXPECT_EQ(parent.GetChildAt(1), c1);
        EXPECT_EQ(parent.GetChildAt(2), c2);
    }

    // RemoveChild
    {
        Widget parent;
        auto child = std::make_unique<Widget>();
        auto* raw = child.get();
        parent.AddChild(std::move(child));
        auto removed = parent.RemoveChild(raw);
        EXPECT_EQ(parent.GetChildCount(), 0);
        EXPECT_EQ(removed.get(), raw);
        EXPECT_TRUE(removed->GetParent() == nullptr);
    }

    // GetChildAt 越界断言（FRAMEWORK_ASSERT 模式：断言终止）
    // 注意：此负面测试无法自动验证断言路径——仅验证正常路径不触发断言
    {
        Widget parent;
        parent.AddChild(std::make_unique<Widget>());
        auto* child = parent.GetChildAt(0);
        EXPECT_TRUE(child != nullptr);
    }

    // 防环：AddChild 拒绝已挂载的 child（前置条件检查——间接验证）
    {
        Widget parent1;
        Widget parent2;
        auto child = std::make_unique<Widget>();
        auto* raw = child.get();
        parent1.AddChild(std::move(child));
        EXPECT_EQ(raw->GetParent(), &parent1);
    }
}

// ── 9.6 收尾方案 A：聚焦框开关（行为开关，默认 true——既有行为不变）──
void TestShowFocusRectToggle()
{
    // 默认显示（回归保护：新增开关不得改变既有默认行为）
    Button button("OK");
    EXPECT_TRUE(button.ShowFocusRect());

    button.SetShowFocusRect(false);
    EXPECT_FALSE(button.ShowFocusRect());

    // 可重新打开
    button.SetShowFocusRect(true);
    EXPECT_TRUE(button.ShowFocusRect());

    // 基类 Widget 同样具备（面板等非聚焦控件也能关——统一行为开关）
    Widget panel;
    EXPECT_TRUE(panel.ShowFocusRect());
    panel.SetShowFocusRect(false);
    EXPECT_FALSE(panel.ShowFocusRect());
}

void TestPanelSetStyle()
{
    // 2026-08-29：单实例覆盖——SetStyle 后背景色生效，且 ApplyTheme 不再回退（D7）
    const Color custom = Color::FromRGBA8(30, 40, 50);

    // 覆盖前：默认透明（2026-08-30 变更）——alpha 短路无 DrawRect，命令流 size 2
    {
        RecordingBackend measurer;
        CommandBuffer commands;
        PaintContext ctx(commands, measurer);
        Panel panel;
        panel.SetPosition(0, 0);
        panel.SetSize(80, 40);
        panel.Paint(ctx, 0, 0);
        EXPECT_EQ(commands.size(), 2);
        EXPECT_TRUE(std::holds_alternative<PopClipCommand>(commands[1]));
    }

    // 覆盖为自定义色：绘制立即反映
    {
        RecordingBackend measurer;
        CommandBuffer commands;
        PaintContext ctx(commands, measurer);
        Panel panel;
        panel.SetPosition(0, 0);
        panel.SetSize(80, 40);
        panel.SetStyle(PanelStyleOverride{ .background = custom });
        panel.Paint(ctx, 0, 0);
        const auto& cmd = std::get<DrawRectCommand>(commands[1]);
        EXPECT_NEAR(cmd.color.r, 30.0f / 255.0f, kEpsilon);
        EXPECT_NEAR(cmd.color.g, 40.0f / 255.0f, kEpsilon);
        EXPECT_NEAR(cmd.color.b, 50.0f / 255.0f, kEpsilon);
    }

    // 覆盖后再次 ApplyTheme 不应回退（overridden 字段 Apply 被忽略）
    {
        RecordingBackend measurer;
        CommandBuffer commands;
        PaintContext ctx(commands, measurer);
        Panel panel;
        panel.SetPosition(0, 0);
        panel.SetSize(80, 40);
        panel.SetStyle(PanelStyleOverride{ .background = custom });
        panel.ApplyTheme(GetDefaultTheme());
        panel.Paint(ctx, 0, 0);
        const auto& cmd2 = std::get<DrawRectCommand>(commands[1]);
        EXPECT_NEAR(cmd2.color.r, 30.0f / 255.0f, kEpsilon);
        EXPECT_NEAR(cmd2.color.g, 40.0f / 255.0f, kEpsilon);
        EXPECT_NEAR(cmd2.color.b, 50.0f / 255.0f, kEpsilon);
    }
}

void TestPanelInputPassThrough()
{
    // 2026-08-30（phase9.6-panel-container-semantics v1.1）：镶板 = 纯容器，自身永不命中；
    // 鼠标事件只由子控件接收（HitTest 子优先递归，子未命中也不回落到 Panel 自身）
    Panel panel;
    panel.SetPosition(0, 0);
    panel.SetSize(100, 100);

    auto child = std::make_unique<Widget>();
    child->SetPosition(10, 10);
    child->SetSize(50, 30);
    Widget* childPtr = child.get();
    panel.AddChild(std::move(child));

    // Panel 自身区域（无子覆盖）：不命中
    EXPECT_EQ(panel.HitTest(80, 80), nullptr);

    // 子控件区域：透传命中子
    EXPECT_EQ(panel.HitTest(20, 20), childPtr);
}

// ── P1：Button hover / Panel 形态（modelprobe-p1-detailed-design §4/§5/§8）──

/// @brief 可测 Button：暴露 protected hover 事件 + 背景呈现值（无窗口树——AnimateBackgroundTo 即时到位）
class TestableButton : public Button
{
public:
	using Button::Button;
	using Button::OnMouseEnter;
	using Button::OnMouseLeave;
	using Button::OnMouseButtonDown;
	using Button::OnMouseButtonUp;
	Color Displayed() const noexcept { return m_displayedBackground; }   // protected 可访问
};

void TestButtonHover()
{
	// hover 目标色过渡（无窗口树 → 即时到位）
	TestableButton btn("Hover");
	const Color normal = Color::FromRGBA8(47, 127, 217, 255);
	const Color hover  = Color::FromRGBA8(79, 156, 247, 255);
	btn.SetStyle(ButtonStyleOverride{
		.background = normal,
		.cornerRadius = 6.0f,
		.hoverBackground = hover,
	});
	EXPECT_EQ(btn.Displayed(), normal);
	btn.OnMouseEnter();
	EXPECT_EQ(btn.Displayed(), hover);      // 进入 → hover 色
	btn.OnMouseLeave();
	EXPECT_EQ(btn.Displayed(), normal);     // 离开 → 还原
}

void TestButtonHoverPressedPriority()
{
	// 优先级：按下 > hover（QSS `:active` 覆盖 `:hover`）
	TestableButton btn("P");
	const Color normal  = Color::FromRGBA8(47, 127, 217, 255);
	const Color hover   = Color::FromRGBA8(79, 156, 247, 255);
	const Color pressed = Color::FromRGBA8(30, 90, 160, 255);
	btn.SetStyle(ButtonStyleOverride{
		.background = normal,
		.cornerRadius = 6.0f,
		.pressedBackground = pressed,
		.hoverBackground = hover,
	});
	btn.OnMouseEnter();
	EXPECT_EQ(btn.Displayed(), hover);
	btn.OnMouseButtonDown(MouseButtonDownEvent(nullptr, 10, 10, MouseButton::Left));
	EXPECT_EQ(btn.Displayed(), pressed);    // 按下覆盖 hover
	btn.OnMouseButtonUp(MouseButtonUpEvent(nullptr, 10, 10, MouseButton::Left));
	EXPECT_EQ(btn.Displayed(), hover);      // 松开 → 回 hover（仍在 hover）
	btn.OnMouseLeave();
	EXPECT_EQ(btn.Displayed(), normal);
}

void TestPanelShapeRounded()
{
	// cornerRadius>0 → 背景命令 DrawRoundedRect（命令流 = PushClip → 圆角背景 → PopClip）
	Panel panel;
	panel.SetPosition(0, 0);
	panel.SetSize(100, 50);
	panel.SetStyle(PanelStyleOverride{
		.background = Color::FromRGBA8(10, 20, 30, 255),
		.cornerRadius = 8.0f,
	});
	RecordingBackend measurer;
	CommandBuffer commands;
	PaintContext ctx(commands, measurer);
	panel.Paint(ctx, 0, 0);
	EXPECT_EQ(commands.size(), 3);
	const auto& rr = std::get<DrawRoundedRectCommand>(commands[1]);
	EXPECT_NEAR(rr.cornerRadius, 8.0f, kEpsilon);
}

void TestPanelShapeBorderRing()
{
	// borderWidth>0 → 双矩形描边环（外层 borderColor + 内层背景内缩）
	Panel panel;
	panel.SetPosition(0, 0);
	panel.SetSize(100, 50);
	panel.SetStyle(PanelStyleOverride{
		.background = Color::FromRGBA8(22, 26, 33, 255),
		.cornerRadius = 8.0f,
		.borderWidth = 1.0f,
		.borderColor = Color::FromRGBA8(42, 49, 64, 255),
	});
	RecordingBackend measurer;
	CommandBuffer commands;
	PaintContext ctx(commands, measurer);
	panel.Paint(ctx, 0, 0);
	EXPECT_EQ(commands.size(), 4);   // PushClip → 外层 → 内层 → PopClip
	const auto& outer = std::get<DrawRoundedRectCommand>(commands[1]);
	EXPECT_EQ(outer.color, Color::FromRGBA8(42, 49, 64, 255));
	EXPECT_NEAR(outer.cornerRadius, 8.0f, kEpsilon);
	const auto& inner = std::get<DrawRoundedRectCommand>(commands[2]);
	EXPECT_EQ(inner.color, Color::FromRGBA8(22, 26, 33, 255));
	EXPECT_NEAR(inner.cornerRadius, 7.0f, kEpsilon);
}

// ── 9.8 AutoSize（phase9.8-autosize-*：GetPreferredSize/AutoSize/意图语义）──

/// @brief 假测量器：每码点 8.0f 宽、行高 16.0f（确定性——测量断言前提）
/// @note 码点计数按 UTF-8 非 continuation byte——测试测量模型，不承担 UTF-8 合法性验证（详设 v1.1）
class FakeTextMeasurer : public TextMeasurer
{
public:
	Size MeasureText(const Font&, const std::string& text) override{
		return Size{ static_cast<float>(CountCodepoints(text)) * 8.0f, 16.0f };
	}
	float LineHeight(const Font&) override{ return 16.0f; }
private:
	static size_t CountCodepoints(const std::string& s){
		size_t count = 0;
		for (size_t i = 0; i < s.size(); ++i)
			if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80)   // 非续字节 = 码点起点
				++count;
		return count;
	}
};

/// @brief 可测 Label：注入 FakeTextMeasurer（ResolveMeasurer 测试接缝——9.8）
class TestableLabel : public Label
{
public:
	using Label::Label;
protected:
	TextMeasurer* ResolveMeasurer() const override{ return &ms_fake; }
private:
	static FakeTextMeasurer ms_fake;
};

FakeTextMeasurer TestableLabel::ms_fake;

void TestPreferredSizeDefault()
{
	// 非文本控件默认 = 当前尺寸（零回归锚点）
	Panel panel;
	panel.SetSize(100, 30);
	const Size preferred = panel.GetPreferredSize();
	EXPECT_NEAR(preferred.width, 100.0f, kEpsilon);
	EXPECT_NEAR(preferred.height, 30.0f, kEpsilon);
}

void TestLabelPreferredMeasured()
{
	// 内容测量：5 码点 × 8 = 40 宽、行高 16——0 inset（显式 SetSize 不影响查询）
	TestableLabel label("Hello");
	label.SetSize(999, 999);
	const Size preferred = label.GetPreferredSize();
	EXPECT_NEAR(preferred.width, 40.0f, kEpsilon);
	EXPECT_NEAR(preferred.height, 16.0f, kEpsilon);
}

void TestLabelAutoSizeResizes()
{
	// AutoSize → 尺寸 = preferred + 返回 true
	TestableLabel label("Hello");
	label.SetSize(999, 999);
	EXPECT_TRUE(label.AutoSize());
	EXPECT_EQ(label.GetWidth(), 40);
	EXPECT_EQ(label.GetHeight(), 16);
}

void TestAutoSizeStretchMutex()
{
	// §3.5 条 1：stretch>0 → no-op false；SetStretch(0) 后重新生效（调用时判断非永久关闭）
	TestableLabel label("Hello");
	label.SetSize(999, 999);
	label.SetStretch(1);
	EXPECT_FALSE(label.AutoSize());
	EXPECT_EQ(label.GetWidth(), 999);   // 尺寸不变（分配语义优先）
	label.SetStretch(0);
	EXPECT_TRUE(label.AutoSize());
	EXPECT_EQ(label.GetWidth(), 40);
}

void TestAutoSizeSameSizeNoOp()
{
	// preferred == 当前尺寸 → false
	TestableLabel label("Hello");
	label.SetSize(999, 999);
	EXPECT_TRUE(label.AutoSize());
	EXPECT_FALSE(label.AutoSize());   // 已到位 → no-op
}

void TestAutoSizePureGeometry()
{
	// §3.7：AutoSize 只改尺寸——位置不漂移（SetSize 虚分派不改 geometry.x/y）
	TestableLabel label("Hello");
	label.SetPosition(17, 23);
	label.SetSize(999, 999);
	EXPECT_TRUE(label.AutoSize());
	EXPECT_EQ(label.GetX(), 17);
	EXPECT_EQ(label.GetY(), 23);
	EXPECT_EQ(label.GetWidth(), 40);
	EXPECT_EQ(label.GetHeight(), 16);
}

void TestAutoSizeLastCallWins()
{
	// R5 v1.5：后调用者赢——AutoSize 覆盖显式 SetSize；反之亦然
	TestableLabel label("Hello");
	label.SetSize(500, 100);
	EXPECT_TRUE(label.AutoSize());      // AutoSize 后调 → 覆盖
	EXPECT_EQ(label.GetWidth(), 40);
	EXPECT_EQ(label.GetHeight(), 16);
	label.SetSize(50, 50);              // SetSize 后调 → 再覆盖回显式
	EXPECT_EQ(label.GetWidth(), 50);
	EXPECT_EQ(label.GetHeight(), 50);
}

} // anonymous namespace

void ECDI::Test::RegisterWidgetTests()
{
    GetTestRegistry().Add("Widget.PanelPaint", &TestPanelPaint);
    GetTestRegistry().Add("Widget.LabelPaint", &TestLabelPaint);
    GetTestRegistry().Add("Widget.ButtonPaint", &TestButtonPaint);
    GetTestRegistry().Add("Widget.ShowFocusRectToggle", &TestShowFocusRectToggle);
    GetTestRegistry().Add("Widget.WidgetTree", &TestWidgetTree);
    GetTestRegistry().Add("Widget.PanelSetStyle", &TestPanelSetStyle);
    GetTestRegistry().Add("Widget.PanelInputPassThrough", &TestPanelInputPassThrough);
    GetTestRegistry().Add("Widget.ButtonHover", &TestButtonHover);                    // P1
    GetTestRegistry().Add("Widget.ButtonHoverPressedPriority", &TestButtonHoverPressedPriority);  // P1
    GetTestRegistry().Add("Widget.PanelShapeRounded", &TestPanelShapeRounded);        // P1
    GetTestRegistry().Add("Widget.PanelShapeBorderRing", &TestPanelShapeBorderRing);  // P1
    GetTestRegistry().Add("PreferredSize.Default", &TestPreferredSizeDefault);         // 9.8
    GetTestRegistry().Add("Label.PreferredMeasured", &TestLabelPreferredMeasured);     // 9.8
    GetTestRegistry().Add("Label.AutoSizeResizes", &TestLabelAutoSizeResizes);         // 9.8
    GetTestRegistry().Add("AutoSize.StretchMutex", &TestAutoSizeStretchMutex);         // 9.8
    GetTestRegistry().Add("AutoSize.SameSizeNoOp", &TestAutoSizeSameSizeNoOp);         // 9.8
    GetTestRegistry().Add("AutoSize.PureGeometry", &TestAutoSizePureGeometry);         // 9.8
    GetTestRegistry().Add("AutoSize.LastCallWins", &TestAutoSizeLastCallWins);         // 9.8
}
