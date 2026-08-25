# Phase 8.5.3 文本系统 2.0 详细设计（Undo/Redo）

> 状态：v1.1（2026-08-25）｜定稿待审（GPT 评审整合）
> 前序：Phase 8.5.2 完结 ✅（commit edfde46）/ 8.5.1 完结 ✅（commit 8ab8300）
> 相关：phase8.5-text-system2.0-preliminary-design.md（B6 Undo Snapshot / C3 Composition 与 Undo / C4 Push 时机）
> 拆分说明：本文件 = 8.5.3 专属详细设计（原草案 §10 定稿）

---

## 1. 范围（8.5.3）

```
Undo/Redo：快照模式（B6）+ 编辑前 Push（C4）+ Composition 与 Undo 衔接（C3）+ Ctrl+Z/Y
实现 + TestCase F35-F43 + 视觉验证
```

**代码事实（2026-08-25 核对）**：
- Undo 机制**完全未实现**——`UpdateComposition` 注释"（Undo 快照 8.5.3 落地，此处留调用点）"为承诺未兑现；`PushUndoSnapshot`/栈成员均不存在
- `SelectionRange` 为 TextBox 内嵌 struct（public，`{start, end}` 码点索引）
- `GetSelection()` 返回 `std::optional<SelectionRange>`（const，只读）——快照捕获可复用
- 编辑操作各自独立、无统一入口：`InsertCodepoint` / `InsertText` / `DeleteBackward` / `DeleteForward` / `CutSelectionToClipboard` / `PasteFromClipboard`（→InsertText）/ `CommitComposition`——Push 需逐操作挂载
- `OnKeyDown` Ctrl 分支（C/V/X/A）已有统一公共尾部——Z/Y 直接加 case
- 多行已就绪：`m_scrollOffsetY` / `m_needsLineRecalc` / `EnsureCaretVisible`（恢复快照后直接复用）

## 2. UndoSnapshot 结构（B6 落地）

```cpp
// TextBox.h private 区：
struct UndoSnapshot {
    std::string text;                        ///< 完整文本（UTF-8）
    size_t caret;                            ///< 光标码点索引
    std::optional<SelectionRange> selection; ///< 选区（nullopt = 无选区）
    float scrollOffsetY;                     ///< 滚动偏移（像素）
};
```

**Selection 方向取舍（D9）**：快照只存 `{min, max}`（`GetSelection()` 返回值），恢复时固定 `anchor=start, caret=end`（正向）。方向信息（Shift+方向选择的 active 端）不恢复——视觉无差、MVP 可接受；第二次用例出现再扩展。

**存储**：
```cpp
std::vector<UndoSnapshot> m_undoStack;          ///< 撤销栈（栈顶 = 最近一次编辑前状态）
std::vector<UndoSnapshot> m_redoStack;          ///< 重做栈（栈顶 = 最近一次撤销前状态）
static constexpr size_t kMaxUndoDepth = 100;    ///< 撤销深度上限（防内存爆炸）
bool m_compositionPushedUndo = false;           ///< 本次组合是否已 Push（取消组合时弹回）
```

**redo 栈深度说明**：redo 栈仅在 Undo 时 push（每次 ≤ 一次）——上限天然 ≤ undo 深度 ≤ 100，无需额外 clamp。

## 3. Push 时机契约（C4——挂载点清单）

**原则**：编辑操作**前** Push 当前状态 → 执行修改 → 清 redo。快照记录"修改前"状态。

| 操作 | Push？ | 挂载点 | 说明 |
|---|---|---|---|
| InsertCodepoint | ✅ | 函数开头（修改前） | 字符输入 |
| InsertText | ✅ | 空串 return 之后 | 粘贴/Enter 换行/程序调用 |
| DeleteBackward | ✅ | 空操作检查之后 | 见下方重构 |
| DeleteForward | ✅ | 空操作检查之后 | 见下方重构 |
| CutSelectionToClipboard | ✅ | 无选区 return 之后 | Ctrl+X |
| PasteFromClipboard | — | — | 经 InsertText 内部已 Push |
| CommitComposition | ❌ | — | 组合开始已 Push（C3） |
| CancelComposition | 弹回 | 见 §6 | 取消 = 状态回快照点 |
| UpdateComposition 首次非空 | ✅ | 见 §6 | C3 唯一例外（组合开始） |
| UpdateComposition 过程 | ❌ | — | 组合中不逐次 Push |
| MoveCaret / Selection / Scroll / Blink | ❌ | — | 纯状态变化（C4） |

**DeleteBackward/DeleteForward 重构**（合并空操作检查，避免空操作 Push 无意义快照）：

