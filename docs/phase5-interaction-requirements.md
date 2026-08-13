# Phase 5.4 交互基础设施职责确认 v1.0

> 日期：2026-08-13 ｜ 状态：已确认 ｜ 方式：清单式问答（I1-I7）+ GPT 评审（5.4 为 Phase 5 质量最高份）

## 背景

5.4 补齐 GUI 框架核心交互基础设施——7 项决策是**完整闭环**：`Invalidate → Focus 通知 → Tab 导航 → Focus 视觉 → Mouse Capture → Button 按下态`。做完后 5.5 TextBox 最难部分（Focus/Capture/Invalidate/Selection/Cursor 基础）已完成一半。

现状：键盘事件链已通（`Application::OnKeyDown → FindFocusedWidget → OnKeyDown`）、点击获取焦点已有（`OnMouseButtonDown → CanFocus → SetFocusedWidget`）、`KeyCode::Tab` 存在。缺：Invalidate / 焦点通知 / Tab 导航 / 焦点视觉 / Mouse Capture。

## 决策记录

### I1 Invalidate 机制 —— A ✅

**RootWidget 持 `Window*`（非拥有，Window 创建根后设置）+ `Widget::Invalidate()` 沿父链上溯到根取 Window\* → `Window::Invalidate()`（内部 `InvalidateRect` → WM_PAINT → PaintFrame）**

```
数据改变 → Widget::Invalidate → 父链上溯 → RootWidget → Window → InvalidateRect → WM_PAINT → PaintFrame
```

- 这是**整个 GUI 框架的数据流**（SetText / m_pressed / 未来 TextBox InsertCharacter 全走它）
- 向上冒泡符合 WidgetTree 架构；唯一入口原则延续（框架层操作从根进）
- Widget→Window 关联是 Phase 3 推迟的"跨系统通知"，现在落地（只建一次，根持有）

### I2 焦点通知 —— A ✅

**Widget 加虚方法 `OnFocusGained()` / `OnFocusLost()`（默认空）+ `SetFocusedWidget` 触发（旧 Lost、新 Gained）+ 两者 Invalidate**

```
oldWidget → OnFocusLost() → Invalidate()
newWidget → OnFocusGained() → Invalidate()
```

TextBox 未来：`OnFocusGained → m_cursorVisible = true` / `OnFocusLost → m_selection.Clear()` 自然接入。

### I3 Tab 焦点切换 —— ⚠️ GPT 修正：放 Window，不放 Application ✅

原方案：Application::OnKeyDown 拦截 Tab → FocusNext。**GPT 修正：Application 不该知道 Tab——焦点属于 Window**（一个 Application 多窗口，Tab 是"当前窗口内部"移动焦点）。

**定稿**：`Window::HandleKeyDown(event)`——**Tab → FocusNext()；否则 → 派发给 m_focusedWidget**。`Application::OnKeyDown` 改为调 `window.HandleKeyDown(event)`（不再直接 FindFocusedWidget）。

- `FocusNext()`：遍历 Widget 树收集 CanFocus 控件（**树前序**）、当前焦点 index+1 循环、无焦点从第一个开始；Shift+Tab 反向
- 子决策：Tab 顺序 = 树前序 ✅；Shift+Tab 反向 ✅（做）

### I4 焦点视觉态 —— A ✅

**`Widget::HasFocus()`**（`m_window && m_window->GetFocusedWidget() == this`）+ 控件 OnPaint 自查；**焦点边框 = 边框高亮**（现有 DrawRect 两命令：边框色整块 → 背景色内缩 2px——**零命令扩展**，虚线框归 Phase 8）

"先复用已有基础设施，不够用再扩展"原则的体现。

### I5 Mouse Capture —— A ✅（全清单最重要项）

**隐式 capture**：`Window::m_captureWidget`——MouseButtonDown 命中 → capture=target；**有 capture 时 MouseMove/Up 直接派发给 captureWidget（不 HitTest）**；MouseButtonUp 后释放。

```
MouseDown → capture = button → MouseMove(移出) → 仍派发 capture → MouseUp → 仍派发 → ReleaseCapture
```

修复"按下移出后 Up 不达、m_pressed 卡死"。Windows 原生 API 经典行为；Button/TextBox/Slider/Scrollbar/Splitter 未来全依赖。

### I6 Button 按下态 —— ⚠️ GPT 修正：拖出释放取消点击 ✅

原方案：Up 必触发 OnClick。**GPT 修正（标准 GUI 行为）**：**"按下→移出→释放"应取消点击**——只有"按下且释放时鼠标仍在按钮内"才触发。

```cpp
void Button::OnMouseButtonUp(const MouseButtonUpEvent& event){
	m_pressed = false;
	if (鼠标仍在自身区域内)   // 绝对坐标比较；坐标转换细节初步设计定
		OnClick();
	Invalidate();
}
```

- 按下变深色：`m_pressed ? FromRGBA8(60,90,180) : FromRGBA8(80,120,220)`；Down/Up 各 Invalidate
- 坐标判断方式（绝对坐标如何获得）初步设计固化

### I7 main.cpp 验证 —— A ✅（GPT：少断言，人工交互为主）

Focus/Capture/Invalidate 是**状态机**——写大量断言意义低。**人工交互验证为主**：

- Tab 在两按钮间切换（焦点框移动）
- 按下按钮变色、释放恢复
- **拖出取消**：按下→移出→释放→**不触发** OnClick（GPT 修正的行为验证）
- 按住拖出释放不触发、在按钮内释放触发
- 断言只保留最基础的（如 HasFocus 状态一条）

## 待初步设计固化

1. Widget→Window 关联的精确形态（RootWidget 设置时机、Invalidate 上溯实现）
2. Window::HandleKeyDown / FocusNext 的实现（树遍历收集 CanFocus + 循环）
3. HasFocus() 与焦点边框绘制（两命令叠层坐标）
4. Capture 在 Application 鼠标派发逻辑的落点（Down 设置 / Move-Up 直接派发 / Up 释放）
5. Button Up 时"鼠标是否在自身区域"的坐标判断（绝对坐标来源）
6. Button 按下态配色与 Invalidate 时机
