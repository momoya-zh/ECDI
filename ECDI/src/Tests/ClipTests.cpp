#include "RunAllTests.h"
#include "TestFramework.h"
#include "ECDI/Render/PaintContext.h"
#include "Render/RecordingBackend.h"
#include "ECDI/Render/RenderCommand.h"
#include "ECDI/Widget/Panel.h"
#include "ECDI/Widget/Button.h"
#include "ECDI/Widget/Label.h"
#include "ECDI/Widget/TextBox.h"
#include "ECDI/Widget/Widget.h"
#include "ECDI/Widget/ScrollView.h"
#include "ECDI/Widget/ScrollBar.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyDownEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyModifier.h"
#include "ECDI/Core/Point.h"
#include "ECDI/Core/Color.h"
#include "ECDI/Core/Size.h"
#include <memory>
#include <utility>
#include <string>
#include <vector>

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

// ── T27-3：边界接触 = 不相交（剔除）——x / y 向对称各一（初设 Q5 严格 >0）──

void TestCullingBoundaryTouch()
{
    CommandBuffer commands;
    RecordingBackend backend;
    // 种子 = 视口 [150..250]×[0..100]
    PaintContext ctx(commands, backend, Rect{ 150.0f, 0.0f, 100.0f, 100.0f });

    // x 向接触：rect [100..150]×[0..100] 与视口交集宽 = 0 ⇒ 剔除
    EXPECT_FALSE(ctx.IsRectVisible(Rect{ 100.0f, 0.0f, 50.0f, 100.0f }));
    // y 向接触：视口 [0..100]×[150..250]，rect [0..100]×[100..150] 交集高 = 0 ⇒ 剔除
    PaintContext ctxY(commands, backend, Rect{ 0.0f, 150.0f, 100.0f, 100.0f });
    EXPECT_FALSE(ctxY.IsRectVisible(Rect{ 0.0f, 100.0f, 100.0f, 50.0f }));
    // 对照：部分相交（交集 10×100 > 0）⇒ 可见
    EXPECT_TRUE(ctx.IsRectVisible(Rect{ 100.0f, 0.0f, 60.0f, 100.0f }));
    // 对照：完全在内 ⇒ 可见
    EXPECT_TRUE(ctx.IsRectVisible(Rect{ 160.0f, 10.0f, 20.0f, 20.0f }));
}

// ── T27-4：部分相交 = 照常可见（ChildOverflow 几何 95×20——金丝雀锚点）──

void TestCullingPartialIntersection()
{
    CommandBuffer commands;
    RecordingBackend backend;
    // 种子 = Clip.ChildOverflow 的父裁剪：Panel(10,20,100,50)
    PaintContext ctx(commands, backend, Rect{ 10.0f, 20.0f, 100.0f, 50.0f });

    // 子 = Button pos(5,5) size(200,20) → 绝对 (15,25,200,20)：交集 95×20 > 0 ⇒ 可见
    EXPECT_TRUE(ctx.IsRectVisible(Rect{ 15.0f, 25.0f, 200.0f, 20.0f }));
    // 对照：完全越界 ⇒ 剔除
    EXPECT_FALSE(ctx.IsRectVisible(Rect{ 200.0f, 25.0f, 50.0f, 20.0f }));
    // 对照：接触型越界（rect 左缘 = 裁剪右缘 110，交集宽 = 0）⇒ 剔除
    EXPECT_FALSE(ctx.IsRectVisible(Rect{ 110.0f, 25.0f, 50.0f, 20.0f }));
}

