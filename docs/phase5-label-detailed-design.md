# Phase 5.2 Label 详细设计 v1.0

> 日期：2026-08-13 ｜ 状态：已确认 ｜ 方式：清单式问答（D1-D4）+ GPT 评审（9.5/10）

## 决策记录

### D1 Label.h 编辑点

**include 区——显式 include（A，Include What You Use）**：

```cpp
#pragma once

#include "ECDI/Core/Color.h"      // 自包含：Label.h 直接用 Color
#include "ECDI/Core/Font.h"       // 自包含：Label.h 直接用 Font
#include "ECDI/Widget/Widget.h"

#include <string>
```

不依赖 `Widget.h → PaintContext.h → Color.h/Font.h` 的传递——否则未来有人删 Widget.h 的 PaintContext include，Label.h 突然编译不过。

**类定义**：

```cpp
class Label : public Widget {
public:
	Label() = default;
	explicit Label(const std::string& text);       // L1：UTF-8
	explicit Label(std::string&& text);

	void SetText(const std::string& text);
	void SetText(std::string&& text);
	const std::string& GetText() const noexcept;

	void SetTextColor(const Color& color);         // L4
	const Color& GetTextColor() const noexcept;

protected:
	void OnPaint(PaintContext& ctx, int x, int y) override;   // L3

private:
	std::string m_text;                            // L1：UTF-8
	Color m_textColor = Color::Black();            // L4
	Font m_font{};  // L4 预留：未来 SetFont() 一行接入（m_font = font），OnPaint 零改动（GPT 补充注释）
};
```

### D2 Label.cpp 编辑点

```cpp
#include "ECDI/Widget/Label.h"

#include <utility>

namespace ECDI{

Label::Label(const std::string& text): m_text(text){
}

Label::Label(std::string&& text): m_text(std::move(text)){
}

void Label::SetText(const std::string& text){
	m_text = text;
}

void Label::SetText(std::string&& text){
	m_text = std::move(text);
}

const std::string& Label::GetText() const noexcept{
	return m_text;
}

void Label::SetTextColor(const Color& color){
	m_textColor = color;
}

const Color& Label::GetTextColor() const noexcept{
	return m_textColor;
}

// ── 绘制（L3/L5/P2：只画文本，透明背景，垂直居中）────────────

void Label::OnPaint(PaintContext& ctx, int x, int y){
	// 空文本：零命令零绘制（P2-a）
	if (m_text.empty())
		return;

	// 垂直居中（P2-b：LineHeight 单次调用；负 offsetY 合法不修正——控件比文本小是布局问题）
	const float lineHeight = ctx.LineHeight(m_font);
	const float offsetY = (static_cast<float>(GetHeight()) - lineHeight) / 2.0f;

	ctx.DrawText(
		Point{ static_cast<float>(x), static_cast<float>(y) + offsetY },
		m_text, m_textColor, m_font
	);
}

}
```

无新增 include（PaintContext 完整类型经 Label.h 传递；OnPaint 只用 ctx.LineHeight/DrawText）。

### D3 main.cpp 编辑点

- **157 行**：`make_unique<ECDI::Label>(L"ECDI Widget System")` → `("ECDI Widget System")`
- **184 行**：`make_unique<ECDI::Label>(L"Second Window")` → `("Second Window")`
- **5.2 断言段**（插在 5.1 文本链路段后、`DemoApplication application;` 前）——**动态期望值**：

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

	const float expectedY = 5.0f + (30.0f - backend.LineHeight(ECDI::Font{})) / 2.0f;
	FRAMEWORK_ASSERT(cmd.pos.y == expectedY);       // 验证"用了垂直居中公式"，不耦合模拟值
	FRAMEWORK_ASSERT(cmd.font.size == 14.0f);       // 默认 Font()
}
```

include 无新增（Label.h 已有）。

### D4 坐标衔接核对（验证通过）

```
Widget::Paint(ctx, offsetX, offsetY) → x = offsetX + m_geometry.x（绝对坐标）
  → Label::OnPaint(ctx, x, y)：x/y = Label 左上角绝对坐标
  → offsetY 只叠垂直偏移、不碰水平 → 衔接无误
```

## ⚠️ 技术债务记录（GPT 补充，明确标记）

**债务：Widget 无法在非 Paint 时刻访问 TextMeasurer**（5.1 路线 X 的架构约束——TextMeasurer 只经 PaintContext 注入）。

影响面（已确认，5.5 TextBox 前必须解决）：

| 受影响能力 | 场景 |
|-----------|------|
| Label::AutoSize | SetText 时算尺寸（L2 已砍） |
| Widget::GetPreferredSize() | 布局/调用方取推荐尺寸（暂缓） |
| Layout 内容自适应 | 布局系统拿不到测量 |
| **TextBox 光标定位** | 非 Paint 时刻测文本宽度（5.5 必然遇到） |

候选解决方向（届时再定）：布局系统注入测量器 / Window 提供测量服务。**当前不做 AutoSize 是正确的边界维护**（L2）。

## 修订记录

- v1.0（2026-08-13）：详细设计定稿——D1-D4 确认 + GPT 两条补充（m_font 注释、技术债务标记）
