# Phase 5.5 TextBox 初步设计

> 状态：v1.0（2026-08-13）｜初步设计完成，待详细设计
> 相关：phase5-textbox-requirements.md（职责确认 v1.0）/ phase5-text-*.md（5.1 文本系统）

## 1. 定稿决策（P1-P7 + GPT 修正）

### P1 Window 测量服务 —— A ✅

```cpp
// Window.h
/// @brief 获取文本测量器（5.5 T1：GDIBackend 兼 TextMeasurer；控件经 protected GetWindow() 获取）
/// @details 返回抽象接口不暴露具体后端——换 GDIBackend/D2D/Skia 只要实现 TextMeasurer，TextBox 零改动
TextMeasurer& GetTextMeasurer() noexcept;

// Window.cpp
TextMeasurer& Window::GetTextMeasurer() noexcept { return m_backend; }
```

- 返回 `TextMeasurer&`（抽象接口），不返回 GDIBackend&（不暴露实现）
- 控件调用：`GetWindow()->GetTextMeasurer().MeasureText(...)`——TextBox 内部直接用，不加 Widget 级便捷方法（唯一消费者，YAGNI）

### P2 + P6 UTF-8 工具 —— Core/UTF8.h 上提（GPT P6 修正，推翻 P2 的"TextBox 私有"）✅

**新文件** `Core/UTF8.h` + `Core/UTF8.cpp`（全部静态，namespace ECDI）：

```cpp
/// @brief 码点 → UTF-8 编码（单码点，返回 1-4 字节字符串）
std::string EncodeUTF8(char32_t codepoint);

/// @brief UTF-8 → 码点（解码第一个码点；非法序列返回 0xFFFD 替换符——不负责完整 error recovery）
char32_t DecodeUTF8(const std::string& text, size_t byteOffset);

/// @brief 码点索引 → 字节偏移（UTF-8 变长：中文 3 字节 / emoji 4 字节；越界返回 text.size()）
size_t CodepointIndexToByteOffset(const std::string& text, size_t codepointIndex);

/// @brief 字节偏移 → 码点索引（与上一函数互逆；越界返回码点数）
size_t ByteOffsetToCodepointIndex(const std::string& text, size_t byteOffset);
```

- **上提理由（GPT）**：第二消费者必然出现（IME 上屏 5.6 / 剪贴板 Ctrl+V / 多行 / 富文本 / 剪贴板）——是基础能力不是猜测性抽象（区别于 AutoSize）
- **命名（GPT P2 修正）**：`CodepointIndexToByteOffset` / `ByteOffsetToCodepointIndex`——"Codepoint"（字符值）与"第几个"（索引）语义区分
- 名字与标准库不冲突（`UTF8.h` 无同名标准头，规避 C3861 教训）

### P3 TextBox 骨架 + 编辑操作 —— A（public 临时，Phase 7 审查）✅

```cpp
class TextBox : public TextWidget {
public:
	TextBox() = default;
	explicit TextBox(const std::string& text);

	bool CanFocus() const noexcept override { return true; }

	// ── 编辑操作（⚠️ 临时 public：5.5.1 阶段为可测试；Phase 7 API 审查重新定可见性，
	//   届时可能是"公开高层 API（InsertText/Clear/SetCaret）+ protected 底层原语"两层结构）──
	void InsertCodepoint(char32_t codepoint);   // 光标处插入（5.5.2：有 Selection 先删选中区）
	void DeleteBackward();                      // Backspace：删光标前一码点
	void DeleteForward();                       // Delete：删光标后一码点
	void MoveCaret(int direction);              // ←→（+1/-1，边界钳制）
	void MoveCaretToStart();                    // Home
	void MoveCaretToEnd();                      // End
	size_t GetCaret() const noexcept;           // 码点索引

protected:
	void OnFocusGained() override;              // m_caretVisible = true + Invalidate
	void OnFocusLost() override;                // m_caretVisible = false + Invalidate
	void OnMouseButtonDown(const MouseButtonDownEvent&) override;   // 点击定位光标（P1 服务第一个消费者）
	void OnKeyDown(const KeyDownEvent&) override;    // 编辑键映射（P4）
	void OnCharInput(const CharInputEvent&) override; // 字符插入（P4）
	void OnPaint(PaintContext& ctx, int x, int y) override;

private:
	size_t m_caret = 0;          ///< 光标位置（码点索引）
	bool m_caretVisible = false; ///< 光标可见（焦点时 true）
};
```

- **GPT 最终结论**：当前阶段（框架内部开发、非库 API 冻结）支持 public——逻辑可测试优先；**文档明确标注临时性**，Phase 7 API 审查时重定（GPT 方案 C 两层结构为最终形态参考：public SetText/InsertText/Clear/SetCaret + protected 原语）
- 编辑操作与事件解耦：事件 → 操作映射薄薄一层，编辑逻辑集中可测（main.cpp 断言不依赖 Window/事件对象）

### P4 输入分流 —— A ✅（GPT 100% 同意）