/// @brief 两命令是否逐位一致（T27-11：kind + PushClip/DrawRect 几何 + DrawText 文本/位置/色）
/// @details RenderCommand 的聚合成员无 operator==——逐字段比对；
/// 未列 kinds（Line/RoundedRect/Image/FocusRect/PopClip）只比 kind——
/// T27-11 的树（Panel/Button 路径）不产生它们。
bool SameCommand(const RenderCommand& a, const RenderCommand& b)
{
    if (a.index() != b.index())
        return false;
    if (const auto* pa = std::get_if<PushClipCommand>(&a)){
        const auto* pb = std::get_if<PushClipCommand>(&b);
        return pa->rect.x == pb->rect.x && pa->rect.y == pb->rect.y
            && pa->rect.width == pb->rect.width && pa->rect.height == pb->rect.height;
    }
    if (const auto* pa = std::get_if<DrawRectCommand>(&a)){
        const auto* pb = std::get_if<DrawRectCommand>(&b);
        return pa->rect.x == pb->rect.x && pa->rect.y == pb->rect.y
            && pa->rect.width == pb->rect.width && pa->rect.height == pb->rect.height
            && pa->color.r == pb->color.r && pa->color.g == pb->color.g
            && pa->color.b == pb->color.b && pa->color.a == pb->color.a;
    }
    if (const auto* pa = std::get_if<DrawTextCommand>(&a)){
        const auto* pb = std::get_if<DrawTextCommand>(&b);
        return pa->text == pb->text && pa->pos.x == pb->pos.x && pa->pos.y == pb->pos.y
            && pa->color.r == pb->color.r && pa->color.g == pb->color.g
            && pa->color.b == pb->color.b && pa->color.a == pb->color.a;
    }
    return true;   // 其余 kinds：只比 kind（本用例的树不产生）
}

// ── T27-11：构造期开关关闭 ⇒ 命令流与既有基线逐位一致（C27-6）──

void TestCullingDisabledStreamIdentical()
{
    // 同树双涂：双参（无界基线）vs 四参 enableCulling=false（client 种子）
    auto PaintTree = [](PaintContext& ctx){
        Panel root;
        root.SetPosition(0, 0);
        root.SetSize(200, 200);
        auto panel = std::make_unique<Panel>();
        panel->SetPosition(10, 10);
        panel->SetSize(100, 50);
        auto* panelRaw = panel.get();
        auto button = std::make_unique<Button>("OK");
        button->SetPosition(5, 5);
        button->SetSize(40, 20);
        panelRaw->AddChild(std::move(button));
        root.AddChild(std::move(panel));
        root.Paint(ctx, 0, 0);
    };

    RecordingBackend backend;
    CommandBuffer baseCommands;
    PaintContext baseCtx(baseCommands, backend);
    PaintTree(baseCtx);

    CommandBuffer offCommands;
    PaintContext offCtx(offCommands, backend, Rect{ 0.0f, 0.0f, 200.0f, 200.0f }, false);
    PaintTree(offCtx);

    EXPECT_EQ(offCommands.size(), baseCommands.size());
    for (size_t i = 0; i < baseCommands.size(); ++i)
        EXPECT_TRUE(SameCommand(baseCommands[i], offCommands[i]));
}

// ── T27-12：双参（无界）vs 四参（client 种子）——超界根子树的判定分野（D27-A 镜像栈语义）──

void TestCullingUnboundedVsSeeded()
{
    CommandBuffer commands;
    RecordingBackend backend;

    // 根子树超出 client 种子：Root(0,0,300,300) ⊃ client(0,0,200,200)
    // 子 = local(250,250,40,40) → 绝对 (250,250)：种子内无交集
    const Rect rootRect{ 0.0f, 0.0f, 300.0f, 300.0f };
    const Rect childRect{ 250.0f, 250.0f, 40.0f, 40.0f };

    // 双参（无界）：PushClip(Root) 后栈顶 = Root ⇒ 子与 Root 相交（50×50）⇒ 可见（不剔除）
    PaintContext unbounded(commands, backend);
    unbounded.PushClip(rootRect);
    EXPECT_TRUE(unbounded.IsRectVisible(childRect));
    unbounded.PopClip();
    // 无界 ctx：Pop 后栈空 = 无界 ⇒ 任何 rect 可见（空栈分支）
    EXPECT_TRUE(unbounded.IsRectVisible(childRect));

    // 四参（client 种子）：PushClip(Root) 后栈顶 = 种子 ∩ Root = (0,0,200,200) ⇒ 子无交集 ⇒ 剔除
    PaintContext seeded(commands, backend, Rect{ 0.0f, 0.0f, 200.0f, 200.0f });
    seeded.PushClip(rootRect);
    EXPECT_FALSE(seeded.IsRectVisible(childRect));
    // 对照：种子内的子 ⇒ 可见
    EXPECT_TRUE(seeded.IsRectVisible(Rect{ 100.0f, 100.0f, 40.0f, 40.0f }));
    seeded.PopClip();
    // Pop 后栈顶回到种子本身——子仍与种子无交集 ⇒ 依旧剔除
    EXPECT_FALSE(seeded.IsRectVisible(childRect));
    // 多调一次 Pop：守卫保证种子不被弹出 ⇒ 判定不受影响（镜像栈与命令栈同步失衡而非崩溃）
    seeded.PopClip();
    EXPECT_FALSE(seeded.IsRectVisible(childRect));
}

