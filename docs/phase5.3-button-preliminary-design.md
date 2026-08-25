# Phase 5.3 Button 初步设计 v1.0

> 日期：2026-08-13 ｜ 状态：已确认 ｜ 方式：清单式问答（P1-P6）+ GPT 评审

## 决策记录

### P1 TextWidget 最终形态（B1/B3/B4 + GPT 确认）

```cpp
class TextWidget : public Widget {
public:
	explicit TextWidget(const std::string& text);
	explicit TextWidget(std::string&& text);

	void SetText(const std::string& text);
	void SetText(std::string&& text);
	const std::string& GetText() const noexcept;

	void SetTextColor(const Color& color);
	const Color& GetTextColor() const noexcept;

protected:
	// 对齐策略虚方法（B3）：默认左对齐+垂直居中；子类 override 改对齐
	// 签名带 textWidth：水平居中需要宽度（Button）；textHeight = 行高（MeasureText 同源）
	virtual Point CalculateTextPosition(int x, int y, float textWidth, float lineHeight) const;

	// 空文本跳过 → MeasureText(宽高一次拿) → CalculateTextPosition → DrawText
	void DrawTextContent(PaintContext& ctx, int x, int y);

	std::string m_text;
	Color m_textColor = Color::Black();
	Font m_font{};
};
```

```cpp
Point TextWidget::CalculateTextPosition(int x, int y, float textWidth, float lineHeight) const{
	(void)textWidth;                              // 默认左对齐不用宽度
	const float offsetY = (static_cast<float>(GetHeight()) - lineHeight) / 2.0f;   // 负值合法不修正
	return Point{ static_cast<float>(x), static_cast<float>(y) + offsetY };
}

void TextWidget::DrawTextContent(PaintContext& ctx, int x, int y){
	if (m_text.empty())
		return;
	const Size textSize = ctx.MeasureText(m_font, m_text);   // 宽高一次拿（居中需要宽度）
	ctx.DrawText(
		CalculateTextPosition(x, y, textSize.width, textSize.height),
		m_text, m_textColor, m_font
	);
}
```

- **DrawTextContent 参数保留 int x/y**（GPT 提议 Point 浮点化——暂缓）：与 `Widget::OnPaint` 接口一致；亚像素/DPI/动画浮点化是 Phase 8 渲染增强的事，现在半浮点化两头不靠
- **MeasureText 替代 LineHeight**：居中需要宽度 → MeasureText 必调，宽高一次拿（`.height` 与 LineHeight 同源，GDIBackend 同用 GetTextMetrics）——5.2 断言不受影响（Recording 返回 {10,14}）

### P2 Label 改造（行为零变化）

```cpp
class Label : public TextWidget {
public:
	Label() = default;
	explicit Label(const std::string& text) : TextWidget(text) {}
	explicit Label(std::string&& text) : TextWidget(std::move(text)) {}
protected:
	void OnPaint(PaintContext& ctx, int x, int y) override;   // = DrawTextContent(ctx, x, y)
};
```

Label.h 大幅简化（SetText/GetText/SetTextColor 全删，继承）；Label.cpp 只剩构造 + OnPaint。**5.2 断言段必须继续通过（回归验证）**。

### P3 Button 改造（⚠️ 定案：水平居中 + 垂直居中）

```cpp
class Button : public TextWidget {
public:
	Button() = default;
	explicit Button(const std::string& text);
	explicit Button(std::string&& text);

	bool CanFocus() const noexcept override { return true; }

protected:
	Point CalculateTextPosition(int x, int y, float textWidth, float lineHeight) const override;   // 新增：水平居中

	void OnMouseButtonDown(const MouseButtonDownEvent&) override;   // 保留
	void OnMouseButtonUp(const MouseButtonUpEvent&) override;       // 保留
	virtual void OnClick();                                          // 保留
	void OnPaint(PaintContext& ctx, int x, int y) override;

private:
	bool m_pressed = false;   // 保留（5.4 按下态视觉用）
};
```

