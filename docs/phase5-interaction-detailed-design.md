# Phase 5.4 交互基础设施详细设计 v1.0

> 日期：2026-08-13 ｜ 状态：已确认 ｜ 方式：清单式问答（D1-D5 对应 5 个 commit）+ GPT 评审

## 决策记录

### D1 = 5.4.1 Invalidate + Widget 基础扩充（GPT 修正 2 处）

**Widget.h**（3 处）：顶部加 `class Window;` 前向声明；新增：

```cpp
public:
	void Invalidate();                           // 上溯到根 → Window::Invalidate
	bool HasFocus() const noexcept;              // 上溯到根 → GetFocusedWidget() == this
	Point GetAbsolutePosition() const noexcept;  // P5 公共（TextBox/ScrollBar/Popup/Tooltip/DragDrop 未来用）
protected:
	Window* GetWindow() noexcept;                // P1 GPT：protected
	const Window* GetWindow() const noexcept;
private:
	void SetWindow(Window* window);              // ⚠️ GPT 修正：private + friend（只有 Window 有权调用）
	friend class Window;                         // ⚠️ 新增（SetWindow 私有化的配套）
	Window* m_window = nullptr;                  // 非拥有；仅根设置
```

**Widget.cpp**：5 个方法实现（GetWindow 上溯 / Invalidate 转发 / HasFocus 比较 / GetAbsolutePosition 父链累加 / SetWindow 赋值）

**Window.h**：`void Invalidate();` ｜ **Window.cpp**：实现（`InvalidateRect(m_handle, nullptr, FALSE)`）+ 构造内 `m_rootWidget->SetWindow(this);`

**验证**：编译（纯基础设施，无调用方）

### D2 = 5.4.2 Mouse Capture（⚠️ GPT 修正：先派发再释放）

**Window.h/cpp**：`SetCaptureWidget(Widget*)` / `GetCaptureWidget()` + `Widget* m_captureWidget = nullptr`

**Application.cpp** 三处改造：

```cpp
// OnMouseButtonDown：
Window& window = *event.GetWindow();
Widget* target = FindTargetWidget(window, mouseX, mouseY);
if (target == nullptr){ window.SetFocusedWidget(nullptr); return; }   // 点击空白清焦点
window.SetCaptureWidget(target);       // 隐式捕获：命中即捕获
if (target->CanFocus()) window.SetFocusedWidget(target);
// Bubbling 原样

// OnMouseMove：target = GetCaptureWidget()（有 capture 直接用，跳过 HitTest）；否则 FindTargetWidget → Bubbling

// OnMouseButtonUp：
Widget* target = window.GetCaptureWidget();
if (!target) target = FindTargetWidget(window, mouseX, mouseY);
// Bubbling 派发 target（先派发）
window.SetCaptureWidget(nullptr);      // ⚠️ GPT 修正：先派发再释放——控件 OnMouseButtonUp 里 GetCaptureWidget() 仍能拿到自身状态
```

**验证**：编译 + 手动（按下按钮→拖出→释放，Button 收到 Up）

### D3 = 5.4.3 Focus 通知（⚠️ GPT 修正：同控件短路 + Invalidate 全覆盖）

**Widget.h**（Focus 区 CanFocus 旁）：`virtual void OnFocusGained() {}` / `virtual void OnFocusLost() {}`

**Window.cpp SetFocusedWidget 改造**（GPT 修正版）：

```cpp
void Window::SetFocusedWidget(Widget* widget){
	if (m_focusedWidget == widget) return;        // ⚠️ 同控件短路：避免 Lost+Gained 空转
	if (m_focusedWidget) m_focusedWidget->OnFocusLost();
	m_focusedWidget = widget;
	// 树归属验证（现有 FRAMEWORK_ASSERT，widget != nullptr 时保留）
	if (m_focusedWidget) m_focusedWidget->OnFocusGained();
	Invalidate();                                 // ⚠️ 两种变化（设新/清空）都重绘
}
```

**验证**：编译 + 手动（点击按钮 → HasFocus 变化 + 重绘）

