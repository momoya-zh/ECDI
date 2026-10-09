#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 防御性 undef（规范 10）：RenderingBackend.h 拉入 Windows.h（Phase 28 批二 T28-3）
#endif

#include "ECDI/Application/Application.h"
#include "ECDI/Render/PaintContext.h"
#include "Render/RecordingBackend.h"
#include "Render/GDITextMeasurer.h"   // Phase 28 批二：真测量器（T28-3 计数包装）
#include "Render/GDIBackend.h"
#include "ECDI/Render/TextMeasurer.h"
#include "ECDI/Widget/Panel.h"
#include "ECDI/Widget/Label.h"
#include "ECDI/Widget/Button.h"
#include "ECDI/Widget/Widget.h"
#include "ECDI/Window/Window.h"
#include "ECDI/Theme/DefaultTheme.h"
#include "ECDI/Core/TextAlignment.h"   // ★ Phase 30 T30-2/3：对齐枚举
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

// ── Phase 28 批二：TextWidget preferred 指纹短路（T28-3a/b——真窗口装置）──────
// ★ 装置 = WindowBackgroundTests.cpp 的 probe 形态（Application + RenderServices 注入）；
//   观察面 = 注入的计数测量器（调用计数差值——非耗时，防噪声污染）。

/// @brief 计数测量器：**拥有**内层真测量器 + 记录调用次数（观察 preferred 是否真的短路）
class CountingMeasurer final : public TextMeasurer
{
public:
	explicit CountingMeasurer(std::unique_ptr<TextMeasurer> inner) : m_inner(std::move(inner)) {}
	Size MeasureText(const Font& font, const std::string& text) override
	{
		++m_measureCalls;
		return m_inner->MeasureText(font, text);
	}
	float LineHeight(const Font& font) override { return m_inner->LineHeight(font); }
	std::size_t MeasureCalls() const { return m_measureCalls; }
private:
	std::unique_ptr<TextMeasurer> m_inner;
	std::size_t m_measureCalls = 0;
};

/// @brief 指纹探针：真窗口 + 计数测量器 + 根下 Label（走真 ResolveMeasurer 链）
struct FingerprintProbe
{
	Application app;
	CountingMeasurer* counter = nullptr;   ///< 非拥有（所有权在 services → Window）
	Window* window = nullptr;              ///< 非拥有（所有权归 Application）
	Label* label = nullptr;                ///< 非拥有（所有权归根子树）

	explicit FingerprintProbe(const char* title)
	{
		auto counterOwned = std::make_unique<CountingMeasurer>(std::make_unique<GDITextMeasurer>());
		counter = counterOwned.get();
		RenderServices services{
			std::make_unique<GDIBackend>(),      // 真后端（本组不驱动帧）
			std::move(counterOwned)
		};
		window = &app.Create(title, 400, 300, std::move(services));

		auto owned = std::make_unique<Label>(std::string("phase28 fingerprint probe text"));
		label = owned.get();
		window->GetRootWidget().AddChild(std::move(owned));
	}

	~FingerprintProbe() { window->Release(); }
};

void Test28PrefFingerprintHit()
{
	FingerprintProbe probe("ECDI_T28_3a");

	// ① 首测：建立指纹（真实测量 ≥ 1 次）
	const Size first = probe.label->GetPreferredSize();
	const std::size_t afterFirst = probe.counter->MeasureCalls();
	EXPECT_TRUE(afterFirst >= 1);

	// ② 再测：文本/字体/DPI 均未变 ⇒ 指纹命中 ⇒ **零 MeasureText 调用**（C28-5 机制判据）
	const Size second = probe.label->GetPreferredSize();
	EXPECT_EQ(probe.counter->MeasureCalls(), afterFirst);
	EXPECT_EQ(first.width, second.width);
	EXPECT_EQ(first.height, second.height);
}

void Test28PrefFingerprintInvalidation()
{
	FingerprintProbe probe("ECDI_T28_3b");

	(void)probe.label->GetPreferredSize();
	const std::size_t base = probe.counter->MeasureCalls();

	// ① SetText 换文本 ⇒ revision 失配 ⇒ 重测（+1）
	probe.label->SetText(std::string("a different text"));
	(void)probe.label->GetPreferredSize();
	EXPECT_EQ(probe.counter->MeasureCalls(), base + 1);

	// ② 换字号（Font 值变化）⇒ 失配 ⇒ 重测（+1）——无需 bump revision（键含字体值）
	probe.label->SetFont(Font{ 22.0f, "" });
	(void)probe.label->GetPreferredSize();
	EXPECT_EQ(probe.counter->MeasureCalls(), base + 2);

	// ③ 再测无变化 ⇒ 命中（计数不增——证明失效后指纹重新建立）
	(void)probe.label->GetPreferredSize();
	EXPECT_EQ(probe.counter->MeasureCalls(), base + 2);
}

// ══════════════════════════════════════════════════════════════════════
// Phase 29 批二：断行（word-wrap）路径 —— T29-1/2/3 + T29-WRAP-1/3 + A2 结构判据
// ══════════════════════════════════════════════════════════════════════
// ★ 装置要点（批一装置沿用、批二扩展）：
//   ① **每码点恒宽 8.0f / 行高 16.0f** 的确定性测量器——断行位置**精确可算**（宽度语义才可断言）；
//      按**字节**计宽会把 CJK 3 字节搅成 24 宽，禁则/软断点的断言全部失真。
//   ② **FitText 计数缝**——覆写 FitText 计数后**显式转发批一默认体**（`TextMeasurer::FitText`），
//      计的是「入口次数」、算的仍是真实默认二分体（不是桩）⇒ 断行逻辑真跑、命中与否可判。
//   ③ **真窗口**（`IsWrapLayoutActive` 要求 `GetWindow() != nullptr`——D29-Ⅰ 退化单行口径），
//      但**不驱动帧**：直接把 label Paint 进调用方自己的 CommandBuffer ⇒ 命令流可逐条断言。
//      窗口只提供 DPI 与父链（`GetWindow()` 沿 m_parent 上行到根的 m_window）。