// ── 批二（Widget/Window 接入）用例公用装置 ──────────────────────────

/// @brief 颜色逐位相等（T27 套件按色检索命令的统一判据）
bool SameColor(const Color& a, const Color& b) noexcept
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

/// @brief 命令流中指定背景色的 DrawRect（按色检索，避开索引脆弱性）
const DrawRectCommand* FindRectOfColor(const CommandBuffer& commands, const Color& color)
{
    for (const auto& cmd : commands)
        if (const auto* rect = std::get_if<DrawRectCommand>(&cmd))
            if (SameColor(rect->color, color))
                return rect;
    return nullptr;
}

/// @brief 命令流中是否存在指定文本的 DrawText（T27-7）
bool HasDrawText(const CommandBuffer& commands, const std::string& text)
{
    for (const auto& cmd : commands)
        if (const auto* draw = std::get_if<DrawTextCommand>(&cmd))
            if (draw->text == text)
                return true;
    return false;
}

/// @brief D27-D 精确测试树（T27-1/2/5 共用）
/// @details 绝对坐标：Root(0,0,200,200) ⊃ Parent(150,50,100,100) ⊃
///   ChildA local(childAX,childAY) —— (60,60) 时绝对 (210,110)：与 Parent 裁剪交集为空 ⇒ 整段剔除；
///   Deep（ChildA 内 local(5,5)）随 ChildA 一起消失（连遍历都不进入）；
///   ChildB local(30,30) → 绝对 (180,80,20,20)：交集 20×20 > 0 ⇒ 保留。
/// @param root 根（裸 Widget：自身不发 DrawRect，只发 PushClip/PopClip）
void BuildCullingTree(Widget& root, int childAX, int childAY)
{
    auto parent = std::make_unique<Panel>();
    parent->SetPosition(150, 50);
    parent->SetSize(100, 100);
    parent->SetStyle(PanelStyleOverride{ .background = Color::FromRGBA8(1, 2, 3, 255), .cornerRadius = 0.0f, .borderWidth = 0.0f });
    auto* parentRaw = parent.get();

    auto childA = std::make_unique<Panel>();
    childA->SetPosition(childAX, childAY);
    childA->SetSize(20, 20);
    childA->SetStyle(PanelStyleOverride{ .background = Color::FromRGBA8(200, 1, 1, 255), .cornerRadius = 0.0f, .borderWidth = 0.0f });
    auto* childARaw = childA.get();

    auto deep = std::make_unique<Label>("DEEP");
    deep->SetPosition(5, 5);
    deep->SetSize(10, 10);
    childARaw->AddChild(std::move(deep));

    auto childB = std::make_unique<Panel>();
    childB->SetPosition(30, 30);
    childB->SetSize(20, 20);
    childB->SetStyle(PanelStyleOverride{ .background = Color::FromRGBA8(1, 200, 1, 255), .cornerRadius = 0.0f, .borderWidth = 0.0f });

    parentRaw->AddChild(std::move(childA));
    parentRaw->AddChild(std::move(childB));
    root.AddChild(std::move(parent));
}

/// @brief 行背景色（T27-6/7——命令流里按色检索，避开索引脆弱性）
constexpr Color kCullingRowColor() noexcept{ return Color::FromRGBA8(7, 8, 9, 255); }

/// @brief 往父节点加一行带背景 Panel（T27-6/7——圆角边框清零，只发一条 DrawRect）
Panel* AddCullingRow(Widget& parent, int width, int height, int x, int y)
{
    auto row = std::make_unique<Panel>();
    row->SetSize(width, height);
    row->SetPosition(x, y);
    row->SetStyle(PanelStyleOverride{ .background = kCullingRowColor(), .cornerRadius = 0.0f, .borderWidth = 0.0f });
    Panel* raw = row.get();
    parent.AddChild(std::move(row));
    return raw;
}

/// @brief n 行文本（T27-7——"line<i>" 换行分隔）
std::string MakeCullingLines(int n)
{
    std::string text;
    for (int i = 0; i < n; ++i){
        if (i > 0)
            text += '\n';
        text += "line";
        text += std::to_string(i);
    }
    return text;
}

