# Phase 5.5 TextBox 职责确认

> 状态：v1.0（2026-08-13）｜职责确认完成，待初步设计
> 相关：phase5-text-requirements（5.1）/ phase5-label-requirements（5.2）/ phase5-button-requirements（5.3）/ phase5-interaction-requirements（5.4）

## 1. 代码事实（5.5 起点）

- **输入链路已通**：`WM_CHAR → CharInputEvent(char32_t 码点) → Application::OnCharInput → 焦点控件->OnCharInput`
- **键盘已通**：`OnKeyDown → Window::HandleKeyDown`（Tab 拦截 + 焦点控件派发，5.4.4）
- **5.4 基础设施全就绪**：CanFocus / OnFocusGained/Lost / Invalidate / HasFocus / GetAbsolutePosition / Mouse Capture
- **TextWidget 可继承**：TextBox 是第三个文本控件（Widget → TextWidget → TextBox）
- **缺的**：测量访问路径（5.2 债务）、KeyEvent 修饰键（5.4 债务）、文本模型（码点级编辑）、光标、Selection

## 2. 核心决策（T1-T6）

### T1 测量能力访问路径（5.2 架构债务落地）—— A ✅

- 背景：TextMeasurer 只经 PaintContext 注入——控件非 Paint 时刻拿不到。TextBox 鼠标点击定位光标（x 坐标 → 最近码点索引）发生在**事件阶段**，需要测量。
- **A：Window 提供 `TextMeasurer& GetTextMeasurer()`**——Window 持有 GDIBackend（5.1 双接口兼 TextMeasurer），控件经 protected `GetWindow()` 获取。
- 符合唯一入口原则（框架服务从 Window 进）；TextMeasurer 接口本身零改动。
- 否决 B（布局注入，链路绕）/ C（全局/静态，多窗口破坏）。

### T2 文本模型 + 光标索引 —— A ✅

- 背景：TextBox 继承 TextWidget（`m_text` 是 UTF-8 `std::string`）。光标/删除必须按**码点**操作（UTF-8 变长：字节索引 ≠ 码点索引，中文/emoji 切半个字）。绘制要字节偏移（DrawText 画前缀）、测量按字节（GDIBackend 转 UTF-16）。
- **A：m_text 保持 string + 光标存码点索引** + 内部小工具（码点索引 ↔ 字节偏移双向转换）。
- 单一文本源（m_text），转换函数封闭在 TextBox 内部。
- 否决 B（码点数组双轨维护，与 m_text 同步复杂）。

### T3 光标形态 —— A ✅（静态）

- 背景：标准文本框光标闪烁（~500ms），但闪烁需要定时器（WM_TIMER + Window 消息 + TextBox 状态）——框架目前无 Timer/Scheduler/Animation 基础设施。
- **A：静态光标**（焦点时画竖线，不闪烁）——零定时器。
- 闪烁是纯观感，未来加（不动架构：Timer + 状态即可）。
- 否决 B（为观感引入定时器机制，YAGNI）。

### T4 Selection —— ⚠️ 分阶段（5.5.1 不做 / 5.5.2 做）✅

- 背景：Selection 不是单一功能，是**编辑系统的横切面**——插入 / Backspace / Delete / 替换全部要感知选中区（有 Selection 删整区，无则正常单字符）。
- **修正（GPT）**：推迟到 5.5.2（与修饰键一起，Shift+方向键依赖修饰键）。
- 5.5.1 MVP 纯输入编辑，Selection 渗透面大，分开风险小。
- 5.5.2 范围：鼠标拖选（Capture 已有）+ Shift+方向键 + 高亮绘制 + 编辑操作感知选中区。

### T5 KeyEvent 修饰键扩展（5.4 债务兑现）—— A ✅（位标志）

- 背景：5.4 记账——KeyEvent 加修饰键状态。Shift+Tab 反向（5.4 暂缓）、Shift+方向键（Selection）、未来 Ctrl+A/C/V 全依赖。
- **实现（GPT 修正）：位标志**——
  ```cpp
  enum class KeyModifier { None = 0, Shift = 1, Ctrl = 2, Alt = 4 };   // 可组合（Ctrl+Shift+A）
  ```
  而非三个 bool。KeyDownEvent/KeyUpEvent 加修饰字段 + `IsShiftDown()/IsCtrlDown()/IsAltDown()`。
