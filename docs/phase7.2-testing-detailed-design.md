# Phase 7.2 无窗口单元测试体系 详细设计

> 状态：v1.2（2026-08-17）｜详细设计（已实现，测试通过）
> 前序：初步设计（v1.0）、职责确认（v1.1）
> 相关文档：phase7.2-testing-preliminary-design.md、phase7.2-testing-requirements.md

---

## 1. 文件结构总览

```
ECDI/src/
├── Tests/
│   ├── RunAllTests.h              ← RunAllTests() + 各模块入口 RunXxxTests() 声明（v1.2：入口声明上移至此）
│   ├── RunAllTests.cpp            ← 统一入口，调用各模块测试块
│   ├── RendererTests.cpp          ← #1 Renderer 转发, #3 DrawText 转发, #6 UTF-8
│   ├── WidgetTests.cpp            ← #2 Panel, #4 Label, #5 Button, T5 Widget 树
│   ├── LayoutTests.cpp            ← #8 HorizontalLayout, T6 VerticalLayout
│   ├── TextBoxTests.cpp           ← #7 现有编辑, T1, T2, T3, T4
│   └── EventTests.cpp             ← T7 Event 构造（P2，可选）
├── main.cpp                       ← 修改：移除 8 个断言块，改为 #include + RunAllTests()
```

---

## 2. 接口变更：T3 GetSelection 查询接口

### 2.1 TextBox.h 变更

在 `TextBox` class 的 public 区域（`GetCaret()` 之后）新增：

```cpp
// ── 选择查询（7.2 新增：只读，无副作用——Phase 10 集成测试前置）──────────

/// @brief 选择区间（start <= end；码点索引，非字节偏移）
struct SelectionRange {
    size_t start;
    size_t end;
};

/// @brief 获取当前选中区（如果存在）
/// @return 无选中区 → nullopt；有选中区 → SelectionRange{min, anchor}
/// @details 只读查询——不修改内部状态；供调试/测试/序列化使用。
std::optional<SelectionRange> GetSelection() const;
```

### 2.2 TextBox.cpp 变更

在 `ClearSelection()` 实现之后新增：

```cpp
// ── 选择查询（7.2 新增）──────────────────────────────

std::optional<TextBox::SelectionRange> TextBox::GetSelection() const {
    if (m_selectionAnchor == m_caret) {
        return std::nullopt;
    }
    return TextBox::SelectionRange{
        (std::min)(m_selectionAnchor, m_caret),
        (std::max)(m_selectionAnchor, m_caret)
    };
}
```

> **v1.2 实现回写**：`SelectionRange` 是 `TextBox` 的嵌套类型，类外定义返回类型时必须完整限定为 `TextBox::SelectionRange`（写 `SelectionRange` 时 clangcl 报 unknown type name——嵌套类型不在类外作用域可见）。

### 2.3 GetSelection 接口的合理性（回应 GPT 评审）

GPT 指出"不应仅为测试修改生产代码"。`GetSelection()` 保留的理由：

- **Phase 8 文本系统 2.0**：剪贴板操作（Ctrl+C/Ctrl+X）需要读取选区内容——`GetSelection()` 是必要前置
- **Phase 9 主题系统**：选区高亮样式可能需要序列化/反序列化选区状态
- **通用调试**：开发者需要查询 TextBox 当前选区状态（类似 `GetCaret()` 的定位）

结论：`GetSelection()` 不是纯测试接口，是框架演进需要的只读查询能力。

`TextBox.h` 需要 `#include <optional>`。由于 `TextWidget.h` 已经间接包含 `<string>` 等标准库头文件，检查是否已有 `<optional>`：

- 若 `TextBox.h` 当前未包含 `<optional>`，需在文件顶部添加 `#include <optional>`

---

## 3. 测试文件详细设计

### 3.1 RunAllTests.h

> **v1.2 实现回写**：各模块入口 `RunXxxTests()` 的声明必须放 `RunAllTests.h`（而非 RunAllTests.cpp）——各测试 cpp 内 `ECDI::Test::RunRendererTests()` 的 out-of-line 定义需要声明可见，否则 clangcl 报 "out-of-line definition does not match any declaration"。