/// @brief OnPaint 计数 Widget（T27-9——退化矩形不得进入 OnPaint）
class CountingWidget final: public Widget{
public:
    using Widget::Widget;
    void OnPaint(PaintContext&, int, int) override{ ++paintCount; }
    int paintCount = 0;
};

// ── T27-1：全可见树 ⇒ 命令流与剔除前逐位一致（双参无界 vs 四参大种子）──

void TestCullingFullyVisibleTreeUnchanged()
{
    // 全在界内树（ChildA local(20,20) → 绝对 (170,70)）：任何构造下都不触发剔除
    auto PaintTree = [](PaintContext& ctx){
        Widget root;
        root.SetPosition(0, 0);
        root.SetSize(200, 200);
        BuildCullingTree(root, 20, 20);
        root.Paint(ctx, 0, 0);
    };

    RecordingBackend backend;
    CommandBuffer baseCommands;
    PaintContext baseCtx(baseCommands, backend);
    PaintTree(baseCtx);

    // 种子 (0,0,400,400) 覆盖全树 ⇒ 一切可见 ⇒ 与无界基线逐位一致
    CommandBuffer seededCommands;
    PaintContext seededCtx(seededCommands, backend, Rect{ 0.0f, 0.0f, 400.0f, 400.0f });
    PaintTree(seededCtx);

    EXPECT_EQ(seededCommands.size(), baseCommands.size());
    for (size_t i = 0; i < baseCommands.size(); ++i)
        EXPECT_TRUE(SameCommand(baseCommands[i], seededCommands[i]));
}

// ── T27-2：视口外子树整段零命令（PushClip/Draw*/PopClip 全 0——A1 整段验证）──

void TestCullingOutOfViewportSubtreeZeroCommands()
{
    RecordingBackend backend;
    CommandBuffer commands;
    PaintContext ctx(commands, backend, Rect{ 0.0f, 0.0f, 200.0f, 200.0f });   // client 种子

    Widget root;
    root.SetPosition(0, 0);
    root.SetSize(200, 200);
    BuildCullingTree(root, 60, 60);   // ChildA → 绝对 (210,110)：视口外
    root.Paint(ctx, 0, 0);

    // PushClip 恰好 3 个（Root/Parent/ChildB）——ChildA 分支连 Push 都没有
    const ClipTrace trace = TraceClip(commands);
    EXPECT_EQ(trace.pushes.size(), 3);
    EXPECT_EQ(trace.finalDepth, 0);
    EXPECT_NEAR(trace.pushes[0]->rect.x, 0.0f, kEps);     // Root
    EXPECT_NEAR(trace.pushes[1]->rect.x, 150.0f, kEps);   // Parent
    EXPECT_NEAR(trace.pushes[2]->rect.x, 180.0f, kEps);   // ChildB（150+30）

    // ChildA 标志色与 Deep 文本均不存在于命令流（整段零命令）
    EXPECT_TRUE(FindRectOfColor(commands, Color::FromRGBA8(200, 1, 1, 255)) == nullptr);
    EXPECT_FALSE(HasDrawText(commands, "DEEP"));
}

// ── T27-5：subtree pruning 全量结构断言（A 连同 Deep 整段消失、B 保留）──

