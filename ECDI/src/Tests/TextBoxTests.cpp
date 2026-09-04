#include "RunAllTests.h"
#include "TestFramework.h"
#include "ECDI/Widget/TextBox.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyDownEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyModifier.h"
#include "ECDI/EventSystem/Window/TimerEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseWheelEvent.h"
#include "ECDI/Render/PaintContext.h"
#include "ECDI/Render/RecordingBackend.h"
#include "ECDI/Render/RenderCommand.h"
#include "ECDI/Render/TextMeasurer.h"

using namespace ECDI;

namespace {

constexpr float kEps = 0.001f;   // EXPECT_NEAR 精度（同 WidgetTests/ProgressBarTests）

/// @brief 可测 TextBox：暴露 protected 键盘选择路径（无窗口安全——SyncTextInputCaret 有 Window 防御）
/// @note CaretIndexFromX 为 private（点击定位算法），派生类不可访问——算法直测
/// 留待最小窗口集成测试（与拖选事件流同归；不为此改框架可见性——P0 边界修正，v0.2 实现记录）。
class FakeTextMeasurer : public TextMeasurer   // 9.8：确定性测量（每码点 8 宽/行高 16——与 WidgetTests 同构）
{
public:
	Size MeasureText(const Font&, const std::string& text) override{
		size_t count = 0;
		for (size_t i = 0; i < text.size(); ++i)
			if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80)
				++count;
		return Size{ static_cast<float>(count) * 8.0f, 16.0f };
	}
	float LineHeight(const Font&) override{ return 16.0f; }
};

class TestableTextBox : public TextBox
{
public:
    using TextBox::TextBox;
    using TextBox::OnKeyDown;   ///< 暴露键盘选择路径（Shift+方向/Home/End）
    using TextBox::OnTimer;     ///< 8.5.1：光标闪烁（protected override——无窗口环境允许验证翻转逻辑）
    using TextBox::UpdateComposition;   ///< 8.5.1：Composition 状态机（protected——Window 转发入口）
    using TextBox::CommitComposition;
    using TextBox::CancelComposition;
    using TextBox::RecalculateLines;   ///< 8.5.2：行缓存重算（private——测试经 using 暴露）
    using TextBox::LineRange;          ///< 8.5.2：行区间查询（private——推断行分割）
    using TextBox::CaretIndexFromPosition;   ///< 8.5.2：多行坐标定位（private——Y 定行无窗口可测）
    using TextBox::GetWordBounds;      ///< 8.5.2：双击选词（private——纯码点逻辑）
    using TextBox::OnMouseWheel;       ///< 8.5.2：滚轮（protected override）
protected:
    TextMeasurer* ResolveMeasurer() const override{ return &ms_fake; }   // 9.8：preferred 测量接缝
private:
    static FakeTextMeasurer ms_fake;
};

FakeTextMeasurer TestableTextBox::ms_fake;

void TestTextBoxInsertDelete()
{
    // ── 原 #7：TextBox 编辑逻辑（迁移 + 扩展）──

    // InsertCodepoint 末尾
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.InsertCodepoint(U'd');
        EXPECT_EQ(box.GetText(), "abcd");
        EXPECT_EQ(box.GetCaret(), 4);
    }
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.InsertCodepoint(U'X');
        EXPECT_EQ(box.GetText(), "abcX");
        EXPECT_EQ(box.GetCaret(), 4);
    }

    // InsertCodepoint 开头
    {
        TextBox box("abc");
        box.MoveCaretToStart();
        box.InsertCodepoint(U'X');
        EXPECT_EQ(box.GetText(), "Xabc");
        EXPECT_EQ(box.GetCaret(), 1);
    }

    // InsertCodepoint 中间
    {
        TextBox box("abc");
        box.MoveCaretToStart();
        box.MoveCaret(TextBox::CaretDirection::Right);
        box.InsertCodepoint(U'X');
        EXPECT_EQ(box.GetText(), "aXbc");
        EXPECT_EQ(box.GetCaret(), 2);
    }

    // DeleteBackward 末尾
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.DeleteBackward();
        EXPECT_EQ(box.GetText(), "ab");
        EXPECT_EQ(box.GetCaret(), 2);
    }

    // DeleteBackward 中间
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.MoveCaret(TextBox::CaretDirection::Left);
        box.DeleteBackward();
        EXPECT_EQ(box.GetText(), "ac");
        EXPECT_EQ(box.GetCaret(), 1);
    }

    // DeleteBackward 头边界
    {
        TextBox box("abc");
        box.MoveCaretToStart();
        box.DeleteBackward();
        EXPECT_EQ(box.GetText(), "abc");
        EXPECT_EQ(box.GetCaret(), 0);
    }

    // DeleteForward 开头
    {
        TextBox box("abc");
        box.MoveCaretToStart();
        box.DeleteForward();
        EXPECT_EQ(box.GetText(), "bc");
        EXPECT_EQ(box.GetCaret(), 0);
    }

    // DeleteForward 中间
    {
        TextBox box("abc");
        box.MoveCaretToStart();
        box.MoveCaret(TextBox::CaretDirection::Right);
        box.DeleteForward();
        EXPECT_EQ(box.GetText(), "ac");
        EXPECT_EQ(box.GetCaret(), 1);
    }

    // DeleteForward 尾边界
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.DeleteForward();
        EXPECT_EQ(box.GetText(), "abc");
        EXPECT_EQ(box.GetCaret(), 3);
    }

    // emoji 不切字
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.InsertCodepoint(U'😀');
        EXPECT_EQ(box.GetText(), "abc😀");
        EXPECT_EQ(box.GetCaret(), 4);
        box.DeleteBackward();
        EXPECT_EQ(box.GetText(), "abc");
        EXPECT_EQ(box.GetCaret(), 3);
    }

    // 中文不切字
    {
        TextBox box("ab");
        box.MoveCaretToEnd();
        box.InsertCodepoint(U'中');
        EXPECT_EQ(box.GetText(), "ab中");
        EXPECT_EQ(box.GetCaret(), 3);
        box.DeleteBackward();
        EXPECT_EQ(box.GetText(), "ab");
        EXPECT_EQ(box.GetCaret(), 2);
    }
}

