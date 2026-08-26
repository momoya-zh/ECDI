#include "RunAllTests.h"
#include "TestFramework.h"
#include "ECDI/Widget/CheckBox.h"
#include "ECDI/Widget/Radio.h"
#include "ECDI/Widget/Widget.h"
#include "ECDI/Theme/DefaultTheme.h"
#include "ECDI/Render/PaintContext.h"
#include "ECDI/Render/RecordingBackend.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyCode.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyDownEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyModifier.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonEvent.h"

#include <memory>
#include <utility>
#include <variant>

using namespace ECDI;

namespace {

// 测试派生类：暴露 protected 键鼠路径（同 TextBoxTests TestableTextBox 先例）
class TestableCheckBox : public CheckBox
{
public:
    using CheckBox::CheckBox;
    using CheckBox::OnKeyDown;
    using CheckBox::OnMouseButtonDown;
    const CheckBoxStyle& GetCheckBoxStyleForTest() const { return m_style; }
};

class TestableRadio : public Radio
{
public:
    using Radio::Radio;
    using Radio::OnKeyDown;
};

// S2: OnCheckedChanged 虚方法记录（子类 override 被调用）
class RecordCheckBox : public TestableCheckBox
{
public:
    using TestableCheckBox::TestableCheckBox;
    int changeCount = 0;
    bool lastChanged = false;
protected:
    void OnCheckedChanged(bool checked) override { ++changeCount; lastChanged = checked; }
};

// S1: CheckBox 状态切换 + 相同值 no-op（无通知）
void TestCheckBoxState()
{
    RecordCheckBox box("Test");
    box.SetChecked(true);
    EXPECT_TRUE(box.IsChecked());
    EXPECT_EQ(box.changeCount, 1);
    EXPECT_TRUE(box.lastChanged);

    box.SetChecked(false);
    EXPECT_FALSE(box.IsChecked());
    EXPECT_EQ(box.changeCount, 2);

    box.SetChecked(false);   // 相同值 no-op——不触发通知
    EXPECT_EQ(box.changeCount, 2);
}

// S3: SetOnCheckedChanged 用户回调（std::function 被调用——与虚方法并存）
void TestCheckBoxCallback()
{
    TestableCheckBox box("Test");
    int callbackCount = 0;
    bool lastValue = false;
    box.SetOnCheckedChanged([&](bool checked){ ++callbackCount; lastValue = checked; });

    box.SetChecked(true);
    EXPECT_EQ(callbackCount, 1);
    EXPECT_TRUE(lastValue);

    box.SetChecked(true);   // 相同值 no-op——回调也不触发
    EXPECT_EQ(callbackCount, 1);
}

// S4: Space 键切换（键鼠共享——OnKeyDown(Space) → OnClickToggle）
void TestCheckBoxSpaceToggle()
{
    TestableCheckBox box("Test");
    box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Space, KeyModifier::None));
    EXPECT_TRUE(box.IsChecked());
    box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Space, KeyModifier::None));
    EXPECT_FALSE(box.IsChecked());
}

// S5: 鼠标点击切换
void TestCheckBoxMouseToggle()
{
    TestableCheckBox box("Test");
    box.OnMouseButtonDown(MouseButtonDownEvent(nullptr, 0, 0, MouseButton::Left));
    EXPECT_TRUE(box.IsChecked());
}

