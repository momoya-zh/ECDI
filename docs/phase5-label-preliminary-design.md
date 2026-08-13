# Phase 5.2 Label 初步设计 v1.0

> 日期：2026-08-13 ｜ 状态：已确认 ｜ 方式：清单式问答（P1-P4）+ GPT 评审

## 决策记录

### P1 Label 类最终形态（L1 迁移 + L4 预留落地）

```cpp
class Label : public Widget {
public:
	Label() = default;
	explicit Label(const std::string& text);        // L1：窄字面量（UTF-8）
	explicit Label(std::string&& text);

	void SetText(const std::string& text);
	void SetText(std::string&& text);
	const std::string& GetText() const noexcept;

	void SetTextColor(const Color& color);          // L4：const& 与 SetText 风格统一
	const Color& GetTextColor() const noexcept;

protected:
	void OnPaint(PaintContext& ctx, int x, int y) override;   // L3：只画文本，透明背景

private:
	std::string m_text;
	Color m_textColor = Color::Black();             // L4：默认黑
	Font m_font{};                                  // L4 预留：未来 SetFont 一行接入，OnPaint 零改动
};
```

- `SetTextColor(const Color&)`：Color 是小型值类型，const&/值传均可；统一 `const&`（与 SetText 一致）
- `m_font{}` 预留：**让绘制代码保持稳定**（`ctx.DrawText(pos, m_text, m_textColor, m_font)` 永远成立），未来 `SetFont` 只赋成员，不改 OnPaint

### P2 OnPaint 绘制逻辑

```cpp
void Label::OnPaint(PaintContext& ctx, int x, int y)
{
	if (m_text.empty())
		return;                          // a：空文本零命令零绘制

	const float lineHeight = ctx.LineHeight(m_font);
	const float offsetY = (static_cast<float>(GetHeight()) - lineHeight) / 2.0f;   // 垂直居中

	ctx.DrawText(
		Point{ static_cast<float>(x), static_cast<float>(y) + offsetY },
		m_text, m_textColor, m_font
	);
}
```

- **a. 空文本跳过**：不发空命令（空串 TextOutW 无害但无意义）
- **b. 垂直居中用 `LineHeight()` 单次调用**：左对齐不需要宽度，省一次 MeasureText
- **⚠️ 负 offsetY 合法（GPT 补充）**：当 `GetHeight() < lineHeight`（控件比文本小）时 offsetY 为负、文本向上偏移——**这是合法的，不要 `std::max(0.0f, offsetY)` 修正**：控件比文本小是布局问题，绘制系统不偷偷修正

### P3 main.cpp 改造（L7）

- 连带改动：win1/win2 的 `Label(L"...")` 构造改**窄字面量**（L1 迁移）
- 新增 5.2 断言段——**期望值动态计算（GPT 修正，不硬编码 13.0f）**：

```cpp
// ── 5.2 Label 文本链路：Label → PaintContext → DrawTextCommand（命令断言）──
{
	ECDI::RecordingBackend backend;
	ECDI::CommandBuffer commands;
	ECDI::PaintContext ctx(commands, backend);

	ECDI::Label label("Hello ECDI");
	label.SetPosition(5, 5);
	label.SetSize(100, 30);
	label.Paint(ctx, 0, 0);

	FRAMEWORK_ASSERT(commands.size() == 1);        // OnPaint 确实被调用（经 Widget::Paint 分发）
	const auto& cmd = std::get<ECDI::DrawTextCommand>(commands[0]);
	FRAMEWORK_ASSERT(cmd.text == "Hello ECDI");
	FRAMEWORK_ASSERT(cmd.color.r == 0.0f);          // Color::Black()
	FRAMEWORK_ASSERT(cmd.pos.x == 5.0f);

	const float expectedY = 5.0f + (30.0f - backend.LineHeight(ECDI::Font{})) / 2.0f;   // 动态算期望
	FRAMEWORK_ASSERT(cmd.pos.y == expectedY);       // 验证"用了垂直居中公式"，而非"恰好等于 13"
	FRAMEWORK_ASSERT(cmd.font.size == 14.0f);       // 默认 Font()
}
```

**测试哲学（GPT 修正）**：断言验证"**使用了垂直居中公式**"，不是"结果恰好是 13"——`LineHeight()` 若未来返回 15/13，测试仍成立（不耦合 RecordingBackend 的模拟值）。

### P4 边界确认

- 无新文件（Label.h/cpp 已有）→ vcxproj/CMake **零改动**
- 不提取 TextWidget（L8，5.3 Button 出现第二个文本控件才做）
- 不做 AutoSize / GetPreferredSize（L2 架构债务已记入 requirements v1.0）

## 待详细设计固化

1. Label.h/cpp 的具体编辑点（精确 diff 范围：构造/SetText/GetText 改 string + 新增 SetTextColor + OnPaint override）
2. main.cpp 的精确插入位置（5.2 断言段放哪、win1/win2 Label 构造改哪几行）
3. 垂直居中公式与 Widget::Paint 坐标传递的衔接核对（OnPaint 拿到的 x/y 已是绝对坐标，offsetY 叠加正确性）
