#pragma once

#include "ECDI/Core/Color.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace ECDI {

/// @brief 段内样式描述符（Phase 44，#88）
/// @details 仅允许覆盖字体族、字重与前景色；严禁包含 size（闸门 3）或斜体（闸门 2 / L44-8）
struct SpanStyle {
	std::optional<std::string>   family     = std::nullopt; ///< 字体族（std::nullopt 表示继承基样式）
	std::optional<std::uint16_t> weight     = std::nullopt; ///< 字重（400=Regular, 700=Bold；std::nullopt 表示继承）
	std::optional<Color>         foreground = std::nullopt; ///< 前景色（std::nullopt 表示继承控件当前前景色）

	bool operator==(const SpanStyle&) const noexcept = default;
};

/// @brief 样式区间契约（Phase 44，#88）
/// @details 码点索引空间；区间半开 [startCp, startCp + cpCount)；空 spans 列表等价于普通纯文本
struct TextSpan {
	std::size_t startCp = 0; ///< 起始码点索引（UTF-8 解码后的字符位置，非字节偏移）
	std::size_t cpCount = 0; ///< 覆盖的码点数量
	SpanStyle   style{};     ///< 样式覆盖属性

	bool operator==(const TextSpan&) const noexcept = default;
};

} // namespace ECDI