```cpp
#pragma once

namespace ECDI::Test {

/// @brief 运行所有无窗口单元测试（Debug 模式调用；失败即终止）
void RunAllTests();

// 各模块测试入口（定义在对应的 Tests/*.cpp 中）
void RunRendererTests();
void RunWidgetTests();
void RunLayoutTests();
void RunTextBoxTests();
// void RunEventTests();  // P2

} // namespace ECDI::Test
```

### 3.2 RunAllTests.cpp

每个模块暴露一个入口函数，避免 `RunAllTests.cpp` 膨胀为几十个头文件声明：

```cpp
#include "RunAllTests.h"
#include "ECDI/Core/ECDIAssert.h"

using namespace ECDI::Test;

void ECDI::Test::RunAllTests()
{
    RunRendererTests();
    RunWidgetTests();
    RunLayoutTests();
    RunTextBoxTests();
    // RunEventTests();  // P2
}
```

> **v1.2 实现回写**：模块入口声明已上移到 `RunAllTests.h`（§3.1），本文件不再重复声明。

### 3.3 RendererTests.cpp

从 main.cpp 迁移以下三个断言块，原样保留断言逻辑：

```cpp
#include "RunAllTests.h"
#include "ECDI/Core/ECDIAssert.h"
#include "ECDI/Render/Renderer.h"
#include "ECDI/Render/RecordingBackend.h"
#include "ECDI/Render/PaintContext.h"
#include "ECDI/Core/Point.h"
#include "ECDI/Core/Color.h"
#include "ECDI/Core/Font.h"
#include "ECDI/Core/UTF8.h"
#include <cmath>    // std::abs（浮点 epsilon 比较）
#include <utility>

using namespace ECDI;

// 浮点比较辅助（GPT 建议：避免浮点 == 直接比较）
constexpr float kEpsilon = 0.001f;
inline bool FloatEq(float a, float b) { return std::abs(a - b) < kEpsilon; }

namespace {

void TestRendererForwarding()
{
    // ── 4.5 原 #1：Command → Renderer → RecordingBackend ──
    RecordingBackend backend;
    Renderer renderer(backend);

    CommandBuffer commands;
    commands.emplace_back(DrawRectCommand{ Rect{ 0, 0, 100, 100 }, Color::Red() });
    commands.emplace_back(DrawRectCommand{ Rect{ 10, 20, 30, 40 }, Color::Gray() });

    renderer.Execute(commands);

    FRAMEWORK_ASSERT(backend.draws.size() == 2);
    FRAMEWORK_ASSERT(FloatEq(backend.draws[0].rect.x, 0.0f) && FloatEq(backend.draws[0].rect.width, 100.0f));
    FRAMEWORK_ASSERT(FloatEq(backend.draws[0].color.r, 1.0f) && FloatEq(backend.draws[0].color.a, 1.0f));
    FRAMEWORK_ASSERT(FloatEq(backend.draws[1].rect.x, 10.0f) && FloatEq(backend.draws[1].rect.y, 20.0f));
    FRAMEWORK_ASSERT(FloatEq(backend.draws[1].color.g, 0.5f));
}

void TestUTF8Utility()
{
    // ── 5.5.1.1 原 #6：UTF-8 工具自测 ──
    FRAMEWORK_ASSERT(EncodeUTF8(U'A') == "A");
    FRAMEWORK_ASSERT(EncodeUTF8(U'中') == "\xE4\xB8\xAD");
    FRAMEWORK_ASSERT(EncodeUTF8(U'😀') == "\xF0\x9F\x98\x80");

    const std::string s = "a中😀";
    FRAMEWORK_ASSERT(CodepointIndexToByteOffset(s, 0) == 0);
    FRAMEWORK_ASSERT(CodepointIndexToByteOffset(s, 1) == 1);
    FRAMEWORK_ASSERT(CodepointIndexToByteOffset(s, 2) == 4);
    FRAMEWORK_ASSERT(CodepointIndexToByteOffset(s, 3) == 8);
    FRAMEWORK_ASSERT(ByteOffsetToCodepointIndex(s, 4) == 2);
    FRAMEWORK_ASSERT(ByteOffsetToCodepointIndex(s, 8) == 3);
}

} // anonymous namespace

void ECDI::Test::RunRendererTests()
{
    TestRendererForwarding();
    TestUTF8Utility();
}
```

### 3.4 WidgetTests.cpp

