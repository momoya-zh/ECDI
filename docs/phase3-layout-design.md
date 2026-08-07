# Layout 详细设计（phase3-layout-design.md）

> 阶段：第三阶段 Widget System → Layout 子模块 → 详细设计（五阶段法第 3 步）
> 前置：Layout 职责确认 + 初步设计已评审通过（决策表 21 项）
> 本文档拍板详细设计层面的事项，供评审后进入实现。

---

## 0. 目标与范围（第一版）

实现 Widget 布局的最小闭环：

- Widget 可持有布局策略（`std::unique_ptr<Layout>`）
- `VerticalLayout`：垂直排列已有尺寸的子 Widget
- 布局入口：`window.GetRootWidget().Arrange()`（与 Dispatch 同构）
- 递归：父先排、子随后
- 触发：显式调用，不自动（无 dirty/通知系统）

明确不做：Margin / Padding / Stretch / SizePolicy / 自动测量 / 居中右对齐 / 自动触发布局 / 自动重绘。

---

## 1. 文件结构

```
ECDI/include/ECDI/Layout/
    Layout.h            ← 布局策略抽象基类
    VerticalLayout.h    ← 第一版布局实现

ECDI/src/Layout/
    Layout.cpp          ← 纯虚基类（可无实现，占位）
    VerticalLayout.cpp  ← Arrange 实现
```

include 关系：

- `Layout.h`：只 `#pragma once` + `class Widget;` 前向声明（Arrange 参数是引用，无需完整类型）
- `VerticalLayout.h`：`#include "Layout.h"`
- `Widget.h`：`class Layout;` 前向声明 + 新增 `std::unique_ptr<Layout> m_layout`
- `Widget.cpp`：`#include "ECDI/Layout/Layout.h"`（析构函数需要完整类型）

无循环包含：Widget.h ↔ Layout.h 都是前向声明，实现在 cpp 层汇合。

---

## 2. 类设计

### 2.1 Layout 基类

```cpp
// ECDI/include/ECDI/Layout/Layout.h
#pragma once

class Widget;

/// @brief 布局策略抽象基类
/// @details
/// 纯策略对象：不绑定平台资源、不参与树所有权、不持有 Widget。
/// 生命周期由持有它的 Widget 通过 unique_ptr 管理。
/// 不禁止拷贝/移动（非资源类，无地址绑定约束），但实际由 unique_ptr 独占持有。
class Layout
{
public:
    virtual ~Layout() = default;

    /// @brief 根据规则排列 parent 的子 Widget
    /// @param parent 容器 Widget（可访问其 children 与 Geometry）
    virtual void Arrange(Widget& parent) = 0;
};
```

### 2.2 VerticalLayout

```cpp
// ECDI/include/ECDI/Layout/VerticalLayout.h
#pragma once
#include "ECDI/Layout/Layout.h"

/// @brief 垂直布局：子 Widget 从上到下依次排列
/// @details
/// 规则（第一版）：
/// - x = 0（Local Coordinate，父内容区左侧起点）
/// - y = 前一个子 Widget 的 bottom（从 0 累加）
/// - width / height 保持子 Widget 原值（尺寸外部提供，Layout 不修改）
class VerticalLayout : public Layout
{
public:
    void Arrange(Widget& parent) override;
};
```

```cpp
// ECDI/src/Layout/VerticalLayout.cpp
#include "ECDI/Layout/VerticalLayout.h"
#include "ECDI/Widget/Widget.h"

void VerticalLayout::Arrange(Widget& parent)
{
    int currentY = 0;

    const size_t count = parent.GetChildCount();

    for (size_t i = 0; i < count; ++i)
    {
        Widget* child = parent.GetChildAt(i);

        child->SetPosition(0, currentY);   // local x=0，y 累加

        currentY += child->GetHeight();    // 未设置尺寸的子 Widget 高度为 0，y 不推进
    }
}
```

行为要点：

