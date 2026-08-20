# Phase 7.2 无窗口单元测试体系 初步设计

> 状态：v1.0（2026-08-17）｜初步设计
> 前序：职责确认（v1.1，2026-08-17，采纳 GPT 评审）
> 相关文档：phase7-testing-requirements.md（职责确认）、phase5-selection-detailed-design.md（P8 承诺）

## 1. 架构总览

### 1.1 文件结构

```
ECDI/src/
├── Tests/
│   ├── RunAllTests.h          ← RunAllTests() 声明
│   ├── RunAllTests.cpp        ← 统一入口，调用各模块测试块
│   ├── RendererTests.cpp      ← #1 (Renderer转发), #3 (DrawText转发)
│   ├── WidgetTests.cpp        ← #2 (Panel绘制), #4 (Label), #5 (Button), T5 (Widget树)
│   ├── LayoutTests.cpp        ← #8 (HorizontalLayout), T6 (VerticalLayout)
│   ├── TextBoxTests.cpp       ← #7 (现有编辑), T1 (纯编辑补测), T2 (光标迁移), T4 (边界)
│   └── EventTests.cpp         ← T7 (Event构造/KeyModifier, P2)
│
├── Application/
├── Window/
├── Widget/
├── Render/
├── Core/
├── Platform/
├── main.cpp                   ← 仅保留 #ifdef _DEBUG RunAllTests(); 和 GUI 启动
```

### 1.2 入口机制

```cpp
// RunAllTests.h
#pragma once
void RunAllTests();

// RunAllTests.cpp
#include "RunAllTests.h"
#include "RendererTests.h"   // 声明 TestRendererXxx 函数
#include "WidgetTests.h"
#include "LayoutTests.h"
#include "TextBoxTests.h"
// #include "EventTests.h"   // P2 可选

void RunAllTests()
{
    TestRendererForwarding();
    TestPanelPaintCommands();
    // ...
    TestTextBoxInsertDelete();
    TestTextBoxCaretMovement();
    TestTextBoxBoundary();
    TestWidgetTree();
    TestVerticalLayout();
    // TestEventConstruction();  // P2
}

// main.cpp
#ifdef _DEBUG
#include "Tests/RunAllTests.h"
#endif

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow)
{
#ifdef _DEBUG
    RunAllTests();
#endif
    // ... 现有 GUI 初始化代码 ...
    return application.Run();
}
```

### 1.3 测试块命名规范

每个测试块是独立函数，命名 `TestXxxXxx`，使用 `FRAMEWORK_ASSERT`：

```cpp
// ── TestTextBoxInsertDelete ──
void TestTextBoxInsertDelete()
{
    // InsertCodepoint at end
    {
        ECDI::TextBox box("abc");
        box.InsertCodepoint(U'd');
        FRAMEWORK_ASSERT(box.GetText() == "abcd");
        FRAMEWORK_ASSERT(box.GetCaret() == 4);
    }
    // InsertCodepoint in middle
    {
        ECDI::TextBox box("abc");
        box.MoveCaretToStart();
        box.MoveCaret(ECDI::TextBox::CaretDirection::Right);
        box.InsertCodepoint(U'X');
        FRAMEWORK_ASSERT(box.GetText() == "aXbc");
        FRAMEWORK_ASSERT(box.GetCaret() == 2);
    }
    // ...
}
```

**规范**：
- 每个测试用例一个代码块 `{ }`，独立命名空间
- `[PASS]` / `[FAIL]` 输出由调用的上层控制（RunAllTests 负责）
- 测试函数命名：`Test<模块><行为>`，如 `TestTextBoxCaretMovement`

## 2. 关键设计决策

### D1 测试框架：FRAMEWORK_ASSERT 延续

**决策**：延续 FRAMEWORK_ASSERT，不引入第三方框架。

**理由**：20-30 个测试，零新增依赖，已有 8 个断言块示范。Phase 10 转库时评估 Catch2。

