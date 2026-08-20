#include "RunAllTests.h"
#include "ECDI/Core/ECDIAssert.h"
#include "ECDI/Widget/TextBox.h"

using namespace ECDI;

namespace {

void TestTextBoxInsertDelete()
{
    // ── 原 #7：TextBox 编辑逻辑（迁移 + 扩展）──

    // InsertCodepoint 末尾
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.InsertCodepoint(U'd');
        FRAMEWORK_ASSERT(box.GetText() == "abcd");
        FRAMEWORK_ASSERT(box.GetCaret() == 4);
    }
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.InsertCodepoint(U'X');
        FRAMEWORK_ASSERT(box.GetText() == "abcX");
        FRAMEWORK_ASSERT(box.GetCaret() == 4);
    }

    // InsertCodepoint 开头
    {
        TextBox box("abc");
        box.MoveCaretToStart();
        box.InsertCodepoint(U'X');
        FRAMEWORK_ASSERT(box.GetText() == "Xabc");
        FRAMEWORK_ASSERT(box.GetCaret() == 1);
    }

    // InsertCodepoint 中间
    {
        TextBox box("abc");
        box.MoveCaretToStart();
        box.MoveCaret(TextBox::CaretDirection::Right);
        box.InsertCodepoint(U'X');
        FRAMEWORK_ASSERT(box.GetText() == "aXbc");
        FRAMEWORK_ASSERT(box.GetCaret() == 2);
    }

    // DeleteBackward 末尾
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.DeleteBackward();
        FRAMEWORK_ASSERT(box.GetText() == "ab");
        FRAMEWORK_ASSERT(box.GetCaret() == 2);
    }

    // DeleteBackward 中间
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.MoveCaret(TextBox::CaretDirection::Left);
        box.DeleteBackward();
        FRAMEWORK_ASSERT(box.GetText() == "ac");
        FRAMEWORK_ASSERT(box.GetCaret() == 1);
    }

    // DeleteBackward 头边界
    {
        TextBox box("abc");
        box.MoveCaretToStart();
        box.DeleteBackward();
        FRAMEWORK_ASSERT(box.GetText() == "abc");
        FRAMEWORK_ASSERT(box.GetCaret() == 0);
    }

    // DeleteForward 开头
    {
        TextBox box("abc");
        box.MoveCaretToStart();
        box.DeleteForward();
        FRAMEWORK_ASSERT(box.GetText() == "bc");
        FRAMEWORK_ASSERT(box.GetCaret() == 0);
    }

    // DeleteForward 中间
    {
        TextBox box("abc");
        box.MoveCaretToStart();
        box.MoveCaret(TextBox::CaretDirection::Right);
        box.DeleteForward();
        FRAMEWORK_ASSERT(box.GetText() == "ac");
        FRAMEWORK_ASSERT(box.GetCaret() == 1);
    }

    // DeleteForward 尾边界
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.DeleteForward();
        FRAMEWORK_ASSERT(box.GetText() == "abc");
        FRAMEWORK_ASSERT(box.GetCaret() == 3);
    }

    // emoji 不切字
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.InsertCodepoint(U'😀');
        FRAMEWORK_ASSERT(box.GetText() == "abc😀");
        FRAMEWORK_ASSERT(box.GetCaret() == 4);
        box.DeleteBackward();
        FRAMEWORK_ASSERT(box.GetText() == "abc");
        FRAMEWORK_ASSERT(box.GetCaret() == 3);
    }

    // 中文不切字
    {
        TextBox box("ab");
        box.MoveCaretToEnd();
        box.InsertCodepoint(U'中');
        FRAMEWORK_ASSERT(box.GetText() == "ab中");
        FRAMEWORK_ASSERT(box.GetCaret() == 3);
        box.DeleteBackward();
        FRAMEWORK_ASSERT(box.GetText() == "ab");
        FRAMEWORK_ASSERT(box.GetCaret() == 2);
    }
}