- **修饰状态来源**：Win32 翻译器（WindowMessageHandler）在翻译 WM_KEYDOWN 时用 `GetKeyState(VK_SHIFT/CONTROL/MENU)` 查询填入——**平台层翻译器内查询不违反分层**（"Win32 负责翻译"的本职）；5.4 否决的是 Window 层查询系统状态，不是翻译器。
- 位置：5.5.2 开头（Selection 的依赖）。

### T6 编辑操作集 —— A ✅（MVP 一次做齐）

- 5.5.1 基础编辑一次做齐（每项小）：**OnCharInput 插入码点 / Backspace 删光标前 / Delete 删光标后 / ←→ 移动光标 / Home/End 跳头尾**。
- 不做：Undo/Redo、剪贴板 Ctrl+C/V/X、多行、滚动。

## 3. 范围决策（T7-T9）

### T7 绘制样式 —— A ✅

- **白底黑字 + 光标竖线 + 焦点内框**（复用 Button 模式）——标准文本框观感，OnPaint = DrawRect(白底) → DrawText → DrawRect(光标)。
- Selection 高亮（浅蓝底）5.5.2 加（DrawRect 高亮块 → DrawText 的绘制顺序）。
- 否决 B（透明底——"输入控件"身份不明确）。

### T8 超长文本（单行溢出）—— A ✅（裁切）

- 文本宽度超过控件宽度：**裁切**（画不下的截断；光标可能超出可视区——第一版接受此限制，注释说明）。
- 否决 B（滚动——独立子系统，ScrollBar/ScrollArea 归 Phase 6）。

### T9 验证 + 边界 —— A ✅

- main.cpp 加 TextBox（win1 面板内）。
- **5.5.1 验证**：输入显示 / 光标移动 / Backspace/Delete / Home/End + 断言段（插入码点 → m_text 变化、光标移动、边界）。
- **5.5.2 验证**：拖选高亮 / Shift+方向键 / 替换与删除选中区。
- **IME 记账（GPT 提醒）**：TextBox 支持 UTF-8（直接码点输入）但**中文输入法组合实际走 WM_IME_***——5.6 IME 完成前"能显示中文、不能通过输入法输中文"。提前记账，避免做到一半才发现。

## 4. 实施顺序（两阶段，每步可编译可测）

```
5.5.1 TextBox MVP
  ① T1 Window 测量服务（地基）
  ② T2 文本模型（码点索引工具）
  ③ T3 静态光标 + T7 绘制（白底/光标/焦点框）
  ④ T6 基础编辑（输入/Backspace/Delete/←→/Home/End）
  → 验证：输入显示、光标移动、删除、Home/End

5.5.2 TextBox 高级
  ⑤ T5 KeyEvent 修饰键（位标志 + 翻译器填入）→ Shift+Tab 反向顺带落地
  ⑥ T4 Selection（拖选 + Shift+方向键 + 高亮 + 编辑感知选中区）
  → 验证：拖选高亮、Shift+方向键选择、替换/删除选中区
```

## 5. 边界（不做清单）

- 多行 / 滚动（Phase 6）
- IME / 中文输入法组合（5.6，可裁剪——记账见 T9）
- Undo/Redo、剪贴板（Ctrl+C/V/X）
- 光标闪烁（T3 静态，未来加）
- 自动尺寸 / GetPreferredSize（测量债务虽解，但 AutoSize 是布局系统的事——未来 Layout 增强时再定）
- 掩码 / 密码框 / 只读模式（未来需求再定）

## 6. 修订记录

- v1.0（2026-08-13）职责确认：T1-T9 定稿。T4 分阶段（GPT 修正：Selection 是编辑系统横切面，推迟 5.5.2）；T5 位标志 KeyModifier（GPT 修正：可组合）；实施拆 5.5.1 MVP / 5.5.2 高级（GPT 重排 + 修正 Selection→修饰键依赖序）；IME 提前记账。