// S6-S9: Radio 互斥（需 AddChild 建树——GetParent 非空才互斥）
void TestRadioMutex()
{
    Widget parent;
    auto a = std::make_unique<TestableRadio>("A");
    auto b = std::make_unique<TestableRadio>("B");
    auto c = std::make_unique<TestableRadio>("C");
    TestableRadio* ra = a.get();
    TestableRadio* rb = b.get();
    TestableRadio* rc = c.get();
    parent.AddChild(std::move(a));
    parent.AddChild(std::move(b));
    parent.AddChild(std::move(c));

    // S6: 同父互斥——选 B → A/C 自动取消
    rb->SetChecked(true);
    EXPECT_TRUE(rb->IsChecked());
    EXPECT_FALSE(ra->IsChecked());
    EXPECT_FALSE(rc->IsChecked());

    // S7: 交互不可取消——已选中再点（Space → OnClickToggle → SetChecked(true)）无变化
    rb->OnKeyDown(KeyDownEvent(nullptr, KeyCode::Space, KeyModifier::None));
    EXPECT_TRUE(rb->IsChecked());

    // S8: 程序 API 可取消
    rb->SetChecked(false);
    EXPECT_FALSE(rb->IsChecked());

    // 通知顺序契约：选 C → A/B 已取消，C 最后选中（兄弟先 false 自身后 true——冻结契约）
    rc->SetChecked(true);
    EXPECT_TRUE(rc->IsChecked());
    EXPECT_FALSE(rb->IsChecked());
}

// S9: 跨父不互斥（不同父容器——不跨级）
void TestRadioCrossParent()
{
    Widget p1, p2;
    auto a = std::make_unique<TestableRadio>("A");
    auto b = std::make_unique<TestableRadio>("B");
    TestableRadio* ra = a.get();
    TestableRadio* rb = b.get();
    p1.AddChild(std::move(a));
    p2.AddChild(std::move(b));

    ra->SetChecked(true);
    rb->SetChecked(true);   // 不同父——不互斥
    EXPECT_TRUE(ra->IsChecked());
    EXPECT_TRUE(rb->IsChecked());
}

// S10: CheckBox ApplyTheme/SetStyle（D7——构造注入 + SetStyle 后 ApplyTheme 不覆盖）
void TestCheckBoxTheme()
{
    TestableCheckBox box("Test");
    const auto& style = box.GetCheckBoxStyleForTest();
    // 构造注入默认值
    EXPECT_EQ(style.boxSize.value, 16.0f);
    EXPECT_EQ(style.border.value, Color::Black());

    CheckBoxStyleOverride override;
    override.border = Color::Red();
    box.SetStyle(override);               // 用户覆盖 border = Red

    box.ApplyTheme(GetDefaultTheme());    // 主题重新应用
    EXPECT_EQ(style.border.value, Color::Red());   // D7：保持用户值
    EXPECT_EQ(style.boxSize.value, 16.0f);        // 未覆盖字段回主题默认
}

// S11: CheckBox 未选中绘制命令（框 + 背景 + 文本——勾线不出现）
void TestCheckBoxPaintUnchecked()
{
    RecordingBackend backend;
    CommandBuffer commands;
    PaintContext ctx(commands, backend);

    TestableCheckBox box("T");
    box.SetSize(100, 24);
    box.Paint(ctx, 0, 0);

    EXPECT_EQ(commands.size(), 3);
    EXPECT_TRUE(std::holds_alternative<DrawRectCommand>(commands[0]));   // 外框
    EXPECT_TRUE(std::holds_alternative<DrawRectCommand>(commands[1]));   // 内背景
    EXPECT_TRUE(std::holds_alternative<DrawTextCommand>(commands[2]));   // 文本
}

// S12: CheckBox 选中绘制命令（勾 = 两条 DrawLine 折线——勾几何 25%→45%→78%）
void TestCheckBoxPaintChecked()
{
    RecordingBackend backend;
    CommandBuffer commands;
    PaintContext ctx(commands, backend);

    TestableCheckBox box("T");
    box.SetSize(100, 24);
    box.SetChecked(true);
    box.Paint(ctx, 0, 0);

    EXPECT_EQ(commands.size(), 5);
    EXPECT_TRUE(std::holds_alternative<DrawRectCommand>(commands[0]));         // 外框
    EXPECT_TRUE(std::holds_alternative<DrawRectCommand>(commands[1]));         // 内背景
    EXPECT_TRUE(std::holds_alternative<DrawLineCommand>(commands[2]));         // 勾段 1（左下→中）
    EXPECT_TRUE(std::holds_alternative<DrawLineCommand>(commands[3]));         // 勾段 2（中→右上）
    EXPECT_TRUE(std::holds_alternative<DrawTextCommand>(commands[4]));         // 文本

    // 勾几何验证（boxSize=16：25%/55% → 45%/75% → 78%/28%）
    const auto& l1 = std::get<DrawLineCommand>(commands[2]);
    EXPECT_NEAR(l1.start.x, 16.0f * 0.25f, 0.01f);
    EXPECT_NEAR(l1.end.x,   16.0f * 0.45f, 0.01f);
    const auto& l2 = std::get<DrawLineCommand>(commands[3]);
    EXPECT_NEAR(l2.start.x, 16.0f * 0.45f, 0.01f);
    EXPECT_NEAR(l2.end.x,   16.0f * 0.78f, 0.01f);
}