- **代码事实**：Win32 里 Backspace 既发 WM_KEYDOWN(VK_BACK) 又发 WM_CHAR(0x08)；Delete 发 WM_CHAR(0x7F)——OnCharInput 不过滤会把控制字符当字符插入
- **OnKeyDown** 处理编辑键（Backspace/Delete/←→/Home/End 全走 KeyCode——KeyCode 枚举已确认齐全）
- **OnCharInput** 过滤控制字符：`if (codepoint < 0x20 || codepoint == 0x7F) return;` 其余插入
- 过滤控制字符 = Event 原则"原始值不归一化、语义判断推迟给消费者"的落地（TextBox 就是消费者）

### P5 光标绘制 —— GPT 修正采纳（与文本起点同源）✅

- **问题（GPT 指出）**：DrawTextContent 已测过一次（LineHeight），DrawCaret 再 MeasureText(prefix) 重复测量；且光标位置若直接从 x 算，与 CalculateTextPosition 两套算法——未来 Button 水平居中/多行必然偏差
- **统一流程**：

```cpp
// OnPaint：
ctx.DrawRect(Rect{全块}, Color::White());                    // 白底
if (HasFocus()) { /* 焦点框：DrawRect(全块, 框色) → DrawRect(内缩2px, 白) */ }

// 文本：复用 TextWidget 统一入口（内部走 CalculateTextPosition 得文本起点）
DrawTextContent(ctx, x, y);

// 光标：与文本起点同源——不直接用 x
if (m_caretVisible){
	const Size textSize = ctx.MeasureText(m_font, m_text);           // 完整文本宽（顺带：DrawTextContent 用同款测量，见性能注记）
	const Point textPos = CalculateTextPosition(x, y, textSize.width, textSize.height);  // 同源！
	const size_t byteOffset = CodepointIndexToByteOffset(m_text, m_caret);
	const Size prefixSize = ctx.MeasureText(m_font, m_text.substr(0, byteOffset));
	ctx.DrawRect(Rect{ textPos.x + prefixSize.width, textPos.y, 2.0f, textSize.height }, Color::Black());  // 竖线 2px
}
```

- 决策点 a：光标竖线 **黑色**（白底上明显）✅；b：焦点框 **深蓝 FromRGBA8(80,120,220)**（与 Button 蓝底呼应）✅
- **性能注记**（与 5.3 D5 同款模式）：OnPaint 两次 MeasureText（完整文本 + 前缀）——每帧测量开销可接受，未来多测量场景（Selection/IME）若成问题，优化封闭在 TextBox 内部（缓存或合并测量），接口零变化

### P7 main.cpp 验证 —— A ✅（保留 emoji/中文断言，GPT 强烈支持）

- win1 面板加 `TextBox("")`（预填 "Hello" 便于看光标位置）
- 断言段（直接调 public 编辑操作，不依赖窗口）：

```cpp
// ── 5.5 TextBox 编辑逻辑：Insert/Delete/Move（不依赖窗口；emoji/中文验证码点索引不切字）──
{
	ECDI::TextBox box("abc");
	box.MoveCaretToEnd();
	box.InsertCodepoint(U'😀');            // emoji 4 字节
	FRAMEWORK_ASSERT(box.GetText() == "abc😀");
	FRAMEWORK_ASSERT(box.GetCaret() == 4); // 4 个码点
	box.DeleteBackward();                  // 删 😀（删前一码点的字节区间，不切残留）
	FRAMEWORK_ASSERT(box.GetText() == "abc");
	box.MoveCaret(-1);
	box.InsertCodepoint(U'中');            // 中文 3 字节
	FRAMEWORK_ASSERT(box.GetText() == "ab中c");
	FRAMEWORK_ASSERT(box.GetCaret() == 3);
}
```

- **main.cpp 的 AppendUTF8 迁移**（P6 连带）：删除本地 AppendUTF8 实现，CharInput 演示改用 `ECDI::EncodeUTF8`（main 是测试入口，逻辑简化）

## 2. 实施顺序（GPT 拆分——4 个小 commit）

```
5.5.1.1  Window::GetTextMeasurer() + Core/UTF8.h/.cpp（4 函数）+ main.cpp AppendUTF8 迁移
5.5.1.2  TextBox 骨架（类定义 + 事件 override 空实现 + CanFocus + 焦点可见性）
5.5.1.3  编辑逻辑（Insert/DeleteBackward/DeleteForward/MoveCaret/Home/End + 断言段）
5.5.1.4  Paint（白底/焦点框/文本/光标同源流程）+ 鼠标点击定位 + main.cpp 控件接入
```

每个 commit 独立可编译可测——**第一次真正处理 Unicode（GPT 定性：5.5 是 Phase 5 分水岭），拆小调试轻松**。

## 3. 边界确认（本阶段）

- 不做：Selection（5.5.2）、KeyEvent 修饰键（5.5.2）、IME（5.6）、多行/滚动、Undo/Redo、剪贴板、光标闪烁
- 超长文本：裁切（T8，滚动归 Phase 6）

## 4. 修订记录

- v1.0（2026-08-13）初步设计：P1-P7 定稿。P2 工具上提 Core/UTF8.h（GPT P6 修正）；P3 编辑操作 public（GPT 最终支持，标注 Phase 7 审查临时性）；P5 光标同源流程（GPT 修正）；P6 Core/UTF8.h 四函数；拆分 4 commit（GPT 建议）。