### D2 文件拆分：独立 Tests/ 目录

**决策**：按 GPT 方案拆分，每个模块一个文件。

**迁移方式**：现有 main.cpp 中的 8 个断言块逐个迁移到对应的 `Tests/*.cpp`，不一次全搬。main.cpp 保留 `#include` 和 GUI 代码。

### D3 失败模式：一崩全崩延续

**决策**：当前 FRAMEWORK_ASSERT 遇失败即终止（`assert(false)`），延续此模式。

**理由**：7.2 测试量小，失败很容易定位——`[FAIL] TestTextBoxInsertDelete` 输出 + 行号 + 弹框。Phase 10 评估框架时再改。

### D4 窗口缺失的处理

**核心发现**：`InsertCodepoint` / `DeleteBackward` / `MoveCaret` 等 public 编辑方法在不挂载窗口时**可正常调用**：

| 方法 | 窗口依赖 | 处理方式 |
|------|---------|---------|
| `InsertCodepoint` | 内部调 `Invalidate()` → `GetWindow()` → null | 优雅空操作 |
| `DeleteBackward` | 同上 | 同上 |
| `MoveCaret` | 同上 | 同上 |
| `MoveCaretToStart/End` | 同上 | 同上 |
| `GetText()` / `GetCaret()` | 无 | 纯访问器 |
| `OnCharInput(Event&)` | Event 构造需要 `Window*` | 不可测（但逻辑等价于 `InsertCodepoint`） |
| `OnKeyDown(Event&)` | Event 构造需要 `Window*` | 不可测（但只是 switch 映射） |

**结论**：所有核心编辑逻辑**已通过 public API 可测试**。`OnCharInput` 和 `OnKeyDown` 的映射层（switch 语句和控制字符过滤）是薄薄一层，不测。

## 3. 各测试块设计

### 3.1 T1：TextBox 纯编辑操作补测（P0）

**文件**：`TextBoxTests.cpp`

**测试内容**（无选中区路径）：

| 测试用例 | 验证 |
|---------|------|
| InsertCodepoint 在末尾插入 | 文本正确 + 光标位置 |
| InsertCodepoint 在中间插入 | 文本正确 + 光标位置 |
| InsertCodepoint 在开头插入 | 文本正确 + 光标位置 |
| InsertCodepoint 空串插入 | 空串非空串 |
| DeleteBackward 删除末尾字符 | 文本正确 + 光标位置 |
| DeleteBackward 删除中间字符 | 文本正确 + 光标位置 |
| DeleteBackward 头边界（无操作） | 文本不变 + 光标归零 |
| DeleteForward 删除开头字符 | 文本正确 + 光标位置 |
| DeleteForward 删除中间字符 | 文本正确 + 光标位置 |
| DeleteForward 尾边界（无操作） | 文本不变 + 光标在末尾 |
| emoji 不切字 | 4 字节码点完整删除/插入 |
| 中文不切字 | 3 字节码点完整删除/插入 |

**代码示例**：