void TestCullingSubtreePruningTree()
{
    RecordingBackend backend;
    CommandBuffer commands;
    PaintContext ctx(commands, backend, Rect{ 0.0f, 0.0f, 200.0f, 200.0f });

    Widget root;
    root.SetPosition(0, 0);
    root.SetSize(200, 200);
    BuildCullingTree(root, 60, 60);
    root.Paint(ctx, 0, 0);

    // 保留支：Parent 背景在 (150,50,100,100)、ChildB 背景在绝对 (180,80,20,20)
    const DrawRectCommand* parent = FindRectOfColor(commands, Color::FromRGBA8(1, 2, 3, 255));
    EXPECT_TRUE(parent != nullptr);
    if (parent != nullptr){
        EXPECT_NEAR(parent->rect.x, 150.0f, kEps);
        EXPECT_NEAR(parent->rect.y, 50.0f, kEps);
    }
    const DrawRectCommand* childB = FindRectOfColor(commands, Color::FromRGBA8(1, 200, 1, 255));
    EXPECT_TRUE(childB != nullptr);
    if (childB != nullptr){
        EXPECT_NEAR(childB->rect.x, 180.0f, kEps);
        EXPECT_NEAR(childB->rect.y, 80.0f, kEps);
        EXPECT_NEAR(childB->rect.width, 20.0f, kEps);
        EXPECT_NEAR(childB->rect.height, 20.0f, kEps);
    }

    // 剔除支：ChildA 标志色 / Deep 文本 / ChildA 位置 (210,110) 的 PushClip 均不存在
    EXPECT_TRUE(FindRectOfColor(commands, Color::FromRGBA8(200, 1, 1, 255)) == nullptr);
    EXPECT_FALSE(HasDrawText(commands, "DEEP"));
    bool pushAtChildA = false;
    for (const auto* push : TraceClip(commands).pushes)
        if (std::abs(push->rect.x - 210.0f) <= kEps && std::abs(push->rect.y - 110.0f) <= kEps)
            pushAtChildA = true;
    EXPECT_FALSE(pushAtChildA);

    // PopClip 与 PushClip 严格配对（整段剔除不破缺配对）
    EXPECT_EQ(TraceClip(commands).finalDepth, 0);
}

// ── T27-6：ScrollView offset——6 行 offset 80：row0..3 整段剔除（row3 边界接触内嵌）──

void TestCullingScrollViewRows()
{
    ScrollView sv;
    sv.SetSize(200, 100);
    for (int i = 0; i < 6; ++i)
        AddCullingRow(sv.GetContentView(), 180, 20, 0, i * 20);   // content y = 0,20,…,100
    sv.SetContentExtent(180, 400);
    sv.SetContentOffset(0, 80);   // row_i 绝对 y = i*20 − 80

    RecordingBackend backend;
    CommandBuffer commands;
    PaintContext ctx(commands, backend, Rect{ 0.0f, 0.0f, 200.0f, 100.0f });   // client 种子 = 视口
    sv.Paint(ctx, 0, 0);

    // 可见行恰好 2 个（row4 [0,20] / row5 [20,40]）——按行背景标志色检索
    // （row3 [−20,0] 与视口边界接触：交集高 = 0 ⇒ 剔除——严格 >0 语义在真实滚动场景的复现）
    std::vector<const DrawRectCommand*> rows;
    for (const auto& cmd : commands)
        if (const auto* rect = std::get_if<DrawRectCommand>(&cmd))
            if (SameColor(rect->color, kCullingRowColor()))
                rows.push_back(rect);
    EXPECT_EQ(rows.size(), 2);
    if (rows.size() == 2){
        EXPECT_NEAR(rows[0]->rect.y, 0.0f, kEps);    // row4：80 − 80
        EXPECT_NEAR(rows[1]->rect.y, 20.0f, kEps);   // row5：100 − 80
    }
}

// ── T27-7：滚出滚回 + 副作用补执行（延迟执行 ≠ 丢失——D3 底线的运行时证明）──

