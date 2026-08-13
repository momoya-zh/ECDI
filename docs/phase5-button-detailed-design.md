# Phase 5.3 Button 详细设计 v1.0

> 日期：2026-08-13 ｜ 状态：已确认 ｜ 方式：清单式问答（D1-D5）+ GPT 评审（8.5/10）

## 决策记录

### D1 TextWidget.h/cpp（新建，K&R + BOM）

```cpp
// TextWidget.h
#pragma once

#include "ECDI/Core/Color.h"
#include "ECDI/Core/Font.h"
#include "ECDI/Core/Point.h"
#include "ECDI/Widget/Widget.h"

#include <string>

namespace ECDI{

class PaintContext;   // 前向声明（引用参数）

/// @brief 文本控件基类（B1：第二个文本控件出现时抽取）
/// @details 职责：文本数据/字体/颜色/文本绘制/位置计算；
/// Widget = 几何/可见性/事件；Label/Button/TextBox = 自己的行为。
class TextWidget: public Widget{

public:

	explicit TextWidget(const std::string& text);

	explicit TextWidget(std::string&& text);

	void SetText(const std::string& text);

	void SetText(std::string&& text);

	const std::string& GetText() const noexcept;

	void SetTextColor(const Color& color);

	const Color& GetTextColor() const noexcept;

protected:

	/// @brief 文本绘制位置（B3：对齐策略虚方法，不写死）
	/// @param textWidth 文本宽度——【供派生类使用】（水平居中需要；基类默认左对齐不用）
	/// @param lineHeight 行高——【所有文本控件使用】（垂直居中）
	/// @return 默认：水平左对齐 + 垂直居中（P7 定案）
	virtual Point CalculateTextPosition(int x, int y, float textWidth, float lineHeight) const;

	/// @brief 绘制文本（空文本跳过 → MeasureText 宽高 → CalculateTextPosition → DrawText）
	void DrawTextContent(PaintContext& ctx, int x, int y);

	std::string m_text;

	Color m_textColor = Color::Black();

	Font m_font{};  // 预留：未来 SetFont() 一行接入，OnPaint 零改动

};

}
```

```cpp
// TextWidget.cpp
#include "ECDI/Widget/TextWidget.h"
#include "ECDI/Core/Size.h"
#include "ECDI/Render/PaintContext.h"

#include <utility>

namespace ECDI{

TextWidget::TextWidget(const std::string& text): m_text(text){ }
TextWidget::TextWidget(std::string&& text): m_text(std::move(text)){ }

void TextWidget::SetText(const std::string& text){ m_text = text; }
void TextWidget::SetText(std::string&& text){ m_text = std::move(text); }
const std::string& TextWidget::GetText() const noexcept{ return m_text; }

void TextWidget::SetTextColor(const Color& color){ m_textColor = color; }
const Color& TextWidget::GetTextColor() const noexcept{ return m_textColor; }

Point TextWidget::CalculateTextPosition(int x, int y, float textWidth, float lineHeight) const{
	(void)textWidth;                              // 默认左对齐不用宽度（D1 注释：textWidth 供派生类）
	const float offsetY = (static_cast<float>(GetHeight()) - lineHeight) / 2.0f;   // 负值合法不修正
	return Point{ static_cast<float>(x), static_cast<float>(y) + offsetY };
}

void TextWidget::DrawTextContent(PaintContext& ctx, int x, int y){
	if (m_text.empty())
		return;
	const Size textSize = ctx.MeasureText(m_font, m_text);   // 宽高一次拿（D5：见性能注记）
	ctx.DrawText(
		CalculateTextPosition(x, y, textSize.width, textSize.height),
		m_text, m_textColor, m_font
	);
}

}
```

**⚠️ D5 性能注记（GPT 提出，决定保持现状 + 记录优化点）**：
- 统一 MeasureText：Label 每帧多测一次宽度（GDI GetTextExtentPoint32W 微秒级，当前控件数量个位数无感）
- GPT 方案 B（条件测量：仅水平对齐需要时测宽）需要引入"是否需要宽度"状态/虚方法——复杂度换取当前不存在的性能问题
- **决定**：保持统一 MeasureText（YAGNI，接口简单）；**优化点封闭在 DrawTextContent 内部**（未来 100+ Label 场景再加条件测量，接口零变化——与 D2 测量 DC 的"未来内部优化"同款模式）

### D2 Label.h/cpp（重写，行为零变化）

```cpp
// Label.h
#pragma once

#include "ECDI/Widget/TextWidget.h"

namespace ECDI{

class Label: public TextWidget{

public:

	Label() = default;

	explicit Label(const std::string& text);

	explicit Label(std::string&& text);

protected:

	void OnPaint(PaintContext& ctx, int x, int y) override;

};

}
```