/// @brief 每码点恒宽 8、行高 16 的确定性测量器（★ Phase 29 批二：断行断言的前提）
/// @details 派生自 `RecordingBackend`（双接口）⇒ 既是 `TextMeasurer`（PaintContext 要）又自带
///          `textDraws` 记录；本组命令流断言走调用方自持的 CommandBuffer，不读它。
class WrapMeasurer final : public RecordingBackend{
public:
	Size MeasureText(const Font&, const std::string& text) override{
		return Size{ 8.0f * static_cast<float>(CountCodepoints(text)), 16.0f };
	}
	float LineHeight(const Font&) override{ return 16.0f; }

	/// ★ 观测缝：只计入口次数，计算**转发批一默认体**（真实二分跑，不是桩——否则断行逻辑没被测）
	TextFit FitText(const Font& font, const std::string& text,
	                std::size_t startCp, float maxWidth) override{
		++m_fitCalls;
		return TextMeasurer::FitText(font, text, startCp, maxWidth);
	}
	[[nodiscard]] std::size_t FitCalls() const { return m_fitCalls; }

private:
	static std::size_t CountCodepoints(const std::string& s){
		std::size_t n = 0;
		for (std::size_t i = 0; i < s.size(); ++i){
			if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) { ++n; }
		}
		return n;
	}
	std::size_t m_fitCalls = 0;
};

/// @brief wrap 探针：真窗口 + label + 计数测量器；命令流落入调用方 buffer
struct WrapProbe{
	Application app;
	WrapMeasurer* measurer = nullptr;   ///< 非拥有（所有权经 RenderServices 归 Window）
	Window* window = nullptr;           ///< 非拥有（所有权归 Application）
	Label* label = nullptr;             ///< 非拥有（所有权归根子树）

	explicit WrapProbe(const char* title, int windowWidth, int windowHeight){
		auto owned = std::make_unique<WrapMeasurer>();
		measurer = owned.get();
		RenderServices services{
			std::make_unique<GDIBackend>(),   // 真后端（本组不驱动帧——直接调 Paint）
			std::move(owned)
		};
		window = &app.Create(title, windowWidth, windowHeight, std::move(services));

		auto labelOwned = std::make_unique<Label>(std::string(""));
		label = labelOwned.get();
		window->GetRootWidget().AddChild(std::move(labelOwned));
	}
	~WrapProbe(){ window->Release(); }

	/// @brief 把 label 的整次 Paint 画进调用方 buffer（窗口在 ⇒ wrap 路径可达）
	void PaintInto(CommandBuffer& commands){
		PaintContext ctx(commands, window->GetTextMeasurer());
		label->Paint(ctx, 0, 0);
	}

	/// @brief buffer 里的 DrawText 命令文本序列（顺序 = 绘制顺序）
	[[nodiscard]] static std::vector<std::string> DrawnTexts(const CommandBuffer& commands){
		std::vector<std::string> out;
		for (const auto& cmd : commands){
			if (const auto* text = std::get_if<DrawTextCommand>(&cmd)){
				out.push_back(text->text);
			}
		}
		return out;
	}

	/// @brief buffer 里的 DrawText 命令 y 坐标序列
	[[nodiscard]] static std::vector<float> DrawnYs(const CommandBuffer& commands){
		std::vector<float> out;
		for (const auto& cmd : commands){
			if (const auto* text = std::get_if<DrawTextCommand>(&cmd)){
				out.push_back(text->pos.y);
			}
		}
		return out;
	}

	/// @brief label 尺寸（宽 = wrap 宽度来源；高须容得下整块——否则 offsetY 为负）
	void SetLabelSize(int width, int height){
		label->SetSize(width, height);
	}
};

// ── T29-1：拉丁按词切分——每行一条 DrawText + 内容正确 + preferred = {宽, 行数×行高}──
void Test29WrapLatinBasic(){
	// 6 词 × 4 字母 + 5 分隔空格 = 29 码点；宽 40 ⇒ 一行至多 5 码点（5×8=40 恰好）…
	//   …但软断点取「空格后」⇒ 每行 4 字母（宽 32）、分隔空格被消费（不属于任何一行）。
	WrapProbe probe("ECDI_T29_1", 200, 200);
	probe.SetLabelSize(40, 200);
	probe.label->SetText(std::string("aaaa bbbb cccc dddd eeee ffff"));
	probe.label->SetWordWrap(true);

	// ① preferred = {控件宽, 行数 × 行高}（D29-B：宽由控件给、高由行数定）
	const Size preferred = probe.label->GetPreferredSize();
	EXPECT_EQ(static_cast<int>(preferred.width), 40);
	EXPECT_EQ(static_cast<int>(preferred.height), 6 * 16);   // 6 行

	// ② 命令流：恰 6 条 DrawText、内容 = 各行、**空行零命令**（本用例无空行）
	CommandBuffer commands;
	probe.PaintInto(commands);
	const std::vector<std::string> texts = WrapProbe::DrawnTexts(commands);
	EXPECT_EQ(texts.size(), static_cast<std::size_t>(6));
	EXPECT_EQ(texts[0], std::string("aaaa"));
	EXPECT_EQ(texts[3], std::string("dddd"));
	EXPECT_EQ(texts[5], std::string("ffff"));

	// ③ 逐行 y 步进 = 行高（16）；首行 y = 区块垂直居中的起点
	const std::vector<float> ys = WrapProbe::DrawnYs(commands);
	EXPECT_EQ(ys.size(), static_cast<std::size_t>(6));
	for (std::size_t i = 1; i < ys.size(); ++i){
		EXPECT_NEAR(ys[i] - ys[i - 1], 16.0f, kEpsilon);   // 行高步进
	}
	// 块高 96、控件高 200 ⇒ offsetY = (200−96)/2 = 52；首行 y = 0 + 52
	EXPECT_NEAR(ys[0], 52.0f, kEpsilon);
}