void TestCullingScrollOutAndBack()
{
    ScrollView sv;
    sv.SetSize(200, 100);
    // 行 4 换成 TextBox（180×20）：多行文本 ⇒ SyncScrollBar 惰性账
    auto box = std::make_unique<TextBox>(MakeCullingLines(10));   // 10 行 × 16 = 160 > 20 ⇒ 溢出
    box->SetPosition(0, 80);
    box->SetSize(180, 20);
    auto* boxRaw = box.get();
    sv.GetContentView().AddChild(std::move(box));
    sv.SetContentExtent(180, 400);

    ScrollBar* bar = boxRaw->GetVerticalScrollBar();
    EXPECT_TRUE(bar->IsVisible());   // 前置：SetSize 已惰性同步（10 行 × 16 = 160 > 20 ⇒ 溢出 ⇒ 条自始可见）

    RecordingBackend backend;

    // ① 滚入视口（offset 80 ⇒ TextBox 绝对 y=0）：OnPaint 执行 ⇒ C-2 守卫同值早退（条态不变）+ 文本上屏
    sv.SetContentOffset(0, 80);
    {
        CommandBuffer commands;
        PaintContext ctx(commands, backend, Rect{ 0.0f, 0.0f, 200.0f, 100.0f });
        sv.Paint(ctx, 0, 0);
        EXPECT_TRUE(bar->IsVisible());
        EXPECT_TRUE(HasDrawText(commands, "line0"));
    }

    // ② 滚出（offset 300 ⇒ TextBox 绝对 y=−220）：整段剔除 ⇒ OnPaint 未执行 ⇒ 账未还
    //   （条保持旧态可见——新文本 1 行其实已不溢出：剔除期间副作用不补执行）
    sv.SetContentOffset(0, 300);
    boxRaw->SetText("one");   // 再置一次账：行缓存失效，条不同步
    {
        CommandBuffer commands;
        PaintContext ctx(commands, backend, Rect{ 0.0f, 0.0f, 200.0f, 100.0f });
        sv.Paint(ctx, 0, 0);
        EXPECT_TRUE(bar->IsVisible());                 // 旧态保持（OnPaint 未执行 ⇒ 未同步）
        EXPECT_FALSE(HasDrawText(commands, "one"));    // TextBox 分支整段 0
    }

    // ③ 滚回（offset 80）：SetText 后首帧 OnPaint ⇒ 账补齐 ⇒ 条翻回隐藏（1 行 × 16 = 16 ≤ 20 不溢出）
    sv.SetContentOffset(0, 80);
    {
        CommandBuffer commands;
        PaintContext ctx(commands, backend, Rect{ 0.0f, 0.0f, 200.0f, 100.0f });
        sv.Paint(ctx, 0, 0);
        EXPECT_FALSE(bar->IsVisible());               // 惰性同步补执行
        EXPECT_TRUE(HasDrawText(commands, "one"));    // 首帧恢复
    }
}

// ── T27-8：C-VIS 正交——隐藏子（停泊 (−w,−h)）与「根内视口外」可见子并存──

void TestCullingHiddenAndOutOfViewportCoexist()
{
    // 根 (0,0,300,300) 超出 client 种子 (0,0,200,200)：
    // 视口外子 (250,250,40,20) 在根内、种子外 ⇒ 双参不剔除 / 四参剔除
    auto PaintTree = [](PaintContext& ctx){
        Panel root;
        root.SetPosition(0, 0);
        root.SetSize(300, 300);
        root.SetStyle(PanelStyleOverride{ .background = Color::FromRGBA8(1, 2, 3, 255), .cornerRadius = 0.0f, .borderWidth = 0.0f });

        // 隐藏子：C-VIS 停泊位（自身 bbox 负区）——Phase 25 语义不变（IsVisible 早退，不 Push 不 Pop）
        auto hidden = std::make_unique<Panel>();
        hidden->SetPosition(-30, -30);
        hidden->SetSize(30, 20);
        hidden->SetVisible(false);
        hidden->SetStyle(PanelStyleOverride{ .background = Color::FromRGBA8(9, 9, 9, 255), .cornerRadius = 0.0f, .borderWidth = 0.0f });
        root.AddChild(std::move(hidden));

        // 可见但视口外子（根内、种子外）
        auto outside = std::make_unique<Panel>();
        outside->SetPosition(250, 250);
        outside->SetSize(40, 20);
        outside->SetStyle(PanelStyleOverride{ .background = Color::FromRGBA8(8, 8, 8, 255), .cornerRadius = 0.0f, .borderWidth = 0.0f });
        root.AddChild(std::move(outside));

        root.Paint(ctx, 0, 0);
    };

    RecordingBackend backend;
    // 双参（无界）：隐藏子不画；视口外子在根内 ⇒ 照画 ⇒ 2 次 PushClip
    CommandBuffer baseCommands;
    PaintContext baseCtx(baseCommands, backend);
    PaintTree(baseCtx);
    const ClipTrace baseTrace = TraceClip(baseCommands);
    EXPECT_EQ(baseTrace.pushes.size(), 2);   // Root + 视口外子（隐藏子无 Push）
    EXPECT_TRUE(FindRectOfColor(baseCommands, Color::FromRGBA8(8, 8, 8, 255)) != nullptr);
    EXPECT_TRUE(FindRectOfColor(baseCommands, Color::FromRGBA8(9, 9, 9, 255)) == nullptr);   // 隐藏子恒无输出

    // 四参（client 种子 = 视口）：视口外子被新判据剔除 ⇒ 仅 Root 的 1 次 PushClip
    CommandBuffer seededCommands;
    PaintContext seededCtx(seededCommands, backend, Rect{ 0.0f, 0.0f, 200.0f, 200.0f });
    PaintTree(seededCtx);
    const ClipTrace seededTrace = TraceClip(seededCommands);
    EXPECT_EQ(seededTrace.pushes.size(), 1);
    EXPECT_EQ(seededTrace.finalDepth, 0);
    EXPECT_TRUE(FindRectOfColor(seededCommands, Color::FromRGBA8(8, 8, 8, 255)) == nullptr);   // 新判据剔除
    EXPECT_TRUE(FindRectOfColor(seededCommands, Color::FromRGBA8(9, 9, 9, 255)) == nullptr);   // C-VIS 不变
}