```cpp
// Label.cpp
Label::Label(const std::string& text): TextWidget(text){ }
Label::Label(std::string&& text): TextWidget(std::move(text)){ }

void Label::OnPaint(PaintContext& ctx, int x, int y){
	DrawTextContent(ctx, x, y);   // 默认对齐（左对齐 + 垂直居中）——与 5.2 行为完全一致
}
```

### D3 Button.h/cpp（⚠️ GPT 反转修正：构造直接初始化成员）

```cpp
// Button.h
#pragma once

#include "ECDI/Core/Point.h"
#include "ECDI/Widget/TextWidget.h"

#include <string>

namespace ECDI{

class MouseButtonDownEvent;
class MouseButtonUpEvent;

class Button: public TextWidget{

public:

	Button() = default;

	explicit Button(const std::string& text);

	explicit Button(std::string&& text);

	bool CanFocus() const noexcept override { return true; }

protected:

	Point CalculateTextPosition(int x, int y, float textWidth, float lineHeight) const override;   // P3 居中

	void OnMouseButtonDown(const MouseButtonDownEvent&)override;

	void OnMouseButtonUp(const MouseButtonUpEvent&)override;

	virtual void OnClick();

	void OnPaint(PaintContext& ctx,int x,int y) override;

private:

	bool m_pressed = false;   // 保留（5.4 按下态视觉用）

};

}
```

```cpp
// Button.cpp
Button::Button(const std::string& text): TextWidget(text){

	m_textColor = Color::White();   // B2 默认白字——构造直接初始化成员（见下）

}

Button::Button(std::string&& text): TextWidget(std::move(text)){

	m_textColor = Color::White();

}

Point Button::CalculateTextPosition(int x, int y, float textWidth, float lineHeight) const{

	const float offsetX = (static_cast<float>(GetWidth()) - textWidth) / 2.0f;
	const float offsetY = (static_cast<float>(GetHeight()) - lineHeight) / 2.0f;

	return Point{ static_cast<float>(x) + offsetX, static_cast<float>(y) + offsetY };

}

// OnMouseButtonDown/Up/OnClick 原样保留

void Button::OnPaint(PaintContext& ctx,int x,int y){

	ctx.DrawRect(Rect{...}, Color::FromRGBA8(80, 120, 220));   // 蓝底不变
	DrawTextContent(ctx, x, y);   // 白字居中叠背景；空文本只跳文本，背景照画

}
```

**⚠️ GPT 前后反转记录**：初步设计时 GPT 建议 `SetTextColor(White)`（子类用公共 API 设样式）；详细设计时 GPT 反转为 `m_textColor = White()`（构造直接初始化成员更自然——"像 vector 构造里 push_back 一样绕一层"）。**采纳本轮**：构造阶段直接赋值成员是 C++ 惯例（构造期间不经过公共接口，避免虚调用风险——虽 SetTextColor 非虚，但精神一致）。两者功能完全等价。

### D4 Color operator==

```cpp
	/// @brief 相等比较（C++20 默认，r/g/b/a 四字段全比较；Alpha 就位后自动生效）
	constexpr bool operator==(const Color&) const noexcept = default;
```

插入 Color.h 的 FromRGBA8 后、`};` 前。测试可读性提升：`FRAMEWORK_ASSERT(color == Color::White())`。

### D5 main.cpp + vcxproj

- main.cpp：`DemoButton(L"Click Me")` → `("Click Me")`（2 处）+ 5.3 断言段（P5 定稿：2 命令顺序 + `txt.color == Color::White()` 完整比较 + 水平/垂直居中动态期望）
- vcxproj：ClCompile 加 `src\Widget\TextWidget.cpp`（Panel.cpp 与 Widget.cpp 间）；ClInclude 加 `Include\ECDI\Widget\TextWidget.h`（Panel.h 与 Widget.h 间）

## 回归验证清单

- 4.5 / 4.6 / 5.1 / 5.2 全部断言继续通过（尤其 5.2 Label 断言——继承后行为不变）
- 5.3 新断言（Button 2 命令顺序 + 水平/垂直居中）
- 窗口：Button 显示 "Click Me" 白字居中叠蓝底；Label 显示 "ECDI Widget System"（左对齐）不变

## 修订记录

- v1.0（2026-08-13）：D1-D5 定稿——GPT 三条处理：D1 参数注释明确、D3 构造成员赋值（反转采纳）、D5 保持统一 MeasureText + 优化点记录
