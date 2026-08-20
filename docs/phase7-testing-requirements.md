# Phase 7.2 无窗口单元测试体系 职责确认

> 状态：v1.1（2026-08-17）｜职责确认
> 前序：Phase 7.1 平台抽象全部完成（7.1.1-7.1.5，2026-08-16）
> 来源：roadmap-deferred.md §1.2 + 5.5.2 详细设计 P8（GPT 要求②"不彻底放弃断言"）
> 相关文档：phase5-selection-detailed-design.md（P8 承诺）、phase5-textbox-detailed-design.md（5.5.1.3 测试块）、README.md
> GPT 评审：2026-08-17 在线评价（核心洞察：Selection 不可测试暴露的是模型缺失，不是接口缺失）

## 1. 背景

### 1.1 当前测试现状

ECDI 从 Phase 4 起，在 main.cpp 的 GUI 窗口启动前，以**断言块**形式嵌入了一系列单元测试：

| 断言块 | 来源 | 测试内容 |
|--------|------|---------|
| #1 | 4.5 | Renderer → RecordingBackend（DrawRect 转发原样性） |
| #2 | 4.6 | Widget → PaintContext → CommandBuffer（Panel 命令断言） |
| #3 | 5.1 | DrawTextCommand → Renderer → RecordingBackend（文本转发原样性） |
| #4 | 5.2 | Label → PaintContext → DrawTextCommand（文本消费者） |
| #5 | 5.3 | Button 绘制顺序（先背景后文本，垂直水平居中） |
| #6 | 5.5.1.1 | UTF-8 工具自测（码点→字节双向转换） |
| #7 | 5.5.1.3 | TextBox 编辑逻辑（Insert/Delete/Move，emoji/中文不切字） |
| #8 | Phase 6 | HorizontalLayout 坐标计算（1/2/3 子，幂等，边界，超出自查） |

**工具链**：
- `FRAMEWORK_ASSERT` 宏（ECDIAssert.h）：Debug 模式输出日志+弹框+`assert(false)`；Release 空操作
- `RecordingBackend`：同时实现 `RenderingBackend` + `TextMeasurer`，记录调用供断言

### 1.2 未覆盖的欠账

| 欠账 | 来源 | 说明 |
|------|------|------|
| TextBox Selection 无单元测试 | 5.5.2 详细设计 P8 | 已实现但只"人工验证"，无自动化断言块 |
| TextBox 编辑操作 + Selection 交互 | 5.5.2 P8 | InsertCodepoint 有选中区先删、Backspace 有选中区删整区等 |
| TextBox 边界条件 | 5.5.2 记账 | 空串 Selection、全选、边界 Click（已实现但未单独测试） |
| Widget 树操作 | 自始 | AddChild 三检查、RemoveChild、Contains 防环 |
| 事件系统单元 | 自始 | EventDispatcher 模板分派（当前仅集成测试） |
| 布局系统覆盖率 | 6.1 契约 | VerticalLayout 对称性（无独立测试，HorizontalLayout 已有） |

### 1.3 约束

- **Phase 10 转库前测试保障**——7.2 是 v1.0 前硬性前置
- **不依赖窗口**：测试必须可脱离窗口运行（当前 main.cpp 断言块即此模式）
- **不引入重型框架**：当前 FRAMEWORK_ASSERT + RecordingBackend 模式已验证可行，优先延续
- **5.5.2 P8 承诺**：GPT 要求"不彻底放弃断言"，Selection 单元测试必须补上

## 2. 核心发现：Selection 不可测试暴露的是模型缺失，不是接口缺失

### 2.1 GPT 的核心洞察

> "你缺少的不是测试接口。你缺少的是一个查询接口。"

当前 TextBox 的 Selection 状态完全是内部私有：

```cpp
class TextBox {
private:
    size_t m_selectionAnchor;  // 无 public getter
    size_t m_caretPosition;    // 有 public getter
    // ...
};
```

要测试"有选中区时 InsertCodepoint 先删再插"这个 5.5.2 承诺的 P0 行为，面临两难：