void TestTextBoxCaretMovement()
{
    // ── T2：光标迁移补测 ──

    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.MoveCaret(TextBox::CaretDirection::Left);
        EXPECT_EQ(box.GetCaret(), 2);
    }
    {
        TextBox box("abc");
        box.MoveCaretToStart();
        box.MoveCaret(TextBox::CaretDirection::Left);
        EXPECT_EQ(box.GetCaret(), 0);
    }
    {
        TextBox box("abc");
        box.MoveCaretToStart();
        box.MoveCaret(TextBox::CaretDirection::Right);
        EXPECT_EQ(box.GetCaret(), 1);
    }
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.MoveCaret(TextBox::CaretDirection::Right);
        EXPECT_EQ(box.GetCaret(), 3);
    }
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.MoveCaretToStart();
        box.MoveCaretToEnd();
        EXPECT_EQ(box.GetCaret(), 3);
    }
    // 中文/emoji 光标移动不切字
    {
        TextBox box("中a😀");
        box.MoveCaretToEnd();
        EXPECT_EQ(box.GetCaret(), 3);
        box.MoveCaret(TextBox::CaretDirection::Left);
        EXPECT_EQ(box.GetCaret(), 2);
        box.MoveCaret(TextBox::CaretDirection::Left);
        EXPECT_EQ(box.GetCaret(), 1);
        box.MoveCaret(TextBox::CaretDirection::Left);
        EXPECT_EQ(box.GetCaret(), 0);
    }
}

void TestTextBoxGetSelection()
{
    // ── T3：GetSelection 查询接口 ──

    {
        TextBox box("hello");
        auto sel = box.GetSelection();
        EXPECT_FALSE(sel.has_value());
    }
    {
        TextBox box("hello");
        box.InsertCodepoint(U'X');
        auto sel = box.GetSelection();
        EXPECT_FALSE(sel.has_value());
    }
    {
        TextBox box("hello");
        box.MoveCaretToEnd();
        auto sel = box.GetSelection();
        EXPECT_FALSE(sel.has_value());
        box.MoveCaret(TextBox::CaretDirection::Left);
        sel = box.GetSelection();
        EXPECT_FALSE(sel.has_value());
    }
    {
        TextBox box("hello");
        box.MoveCaretToEnd();
        box.DeleteBackward();
        auto sel = box.GetSelection();
        EXPECT_FALSE(sel.has_value());
    }
    {
        TextBox box("");
        auto sel = box.GetSelection();
        EXPECT_FALSE(sel.has_value());
    }
}

void TestTextBoxBoundary()
{
    // ── T4：TextBox 边界条件 ──

    {
        TextBox box("");
        box.InsertCodepoint(U'a');
        EXPECT_EQ(box.GetText(), "a");
        EXPECT_EQ(box.GetCaret(), 1);
    }
    {
        TextBox box("");
        box.DeleteBackward();
        EXPECT_EQ(box.GetText(), "");
        EXPECT_EQ(box.GetCaret(), 0);
    }
    {
        TextBox box("");
        box.DeleteForward();
        EXPECT_EQ(box.GetText(), "");
        EXPECT_EQ(box.GetCaret(), 0);
    }
    {
        TextBox box("中🎉a");
        box.MoveCaretToEnd();
        box.InsertCodepoint(U'X');
        EXPECT_EQ(box.GetText(), "中🎉aX");
        EXPECT_EQ(box.GetCaret(), 4);
    }
    {
        TextBox box("中🎉a");
        box.MoveCaretToEnd();
        box.DeleteBackward();
        EXPECT_EQ(box.GetText(), "中🎉");
        EXPECT_EQ(box.GetCaret(), 2);
    }
    {
        TextBox box("中🎉");
        box.MoveCaretToEnd();
        box.DeleteBackward();
        EXPECT_EQ(box.GetText(), "中");
        EXPECT_EQ(box.GetCaret(), 1);
    }
    {
        TextBox box("x");
        box.MoveCaretToEnd();
        box.DeleteBackward();
        EXPECT_EQ(box.GetText(), "");
        EXPECT_EQ(box.GetCaret(), 0);
    }
    {
        TextBox box("x");
        box.DeleteForward();
        EXPECT_EQ(box.GetText(), "");
        EXPECT_EQ(box.GetCaret(), 0);
    }
}

void TestTextBoxCallback()
{
    // ── 7.5：回调注册（D4 RaiseXxx 分离模式 / D7 仅编辑操作触发）──

    // TC1: InsertCodepoint 触发回调 + 新文本正确
    {
        TextBox box("abc");
        std::string lastText;
        box.SetOnTextChanged([&lastText](const std::string& text){ lastText = text; });
        box.MoveCaretToEnd();
        box.InsertCodepoint(U'd');
        EXPECT_EQ(box.GetText(), "abcd");
        EXPECT_EQ(lastText, "abcd");
    }

    // TC1b: DeleteBackward 触发回调（普通删除路径）
    {
        TextBox box("abc");
        int count = 0;
        box.SetOnTextChanged([&count](const std::string&){ ++count; });
        box.MoveCaretToEnd();
        box.DeleteBackward();
        EXPECT_EQ(box.GetText(), "ab");
        EXPECT_EQ(count, 1);
    }

    // TC1c: DeleteForward 触发回调
    {
        TextBox box("abc");
        int count = 0;
        box.SetOnTextChanged([&count](const std::string&){ ++count; });
        box.MoveCaretToStart();
        box.DeleteForward();
        EXPECT_EQ(box.GetText(), "bc");
        EXPECT_EQ(count, 1);
    }

    // TC2: DeleteBackward 头边界空操作 → 不触发（D7 边界语义）
    {
        TextBox box("abc");
        int count = 0;
        box.SetOnTextChanged([&count](const std::string&){ ++count; });
        box.MoveCaretToStart();
        box.DeleteBackward();
        EXPECT_EQ(count, 0);
    }

    // TC2b: DeleteForward 尾边界空操作 → 不触发
    {
        TextBox box("abc");
        int count = 0;
        box.SetOnTextChanged([&count](const std::string&){ ++count; });
        box.MoveCaretToEnd();
        box.DeleteForward();
        EXPECT_EQ(count, 0);
    }

    // TC3: SetText 不触发回调（D7 核心：程序设值不算用户修改）
    {
        TextBox box("abc");
        int count = 0;
        box.SetOnTextChanged([&count](const std::string&){ ++count; });
        box.SetText("xyz");
        EXPECT_EQ(box.GetText(), "xyz");
        EXPECT_EQ(count, 0);
    }

    // TC3b: MoveCaret 系列不触发回调（仅光标移动，文本未变）
    {
        TextBox box("abc");
        int count = 0;
        box.SetOnTextChanged([&count](const std::string&){ ++count; });
        box.MoveCaretToStart();
        box.MoveCaretToEnd();
        box.MoveCaret(TextBox::CaretDirection::Left);
        EXPECT_EQ(count, 0);
    }

    // TC3c: 空回调 + 编辑 → 不崩溃（空 std::function 安全性，D5）
    {
        TextBox box("abc");
        box.SetOnTextChanged({});
        box.MoveCaretToEnd();
        box.InsertCodepoint(U'd');
        EXPECT_EQ(box.GetText(), "abcd");
    }

    // TC4: override OnTextChanged 不吞回调（D4 核心语义）
    {
        class MyTextBox : public TextBox{
        public:
            bool hookCalled = false;   // 类成员变量——override 内可访问（hook 而非 base：未调用基类）

        protected:
            void OnTextChanged(const std::string&) override{
                hookCalled = true;
                // 不调 TextBox::OnTextChanged() —— 模拟"忘记调基类"
            }
        };

        MyTextBox box;
        bool callbackCalled = false;
        box.SetOnTextChanged([&callbackCalled](const std::string&){ callbackCalled = true; });
        box.InsertCodepoint(U'X');

        EXPECT_TRUE(box.hookCalled);     // 虚方法被调用
        EXPECT_TRUE(callbackCalled);     // 回调仍被调用（D4 核心收益：override 不吞回调）
    }
}

