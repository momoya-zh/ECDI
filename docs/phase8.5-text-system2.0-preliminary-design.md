# Phase 8.5 文本系统 2.0 初步设计

> 状态：v1.2（2026-08-24）｜初步设计待审（GPT 两轮评审整合）
> 前序：Phase 8.5 职责确认 v1.1（GPT 评审整合）/ Phase 7.2 测试体系补强 ✅
> 相关：phase8.5-text-system2.0-requirements.md（职责确认 v1.1）/ phase5.5-textbox-detailed-design.md（5.5）/ phase5.6-ime-detailed-design.md（5.6）

## 1. 设计目标

在 Phase 8.5 职责确认 v1.1 基础上，解决 GPT 评审提出的 8 个边界问题，为详细设计铺路。

## 2. 边界问题解答（GPT 8 点）

### B1 IME Composition State 归属

**问题**：IME Composition State 到底属于 TextBox 还是独立文本编辑状态？

**解答**：**属于 TextBox，但分两层**

- **TextBox 层**：持有 Composition 状态（组合区间——见下方数据模型）
- **PlatformWindow 层**：只负责候选窗口定位（已有 `UpdateTextInputCaret`），不持有 Composition State

**数据模型**（GPT 评审修正——`m_compositionStart` 单点不足以描述组合区间）：

```cpp
// TextBox 新增成员：
std::string m_compositionText;    ///< 当前拼音半成品（UTF-8）
size_t m_compositionStart = 0;    ///< 组合起始码点索引（相对 m_text）
size_t m_compositionLength = 0;   ///< 组合覆盖的码点长度（临时文本区间）
size_t m_compositionCaret = 0;    ///< 组合内光标位置（相对组合串起点）
bool m_isComposing = false;       ///< 是否在组合中
```

**模型 B 锁定（GPT 第二轮评审——Composition 覆盖正式文本的临时区间）**：

```
模型 A（否决）：Composition 独立于 m_text，"额外绘制字符串"
    m_text = "你好"，composition = "nihao" → 显示 "你好nihao"
    Commit 后 m_text = "你好nihao"

模型 B（采纳）：Composition 覆盖 m_text 中的临时区间
    m_text = "你好nihao"，compositionStart = 2，compositionLength = 5
    视觉 "你好[nihao]"；Commit 后 m_text = "你好你好"
    compositionLength 描述 = m_text 中当前被组合串占用的码点区间长度
```

**理由**：
1. Composition 本质是**临时文本区间**（start + length + caret），不只是起点——8.5.2 的 Backspace/候选切换/取消组合/组合内移动都需要完整区间
2. **模型 B 与 m_compositionStart + m_compositionLength 设计一致**（GPT 论证：否则 compositionLength 描述什么会含糊）；Commit 语义 = 用 m_compositionText 替换 m_text[compositionStart, compositionStart+compositionLength) 区间
3. 不抽象成独立 CompositionState 类（GPT 确认：YAGNI——当前只有一个可编辑控件 TextBox，先够用即可）
4. Phase 5.6 已建立模式：TextBox 持有客户区坐标，PlatformWindow 负责平台转换

**演进路径**：第二个可编辑控件出现时 → 抽象 EditableTextWidget，共享 Composition State 逻辑

### B2 剪贴板 PlatformWindow 接口

**问题**：剪贴板 PlatformWindow 接口具体长什么样？

**解答**：**扩展 PlatformWindow，新增两个虚函数**（剪贴板是 Platform capability，不是 Event 类型——GPT 评审修正，见 §7 C1）

```cpp
// PlatformWindow.h 新增：
/// @brief 从系统剪贴板读取文本（UTF-8）
/// @return 空字符串 = 剪贴板无文本数据
virtual std::string GetClipboardText() const = 0;

/// @brief 写入文本到系统剪贴板（UTF-8）
virtual void SetClipboardText(const std::string& text) = 0;
```

