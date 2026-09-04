#pragma once

#include "ECDI/Core/Color.h"
#include "ECDI/Core/Font.h"
#include "ECDI/Core/Point.h"
#include "ECDI/Core/Size.h"
#include "ECDI/Theme/TextStyle.h"
#include "ECDI/Widget/Widget.h"

#include <string>

namespace ECDI{

class PaintContext;
class TextMeasurer;
class Theme;

/// @brief 文本控件基类（B1：第二个文本控件出现时抽取）
/// @details 职责：文本数据/字体/颜色/文本绘制/位置计算；
/// Widget = 几何/可见性/事件；Label/Button/TextBox = 自己的行为。
/// Phase 9：持有 TextStyle（m_style——文字视觉唯一来源）；ApplyTheme/SetStyle 注入默认值/运行时覆盖。
class TextWidget: public Widget{

public:

	TextWidget();   // 空文本控件（Label/Button/TextBox 默认构造依赖——构造期注入 TextStyle）

	explicit TextWidget(const std::string& text);

	explicit TextWidget(std::string&& text);

	/// @brief 设置文本（9.7 修：升格 virtual——TextBox override 置行缓存失效；外部替换文本绕过编辑操作路径）
	virtual void SetText(const std::string& text);

	virtual void SetText(std::string&& text);

	const std::string& GetText() const noexcept;

	/// @brief 设置文本颜色（旧 API 保留——内部转发 SetStyle，单一状态来源；Phase 9）
	void SetTextColor(const Color& color);

	const Color& GetTextColor() const noexcept;

	/// @brief 设置字体（旧 API 保留——内部转发 SetStyle；Font 纯数据值语义）
	void SetFont(const Font& font);

	/// @brief 应用主题（同步默认值到 m_style，只更新未 Override 属性——D7）
	/// @details virtual——Button/TextBox override 扩展（先调基类注入 TextStyle，再注入专属 Style）。
	/// 基类构造期调用时静态派发到 TextWidget::ApplyTheme（派生类构造函数需再次调用）。
	virtual void ApplyTheme(const Theme& theme);

	/// @brief 运行时文本样式覆盖（D7：Set() 标记 Override，后续 ApplyTheme 不覆盖）
	void SetStyle(TextStyleOverride override);

	/// @brief 内容测量 preferred（9.8 override——单行文本宽 + 行高；经 ResolveMeasurer 拿测量器）
	/// @details 有测量器 → 内容测量；无（运行时 fallback——无窗口且未注入）→ Widget 默认当前尺寸
	[[nodiscard]] Size GetPreferredSize() const override;

protected:

	/// @brief 文本绘制位置（B3：对齐策略虚方法，不写死）
	/// @param textWidth 文本宽度——供派生类使用（水平居中需要；基类默认左对齐不用）
	/// @param lineHeight 行高——所有文本控件使用（垂直居中）
	/// @return 默认：水平左对齐 + 垂直居中（P7 定案）
	virtual Point CalculateTextPosition(int x, int y, float textWidth, float lineHeight) const;

	/// @brief 测量器解析接缝（9.8——ProgressBar ResolveAnimationManager 同构；仅服务 preferred 测量，不扩散）
	/// @details 正常运行 = Window 的 TextMeasurer（const 方法内 const_cast——GetLineHeight 先例）；
	/// 测试派生类 override 返回 FakeTextMeasurer；nullptr = 无窗口且未注入 → 调用方走 Widget 默认
	[[nodiscard]] virtual TextMeasurer* ResolveMeasurer() const;

	/// @brief 绘制文本（空文本跳过 → MeasureText 宽高 → CalculateTextPosition → DrawText）
	void DrawTextContent(PaintContext& ctx, int x, int y);

	std::string m_text;

	/// @brief 文本样式（foreground + font）——所有文本控件的文字视觉唯一来源（Phase 9）
	TextStyle m_style;

private:

	/// @brief preferred 内容测量实现（9.8——private：TextWidget 语义组成部分，非 cpp 匿名辅助）
	/// @details Label/Button 0 inset（§3.2 冻结）：{文本测量宽, 行高}——空文本 MeasureText 返回 {0,0} → 宽 0 诚实
	[[nodiscard]] Size DoMeasureText(TextMeasurer& measurer) const;

};

}
