#pragma once

#include "ECDI/Core/Color.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace ECDI {

/// @brief 段内样式描述符（Phase 44，#88；Phase 45 扩展）
/// @details 允许覆盖字体族、字重、斜体、字号与前景色。size 覆盖 = DIP 绝对值（与 Font.size 同语义，不跨换算）；
///          无效值（非有限 / ≤ 0）视同 nullopt（D45-11）。
struct SpanStyle {
	std::optional<std::string>   family     = std::nullopt; ///< 字体族（std::nullopt 表示继承基样式）
	std::optional<std::uint16_t> weight     = std::nullopt; ///< 字重（400=Regular, 700=Bold；std::nullopt 表示继承）
	std::optional<Color>         foreground = std::nullopt; ///< 前景色（std::nullopt 表示继承控件当前前景色）
	std::optional<bool>          italic     = std::nullopt; ///< 斜体（std::nullopt 表示继承）
	std::optional<float>         size       = std::nullopt; ///< 字号（DIP 绝对值；std::nullopt 表示继承）

	bool operator==(const SpanStyle&) const noexcept = default;
};

/// @brief 是否具有有效的 size 覆盖（D45-11 内部辅助）
inline bool IsEffectiveSizeOverride(const SpanStyle& s) noexcept {
	return s.size.has_value() && *s.size < 1.0e9f && *s.size > 0.0f;
}

/// @brief 样式区间契约（Phase 44，#88）
/// @details 码点索引空间；区间半开 [startCp, startCp + cpCount)；空 spans 列表等价于普通纯文本
struct TextSpan {
	std::size_t startCp = 0; ///< 起始码点索引（UTF-8 解码后的字符位置，非字节偏移）
	std::size_t cpCount = 0; ///< 覆盖的码点数量
	SpanStyle   style{};     ///< 样式覆盖属性

	bool operator==(const TextSpan&) const noexcept = default;
};

} // namespace ECDI