**调用链**（不经 ClipboardCopy/PasteEvent——Ctrl+C/V 是 KeyDown 的语义动作）：

```cpp
// TextBox::OnKeyDown：
case Ctrl+C → GetSelection() → GetWindow()->GetPlatformWindow()->SetClipboardText(...)
case Ctrl+V → GetWindow()->GetPlatformWindow()->GetClipboardText() → InsertText(...)
case Ctrl+X → 复制 + 删除选中区
case Ctrl+A → SelectAll
```

**理由**：
1. 与 `UpdateTextInputCaret` 同模式（平台能力下沉到 PlatformWindow）
2. Ctrl+C/V/X 是键盘快捷键产生的语义动作，走 KeyDownEvent 即可——不需要为每个快捷键造专用 Event（否则 Ctrl+A→SelectAllEvent、Ctrl+Z→UndoEvent……会把 TextBox 内部编辑命令泄漏进全局 Event 系统）
3. Win32 实现：`GetClipboardText` = `CF_UNICODETEXT` + `GlobalLock` + `WideToUTF8`；`SetClipboardText` = `UTF8ToWide` + `CF_UNICODETEXT` + `SetClipboardData`

**分层**：TextBox → Window → PlatformWindow → Win32 API（零平台类型泄漏）

### B3 Timer 层级

**问题**：Timer 到底在哪一层产生？

**解答**：**PlatformWindow 层产生，Window 层翻译，TextBox 层消费**（Timer 接口语义通用化——GPT 评审修正，见 §7 C2）

```
TextBox::OnFocusGained()
    ↓
StartTimer(kCaretBlinkTimer, 500)     // TextBox 拥有 TimerId 常量
    ↓
PlatformWindow (Win32PlatformWindow)
    ↓
WM_TIMER（500ms 周期）
    ↓
WindowMessageHandler 翻译为 TimerEvent
    ↓
Window::DispatchTimerEvent()
    ↓
焦点 TextBox::OnTimer()
    ↓
ToggleCaretVisibility() + Invalidate()
```

**接口**（通用 Timer，平台不知道"Caret Blink"）：

```cpp
// PlatformWindow.h 新增：
/// @brief 启动周期定时器（平台能力；ID 语义由调用方定义）
/// @param timerId 定时器标识（调用方自定义）
/// @param intervalMs 触发间隔（毫秒）
virtual void StartTimer(int timerId, unsigned int intervalMs) = 0;

/// @brief 停止定时器（幂等）
virtual void StopTimer(int timerId) = 0;

// TextBox 内部：
static constexpr int kCaretBlinkTimer = 1;   ///< 光标闪烁定时器 ID
static constexpr unsigned int kCaretBlinkMs = 500;
```

**理由**：
1. 保持 Phase 7.1 平台边界（Timer 是平台能力，与 IME 同性质）
2. `StartCaretBlink/StopCaretBlink` 泄漏 TextBox 语义——平台只应知道"有个 Timer ID 每 500ms 触发"，不该知道"这是光标闪烁"
3. TextBox 不碰 WM_TIMER（GPT 评审修正）
4. YAGNI：不引入通用 Timer/Scheduler 管理器，只做 id + interval 的最简接口

**实现细节**：
- `Win32PlatformWindow::StartTimer`：`SetTimer(m_handle, timerId, intervalMs, nullptr)`
- `Win32PlatformWindow::StopTimer`：`KillTimer(m_handle, timerId)`
- `WindowMessageHandler`：`case WM_TIMER: return TimerEvent{ timerId };`（TimerEvent 带 id 字段）
- `Window::DispatchTimerEvent()`：转发给焦点控件 `m_focusedWidget->OnTimer(event)`

### B4 多行文本换行模型

**问题**：多行文本的换行模型？

**解答**：**显式换行符（\n） + 自动换行（可选）**