```cpp
void TestTextBoxInsertDelete()
{
    // ── InsertCodepoint ──
    {
        ECDI::TextBox box("abc");
        box.InsertCodepoint(U'd');
        FRAMEWORK_ASSERT(box.GetText() == "abcd");
        FRAMEWORK_ASSERT(box.GetCaret() == 4);
    }
    {
        ECDI::TextBox box("abc");
        box.MoveCaretToEnd();
        box.InsertCodepoint(U'X');
        FRAMEWORK_ASSERT(box.GetText() == "abcX");
        FRAMEWORK_ASSERT(box.GetCaret() == 4);
    }
    {
        ECDI::TextBox box("abc");
        box.MoveCaretToStart();             // caret=0
        box.InsertCodepoint(U'X');
        FRAMEWORK_ASSERT(box.GetText() == "Xabc");
        FRAMEWORK_ASSERT(box.GetCaret() == 1);
    }
    {
        ECDI::TextBox box("");               // 空串
        box.InsertCodepoint(U'a');
        FRAMEWORK_ASSERT(box.GetText() == "a");
        FRAMEWORK_ASSERT(box.GetCaret() == 1);
    }

    // ── DeleteBackward ──
    {
        ECDI::TextBox box("abc");
        box.MoveCaretToEnd();                // caret=3
        box.DeleteBackward();                // 删 'c'
        FRAMEWORK_ASSERT(box.GetText() == "ab");
        FRAMEWORK_ASSERT(box.GetCaret() == 2);
    }
    {
        ECDI::TextBox box("abc");
        box.MoveCaretToStart();              // caret=0
        box.DeleteBackward();                // 头边界，无操作
        FRAMEWORK_ASSERT(box.GetText() == "abc");
        FRAMEWORK_ASSERT(box.GetCaret() == 0);
    }

    // ── DeleteForward ──
    {
        ECDI::TextBox box("abc");
        box.MoveCaretToStart();              // caret=0
        box.DeleteForward();                 // 删 'a'
        FRAMEWORK_ASSERT(box.GetText() == "bc");
        FRAMEWORK_ASSERT(box.GetCaret() == 0);
    }
    {
        ECDI::TextBox box("abc");
        box.MoveCaretToEnd();                // caret=3
        box.DeleteForward();                 // 尾边界，无操作
        FRAMEWORK_ASSERT(box.GetText() == "abc");
        FRAMEWORK_ASSERT(box.GetCaret() == 3);
    }
}
```

### 3.2 T2：光标迁移补测（P0）

**文件**：`TextBoxTests.cpp`

**测试内容**：

| 测试用例 | 验证 |
|---------|------|
| MoveCaretLeft 右移 | 光标 -1 |
| MoveCaretLeft 头边界 | 光标归零不越界 |
| MoveCaretRight 右移 | 光标 +1 |
| MoveCaretRight 尾边界 | 光标在末尾不越界 |
| MoveCaretToStart | 光标归零 |
| MoveCaretToEnd | 光标在末尾 |
| 中文/emoji 光标移动 | 码点索引正确（不切字节） |

**代码示例**：

```cpp
void TestTextBoxCaretMovement()
{
    // ── MoveCaret ──
    {
        ECDI::TextBox box("abc");
        box.MoveCaretToEnd();
        box.MoveCaret(ECDI::TextBox::CaretDirection::Left);
        FRAMEWORK_ASSERT(box.GetCaret() == 2);
    }
    {
        ECDI::TextBox box("abc");
        box.MoveCaretToStart();
        box.MoveCaret(ECDI::TextBox::CaretDirection::Left);  // 边界
        FRAMEWORK_ASSERT(box.GetCaret() == 0);
    }
    {
        ECDI::TextBox box("abc");
        box.MoveCaretToStart();
        box.MoveCaret(ECDI::TextBox::CaretDirection::Right);
        FRAMEWORK_ASSERT(box.GetCaret() == 1);
    }
    {
        ECDI::TextBox box("abc");
        box.MoveCaretToEnd();
        box.MoveCaret(ECDI::TextBox::CaretDirection::Right);  // 边界
        FRAMEWORK_ASSERT(box.GetCaret() == 3);
    }

    // ── MoveCaretToStart/End ──
    {
        ECDI::TextBox box("abc");
        box.MoveCaretToEnd();   // 3
        box.MoveCaretToStart(); // 0
        FRAMEWORK_ASSERT(box.GetCaret() == 0);
    }
    {
        ECDI::TextBox box("abc");
        box.MoveCaretToEnd();   // 3
        FRAMEWORK_ASSERT(box.GetCaret() == 3);
    }
}
```

### 3.3 T3：GetSelection 查询接口（P0）