// ── P0：Selection 测试（7.2 补欠账——5.5.2 P8 承诺）──────────────
// 全部经键盘路径（OnKeyDown 暴露）：纯码点逻辑 + SyncTextInputCaret 有 Window 防御 → 无窗口可测。
// 索引单位 = 码点（TextBox.h SelectionRange 注释明确）；anchor 固定端 + caret active 端模型。

void TestTextBoxSelectionKeyboard()
{
    // S1: Shift+Right 扩展选择（anchor=2 起点——先无 Shift 移动设 anchor，再 Shift 扩张）
    {
        TestableTextBox box("abcd");
        box.MoveCaretToStart();                                  // caret=0, anchor=0
        box.MoveCaret(TextBox::CaretDirection::Right);           // caret=1, anchor=1
        box.MoveCaret(TextBox::CaretDirection::Right);           // caret=2, anchor=2（无选择）
        EXPECT_FALSE(box.GetSelection().has_value());

        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Right, KeyModifier::Shift));   // caret 2→3
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Right, KeyModifier::Shift));   // caret 3→4
        auto sel = box.GetSelection();
        EXPECT_TRUE(sel.has_value());
        EXPECT_EQ(sel->start, 2);   // anchor 固定
        EXPECT_EQ(sel->end, 4);     // active 端
        EXPECT_EQ(box.GetCaret(), 4);
    }

    // S2a: Shift+Left 收缩（{0,3} → {0,2}）
    {
        TestableTextBox box("abcd");
        box.MoveCaretToStart();
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Right, KeyModifier::Shift));   // 1
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Right, KeyModifier::Shift));   // 2
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Right, KeyModifier::Shift));   // 3 → {0,3}
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));    // 2 → {0,2}
        auto sel = box.GetSelection();
        EXPECT_TRUE(sel.has_value());
        EXPECT_EQ(sel->start, 0);
        EXPECT_EQ(sel->end, 2);
    }

    // S2b: Shift+Left 反向跨越（anchor=4，向左扩到整行 → {0,4}）
    {
        TestableTextBox box("abcd");
        box.MoveCaretToEnd();                                    // caret=4, anchor=4
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));   // caret 0
        auto sel = box.GetSelection();
        EXPECT_TRUE(sel.has_value());
        EXPECT_EQ(sel->start, 0);
        EXPECT_EQ(sel->end, 4);
        EXPECT_EQ(box.GetCaret(), 0);
    }

    // S3: Shift+Home/End 扩展到行首/行尾
    {
        TestableTextBox box("abc");
        box.MoveCaretToEnd();                                    // caret=3, anchor=3
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Home, KeyModifier::Shift));   // caret 0
        auto sel = box.GetSelection();
        EXPECT_TRUE(sel.has_value());
        EXPECT_EQ(sel->start, 0);
        EXPECT_EQ(sel->end, 3);
        EXPECT_EQ(box.GetCaret(), 0);
    }
    {
        TestableTextBox box("abc");
        box.MoveCaretToStart();                                  // caret=0, anchor=0
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::End, KeyModifier::Shift));    // caret 3
        auto sel = box.GetSelection();
        EXPECT_TRUE(sel.has_value());
        EXPECT_EQ(sel->start, 0);
        EXPECT_EQ(sel->end, 3);
        EXPECT_EQ(box.GetCaret(), 3);
    }

    // S4: 无 Shift 移动清除选择（先动后清——防幽灵选择）
    {
        TestableTextBox box("abcd");
        box.MoveCaretToStart();
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Right, KeyModifier::Shift));   // {0,1}
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Right, KeyModifier::Shift));   // {0,2}
        EXPECT_TRUE(box.GetSelection().has_value());

        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::None));     // caret 2→1 + 清选择
        EXPECT_FALSE(box.GetSelection().has_value());
        EXPECT_EQ(box.GetCaret(), 1);
    }

    // S7: 跨代理对选择（emoji 整体占 1 码点索引——不切半个代理对）
    {
        TestableTextBox box("a😀b");                            // 码点：a(0) 😀(1) b(2)
        EXPECT_EQ(box.GetCaret(), 0);
        box.MoveCaretToEnd();                                   // caret=3, anchor=3
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));    // caret 2
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));    // caret 1
        auto sel = box.GetSelection();
        EXPECT_TRUE(sel.has_value());
        EXPECT_EQ(sel->start, 1);   // 😀 整体（非 UTF-16 代理对拆半）
        EXPECT_EQ(sel->end, 3);
        EXPECT_EQ(box.GetCaret(), 1);

        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));    // caret 0
        sel = box.GetSelection();
        EXPECT_TRUE(sel.has_value());
        EXPECT_EQ(sel->start, 0);
        EXPECT_EQ(sel->end, 3);
    }

    // S8: 选区-光标一致性（向右扩张 caret==end；anchor 恒 0）
    {
        TestableTextBox box("abcde");
        box.MoveCaretToStart();
        for (int i = 0; i < 4; ++i)
        {
            box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Right, KeyModifier::Shift));
            auto sel = box.GetSelection();
            EXPECT_TRUE(sel.has_value());
            EXPECT_EQ(box.GetCaret(), sel->end);   // active 端 = caret
        }
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));    // 收缩
        auto sel = box.GetSelection();
        EXPECT_TRUE(sel.has_value());
        EXPECT_EQ(box.GetCaret(), sel->end);
    }

    // S10: 空文本/单字符边界（不崩 + 选区合法）
    {
        TestableTextBox box("");
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Right, KeyModifier::Shift));   // 0<0 → 不动
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));
        EXPECT_EQ(box.GetCaret(), 0);
        EXPECT_FALSE(box.GetSelection().has_value());
    }
    {
        TestableTextBox box("x");
        box.MoveCaretToEnd();                                   // caret=1, anchor=1
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));    // caret 0
        auto sel = box.GetSelection();
        EXPECT_TRUE(sel.has_value());
        EXPECT_EQ(sel->start, 0);
        EXPECT_EQ(sel->end, 1);
        EXPECT_EQ(box.GetCaret(), 0);
    }
}

