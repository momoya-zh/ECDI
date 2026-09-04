#include "RunAllTests.h"
#include "TestFramework.h"
#include "ECDI/Render/PaintContext.h"
#include "ECDI/Render/RecordingBackend.h"
#include "ECDI/Render/RenderCommand.h"
#include "ECDI/Widget/Panel.h"
#include "ECDI/Widget/Button.h"
#include "ECDI/Widget/Label.h"
#include "ECDI/Widget/TextBox.h"
#include "ECDI/Widget/Widget.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyDownEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyModifier.h"
#include "ECDI/Core/Point.h"
#include "ECDI/Core/Color.h"
#include "ECDI/Core/Size.h"
#include <memory>
#include <utility>

using namespace ECDI;

constexpr float kEps = 0.001f;

namespace {

/// @brief 按字符数给真实宽度的测量桩（Clip.SelectionNoClamp 专用）
/// @details RecordingBackend::MeasureText 对任意输入（含空串）恒返回 {10,14}——
/// 高亮宽 = hlMax - hlMin 恒为 0，「超宽 + 不 clamp」语义不可观测。
/// 本桩：宽 = 5px × 字符数（空串 = 0）、行高 16——26 字符 = 130 > 96 可视宽，恢复测试前提。
class ProportionalMeasurer final: public RecordingBackend{
public:

	Size MeasureText(const Font&, const std::string& text) override{

		return Size{ 5.0f * static_cast<float>(text.size()), 16.0f };

	}