**变更文件**：
- `TextBox.h`：新增 `SelectionRange` 结构体和 `GetSelection()` 声明
- `TextBox.cpp`：新增 `GetSelection()` 实现
- `TextBoxTests.cpp`：测试

#### 接口设计

```cpp
// TextBox.h（public 区域）
struct SelectionRange {
    size_t start;
    size_t end;
    // 保证 start <= end
};

class TextBox : public TextWidget {
public:
    // ... 现有接口 ...

    /// @brief 获取当前选中区（如果存在）
    /// @return 无选中区 → nullopt；有选中区 → SelectionRange{min, max}
    std::optional<SelectionRange> GetSelection() const;

    // ...
private:
    size_t m_selectionAnchor;
    size_t m_caretPosition;
    // ...
};
```

#### 实现

```cpp
// TextBox.cpp
std::optional<SelectionRange> TextBox::GetSelection() const
{
    if (m_selectionAnchor == m_caret) {
        return std::nullopt;
    }
    return SelectionRange{
        (std::min)(m_selectionAnchor, m_caret),
        (std::max)(m_selectionAnchor, m_caret)
    };
}
```

#### 测试

```cpp
void TestTextBoxGetSelection()
{
    // 初始无选中区
    {
        ECDI::TextBox box("hello");
        auto sel = box.GetSelection();
        FRAMEWORK_ASSERT(!sel.has_value());
    }

    // 编辑后无选中区（ClearSelection 自动调用）
    {
        ECDI::TextBox box("hello");
        box.InsertCodepoint(U'X');
        auto sel = box.GetSelection();
        FRAMEWORK_ASSERT(!sel.has_value());
    }
    {
        ECDI::TextBox box("hello");
        box.MoveCaretToEnd();
        auto sel = box.GetSelection();
        FRAMEWORK_ASSERT(!sel.has_value());
    }
    {
        ECDI::TextBox box("hello");
        box.DeleteBackward();  // 空串无操作，无选中区
        auto sel = box.GetSelection();
        FRAMEWORK_ASSERT(!sel.has_value());
    }
}
```

### 3.4 T4：TextBox 边界条件（P1）

**文件**：`TextBoxTests.cpp`

**测试内容**：

| 测试用例 | 验证 |
|---------|------|
| 空串 InsertCodepoint | 从空串插入 |
| 空串 DeleteBackward/Forward | 无操作 |
| emoji 混合编辑 | 4 字节码点完整处理 |
| 中文混合编辑 | 3 字节码点完整处理 |
| 单字符串编辑 | 边界最小情况 |

**代码示例**：

```cpp
void TestTextBoxBoundary()
{
    // 空串
    {
        ECDI::TextBox box("");
        box.InsertCodepoint(U'a');
        FRAMEWORK_ASSERT(box.GetText() == "a");
        FRAMEWORK_ASSERT(box.GetCaret() == 1);
    }
    {
        ECDI::TextBox box("");
        box.DeleteBackward();  // 空串无操作
        FRAMEWORK_ASSERT(box.GetText() == "");
        FRAMEWORK_ASSERT(box.GetCaret() == 0);
    }
    {
        ECDI::TextBox box("");
        box.DeleteForward();   // 空串无操作
        FRAMEWORK_ASSERT(box.GetText() == "");
        FRAMEWORK_ASSERT(box.GetCaret() == 0);
    }

    // emoji/中文混合
    {
        ECDI::TextBox box("中🎉a");
        box.MoveCaretToEnd();
        box.InsertCodepoint(U'X');
        FRAMEWORK_ASSERT(box.GetText() == "中🎉aX");
        FRAMEWORK_ASSERT(box.GetCaret() == 4);
    }
    {
        ECDI::TextBox box("中🎉a");
        box.MoveCaretToEnd();  // caret=3
        box.DeleteBackward();  // 删 'a'
        FRAMEWORK_ASSERT(box.GetText() == "中🎉");
        FRAMEWORK_ASSERT(box.GetCaret() == 2);
    }

    // 单字符
    {
        ECDI::TextBox box("x");
        box.DeleteBackward();
        FRAMEWORK_ASSERT(box.GetText() == "");
        FRAMEWORK_ASSERT(box.GetCaret() == 0);
    }
}
```