| 方案 | 问题 |
|------|------|
| `SetSelectionForTesting(anchor, caret)` + `#ifdef _DEBUG` | 污染生产接口，长期膨胀（GPT 强烈反对） |
| 通过 `OnKeyDown` 模拟 Shift+方向键 | 需要 `Window*`，不满足"不依赖窗口" |
| 通过公有编辑 API 间接构造 | 无 `SelectAll()` 等公开选择方法 |

### 2.2 GPT 的结论（我采纳）

> Selection 是一个交互功能。既然是交互功能，就归入集成测试。

**GPT 的建议**：给 `TextBox` 增加一个只读查询接口 `GetSelection()`，**不提供 setter**。这样：

- 纯编辑操作（InsertCodepoint/DeleteBackward/DeleteForward 无选中区路径）→ 7.2 单元测试
- Shift+方向键选择、选中区编辑 → Phase 10 集成测试
- `GetSelection()` 查询接口本身 → 7.2 加（只读，零副作用，不污染 API）

### 2.3 长远方案：模型分离

GPT 指出，最终解决方案是分离编辑器模型：

```
TextBox
├── TextBuffer       ← 纯字符串操作
├── SelectionModel   ← 选择范围逻辑
└── CaretModel       ← 光标位置逻辑
```

但这属于编辑器架构重构，至少 Phase 8.5 以后。7.2 不做。

## 3. 职责边界

### 3.1 在范围内（Phase 7.2 做）

| # | 职责 | 内容 | 优先级 |
|---|------|------|--------|
| T1 | **TextBox 纯编辑操作补测** | InsertCodepoint（无选中区）、DeleteBackward（无选中区）、DeleteForward（无选中区）的边界行为 | **P0** |
| T2 | **TextBox 光标迁移补测** | MoveCaretLeft/MoveCaretRight（纯光标移动，无选中区交互） | **P0** |
| T3 | **GetSelection 查询接口** | 增加 `std::optional<SelectionRange> GetSelection() const` 只读查询 | **P0** |
| T4 | **TextBox 边界条件** | 空串操作、emoji/中文混合编辑、全选后删除（通过 `SetText` + 编辑操作间接触发） | P1 |
| T5 | **Widget 树操作** | AddChild 三检查（非空/无父/防环）、RemoveChild 正常/异常、GetChildAt | P1 |
| T6 | **Layout 补测** | VerticalLayout 对称性实战（与 HorizontalLayout 同构）+ 多种子宽高组合 | P1 |
| T7 | **Event 构造补测** | KeyDownEvent 修饰键组合（Ctrl/Shift/Alt 枚举值）、EventType/StaticType 一致性 | P2 |
| T8 | **测试基础设施** | 文件拆分（从 main.cpp 剥离）、命名规范、断言块组织 | P0 |

### 3.2 明确归入 Phase 10 集成测试（原T1-T3中依赖交互的部分）

| 原计划 | 新归属 | 原因 |
|--------|--------|------|
| InsertCodepoint 有选中区先删 | Phase 10 | 需要 `SetSelection` 或 Shift+方向键交互，无法在无窗口下构造前置状态 |
| DeleteBackward 有选中区删整区 | Phase 10 | 同上 |
| DeleteForward 有选中区删整区 | Phase 10 | 同上 |
| Shift+方向键选择 | Phase 10 | 需要 `OnKeyDown` + `Window*` |
| Shift+Home/End 扩展选择 | Phase 10 | 同上 |

### 3.3 不在范围内（Phase 7.2 不做）

| 不做的 | 原因 | 归属 |
|--------|------|------|
| 引入 Google Test / Catch2 等第三方测试框架 | 当前 FRAMEWORK_ASSERT + main.cpp 断言块已验证可行；转库前引入重型框架增加构建复杂度 | Phase 10 评估 |
| 窗口依赖测试（事件分发/HitTest 集成/焦点/IME） | 需要 HWND 和消息泵，无法脱离窗口 | Phase 10 集成测试 |
| Renderer 命令管线测试 | 已有（4.5/5.1/5.2） | 已覆盖 |
| Paint 输出测试 | 已有（4.6/5.3） | 已覆盖 |
| UI 自动化测试（模拟鼠标点击/键盘输入） | 需要窗口和消息循环 | Phase 10 集成测试 |
| TextBox 多层编辑序列/Undo-Redo | 还没做（Phase 8.5） | 未来 |
| Invalidate 两层结构 | 不是测试，是 API 重构 | 技术债务 |
| Selection 交互逻辑（Shift+方向键/选中区编辑） | 需要窗口依赖或 `SetSelection` 接口，设计中不引入测试专用 setter | Phase 10 |
| TextBuffer/SelectionModel/CaretModel 分离 | 编辑器架构重构，Phase 8.5 以后评估 | 未来 |