	float LineHeight(const Font&) override{ return 16.0f; }

};

/// @brief 可测 TextBox：暴露 protected 成员（9.5 R1——横向滚动纯规则/坐标定位/滚动维护）
class TestableTextBox : public TextBox
{
public:
    using TextBox::TextBox;
    using TextBox::OnKeyDown;               ///< 键盘选择路径（S4 造 Selection——Shift+方向）
    using TextBox::CaretIndexFromPosition;   ///< 多行坐标定位（protected）
    using TextBox::ClampScrollOffsetX;       ///< 横向滚动纯规则（protected static——S7/S8 直测）
};

// ── Clip 命令辅助：遍历命令流，跟踪 PushClip/PopClip 深度序列 ──

/// @brief 遍历命令，返回 PushClip 命令索引（按出现顺序）与 Clip 深度终值
struct ClipTrace
{
    std::vector<const PushClipCommand*> pushes;   ///< 按出现顺序
    std::vector<size_t> pushIndices;              ///< 在 commands 中的索引
    int finalDepth = 0;                           ///< 遍历完的深度（必须 0）
};

ClipTrace TraceClip(const CommandBuffer& commands)
{
    ClipTrace trace;
    int depth = 0;
    for (size_t i = 0; i < commands.size(); ++i){
        if (const auto* push = std::get_if<PushClipCommand>(&commands[i])){
            trace.pushes.push_back(push);
            trace.pushIndices.push_back(i);
            ++depth;
        }
        else if (std::get_if<PopClipCommand>(&commands[i])){
            --depth;   // 配对契约：遍历中 depth 恒 ≥ 0（I2）
        }
    }
    trace.finalDepth = depth;
    return trace;
}

// ── R1-S1：3 层树 Clip 深度序列（0→1→2→3→2→1→0，嵌套顺序）──

void TestClipDepthSequence()
{
    RecordingBackend backend;
    CommandBuffer commands;
    PaintContext ctx(commands, backend);

    Widget root;
    root.SetPosition(0, 0);
    root.SetSize(200, 150);

    auto panel = std::make_unique<Panel>();
    panel->SetPosition(10, 20);
    panel->SetSize(100, 50);
    auto* panelRaw = panel.get();

    auto button = std::make_unique<Button>("OK");
    button->SetPosition(5, 5);
    button->SetSize(30, 20);
    auto* buttonRaw = button.get();

    auto label = std::make_unique<Label>("Hi");
    label->SetPosition(3, 3);
    label->SetSize(20, 10);
    auto* labelRaw = label.get();

    buttonRaw->AddChild(std::move(label));
    panelRaw->AddChild(std::move(button));
    root.AddChild(std::move(panel));

    root.Paint(ctx, 0, 0);

    // 深度序列：4 个 Push（root/panel/button/label）+ 4 个 Pop，终值 0
    const ClipTrace trace = TraceClip(commands);
    EXPECT_EQ(trace.finalDepth, 0);
    EXPECT_EQ(trace.pushes.size(), 4);

    // 嵌套顺序：Push 顺序 = root → panel → button → label（父先于子）
    EXPECT_NEAR(trace.pushes[0]->rect.x, 0.0f, kEps);     // root
    EXPECT_NEAR(trace.pushes[1]->rect.x, 10.0f, kEps);    // panel
    EXPECT_NEAR(trace.pushes[2]->rect.x, 15.0f, kEps);    // button（10+5）
    EXPECT_NEAR(trace.pushes[3]->rect.x, 18.0f, kEps);    // label（15+3）

    // Pop 必须逆序（子先于父）——最后一个命令是 PopClip（label 的 Pop 在最后）
    EXPECT_TRUE(std::holds_alternative<PopClipCommand>(commands.back()));
}

// ── R1-S2：裁剪矩形坐标（offset 累加 = Window 客户区绝对坐标）──

void TestClipRectAbsolute()
{
    RecordingBackend backend;
    CommandBuffer commands;
    PaintContext ctx(commands, backend);

    Panel panel;
    panel.SetPosition(10, 20);
    panel.SetSize(100, 50);
    // 2026-08-30（phase9.6-panel-container-semantics v1.1）：Panel 默认背景透明 → a==0 短路无 DrawRect，
    // 命令流 = PushClip → PopClip（size 2）；PushClip 几何断言不受影响
    panel.Paint(ctx, 0, 0);

    // PushClip(10,20,100,50) → PopClip
    EXPECT_EQ(commands.size(), 2);
    const auto& push = std::get<PushClipCommand>(commands[0]);
    EXPECT_NEAR(push.rect.x, 10.0f, kEps);
    EXPECT_NEAR(push.rect.y, 20.0f, kEps);
    EXPECT_NEAR(push.rect.width, 100.0f, kEps);
    EXPECT_NEAR(push.rect.height, 50.0f, kEps);
    EXPECT_TRUE(std::holds_alternative<PopClipCommand>(commands[1]));
}

// ── R1-S3：TextBox 超宽行不截断（DrawText 命令 = 整行 + 位于 PushClip 后）──

void TestClipTextBoxFullLine()
{
    RecordingBackend backend;
    CommandBuffer commands;
    PaintContext ctx(commands, backend);

    // 超宽文本：26 字母，可视宽 ~96px（100 - 焦点内缩 2×2=4）——文本宽远超可视
    TestableTextBox box("abcdefghijklmnopqrstuvwxyz");
    box.SetSize(100, 30);
    box.Paint(ctx, 0, 0);

    // 命令流（2026-08-27 文本区 Clip 后）：
    // PushClip(控件边界) → DrawRect(背景) → PushClip(背景区) → DrawText(整行) → PopClip → PopClip
    EXPECT_EQ(commands.size(), 6);
    EXPECT_TRUE(std::holds_alternative<PushClipCommand>(commands[0]));   // 控件边界（Widget::Paint）
    EXPECT_TRUE(std::holds_alternative<PushClipCommand>(commands[2]));   // 背景区（TextBox::OnPaint 内）
    const auto& text = std::get<DrawTextCommand>(commands[3]);
    EXPECT_EQ(text.text, "abcdefghijklmnopqrstuvwxyz");   // 整行——不再 substr 截断
    EXPECT_TRUE(std::holds_alternative<PopClipCommand>(commands[4]));
    EXPECT_TRUE(std::holds_alternative<PopClipCommand>(commands[5]));   // 背景区先出栈（后进先出）
}

// ── R1-S4：Selection 高亮不 clamp（完整逻辑几何，超宽由 Clip 裁）──

void TestClipSelectionNoClamp()
{
    ProportionalMeasurer backend;   // 5px/字符、行高 16——超宽前提可观测（26 字符 = 130 > 96）
    CommandBuffer commands;
    PaintContext ctx(commands, backend);

    TestableTextBox box("abcdefghijklmnopqrstuvwxyz");   // 26 字母（超宽）
    box.SetSize(100, 30);
    box.MoveCaretToStart();
    // Shift+Right ×26 → 选中全文本（anchor=0, caret=26）
    for (int i = 0; i < 26; ++i)
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Right, KeyModifier::Shift));

    box.Paint(ctx, 0, 0);

    // 高亮矩形 = DrawRect{ viewX + hlMin, lineY, hlMax - hlMin, lineH }
    //   hlMin = MeasureText(前缀="") = 0；hlMax = MeasureText(全文) = 130 → {0,0,130,16}
    // 定位条件：高 = 行高 16（背景高 30 排除；组合下划线高 1 排除）+ 宽 > 96（超宽——
    //   背景宽 100 虽超宽但高度不匹配；"宽 == 全文测量宽 130"即不 clamp 语义——
    //   若实现错误 clamp 到可视宽会得到 96 或文本区宽 100，断言即失败）
    const float fullWidth = backend.MeasureText(Font{}, "abcdefghijklmnopqrstuvwxyz").width;   // == 130
    bool foundSelection = false;
    for (const auto& cmd : commands){
        if (const auto* rect = std::get_if<DrawRectCommand>(&cmd)){
            if (std::abs(rect->rect.height - 16.0f) <= kEps && rect->rect.width > 96.0f){
                foundSelection = true;
                EXPECT_NEAR(rect->rect.width, fullWidth, kEps);   // 完整逻辑几何（不 clamp）
            }
        }
    }
    EXPECT_TRUE(foundSelection);
}