```cpp
void TextBox::DeleteBackward(){
    // 空操作检查（不 Push——D7 边界语义：无变化不产生快照）
    if (!HasSelection() && m_caret == 0)
        return;
    PushUndoSnapshot();   // 编辑前快照（C4）
    if (HasSelection()){
        // ...原删选中区分支...
        return;
    }
    // ...原单字符分支...
}
// DeleteForward 同构：if (!HasSelection() && m_caret >= GetCodepointCount()) return;
```

## 4. Undo/Redo 执行（栈流转 + RestoreSnapshot）

```cpp
// TextBox.h public 区（同 InsertText 先例——对外可用，测试直接调）：
void Undo();   ///< 撤销：栈空 no-op；当前状态 → redo，恢复栈顶，弹栈
void Redo();   ///< 重做：栈空 no-op；当前状态 → undo，恢复栈顶，弹栈

// TextBox.cpp：
void TextBox::Undo(){
    if (m_isComposing)
        return;   // 组合态防御（GPT 评审补充）：IME 占用输入通道，键盘撤销不中断组合
    if (m_undoStack.empty())
        return;   // 空栈 no-op（F38）
    m_redoStack.push_back(CaptureCurrentState());   // C4：当前状态 → RedoStack
    RestoreSnapshot(m_undoStack.back());
    m_undoStack.pop_back();
}

void TextBox::Redo(){
    if (m_isComposing)
        return;   // 同上
    if (m_redoStack.empty())
        return;
    m_undoStack.push_back(CaptureCurrentState());   // 对称
    RestoreSnapshot(m_redoStack.back());
    m_redoStack.pop_back();
}
```

**CaptureCurrentState / RestoreSnapshot / PushUndoSnapshot**（private）：

```cpp
UndoSnapshot TextBox::CaptureCurrentState() const{
    UndoSnapshot s;
    s.text = m_text;
    s.caret = m_caret;
    s.selection = GetSelection();   // 复用现有只读查询（min/max）
    s.scrollOffsetY = m_scrollOffsetY;
    return s;
}

void TextBox::PushUndoSnapshot(){
    m_undoStack.push_back(CaptureCurrentState());
    if (m_undoStack.size() > kMaxUndoDepth)
        m_undoStack.erase(m_undoStack.begin());   // 超限丢最旧
    m_redoStack.clear();   // 新编辑 → 作废 redo 分支（C4）
}

void TextBox::RestoreSnapshot(const UndoSnapshot& s){
    m_text = s.text;
    m_caret = s.caret;
    if (s.selection){
        m_selectionAnchor = s.selection->start;   // 方向固定正向（D9）
        m_caret = s.selection->end;
    }
    else{
        m_selectionAnchor = m_caret;   // ClearSelection 语义
    }
    m_scrollOffsetY = s.scrollOffsetY;
    m_needsLineRecalc = true;   // 8.5.2：文本恢复 → 行缓存失效（惰性重算已在消费者保证）
    Invalidate();
    SyncTextInputCaret();
    EnsureCaretVisible();   // 恢复后统一 clamp（文本变短 → maxScroll 变小）
    RaiseTextChanged();   // D4：Undo/Redo 实际改变文本 → 通知（表单脏标记等外部观察者）
}
```

## 5. Composition 与 Undo 衔接（C3——8.5 易踩坑点）

```
[组合开始] 首次 UpdateComposition 非空 → PushUndoSnapshot（记录组合开始前状态）
[组合过程] UpdateComposition 每次更新 → 不 Push（组合串临时编辑）
[Commit]   CommitComposition → 不再 Push（快照已在开始推——Ctrl+Z 一次撤销整个组合）
[Cancel]   CancelComposition → 状态已回到组合开始前（= 栈顶快照）→ 弹回该快照
```

**UpdateComposition 修改**（8.5.1 注释预留调用点兑现）：

```cpp
void TextBox::UpdateComposition(const std::string& compositionText){
    if (!m_isComposing){
        m_isComposing = true;
        m_compositionStart = m_caret;
        if (!compositionText.empty()){
            PushUndoSnapshot();   // 8.5.3：组合开始前快照（C3）
            m_compositionPushedUndo = true;
        }
    }
    // ...其余不变（ReplaceTextRange 替换区间 + 不 RaiseTextChanged）...
}
```

**CancelComposition 修改**（GPT 评审修正——🔴 弹栈必须**真正恢复快照**，不能只 pop）：