void TestTextBoxSelectionEdit()
{
    // S5: InsertCodepoint 删选中区（编辑操作自包含——有 Selection 先删）
    {
        TestableTextBox box("abcd");
        box.MoveCaretToEnd();                                   // caret=4, anchor=4
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));    // 3
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));    // 2 → {2,4}
        box.InsertCodepoint(U'X');                              // 删 [2,4)="cd" → "ab" + X → "abX"
        EXPECT_EQ(box.GetText(), "abX");
        EXPECT_EQ(box.GetCaret(), 3);
        EXPECT_FALSE(box.GetSelection().has_value());
    }

    // S6a: DeleteBackward 删选中区（一次删除，非单字符）
    {
        TestableTextBox box("abcd");
        box.MoveCaretToEnd();
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));    // 3
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));    // 2 → {2,4}
        box.DeleteBackward();
        EXPECT_EQ(box.GetText(), "ab");
        EXPECT_EQ(box.GetCaret(), 2);   // 新光标 = 选区 min
        EXPECT_FALSE(box.GetSelection().has_value());
    }

    // S6b: DeleteForward 删选中区
    {
        TestableTextBox box("abcd");
        box.MoveCaretToStart();
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Right, KeyModifier::Shift));   // 1
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Right, KeyModifier::Shift));   // 2 → {0,2}
        box.DeleteForward();
        EXPECT_EQ(box.GetText(), "cd");
        EXPECT_EQ(box.GetCaret(), 0);
        EXPECT_FALSE(box.GetSelection().has_value());
    }
}

// ── 8.5.1：文本系统 2.0 测试（F1-F15）──────────────────────────
// 覆盖：InsertText 多码点 / Composition 模型 B 状态机（C7/C8/C12）/ Ctrl 组合（C1）/
// OnTimer 闪烁（C2）/ 中间替换（F14）/ code point 索引（F15）。

void TestTextBoxTextSystem2()
{
    // F1: InsertText 多码点插入 + 空串 no-op
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.InsertText("def");
        EXPECT_EQ(box.GetText(), "abcdef");
        EXPECT_EQ(box.GetCaret(), 6);
    }
    {
        TextBox box("abc");
        int count = 0;
        box.SetOnTextChanged([&count](const std::string&){ ++count; });
        box.MoveCaretToEnd();
        box.InsertText("");
        EXPECT_EQ(box.GetText(), "abc");
        EXPECT_EQ(count, 0);   // 空串 no-op（D7 边界语义）
    }
    // F2: InsertText 有 Selection 先删（粘贴覆盖选中区）
    {
        TestableTextBox box("abcd");
        box.MoveCaretToEnd();
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));    // 3
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Left, KeyModifier::Shift));    // 2 → {2,4}
        box.InsertText("XY");
        EXPECT_EQ(box.GetText(), "abXY");
        EXPECT_EQ(box.GetCaret(), 4);
        EXPECT_FALSE(box.GetSelection().has_value());
    }
    // F3: Composition 首帧占位（模型 B 起始）
    {
        TestableTextBox box("abc");
        box.MoveCaretToEnd();   // caret=3
        box.UpdateComposition("nihao");
        EXPECT_EQ(box.GetText(), "abcnihao");
        EXPECT_EQ(box.GetCaret(), 8);   // 3 + 5 码点
        // m_isComposing 无法直接断言（private）——经 F5/F7 行为验证
    }
    // F4: Composition 更新替换区间（非追加）
    {
        TestableTextBox box("abc");
        box.MoveCaretToEnd();
        box.UpdateComposition("nihao");
        box.UpdateComposition("nihao2");
        EXPECT_EQ(box.GetText(), "abcnihao2");   // 区间替换非追加
        EXPECT_EQ(box.GetCaret(), 9);
    }
    // F5: Composition 空串不结束（C7——空串 ≠ Commit）
    {
        TestableTextBox box("abc");
        box.MoveCaretToEnd();
        box.UpdateComposition("nihao");
        box.UpdateComposition("");   // 组合中无内容——组合仍在
        EXPECT_EQ(box.GetText(), "abc");   // 区间被清空（临时）
        box.UpdateComposition("nih");      // 组合继续 → 区间恢复
        EXPECT_EQ(box.GetText(), "abcnih");
    }
    // F6: Composition 期间不触发 TextChanged（C8——经 ReplaceTextRange 非 InsertText）
    {
        TestableTextBox box("abc");
        int count = 0;
        box.SetOnTextChanged([&count](const std::string&){ ++count; });
        box.MoveCaretToEnd();
        box.UpdateComposition("n");
        box.UpdateComposition("ni");
        box.UpdateComposition("nihao");
        EXPECT_EQ(count, 0);   // 组合过程零回调
        box.CommitComposition("你好");
        EXPECT_EQ(count, 1);   // Commit = 正式编辑 → 一次回调
    }
    // F7: CommitComposition 正式生效（C7/C3）
    {
        TestableTextBox box("abc");
        box.MoveCaretToEnd();
        box.UpdateComposition("nihao");
        box.CommitComposition("你好");
        EXPECT_EQ(box.GetText(), "abc你好");
        EXPECT_EQ(box.GetCaret(), 5);
        box.UpdateComposition("zz");   // 组合已结束——下次 Update 视为新组合开始
        EXPECT_EQ(box.GetText(), "abc你好zz");
    }
    // F8: Commit 无组合 no-op（fail-safe）
    {
        TestableTextBox box("abc");
        box.CommitComposition("xyz");
        EXPECT_EQ(box.GetText(), "abc");   // 不变
        EXPECT_EQ(box.GetCaret(), 0);
    }
    // F9: CancelComposition 擦除区间
    {
        TestableTextBox box("abc");
        box.MoveCaretToEnd();
        box.UpdateComposition("nihao");
        box.CancelComposition();
        EXPECT_EQ(box.GetText(), "abc");
        EXPECT_EQ(box.GetCaret(), 3);   // 光标回组合起点
    }
    // F10: Ctrl+A 全选（C1 键盘路径）
    {
        TestableTextBox box("hello");
        box.MoveCaretToStart();
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::A, KeyModifier::Ctrl));
        auto sel = box.GetSelection();
        EXPECT_TRUE(sel.has_value());
        EXPECT_EQ(sel->start, 0);
        EXPECT_EQ(sel->end, 5);
    }
    // F11: Ctrl+C 无选区 no-op（无窗口 GetWindow()==nullptr → 静默跳过，不崩）
    {
        TestableTextBox box("hello");
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::C, KeyModifier::Ctrl));
        EXPECT_EQ(box.GetText(), "hello");
    }
    // F12: OnTimer 切换光标（无窗口环境允许验证翻转逻辑）
    {
        TestableTextBox box("abc");
        EXPECT_FALSE(box.IsCaretVisible());
        box.OnTimer(TimerEvent(nullptr, TextBox::kCaretBlinkTimer));
        EXPECT_TRUE(box.IsCaretVisible());
        box.OnTimer(TimerEvent(nullptr, TextBox::kCaretBlinkTimer));
        EXPECT_FALSE(box.IsCaretVisible());
    }
    // F13: OnTimer 非本 id 忽略（多 timer 隔离）
    {
        TestableTextBox box("abc");
        box.OnTimer(TimerEvent(nullptr, TextBox::kCaretBlinkTimer));   // true
        box.OnTimer(TimerEvent(nullptr, 999));
        EXPECT_TRUE(box.IsCaretVisible());   // 非本 id 不翻转
    }
    // F14: Composition 中间替换（GPT 检查点 4——不破坏前后文本）
    {
        TestableTextBox box("abcDEF");
        box.MoveCaretToEnd();                                    // caret=6
        box.MoveCaret(TextBox::CaretDirection::Left);            // 5
        box.MoveCaret(TextBox::CaretDirection::Left);            // 4
        box.MoveCaret(TextBox::CaretDirection::Left);            // 3（'c' 后）
        box.UpdateComposition("nihao");
        EXPECT_EQ(box.GetText(), "abcnihaoDEF");
        box.UpdateComposition("你好");
        EXPECT_EQ(box.GetText(), "abc你好DEF");
        box.CommitComposition("你好");
        EXPECT_EQ(box.GetText(), "abc你好DEF");
        EXPECT_EQ(box.GetCaret(), 5);
    }
    // F15: Composition code point 索引（GPT 检查点 5——中文不按 UTF-8 byte 切）
    {
        TestableTextBox box("你ABC好");   // 码点：你(0) A(1) B(2) C(3) 好(4)
        box.MoveCaretToStart();
        box.MoveCaret(TextBox::CaretDirection::Right);   // caret=1（'你' 后——非 byte 3）
        box.UpdateComposition("中文");
        EXPECT_EQ(box.GetText(), "你中文ABC好");
        EXPECT_EQ(box.GetCaret(), 3);   // 1 + 2 码点
        box.CommitComposition("中文");
        EXPECT_EQ(box.GetText(), "你中文ABC好");
        EXPECT_EQ(box.GetCaret(), 3);
    }
}