- **幂等**：每次从 `currentY = 0` 重算，重复调用结果一致
- **0x0 子 Widget**：y 不推进，后续兄弟原地重叠——预期行为（尺寸外部提供策略的推论）
- **不可见子 Widget**：第一版不跳过（`IsVisible()` 不参与判断），列为后续增强
- 不触碰 `child->SetSize()`，width/height 原样保留

---

## 3. Widget 改造

### 3.1 头文件（`Widget.h`）

新增前向声明（放在现有事件类前向声明附近）：

```cpp
class Layout;
```

新增 public 接口：

```cpp
// ── Layout ────────────────────────────────────────

/// @brief 安装布局策略（转移所有权）
/// @param layout 布局策略；nullptr 表示移除布局
void SetLayout(std::unique_ptr<Layout> layout);

/// @brief 排列自身子树（布局入口，约定只能从 RootWidget 调用）
/// @details
/// 与 HitTest 相同的架构约定：外部只从根 Widget 触发；
/// 禁止对非根 Widget 调用（如 button.Arrange()），此约束为软约束。
void Arrange();
```

新增 private 成员：

```cpp
std::unique_ptr<Layout> m_layout;   ///< 布局策略（非拥有性：不持有 children）
```

### 3.2 析构函数调整（关键实现细节）

现状：`virtual ~Widget() = default;`（头文件内联）

改造：**声明式析构，定义移入 Widget.cpp**：

```cpp
// Widget.h
virtual ~Widget();     // 不再 = default 内联

// Widget.cpp
#include "ECDI/Layout/Layout.h"
Widget::~Widget() = default;   // 此处 Layout 是完整类型，unique_ptr 可正确析构
```

原因：`std::unique_ptr<Layout>` 成员析构需要 Layout 完整类型，而 Widget.h 只前向声明 Layout。内联 `= default` 会在每个包含 Widget.h 的编译单元生成析构点，导致编译错误。

### 3.3 新增 GetChildAt（Layout 的前置依赖）

```cpp
// Widget.h（public，内联）
Widget* GetChildAt(size_t index) noexcept { return m_children[index].get(); }
const Widget* GetChildAt(size_t index) const noexcept { return m_children[index].get(); }
```

不暴露 `std::vector<std::unique_ptr<Widget>>&`：防外部破坏树结构、防所有权泄露。Layout 只通过 `GetChildCount() + GetChildAt()` 遍历（与 Paint/HitTest 共用同一 children 序）。

---

## 4. 布局入口与递归

### 4.1 调用链

```
Window::WindowProc (WM_SIZE)
   ↓ m_rootWidget->SetSize(...)          ← 已有，Window.cpp:116-120
   ↓ 派发 WindowResizedEvent             ← 已有
Application::OnWindowResized(event)
   ↓ event.GetWindow()->GetRootWidget().Arrange()   ← 新增（本子模块）
Widget::Arrange()                         ← public 入口
   ↓ ArrangeInternal()                    ← private 递归
Layout::Arrange(*this)                    ← 若 m_layout 非空
   ↓ for each child: child->ArrangeInternal()
```

### 4.2 Widget 递归实现（Widget.cpp）

```cpp
void Widget::Arrange()
{
    ArrangeInternal();
}

void Widget::ArrangeInternal()
{
    if (m_layout)
    {
        m_layout->Arrange(*this);   // 父先排
    }

    for (auto& child : m_children)
    {
        child->ArrangeInternal();   // 子随后（递归）
    }
}
```

`ArrangeInternal` 为 private：C++ 私有成员可被同类其他实例调用，递归在 Widget 内部完成，外部不可见。

**语义注记**：当前为父先、子后（pre-order）。第一版尺寸外部提供，顺序无影响；未来若引入自动测量（Label 按文字算尺寸），需改为后序或两遍遍历——届时再设计。

### 4.3 入口约束

- `Arrange()` 是 public（无类型隔离手段），禁止非根调用的约束与 `HitTest()` 同款：**架构约定，软约束**
- 不新建 RootWidget 类（项目无此类型，根是普通 Widget 实例）

---

## 5. 触发链路（写死）

第一版唯一自动触发点：**窗口大小变化**。