### D4 = 5.4.4 Tab 导航（GPT 确认：仅正向；CollectFocusables 进匿名 namespace）

**Window.h**：`void HandleKeyDown(const KeyDownEvent& event);`（加 KeyDownEvent 前向声明）+ 私有 `void FocusNext(int direction = 1);`

- ⚠️ **direction 参数保留**（GPT 建议无参，但我们**明确计划 5.5 Shift+Tab 复用 FocusNext(-1)**——债务已记，非猜测性参数；默认参数让 5.4 调用处写 `FocusNext()` 干净）

**Window.cpp**：
```cpp
// 匿名 namespace：DFS 辅助（⚠️ GPT 修正：不放 Window 类，非公开能力）
namespace {
void CollectFocusables(Widget* node, std::vector<Widget*>& out){
	if (node->CanFocus()) out.push_back(node);
	for (size_t i = 0; i < node->GetChildCount(); ++i)
		CollectFocusables(node->GetChildAt(i), out);
}
}

void Window::HandleKeyDown(const KeyDownEvent& event){
	if (event.GetKeyCode() == KeyCode::Tab){ FocusNext(); return; }   // 仅正向（P2 用户修正：Shift+Tab 留 5.5）
	if (m_focusedWidget) m_focusedWidget->OnKeyDown(event);
}

void Window::FocusNext(int direction){
	std::vector<Widget*> focusables;
	CollectFocusables(m_rootWidget.get(), focusables);
	if (focusables.empty()) return;
	int index = -1;
	for (size_t i = 0; i < focusables.size(); ++i)
		if (focusables[i] == m_focusedWidget){ index = static_cast<int>(i); break; }
	const int next = (index < 0) ? 0 : (index + direction + static_cast<int>(focusables.size())) % static_cast<int>(focusables.size());
	SetFocusedWidget(focusables[next]);
}
```

**Application.cpp OnKeyDown**：`event.GetWindow()->HandleKeyDown(event);`（替代 FindFocusedWidget + OnKeyDown）

**验证**：编译 + 手动（Tab 在两按钮间移动焦点框）

### D5 = 5.4.5 Button 按下态 + 焦点边框（⚠️ GPT 修正：先恢复视觉再 OnClick）

**Button.cpp**（GPT 修正顺序）：

```cpp
// OnMouseButtonDown：m_pressed = true; Invalidate();
// OnMouseButtonUp：
const bool inside = [GetAbsolutePosition + 事件坐标判断];
m_pressed = false;
Invalidate();                 // ⚠️ 先恢复视觉（m_pressed=false + 重绘）
if (inside) OnClick();        // ⚠️ 再触发点击（用户直觉：视觉恢复后 OnClick）

// OnPaint：
//   背景：m_pressed ? FromRGBA8(60,90,180) : FromRGBA8(80,120,220)
//   HasFocus() → 内框：DrawRect(全块, White) → DrawRect(内缩2px, 背景) → DrawTextContent
//   否则：DrawRect(全块, 背景) → DrawTextContent
```

**main.cpp**：人工交互验证为主（Tab 切换/按下变色/拖出取消——I7 定案，少断言）

## ⚠️ 架构债务记录（GPT 观察，Phase 5 后处理）

5.4 暴露**责任交叉**：Mouse/Keyboard/Focus/Capture/Invalidate 全部堆在 Application/Window 层（`FindTargetWidget` → `SetFocusedWidget` → `SetCaptureWidget` → 派发…）。

- **Phase 5 结束后做一次架构回顾**：评估是否引入 `InputManager` / `FocusManager` 等子系统
- **现在绝对不重构**——5.5 TextBox 大概率会把这些暴露出来，届时一起评估
- 记账：Hover/DoubleClick/MouseEnter/Leave 未来也会加入，进一步加剧交叉

## 修订记录

- v1.0（2026-08-13）：D1-D5 定稿——GPT 修正 5 处（SetWindow private+friend / capture 先派发后释放 / 同控件短路+Invalidate 全覆盖 / CollectFocusables 匿名 namespace / Up 先视觉后 OnClick）+ 架构债务记录（Phase 5 后架构回顾）