// ── 8.5.2：多行与滚动测试（F16-F30）──────────────────────────
// 覆盖：行分割（LineRange 推断——空尾行/连续换行/UTF-8 多字节）/ Enter 换行 / 跨行光标 /
// 坐标定位 Y 定行（无窗口 GetLineHeight=16 兜底）/ 滚动 clamp / 双击选词（word/non-word/中文/emoji）。

void TestTextBoxMultiline()
{
    // F16: RecalculateLines 行分割（"ab\ncd\ne" → 行起始 {0,3,6}——经 LineRange 推断）
    {
        TestableTextBox box("ab\ncd\ne");
        box.RecalculateLines();
        auto r0 = box.LineRange(0);
        auto r1 = box.LineRange(1);
        auto r2 = box.LineRange(2);
        EXPECT_EQ(r0.first, 0);  EXPECT_EQ(r0.second, 2);
        EXPECT_EQ(r1.first, 3);  EXPECT_EQ(r1.second, 5);
        EXPECT_EQ(r2.first, 6);  EXPECT_EQ(r2.second, 7);
    }
    // F17: Enter 插入换行（OnKeyDown 键盘路径）
    {
        TestableTextBox box("abc");
        box.MoveCaretToEnd();
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Enter, KeyModifier::None));
        EXPECT_EQ(box.GetText(), "abc\n");
        EXPECT_EQ(box.GetCaret(), 4);
    }
    // F18: 跨行光标移动（越过 \n）
    {
        TestableTextBox box("a\nb");   // 码点 a(0) \n(1) b(2)
        box.MoveCaretToStart();
        box.MoveCaret(TextBox::CaretDirection::Right);   // 1（\n 处）
        EXPECT_EQ(box.GetCaret(), 1);
        box.MoveCaret(TextBox::CaretDirection::Right);   // 2（跨到第二行 b）
        EXPECT_EQ(box.GetCaret(), 2);
    }
    // F19a: CaretIndexFromPosition Y 定行（无窗口 GetLineHeight=16 兜底——第一行）
    {
        TestableTextBox box("ab\ncd");
        box.RecalculateLines();
        EXPECT_EQ(box.CaretIndexFromPosition(Point{ 5.0f, 0.0f }), 0);     // 第一行 → 行起始 0
        EXPECT_EQ(box.CaretIndexFromPosition(Point{ 5.0f, 10.0f }), 0);    // 第一行内
    }
    // F20a: CaretIndexFromPosition 跨行（第二行）
    {
        TestableTextBox box("ab\ncd");
        box.RecalculateLines();
        EXPECT_EQ(box.CaretIndexFromPosition(Point{ 5.0f, 16.0f }), 3);    // y=16 → 第二行起始 3
        EXPECT_EQ(box.CaretIndexFromPosition(Point{ 5.0f, 30.0f }), 3);    // 第二行内
    }
    // F21: OnMouseWheel 滚动（向上减小 / 向下增加 + clamp [0, max]）
    {
        TestableTextBox box("a\nb\nc\nd\ne");   // 5 行 × 16 = 80 内容高
        box.SetSize(200, 40);                  // 可视 40 → maxScroll = 80-40 = 40
        box.MoveCaretToEnd();                  // EnsureCaretVisible → 已滚到 40（max）
        EXPECT_EQ(box.GetScrollOffsetY(), 40.0f);
        box.OnMouseWheel(MouseWheelEvent(nullptr, 0, 0, 120));    // 向上滚一行 → 40-16 = 24
        EXPECT_EQ(box.GetScrollOffsetY(), 24.0f);
        box.OnMouseWheel(MouseWheelEvent(nullptr, 0, 0, -1200));  // 向下狂滚 → 24+160 = 184 → clamp 40
        EXPECT_EQ(box.GetScrollOffsetY(), 40.0f);
        box.OnMouseWheel(MouseWheelEvent(nullptr, 0, 0, 12000));  // 向上狂滚 → clamp 0
        EXPECT_EQ(box.GetScrollOffsetY(), 0.0f);
    }
    // F22: 双击英文选词（"hello world" 点 world 中）
    {
        TestableTextBox box("hello world");
        auto w = box.GetWordBounds(8);   // "world" 的 o（码点 8）
        EXPECT_EQ(w.first, 6);
        EXPECT_EQ(w.second, 11);
    }
    // F23: 双击中文选词（单码点独立）
    {
        TestableTextBox box("你好世界");
        auto w = box.GetWordBounds(1);   // 第 2 个码点（好）
        EXPECT_EQ(w.first, 1);
        EXPECT_EQ(w.second, 2);
    }
    // F24: 双击 emoji UTF-8 code point 完整选择（😀 = 4 字节 1 码点）
    {
        TestableTextBox box("a😀b");
        auto w = box.GetWordBounds(1);   // 😀
        EXPECT_EQ(w.first, 1);
        EXPECT_EQ(w.second, 2);
    }
    // F25: EnsureCaretVisible 光标跟随（末尾超出可视 → 滚动）
    {
        TestableTextBox box("a\nb\nc\nd\ne");   // 5 行 × 16 = 80
        box.SetSize(200, 40);                  // 可视 40 → maxScroll 40
        box.MoveCaretToEnd();                  // caret 第 5 行 top=64 → 滚到 64+16-40=40
        EXPECT_EQ(box.GetScrollOffsetY(), 40.0f);
    }
    // F26: 尾部换行空行（"ab\n" → line1=[3,3]）
    {
        TestableTextBox box("ab\n");
        box.RecalculateLines();
        auto r0 = box.LineRange(0);
        auto r1 = box.LineRange(1);
        EXPECT_EQ(r0.first, 0);  EXPECT_EQ(r0.second, 2);
        EXPECT_EQ(r1.first, 3);  EXPECT_EQ(r1.second, 3);   // 空尾行
    }
    // F27: 空文本（"" → 单行 [0,0]）
    {
        TestableTextBox box("");
        box.RecalculateLines();
        auto r0 = box.LineRange(0);
        EXPECT_EQ(r0.first, 0);  EXPECT_EQ(r0.second, 0);
    }
    // F28: 连续换行（"a\n\nb" → line0=[0,1] line1=[2,2] line2=[3,4]）
    {
        TestableTextBox box("a\n\nb");
        box.RecalculateLines();
        auto r0 = box.LineRange(0);
        auto r1 = box.LineRange(1);
        auto r2 = box.LineRange(2);
        EXPECT_EQ(r0.first, 0);  EXPECT_EQ(r0.second, 1);   // "a"
        EXPECT_EQ(r1.first, 2);  EXPECT_EQ(r1.second, 2);   // ""（空行）
        EXPECT_EQ(r2.first, 3);  EXPECT_EQ(r2.second, 4);   // "b"
    }
    // F29: 双击标点/空格（non-word → 单码点）
    {
        TestableTextBox box("hello, world");
        auto comma = box.GetWordBounds(5);   // 逗号（码点 5）
        EXPECT_EQ(comma.first, 5);  EXPECT_EQ(comma.second, 6);
        auto space = box.GetWordBounds(6);   // 空格（码点 6）
        EXPECT_EQ(space.first, 6);  EXPECT_EQ(space.second, 7);
        auto hello = box.GetWordBounds(1);   // "hello" 中
        EXPECT_EQ(hello.first, 0);  EXPECT_EQ(hello.second, 5);
    }
    // F30: UTF-8 多字节换行索引（"你\n好" → 行起始 {0,2}——"你" 3 字节但 1 码点）
    {
        TestableTextBox box("你\n好");
        box.RecalculateLines();
        auto r0 = box.LineRange(0);
        auto r1 = box.LineRange(1);
        EXPECT_EQ(r0.first, 0);  EXPECT_EQ(r0.second, 1);   // "你"（码点 0，3 字节）
        EXPECT_EQ(r1.first, 2);  EXPECT_EQ(r1.second, 3);   // "好"（码点 2）
    }
    // F31: Up/Down 基本跨行（无窗口：preferred column=0 → 行起始）
    {
        TestableTextBox box("ab\ncd");
        box.MoveCaretToStart();                                  // caret=0（第一行）
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Down, KeyModifier::None));
        EXPECT_EQ(box.GetCaret(), 3);                            // → 第二行起始（码点 3）
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Up, KeyModifier::None));
        EXPECT_EQ(box.GetCaret(), 0);                            // → 回第一行起始
    }
    // F32: 边界 no-op（第一行 Up / 最后一行 Down 不动）
    {
        TestableTextBox box("ab\ncd");
        box.MoveCaretToStart();                                  // caret=0（第一行）
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Up, KeyModifier::None));
        EXPECT_EQ(box.GetCaret(), 0);                            // 第一行 Up → no-op
        box.MoveCaretToEnd();                                    // caret=5（末尾——"ab\ncd" 共 5 码点）
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Down, KeyModifier::None));
        EXPECT_EQ(box.GetCaret(), 5);                            // 最后一行 Down → no-op
    }
    // F33: Shift+Down 扩展选择（anchor 保留、caret 到下一行）
    {
        TestableTextBox box("ab\ncd");
        box.MoveCaretToStart();                                  // caret=0
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Down, KeyModifier::Shift));
        EXPECT_EQ(box.GetCaret(), 3);                            // caret → 第二行起始
        auto sel = box.GetSelection();
        EXPECT_TRUE(sel.has_value());
        EXPECT_EQ(sel->start, 0);  EXPECT_EQ(sel->end, 3);       // anchor=0 保留 → {0,3}
    }
    // F34: Down→Up 往返（preferred column 跨行不重置——无窗口恒 0，验证机制往返）
    {
        TestableTextBox box("abc\ndef");
        box.MoveCaretToEnd();                                    // caret=6（第二行末尾）
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Up, KeyModifier::None));
        EXPECT_EQ(box.GetCaret(), 0);                            // → 第一行起始（preferred=0）
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Down, KeyModifier::None));
        EXPECT_EQ(box.GetCaret(), 4);                            // → 回第二行起始
        EXPECT_FALSE(box.GetSelection().has_value());            // 无 Shift → 无选择残留
    }
}

