# Phase 5 架构回顾：输入责任分布评估

> 状态：v1.0（2026-08-15）｜结论定稿（用户确认）
> 相关：phase3-focus-design.md（5.4 焦点设计）/ phase5-ime-*（5.6 引入 caret 中介）
> 背景：技术债"5.4 架构回顾（Mouse/Keyboard/Focus/Capture 责任交叉，评估 InputManager/FocusManager）"在 Phase 5 结束后到期

## 1. 现状责任分布（代码核实）

| 责任 | 归属 | 依据 |
|---|---|---|
| 鼠标分发（HitTest → Target → Bubbling） | Application（OnMouseMove/Down/Up/Wheel） | Application.cpp 149-268 |
| 捕获优先于 HitTest（5.4.2） | Application + Window::m_captureWidget | OnMouseMove/Up 先查 GetCaptureWidget |
| 焦点状态（m_focusedWidget + SetFocusedWidget + FocusNext） | **Window** | Window.h/cpp |
| Tab 拦截 + 键盘派发 | **Window::HandleKeyDown** | 5.4.4 |
| 键盘字符/KeyUp 直派 | Application（FindFocusedWidget） | 不 Bubbling（3.2 决策） |
| IME caret（UpdateTextInputCaret/Destroy） | **Window** | 5.6 v1.0.4 |

**结论：三层职责清晰**——Application=分发编排、Window=状态持有+焦点导航+平台中介、Widget=接收。无"责任打架"，但有入口不对称与一处冗余。

## 2. 发现的问题

### ① 重复 SetFocusedWidget（已修，R1）
- **位置**：Application::OnMouseButtonDown——197 行（5.4.3 修正版：`CanFocus ? target : nullptr`）与 205 行（5.4.3 之前遗留：`if (CanFocus) Set`）**两次设置同一目标**
- **影响**：无功能 bug（SetFocusedWidget 同控件短路），纯冗余
- **处理**：删 205 行冗余块（R1，本次已改）

### ② 键盘入口不对称（记账，R2）
- OnKeyDown → Window::HandleKeyDown（Tab 拦截必需）；OnKeyUp/OnCharInput → Application 直派
- **合理性**：Tab 拦截必须走 Window（焦点导航是 Window 职责）；其余直派是 3.2 决策（键盘不 Bubbling）
- **风险**：未来加全局快捷键/InputManager 时 3 个入口要统一
- **处理**：保持现状，记账；未来全局输入需求出现时再统一

### ③ 多窗口焦点语义（记账，R3）
- 现状：焦点/捕获是**窗口级**（每 Window 独立）——语义如此（Win32 焦点窗口级）
- **风险**：未来需要"应用级焦点"（全局快捷键/IME 全局态）时需引入窗口归属概念
- **处理**：记账；Phase 7 平台抽象时评估

## 3. InputManager / FocusManager 评估（YAGNI）

**结论：不引入**。契约语言论证：

1. **现状职责边界清晰**：回顾前担心"责任交叉"，代码核实后实为**分工明确**（三层各司其职），引入管理器是解决不存在的问题
2. **多窗口焦点/捕获天然窗口级**：语义如此，集中到应用级管理器反而引入"窗口归属"额外概念
3. **5.6 验证的"控件经 Window 访问平台"模式**（TextBox → GetWindow() → caret/IME）与 InputManager 正交——输入状态管理不解决平台中介问题
4. **引入成本 > 收益**：新类 + Application/Window 调用链重构，收益仅为"入口统一"（现状可工作、语义清晰）
5. **触发条件**（满足任一再评估）：全局快捷键系统 / 跨窗口输入状态共享需求 / Phase 7 平台抽象后出现新输入维度

## 4. 决策记录

| # | 决策 | 状态 |
|---|---|---|
| R1 | 删 OnMouseButtonDown 重复 SetFocusedWidget | ✅ 已改（Application.cpp） |
| R2 | 键盘入口不对称保持现状，记账 | ✅ 记账（README） |
| R3 | 多窗口焦点语义记账，Phase 7 评估 | ✅ 记账（README） |
| R4 | 本文档落盘 | ✅ 本文档 |

## 5. 修订记录

- v1.0（2026-08-15）回顾结论定稿：不引入 InputManager/FocusManager；R1 冗余修复；R2/R3 记账。