### 3.4 可选项（评估后决定）

| 可选项 | 考量 | 决策 |
|--------|------|------|
| 测试独立于 main.cpp | 当前断言块在 GUI 启动前，直观但随测试增多 main.cpp 膨胀 | ✅ 采纳 GPT 方案（见 §5） |
| 创建单独的测试目标（CMake test target） | 转库后自然需要 | Phase 10 |
| 断言结果汇总（非"一崩全崩"） | 当前 FRAMEWORK_ASSERT 遇失败即终止，多测试块无法全部运行 | Phase 10 评估 |

## 4. 关键决策点

### D1 测试框架：延续 FRAMEWORK_ASSERT

**GPT 评价**：✅ 赞成。当前 20～30 个测试，完全没必要引入 Google Test / Catch2 / doctest。

**决策**：延续 **FRAMEWORK_ASSERT**。Phase 10 转库时评估是否引入 Catch2（单头文件版）。

### D2 测试组织：从 main.cpp 拆分为独立文件

**GPT 评价**：✅ 强烈赞成。"这个我强烈建议立即做。"

**GPT 方案**：
```
src/
├── Tests/
│   ├── RendererTests.cpp
│   ├── WidgetTests.cpp
│   ├── LayoutTests.cpp
│   ├── TextBoxTests.cpp
│   └── RunAllTests.cpp        ← 统一入口
```

main.cpp 改为：
```cpp
int main()
{
#ifdef _DEBUG
    RunAllTests();
#endif
    RunApplication();
}
```

**决策**：采纳 GPT 方案。

### D3 RecordingBackend 是否够用

RecordingBackend 同时实现 RenderingBackend + TextMeasurer，固定测量值（宽 10，高 14）。对于 TextBox 的纯字符串操作测试（无坐标依赖），固定值足够。坐标相关测试（光标位置、Selection 高亮绘制）归 Phase 10。

**决策**：RecordingBackend 继续使用，不修改。

### D4 是否要加 GetSelection() 查询接口

**GPT 建议**：增加 `std::optional<SelectionRange> GetSelection() const` 只读接口。

**理由**：
- 零副作用，纯查询
- 不违反"不为测试改生产接口"原则（查询是合法业务需求）
- Phase 10 集成测试可直接验证 Selection 状态
- 对调试也有帮助

**决策**：✅ 在 Phase 7.2 实现 T3 时加入 `GetSelection()`。

## 5. 文件组织方案（GPT 方案采纳）

### 5.1 目录结构

```
ECDI/src/
├── Tests/
│   ├── RunAllTests.h          ← 声明 RunAllTests() 入口
│   ├── RunAllTests.cpp        ← 调用各测试块的入口
│   ├── RendererTests.cpp      ← #1, #3（Renderer 转发）
│   ├── WidgetTests.cpp        ← #2, #4, #5, T5（Widget 绘制 + 树操作）
│   ├── LayoutTests.cpp        ← #8, T6（布局）
│   ├── TextBoxTests.cpp       ← #7, T1, T2, T4（TextBox 编辑）
│   └── EventTests.cpp         ← T7（可选，P2）
│
├── Application/
├── Window/
├── Widget/
├── Render/
├── Core/
├── main.cpp                   ← 只保留 #ifdef _DEBUG RunAllTests(); #endif
```

### 5.2 main.cpp 改为

```cpp
#ifdef _DEBUG
#include "Tests/RunAllTests.h"
#endif

int main()
{
#ifdef _DEBUG
    RunAllTests();
#endif
    // ... 现有 GUI 初始化代码 ...
}
```

### 5.3 测试块命名规范