// ── 8.5.3：Undo/Redo（快照模式 B6 + C3/C4/D4/D7/D9 契约）──
void TestTextBoxUndoRedo()
{
    // F35：基本撤销（InsertCodepoint）
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.InsertCodepoint(U'd');
        EXPECT_EQ(box.GetText(), "abcd");
        box.Undo();
        EXPECT_EQ(box.GetText(), "abc");
        EXPECT_EQ(box.GetCaret(), 3);
    }
    // F36：逐级撤销 + redo（InsertText 多步）
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.InsertText("de");                                     // "abcde"
        box.InsertText("f");                                      // "abcdef"
        EXPECT_EQ(box.GetText(), "abcdef");
        box.Undo();                                               // 撤销 "f" → "abcde"
        EXPECT_EQ(box.GetText(), "abcde");
        box.Redo();                                               // 重做 "f" → "abcdef"
        EXPECT_EQ(box.GetText(), "abcdef");
    }
    // F37：新编辑清空 redo 栈（C4b——作废旧分支）
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.InsertText("d");                                      // "abcd"
        box.Undo();                                               // → "abc"
        box.InsertText("x");                                      // 新编辑 → "abcx"（清 redo）
        EXPECT_EQ(box.GetText(), "abcx");
        box.Redo();                                               // redo 已清空 → no-op
        EXPECT_EQ(box.GetText(), "abcx");
    }
    // F38：空栈 no-op（Undo/Redo 各试）
    {
        TextBox box("abc");
        box.Undo();                                               // undo 栈空
        EXPECT_EQ(box.GetText(), "abc");
        box.Redo();                                               // redo 栈空
        EXPECT_EQ(box.GetText(), "abc");
    }
    // F39：空操作不 Push（DeleteBackward 头边界 no-op 后 Undo 无操作——D7）
    {
        TextBox box("abc");                                       // caret=0（构造默认）
        box.DeleteBackward();                                     // 头边界 → no-op（不 Push）
        EXPECT_EQ(box.GetText(), "abc");
        box.Undo();                                               // 栈空 → no-op
        EXPECT_EQ(box.GetText(), "abc");
    }
    // F40：Composition 一次撤销（C3——组合本身是一个 Undo 单元）
    {
        TestableTextBox box("abc");
        box.MoveCaretToEnd();
        box.UpdateComposition("nihao");                           // 首次 Push "abc" + 占位
        box.CommitComposition("你好");                             // 正式文本（不再 Push）
        EXPECT_EQ(box.GetText(), "abc你好");
        box.Undo();                                               // 一次撤销整个组合 → "abc"
        EXPECT_EQ(box.GetText(), "abc");
    }
    // F41：快照含多行文本与光标（Undo 恢复完整编辑状态）
    {
        TextBox box("a\nb");
        box.MoveCaretToEnd();                                     // caret=3
        box.InsertText("\nc");                                    // "a\nb\nc"
        EXPECT_EQ(box.GetText(), "a\nb\nc");
        box.Undo();
        EXPECT_EQ(box.GetText(), "a\nb");
    }
    // F42：InsertText Undo（粘贴/Enter 路径）
    {
        TextBox box("ab");
        box.MoveCaretToEnd();
        box.InsertText("cd");                                     // "abcd"
        box.Undo();
        EXPECT_EQ(box.GetText(), "ab");
    }
    // F43：Cut Undo（剪切 = 复制 + 删选中区，一次编辑一次快照——走 Ctrl+X 键盘路径）
    {
        TestableTextBox box("hello world");
        box.MoveCaretToStart();
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Right, KeyModifier::Shift));
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Right, KeyModifier::Shift));   // 选区 {0,2}
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::X, KeyModifier::Ctrl));        // 剪 "he"（无窗口剪贴板静默跳过复制，删除生效）
        EXPECT_EQ(box.GetText(), "llo world");
        box.Undo();
        EXPECT_EQ(box.GetText(), "hello world");
    }
    // F44：Composition Cancel（恢复快照 + 不留历史 + 后续 Undo no-op——GPT 🔴）
    {
        TestableTextBox box("abc");
        box.MoveCaretToEnd();
        box.UpdateComposition("nihao");                           // → "abcnihao"，Push "abc"
        EXPECT_EQ(box.GetText(), "abcnihao");
        box.CancelComposition();                                  // RestoreSnapshot → "abc"，弹栈
        EXPECT_EQ(box.GetText(), "abc");
        box.Undo();                                               // 栈空（快照已弹）→ no-op
        EXPECT_EQ(box.GetText(), "abc");                          // Cancel 不是编辑，不留历史
    }
    // F45：连续 Redo（Undo↔Redo 双向流转完整链）
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.InsertText("d");                                      // "abcd"
        box.InsertText("e");                                      // "abcde"
        box.Undo();                                               // → "abcd"
        box.Undo();                                               // → "abc"
        EXPECT_EQ(box.GetText(), "abc");
        box.Redo();                                               // → "abcd"
        box.Redo();                                               // → "abcde"
        EXPECT_EQ(box.GetText(), "abcde");
    }
    // F45b：Ctrl+Z/Y 键盘路径（走 OnKeyDown Ctrl 分支）
    {
        TestableTextBox box("abc");
        box.MoveCaretToEnd();
        box.InsertText("xyz");                                    // "abcxyz"
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Z, KeyModifier::Ctrl));
        EXPECT_EQ(box.GetText(), "abc");
        box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Y, KeyModifier::Ctrl));
        EXPECT_EQ(box.GetText(), "abcxyz");
    }
}

