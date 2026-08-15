# Phase 5.5.2 TextBox Selection + 修饰键 职责确认

> 状态：v1.0（2026-08-14）｜职责确认完成，待初步设计
> 相关：phase5-textbox-requirements.md（5.5 总）/ phase5-interaction-requirements.md（5.4 交互）

## 1. 代码事实（5.5.2 起点）

- **KeyEvent 只有 m_keyCode**（无修饰键）——修饰键是 5.4 记账债务
- **WM_KEYDOWN 翻译在 WindowMessageHandler**（平台翻译器）——修饰键填入点正确（翻译器内 GetKeyState，分层允许）
- **HandleKeyDown**（5.4.4）：Tab → FocusNext(1)，Shift+Tab 反向待落地（FocusNext(direction) 参数已就位）
- **TextBox**：CaretIndexFromX 已预留（点击定位）、Capture 已就绪（拖选基础）、编辑操作与事件解耦（Selection 横切面好改）

## 2. 决策记录（M1-M2 + S1-S7）

### M1 KeyEvent 修饰键扩展（5.4 债务兑现）—— A ✅

- **`enum class KeyModifier{ None=0, Shift=1, Ctrl=2, Alt=4 }`**（位标志，可组合——Ctrl+Shift+A 未来自然支持）
- KeyEvent 加 `m_modifier` + **`HasModifier(KeyModifier)` 辅助**（GPT 补充：位与判断）+ `IsShiftDown()/IsCtrlDown()/IsAltDown()`（基于 HasModifier）
- KeyDown/KeyUp 构造加 modifier 参数；翻译器 WM_KEYDOWN/UP 时 `GetKeyState(VK_SHIFT/CONTROL/MENU)` 填位（**平台翻译器内查询，不违反分层**——5.4 拍板）
- **字段全做**（Shift/Ctrl/Alt）：已拍板的债务一次定型，未来 Ctrl+A/C/V 零改动；第一版只消费 Shift（Selection 需要）

### M2 Shift+Tab 反向落地 —— A ✅

- `Window::HandleKeyDown`：Tab 时 `event.IsShiftDown() ? FocusNext(-1) : FocusNext(1)`——5.4 留的尾巴（direction 参数就位），M1 提供能力后顺手落地

### S1 Selection 数据模型 —— A ✅（GPT 修订：anchor/active 语义）

- **`m_selectionAnchor`（锚点，固定端）+ `m_caret`（活动端 active）**——不是 start/end！
- 关键洞察（GPT）：**光标 = active 端**——Shift+←→ 就是移动光标；`ab|cdef → Shift+→ → ab[c]def → Shift+← → ab|cdef`（收缩回 anchor，标准行为）
- 辅助：`HasSelection()`（anchor != caret）/ `GetSelectionMin()` / `GetSelectionMax()`（绘制用——拖选可能反向，min/max 统一封装防几十次 std::min/max 散落）
- 否决 optional<pair>（操作啰嗦）

### S2 拖选 —— A ✅

- **`m_mouseDown`（TextBox 内部状态）**——Down 置 true / Up 置 false；与 Window Capture 机制解耦（Capture 是窗口级机制、拖选是控件行为——GPT：不绑死，未来程序释放 Capture/失焦状态可推导）
- MouseDown：caret = CaretIndexFromX + **anchor = caret**（准备拖）
- MouseMove（m_mouseDown 时）：caret = CaretIndexFromX + Invalidate
- MouseUp：m_mouseDown = false（保留选择）

### S3 Shift+方向键 —— A ✅（GPT 修订：anchor 固定 + active 移动）

- Shift+←/→：移动 m_caret（active），anchor 不动
- Shift+Home/End：扩展到头/尾
- 无 Shift 的 ←/→/Home/End：**清选择**（anchor 无效化）+ 移动光标

### S4 编辑感知选中区（Selection 横切面）—— A ✅（GPT：DeleteSelection 返回新 caret）

- **`size_t DeleteSelection()`**（GPT：返回删除后光标位置 = min 处）——删除逻辑抽一次，三处复用
- InsertCodepoint：`if (HasSelection()) m_caret = DeleteSelection();` 再插入
- DeleteBackward/DeleteForward：`if (HasSelection()){ m_caret = DeleteSelection(); return; }`（一次删完，非单字符）
- 删除/插入后无选择（anchor 无效化）

### S5 高亮绘制 —— A ✅

- OnPaint：白底 → **Selection 高亮块（浅蓝底）** → 文本 → 光标
- **高亮与文本裁切共享 maxTextWidth**（GPT：文本裁切 → Selection 裁切 → 光标钉右缘 共享同一可视宽度——高亮不溢出）
- 高亮块 = GetSelectionMin/Max 前缀宽度区间（两次 MeasureText）

### S6 Ctrl+A —— ⚠️ 推迟（GPT 用户预期一致性）✅

- **统一推迟 Ctrl+A/C/V/X** 到剪贴板子系统（Phase 6+）——"Ctrl+A 有而 C/V 没有"体验割裂，Ctrl+A 是剪贴板时代入口，单独做会诱导尝试其他快捷键
- 否决我初稿的"顺手做 Ctrl+A"

### S7 范围控制 + 验证 —— A ✅

- **必须做**：M1 / M2 / S1 / S2 / S3 / S4 / S5
- **明确不做**：双击选词 / 三击选行 / 剪贴板 Ctrl+C/V/X / Ctrl+A / 自动滚动 / 多行 / IME / Undo/Redo
- **验证**：人工交互（拖选高亮 / Shift+方向键含反向收缩 / 输入替换选中区 / Backspace 删选中区 / 点击取消选择 / Shift+Tab 反向）+ 少量断言（S4 编辑感知逻辑——DeleteSelection/插入替换的正确性）

## 3. 修订记录

- v1.0（2026-08-14）职责确认：M1-M2 + S1-S7 定稿。S1 anchor/active 语义（GPT 修订——光标=active，Shift 反向收缩标准行为）；S2 m_mouseDown（GPT：控件内部状态与 Window 机制解耦）；S3 anchor 固定 active 移动（GPT）；S4 DeleteSelection 返回新 caret（GPT）；S6 Ctrl+A 推迟（GPT 用户预期一致性）；M1 HasModifier 辅助（GPT）。