```cpp
#include "RunAllTests.h"
#include "ECDI/Core/ECDIAssert.h"
#include "ECDI/Render/PaintContext.h"
#include "ECDI/Render/RecordingBackend.h"
#include "ECDI/Widget/Panel.h"
#include "ECDI/Widget/Label.h"
#include "ECDI/Widget/Button.h"
#include "ECDI/Widget/Widget.h"
#include "ECDI/Core/Point.h"
#include "ECDI/Core/Color.h"
#include <cmath>
#include <memory>
#include <utility>

using namespace ECDI;

constexpr float kEpsilon = 0.001f;
inline bool FloatEq(float a, float b) { return std::abs(a - b) < kEpsilon; }

namespace {

void TestPanelPaint()
{
    // ── 4.6 原 #2：Panel → PaintContext → DrawRectCommand ──
    RecordingBackend measurer;
    CommandBuffer commands;
    PaintContext ctx(commands, measurer);

    Panel panel;
    panel.SetPosition(10, 20);
    panel.SetSize(100, 50);
    panel.Paint(ctx, 0, 0);

    FRAMEWORK_ASSERT(commands.size() == 1);
    const auto& cmd = std::get<DrawRectCommand>(commands[0]);
    FRAMEWORK_ASSERT(FloatEq(cmd.rect.x, 10.0f) && FloatEq(cmd.rect.y, 20.0f));
    FRAMEWORK_ASSERT(FloatEq(cmd.rect.width, 100.0f) && FloatEq(cmd.rect.height, 50.0f));
    FRAMEWORK_ASSERT(FloatEq(cmd.color.r, 0.5f) && FloatEq(cmd.color.g, 0.5f));
}

void TestLabelPaint()
{
    // ── 5.2 原 #4：Label → PaintContext → DrawTextCommand ──
    RecordingBackend backend;
    CommandBuffer commands;
    PaintContext ctx(commands, backend);

    Label label("Hello ECDI");
    label.SetPosition(5, 5);
    label.SetSize(100, 30);
    label.Paint(ctx, 0, 0);

    FRAMEWORK_ASSERT(commands.size() == 1);
    const auto& cmd = std::get<DrawTextCommand>(commands[0]);
    FRAMEWORK_ASSERT(cmd.text == "Hello ECDI");
    FRAMEWORK_ASSERT(FloatEq(cmd.color.r, 0.0f));
    FRAMEWORK_ASSERT(FloatEq(cmd.pos.x, 5.0f));

    const float expectedY = 5.0f + (30.0f - backend.LineHeight(Font{})) / 2.0f;
    FRAMEWORK_ASSERT(FloatEq(cmd.pos.y, expectedY));
    FRAMEWORK_ASSERT(FloatEq(cmd.font.size, 14.0f));
}

void TestButtonPaint()
{
    // ── 5.3 原 #5：Button 先背景后文本 ──
    RecordingBackend backend;
    CommandBuffer commands;
    PaintContext ctx(commands, backend);

    Button button("OK");
    button.SetPosition(10, 10);
    button.SetSize(100, 40);
    button.Paint(ctx, 0, 0);

    FRAMEWORK_ASSERT(commands.size() == 2);
    const auto& bg = std::get<DrawRectCommand>(commands[0]);
    FRAMEWORK_ASSERT(FloatEq(bg.rect.x, 10.0f) && FloatEq(bg.rect.width, 100.0f));
    const auto& txt = std::get<DrawTextCommand>(commands[1]);
    FRAMEWORK_ASSERT(txt.text == "OK");
    FRAMEWORK_ASSERT(txt.color == Color::White());

    const float expectedX = 10.0f + (100.0f - backend.MeasureText(Font{}, "OK").width) / 2.0f;
    FRAMEWORK_ASSERT(FloatEq(txt.pos.x, expectedX));
    const float expectedY = 10.0f + (40.0f - backend.LineHeight(Font{})) / 2.0f;
    FRAMEWORK_ASSERT(FloatEq(txt.pos.y, expectedY));
}

void TestWidgetTree()
{
    // ── T5：Widget 树操作 ──

    // 正常添加
    {
        Widget parent;
        parent.AddChild(std::make_unique<Widget>());
        FRAMEWORK_ASSERT(parent.GetChildCount() == 1);
        FRAMEWORK_ASSERT(parent.GetChildAt(0)->GetParent() == &parent);
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
        FRAMEWORK_ASSERT(parent.GetChildAt(0) == c0);
        FRAMEWORK_ASSERT(parent.GetChildAt(1) == c1);
        FRAMEWORK_ASSERT(parent.GetChildAt(2) == c2);
    }

    // RemoveChild
    {
        Widget parent;
        auto child = std::make_unique<Widget>();
        auto* raw = child.get();
        parent.AddChild(std::move(child));
        auto removed = parent.RemoveChild(raw);
        FRAMEWORK_ASSERT(parent.GetChildCount() == 0);
        FRAMEWORK_ASSERT(removed.get() == raw);
        FRAMEWORK_ASSERT(removed->GetParent() == nullptr);
    }

    // GetChildAt 越界断言（FRAMEWORK_ASSERT 模式：断言终止）
    // 注意：此负面测试无法自动验证断言路径——仅验证正常路径不触发断言
    {
        Widget parent;
        parent.AddChild(std::make_unique<Widget>());
        auto* child = parent.GetChildAt(0);
        FRAMEWORK_ASSERT(child != nullptr);
    }

    // 防环：AddChild 拒绝已挂载的 child（前置条件检查——间接验证）
    {
        Widget parent1;
        Widget parent2;
        auto child = std::make_unique<Widget>();
        auto* raw = child.get();
        parent1.AddChild(std::move(child));
        FRAMEWORK_ASSERT(raw->GetParent() == &parent1);
    }
}

} // anonymous namespace

void ECDI::Test::RunWidgetTests()
{
    TestPanelPaint();
    TestLabelPaint();
    TestButtonPaint();
    TestWidgetTree();
}
```

