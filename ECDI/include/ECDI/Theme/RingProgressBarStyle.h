#pragma once

#include "ECDI/Theme/StyleField.h"
#include "ECDI/Core/Color.h"

#include <optional>

namespace ECDI{

/// @brief RingProgressBar 专属样式（Phase 33——轨道/进度双色 + 环宽；无 TextStyle——纯视觉控件无文字）
/// @details 与 ProgressBarStyle 同族同形态（D6：命名与 ProgressBar 同族同词；条 95① 一个概念只用一个词）。
///          ★ 字段只取三样：轨道色 / 进度色 / 环宽——C33-10④「不放内外边距」（无消费者，YAGNI 条 22）。
struct RingProgressBarStyle{
	StyleField<Color> trackColor;    ///< 轨道色（整环）
	StyleField<Color> fillColor;     ///< 进度弧色
	StyleField<float> thickness;     ///< 环宽（px；<= 0 ⇒ 后端 NormalizeArc 判定不绘制——沿 C33-4）
};

/// @brief RingProgressBar 样式运行时覆盖（D7 契约同 ProgressBarStyleOverride——Set() 标记 overridden）
struct RingProgressBarStyleOverride{
	std::optional<Color> trackColor;
	std::optional<Color> fillColor;
	std::optional<float> thickness;
};

}