// ── P1：echo 掩码 / 只读 / 形态（modelprobe-p1-detailed-design §3/§8）──

void TestTextBoxEchoMasked()
{
	// PaintMasked：Password 绘制串 = 每码点一个 •（U+2022，UTF-8 3 字节）
	TextBox box("abc");
	box.SetEchoMode(TextBox::EchoMode::Password);
	box.SetSize(200, 30);
	RecordingBackend backend;
	CommandBuffer commands;
	PaintContext ctx(commands, backend);
	box.Paint(ctx, 10, 20);
	bool foundMasked = false;
	for (const auto& cmd : commands){
		if (const auto* text = std::get_if<DrawTextCommand>(&cmd)){
			EXPECT_EQ(text->text, "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2");   // •••
			foundMasked = true;
		}
	}
	EXPECT_TRUE(foundMasked);
}

void TestTextBoxEchoGetTextReal()
{
	// GetTextReal：数据层不变（显示层打点）——编辑作用于真实文本
	TextBox box("sk-abc");
	box.SetEchoMode(TextBox::EchoMode::Password);
	EXPECT_EQ(box.GetText(), "sk-abc");
	box.MoveCaretToEnd();
	box.InsertCodepoint(U'X');
	EXPECT_EQ(box.GetText(), "sk-abcX");
	EXPECT_EQ(box.GetCaret(), 7);
}

void TestTextBoxReadOnlyEditNoOp()
{
	TextBox box("abc");
	box.SetReadOnly(true);
	box.MoveCaretToEnd();
	box.InsertCodepoint(U'X');
	box.DeleteBackward();
	box.InsertText("YY");
	box.Undo();
	box.Redo();
	EXPECT_EQ(box.GetText(), "abc");   // 编辑全 no-op
	EXPECT_EQ(box.GetCaret(), 3);
	// SetText 仍可程序写入（预览刷新）
	box.SetText("{\"k\":1}");
	EXPECT_EQ(box.GetText(), "{\"k\":1}");
	// 关闭只读恢复编辑
	box.SetReadOnly(false);
	box.MoveCaretToEnd();
	box.InsertCodepoint(U'Z');
	EXPECT_EQ(box.GetText(), "{\"k\":1}Z");
}

void TestTextBoxReadOnlyCompositionRefuse()
{
	TestableTextBox box("abc");   // TestableTextBox：UpdateComposition/CommitComposition 经 using 暴露（protected）
	box.SetReadOnly(true);
	box.MoveCaretToEnd();
	box.UpdateComposition("ni");   // 只读 → no-op（不进入组合）
	EXPECT_EQ(box.GetText(), "abc");
	box.CommitComposition("你好");
	EXPECT_EQ(box.GetText(), "abc");   // 未组合 → no-op
}

