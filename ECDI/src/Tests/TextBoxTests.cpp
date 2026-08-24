#include "RunAllTests.h"
#include "TestFramework.h"
#include "ECDI/Widget/TextBox.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyDownEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyModifier.h"

using namespace ECDI;

namespace {

/// @brief 可测 TextBox：暴露 protected 键盘选择路径（无窗口安全——SyncTextInputCaret 有 Window 防御）
/// @note CaretIndexFromX 为 private（点击定位算法），派生类不可访问——算法直测
/// 留待最小窗口集成测试（与拖选事件流同归；不为此改框架可见性——P0 边界修正，v0.2 实现记录）。
class TestableTextBox : public TextBox
{
public:
    using TextBox::TextBox;
    using TextBox::OnKeyDown;   ///< 暴露键盘选择路径（Shift+方向/Home/End）
};

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
}