### 3.5 T5：Widget 树操作（P1）

**文件**：`WidgetTests.cpp`

**测试内容**：

| 测试用例 | 验证 |
|---------|------|
| AddChild 正常添加 | 父子关系正确 |
| AddChild 非空检查 | 传入 nullptr 断言失败 |
| AddChild 无父检查 | 已挂载的 child 拒绝二次添加 |
| AddChild 防环检查 | 检测并拒绝环形引用 |
| RemoveChild 正常移除 | 返回正确 + 父指针清除 |
| RemoveChild 非子节点 | 断言失败 |
| GetChildAt 越界 | 断言失败 |
| 多子节点顺序 | 索引顺序与添加顺序一致 |

**代码示例**：

```cpp
void TestWidgetTree()
{
    // 正常添加
    {
        ECDI::Widget parent;
        ECDI::Widget child;
        parent.AddChild(std::make_unique<ECDI::Widget>());
        FRAMEWORK_ASSERT(parent.GetChildCount() == 1);
        FRAMEWORK_ASSERT(parent.GetChildAt(0)->GetParent() == &parent);
    }

    // 防环
    {
        ECDI::Widget grandparent;
        ECDI::Widget parent;
        ECDI::Widget child;
        grandparent.AddChild(std::make_unique<ECDI::Widget>());
        // 需要提取出指针来测试防环
        // 由于 AddChild 内 FRAMEWORK_ASSERT 会终止，防环测试依赖"断言成功"路径
        // 实际测试中：验证 AddChild 不会产生环 → 正常路径
    }
}
```

**注意**：`AddChild` 内使用 `FRAMEWORK_ASSERT` 进行前置条件检查，失败的断言会终止程序。所以"非空检查失败"、"无父检查失败"、"防环检查失败"这些**负面测试**在 FRAMEWORK_ASSERT 模式下无法自动验证（断言即终止）。这些测试只能通过**人工检查代码确认**（AddChild 第 57-59 行前置条件完备），不写自动化断言。

### 3.6 T6：Layout 补测（P1）

**文件**：`LayoutTests.cpp`

**测试内容**：

| 测试用例 | 验证 |
|---------|------|
| VerticalLayout 两个子节点 | 垂直累加 y 坐标正确 |
| VerticalLayout 三个子节点 | 幂等性 + 累加正确 |
| VerticalLayout 0 子节点 | 不崩溃 |
| VerticalLayout 1 子节点 | 归零位 |
| VerticalLayout 不同高度 | 各子高度独立累加 |

**代码示例**：