// ── T27-9：退化矩形（0 宽 / 0 高 widget）恒剔除且不进 OnPaint（D27-3）──

void TestCullingDegenerateRectsAlwaysCulled()
{
    RecordingBackend backend;

    auto PaintTree = [](PaintContext& ctx, int& zwCount, int& zhCount){
        Panel root;
        root.SetPosition(0, 0);
        root.SetSize(200, 200);
        root.SetStyle(PanelStyleOverride{ .background = Color::FromRGBA8(1, 2, 3, 255), .cornerRadius = 0.0f, .borderWidth = 0.0f });

        auto zw = std::make_unique<CountingWidget>();
        zw->SetPosition(10, 10);
        zw->SetSize(0, 20);   // 0 宽：交集宽 = 0 ⇒ 恒剔除
        auto* zwRaw = zw.get();
        root.AddChild(std::move(zw));

        auto zh = std::make_unique<CountingWidget>();
        zh->SetPosition(10, 40);
        zh->SetSize(20, 0);   // 0 高：交集高 = 0 ⇒ 恒剔除
        auto* zhRaw = zh.get();
        root.AddChild(std::move(zh));

        root.Paint(ctx, 0, 0);
        zwCount = zwRaw->paintCount;   // 在树析构前读数
        zhCount = zhRaw->paintCount;
    };

    // 双参（无界）与四参（client 种子）：退化矩形恒剔除（D27-3，与构造无关）
    {
        CommandBuffer commands;
        PaintContext ctx(commands, backend);
        int zwCount = -1, zhCount = -1;
        PaintTree(ctx, zwCount, zhCount);
        EXPECT_EQ(zwCount, 0);
        EXPECT_EQ(zhCount, 0);
        EXPECT_EQ(TraceClip(commands).pushes.size(), 1);   // 仅 Root
    }
    {
        CommandBuffer commands;
        PaintContext ctx(commands, backend, Rect{ 0.0f, 0.0f, 200.0f, 200.0f });
        int zwCount = -1, zhCount = -1;
        PaintTree(ctx, zwCount, zhCount);
        EXPECT_EQ(zwCount, 0);
        EXPECT_EQ(zhCount, 0);
        EXPECT_EQ(TraceClip(commands).pushes.size(), 1);   // 仅 Root
    }
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
    GetTestRegistry().Add("Culling.BoundaryTouch", &TestCullingBoundaryTouch);
    GetTestRegistry().Add("Culling.PartialIntersection", &TestCullingPartialIntersection);
    GetTestRegistry().Add("Culling.DisabledStreamIdentical", &TestCullingDisabledStreamIdentical);
    GetTestRegistry().Add("Culling.UnboundedVsSeeded", &TestCullingUnboundedVsSeeded);
    GetTestRegistry().Add("Culling.FullyVisibleTreeUnchanged", &TestCullingFullyVisibleTreeUnchanged);
    GetTestRegistry().Add("Culling.OutOfViewportSubtreeZeroCommands", &TestCullingOutOfViewportSubtreeZeroCommands);
    GetTestRegistry().Add("Culling.SubtreePruningTree", &TestCullingSubtreePruningTree);
    GetTestRegistry().Add("Culling.ScrollViewRows", &TestCullingScrollViewRows);
    GetTestRegistry().Add("Culling.ScrollOutAndBack", &TestCullingScrollOutAndBack);
    GetTestRegistry().Add("Culling.HiddenAndOutOfViewportCoexist", &TestCullingHiddenAndOutOfViewportCoexist);
    GetTestRegistry().Add("Culling.DegenerateRectsAlwaysCulled", &TestCullingDegenerateRectsAlwaysCulled);
}
