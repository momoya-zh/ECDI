#pragma once

#include "ECDI/Theme/StyleField.h"
#include "ECDI/Core/Color.h"

#include <optional>

namespace ECDI{

/// @brief TextBox 专属样式（文字颜色/字体统一来自 TextStyle——禁止在此重复，单一视觉真相）
struct TextBoxStyle{
	StyleField<Color> background;    ///< 背景色
	StyleField<Color> border;        ///< 焦点边框色（点线框）
	StyleField<Color> selection;     ///< 选区高亮色
	StyleField<Color> composition;   ///< 组合串下划线色（IME）
	StyleField<float> caretWidth;    ///< 光标竖线宽
	StyleField<float> padding;       ///< 文本内边距（9.6 收尾方案 B：常驻布局属性，默认 0，不随焦点变化——焦点框改由 DrawFocusRect 绘制，零布局副作用）
	StyleField<float> cornerRadius;  ///< 圆角半径（0 = 直角；P1 形态——Phase 8 DrawRoundedRect 消费）
	StyleField<float> borderWidth;   ///< 恒显描边宽（0 = 无恒显边框；>0 = 双矩形描边环——⚠️ 与 border 焦点点线框并存，新语义非旧"焦点内缩"）
	StyleField<Color> borderColor;   ///< 恒显边框色（默认透明 = 不画）
};

/// @brief TextBox 样式运行时覆盖（无 foreground——文字经 TextStyleOverride）
struct TextBoxStyleOverride{
	std::optional<Color> background;
	std::optional<Color> border;
	std::optional<Color> selection;
	std::optional<Color> composition;
	std::optional<float> caretWidth;
	std::optional<float> padding;
	std::optional<float> cornerRadius;
	std::optional<float> borderWidth;
	std::optional<Color> borderColor;
};

}