// ── R1-S5：越界子控件（父 PushClip 在命令流中，绘制约束由后端保证）──

void TestClipChildOverflow()
{
    RecordingBackend backend;
    CommandBuffer commands;
    PaintContext ctx(commands, backend);

    Panel panel;
    panel.SetPosition(10, 20);
    panel.SetSize(100, 50);

    auto button = std::make_unique<Button>("Wide");
    button->SetPosition(5, 5);
    button->SetSize(200, 20);   // 超出父边界（10+200 > 110）
    auto* buttonRaw = button.get();
    panel.AddChild(std::move(button));

    panel.Paint(ctx, 0, 0);

    const ClipTrace trace = TraceClip(commands);
    EXPECT_EQ(trace.finalDepth, 0);
    EXPECT_EQ(trace.pushes.size(), 2);
    // 父 PushClip 先于子 PushClip（父绘制约束在子绘制前生效）
    EXPECT_NEAR(trace.pushes[0]->rect.x, 10.0f, kEps);    // Panel
    EXPECT_NEAR(trace.pushes[1]->rect.x, 15.0f, kEps);    // Button（10+5，宽 200 越界）
    EXPECT_TRUE(trace.pushIndices[0] < trace.pushIndices[1]);
}

// ── R1-S6：不可见控件不裁剪（Paint 提前 return，无 PushClip）──

void TestClipInvisibleNoPush()
{
    RecordingBackend backend;
    CommandBuffer commands;
    PaintContext ctx(commands, backend);

    Panel panel;
    panel.SetPosition(0, 0);
    panel.SetSize(100, 50);

    auto button = std::make_unique<Button>("Hidden");
    button->SetPosition(10, 10);
    button->SetSize(30, 20);
    button->SetVisible(false);   // 不可见
    panel.AddChild(std::move(button));

    panel.Paint(ctx, 0, 0);

    const ClipTrace trace = TraceClip(commands);
    EXPECT_EQ(trace.finalDepth, 0);
    EXPECT_EQ(trace.pushes.size(), 1);   // 只有 Panel 的 PushClip
    EXPECT_NEAR(trace.pushes[0]->rect.x, 0.0f, kEps);
}