- 每个测试块是一个函数，返回 `void`，命名 `TestXxxXxx`
- 测试块内使用 `FRAMEWORK_ASSERT`
- 测试块失败时打印 `[FAIL] 测试名称`，成功打印 `[PASS] 测试名称`
- 统一格式：`// ── TestName ──` 分隔

## 6. 测试清单（T1-T7 详细）

### T1 TextBox 纯编辑操作补测（P0）

**可测的外部行为**（无选中区路径）：

```cpp
// InsertCodepoint 在末尾插入
{
    ECDI::TextBox box("abc");
    box.OnChar(U'中');       // 通过 OnChar 直接调用（EventRouter 虚方法，无需窗口）
    FRAMEWORK_ASSERT(box.GetText() == "abc中");
    FRAMEWORK_ASSERT(box.GetCaret() == 4);  // 3 个 ASCII + 1 个中文
}

// DeleteBackward 删除光标前字符
{
    ECDI::TextBox box("abcdef");
    // 移动光标到 'c' 和 'd' 之间
    // 通过 OnKeyDown 模拟方向键（需要 Window*？）
    // 或通过 MoveCaretToEnd/MoveCaretToStart + 左移 → 仍然需要 OnKeyDown
}
```

**关键约束**：当前 `MoveCaretLeft` / `MoveCaretRight` 等光标迁移方法是通过 `OnKeyDown` 触发的，`OnKeyDown` 需要 `KeyDownEvent&` 参数，而 `KeyDownEvent` 构造需要 `Window*`。

**实际可测范围**：
- `OnChar(char32_t)` 在末尾插入（无光标移动）
- 通过 `SetText` + `GetText` 验证字符串操作
- 光标通过 `MoveCaretToStart()` / `MoveCaretToEnd()` 设置（这些方法**可能**不需要 Window*，需验证）

**待验证项**：实现时确认 `MoveCaretToStart()` / `MoveCaretToEnd()` 是否依赖 `Window*`。

### T2 光标迁移补测（P0）

与 T1 同理，依赖 `OnKeyDown` 的 `MoveCaretLeft` / `MoveCaretRight` 路径需要 `Window*`。

**可测范围**：`MoveCaretToStart()` / `MoveCaretToEnd()` 的纯逻辑（如果它们不依赖 Window）。

### T3 GetSelection 查询接口（P0）

**新增接口**：

```cpp
// TextBox.h
struct SelectionRange {
    size_t start;
    size_t end;
};

class TextBox : public Widget {
public:
    std::optional<SelectionRange> GetSelection() const;
    // ...
};
```

**实现逻辑**：
- 如果 `m_selectionAnchor == m_caretPosition` → 返回 `std::nullopt`（无选中区）
- 否则 → 返回 `SelectionRange{min, max}`（`min ≤ max`）

**测试**：
```cpp
{
    ECDI::TextBox box("hello");
    // 刚创建，无选中区
    FRAMEWORK_ASSERT(!box.GetSelection().has_value());
}
```

### T4 TextBox 边界条件（P1）

```cpp
// 空串操作
{
    ECDI::TextBox box("");
    box.OnChar(U'a');
    FRAMEWORK_ASSERT(box.GetText() == "a");
}

// emoji/中文混合
{
    ECDI::TextBox box("中🎉a");
    box.OnChar(U'X');
    FRAMEWORK_ASSERT(box.GetText() == "中🎉aX");
}
```

### T5 Widget 树操作（P1）

```cpp
// AddChild 非空检查
{
    ECDI::Widget parent;
    ECDI::Widget child;
    parent.AddChild(child);
    FRAMEWORK_ASSERT(child.GetParent() == &parent);
    FRAMEWORK_ASSERT(parent.GetChildCount() == 1);
}

// AddChild 无父检查
{
    ECDI::Widget parent;
    ECDI::Widget child;
    parent.AddChild(child);
    // 再次添加同一 child → 应失败（已有父）
    bool result = parent.AddChild(child);
    FRAMEWORK_ASSERT(!result);
    FRAMEWORK_ASSERT(parent.GetChildCount() == 1);
}

// AddChild 防环检查
{
    ECDI::Widget grandparent;
    ECDI::Widget parent;
    ECDI::Widget child;
    grandparent.AddChild(parent);
    parent.AddChild(child);
    // 将 grandparent 添加为 child 的子 → 形成环，应失败
    bool result = child.AddChild(grandparent);
    FRAMEWORK_ASSERT(!result);
}
```