- **显式换行**：TextBox 支持 `\n` 输入与显示（回车键插入 `\n`）
- **自动换行**：Phase 8.5 **不做**（复杂度高：需要行宽计算、断词、滚动联动）
- **行高计算**：`TextMeasurer::LineHeight(m_font)` × 行数（简单乘法）

**数据结构**：
```cpp
// TextBox 新增成员：
std::vector<size_t> m_lineStarts;  // 每行起始码点索引（缓存，避免每次重算）
bool m_needsLineRecalc = true;     // 编辑后标记需要重算行信息
```

**缓存失效责任（GPT 评审补充——谁负责置 m_needsLineRecalc）**：

```
置 m_needsLineRecalc = true（文本内容变化）：
  Insert / DeleteBackward / DeleteForward / Paste / Cut /
  Undo / Redo / IME Commit / Enter（插入 \n）/ SetText

不置（纯视觉/光标变化，不重新扫描文本）：
  Caret 移动 / Selection 变化 / Scroll / Caret Blink
```

否则多行 TextBox 会变成"每次光标移动 → O(n) 扫描全文"，与引入 m_lineStarts 的目的相反。

**理由**：
1. 显式换行是基础能力，自动换行是复杂功能（YAGNI）
2. 行信息缓存避免每次绘制/点击重算（O(n) 扫描换行符）
3. Phase 9.5 收尾时可评估自动换行（LinearLayout 抽象后）

### B5 滚动状态归属

**问题**：滚动状态属于 TextBox 还是 ScrollArea？

**解答**：**TextBox 内建垂直滚动**（Phase 8.5 职责确认 D3 已决定）

**滚动状态**：
```cpp
// TextBox 新增成员：
float m_scrollOffsetY = 0.0f;      // 垂直滚动偏移（像素）
```

**滚动逻辑**：
1. **光标跟随**：编辑/移动光标时，自动滚动使光标可见
2. **鼠标滚轮**：`OnMouseWheel` 响应 `WheelDelta`，调整 `m_scrollOffsetY`
3. **可视区域计算**：`GetVisibleLineRange()` = `[scrollOffsetY / lineH, (scrollOffsetY + height) / lineH)`

**理由**：
1. 文本编辑是紧密耦合操作（光标定位、选择高亮、滚动跟随），外部 ScrollArea 难以精确协调
2. 与 Phase 5.5 T8 超长文本裁切同性质（内部状态管理）
3. Phase 9.5 收尾时可评估外部 ScrollArea 集成（如果需要水平滚动或统一滚动行为）

### B6 Undo Snapshot 状态

**问题**：Undo Snapshot 包含哪些状态？

**解答**：**快照 = 文本 + 光标 + Selection + 滚动偏移**

```cpp
struct UndoSnapshot {
    std::string text;           // 完整文本（UTF-8）
    size_t caret;               // 光标码点索引
    SelectionRange selection;   // 选区（起始/结束码点索引）
    float scrollOffsetY;        // 滚动偏移
};
```

**理由**：
1. 快照模式 = 记录完整状态（MVP 先行，GPT 认可）
2. Selection 是编辑系统的横切面（Phase 5.5 T4 决策），必须记录
3. 滚动偏移影响视觉状态，撤销时应恢复

**存储**：
```cpp
// TextBox 新增成员：
std::vector<UndoSnapshot> m_undoStack;
std::vector<UndoSnapshot> m_redoStack;
size_t m_maxUndoDepth = 100;    // 防内存爆炸
```

**Push 时机契约（GPT 评审补充——详细设计必须锁死）**：

```
执行编辑操作前：Push 当前状态 → 执行修改 → Clear redo
（Snapshot 记录"修改前"状态；Ctrl+Z：当前状态 → RedoStack，UndoStack top → 当前状态）

产生 Undo Snapshot（文本内容改变）：
  输入字符 / 删除 / 粘贴 / 剪切 / IME Commit → YES

不产生 Undo Snapshot（纯状态变化）：
  移动光标 / Shift 选择 / 鼠标点击 / 滚动 / Caret Blink → NO
```