```cpp
void TextBox::CancelComposition(){
    if (!m_isComposing)
        return;
    if (m_compositionPushedUndo && !m_undoStack.empty()){
        // 取回组合开始前快照 → 弹栈 → 完整恢复（文本/光标/选区/滚动——
        // 模型 B 中组合串已在 m_text，仅 pop 不恢复会残留拼音占位）
        const UndoSnapshot snapshot = m_undoStack.back();
        m_undoStack.pop_back();
        RestoreSnapshot(snapshot);
        m_compositionPushedUndo = false;
        m_isComposing = false;
        m_compositionText.clear();
        m_compositionLength = 0;
        m_compositionStart = 0;
        m_compositionCaret = 0;
        return;   // 状态已完全恢复——原"擦除组合区间"逻辑不再执行（区间已在快照文本中不存在）
    }
    // 无 Push 过的组合（首次空串开始、后续才有内容）：走原擦除逻辑兜底
    m_caret = m_compositionStart;
    ReplaceTextRange(m_compositionStart, m_compositionStart + m_compositionLength, {});
    m_isComposing = false;
    m_compositionText.clear();
    m_compositionLength = 0;
    m_compositionCaret = 0;
    ClearSelection();
    m_needsLineRecalc = true;
    Invalidate();
    SyncTextInputCaret();
    EnsureCaretVisible();
}
```

**Cancel vs Undo 语义表**（GPT 评审补充——写进契约）：

| 操作 | 恢复快照 | 移除快照 | 当前状态进 Redo |
|---|---|---|---|
| Undo | ✅ | ✅ | ✅ |
| CancelComposition | ✅ | ✅ | ❌（取消不是编辑——不产生可重做历史） |

**边界**：
- 首次 UpdateComposition 空串（组合开始但无内容）→ 不 Push（无实际变化）、`m_compositionPushedUndo` 保持 false
- Commit 空结果（C12：`CommitComposition("")`）→ 组合区间删除，仍由组合开始快照覆盖（Ctrl+Z 恢复组合前文本）
- **组合期间 Ctrl+Z/Y 防御**（GPT 评审补充）：`Undo()/Redo()` 开头 `if (m_isComposing) return;`——组合状态占用输入通道，键盘撤销不中断组合（fail-safe，不崩）；IME 通常拦截组合态快捷键，此为防御路径
- 组合期间的新编辑？——不可能：Composition 期间用户输入走 IME 通道（无 OnCharInput 直插），无并发编辑路径
- **一次用户可感知编辑 = 恰好一次 Push**（GPT 评审补充契约）：InsertCodepoint 与 InsertText **各自独立实现、互不嵌套**（已核对代码事实——InsertCodepoint 直接 insert，InsertText 走 ReplaceTextRange）；未来重构不得在两者间建立嵌套调用而不合并 Push

## 6. 快捷键（Ctrl+Z / Ctrl+Y）

`OnKeyDown` Ctrl 分支加两个 case（C1 契约：快捷键 = KeyDown 语义，非独立 Event）：

```cpp
case KeyCode::Z:    Undo(); break;
case KeyCode::Y:    Redo(); break;
// 现有 C/V/X/A 分支不变——公共尾部（SyncTextInputCaret/EnsureCaretVisible/ResetPreferredColumn）统一执行
```

（`KeyCode::Z/Y` 需实施时确认枚举存在——字母键位，预期有）

## 7. 边界与契约总结

| # | 契约 |
|---|---|
| C4 | 文本内容改变 → Push（编辑前快照）；纯光标/Selection/Scroll/Blink → 不 Push |
| C4b | 新编辑 Push 后清空 redo 栈（新分支作废旧分支） |
| C4c | **一次用户可感知编辑 = 恰好一次 Push**（InsertCodepoint/InsertText 独立实现互不嵌套——代码事实已核；重构不得破坏） |
| C3 | Composition 过程不逐次 Push；首次非空 Push 一次；Commit 不再 Push；**Cancel = 完整恢复快照 + 弹栈（当前状态不进 Redo）** |
| C3b | **组合态 Ctrl+Z/Y 防御**：Undo/Redo 开头 `if (m_isComposing) return;`（IME 占用输入通道，键盘撤销不中断组合） |
| D4 | Undo/Redo 触发 RaiseTextChanged（文本实际变化——外部观察者感知） |
| D9 | Selection 方向恢复为正向（{min,max} → anchor=min, caret=max） |
| D7 | 空操作不 Push（DeleteBackward 头边界/DeleteForward 尾边界/InsertText 空串/无选区 Cut） |
| 深度 | undo 栈上限 100（超限丢最旧）；redo 栈天然 ≤100 |
| 恢复 | 快照恢复含 scrollOffsetY + 行缓存失效 + EnsureCaretVisible clamp |

## 8. 8.5.3 TestCase（F35-F43，TextBoxTests.cpp 新增，7.2 体系）