### 3.5 LayoutTests.cpp

```cpp
#include "RunAllTests.h"
#include "ECDI/Core/ECDIAssert.h"
#include "ECDI/Widget/Panel.h"
#include "ECDI/Widget/Widget.h"
#include "ECDI/Layout/HorizontalLayout.h"
#include "ECDI/Layout/VerticalLayout.h"
#include <memory>
#include <utility>

using namespace ECDI;

namespace {

void TestHorizontalLayout()
{
    // ── Phase 6 原 #8：HorizontalLayout（迁移）──

    // 测试 1：不同宽度累加
    {
        Panel panel;
        panel.SetSize(300, 50);
        panel.SetLayout(std::make_unique<HorizontalLayout>());

        auto box1 = std::make_unique<Widget>();
        box1->SetSize(100, 30);
        auto box2 = std::make_unique<Widget>();
        box2->SetSize(80, 30);
        auto box3 = std::make_unique<Widget>();
        box3->SetSize(60, 30);
        auto* b1 = box1.get();
        auto* b2 = box2.get();
        auto* b3 = box3.get();

        panel.AddChild(std::move(box1));
        panel.AddChild(std::move(box2));
        panel.AddChild(std::move(box3));
        panel.Arrange();

        FRAMEWORK_ASSERT(b1->GetX() == 0    && b1->GetY() == 0);
        FRAMEWORK_ASSERT(b2->GetX() == 100  && b2->GetY() == 0);
        FRAMEWORK_ASSERT(b3->GetX() == 180  && b3->GetY() == 0);

        panel.Arrange();
        FRAMEWORK_ASSERT(b1->GetX() == 0 && b2->GetX() == 100 && b3->GetX() == 180);
    }

    // 测试 2：超出父容器
    {
        Panel panel;
        panel.SetSize(200, 50);
        panel.SetLayout(std::make_unique<HorizontalLayout>());

        auto box1 = std::make_unique<Widget>();
        box1->SetSize(100, 30);
        auto box2 = std::make_unique<Widget>();
        box2->SetSize(100, 30);
        auto box3 = std::make_unique<Widget>();
        box3->SetSize(100, 30);
        auto* b3 = box3.get();

        panel.AddChild(std::move(box1));
        panel.AddChild(std::move(box2));
        panel.AddChild(std::move(box3));
        panel.Arrange();

        FRAMEWORK_ASSERT(b3->GetX() == 200);
    }

    // 测试 3：边界——0 子控件 / 1 子控件
    {
        Panel empty;
        empty.SetSize(200, 50);
        empty.SetLayout(std::make_unique<HorizontalLayout>());
        empty.Arrange();

        Panel single;
        single.SetSize(200, 50);
        single.SetLayout(std::make_unique<HorizontalLayout>());
        auto box = std::make_unique<Widget>();
        box->SetSize(100, 30);
        auto* b = box.get();
        single.AddChild(std::move(box));
        single.Arrange();
        FRAMEWORK_ASSERT(b->GetX() == 0 && b->GetY() == 0);
    }
}

void TestVerticalLayout()
{
    // ── T6：VerticalLayout 补测 ──

    // 两个子节点
    {
        Panel panel;
        panel.SetSize(200, 100);
        panel.SetLayout(std::make_unique<VerticalLayout>());

        auto box1 = std::make_unique<Widget>();
        box1->SetSize(100, 30);
        auto box2 = std::make_unique<Widget>();
        box2->SetSize(100, 40);
        auto* b1 = box1.get();
        auto* b2 = box2.get();

        panel.AddChild(std::move(box1));
        panel.AddChild(std::move(box2));
        panel.Arrange();

        FRAMEWORK_ASSERT(b1->GetX() == 0 && b1->GetY() == 0);
        FRAMEWORK_ASSERT(b2->GetX() == 0 && b2->GetY() == 30);
    }

    // 幂等性
    {
        Panel panel;
        panel.SetSize(200, 100);
        panel.SetLayout(std::make_unique<VerticalLayout>());

        auto box1 = std::make_unique<Widget>();
        box1->SetSize(100, 30);
        auto box2 = std::make_unique<Widget>();
        box2->SetSize(100, 40);
        auto* b1 = box1.get();
        auto* b2 = box2.get();

        panel.AddChild(std::move(box1));
        panel.AddChild(std::move(box2));
        panel.Arrange();
        panel.Arrange();

        FRAMEWORK_ASSERT(b1->GetY() == 0);
        FRAMEWORK_ASSERT(b2->GetY() == 30);
    }

    // 0 子节点
    {
        Panel empty;
        empty.SetSize(200, 100);
        empty.SetLayout(std::make_unique<VerticalLayout>());
        empty.Arrange();
    }

    // 1 子节点
    {
        Panel single;
        single.SetSize(200, 100);
        single.SetLayout(std::make_unique<VerticalLayout>());
        auto box = std::make_unique<Widget>();
        box->SetSize(100, 50);
        auto* b = box.get();
        single.AddChild(std::move(box));
        single.Arrange();
        FRAMEWORK_ASSERT(b->GetX() == 0 && b->GetY() == 0);
    }

    // 不同高度累加
    {
        Panel panel;
        panel.SetSize(200, 200);
        panel.SetLayout(std::make_unique<VerticalLayout>());

        auto box1 = std::make_unique<Widget>();
        box1->SetSize(100, 20);
        auto box2 = std::make_unique<Widget>();
        box2->SetSize(100, 50);
        auto box3 = std::make_unique<Widget>();
        box3->SetSize(100, 30);
        auto* b1 = box1.get();
        auto* b2 = box2.get();
        auto* b3 = box3.get();

        panel.AddChild(std::move(box1));
        panel.AddChild(std::move(box2));
        panel.AddChild(std::move(box3));
        panel.Arrange();

        FRAMEWORK_ASSERT(b1->GetY() == 0);
        FRAMEWORK_ASSERT(b2->GetY() == 20);
        FRAMEWORK_ASSERT(b3->GetY() == 70);
    }
}

} // anonymous namespace

void ECDI::Test::RunLayoutTests()
{
    TestHorizontalLayout();
    TestVerticalLayout();
}
```