// ── T29-2：CJK + 禁则（kinsoku）——四段式：输入 / 错误 / 允许 / 理想 ────────────
// ★ 措辞受 maxWidth 约束（v1.1 评审建议⑤）：宽度允许时同行；必须换行时闭标点**不得独占行首**。
void Test29WrapCjkKinsuku(){
	WrapProbe probe("ECDI_T29_2", 200, 200);
	probe.SetLabelSize(16, 200);   // 宽 16 ⇒ 一行至多 2 码点

	// ── 段落 A：宽度允许 ⇒ 「你好）世界」整段同一行（不换行）──
	probe.label->SetText(std::string("\xE4\xBD\xA0\xE5\xA5\xBD\xEF\xBC\x89\xE4\xB8\x96\xE7\x95\x8C"));  // 你好）世界
	probe.label->SetWordWrap(true);
	probe.SetLabelSize(64, 200);   // 5 码点 × 8 = 40 ≤ 64 ⇒ 全放得下
	EXPECT_EQ(probe.label->GetPreferredSize().height, 16.0f);   // 1 行

	// ── 段落 B：必须换行 ⇒ 闭标点「）」悬挂进**前行**（微超 = 该标点自身 advance）──
	//   输入 = 你好）世界、宽 16（2 码点）
	//   ✗ 错误 = ["你好", "）世界"]（闭标点独占行首——禁则违背）
	//   △ 允许 = ["你好）世", "界"]
	//   ✓ 理想 = ["你好）", "世界"]（）悬挂上行，行宽 24 > 16 = 微超 8 = 一个标点 advance）
	probe.SetLabelSize(16, 200);
	const Size narrow = probe.label->GetPreferredSize();
	EXPECT_EQ(static_cast<int>(narrow.height), 2 * 16);   // 2 行

	CommandBuffer commands;
	probe.PaintInto(commands);
	const std::vector<std::string> texts = WrapProbe::DrawnTexts(commands);
	EXPECT_EQ(texts.size(), static_cast<std::size_t>(2));
	EXPECT_EQ(texts[0], std::string("\xE4\xBD\xA0\xE5\xA5\xBD\xEF\xBC\x89"));   // 你好）——含悬挂标点
	EXPECT_EQ(texts[1], std::string("\xE4\xB8\x96\xE7\x95\x8C"));               // 世界

	// ── 段落 C：行尾禁则——开括号不得独占行尾 ⇒ 挪到下行 ──
	//   输入 = "ab（cd"、宽 24（3 码点）：自然断点落在 （ 之后 ⇒ ④ 把 （ 挪下去
	probe.label->SetText(std::string("ab\xEF\xBC\x88" "cd"));   // ab（cd
	probe.SetLabelSize(24, 200);
	CommandBuffer commands2;
	probe.PaintInto(commands2);
	const std::vector<std::string> texts2 = WrapProbe::DrawnTexts(commands2);
	EXPECT_EQ(texts2.size(), static_cast<std::size_t>(2));
	EXPECT_EQ(texts2[0], std::string("ab"));                       // （ 被挪到下行
	EXPECT_EQ(texts2[1], std::string("\xEF\xBC\x88" "cd"));      // （cd
}

// ── T29-3：宽度动态——宽度变 ⇒ 行数/preferred 同步变；不变 ⇒ 命中零调用 ─────────
void Test29WrapWidthDynamics(){
	WrapProbe probe("ECDI_T29_3", 200, 200);
	probe.SetLabelSize(32, 200);
	probe.label->SetText(std::string("aaaa bbbb cccc dddd eeee ffff"));
	probe.label->SetWordWrap(true);

	// ① 宽 32（4 码点/行）⇒ 6 行；宽 16（2 码点/行）⇒ 12 行；宽 128 ⇒ 2 行
	EXPECT_EQ(static_cast<int>(probe.label->GetPreferredSize().height), 6 * 16);
	probe.SetLabelSize(16, 200);
	EXPECT_EQ(static_cast<int>(probe.label->GetPreferredSize().height), 12 * 16);
	probe.SetLabelSize(128, 200);
	EXPECT_EQ(static_cast<int>(probe.label->GetPreferredSize().height), 2 * 16);

	// ② 宽度不变（同值 SetSize 亦是「不变」）⇒ 指纹命中 ⇒ **零 FitText 调用**（C29-10 机制判据）
	probe.SetLabelSize(128, 200);
	const std::size_t afterBuild = probe.measurer->FitCalls();
	EXPECT_TRUE(afterBuild > 0);                       // 前面确曾构建过（非空跑）
	(void)probe.label->GetPreferredSize();
	EXPECT_EQ(probe.measurer->FitCalls(), afterBuild);   // ★ 零调用

	// ③ 换宽度 ⇒ 重建（FitText 再被调）
	probe.SetLabelSize(64, 200);
	(void)probe.label->GetPreferredSize();
	EXPECT_TRUE(probe.measurer->FitCalls() > afterBuild);

	// ④ SetWordWrap(false) 切回单行 ⇒ preferred = {测量宽, 行高}（非 {控件宽, 总高}）
	probe.label->SetWordWrap(false);
	const Size single = probe.label->GetPreferredSize();
	EXPECT_NEAR(single.height, 16.0f, kEpsilon);                     // 1 行
	EXPECT_NEAR(single.width, static_cast<float>(29 * 8), kEpsilon); // 全串测量宽
}