```cpp
void TestVerticalLayout()
{
    // 两个子节点
    {
        ECDI::Panel panel;
        panel.SetSize(200, 100);
        panel.SetLayout(std::make_unique<ECDI::VerticalLayout>());

        auto box1 = std::make_unique<ECDI::Widget>();
        box1->SetSize(100, 30);
        auto box2 = std::make_unique<ECDI::Widget>();
        box2->SetSize(100, 40);
        auto* b1 = box1.get();
        auto* b2 = box2.get();

        panel.AddChild(std::move(box1));
        panel.AddChild(std::move(box2));
        panel.Arrange();

        FRAMEWORK_ASSERT(b1->GetX() == 0 && b1->GetY() == 0);
        FRAMEWORK_ASSERT(b2->GetX() == 0 && b2->GetY() == 30);  // y=0 + height=30
    }

    // 幂等性
    {
        ECDI::Panel panel;
        panel.SetSize(200, 100);
        panel.SetLayout(std::make_unique<ECDI::VerticalLayout>());

        auto box1 = std::make_unique<ECDI::Widget>();
        box1->SetSize(100, 30);
        auto box2 = std::make_unique<ECDI::Widget>();
        box2->SetSize(100, 40);
        auto* b1 = box1.get();
        auto* b2 = box2.get();

        panel.AddChild(std::move(box1));
        panel.AddChild(std::move(box2));
        panel.Arrange();
        panel.Arrange();  // 第二次 Arrange

        FRAMEWORK_ASSERT(b1->GetY() == 0);
        FRAMEWORK_ASSERT(b2->GetY() == 30);
    }

    // 0 子节点
    {
        ECDI::Panel empty;
        empty.SetSize(200, 100);
        empty.SetLayout(std::make_unique<ECDI::VerticalLayout>());
        empty.Arrange();  // 不崩溃
    }

    // 1 子节点
    {
        ECDI::Panel single;
        single.SetSize(200, 100);
        single.SetLayout(std::make_unique<ECDI::VerticalLayout>());
        auto box = std::make_unique<ECDI::Widget>();
        box->SetSize(100, 50);
        auto* b = box.get();
        single.AddChild(std::move(box));
        single.Arrange();
        FRAMEWORK_ASSERT(b->GetX() == 0 && b->GetY() == 0);
    }
}
```

### 3.7 T7：Event 构造补测（P2）

**文件**：`EventTests.cpp`

**测试内容**：

| 测试用例 | 验证 |
|---------|------|
| KeyModifier 位运算 | None | Shift | Ctrl | Alt 组合值正确 |
| EventType 枚举值 | 各类型值非零且唯一 |
| StaticType() 一致性 | GetType() == StaticType() |

**注意**：Event 构造需要 `Window*`，但 Event 的 `GetType()` 和 `StaticType()` 不依赖窗口——纯虚函数查询。Event 对象可以在测试中构造（传入 `nullptr` 作为 Window*）。

**代码示例**：

```cpp
void TestEventConstruction()
{
    // KeyModifier 位运算
    {
        ECDI::KeyModifier m = ECDI::KeyModifier::None;
        FRAMEWORK_ASSERT(static_cast<int>(m) == 0);

        m = ECDI::KeyModifier::Shift;
        FRAMEWORK_ASSERT(static_cast<int>(m) == 1);

        m = ECDI::KeyModifier::Ctrl;
        FRAMEWORK_ASSERT(static_cast<int>(m) == 2);

        m = ECDI::KeyModifier::Alt;
        FRAMEWORK_ASSERT(static_cast<int>(m) == 4);

        m = ECDI::KeyModifier::Shift | ECDI::KeyModifier::Ctrl;
        FRAMEWORK_ASSERT(static_cast<int>(m) == 3);
    }

    // EventType 枚举值
    {
        FRAMEWORK_ASSERT(static_cast<int>(ECDI::EventType::KeyDown) != 0);
        FRAMEWORK_ASSERT(static_cast<int>(ECDI::EventType::KeyUp) != 0);
        FRAMEWORK_ASSERT(static_cast<int>(ECDI::EventType::CharInput) != 0);
    }

    // StaticType 一致性
    {
        ECDI::KeyDownEvent ev(nullptr, ECDI::KeyCode::A, ECDI::KeyModifier::None);
        FRAMEWORK_ASSERT(ev.GetType() == ECDI::EventType::KeyDown);
        FRAMEWORK_ASSERT(ev.GetType() == ECDI::KeyDownEvent::StaticType());

        // 修饰键查询
        FRAMEWORK_ASSERT(!ev.IsShiftDown());
        FRAMEWORK_ASSERT(!ev.IsCtrlDown());
        FRAMEWORK_ASSERT(!ev.IsAltDown());

        ECDI::KeyDownEvent evShift(nullptr, ECDI::KeyCode::Left, ECDI::KeyModifier::Shift);
        FRAMEWORK_ASSERT(evShift.IsShiftDown());
    }
}
```

## 4. 实现计划

### 4.1 实现顺序