### 3.6 TextBoxTests.cpp

```cpp
#include "RunAllTests.h"
#include "ECDI/Core/ECDIAssert.h"
#include "ECDI/Widget/TextBox.h"

using namespace ECDI;

namespace {

void TestTextBoxInsertDelete()
{
    // ── 原 #7：TextBox 编辑逻辑（迁移 + 扩展）──
    // ⚠️ v1.2 实现回写：TextBox 构造后光标默认在开头（m_caret=0，不自动跳末尾）——
    // 所有"期望末尾行为"的测试必须显式 MoveCaretToEnd()（VS 实测暴露 5 处遗漏，已全部补齐）

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
        // DeleteForward 不需 MoveCaretToEnd：caret=0 时恰好删第 0 个字符（与 DeleteBackward 相反）
        TextBox box("x");
        box.DeleteForward();
        FRAMEWORK_ASSERT(box.GetText() == "");
        FRAMEWORK_ASSERT(box.GetCaret() == 0);
    }
}

} // anonymous namespace

void ECDI::Test::RunTextBoxTests()
{
    TestTextBoxInsertDelete();
    TestTextBoxCaretMovement();
    TestTextBoxGetSelection();
    TestTextBoxBoundary();
}
```

---

### 3.7 EventTests.cpp（P2，可选）

