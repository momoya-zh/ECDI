# Phase 5.4 交互基础设施初步设计 v1.0

> 日期：2026-08-13 ｜ 状态：已确认 ｜ 方式：清单式问答（P1-P6）+ GPT 评审（含修正与实现顺序拆分）

## 决策记录

### P1 Widget→Window 关联 + Invalidate + HasFocus —— A ✅（GPT：GetWindow 应 protected）

```cpp
// Widget.h 新增（前向声明 class Window; 到 Widget.h 顶部）
protected:
	Window* GetWindow() noexcept;               // ⚠️ protected（GPT 修正：TextBox/ScrollBar/ComboBox 未来要访问 Window）
	const Window* GetWindow() const noexcept;

public:
	void Invalidate();                          // 上溯到根 → Window::Invalidate
	bool HasFocus() const noexcept;             // 上溯到根 → GetFocusedWidget() == this

private:
	Window* m_window = nullptr;                 // 非拥有；仅 RootWidget 由 Window 设置

// Widget.cpp
Window* Widget::GetWindow() noexcept{
	Widget* root = this;
	while (root->m_parent) root = root->m_parent;
	return root->m_window;
}
void Widget::Invalidate(){ if (Window* w = GetWindow()) w->Invalidate(); }
bool Widget::HasFocus() const noexcept{
	const Widget* root = this;
	while (root->m_parent) root = root->m_parent;
	return root->m_window && root->m_window->GetFocusedWidget() == this;
}
```

```cpp
// Window 新增
void Window::Invalidate(){ if (m_handle) InvalidateRect(m_handle, nullptr, FALSE); }
// Window.cpp 构造内：m_rootWidget 创建后 → m_rootWidget->SetWindow(this)
```

- **GetWindow() protected**（GPT 修正）：Invalidate/HasFocus/未来 RequestLayout/主题查询复用同一入口；派生控件可直接访问 Window
- m_window 只设根 + 上溯获取（一次关联，多入口共享）

### P2 Window::HandleKeyDown + FocusNext —— A ✅（用户修正：不用 Win32 API，Shift+Tab 留 5.5）

```cpp
// Window 新增（Application::OnKeyDown 改调它，替代直接派发）
void Window::HandleKeyDown(const KeyDownEvent& event){
	if (event.GetKeyCode() == KeyCode::Tab){
		FocusNext(1);   // ⚠️ 5.4 只做正向 Tab（用户修正：不做反向）
		return;
	}
	if (m_focusedWidget) m_focusedWidget->OnKeyDown(event);
}
```

- **用户修正：不用 `GetKeyState(VK_SHIFT)`**——Win32 API 破坏平台无关性（未来 PlatformWindow 抽象要改）；且**修饰键状态本该由事件携带**（Win32 翻译器翻译 WM_KEYDOWN 时填入），不该由 Window/控件查询系统
- **Shift+Tab 留到 5.5**：与 KeyEvent 扩展（加 shift/ctrl/alt 字段）一起做——届时 `FocusNext(event.IsShiftDown() ? -1 : 1)` 自然实现
- `FocusNext(int direction)`：**保留 direction 参数**（5.5 传 -1 反向；5.4 只传 +1）；树前序递归收集 CanFocus 控件 → 当前 index + direction（无焦点从第一个）→ 循环取模 → SetFocusedWidget
- **记入债务**：KeyEvent 修饰键字段（shift/ctrl/alt）+ Shift+Tab 反向 = 5.5 一起做

### P3 焦点边框 —— 内框方案 ✅

```cpp
// Button::OnPaint：
// 无焦点：DrawRect(全块, bg) → DrawTextContent
// 有焦点：DrawRect(全块, White) → DrawRect(内缩2px, bg) → DrawTextContent
//   → 控件内部边缘露出 2px 白边框（不越界、不覆盖相邻控件；零命令扩展）
```

### P4 Mouse Capture —— 隐式 capture ✅（本质是修 Bug）

```cpp
// Window 新增：SetCaptureWidget/GetCaptureWidget + m_captureWidget
// Application 鼠标派发改造：
//   Down：target = FindTargetWidget；命中 → SetCaptureWidget(target)；target==null → 清除焦点
//   Move/Up：target = GetCaptureWidget()（有 capture 直接用）→ 否则 FindTargetWidget → 派发
//   Up 派发后：SetCaptureWidget(nullptr)
```

修复"按下→移出→Up 不达→m_pressed 卡死"。

### P5 GetAbsolutePosition —— A ✅（GPT 修正：进 Widget 公共接口）

```cpp
// Widget.h 公共接口
Point GetAbsolutePosition() const noexcept;    // ⚠️ 公共（GPT 修正：TextBox 光标/ScrollBar 拖动/Popup 定位/Tooltip 都要用）

// Widget.cpp：父链累加 GetX/GetY
Point Widget::GetAbsolutePosition() const noexcept{
	Point pos{ static_cast<float>(GetX()), static_cast<float>(GetY()) };
	const Widget* p = m_parent;
	while (p){ pos.x += static_cast<float>(p->GetX()); pos.y += static_cast<float>(p->GetY()); p = p->GetParent(); }
	return pos;
}
```

### P6 Button 按下态 —— A ✅

```cpp
// Down：m_pressed = true; Invalidate();
// Up：m_pressed = false; [GetAbsolutePosition 判断鼠标在自身内 → OnClick]; Invalidate();
// OnPaint：m_pressed ? FromRGBA8(60,90,180) : FromRGBA8(80,120,220)（按下变深）
```

## ⚠️ 实现顺序拆分（GPT 建议采纳：5 个 commit，每步可测）

按依赖关系重新排序，**不要一次改 10 个文件**：

```
5.4.1 Invalidate          ← 基础（Widget→Window 关联 + Widget::Invalidate + Window::Invalidate + GetAbsolutePosition）
5.4.2 Mouse Capture       ← 鼠标（Window capture 状态 + Application 派发改造 + 点击空白清焦点）
5.4.3 Focus 通知          ← 焦点（OnFocusGained/Lost + SetFocusedWidget 触发 + HasFocus）
5.4.4 Tab 导航            ← 键盘（HandleKeyDown + FocusNext + GetKeyState）
5.4.5 Button 按下态       ← 消费（m_pressed 视觉 + Up 坐标判断 + 焦点边框）
```

每步独立可编译可测。5.4 是**第一次真正进入 GUI 框架系统设计**（焦点管理/输入管理/鼠标捕获/重绘机制——5.5 TextBox、6.x Layout、7.x Theme 全建立在它之上）。

## 待详细设计固化

1. Widget.h/cpp 精确编辑点（前向声明 Window + 5 个方法 + m_window 成员）
2. Window.h/cpp（Invalidate/HandleKeyDown/FocusNext/capture 状态）
3. Application.cpp 鼠标派发改造精确 diff
4. Button.cpp（按下态 + 焦点边框 + Up 判断）
5. main.cpp 验证（人工交互为主）