// ── T29-WRAP-1：连续 `\n` ⇒ 空行——占行高但**零 DrawText 命令**（D29-Ⅴ）──────
void Test29WrapEmptyLines(){
	WrapProbe probe("ECDI_T29_WRAP1", 200, 200);
	probe.SetLabelSize(40, 200);
	probe.label->SetWordWrap(true);

	// ① "A\n\nB" ⇒ 3 段（第 2 段空）⇒ 3 行（空行占行高、零命令）
	probe.label->SetText(std::string("A\n\nB"));
	EXPECT_EQ(static_cast<int>(probe.label->GetPreferredSize().height), 3 * 16);

	CommandBuffer commands;
	probe.PaintInto(commands);
	const std::vector<std::string> texts = WrapProbe::DrawnTexts(commands);
	EXPECT_EQ(texts.size(), static_cast<std::size_t>(2));   // ★ 空行零命令
	EXPECT_EQ(texts[0], std::string("A"));
	EXPECT_EQ(texts[1], std::string("B"));

	// 空行仍占位置 ⇒ B 的 y = 块起点 + 2×行高（跳过空行那一行的高度）
	const std::vector<float> ys = WrapProbe::DrawnYs(commands);
	EXPECT_EQ(ys.size(), static_cast<std::size_t>(2));
	EXPECT_NEAR(ys[1] - ys[0], 2 * 16.0f, kEpsilon);

	// ② 首行可为空（"\nA" = 2 行）· 末行可为空（"A\n" = 2 行）——D29-Ⅴ 边界
	probe.label->SetText(std::string("\nA"));
	EXPECT_EQ(static_cast<int>(probe.label->GetPreferredSize().height), 2 * 16);
	probe.label->SetText(std::string("A\n"));
	EXPECT_EQ(static_cast<int>(probe.label->GetPreferredSize().height), 2 * 16);

	// ③ 空串 ⇒ 1 个空行（不是 0 行——0 行会让 preferred 高度归零、控件整块消失）
	probe.label->SetText(std::string(""));
	EXPECT_EQ(static_cast<int>(probe.label->GetPreferredSize().height), 1 * 16);
	CommandBuffer emptyCommands;
	probe.PaintInto(emptyCommands);
	EXPECT_EQ(WrapProbe::DrawnTexts(emptyCommands).size(), static_cast<std::size_t>(0));
}

// ── T29-WRAP-3：同宽 SetSize ⇒ 布局命中零重建（C29-10 指纹宽度维）────────────
void Test29WrapSameWidthNoRebuild(){
	WrapProbe probe("ECDI_T29_WRAP3", 200, 200);
	probe.SetLabelSize(40, 200);
	probe.label->SetText(std::string("aaaa bbbb cccc dddd"));
	probe.label->SetWordWrap(true);

	(void)probe.label->GetPreferredSize();
	const std::size_t afterBuild = probe.measurer->FitCalls();
	EXPECT_TRUE(afterBuild > 0);

	// ① 同宽 SetSize（不同高——高不进指纹键）⇒ 零重建
	probe.SetLabelSize(40, 300);
	(void)probe.label->GetPreferredSize();
	EXPECT_EQ(probe.measurer->FitCalls(), afterBuild);

	// ② 再次同宽 GetPreferredSize ⇒ 零重建
	(void)probe.label->GetPreferredSize();
	EXPECT_EQ(probe.measurer->FitCalls(), afterBuild);

	// ③ 反证：换宽 ⇒ 重建（证明上面「零调用」是**缓存命中**而非「压根没建」）
	probe.SetLabelSize(32, 300);
	(void)probe.label->GetPreferredSize();
	EXPECT_TRUE(probe.measurer->FitCalls() > afterBuild);
}

// ── A2 结构判据（批二验收前半）：`wrap == false` ⇒ FitText / 布局构建**零调用** ──
// ★ 红线①「默认关 = 100% 原路径」的可判定形式（命令流逐字节比对在批三 T29-4）。
void Test29WrapDefaultOffZeroFit(){
	WrapProbe probe("ECDI_T29_DEFOFF", 200, 200);
	probe.SetLabelSize(40, 200);
	probe.label->SetText(std::string("aaaa bbbb cccc dddd eeee ffff"));
	// ★ 不调 SetWordWrap（默认 false）

	// ① preferred 走 Phase 28 单行路径 ⇒ 零 FitText 调用
	const Size preferred = probe.label->GetPreferredSize();
	EXPECT_NEAR(preferred.height, 16.0f, kEpsilon);                     // 1 行高
	EXPECT_NEAR(preferred.width, static_cast<float>(29 * 8), kEpsilon); // 全串宽
	EXPECT_EQ(probe.measurer->FitCalls(), static_cast<std::size_t>(0));  // ★ 零调用

	// ② Paint 同样零调用 + **一条** DrawText（原路径）
	CommandBuffer commands;
	probe.PaintInto(commands);
	const std::vector<std::string> texts = WrapProbe::DrawnTexts(commands);
	EXPECT_EQ(texts.size(), static_cast<std::size_t>(1));
	EXPECT_EQ(texts[0].size(), static_cast<std::size_t>(29));   // 整串原样
	EXPECT_EQ(probe.measurer->FitCalls(), static_cast<std::size_t>(0));

	// ③ 宽度 **不够**也不换行（wrap=false 无视宽度——原路径横向溢出，由 Clip 裁）
	probe.SetLabelSize(8, 200);
	CommandBuffer narrowCommands;
	probe.PaintInto(narrowCommands);
	EXPECT_EQ(WrapProbe::DrawnTexts(narrowCommands).size(), static_cast<std::size_t>(1));
}

// ══════════════════════════════════════════════════════════════════════
// Phase 29 批三：T29-4 / T29-5 / T29-WRAP-2 / T29-6 —— A1–A4 全判 + 气泡端到端
// ══════════════════════════════════════════════════════════════════════