void TestTextBoxCaretMovement()
{
    // ── T2：光标迁移补测 ──

    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.MoveCaret(TextBox::CaretDirection::Left);
        FRAMEWORK_ASSERT(box.GetCaret() == 2);
    }
    {
        TextBox box("abc");
        box.MoveCaretToStart();
        box.MoveCaret(TextBox::CaretDirection::Left);
        FRAMEWORK_ASSERT(box.GetCaret() == 0);
    }
    {
        TextBox box("abc");
        box.MoveCaretToStart();
        box.MoveCaret(TextBox::CaretDirection::Right);
        FRAMEWORK_ASSERT(box.GetCaret() == 1);
    }
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.MoveCaret(TextBox::CaretDirection::Right);
        FRAMEWORK_ASSERT(box.GetCaret() == 3);
    }
    {
        TextBox box("abc");
        box.MoveCaretToEnd();
        box.MoveCaretToStart();
        box.MoveCaretToEnd();
        FRAMEWORK_ASSERT(box.GetCaret() == 3);
    }
    // 中文/emoji 光标移动不切字
    {
        TextBox box("中a😀");
        box.MoveCaretToEnd();
        FRAMEWORK_ASSERT(box.GetCaret() == 3);
        box.MoveCaret(TextBox::CaretDirection::Left);
        FRAMEWORK_ASSERT(box.GetCaret() == 2);
        box.MoveCaret(TextBox::CaretDirection::Left);
        FRAMEWORK_ASSERT(box.GetCaret() == 1);
        box.MoveCaret(TextBox::CaretDirection::Left);
        FRAMEWORK_ASSERT(box.GetCaret() == 0);
    }
}

void TestTextBoxGetSelection()
{
    // ── T3：GetSelection 查询接口 ──

    {
        TextBox box("hello");
        auto sel = box.GetSelection();
        FRAMEWORK_ASSERT(!sel.has_value());
    }
    {
        TextBox box("hello");
        box.InsertCodepoint(U'X');
        auto sel = box.GetSelection();
        FRAMEWORK_ASSERT(!sel.has_value());
    }
    {
        TextBox box("hello");
        box.MoveCaretToEnd();
        auto sel = box.GetSelection();
        FRAMEWORK_ASSERT(!sel.has_value());
        box.MoveCaret(TextBox::CaretDirection::Left);
        sel = box.GetSelection();
        FRAMEWORK_ASSERT(!sel.has_value());
    }
    {
        TextBox box("hello");
        box.MoveCaretToEnd();
        box.DeleteBackward();
        auto sel = box.GetSelection();
        FRAMEWORK_ASSERT(!sel.has_value());
    }
    {
        TextBox box("");
        auto sel = box.GetSelection();
        FRAMEWORK_ASSERT(!sel.has_value());
    }
}

