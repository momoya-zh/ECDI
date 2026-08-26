#pragma once

#include "ECDI/Theme/RadioStyle.h"
#include "ECDI/Widget/StateWidget.h"

namespace ECDI{

class PaintContext;

/// @brief 单选按钮（6.2；外圆 + 选中圆点；同父互斥）
/// @details 互斥范围 = 直接父 Widget 的直接子节点中类型为 Radio 的控件（契约 4——
/// 不跨 Container 嵌套；不引入 RadioGroup——YAGNI）。
/// 程序 API 可取消（SetChecked(false)）；用户交互不可取消（OnClickToggle = SetChecked(true)）。
/// 通知顺序契约（v1.1 冻结）：SetChecked(true) → 兄弟先 OnCheckedChanged(false) → 自身后 OnCheckedChanged(true)。
class Radio: public StateWidget{

public:

	Radio();                                   // 默认构造——注入 RadioStyle
	explicit Radio(const std::string& text);

	using TextWidget::SetStyle;   // 防名字隐藏——保留 TextStyle 设置

	void ApplyTheme(const Theme& theme) override;
	void SetStyle(RadioStyleOverride override);

	/// @brief 设置选中状态（override——checked=true 时同父互斥：先取消兄弟 Radio 再选中自身）
	void SetChecked(bool checked) override;

protected:

	/// @brief Radio 专属样式（protected——测试派生类可访问；不含 foreground——TextStyle 是文字唯一来源）
	RadioStyle m_style;

	/// @brief 文本位置（override——与外圆垂直中心对齐，非控件中心：外圆 16px 在顶部，默认居中会偏下半个字）
	/// @details DrawTextContent 的 x 已含圆偏移（x+circleSize+4）；本方法只修正垂直偏移 = (circleSize - lineHeight)/2
	Point CalculateTextPosition(int x, int y, float textWidth, float lineHeight) const override;

	void OnClickToggle() override;   // SetChecked(true)（非反转——契约 5）

	void OnPaint(PaintContext& ctx, int x, int y) override;

private:

	/// @brief 取消同父兄弟 Radio（互斥实现——遍历 GetParent() 直接子节点中 Radio ≠ this）
	void UncheckSiblings();

};

}