**IME Composition 与 Undo 的关系（GPT 评审补充——8.5 易踩坑点）**：

```
[开始输入拼音] → Composition 状态变化 → 不产生 Undo
[IME Commit "你好"] → 作为一次文本编辑进入 Undo 历史

禁止：Composition 编辑过程逐键进 Undo
（否则 Ctrl+Z 会变成 你好→nihao→niha→nih 的拼音回退，非正常 TextBox 体验）
```

**理由**：
1. 快照模式 = 记录完整状态（MVP 先行，GPT 认可）
2. Selection 是编辑系统的横切面（Phase 5.5 T4 决策），必须记录
3. 滚动偏移影响视觉状态，撤销时应恢复

### B7 双击选词 code point 边界

**问题**：双击选词的 code point 边界？

**解答**：**Unicode code point 边界（GPT 修正）**

**分词规则**：
1. **英文**：空格/标点作为分隔符（连续字母/数字为一个词）
2. **中文**：每个 code point 作为独立单元（无空格分隔）
3. **Emoji**：按 TextBox 当前 code point 索引模型处理（GPT 修正——👨‍👩‍👧‍👦 是多个 code point + ZWJ，👍🏽 也是多个 code point；Phase 8.5 **不引入 grapheme cluster 语义**，否则范围爆炸）

**算法**：
```cpp
// TextBox 新增 private 方法：
/// @brief 双击位置 → 选中词的码点范围 [start, end)
/// @param clickIndex 点击位置的码点索引
/// @return 选区范围（起始索引，结束索引）
std::pair<size_t, size_t> GetWordBounds(size_t clickIndex) const;
```

**理由**：
1. 与 TextBox 内部索引单位统一（code point）
2. 简单分词满足 90% 场景（YAGNI）
3. Phase 9.5 收尾时可评估复杂分词（如果需要）

### B8 SetFont 与 TextMeasurer 所有权

**问题**：SetFont 与 TextMeasurer 的所有权关系？

**解答**：**TextBox 持有 Font 描述/值，RenderingBackend/TextMeasurer 管理平台字体资源**（GPT 修正措辞——避免"引用"歧义）

**代码事实确认（2026-08-24，GPT 第二轮要求核实）**：

```cpp
// ECDI/Core/Font.h:14 —— 已确认是纯数据值语义：
struct Font
{
	float size = 14.0f;        ///< 字号（第一版：像素高度）
	std::string family;        ///< 字体族（UTF-8，空 = 系统默认）
};
// "零平台资源、无方法、可值拷贝进命令；实例化（HFONT）与测量在平台层"——注释明示
```

✅ `m_font = font` 值拷贝**成立**，`SetFont(const Font&)` 无需改设计。

**接口**：
```cpp
// TextBox 新增：
void SetFont(const Font& font);   // 更新 m_font（值拷贝）+ Invalidate
```

**流程**：
1. `TextBox::SetFont(font)` → `m_font = font`（值复制）→ `Invalidate()`
2. 绘制时：`ctx.MeasureText(m_font, m_text)` → TextMeasurer 用 m_font 测量
3. 字体资源生命周期：TextMeasurer（GDIBackend）持有 HFONT，TextBox 不管理

**理由**：
1. 与现有 `m_font` 成员一致（TextBox 持有字体描述值，TextMeasurer 持有字体资源）
2. 最小化接口（`SetFont` 只更新内部状态 + 触发重绘）
3. 字体资源由后端管理（GDIBackend::CreateFont），TextBox 不碰平台句柄

### B9 多行坐标 → Caret 映射（GPT 第二轮补充——详细设计必须锁死）

**问题**：进入多行后，CaretIndexFromX 单行模型如何升级？

**解答**：**CaretIndexFromX → CaretIndexFromPosition(Point)**，两阶段定位