// S13: Radio 未选中绘制命令（外圆 + 内背景 + 文本）
void TestRadioPaintUnchecked()
{
    RecordingBackend backend;
    CommandBuffer commands;
    PaintContext ctx(commands, backend);

    TestableRadio radio("T");
    radio.SetSize(100, 24);
    radio.Paint(ctx, 0, 0);

    EXPECT_EQ(commands.size(), 3);
    EXPECT_TRUE(std::holds_alternative<DrawRoundedRectCommand>(commands[0]));  // 外圆
    EXPECT_TRUE(std::holds_alternative<DrawRoundedRectCommand>(commands[1]));  // 内背景
    EXPECT_TRUE(std::holds_alternative<DrawTextCommand>(commands[2]));         // 文本
}

// S14: Radio 选中绘制命令（外圆 + 内背景 + 圆点——圆点 = circleSize*40%）
void TestRadioPaintChecked()
{
    RecordingBackend backend;
    CommandBuffer commands;
    PaintContext ctx(commands, backend);

    TestableRadio radio("T");
    radio.SetSize(100, 24);
    radio.SetChecked(true);
    radio.Paint(ctx, 0, 0);

    EXPECT_EQ(commands.size(), 4);
    EXPECT_TRUE(std::holds_alternative<DrawRoundedRectCommand>(commands[0]));  // 外圆
    EXPECT_TRUE(std::holds_alternative<DrawRoundedRectCommand>(commands[1]));  // 内背景
    EXPECT_TRUE(std::holds_alternative<DrawRoundedRectCommand>(commands[2]));  // 圆点
    EXPECT_TRUE(std::holds_alternative<DrawTextCommand>(commands[3]));         // 文本

    // 圆点几何验证（circleSize=16 → dot=6.4, offset=4.8）
    const auto& dot = std::get<DrawRoundedRectCommand>(commands[2]);
    EXPECT_NEAR(dot.rect.x, 4.8f, 0.01f);
    EXPECT_NEAR(dot.rect.width, 6.4f, 0.01f);
    EXPECT_NEAR(dot.cornerRadius, 3.2f, 0.01f);
}

} // anonymous namespace

void ECDI::Test::RegisterCheckBoxTests()
{
    GetTestRegistry().Add("CheckBox.State",           &TestCheckBoxState);
    GetTestRegistry().Add("CheckBox.Callback",        &TestCheckBoxCallback);
    GetTestRegistry().Add("CheckBox.SpaceToggle",     &TestCheckBoxSpaceToggle);
    GetTestRegistry().Add("CheckBox.MouseToggle",     &TestCheckBoxMouseToggle);
    GetTestRegistry().Add("Radio.Mutex",              &TestRadioMutex);
    GetTestRegistry().Add("Radio.CrossParent",        &TestRadioCrossParent);
    GetTestRegistry().Add("CheckBox.Theme",           &TestCheckBoxTheme);
    GetTestRegistry().Add("CheckBox.PaintUnchecked",  &TestCheckBoxPaintUnchecked);
    GetTestRegistry().Add("CheckBox.PaintChecked",    &TestCheckBoxPaintChecked);
    GetTestRegistry().Add("Radio.PaintUnchecked",     &TestRadioPaintUnchecked);
    GetTestRegistry().Add("Radio.PaintChecked",       &TestRadioPaintChecked);
}