// ── T29-4：`wrap == false` 命令流**逐字段**等于 Phase 28 基线（A2「旧路径保留」的终判）──
// ★ 期望命令**手工独立计算**（Phase 28 公式），非调用被测代码——「原样」才有独立参照物。
void Test29WrapDefaultOff(){
	WrapProbe probe("ECDI_T29_4", 200, 200);
	probe.SetLabelSize(40, 200);
	const std::string text = "aaaa bbbb cccc dddd eeee ffff";   // 29 码点
	probe.label->SetText(text);
	// ★ 不调 SetWordWrap（默认 false）

	CommandBuffer commands;
	probe.PaintInto(commands);

	// ① 恰三条命令 = [PushClip(self), DrawText, PopClip]——Widget::Paint 的固有包裹
	//   （Widget.cpp:267/282），**无任何 wrap 侧附加命令**（Label 透明背景 ⇒ 零 DrawRect）
	EXPECT_EQ(commands.size(), static_cast<std::size_t>(3));
	EXPECT_TRUE(std::holds_alternative<PushClipCommand>(commands[0]));
	EXPECT_TRUE(std::holds_alternative<DrawTextCommand>(commands[1]));
	EXPECT_TRUE(std::holds_alternative<PopClipCommand>(commands[2]));
	const auto& cmd = std::get<DrawTextCommand>(commands[1]);

	// ② 逐字段比对 Phase 28 基线（手工算——左对齐 + 垂直居中 (H−行高)/2，P7 定案）
	EXPECT_EQ(cmd.text, text);                                     // 整串逐字节
	EXPECT_NEAR(cmd.pos.x, 0.0f, kEpsilon);                        // 左对齐
	EXPECT_NEAR(cmd.pos.y, (200.0f - 16.0f) / 2.0f, kEpsilon);     // 垂直居中
	EXPECT_TRUE(cmd.color == probe.label->GetTextColor());         // 主题前景色
	const Font themeFont = GetDefaultTheme().GetTextStyle().font.value;
	EXPECT_NEAR(cmd.font.size, themeFont.size, kEpsilon);          // 主题字体（未变）
	EXPECT_EQ(cmd.font.family, themeFont.family);

	// ③ 全程零 FitText（A2 结构判据——连 Paint 路径都零，与批二计数用例互补）
	EXPECT_EQ(probe.measurer->FitCalls(), static_cast<std::size_t>(0));
}

// ── T29-5：退化边界（A3 收尾：width=0 / 超长无空格词 / 宽 < 一码点 / 空串 / 单字符）──
void Test29WrapDegenerate(){
	WrapProbe probe("ECDI_T29_5", 200, 200);
	probe.label->SetWordWrap(true);

	// ① width = 0 ⇒ **退化单行**（D29-B：wrap 不自造宽度约束——preferred 无宽来源 ⇒ 走原路径）
	probe.SetLabelSize(0, 200);
	probe.label->SetText(std::string("aaaa bbbb"));
	const Size zero = probe.label->GetPreferredSize();
	EXPECT_NEAR(zero.width, static_cast<float>(9 * 8), kEpsilon);   // 全串测量宽（Phase 28 原语义）
	EXPECT_NEAR(zero.height, 16.0f, kEpsilon);                       // 1 行
	EXPECT_EQ(probe.measurer->FitCalls(), static_cast<std::size_t>(0));   // 布局引擎零接触
	CommandBuffer zeroCommands;
	probe.PaintInto(zeroCommands);
	EXPECT_EQ(WrapProbe::DrawnTexts(zeroCommands).size(), static_cast<std::size_t>(1));   // 单条

	// ② 超长无空格词 ⇒ **按字硬断**（20 字母 / 宽 40 ⇒ 4 行 × 5 字母——恰好贴边 5×8=40）
	probe.SetLabelSize(40, 200);
	probe.label->SetText(std::string("aaaaaaaaaaaaaaaaaaaa"));   // 20 'a'
	const Size longWord = probe.label->GetPreferredSize();
	EXPECT_EQ(static_cast<int>(longWord.height), 4 * 16);
	CommandBuffer longCommands;
	probe.PaintInto(longCommands);
	const std::vector<std::string> lines = WrapProbe::DrawnTexts(longCommands);
	EXPECT_EQ(lines.size(), static_cast<std::size_t>(4));
	for (const std::string& l : lines){
		EXPECT_EQ(l, std::string("aaaaa"));
	}

	// ③ 宽 < 一个码点 ⇒ 按字硬断**一字**、**绝不挂死**（E7 推进守卫的直接锚——
	//   FitText 返 fitCp=0 时若不强制推进，Paint 会死循环；本断言「能跑完」即守卫成立）
	probe.SetLabelSize(4, 200);
	probe.label->SetText(std::string("AB"));
	const Size tiny = probe.label->GetPreferredSize();
	EXPECT_EQ(static_cast<int>(tiny.height), 2 * 16);   // A / B 各一行
	CommandBuffer tinyCommands;
	probe.PaintInto(tinyCommands);
	const std::vector<std::string> tinyLines = WrapProbe::DrawnTexts(tinyCommands);
	EXPECT_EQ(tinyLines.size(), static_cast<std::size_t>(2));
	EXPECT_EQ(tinyLines[0], std::string("A"));
	EXPECT_EQ(tinyLines[1], std::string("B"));

	// ④ 空串 ⇒ 1 空行（非 0 行——否则 preferred 高度归零）；单字符 ⇒ 1 行
	probe.SetLabelSize(40, 200);
	probe.label->SetText(std::string(""));
	EXPECT_EQ(static_cast<int>(probe.label->GetPreferredSize().height), 16);
	probe.label->SetText(std::string("A"));
	EXPECT_EQ(static_cast<int>(probe.label->GetPreferredSize().height), 16);
	CommandBuffer singleCommands;
	probe.PaintInto(singleCommands);
	const std::vector<std::string> singleLines = WrapProbe::DrawnTexts(singleCommands);
	EXPECT_EQ(singleLines.size(), static_cast<std::size_t>(1));
	EXPECT_EQ(singleLines[0], std::string("A"));
}