```cpp
// TextBox private（替代原单行 CaretIndexFromX）：
/// @brief 客户区相对位置 → 最近码点索引（多行：Y 定行 → X 定行内码点 → 全局码点索引）
/// @param localPos 相对文本框左上角的客户区坐标（非窗口绝对）
size_t CaretIndexFromPosition(Point localPos) const;
```

**定位链**（GPT 论证——避免多行 + Selection + 双击 + Scroll 坐标体系混乱）：

```
鼠标坐标（相对文本框）
    ↓ 1. Y → 行号（m_scrollOffsetY 参与：localY + scrollOffsetY 对应的逻辑行）
    ↓ 2. 行号 → 该行起始码点索引（m_lineStarts 查表）
    ↓ 3. X → 行内码点索引（行内前缀测量，复用单行 CaretIndexFromX 逻辑）
    ↓ 4. 行内索引 + 行起始索引 = 全局 caret 索引
```

**理由**：
1. 单行 CaretIndexFromX 是"X → 码点"直接映射，多行必须插入行号维度
2. m_lineStarts 缓存在步骤 2 提供 O(1) 行起始索引（缓存失效契约见 B4）
3. Scroll 偏移参与第 1 步（可视行 ↔ 逻辑行换算），保证点击定位与绘制同源

### B10 索引单位契约（GPT 第二轮补充——全局锁死）

**契约**：**所有 TextBox 内部索引 API 的单位均为 Unicode code point，不是 UTF-8 byte offset**

```cpp
// 索引单位速记：
// UTF-8 byte index     ≠  Unicode code point index   ≠  grapheme cluster index
//     m_text[i]              m_caret / SelectionRange      （8.5 不处理）

// ⚠️ 禁止：std::string::substr(m_caret)  —— m_caret 是码点索引，不是字节偏移！
// 必须经 CodepointIndexToByteOffset(m_text, m_caret) 转换后再 substr
```

**覆盖范围**：m_caret / m_selectionAnchor / SelectionRange.start|end / m_compositionStart / m_compositionLength / m_compositionCaret / m_lineStarts / GetWordBounds 返回值——**全部码点索引**。

**理由**：
1. 防止 m_caret 被当字节偏移用（substr 直接炸）——Phase 5.5 已踩过 UTF-8 变长坑
2. 与 Core/UTF8.h 转换工具（CodepointIndexToByteOffset/ByteOffsetToCodepointIndex）配套
3. grapheme cluster 明确排除（C5 契约，8.5 不做）

## 3. 模块划分

### 3.1 TextBox 内部模块

```
TextBox
├── 文本模型（m_text + 光标 + Selection）
├── IME 状态（m_compositionText + m_compositionStart + m_compositionLength + m_compositionCaret + m_isComposing）
├── 多行状态（m_lineStarts + m_needsLineRecalc）
├── 滚动状态（m_scrollOffsetY）
├── Undo 状态（m_undoStack + m_redoStack + m_maxUndoDepth）
├── 定时器（kCaretBlinkTimer 常量 + OnTimer 处理）
├── 绘制逻辑（OnPaint）
├── 事件处理（OnKeyDown/OnCharInput/OnMouseButtonDown/OnMouseMove/OnMouseWheel/OnTimer）
└── 辅助方法（CaretIndexFromX/GetWordBounds/GetVisibleLineRange）
```

### 3.2 PlatformWindow 扩展

```
PlatformWindow
├── 现有：Show/Release/Invalidate/GetClientSize/GetRenderContext
├── 现有：UpdateTextInputCaret/DestroyTextInputCaret
├── 新增：GetClipboardText/SetClipboardText（剪贴板 capability）
├── 新增：StartTimer/StopTimer（通用定时器——ID 语义由调用方定义，平台不知道 Caret）
└── Win32 实现：Win32PlatformWindow
```

### 3.3 事件扩展