// ── R1-S7：横向跟手——右越界（caretX > current + viewWidth → caretX - viewWidth）──

void TestClipHScrollRight()
{
    // ClampScrollOffsetX(0, 300, 100)：光标 300px 处，可视 0-100 → 滚到 200
    EXPECT_NEAR(TestableTextBox::ClampScrollOffsetX(0.0f, 300.0f, 100.0f), 200.0f, kEps);
    // 已在可视区内 → 保持
    EXPECT_NEAR(TestableTextBox::ClampScrollOffsetX(50.0f, 80.0f, 100.0f), 50.0f, kEps);
}

// ── R1-S8：横向左边界回卷（caretX < current → caretX，回 0）──

void TestClipHScrollLeft()
{
    // 光标移回行首附近：current=300 但 caretX=50 → 滚到 50
    EXPECT_NEAR(TestableTextBox::ClampScrollOffsetX(300.0f, 50.0f, 100.0f), 50.0f, kEps);
    // 光标在行首 → 0
    EXPECT_NEAR(TestableTextBox::ClampScrollOffsetX(200.0f, 0.0f, 100.0f), 0.0f, kEps);
    // 恒 ≥ 0（负数 caretX 视为 0——clamp 左界）
    EXPECT_NEAR(TestableTextBox::ClampScrollOffsetX(0.0f, -10.0f, 100.0f), 0.0f, kEps);
}

// ── R1-S9：点击定位无窗口兼容（scrollOffsetX=0 时与既有行为一致——行起始返回）──

void TestClipCaretIndexCompat()
{
    TestableTextBox box("abcd");
    box.SetSize(100, 30);
    // 无窗口（GetWindow()==nullptr）→ CaretIndexFromLineX 跳过测量返回行起始（既有契约）
    // scrollOffsetX 默认 0 → innerX = localX - inset + 0，不改变既有语义
    const size_t index = box.CaretIndexFromPosition(Point{ 10.0f, 10.0f });
    EXPECT_EQ(index, 0);   // 行 0 起始
    EXPECT_EQ(box.GetScrollOffsetX(), 0.0f);   // 无窗口不产生滚动（测量分支跳过）
}

// ── R1-S11：SetSize override 安全（无窗口——EnsureCaretVisible 测量分支跳过，调用链不崩）──

void TestClipResizeSafe()
{
    TestableTextBox box("abc");
    box.SetSize(200, 40);   // override：基类 + EnsureCaretVisible + Invalidate
    EXPECT_EQ(box.GetWidth(), 200);
    EXPECT_EQ(box.GetHeight(), 40);
    EXPECT_EQ(box.GetScrollOffsetX(), 0.0f);   // 无窗口：横向测量跳过，偏移不变
    box.SetSize(80, 30);    // 缩小再设——调用链安全
    EXPECT_EQ(box.GetWidth(), 80);
}

} // anonymous namespace

void ECDI::Test::RegisterClipTests()
{
    GetTestRegistry().Add("Clip.DepthSequence", &TestClipDepthSequence);
    GetTestRegistry().Add("Clip.RectAbsolute", &TestClipRectAbsolute);
    GetTestRegistry().Add("Clip.TextBoxFullLine", &TestClipTextBoxFullLine);
    GetTestRegistry().Add("Clip.SelectionNoClamp", &TestClipSelectionNoClamp);
    GetTestRegistry().Add("Clip.ChildOverflow", &TestClipChildOverflow);
    GetTestRegistry().Add("Clip.InvisibleNoPush", &TestClipInvisibleNoPush);
    GetTestRegistry().Add("Clip.HScrollRight", &TestClipHScrollRight);
    GetTestRegistry().Add("Clip.HScrollLeft", &TestClipHScrollLeft);
    GetTestRegistry().Add("Clip.CaretIndexCompat", &TestClipCaretIndexCompat);
    GetTestRegistry().Add("Clip.ResizeSafe", &TestClipResizeSafe);
}