// ── T29-WRAP-2：禁则微超 = **单标点/整链悬挂、不吞普通字符**（C29-3 上界的直测）────
void Test29WrapKinsukuOverflow(){
	WrapProbe probe("ECDI_T29_WRAP2", 200, 200);
	probe.SetLabelSize(24, 200);   // 宽 24 ⇒ 一行至多 3 码点
	probe.label->SetWordWrap(true);

	// ① 单标点悬挂：abc）de ⇒ ["abc）", "de"]（行宽 32 = 24 + 8 = 微超一个标点 advance）
	probe.label->SetText(std::string("abc\xEF\xBC\x89" "de"));   // abc）de
	CommandBuffer commands;
	probe.PaintInto(commands);
	std::vector<std::string> texts = WrapProbe::DrawnTexts(commands);
	EXPECT_EQ(texts.size(), static_cast<std::size_t>(2));
	EXPECT_EQ(texts[0], std::string("abc\xEF\xBC\x89"));   // ）悬挂进前行
	EXPECT_EQ(texts[1], std::string("de"));                // ★ 普通字符 **未被吸收**

	// ② 连续禁则链：abc））de ⇒ ["abc））", "de"]（微超 = 链 advance 总和 = 2 标点）
	probe.label->SetText(std::string("abc\xEF\xBC\x89\xEF\xBC\x89" "de"));
	CommandBuffer chainCommands;
	probe.PaintInto(chainCommands);
	texts = WrapProbe::DrawnTexts(chainCommands);
	EXPECT_EQ(texts.size(), static_cast<std::size_t>(2));
	EXPECT_EQ(texts[0], std::string("abc\xEF\xBC\x89\xEF\xBC\x89"));   // 整条链悬挂（C29-3）
	EXPECT_EQ(texts[1], std::string("de"));               // ★ 链外普通字符不得吸收

	// ③ 整段全是闭标点 ⇒ 仍有界终止（链到段末即停——一行全挂，微超 16）
	probe.label->SetText(std::string("\xEF\xBC\x89\xEF\xBC\x89\xEF\xBC\x89"));   // ）））
	probe.SetLabelSize(8, 200);
	CommandBuffer allCommands;
	probe.PaintInto(allCommands);
	texts = WrapProbe::DrawnTexts(allCommands);
	EXPECT_EQ(texts.size(), static_cast<std::size_t>(1));
	EXPECT_EQ(texts[0], std::string("\xEF\xBC\x89\xEF\xBC\x89\xEF\xBC\x89"));
}

// ── T29-6：气泡端到端（需求 §1.1 消费者场景——A4 线性判据 + 高度自适应）──────────
void Test29BubbleEndToEnd(){
	WrapProbe probe("ECDI_T29_6", 300, 400);
	probe.SetLabelSize(200, 50);   // 气泡内容宽 200（= 25 码点/行）
	const std::string message =
		"Phase 29 bubble message: 你好，这是一条混合中英文的长消息 with numbers 12345 "
		"and punctuation！The quick brown fox jumps over the lazy dog，而中文部分逐字断行。";
	probe.label->SetText(message);
	probe.label->SetWordWrap(true);

	// ① 高度自适应：AutoSize ⇒ {气泡宽, 行数 × 行高}（D29-B；长文本必折行 N > 1）
	EXPECT_TRUE(probe.label->AutoSize());
	EXPECT_EQ(probe.label->GetWidth(), 200);
	const int height = probe.label->GetHeight();
	EXPECT_TRUE(height > 16);
	EXPECT_EQ(height % 16, 0);                         // 行高整倍数
	const int lineCount = height / 16;

	// ② ★ **A4 线性判据**：FitText 调用数 == 行数（每行恰一次 fit——扫描状态不重扫，盯防⑧）
	EXPECT_EQ(probe.measurer->FitCalls(), static_cast<std::size_t>(lineCount));

	// ③ 每行都在宽度界内（200/8 = 25 + 禁则链松弛 2）
	CommandBuffer commands;
	probe.PaintInto(commands);
	const std::vector<std::string> texts = WrapProbe::DrawnTexts(commands);
	EXPECT_EQ(texts.size(), static_cast<std::size_t>(lineCount));
	for (const std::string& line : texts){
		std::size_t cps = 0;
		for (std::size_t i = 0; i < line.size(); ++i){
			if ((static_cast<unsigned char>(line[i]) & 0xC0) != 0x80) { ++cps; }
		}
		EXPECT_TRUE(cps <= 27);
	}

	// ④ 宽度动态（端到端形态）：变窄 ⇒ 更多行；恢复原宽 ⇒ 行数复原（指纹失效 + 确定性）
	probe.SetLabelSize(100, 50);
	EXPECT_TRUE(probe.label->AutoSize());
	const int narrowHeight = probe.label->GetHeight();
	EXPECT_TRUE(narrowHeight > height);                // 更窄 ⇒ 更多行
	probe.SetLabelSize(200, 50);
	EXPECT_TRUE(probe.label->AutoSize());
	EXPECT_EQ(probe.label->GetHeight(), height);       // 恢复原行数（同宽同布局）
}

// ══════════════════════════════════════════════════════════════════════
// Phase 30 批一：T30-2 / T30-3 —— Button 文字对齐（C30-2/C30-3）
// ══════════════════════════════════════════════════════════════════════
// ★ 装置 = RecordingBackend（MeasureText 恒 {10,14}）——textWidth 确定 ⇒ pos.x **手算可判**：
//   Left = 0 · Center = (100−10)/2 = 45 · Right = 100−10 = 90；y = (40−14)/2 = 13 三态恒同（P7）。

/// @brief 画一次 button 并取 DrawText 的 pos.x（每次新 PaintContext——镜像栈不留状态）
static float PaintButtonTextX(Button& button, RecordingBackend& backend){
	CommandBuffer commands;
	PaintContext ctx(commands, backend);
	button.Paint(ctx, 0, 0);
	for (const auto& cmd : commands){
		if (const auto* t = std::get_if<DrawTextCommand>(&cmd)){
			return t->pos.x;
		}
	}
	return -1.0f;   // 不可达（Button 恒画文本）——调用方断言会失败
}

void Test30ButtonAlignmentModes(){
	RecordingBackend backend;
	Button button("OK");
	button.SetSize(100, 40);

	// ① 默认（主题注入 Center，未 SetStyle）⇒ 与 Phase 30 前硬编码居中**逐位一致**（C30-3 零变化锚）
	EXPECT_NEAR(PaintButtonTextX(button, backend), 45.0f, kEpsilon);

	// ② 三态切换：Left / Center / Right（C30-2 公式——内容区 = 全控件矩形）
	ButtonStyleOverride left;  left.textAlignment  = TextAlignment::Left;
	ButtonStyleOverride center; center.textAlignment = TextAlignment::Center;
	ButtonStyleOverride right; right.textAlignment  = TextAlignment::Right;

	button.SetStyle(left);
	EXPECT_NEAR(PaintButtonTextX(button, backend), 0.0f, kEpsilon);

	button.SetStyle(center);
	EXPECT_NEAR(PaintButtonTextX(button, backend), 45.0f, kEpsilon);

	button.SetStyle(right);
	EXPECT_NEAR(PaintButtonTextX(button, backend), 90.0f, kEpsilon);

	// ③ 垂直恒居中（P7 三态不变）：取当前 Right 态的 y
	CommandBuffer commands;
	PaintContext ctx(commands, backend);
	button.Paint(ctx, 0, 0);
	const auto* t = std::get_if<DrawTextCommand>(&commands[2]);
	EXPECT_TRUE(t != nullptr);
	if (t) { EXPECT_NEAR(t->pos.y, 13.0f, kEpsilon); }
}