```
第 1 步：T3 GetSelection 查询接口（P0）
  - 修改 TextBox.h：新增 SelectionRange 结构体 + GetSelection() 声明
  - 修改 TextBox.cpp：新增 GetSelection() 实现（7 行）
  - 编译验证

第 2 步：T8 测试基础设施（P0）
  - 创建 Tests/ 目录
  - 创建 RunAllTests.h / RunAllTests.cpp
  - 创建 TextBoxTests.cpp（迁入现有 #7 断言块 + 新增 T1/T2/T3/T4）
  - 创建 WidgetTests.cpp（迁入现有 #2, #4, #5 + 新增 T5）
  - 创建 LayoutTests.cpp（迁入现有 #8 + 新增 T6）
  - 创建 RendererTests.cpp（迁入现有 #1, #3）
  - 修改 main.cpp：替换原位断言块为 #include + RunAllTests()
  - 编译验证

第 3 步：T1 TextBox 纯编辑操作补测（P0）
  - 在 TextBoxTests.cpp 中新增 TestTextBoxInsertDelete()
  - 编译 + 运行验证

第 4 步：T2 光标迁移补测（P0）
  - 在 TextBoxTests.cpp 中新增 TestTextBoxCaretMovement()
  - 编译 + 运行验证

第 5 步：T4 TextBox 边界条件（P1）
  - 在 TextBoxTests.cpp 中新增 TestTextBoxBoundary()
  - 编译 + 运行验证

第 6 步：T5 Widget 树操作（P1）
  - 在 WidgetTests.cpp 中新增 TestWidgetTree()
  - 编译 + 运行验证

第 7 步：T6 Layout 补测（P1）
  - 在 LayoutTests.cpp 中新增 TestVerticalLayout()
  - 编译 + 运行验证

第 8 步：T7 Event 构造补测（P2，可选）
  - 创建 EventTests.cpp
  - 编译 + 运行验证
```

### 4.2 迁移策略（现有 main.cpp 断言块 → Tests/）

| 现有块 | 目标文件 | 迁移方式 |
|--------|---------|---------|
| #1 Renderer 转发 | RendererTests.cpp | 直接迁移，内容不变 |
| #2 Panel 绘制 | WidgetTests.cpp | 直接迁移 |
| #3 DrawText 转发 | RendererTests.cpp | 与 #1 合并 |
| #4 Label 绘制 | WidgetTests.cpp | 与 #2 合并 |
| #5 Button 绘制 | WidgetTests.cpp | 与 #2 合并 |
| #6 UTF-8 工具 | RendererTests.cpp 或 CoreTests.cpp | 归属 Renderer 工具层 |
| #7 TextBox 编辑 | TextBoxTests.cpp | 直接迁移 + 新增 T1/T2/T4 |
| #8 HorizontalLayout | LayoutTests.cpp | 直接迁移 + 新增 T6 |

## 5. 风险与缓解

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| `MoveCaret(Left/Right)` 调用了 `SyncTextInputCaret()` 内部 `GetWindow()` → nullptr 时可能崩溃 | 测试失败 | 代码审查确认 `GetWindow()` 返回 nullptr 时 `SyncTextInputCaret()` 内部有 `if (Window* w = GetWindow())` 保护，安全 |
| `Invalidate()` 内部 `GetWindow()` → nullptr 时可能崩溃 | 同上 | 代码审查确认 `if (Window* w = GetWindow())` 保护 |
| `FRAMEWORK_ASSERT` 失败即终止，多测试块无法全部运行 | 调试效率低 | 7.2 测试量小，失败容易定位；Phase 10 评估框架 |
| 测试文件拆分后 `main.cpp` 的 #include 路径变化 | 编译失败 | 提前规划路径（`src/Tests/` vs `Tests/`），使用相对路径 `include "Tests/RunAllTests.h"` |

## 6. 修订记录

- v1.0（2026-08-17）初步设计初稿