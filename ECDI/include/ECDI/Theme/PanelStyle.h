#pragma once

#include "ECDI/Theme/StyleField.h"
#include "ECDI/Core/Color.h"

#include <optional>

namespace ECDI{

/// @brief Panel 专属样式
/// @details 样式字段均为 StyleField（含 override 标志位，D7 契约）。
/// 单实例覆盖经 Panel::SetStyle(PanelStyleOverride)——2026-08-29 需求落地
/// （此前 MVP 主动不支持 Override，归 Phase 9 范围控制 YAGNI；需求出现即补，与 Button/TextBox 同构）。
struct PanelStyle{
	StyleField<Color> background;    ///< 背景色
	StyleField<float> cornerRadius;  ///< 圆角半径（0 = 直角；P1 形态——Phase 8 DrawRoundedRect 消费）
	StyleField<float> borderWidth;   ///< 恒显描边宽（0 = 无边框；>0 = 双矩形描边环）
	StyleField<Color> borderColor;   ///< 边框色（默认透明 = 不画）
};

/// @brief Panel 样式运行时覆盖（P1 扩展：background/cornerRadius/borderWidth/borderColor）
/// @details 与 ButtonStyleOverride / TextBoxStyleOverride 同构：以 std::optional 表达"是否覆盖该字段"。
struct PanelStyleOverride{
	std::optional<Color> background;
	std::optional<float> cornerRadius;
	std::optional<float> borderWidth;
	std::optional<Color> borderColor;
};

}