void Test30ButtonAlignmentOverride(){
	RecordingBackend backend;
	Button button("OK");
	button.SetSize(100, 40);

	// ① Override Right ⇒ 生效
	ButtonStyleOverride o; o.textAlignment = TextAlignment::Right;
	button.SetStyle(o);
	EXPECT_NEAR(PaintButtonTextX(button, backend), 90.0f, kEpsilon);

	// ② 再 ApplyTheme ⇒ **仍 Right**（StyleField D7：Override 不被主题覆盖）
	button.ApplyTheme(GetDefaultTheme());
	EXPECT_NEAR(PaintButtonTextX(button, backend), 90.0f, kEpsilon);

	// ③ 空 Override 结构不重置（Set 只处理显式字段）
	button.SetStyle(ButtonStyleOverride{});
	EXPECT_NEAR(PaintButtonTextX(button, backend), 90.0f, kEpsilon);
}

} // anonymous namespace

// ══════════════════════════════════════════════════════════════════════
// Phase 31 批二：T31-2 —— 行高的**下游布局行为**
// 判据（详设 §6 T31-2）：**只测 LineHeight() 返回值不足以证明布局正确**——
//   须覆盖「单行居中 / 多行步进 / totalHeight」三处的实际消费。
// ★ 装置用**确定性测量器**（行高可参数化）——把「消费者如何使用 LineHeight」
//   与「后端具体数值」解耦（后者由 T31-1 覆盖）。
// ══════════════════════════════════════════════════════════════════════

/// @brief 行高**可参数化**的确定性测量器（每码点宽 8；行高由构造参数定）
/// @details 与 `WrapMeasurer` 同构，唯一差别 = `LineHeight` 可注入
///          ⇒ 可断言「布局是否**跟随** LineHeight 变化」（而非恰好等于某个常数）
class ParamLineHeightMeasurer final : public RecordingBackend{
public:
	explicit ParamLineHeightMeasurer(float lineHeight) : m_lineHeight(lineHeight) {}

	Size MeasureText(const Font&, const std::string& text) override{
		// ★ 自有码点计数（与 WrapMeasurer 同模型：1 码点 = 8 宽；不依赖其他类的私有成员）
		return Size{ 8.0f * static_cast<float>(CountCodepoints(text)), m_lineHeight };
	}
	float LineHeight(const Font&) override{ return m_lineHeight; }

private:
	/// @brief UTF-8 码点计数（非续字节即为码点首字节——与 WrapMeasurer 同款）
	static std::size_t CountCodepoints(const std::string& s){
		std::size_t n = 0;
		for (std::size_t i = 0; i < s.size(); ++i){
			if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) { ++n; }
		}
		return n;
	}

	float m_lineHeight;
};

/// @brief 与 WrapProbe 同构、但测量器行高可参数化
struct ParamProbe{
	Application app;
	ParamLineHeightMeasurer* measurer = nullptr;
	Window* window = nullptr;
	Label* label = nullptr;

	ParamProbe(const char* title, int w, int h, float lineHeight){
		auto owned = std::make_unique<ParamLineHeightMeasurer>(lineHeight);
		measurer = owned.get();
		RenderServices services{ std::make_unique<GDIBackend>(), std::move(owned) };
		window = &app.Create(title, w, h, std::move(services));

		auto labelOwned = std::make_unique<Label>(std::string(""));
		label = labelOwned.get();
		window->GetRootWidget().AddChild(std::move(labelOwned));
	}
	~ParamProbe(){ window->Release(); }

	void PaintInto(CommandBuffer& commands){
		PaintContext ctx(commands, window->GetTextMeasurer());
		label->Paint(ctx, 0, 0);
	}

	[[nodiscard]] static std::vector<float> DrawnYs(const CommandBuffer& commands){
		std::vector<float> out;
		for (const auto& cmd : commands){
			if (const auto* text = std::get_if<DrawTextCommand>(&cmd)){
				out.push_back(text->pos.y);
			}
		}
		return out;
	}

	[[nodiscard]] static std::size_t DrawnCount(const CommandBuffer& commands){
		std::size_t n = 0;
		for (const auto& cmd : commands){
			if (std::holds_alternative<DrawTextCommand>(cmd)){ ++n; }
		}
		return n;
	}
};

// ── T31-2a：多行步进 == LineHeight（逐行 y 差 = 行高）──────────────────

void Test31LineHeightDrivesMultiLineStep()
{
	const float kLine = 24.0f;   // ★ 非 16——若布局硬编码 16，本用例必失败

	ParamProbe probe("ECDI_T31_2a", 200, 200, kLine);
	probe.label->SetSize(40, 200);
	probe.label->SetText(std::string("aaaa bbbb cccc dddd"));   // 4 词 ⇒ 每行 1 词（宽 32 ≤ 40）
	probe.label->SetWordWrap(true);

	CommandBuffer commands;
	probe.PaintInto(commands);

	const std::vector<float> ys = ParamProbe::DrawnYs(commands);
	EXPECT_TRUE(ys.size() >= 3);   // 至少 3 行

	// ★ 核心：相邻两行的 y 差 == LineHeight（逐行步进由行高决定）
	for (std::size_t i = 1; i < ys.size(); ++i)
	{
		EXPECT_NEAR(ys[i] - ys[i - 1], kLine, kEpsilon);
	}

	// ★ 整个块高 = 行数 × 行高（preferred 同源）
	const Size preferred = probe.label->GetPreferredSize();
	EXPECT_NEAR(preferred.height, kLine * static_cast<float>(ys.size()), kEpsilon);
}