GPT 建议：删除 `EventType != 0` 和 `KeyModifier::Shift == 1` 等无意义测试（测试的是语言规则/实现细节，不是行为）。仅保留验证行为的测试（StaticType 一致性、IsShiftDown/IsCtrlDown 位检查）：

```cpp
#include "RunAllTests.h"
#include "ECDI/Core/ECDIAssert.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyDownEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyModifier.h"

using namespace ECDI;

namespace {

void TestKeyDownEventBehavior()
{
    // StaticType 一致性（传入 nullptr 作为 Window*——测试环境无窗口）
    {
        KeyDownEvent ev(nullptr, KeyCode::A, KeyModifier::None);
        FRAMEWORK_ASSERT(ev.GetType() == EventType::KeyDown);
        FRAMEWORK_ASSERT(ev.GetType() == KeyDownEvent::StaticType());
    }

    // 修饰键位检查：IsShiftDown / IsCtrlDown / IsAltDown 行为正确
    {
        KeyDownEvent evNone(nullptr, KeyCode::A, KeyModifier::None);
        FRAMEWORK_ASSERT(!evNone.IsShiftDown());
        FRAMEWORK_ASSERT(!evNone.IsCtrlDown());
        FRAMEWORK_ASSERT(!evNone.IsAltDown());
    }
    {
        KeyDownEvent evShift(nullptr, KeyCode::Left, KeyModifier::Shift);
        FRAMEWORK_ASSERT(evShift.IsShiftDown());
        FRAMEWORK_ASSERT(!evShift.IsCtrlDown());
    }
    {
        KeyDownEvent evCtrlShift(nullptr, KeyCode::Home, KeyModifier::Shift | KeyModifier::Ctrl);
        FRAMEWORK_ASSERT(evCtrlShift.IsShiftDown());
        FRAMEWORK_ASSERT(evCtrlShift.IsCtrlDown());
        FRAMEWORK_ASSERT(!evCtrlShift.IsAltDown());
    }
    {
        KeyDownEvent evAlt(nullptr, KeyCode::Tab, KeyModifier::Alt);
        FRAMEWORK_ASSERT(!evAlt.IsShiftDown());
        FRAMEWORK_ASSERT(evAlt.IsAltDown());
    }
}

} // anonymous namespace

void ECDI::Test::RunEventTests()
{
    TestKeyDownEventBehavior();
}
```

---

## 4. main.cpp 变更

### 4.1 变更内容

**移除** main.cpp 中以下 8 个断言块（第 69-291 行）：

- 第 69-85 行：#1 Renderer 转发
- 第 89-104 行：#2 Panel 绘制
- 第 108-127 行：#3 DrawText 转发
- 第 131-150 行：#4 Label 绘制
- 第 154-175 行：#5 Button 绘制
- 第 179-191 行：#6 UTF-8 工具
- 第 195-219 行：#7 TextBox 编辑
- 第 223-291 行：#8 HorizontalLayout

**替换为**：

```cpp
#ifdef _DEBUG
#include "src/Tests/RunAllTests.h"
#endif
```

> **v1.2 实现回写**：include 路径为 `src/Tests/RunAllTests.h` 而非 `Tests/RunAllTests.h`——main.cpp 位于 `ECDI/` 目录，相对 include 以本文件所在目录为基准，Tests 在 `ECDI/src/Tests/`（clangcl 实测 `Tests/RunAllTests.h` 报 file not found）。

并在 `wWinMain` 开头添加：

```cpp
#ifdef _DEBUG
    ECDI::Test::RunAllTests();
#endif
```

### 4.2 变更后 main.cpp 结构（开头）