### T6 Layout 补测（P1）

VerticalLayout 与 HorizontalLayout 同构，方向相反：
- 子控件垂直排列，总高度 = 各子高度之和
- 宽度 = 最大子宽度
- 坐标计算：x 相同，y 累加

### T7 Event 构造补测（P2）

```cpp
// KeyModifier 组合枚举值
{
    ECDI::KeyDownEvent ev(/* ... */);
    // 验证修饰键组合
}
```

## 7. GetSelection 接口设计

### 7.1 接口声明

```cpp
// TextBox.h
namespace ECDI {

struct SelectionRange {
    size_t start;
    size_t end;
    // 保证 start <= end
};

class TextBox : public Widget {
public:
    // ... 现有接口 ...
    
    /// 获取当前选中区（如果存在）
    /// 无选中区（m_selectionAnchor == m_caretPosition）→ nullopt
    /// 有选中区 → SelectionRange{min, max}
    std::optional<SelectionRange> GetSelection() const;
    
    // ...
};
}
```

### 7.2 实现

```cpp
// TextBox.cpp
std::optional<SelectionRange> TextBox::GetSelection() const
{
    if (m_selectionAnchor == m_caretPosition) {
        return std::nullopt;
    }
    return SelectionRange{
        (std::min)(m_selectionAnchor, m_caretPosition),
        (std::max)(m_selectionAnchor, m_caretPosition)
    };
}
```

### 7.3 原则

- **只读，无副作用**：不修改任何内部状态
- **不依赖窗口**：纯逻辑，仅比较两个 size_t
- **不提供 setter**：GPT 明确反对 `SetSelection()`，只提供查询
- **Phase 10 集成测试时可直接验证**：`box.GetSelection()->start == 2`

## 8. 实现顺序建议

```
第一阶段（P0 必做）：
  T3  GetSelection 查询接口           ← 先实现，依赖最小
  T1  TextBox 纯编辑操作补测           ← 利用现有 OnChar 路径
  T2  光标迁移补测（可测部分）          ← MoveCaretToStart/End
  T8  测试基础设施（文件拆分）          ← 先拆再测

第二阶段（P1 尽量做）：
  T4  TextBox 边界条件补测
  T5  Widget 树操作补测
  T6  Layout 补测

第三阶段（P2 可选）：
  T7  Event 构造补测
```

## 9. 当前方案总结

### 核心变化（v1.0 → v1.1）

| 项目 | v1.0 | v1.1（GPT 采纳） |
|------|------|-----------------|
| 标题 | "Phase 7.2 测试体系" | "Phase 7.2 无窗口单元测试体系" |
| Selection 测试方案 | `SetSelectionForTesting()` + `#ifdef _DEBUG` | ❌ 移除。承认 Selection 交互归 Phase 10 |
| 查询接口 | 无 | ✅ 增加 `GetSelection()` 只读查询 |
| 文件组织 | 粗略提到独立文件 | ✅ 采纳 GPT 的 `Tests/*.cpp` 方案 |
| 测试边界 | T1-T3 全部在 7.2 | 重新划分：纯编辑归 7.2，交互归 Phase 10 |

### 测试文件组织

采纳 GPT 方案：
```
src/Tests/
  RendererTests.cpp
  WidgetTests.cpp
  LayoutTests.cpp
  TextBoxTests.cpp
  EventTests.cpp (P2)
  RunAllTests.cpp
  RunAllTests.h
```

### 框架选择

延续 FRAMEWORK_ASSERT，Phase 10 评估 Catch2。

### Selection 的最终处理

- **7.2**：增加 `GetSelection()` 查询接口，纯编辑操作补测
- **Phase 10**：集成测试覆盖 Shift+方向键选择、选中区编辑、Home/End 扩展

## 10. 修订记录

- v1.0（2026-08-17）职责确认初稿
- v1.1（2026-08-17）采纳 GPT 评审：移除 `SetSelectionForTesting()`，增加 `GetSelection()` 查询接口，重新划分测试边界，采用 GPT 文件组织方案，改名"无窗口单元测试体系"