// ── T31-2b：单行垂直居中位置跟随 LineHeight ──────────────────────────

void Test31LineHeightDrivesSingleLineCenter()
{
	const float kLineA = 16.0f;
	const float kLineB = 40.0f;   // ★ 换行高 ⇒ 居中位置必须随之变化

	// 控件高 100、单行文本 ⇒ 居中 offset = (100 − 行高) / 2
	auto CenterYFor = [](const char* title, float lineHeight) -> float {
		ParamProbe probe(title, 200, 200, lineHeight);
		probe.label->SetSize(200, 100);
		probe.label->SetText(std::string("abc"));   // 单行（宽 24 < 200，不 wrap）
		probe.label->SetWordWrap(false);            // ★ 走非 wrap 路径（用 LineHeight 居中）

		CommandBuffer commands;
		probe.PaintInto(commands);
		const std::vector<float> ys = ParamProbe::DrawnYs(commands);
		EXPECT_EQ(ys.size(), static_cast<std::size_t>(1));
		return ys.empty() ? -1.0f : ys[0];
	};

	const float yA = CenterYFor("ECDI_T31_2b_a", kLineA);
	const float yB = CenterYFor("ECDI_T31_2b_b", kLineB);

	EXPECT_TRUE(yA >= 0.0f && yB >= 0.0f);
	// ★ 判据 = 两值之差 == (kLineB − kLineA) / 2
	//   推导：offsetY = (H − lineHeight) / 2 ⇒ 行高**更大** ⇒ offsetY **更小**（行顶上移）
	//   yA − yB = (H − kLineA)/2 − (H − kLineB)/2 = (kLineB − kLineA)/2 = (40 − 16)/2 = +12
	EXPECT_NEAR(yA - yB, (kLineB - kLineA) / 2.0f, kEpsilon);

	// 且与公式一致（y = (控件高 − 行高) / 2）
	EXPECT_NEAR(yA, (100.0f - kLineA) / 2.0f, kEpsilon);
	EXPECT_NEAR(yB, (100.0f - kLineB) / 2.0f, kEpsilon);
}

// ── T31-2c：空行占行高（totalHeight 含空行——D29-Ⅴ 与行高的交互）──────

void Test31LineHeightEmptyLineOccupies()
{
	const float kLine = 20.0f;

	ParamProbe probe("ECDI_T31_2c", 200, 200, kLine);
	probe.label->SetSize(200, 200);
	probe.label->SetText(std::string("A\n\nB"));   // 3 行（含 1 空行）
	probe.label->SetWordWrap(true);

	// totalHeight = 3 × 行高（空行**占行高**——D29-Ⅴ）
	const Size preferred = probe.label->GetPreferredSize();
	EXPECT_NEAR(preferred.height, 3.0f * kLine, kEpsilon);

	// DrawText 命令 = 2 条（空行零命令——C29-4）
	CommandBuffer commands;
	probe.PaintInto(commands);
	EXPECT_EQ(ParamProbe::DrawnCount(commands), static_cast<std::size_t>(2));

	// ★ 两条命令的 y 差 = 2 × 行高（跨过空行）
	const std::vector<float> ys = ParamProbe::DrawnYs(commands);
	EXPECT_EQ(ys.size(), static_cast<std::size_t>(2));
	if (ys.size() == 2)
	{
		EXPECT_NEAR(ys[1] - ys[0], 2.0f * kLine, kEpsilon);
	}
}

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
    GetTestRegistry().Add("TextWidget.PrefFingerprintHit",      &Test28PrefFingerprintHit);         // T28-3a
    GetTestRegistry().Add("TextWidget.PrefFingerprintInvalidate", &Test28PrefFingerprintInvalidation); // T28-3b
    GetTestRegistry().Add("TextWidget.WrapLatinBasic",          &Test29WrapLatinBasic);           // T29-1
    GetTestRegistry().Add("TextWidget.WrapCjkKinsuku",          &Test29WrapCjkKinsuku);           // T29-2
    GetTestRegistry().Add("TextWidget.WrapWidthDynamics",       &Test29WrapWidthDynamics);        // T29-3
    GetTestRegistry().Add("TextWidget.WrapEmptyLines",          &Test29WrapEmptyLines);           // T29-WRAP-1
    GetTestRegistry().Add("TextWidget.WrapSameWidthNoRebuild",  &Test29WrapSameWidthNoRebuild);   // T29-WRAP-3
    GetTestRegistry().Add("TextWidget.WrapDefaultOffZeroFit",   &Test29WrapDefaultOffZeroFit);    // A2 结构判据（批二）
    GetTestRegistry().Add("TextWidget.WrapDefaultOff",          &Test29WrapDefaultOff);           // T29-4（批三：命令流逐字段基线）
    GetTestRegistry().Add("TextWidget.WrapDegenerate",          &Test29WrapDegenerate);           // T29-5（批三：退化边界）
    GetTestRegistry().Add("TextWidget.WrapKinsukuOverflow",     &Test29WrapKinsukuOverflow);      // T29-WRAP-2（批三：微超不吞普通字符）
    GetTestRegistry().Add("TextWidget.BubbleEndToEnd",          &Test29BubbleEndToEnd);           // T29-6（批三：气泡端到端 + A4）
    GetTestRegistry().Add("Button.TextAlignmentModes",          &Test30ButtonAlignmentModes);     // T30-2（批一）
    GetTestRegistry().Add("Button.TextAlignmentOverride",       &Test30ButtonAlignmentOverride);  // T30-3（批一）
    // ── Phase 31 批二：行高的下游布局行为 ──
    GetTestRegistry().Add("TextWidget.LineHeightMultiLineStep", &Test31LineHeightDrivesMultiLineStep);   // T31-2a
    GetTestRegistry().Add("TextWidget.LineHeightSingleLineCenter", &Test31LineHeightDrivesSingleLineCenter); // T31-2b
    GetTestRegistry().Add("TextWidget.LineHeightEmptyLine",     &Test31LineHeightEmptyLineOccupies);   // T31-2c
}
