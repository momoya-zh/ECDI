# Focus 详细设计（phase3-focus-design.md）

> 阶段：第三阶段 Widget System → Focus 子模块
> 前置：职责确认 + 初步设计已评审通过（"获取 / 保持 / 释放 / 键盘路由"四件事完整定义）
> **状态：已实现（2026-08-07，随 Widget System MVP 提交 `a11cb9b`）**

---

## 0. 目标

补齐"当前 Window 中哪个 Widget 接收键盘事件"的**获取与生命周期管理**。键盘分发链路本身在 Phase3 早期已存在，本模块补的是它的触发源与合法性保障。

## 1. 职责确认

### 1.1 定义

Focus System 管理：

> 当前 Window 中哪个 Widget 接收 Keyboard Event。

- 输入：Mouse Interaction / Application API / Widget 生命周期变化
- 输出：Focused Widget

### 1.2 所属关系（定稿）

```
Window
 ├── RootWidget
 └── FocusedWidget*（m_focusedWidget）
```

- **Window 管理 Focus 状态**：一个 HWND 对应一个键盘焦点，多窗口之间焦点天然隔离
- **Application 负责 Keyboard Event Dispatch**：`OnKeyDown → FindFocusedWidget → window.GetFocusedWidget() → target->OnKeyDown`
- 不引入 Widget → Window 反向引用

### 1.3 已有基础（本模块前置，已实现）

| 项 | 状态 |
|---|---|
| `Window::SetFocusedWidget(Widget*)` / `GetFocusedWidget()` | ✅ 已存在（Phase3 早期） |
| 键盘分发链（Application → GetFocusedWidget → Widget::OnKeyDown） | ✅ 已存在 |
| **SetFocusedWidget 的调用点** | ❌ 缺失（焦点永远 nullptr）——本模块补 |

### 1.4 本模块新增

| 项 | 内容 |
|---|---|
| Widget 能力声明 | `virtual bool CanFocus() const noexcept`（默认 false，inline） |
| Button 开启聚焦 | `CanFocus() override → true` |
| 获取触发 | `OnMouseButtonDown`：HitTest 后、Dispatch 前，`if (target->CanFocus()) window->SetFocusedWidget(target)` |
| 合法性保障 | `SetFocusedWidget` 入口验证：Parent 链回溯到树根，必须是本窗口 RootWidget |

## 2. 关键决策（初步设计定稿）

| # | 决策点 | 结论 |
|---|--------|------|
| 1 | Focus 归属 | Window 层（一个 HWND 一个焦点），Application 只做分发 |
| 2 | 获取时机 | MouseDown 后、Dispatch 前（回调修改树结构不影响焦点获取） |
| 3 | 点击空白 | **保持焦点不清除**（符合 Windows 原生行为，减少状态变化） |
| 4 | 键盘 Bubbling | **不 Bubbling**（Direct 到 Focused Widget；容器监听键盘归未来 Shortcut System） |
| 5 | 悬垂防护方案 | **方案 C（验证式）**：不在 RemoveChild 时清理，而在设置入口 + 使用路径校验。理由：Widget Tree 快速变化期、不为 Focus 引入反向依赖、键盘事件低频验证成本可忽略 |
| 6 | Widget→Window 反向引用 | 不引入（第一版"不保证运行时删除 Focused Widget"是已知限制，文档化） |
| 7 | 范围裁剪 | ❌ Tab 遍历 / Focus 顺序 / Focus Visual / Enter-Leave / 多窗口切换 / 销毁自动转移焦点 |

## 3. 实现（已落地）

### 3.1 Widget.h

```cpp
// ── Focus ────────────────────────────────────────
/// @brief 是否可以获得键盘焦点
/// @return 默认 false（不可聚焦），子类 override 返回 true
/// @details 只声明能力，visible/enabled 判断由事件系统前置处理
virtual bool CanFocus() const noexcept { return false; }
```

### 3.2 Button.h

```cpp
bool CanFocus() const noexcept override { return true; }
```

### 3.3 Application.cpp —— OnMouseButtonDown（插入式，保留原有 Bubbling）

```cpp
void Application::OnMouseButtonDown(const MouseButtonDownEvent& event) {

	Widget* target = FindTargetWidget(*event.GetWindow(), event.GetMouseX(), event.GetMouseY());

	if (target == nullptr) {
		return;
	}

	// Focus 获取：命中且可聚焦 → 设置窗口焦点（在派发前，避免回调改树影响焦点）
	if (target->CanFocus()) {
		event.GetWindow()->SetFocusedWidget(target);
	}

	// 原有 bubbling 保留
	Widget* current = target;
	while (current != nullptr) {
		current->OnMouseButtonDown(event);
		current = current->GetParent();
	}
}
```

### 3.4 Window.cpp —— SetFocusedWidget 合法性验证

沿 Parent 链回溯到树根，必须是本窗口 RootWidget（非法焦点直接断言拒绝）：

```cpp
void Window::SetFocusedWidget(Widget* widget) {

	if (widget == nullptr) {
		m_focusedWidget = nullptr;
		return;
	}

	// 沿 Parent 链回溯到树根，验证 widget 属于当前窗口的 Widget 树
	Widget* current = widget;
	while (current->GetParent()) {
		current = current->GetParent();
	}

	// 树根必须是当前窗口的 RootWidget
	FRAMEWORK_ASSERT(current == &GetRootWidget());

	m_focusedWidget = widget;
}
```

验证实现采用 **GetParent 链回溯**（`GetParent()` 是 public），不暴露 Widget 的私有 `Contains()`，不新增 Widget API。

## 4. 已知限制（文档化）

- **第一版不保证运行时删除 Focused Widget**：如果用户 `RemoveChild` 销毁了当前 Focused Widget 再按键，行为未定义（悬垂指针风险）。不引入 Widget→Window 反向引用的代价，接受并文档化
- 键盘事件到达时 `GetFocusedWidget()` 可能为 nullptr（未聚焦过）——现有分发链已判空，安全

## 5. 测试计划

| 用例 | 步骤 | 预期 |
|------|------|------|
| T1 获取焦点 | 点击 Button | Logger 输出目标命中，焦点 = Button |
| T2 键盘分发 | 点击 Button 后按 A | Button 收到 OnKeyDown（需测试类 override 输出） |
| T3 点击空白保持 | 点击 Button → 点击空白 → 按键 | 键盘仍到 Button（焦点未被清除） |
| T4 不可聚焦 Widget | 点击 Label | 焦点不变（Label CanFocus 默认 false） |