```
WM_SIZE → Window.cpp 同步 RootWidget 尺寸 → WindowResizedEvent
        → Application::OnWindowResized → GetRootWidget().Arrange()
```

`Application::OnWindowResized` 是 EventRouter 的既有 override，此处加一行调用即可：

```cpp
void Application::OnWindowResized(const WindowResizedEvent& event)
{
    event.GetWindow()->GetRootWidget().Arrange();
}
```

其余场景（AddChild / SetSize / SetVisible 后）**不自动触发**，由应用代码显式调用 `GetRootWidget().Arrange()`。此行为写入使用约定，避免"改了树没重排"的困惑。

---

## 6. 决策点汇总（本阶段拍板）

| # | 决策 | 结论 |
|---|------|------|
| 1 | Layout 基类形态 | 纯虚策略基类，`Arrange(Widget&)` |
| 2 | Widget 持有方式 | `std::unique_ptr<Layout> m_layout` |
| 3 | Widget 析构 | 改声明式，定义移入 Widget.cpp |
| 4 | children 遍历 | 新增 `GetChildAt()`（const 双版本），不暴露 vector |
| 5 | 布局入口 | `GetRootWidget().Arrange()`，软约束禁止非根调用 |
| 6 | 递归顺序 | 父先子后（pre-order），含未来 Measure 注记 |
| 7 | VerticalLayout 规则 | x=0 / y 累加 / width、height 保持原值 |
| 8 | 0x0 子 Widget | y 不推进，允许重叠（预期行为） |
| 9 | 不可见子 Widget | 不跳过（后续增强） |
| 10 | 自动触发 | 仅 WM_SIZE 链路；其余显式调用 |
| 11 | GetLayout() | 第一版不实现（YAGNI，无消费方） |

---

## 7. 实现约束（编码规范）

- 新文件含中文注释：**保存为 UTF-8 with BOM 或 GBK**，避免 MSVC 936 代码页下 C4819 警告（与项目现有文件一致）
- 头文件名不与标准库同名（Layout.h / VerticalLayout.h 无冲突）
- 不修改 main.cpp（测试/找 bug 入口，由用户维护）
- 资源类禁复制禁移动规则**不适用**于 Layout（非资源类），但 Widget 自身禁复制禁移动保持

---

## 8. 测试计划（接入验证）

由用户在 main.cpp（或临时测试入口）验证，预期结果：

| 用例 | 步骤 | 预期 |
|------|------|------|
| T1 单层垂直排列 | Panel 挂 VerticalLayout，3 个 child 分别 SetSize(100,50)/(100,30)/(100,50) 后 Arrange | y 依次 0/50/80，x 均为 0，尺寸不变 |
| T2 幂等 | 连续调 Arrange 两次 | 结果与第一次一致 |
| T3 未设尺寸 | 中间插一个未 SetSize 的 child | 该 child 0x0，y 不推进，后续兄弟位置前移重叠 |
| T4 嵌套递归 | 外层 Panel 垂直排两个内层 Panel，内层各挂 VerticalLayout 排 Button | 外层先排内层容器 y，内层再排 Button |
| T5 无布局 Widget | 某容器不 SetLayout | 只递归 children，自身不排（children 保持原坐标） |
| T6 Resize 触发 | 拖拽窗口改变大小 | RootWidget 尺寸更新 + 布局自动重排（T1 结果按新尺寸重现） |

验证方式：临时在 Arrange 后读取 `child->GetX()/GetY()/GetWidth()/GetHeight()` 并 Logger 输出比对。

---

## 9. 待最终确认项（提交评审）

1. `Application::OnWindowResized` 里加 Arrange 调用——是否同意（默认触发点）？
2. `GetChildAt()` 放 public 内联——是否有异议？
3. Widget 析构改声明式（移入 cpp）——是否同意（unique_ptr 成员的必要调整）？
4. VerticalLayout 不跳过不可见子 Widget——确认接受？
5. 目录 `ECDI/include/ECDI/Layout/` + `ECDI/src/Layout/`——命名与位置是否认可？

确认后进入实现。