```
Event
├── 现有：KeyDownEvent/KeyUpEvent/CharInputEvent/MouseButtonDownEvent/...
├── 新增：TimerEvent（带 timerId 字段；光标闪烁消费）
├── 新增：MouseWheelEvent（滚动）
└── 不新增：ClipboardCopyEvent/ClipboardPasteEvent（GPT 修正 C1——
    Ctrl+A/C/V/X 走 KeyDownEvent 语义处理，剪贴板是 Platform capability 不是 Event）
```

## 4. 实施顺序（预估）

```
Phase 8.5.1 核心升级（B1-B3 + B8）
  ① B1 IME Composition State（TextBox 组合区间状态 + 绘制）
  ② B2 剪贴板 PlatformWindow 接口（GetClipboardText/SetClipboardText + OnKeyDown Ctrl 组合）
  ③ B3 Timer 层级（PlatformWindow::StartTimer/StopTimer + TimerEvent + OnTimer）
  ④ B8 SetFont（TextBox::SetFont + Font 值语义确认）
  → 验证：拼音输入显示、复制粘贴、光标闪烁、字体设置

Phase 8.5.2 多行与滚动（B4-B5 + B7）
  ⑤ B4 多行文本（\n 换行 + 行高计算 + m_lineStarts 缓存 + 失效责任）
  ⑥ B5 滚动状态（m_scrollOffsetY + OnMouseWheel + 光标跟随）
  ⑦ B7 双击选词（GetWordBounds + 鼠标双击事件 + code point 边界）
  → 验证：多行输入、滚动、双击选词

Phase 8.5.3 高级功能（B6）
  ⑧ B6 Undo/Redo（UndoSnapshot + Push 时机契约 + Ctrl+Z/Y + IME Commit 入历史）
  → 验证：撤销重做
```

**实施方式（GPT 第十点——7.2 完成后改变测试节奏）**：

```
设计一个子系统 → 实现 → 立即写对应 TestCase → VS 编译运行 → 进入下一项
（不再采用"先做功能，最后补测试"；TextBox 状态多，逐项测试收益大）
```

## 5. 与既有约束的对齐

| 约束 | 对齐方式 |
|---|---|
| skill 15 分层 | IME/剪贴板/Timer 平台依赖经 PlatformWindow 抽象，TextBox 零平台类型 |
| skill 16 Event 原则 | TimerEvent（已发生事实，带 timerId）/ MouseWheelEvent 是 Event；剪贴板是 Platform capability（Ctrl 组合走 KeyDownEvent，不造专用 Event） |
| skill 21 YAGNI | 简单分词、快照 Undo、内建滚动、通用 StartTimer/StopTimer 不做管理器——不做投机性抽象 |
| skill 22 分层论证 | 接口设计用契约语言，不引用 Win32 API 细节 |
| 资源类禁复制禁移动 | TextBox 值语义（m_text std::string）；UndoSnapshot 值语义；Font 需确认值语义（可复制描述） |
| 测试由用户做 | 新功能测试由用户编译运行验证；8.5 采用逐项 TestCase 节奏 |
| 五阶段法 | 本文档 = 初步设计；确认后进详细设计 |

## 6. GPT 评审整合（v1.1）

> GPT 评审时间：2026-08-24 20:30
> 评审结论：**可以进入详细设计（85~90% 成熟）**，但需先锁死 C1-C6 六个契约。

### 修正项（3 处重要）

