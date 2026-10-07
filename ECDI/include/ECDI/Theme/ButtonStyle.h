#pragma once

#include "ECDI/Theme/StyleField.h"
#include "ECDI/Core/Color.h"
#include "ECDI/Core/TextAlignment.h"   // ★ Phase 30 △3：textAlignment 字段（公共→公共——叶子头零依赖）

#include <optional>

namespace ECDI{

/// @brief Button 专属样式（文字颜色/字体统一来自 TextStyle——禁止在此重复，单一视觉真相）
struct ButtonStyle{
	StyleField<Color> background;         ///< 背景色（正常态）
	StyleField<Color> border;             ///< 焦点边框色
	StyleField<float> borderWidth;        ///< 边框宽度（内缩量）
	StyleField<float> cornerRadius;       ///< 圆角半径（0 = 直角；Phase 8 DrawRoundedRect 消费）
	StyleField<Color> pressedBackground;  ///< 按下态背景色
	StyleField<Color> hoverBackground;    ///< hover 背景色（默认 = background——无 override 零视觉变化；改 background 必须同设，否则 hover 回落主题默认）
	StyleField<TextAlignment> textAlignment;   ///< ★ Phase 30 △3：文字水平对齐（主题缺省 **Center** = Phase 30 前行为逐位一致——⚠️ 不得依赖枚举零值 Left，缺省必须显式注入 C30-3；垂直恒居中 P7 不动）
};

/// @brief Button 样式运行时覆盖（无 foreground——文字经 TextStyleOverride）
struct ButtonStyleOverride{
	std::optional<Color> background;
	std::optional<Color> border;
	std::optional<float> borderWidth;
	std::optional<float> cornerRadius;
	std::optional<Color> pressedBackground;
	std::optional<Color> hoverBackground;
	std::optional<TextAlignment> textAlignment;   ///< ★ Phase 30 △3：对齐覆盖
};

}