```cpp
#include <Windows.h>

#include "ECDI/Window/Window.h"
#include "ECDI/Application/Application.h"
#include "ECDI/Core/Logger.h"
#include "ECDI/EventSystem/Input/KeyBoard/CharInputEvent.h"
#include "ECDI/Widget/Panel.h"
#include "ECDI/Widget/Label.h"
#include "ECDI/Widget/Button.h"
#include "ECDI/Widget/TextBox.h"
#include "ECDI/Layout/VerticalLayout.h"
#include "ECDI/Layout/HorizontalLayout.h"
#include "ECDI/Core/String.h"

#include <iostream>
#include <string>
#include <utility>

// ... DemoButton / DemoApplication 定义 ...

#ifdef _DEBUG
#include "src/Tests/RunAllTests.h"
#endif

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow)
{
#ifdef _DEBUG
    ECDI::Test::RunAllTests();
#endif

    DemoApplication application;
    // ... 窗口创建代码 ...
```

### 4.3 变更后头文件依赖变化

> **v1.2 实现回写**：以下列表为最终实现状态（与初稿差异：`UTF8.h` 不在移除列表——`DemoApplication::OnCharInput` 使用 `ECDI::EncodeUTF8`，保留）。

**移除的头文件**（随断言块一起移除）：
- `#include "ECDI/Core/Point.h"`
- `#include "ECDI/Core/ECDIAssert.h"`
- `#include "ECDI/Render/PaintContext.h"`
- `#include "ECDI/Render/Renderer.h"`
- `#include "ECDI/Render/RecordingBackend.h"`

**新增的头文件**：
- `#include "src/Tests/RunAllTests.h"`（仅 Debug）

**保留的头文件**：`#include "ECDI/Core/UTF8.h"`（`DemoApplication::OnCharInput` 使用 `ECDI::EncodeUTF8`）

---

## 5. 构建系统集成

### 5.1 CMake 变更

CMakeLists.txt 的 `file(GLOB_RECURSE SOURCES ...)` 已经使用 `CONFIGURE_DEPENDS`，新增 `.cpp` 会自动包含。

验证：当前模式：
```cmake
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/ECDI/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ECDI/main.cpp"
)
```

`src/Tests/*.cpp` 会被 `src/*.cpp` 递归匹配到，**无需修改 CMakeLists.txt**。

### 5.2 vcxproj 变更

> **v1.2 实现回写**：实际添加 5 个文件（EventTests.cpp 为 P2 未实现，不添加）。

vcxproj 使用静态文件列表（`<ClCompile Include="..."/>`），需要手动添加 5 个新文件：

```xml
<ClCompile Include="ECDI\src\Tests\RunAllTests.cpp" />
<ClCompile Include="ECDI\src\Tests\RendererTests.cpp" />
<ClCompile Include="ECDI\src\Tests\WidgetTests.cpp" />
<ClCompile Include="ECDI\src\Tests\LayoutTests.cpp" />
<ClCompile Include="ECDI\src\Tests\TextBoxTests.cpp" />
<!-- EventTests.cpp（P2，可选）——未实现，不添加 -->
```

同时将 main.cpp 的 `<ClCompile>` 保持不变（只改内容）。

---

## 6. 实现顺序（原子步骤）

```
步骤 1：修改 TextBox.h / TextBox.cpp（新增 GetSelection）
   - TextBox.h: #include <optional>, 新增 SelectionRange + GetSelection() 声明
   - TextBox.cpp: 新增 GetSelection() 实现
   - 编译验证

步骤 2：创建 RunAllTests.h / RunAllTests.cpp
   - 创建 Tests/ 目录
   - 创建 RunAllTests.h（声明）
   - 创建 RunAllTests.cpp（调用各 Test 函数）

步骤 3：创建测试文件并迁移现有断言块
   - RendererTests.cpp: 迁入 #1, #3, #6
   - WidgetTests.cpp: 迁入 #2, #4, #5
   - LayoutTests.cpp: 迁入 #8
   - TextBoxTests.cpp: 迁入 #7

步骤 4：新增测试块（补测）
   - TextBoxTests.cpp: 新增 TestTextBoxGetSelection
   - TextBoxTests.cpp: 新增 TestTextBoxInsertDelete（扩展原 #7）
   - TextBoxTests.cpp: 新增 TestTextBoxCaretMovement
   - TextBoxTests.cpp: 新增 TestTextBoxBoundary
   - WidgetTests.cpp: 新增 TestWidgetTree
   - LayoutTests.cpp: 新增 TestVerticalLayout

步骤 5：修改 main.cpp
   - 移除 8 个断言块
   - 新增 #ifdef _DEBUG #include "src/Tests/RunAllTests.h"
   - 在 wWinMain 开头新增 RunAllTests() 调用

步骤 6：编译 + 运行验证
   - Debug 模式：所有测试通过 + GUI 启动正常
   - Release 模式：测试被编译为空，GUI 启动正常

步骤 7：可选 T7（P2）
   - 创建 EventTests.cpp
   - RunAllTests.cpp 取消注释 TestEventConstruction() 调用

步骤 8：vcxproj 同步（如使用 vcxproj 而非 CMake）
   - 添加 6 个新文件到项目
```

