# Phase 5.6 IME 职责确认

> 状态：v1.0（2026-08-14）｜职责确认完成，待初步设计
> 相关：phase5-textbox-requirements.md（5.5）/ phase5-selection-requirements.md（5.5.2）

## 1. 代码事实（5.6 起点）

- **当前无 WM_IME_* 处理**（走 DefWindowProc 系统默认）
- **实测：中文输入法汉字能上屏**（IME 结果走 WM_CHAR → CharInputEvent → TextBox.InsertCodepoint）——5.6 是**体验增强**非必需功能
- **问题**：候选窗口飘在屏幕左上角（系统默认位置，不跟随 TextBox 光标）；组合串（拼音半成品）不在 TextBox 内渲染

## 2. 决策记录（I1-I5 + GPT 修订）

### I1 范围 —— A ✅（只做候选窗口跟随）

- **A：候选窗口跟随光标**——WM_IME_STARTCOMPOSITION 时 `ImmSetCompositionWindow` 设到 TextBox 光标客户区坐标（转屏幕）
- **否决 B（组合串内嵌）**：GPT 论证——B 是"文本系统 2.0"非体验增强，引入 6 个问题（组合串是否 Text 一部分 / Selection / Backspace / Delete / 取消组合 / 候选切换），全是编辑系统升级，超 Phase 5 范围
- **否决 C（裁剪）**：用户倾向做，A 解决最明显的体验问题
- **5.6 一句话定义（GPT）**：**不改 TextBox 编辑系统，只补 Window↔IME 桥接层**

### I2 分层方案 —— A ✅（Window 中介）

- **A：翻译器收 WM_IME_* → Window::NotifyIMEComposition() → 焦点控件 GetCaretClientPosition() → Window 层 ClientToScreen + ImmSetCompositionWindow**
- 与 GetTextMeasurer 同款模式（Widget→Window→Platform API）
- 否决 B（翻译器直接查控件状态——分层破坏）
- **TextBox 零平台依赖**（不碰 Imm/HIMC/COMPOSITIONFORM/ClientToScreen/HWND——平台代码全在 Window 及以下）

### I3 坐标接口 —— GPT 修正 ✅

- **`Point GetCaretClientPosition() const`**（TextBox 返回客户区坐标——纯计算 GetAbsolutePosition + textPos + prefixSize）
- 否决 `GetCaretScreenPosition()`（GPT：ScreenPosition 是平台概念，TextBox 只负责客户区，Window 负责 ClientToScreen——职责清晰）

### I4 InsertText —— 不做 ✅

- I1=A 时 WM_CHAR 仍工作（汉字已上屏验证），不需要多码点插入
- 否决提前引入 InsertText（会连带 Clipboard/IME/Paste/Multi-line/Undo-Redo 未来需求）

### I5 范围控制 —— A ✅

- **做**：WM_IME_STARTCOMPOSITION 处理（候选跟随）+ WM_IME_ENDCOMPOSITION **空通道预留**（GPT 建议——START/COMPOSITION/END 生命周期完整，未来 B 升级通道就位）
- **不做**：组合串内嵌渲染 / 自定义候选框 UI / 剪贴板 / 多码点上屏 / IME 光标控制（GCS_CURSORPOS）/ Reconvert
- **可选**：WM_IME_COMPOSITION 重新定位（第一版 START 一次大概率够用——打字时光标不动候选窗口不动）

## 3. 与 Phase 7 平台抽离的关系（用户提问，已澄清）

- **不影响架构**：5.6 平台代码（Imm/ClientToScreen）暂存 Window 层，与 GDIBackend（决策 35 值成员）**同性质**——Phase 7 统一下沉
- **Phase 7 迁移清单追加**：Window::NotifyIMEComposition 的 Imm 部分 → `PlatformWindow::SetIMECompositionPosition`；WindowMessageHandler → Win32PlatformWindow 内部
- **底线**：TextBox 零平台依赖（GetCaretClientPosition 纯客户区坐标）——公共层干净，Phase 7 只搬 Window 层平台代码
- **不提前预留 PlatformWindow**（YAGNI——无第二个平台验证接口形态）

## 4. 实现面预估（极小）

- WindowMessageHandler.cpp：WM_IME_STARTCOMPOSITION/END 两个 case
- Window.h/cpp：`NotifyIMEComposition()`（ClientToScreen + ImmSetCompositionWindow）
- TextBox.h/cpp：`GetCaretClientPosition()`
- include `<Imm.h>` + 链接 Imm32（vcxproj/CMake）

## 5. 修订记录

- v1.0（2026-08-14）职责确认：I1-I5 定稿。I1 A（GPT：B 是文本系统 2.0，6 问题论证）；I2 Window 中介；I3 GetCaretClientPosition（GPT 修正——Client 职责归控件、Screen 归 Window）；I4 不加 InsertText；I5 最小范围 + END 通道预留（GPT）；与 Phase 7 关系澄清（同 GDIBackend 性质，Phase 7 下沉）。
