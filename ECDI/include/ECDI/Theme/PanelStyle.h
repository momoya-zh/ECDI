#pragma once

#include "ECDI/Theme/StyleField.h"
#include "ECDI/Core/Color.h"

namespace ECDI{

/// @brief Panel 专属样式
/// @details MVP 主动不支持 Override（无 PanelStyleOverride）——非 StyleField 不支持，
/// 而是 Phase 9 范围控制（YAGNI）；未来有需求再加。
struct PanelStyle{
	StyleField<Color> background;    ///< 背景色
};

}