| # | 测试 | 断言点 | 说明 |
|---|---|---|---|
| F35 | 基本 Undo/Redo | "abc" 插入 'd' → Undo → "abc"/caret=3 → Redo → "abcd"/caret=4 | 快照全链路 |
| F36 | 连续输入逐级撤销 | 插入 a、b → Undo → "a" → Undo → "" → Undo no-op | 每编辑一快照 |
| F37 | 新编辑清 redo | Undo 后插入 'x' → Redo no-op | C4b |
| F38 | 空栈 no-op | 初始 Undo/Redo → 不崩、文本不变 | 边界 |
| F39 | 空操作不 Push | 空文本 DeleteBackward → Undo no-op | D7 |
| F40 | Composition 一次撤销 | "abc"+组合"nihao"+Commit"你好" → Undo → "abc"（整个组合一次撤销） | C3 核心 |
| F41 | 快照含滚动/多行 | "abc\ndef" 末尾插入 → Undo → 文本/光标/scrollOffsetY 恢复 | 快照完整性 |
| F42 | InsertText Undo | InsertText("xy") → Undo → 恢复 | 粘贴/Enter 路径 |
| F43 | Cut Undo | 有选区 Cut → Undo → 文本与选区恢复 | 剪切路径 |
| F44 | Composition Cancel（GPT 新增） | "abc"+组合"nihao" → Cancel → "abc"（**快照真正恢复**）→ Undo no-op（**快照已弹**） | 同时验证恢复+弹栈+不留历史 |
| F45 | 连续 Redo（GPT 新增） | "abc"+d+e → Undo×2 → "abc" → Redo×2 → "abcde" | Undo↔Redo 双向流转 |

**测试可测性**：`Undo/Redo` public 直接调；断言用 `GetText/GetCaret/GetSelection/GetScrollOffsetY`（均 public 只读）——无窗口环境全可测（Composition 路径走 TestableTextBox 的 using 暴露，同 8.5.1 F3-F15 先例）。

## 9. 文件改动清单（8.5.3——原子授权）

| # | 文件 | 改动内容 |
|---|---|---|
| 1 | `ECDI/include/ECDI/Widget/TextBox.h` | public 加 `Undo/Redo`；private 加 `UndoSnapshot` struct + `PushUndoSnapshot/CaptureCurrentState/RestoreSnapshot` 声明 + `m_undoStack/m_redoStack/kMaxUndoDepth/m_compositionPushedUndo` |
| 2 | `ECDI/src/Widget/TextBox.cpp` | 全部实现；编辑操作挂 Push（InsertCodepoint/InsertText/DeleteBackward/DeleteForward 重构/Cut）；UpdateComposition 首次 Push + CancelComposition 弹回；OnKeyDown Ctrl 加 Z/Y case |
| 3 | `ECDI/src/Tests/TextBoxTests.cpp` | F35-F43 |

**main.cpp 不动**（skill 2）；**双工程零改动**（3 个均现有文件，无新头文件）；**无新文件**。

## 10. 与既有约束的对齐

| 约束 | 对齐方式 |
|---|---|
| skill 15 分层 | Undo 纯 TextBox 内部状态（文本/光标/选区/滚动）——零平台依赖 |
| skill 16 Event 原则 | Ctrl+Z/Y = KeyDown 语义（C1），非独立 UndoEvent |
| skill 21 YAGNI | 快照模式（非命令模式——GPT 认可 MVP）；Selection 方向不存（第二次用例再扩展）；不做重做深度单独限制 |
| 资源类禁复制禁移动 | UndoSnapshot 值语义（string/size_t/optional/float）可拷贝入栈 |
| 7.2 测试体系 | F35-F43 走 7.2 TestCase 体系（无窗口可测） |
| 五阶段法 | 本文档 = 详细设计；确认后进实现 |

## 11. 修订记录

- v1.1（2026-08-25）GPT 评审整合：🔴 **CancelComposition 真正 RestoreSnapshot**（模型 B 组合串已在 m_text——仅 pop 会残留拼音占位）+ Cancel vs Undo 语义表（恢复✅/移除✅/进 Redo❌）；🟡 **C4c 一次编辑=一次 Push 契约**（InsertCodepoint/InsertText 独立实现已核）；🟡 **C3b 组合态 Undo/Redo 防御**（`if (m_isComposing) return`）；🟢 TestCase 补 F44（Composition Cancel 恢复+弹栈+不留历史）/F45（连续 Redo 双向流转）。
- v1.0（2026-08-25）8.5.3 定稿：B6 快照结构落地 + C4 挂载点清单（含 DeleteBackward/Forward 空操作检查重构）+ Undo/Redo 栈流转 + C3 Composition 衔接（首次 Push/Commit 不 Push/Cancel 弹回）+ Ctrl+Z/Y + D4/D7/D9 契约 + F35-F43。
