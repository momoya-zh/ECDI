#include "RunAllTests.h"
#include "TestFramework.h"
#include "ECDI/Theme/StyleField.h"
#include "ECDI/Theme/DefaultTheme.h"
#include "ECDI/Widget/Button.h"

using namespace ECDI;

namespace {

// T-F01: StyleField 初始值
void TestStyleFieldInitial()
{
    StyleField<Color> field;
    EXPECT_EQ(field.value, Color{});
    EXPECT_FALSE(field.overridden);
}

// T-F02: Set() 修改 value + overridden=true
void TestStyleFieldSet()
{
    StyleField<Color> field;
    field.Set(Color::Red());
    EXPECT_EQ(field.value, Color::Red());
    EXPECT_TRUE(field.overridden);
}

// T-F03: Apply() 未 override 时更新
void TestStyleFieldApplyNotOverridden()
{
    StyleField<Color> field;
    field.Apply(Color::Blue());
    EXPECT_EQ(field.value, Color::Blue());
    EXPECT_FALSE(field.overridden);   // Apply 不改变标志位
}

// T-F04: Apply() 已 override 时不更新（D7 核心）
void TestStyleFieldApplyOverridden()
{
    StyleField<Color> field;
    field.Set(Color::Red());
    field.Apply(Color::Blue());
    EXPECT_EQ(field.value, Color::Red());   // 保持 Red
    EXPECT_TRUE(field.overridden);
}

// T-F05: 多次 Apply 幂等
void TestStyleFieldApplyIdempotent()
{
    StyleField<Color> field;
    field.Apply(Color::Green());
    field.Apply(Color::Green());
    field.Apply(Color::Green());
    EXPECT_EQ(field.value, Color::Green());
}

// T-F06: DefaultTheme 所有字段默认值（完整复刻当前视觉——迁移前后一致）
void TestDefaultThemeValues()
{
    const DefaultTheme& theme = GetDefaultTheme();

    // TextStyle
    auto ts = theme.GetTextStyle();
    EXPECT_EQ(ts.foreground.value, Color::Black());
    EXPECT_EQ(ts.font.value.size, 14.0f);

    // ButtonStyle（全部字段）
    auto btn = theme.GetButtonStyle();
    EXPECT_EQ(btn.background.value, Color::FromRGBA8(80, 120, 220));
    EXPECT_EQ(btn.border.value, Color::White());
    EXPECT_EQ(btn.borderWidth.value, 2.0f);
    EXPECT_EQ(btn.cornerRadius.value, 0.0f);
    EXPECT_EQ(btn.pressedBackground.value, Color::FromRGBA8(60, 90, 180));

    // TextBoxStyle（全部字段）
    auto tb = theme.GetTextBoxStyle();
    EXPECT_EQ(tb.background.value, Color::White());
    EXPECT_EQ(tb.border.value, Color::FromRGBA8(80, 120, 220));
    // 9.6 收尾：TextBoxStyle.borderWidth 已删除（方案 B 后无消费者——见 TextBoxStyle.h/DefaultTheme）
    EXPECT_EQ(tb.selection.value, Color::FromRGBA8(173, 216, 230));
    EXPECT_EQ(tb.composition.value, Color::FromRGBA8(80, 120, 220));
    EXPECT_EQ(tb.caretWidth.value, 2.0f);
    EXPECT_EQ(tb.padding.value, 0.0f);   // 9.6 收尾方案 B：常驻内边距默认 0（旧 2.0f = 焦点内缩，焦点切换会位移）

    // PanelStyle（2026-08-30 v1.7：默认透明——镶板 = 隐形布局容器，phase9.6-panel-container-semantics v1.1）
    auto panel = theme.GetPanelStyle();
    EXPECT_EQ(panel.background.value, Color::FromRGBA8(0, 0, 0, 0));
}

// T-F07: Button 构造后样式已注入（真实集成测试——TestableButton 暴露 protected m_style）
class TestableButton : public Button
{
public:
    using Button::Button;
    const ButtonStyle& GetButtonStyleForTest() const { return m_style; }
};

void TestButtonApplyTheme()
{
    TestableButton button("Test");
    const auto& style = button.GetButtonStyleForTest();
    EXPECT_EQ(style.background.value, Color::FromRGBA8(80, 120, 220));
    EXPECT_EQ(style.border.value, Color::White());
    EXPECT_EQ(style.borderWidth.value, 2.0f);
    EXPECT_EQ(style.pressedBackground.value, Color::FromRGBA8(60, 90, 180));
}

// T-F08: SetStyle 后 ApplyTheme 不覆盖（D7 契约）
void TestSetStyleThenApplyTheme()
{
    TestableButton button("Test");
    ButtonStyleOverride override;
    override.background = Color::Red();
    button.SetStyle(override);              // 用户覆盖 background = Red

    button.ApplyTheme(GetDefaultTheme());   // 主题重新应用

    const auto& style = button.GetButtonStyleForTest();
    EXPECT_EQ(style.background.value, Color::Red());   // 保持用户值（D7）
    EXPECT_EQ(style.border.value, Color::White());     // 未覆盖的属性恢复主题默认值
}

// T-F09: DefaultTheme 自身一致性
void TestDefaultThemeConsistency()
{
    const DefaultTheme& theme = GetDefaultTheme();
    auto btn1 = theme.GetButtonStyle();
    auto btn2 = theme.GetButtonStyle();
    EXPECT_EQ(btn1.background.value, btn2.background.value);
    EXPECT_EQ(btn1.pressedBackground.value, btn2.pressedBackground.value);
}

// T-F10: TextStyle 与 ButtonStyle 独立覆盖共存（using SetStyle + D7 联动）
//        场景：TextStyle.foreground=Red + ButtonStyle.background=Blue → ApplyTheme
//        → 两处 Override 都保留，未覆盖字段回主题默认
void TestButtonTextAndButtonStyleIndependent()
{
    TestableButton button("Test");

    TextStyleOverride textOverride;
    textOverride.foreground = Color::Red();
    button.SetStyle(textOverride);          // 经 using 暴露的基类 SetStyle

    ButtonStyleOverride buttonOverride;
    buttonOverride.background = Color::Blue();
    button.SetStyle(buttonOverride);        // Button 专属 SetStyle

    button.ApplyTheme(GetDefaultTheme());   // 主题重新应用——不破坏任何 Override

    // ButtonStyle: background Override 保留
    EXPECT_EQ(button.GetButtonStyleForTest().background.value, Color::Blue());
    // ButtonStyle: 未覆盖的 border 回主题默认
    EXPECT_EQ(button.GetButtonStyleForTest().border.value, Color::White());
    // TextStyle: foreground Override 保留（经 GetTextColor 只读查询）
    EXPECT_EQ(button.GetTextColor(), Color::Red());
}

} // anonymous namespace

