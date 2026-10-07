#pragma once

#include <cstdint>

namespace ECDI{

/// @brief 水平文本对齐（★ Phase 30 △1：纯值类型、零依赖——Button 立即消费，
///        Phase 29 O1（wrap 每行对齐，经 `Line.width`）未来复用同一枚举）
/// @details ★ **独立公共头**（初设 D30-A 定案）：塞 `ButtonStyle.h` 会让未来 multiline
///          对齐产生 `TextWidget → ButtonStyle.h → TextAlignment` 的怪依赖方向；
///          独立头则 `ButtonStyle` 与 `TextWidget` **并列**依赖它。
/// @note ⚠️ **缺省值陷阱**：枚举零值 = `Left` ≠ 任何控件的「缺省对齐」——缺省语义
///          一律由主题**显式注入**（C30-3：Button 主题注入 `Center`），
///          禁止依赖 `TextAlignment{}` 零值。
enum class TextAlignment : std::uint8_t{
	Left   = 0,   ///< 左对齐（内容区起点）
	Center = 1,   ///< 水平居中（Button 主题缺省——与 Phase 30 前行为逐位一致）
	Right  = 2,   ///< 右对齐（内容区末端）
};

}