---

## 7. 风险与缓解

| 风险 | 概率 | 缓解措施 |
|------|------|---------|
| TextBox.h 加 `#include <optional>` 引发编译警告 | 低 | C++17 起标准库组件，项目已 C++20，无问题 |
| `std::optional<SelectionRange>` 需要完整类型定义 | 低 | SelectionRange 是值类型，头文件中定义，析构平凡 |
| vcxproj 文件遗漏 | 中 | CMake 自动发现；vcxproj 手动添加时逐一核对 |
| main.cpp 移除断言块时误删 DemoApplication | 低 | 精确行号定位 + 编辑前 Read 确认 |
| `GetSelection()` 命名与 Win32 GetSelection 宏冲突 | **中** | Win32 无此宏（有 GetSelectionCaret 等）；但 `SelectObject` 等存在。**若冲突**：方法名改为 `GetSelectionRange()` |

---

## 8. 设计决策记录

| 决策 | 内容 |
|------|------|
| **D-Test-1** | 测试代码在 `src/Tests/` 下（与 GPT 确认），不在 `include/ECDI/` 下——转库时不暴露测试 API |
| **D-Test-2** | 每个模块暴露一个 `RunXxxTests()` 入口（`RunRendererTests` / `RunWidgetTests` / `RunLayoutTests` / `RunTextBoxTests` / `RunEventTests`），内部包含所有 Test 块声明——避免 `RunAllTests.cpp` 膨胀为几十个头文件声明 |
| **D-Test-3** | `GetSelection()` 是只读查询接口，不提供 `SetSelection()`——测试中无法构造"有选中区"的前置状态，Selection 交互逻辑归 Phase 10 集成测试 |
| **D-Test-4** | `SelectionRange` 是公开结构体（`TextBox` 内部定义）——供未来 Phase 10 集成测试直接使用 |
| **D-Test-5** | EventTests.cpp 标记为 P2（可选）——`KeyDownEvent` 构造需要 `Window*`（可传 `nullptr`），但优先级低于其他测试 |

---

## 9. 修订记录

- v1.2（2026-08-17）实现回写（测试通过后）：
  - §2.2 `GetSelection()` 返回类型必须完整限定 `TextBox::SelectionRange`（嵌套类型类外不可见）
  - §3.1/3.2 各模块入口 `RunXxxTests()` 声明上移 `RunAllTests.h`（各测试 cpp out-of-line 定义需声明可见）
  - §3.6 TextBoxTests 补 5 处 `MoveCaretToEnd()` 前置（TextBox 构造后光标默认开头；VS 实测暴露遗漏）
  - §4.1/4.2/4.3 main.cpp include 改为 `src/Tests/RunAllTests.h`（相对 main.cpp 目录）；UTF8.h 保留（DemoApplication 用 EncodeUTF8）
  - §5.2 vcxproj 实际添加 5 个文件（EventTests P2 未做）
- v1.1（2026-08-17）整合 GPT 评审：
  - 浮点比较改用 `FloatEq` epsilon 辅助函数（避免 `==` 直接比较）
  - EventTests 删除 `EventType != 0` 等无意义测试，仅保留行为测试
  - RunAllTests 改为每模块一个 `RunXxxTests()` 入口（避免声明膨胀）
  - GetSelection 接口标注"Phase 8+ 可能用于复制/剪切/序列化"（非纯测试接口）
- v1.0（2026-08-17）详细设计初稿