// ══════════════════════════════════════════════════════════════════════
// 审计 H10（#66）：Button 文字色主题通路（契约变更 = 方案 B）
// ══════════════════════════════════════════════════════════════════════
// ★ 缺陷（变更前）：`Button::ApplyTheme` 在 `TextWidget::ApplyTheme` 之后**硬编码**注入
//   `Color::White()` ⇒ ① 自定义主题的 `GetTextStyle().foreground` 对 Button **恒被盖掉**
//   （主题侧死值）；② 同一属性两个互相冲突的默认值（Black 立刻被 White 盖）；
//   ③ `GetButtonStyle().foreground` 即使设了也无效。
// ★ 修法（方案 B · 改契约）：Button 文字色**归主题**——`ButtonStyle` 增 `foreground`
//   字段（`DefaultTheme` 给 White = 原硬编码值，默认观感零变化），`Button::ApplyTheme`
//   改注 `ButtonStyle.foreground`；`TextStyle.foreground` 保留为**单点覆盖**通路。
//   两通路写的是同一个字段（`TextWidget::m_style.foreground`）⇒ D7 语义不变。
// ★ 用户拍板（2026-10-12）：「目前我们还没正式成库，改契约其实没什么问题」。

namespace{

/// @brief 自定义主题：Button 文字色 = 红（用于证明**主题通路真的生效**）
/// @details ★ 这是本组用例的核心装置——变更前这个主题对 Button 文字色**完全无效**。
struct RedButtonTextTheme : public Theme{
	TextStyle GetTextStyle() const override{
		TextStyle s;
		s.foreground.value = Color::Black();   // 全局文本默认（Label/TextBox）
		s.font.value = Font{};
		return s;
	}
	ButtonStyle GetButtonStyle() const override{
		ButtonStyle s = DefaultTheme{}.GetButtonStyle();
		s.foreground.value = Color::Red();     // ★ 本主题的 Button 文字色
		return s;
	}
	TextBoxStyle GetTextBoxStyle() const override{ return DefaultTheme{}.GetTextBoxStyle(); }
	PanelStyle GetPanelStyle() const override{ return DefaultTheme{}.GetPanelStyle(); }
	CheckBoxStyle GetCheckBoxStyle() const override{ return DefaultTheme{}.GetCheckBoxStyle(); }
	RadioStyle GetRadioStyle() const override{ return DefaultTheme{}.GetRadioStyle(); }
	ProgressBarStyle GetProgressBarStyle() const override{ return DefaultTheme{}.GetProgressBarStyle(); }
	ScrollBarStyle GetScrollBarStyle() const override{ return DefaultTheme{}.GetScrollBarStyle(); }
};

/// @brief 读 Button 的**实际文字视觉字段**（`TextWidget::m_style.foreground`）
/// @details ★ 关键：不能读 `Button::m_style.foreground`（那是 ButtonStyle 的主题副本）。
///          `OnPaint` 经 `DrawTextContent` 消费的是 **TextWidget** 的那个字段 ⇒
///          本探针读的才是「用户真正看到的文字色」。
///          `GetTextColor()` 正是该字段的公共只读入口（TextWidget.cpp:64-68）。
struct ButtonProbe : public Button{
	using Button::Button;
	const ButtonStyle& ButtonStyleForTest() const{ return m_style; }
};

} // anonymous namespace