1. **C1 剪贴板不做 Event**：删除 ClipboardCopyEvent/ClipboardPasteEvent——Ctrl+A/C/V/X 走 KeyDownEvent 语义处理 + PlatformWindow Clipboard capability；避免"每个快捷键造一个 Event"（Ctrl+A→SelectAllEvent、Ctrl+Z→UndoEvent……把 TextBox 内部命令泄漏进全局 Event 系统）
2. **C2 Timer 接口通用化**：`StartCaretBlink/StopCaretBlink` → `StartTimer(id, intervalMs)/StopTimer(id)`——PlatformWindow 不应知道"这是光标闪烁"，只应知道"有个 Timer ID 每 500ms 触发"；TextBox 拥有 `kCaretBlinkTimer` 常量
3. **B1 IME Composition 数据模型扩展**：`m_compositionStart` 单点不足 → `m_compositionStart + m_compositionLength + m_compositionCaret + m_isComposing`（组合是临时文本区间）；不抽象独立类（YAGNI）

### 补充契约（详细设计必须锁死）

- **C3 Composition 与 Undo**：Composition update 不进入 Undo；**Composition Commit 才作为一次文本编辑进入 Undo 历史**（否则 Ctrl+Z 会拼音回退）
- **C4 Undo 边界**：文本内容改变 → 产生 Undo；纯光标/Selection/Scroll 变化 → 不产生；Snapshot 记录**修改前**状态
- **C5 Unicode 范围**：TextBox 编辑索引 = Unicode code point；**Phase 8.5 不处理 grapheme cluster**（👨‍👩‍👧‍👦/👍🏽 多 code point，按现有索引模型处理）
- **C6 Font 所有权**：TextBox → Font description/value（可复制）；Backend/TextMeasurer → platform font resource（HFONT），TextBox 不碰平台句柄

### 实施方式变更（7.2 完成后的红利）

- 8.5 **不再采用"先做功能最后补测试"**——改为：设计子系统 → 实现 → 立即写对应 TestCase → 编译运行 → 下一项
- 8.5 是第一个充分利用新测试体系的**大型功能 Phase**

### 第二轮评审整合（GPT 2026-08-24 20:44）

> 评审结论：**初步设计通过，建议进入详细设计。成熟度约 90%。** 3 个⚠️项已全部处理：

1. **Composition 模型 B 锁定**（⚠️→✅）：Composition 覆盖 m_text 中的临时区间（模型 B），非"额外绘制字符串"（模型 A 否决）——与 m_compositionStart/Length 设计一致，Commit = 区间替换（见 B1）
2. **Font 代码事实确认**（⚠️→✅）：`Core/Font.h` 已是纯数据值语义 struct（零平台资源、可值拷贝），`SetFont(const Font&)` + `m_font = font` 成立（见 B8）
3. **多行坐标→Caret 锁死**（⚠️→✅）：`CaretIndexFromX` → `CaretIndexFromPosition(Point)`——Y 定行（含 scrollOffset）→ 行起始索引 → X 定行内码点 → 全局索引（见 B9）
4. **索引单位契约**（新增）：所有内部索引 API 单位 = Unicode code point，禁 `substr(m_caret)`（见 B10）

## 7. 修订记录

- v1.2（2026-08-24）GPT 第二轮评审整合（通过，成熟度 90%）：B1 锁定**模型 B**（Composition 覆盖 m_text 临时区间，Commit = 区间替换）；B8 **代码事实确认**（Core/Font.h 已是纯数据值语义 struct，SetFont 值拷贝成立）；新增 **B9 多行坐标→Caret 映射**（CaretIndexFromX → CaretIndexFromPosition(Point)，Y 定行 → X 定行内 → 全局索引）；新增 **B10 索引单位契约**（所有内部索引 API 单位 = code point，禁 substr(m_caret)）。
- v1.1（2026-08-24）GPT 评审整合：C1 剪贴板不做 Event；C2 Timer 接口通用化（StartTimer/StopTimer）；B1 Composition 数据模型扩展（start+length+caret）；B4 缓存失效责任明确；B6 Undo Push 时机契约 + IME Commit 入历史；B7 Emoji 措辞（不引入 grapheme cluster）；B8 Font 值语义确认；实施方式改逐项 TestCase。
- v1.0（2026-08-24）初步设计初稿：B1-B8 边界问题解答 + 模块划分 + 实施顺序。

