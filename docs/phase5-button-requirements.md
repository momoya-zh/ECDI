# Phase 5.3 Button 完整化职责确认 v1.0

> 日期：2026-08-13 ｜ 状态：已确认 ｜ 方式：清单式问答（B1-B6）+ GPT 评审

## 背景

Button 现状：`m_text` 为 `std::wstring`（5.1 D8 暂缓项）、OnPaint 只画蓝底（`FromRGBA8(80,120,220)`）无文本、`m_pressed` 状态成员**已有**（OnMouseButtonDown/Up 管理 + OnClick）。5.2 Label 已落地（第一个文本消费者），Button 是**第二个文本控件**——职责确认 L8 定的 TextWidget 抽取时机。

## 决策记录

### B1 TextWidget 抽象形态 —— A：继承基类 ✅

```cpp
class TextWidget : public Widget {
public:
	explicit TextWidget(const std::string& text);      // B4：直接 string
	explicit TextWidget(std::string&& text);

	void SetText(const std::string& text);
	void SetText(std::string&& text);
	const std::string& GetText() const noexcept;

	void SetTextColor(const Color& color);
	const Color& GetTextColor() const noexcept;

protected:
	// ⚠️ B3 修订：对齐策略不写死——子类可 override（Label 默认左对齐；Button 未来水平居中）
	virtual Point CalculateTextPosition(int x, int y, float lineHeight) const;

	// 实际绘制：空文本跳过 → 测量行高 → CalculateTextPosition → DrawText
	void DrawTextContent(PaintContext& ctx, int x, int y);

	std::string m_text;
	Color m_textColor = Color::Black();
	Font m_font{};                                     // 预留
};

class Label : public TextWidget {   // OnPaint = DrawTextContent（用默认对齐）
class Button : public TextWidget {  // OnPaint = DrawRect(蓝底) + DrawTextContent
```

**职责分层**：Widget = 几何/可见性/事件；TextWidget = 文本/字体/颜色/文本绘制；Label/Button = 自己的行为。经典继承结构（TextBox 第三个消费者加入时直接继承）。

### B2 Button 文本颜色默认白 —— A ✅

TextWidget 默认 `m_textColor = Black()`（Label 用）；**Button 成员初始化/构造设 `Color::White()`**（控件自带默认样式，调用方 `Button("OK")` 即白字叠蓝底，不用额外 SetTextColor）。

### B3 对齐策略 —— ⚠️ GPT 修正：不写死对齐算法 ✅

原方案把"左对齐+垂直居中"写死在 DrawTextContent——**GPT 修正**：Label 默认左对齐、Button 未来很可能水平居中（多数 GUI 框架 Button 默认 Center），写死会导致 Button 被迫重写绘制逻辑、抽象失去意义。

**定稿**：TextWidget 提供 `virtual Point CalculateTextPosition(int x, int y, float lineHeight) const`——**默认实现 = 左对齐 + 垂直居中**（P7 定案），子类可 override 改对齐（Button 未来居中只 override 这一个方法）。DrawTextContent 内部：空文本跳过 → LineHeight → CalculateTextPosition → DrawText。

### B4 wstring → string —— A：随抽取一步到位 ✅

5.1 D8 收尾：`m_text` 以 `std::string` 形态进入 TextWidget，Button 构造/SetText 全 string 化（Label 5.2 已迁）。`DemoButton` 的 `using Button::Button` 继承构造自动获得 string 版。

### B5 main.cpp —— A ✅（含 GPT 修正）

- `DemoButton(L"Click Me")` → `("Click Me")`（win1/win2 共 2 处）
- 5.3 断言段：Button → 2 命令顺序断言（先 DrawRect 背景后 DrawText 文本 = D5 命令顺序语义）
- **GPT 修正**：颜色断言用**完整比较**（`txt.color == Color::White()` 或逐字段），不只查 `r`——为未来 Alpha（RGBA 四通道）准备
- ⚠️ 连带决策点：`Color` 目前无 `operator==`（POD 聚合）——为完整比较需给 Color 加 **constexpr operator==（4 字段全比较）**（未来主题系统比较颜色也要用）

### B6 边界 —— A：强烈同意 ✅

- **按下态视觉：不做**——`m_pressed` 数据已就绪，但 OnPaint 用它变色需要重绘触发（Invalidate 未实现，L6 定案）。不为按下态提前引入 Invalidate（跨层依赖陷阱：Widget→Window 通知）。**全部归 5.4**（Focus/Capture/Invalidate/Pressed Visual 一起做）
- 点击行为不动（OnMouseButtonDown/Up/OnClick 已工作）
- **Label 回归验证**（抽 TextWidget 后行为必须不变）
- 不做 AutoSize（架构债务同 Label）
- 新文件：TextWidget.h/cpp（vcxproj 注册 2 条，CMake GLOB 自动收）

## 待初步设计固化

1. TextWidget 类最终形态（含 CalculateTextPosition 默认实现）
2. Button OnPaint 完整逻辑（背景色 + 文本叠层）
3. Label 改造点（继承 TextWidget，行为不变）
4. Color operator== 的添加（若确认）
5. main.cpp 断言段精确内容（完整颜色比较）
