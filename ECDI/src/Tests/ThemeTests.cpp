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
    EXPECT_EQ(tb.borderWidth.value, 2.0f);
    EXPECT_EQ(tb.selection.value, Color::FromRGBA8(173, 216, 230));
    EXPECT_EQ(tb.composition.value, Color::FromRGBA8(80, 120, 220));
    EXPECT_EQ(tb.caretWidth.value, 2.0f);
    EXPECT_EQ(tb.padding.value, 2.0f);

    // PanelStyle
    auto panel = theme.GetPanelStyle();
    EXPECT_EQ(panel.background.value, Color::Gray());
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
}