// ── H10-①：自定义主题的 Button 文字色**真的生效**（变更前的核心失效面）──────
void Test66ThemeButtonForegroundHonored()
{
	ButtonProbe button("Test");
	button.ApplyTheme(GetDefaultTheme());
	EXPECT_EQ(button.GetTextColor(), Color::White());   // 缺省：白字（与原硬编码值逐位一致）

	// ★ 换自定义主题 ⇒ Button 文字色必须跟随（变更前**恒为 White**、本断言必红）
	button.ApplyTheme(RedButtonTextTheme{});
	EXPECT_EQ(button.GetTextColor(), Color::Red());

	// ★ 且主题通路不只影响「首次」——再次应用别的主题仍跟随（无粘滞）
	button.ApplyTheme(GetDefaultTheme());
	EXPECT_EQ(button.GetTextColor(), Color::White());
}

// ── H10-②：默认观感零变化（契约变更不得改默认视觉）──────────────────────
void Test66DefaultButtonTextUnchanged()
{
	// ① 构造即注入（Button 构造体调 ApplyTheme(GetDefaultTheme())）
	ButtonProbe constructed("Test");
	EXPECT_EQ(constructed.GetTextColor(), Color::White());

	// ② DefaultTheme 的 ButtonStyle **确实**携带 White（不再是死值）
	EXPECT_EQ(DefaultTheme{}.GetButtonStyle().foreground.value, Color::White());

	// ③ 与 TextStyle 的全局默认（Black）**有意不同**——证明两个默认值不再互相覆盖：
	//    Label/TextBox 走 TextStyle=Black；Button 走 ButtonStyle=White。
	EXPECT_EQ(DefaultTheme{}.GetTextStyle().foreground.value, Color::Black());
}

// ── H10-③：D7 Override 在两通路上都成立（各自 Set 后不被主题覆盖）────────
void Test66ButtonForegroundOverrideBothPaths()
{
	// 通路 ①：ButtonStyleOverride.foreground（Button 侧入口）
	{
		ButtonProbe button("Test");
		ButtonStyleOverride o;
		o.foreground = Color::Green();
		button.SetStyle(o);
		button.ApplyTheme(RedButtonTextTheme{});          // 主题换成红
		EXPECT_EQ(button.GetTextColor(), Color::Green());  // ★ Override 保留（D7）
	}

	// 通路 ②：TextStyleOverride.foreground（基类通用入口——经 using 暴露）
	{
		ButtonProbe button("Test");
		TextStyleOverride o;
		o.foreground = Color::Blue();
		button.SetStyle(o);
		button.ApplyTheme(RedButtonTextTheme{});
		EXPECT_EQ(button.GetTextColor(), Color::Blue());   // ★ 同款保留
	}

	// 交叉：两条通路写同一字段 ⇒ 后写者赢
	{
		ButtonProbe button("Test");
		ButtonStyleOverride b;
		b.foreground = Color::Green();
		button.SetStyle(b);
		TextStyleOverride t;
		t.foreground = Color::Blue();
		button.SetStyle(t);                                // 后写 ⇒ 蓝赢
		EXPECT_EQ(button.GetTextColor(), Color::Blue());
		button.ApplyTheme(RedButtonTextTheme{});
		EXPECT_EQ(button.GetTextColor(), Color::Blue());    // 仍是 Override 值
	}
}

void ECDI::Test::RegisterThemeTests()
{
    GetTestRegistry().Add("Theme.StyleFieldInitial",         &TestStyleFieldInitial);
    GetTestRegistry().Add("Theme.StyleFieldSet",             &TestStyleFieldSet);
    GetTestRegistry().Add("Theme.StyleFieldApply",           &TestStyleFieldApplyNotOverridden);
    GetTestRegistry().Add("Theme.StyleFieldApplyNoOverride", &TestStyleFieldApplyOverridden);
    GetTestRegistry().Add("Theme.StyleFieldApplyIdempotent", &TestStyleFieldApplyIdempotent);
    GetTestRegistry().Add("Theme.DefaultThemeValues",        &TestDefaultThemeValues);
    GetTestRegistry().Add("Theme.ButtonApplyTheme",          &TestButtonApplyTheme);
    GetTestRegistry().Add("Theme.SetStyleThenApply",         &TestSetStyleThenApplyTheme);
    GetTestRegistry().Add("Theme.DefaultThemeConsistent",    &TestDefaultThemeConsistency);
    GetTestRegistry().Add("Theme.ButtonTextAndButtonStyle",  &TestButtonTextAndButtonStyleIndependent);
    // ── 审计 H10（#66）：Button 文字色主题通路（契约变更 = 方案 B）──
    GetTestRegistry().Add("Theme.ButtonForegroundThemeHonored",      &Test66ThemeButtonForegroundHonored);
    GetTestRegistry().Add("Theme.ButtonForegroundDefaultUnchanged",  &Test66DefaultButtonTextUnchanged);
    GetTestRegistry().Add("Theme.ButtonForegroundOverrideBothPaths", &Test66ButtonForegroundOverrideBothPaths);
}