void TestTextBoxSingleLineEnterNoOp()
{
	// 单行：Enter 不插入 \n（多行默认 Enter 换行——8.5.2 行为不变）
	TestableTextBox box("abc");
	box.MoveCaretToEnd();
	box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Enter, KeyModifier::None));
	EXPECT_EQ(box.GetText(), "abc\n");   // 默认多行：Enter 换行
	EXPECT_EQ(box.GetCaret(), 4);

	box.SetSingleLine(true);
	box.MoveCaretToEnd();
	box.OnKeyDown(KeyDownEvent(nullptr, KeyCode::Enter, KeyModifier::None));
	EXPECT_EQ(box.GetText(), "abc\n");   // 单行：Enter no-op
	EXPECT_EQ(box.GetCaret(), 4);

	// 单行开关幂等
	box.SetSingleLine(true);
	EXPECT_TRUE(box.IsSingleLine());
}

void TestTextBoxPreferredMultilineNotParticipating()
{
	// §3.4：多行（含 \n）→ preferred = 当前尺寸（v1 不参与 AutoSize——高度仍手工 SetSize）
	TestableTextBox box("ab\ncd");
	box.SetSize(100, 30);
	const Size preferred = box.GetPreferredSize();
	EXPECT_NEAR(preferred.width, 100.0f, kEps);
	EXPECT_NEAR(preferred.height, 30.0f, kEps);
}

void TestTextBoxPreferredSingleLine()
{
	// §3.6 验证项：单行 "Hi"（2 码点 × 8 = 16 宽）+ padding 4 → {16+8, 16+8} = {24, 24}——
	// height == lineH + padding×2 → AutoSize 后上下对称自然垂直居中（不做 VerticalCentered API）
	TestableTextBox box("Hi");
	box.SetSize(999, 999);
	box.SetStyle(TextBoxStyleOverride{ .padding = 4.0f });
	const Size preferred = box.GetPreferredSize();
	EXPECT_NEAR(preferred.width, 24.0f, kEps);
	EXPECT_NEAR(preferred.height, 24.0f, kEps);
	EXPECT_TRUE(box.AutoSize());
	EXPECT_EQ(box.GetWidth(), 24);
	EXPECT_EQ(box.GetHeight(), 24);
}

void TestTextBoxShapeRounded()
{
	TextBox box;
	box.SetSize(100, 30);
	box.SetStyle(TextBoxStyleOverride{
		.background = Color::White(),
		.cornerRadius = 6.0f,
	});
	RecordingBackend backend;
	CommandBuffer commands;
	PaintContext ctx(commands, backend);
	box.Paint(ctx, 0, 0);
	// 背景命令 = DrawRoundedRectCommand（radius 6）——命令流 = PushClip → 背景 → PopClip
	EXPECT_EQ(commands.size(), 3);
	EXPECT_TRUE(std::holds_alternative<DrawRoundedRectCommand>(commands[1]));
	const auto& rr = std::get<DrawRoundedRectCommand>(commands[1]);
	EXPECT_NEAR(rr.cornerRadius, 6.0f, kEps);
}

void TestTextBoxShapeBorderRing()
{
	TextBox box;
	box.SetSize(100, 30);
	box.SetStyle(TextBoxStyleOverride{
		.background = Color::FromRGBA8(28, 33, 43, 255),
		.cornerRadius = 6.0f,
		.borderWidth = 1.0f,
		.borderColor = Color::FromRGBA8(42, 49, 64, 255),
	});
	RecordingBackend backend;
	CommandBuffer commands;
	PaintContext ctx(commands, backend);
	box.Paint(ctx, 0, 0);
	// 双矩形序：PushClip → ① 外层 borderColor（radius 6）② 内层背景（内缩 1，radius 5）→ PopClip
	EXPECT_EQ(commands.size(), 4);
	const auto& outer = std::get<DrawRoundedRectCommand>(commands[1]);
	EXPECT_EQ(outer.color, Color::FromRGBA8(42, 49, 64, 255));
	EXPECT_NEAR(outer.cornerRadius, 6.0f, kEps);
	const auto& inner = std::get<DrawRoundedRectCommand>(commands[2]);
	EXPECT_EQ(inner.color, Color::FromRGBA8(28, 33, 43, 255));
	EXPECT_NEAR(inner.cornerRadius, 5.0f, kEps);
}

} // anonymous namespace

void ECDI::Test::RegisterTextBoxTests()
{
    GetTestRegistry().Add("TextBox.InsertDelete", &TestTextBoxInsertDelete);
    GetTestRegistry().Add("TextBox.CaretMovement", &TestTextBoxCaretMovement);
    GetTestRegistry().Add("TextBox.GetSelection", &TestTextBoxGetSelection);
    GetTestRegistry().Add("TextBox.Boundary", &TestTextBoxBoundary);
    GetTestRegistry().Add("TextBox.Callback", &TestTextBoxCallback);
    GetTestRegistry().Add("TextBox.SelectionKeyboard", &TestTextBoxSelectionKeyboard);
    GetTestRegistry().Add("TextBox.SelectionEdit", &TestTextBoxSelectionEdit);
    GetTestRegistry().Add("TextBox.TextSystem2", &TestTextBoxTextSystem2);   // 8.5.1：F1-F15
    GetTestRegistry().Add("TextBox.Multiline", &TestTextBoxMultiline);       // 8.5.2：F16-F30
    GetTestRegistry().Add("TextBox.UndoRedo", &TestTextBoxUndoRedo);         // 8.5.3：F35-F45
    GetTestRegistry().Add("TextBox.EchoMasked", &TestTextBoxEchoMasked);     // P1：掩码绘制
    GetTestRegistry().Add("TextBox.EchoGetTextReal", &TestTextBoxEchoGetTextReal);  // P1：数据层真实值
    GetTestRegistry().Add("TextBox.ReadOnlyEditNoOp", &TestTextBoxReadOnlyEditNoOp);  // P1：只读门禁
    GetTestRegistry().Add("TextBox.ReadOnlyCompositionRefuse", &TestTextBoxReadOnlyCompositionRefuse);  // P1：只读拒 IME
    GetTestRegistry().Add("TextBox.SingleLineEnterNoOp", &TestTextBoxSingleLineEnterNoOp);  // P1：单行 Enter 不换行
    GetTestRegistry().Add("TextBox.MultilineNotParticipating", &TestTextBoxPreferredMultilineNotParticipating);  // 9.8：多行不参与
    GetTestRegistry().Add("TextBox.SingleLinePreferred", &TestTextBoxPreferredSingleLine);  // 9.8：padding 修正 + 自然居中验证
    GetTestRegistry().Add("TextBox.ShapeRounded", &TestTextBoxShapeRounded);  // P1：圆角背景
    GetTestRegistry().Add("TextBox.ShapeBorderRing", &TestTextBoxShapeBorderRing);  // P1：双矩形描边环
}