void TestTextBoxBoundary()
{
    // ── T4：TextBox 边界条件 ──

    {
        TextBox box("");
        box.InsertCodepoint(U'a');
        FRAMEWORK_ASSERT(box.GetText() == "a");
        FRAMEWORK_ASSERT(box.GetCaret() == 1);
    }
    {
        TextBox box("");
        box.DeleteBackward();
        FRAMEWORK_ASSERT(box.GetText() == "");
        FRAMEWORK_ASSERT(box.GetCaret() == 0);
    }
    {
        TextBox box("");
        box.DeleteForward();
        FRAMEWORK_ASSERT(box.GetText() == "");
        FRAMEWORK_ASSERT(box.GetCaret() == 0);
    }
    {
        TextBox box("中🎉a");
        box.MoveCaretToEnd();
        box.InsertCodepoint(U'X');
        FRAMEWORK_ASSERT(box.GetText() == "中🎉aX");
        FRAMEWORK_ASSERT(box.GetCaret() == 4);
    }
    {
        TextBox box("中🎉a");
        box.MoveCaretToEnd();
        box.DeleteBackward();
        FRAMEWORK_ASSERT(box.GetText() == "中🎉");
        FRAMEWORK_ASSERT(box.GetCaret() == 2);
    }
    {
        TextBox box("中🎉");
        box.MoveCaretToEnd();
        box.DeleteBackward();
        FRAMEWORK_ASSERT(box.GetText() == "中");
        FRAMEWORK_ASSERT(box.GetCaret() == 1);
    }
    {
        TextBox box("x");
        box.MoveCaretToEnd();
        box.DeleteBackward();
        FRAMEWORK_ASSERT(box.GetText() == "");
        FRAMEWORK_ASSERT(box.GetCaret() == 0);
    }
    {
        TextBox box("x");
        box.DeleteForward();
        FRAMEWORK_ASSERT(box.GetText() == "");
        FRAMEWORK_ASSERT(box.GetCaret() == 0);
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
        FRAMEWORK_ASSERT(box.GetText() == "abcd");
        FRAMEWORK_ASSERT(lastText == "abcd");
    }

    // TC1b: DeleteBackward 触发回调（普通删除路径）
    {
        TextBox box("abc");
        int count = 0;
        box.SetOnTextChanged([&count](const std::string&){ ++count; });
        box.MoveCaretToEnd();
        box.DeleteBackward();
        FRAMEWORK_ASSERT(box.GetText() == "ab");
        FRAMEWORK_ASSERT(count == 1);
    }

    // TC1c: DeleteForward 触发回调
    {
        TextBox box("abc");
        int count = 0;
        box.SetOnTextChanged([&count](const std::string&){ ++count; });
        box.MoveCaretToStart();
        box.DeleteForward();
        FRAMEWORK_ASSERT(box.GetText() == "bc");
        FRAMEWORK_ASSERT(count == 1);
    }

    // TC2: DeleteBackward 头边界空操作 → 不触发（D7 边界语义）
    {
        TextBox box("abc");
        int count = 0;
        box.SetOnTextChanged([&count](const std::string&){ ++count; });
        box.MoveCaretToStart();
        box.DeleteBackward();
        FRAMEWORK_ASSERT(count == 0);
    }

    // TC2b: DeleteForward 尾边界空操作 → 不触发
    {
        TextBox box("abc");
        int count = 0;
        box.SetOnTextChanged([&count](const std::string&){ ++count; });
        box.MoveCaretToEnd();
        box.DeleteForward();
        FRAMEWORK_ASSERT(count == 0);
    }

    // TC3: SetText 不触发回调（D7 核心：程序设值不算用户修改）
    {
        TextBox box("abc");
        int count = 0;
        box.SetOnTextChanged([&count](const std::string&){ ++count; });
        box.SetText("xyz");
        FRAMEWORK_ASSERT(box.GetText() == "xyz");
        FRAMEWORK_ASSERT(count == 0);
    }

    // TC3b: MoveCaret 系列不触发回调（仅光标移动，文本未变）
    {
        TextBox box("abc");
        int count = 0;
        box.SetOnTextChanged([&count](const std::string&){ ++count; });
        box.MoveCaretToStart();
        box.MoveCaretToEnd();
        box.MoveCaret(TextBox::CaretDirection::Left);
        FRAMEWORK_ASSERT(count == 0);
    }

    // TC3c: 空回调 + 编辑 → 不崩溃（空 std::function 安全性，D5）
    {
        TextBox box("abc");
        box.SetOnTextChanged({});
        box.MoveCaretToEnd();
        box.InsertCodepoint(U'd');
        FRAMEWORK_ASSERT(box.GetText() == "abcd");
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

        FRAMEWORK_ASSERT(box.hookCalled);     // 虚方法被调用
        FRAMEWORK_ASSERT(callbackCalled);     // 回调仍被调用（D4 核心收益：override 不吞回调）
    }
}

} // anonymous namespace

void ECDI::Test::RunTextBoxTests()
{
    TestTextBoxInsertDelete();
    TestTextBoxCaretMovement();
    TestTextBoxGetSelection();
    TestTextBoxBoundary();
    TestTextBoxCallback();
}