```cpp
Button::Button(const std::string& text) : TextWidget(text){
	SetTextColor(Color::White());          // B2 默认白字——GPT 修正：用公共 API，不直接改基类成员
}

Point Button::CalculateTextPosition(int x, int y, float textWidth, float lineHeight) const{
	const float offsetX = (static_cast<float>(GetWidth()) - textWidth) / 2.0f;   // 水平居中
	const float offsetY = (static_cast<float>(GetHeight()) - lineHeight) / 2.0f; // 垂直居中
	return Point{ static_cast<float>(x) + offsetX, static_cast<float>(y) + offsetY };
}

void Button::OnPaint(PaintContext& ctx, int x, int y){
	ctx.DrawRect(
		Rect{ static_cast<float>(x), static_cast<float>(y),
		      static_cast<float>(GetWidth()), static_cast<float>(GetHeight()) },
		Color::FromRGBA8(80, 120, 220)     // 蓝底精确保持
	);
	DrawTextContent(ctx, x, y);            // 白字居中叠背景；空文本只跳文本，背景照画
}
```

- **Button 水平对齐 = 居中**（GPT 推荐方案 A + 用户表态确认）——绝大多数 GUI 框架按钮默认居中
- 构造内 `SetTextColor(White)`（GPT 风格修正：子类用公共 API 设默认样式，不直接改基类成员）

### P4 Color operator==（连带决策点）

```cpp
// Color.h 内（C++20 默认比较，一行）
constexpr bool operator==(const Color&) const noexcept = default;
```

四字段（r/g/b/a）全比较，Alpha 就位后自动生效。

## 实现

### P5 main.cpp

- `DemoButton(L"Click Me")` → `("Click Me")`（2 处）
- 5.3 断言段（插 5.2 段后）——**含水平居中期望（动态计算）**：

```cpp
// ── 5.3 Button 文本链路：先背景后文本 + 水平垂直居中，命令顺序 = 绘制顺序 ──
{
	ECDI::RecordingBackend backend;
	ECDI::CommandBuffer commands;
	ECDI::PaintContext ctx(commands, backend);

	ECDI::Button button("OK");
	button.SetPosition(10, 10);
	button.SetSize(100, 40);
	button.Paint(ctx, 0, 0);

	FRAMEWORK_ASSERT(commands.size() == 2);                       // 背景 + 文本
	const auto& bg = std::get<ECDI::DrawRectCommand>(commands[0]);   // 先背景
	FRAMEWORK_ASSERT(bg.rect.x == 10.0f && bg.rect.width == 100.0f);
	const auto& txt = std::get<ECDI::DrawTextCommand>(commands[1]);  // 后文本
	FRAMEWORK_ASSERT(txt.text == "OK");
	FRAMEWORK_ASSERT(txt.color == ECDI::Color::White());          // 完整比较（operator==）

	const float expectedX = 10.0f + (100.0f - backend.MeasureText(ECDI::Font{}, "OK").width) / 2.0f;
	FRAMEWORK_ASSERT(txt.pos.x == expectedX);                     // 水平居中（动态期望）
	const float expectedY = 10.0f + (40.0f - backend.LineHeight(ECDI::Font{})) / 2.0f;
	FRAMEWORK_ASSERT(txt.pos.y == expectedY);                     // 垂直居中（动态期望）
}
```

### P6 文件与构建

- **新增** TextWidget.h/cpp（vcxproj 注册 2 条；CMake GLOB 自动收）
- **重写** Label.h/cpp（简化）｜ **修改** Button.h/cpp ｜ **修改** Color.h（operator==）｜ **修改** main.cpp
- 风格：TextWidget 新文件按 Widget 系列（K&R + BOM + include 规范）

## 待详细设计固化

1. 各文件精确编辑点（TextWidget 新建 / Label 重写 / Button 修改 / Color 一行 / main.cpp 2+1）
2. vcxproj 注册点
3. 回归验证清单（5.2 Label 断言 + 4.5/4.6/5.1 断言全绿）
