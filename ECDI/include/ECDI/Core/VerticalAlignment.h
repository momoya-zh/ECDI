#pragma once

#include <cstdint>

namespace ECDI{

/// @brief 文本垂直对齐（内容不足一屏时行箱的起始位置）
/// @note 缺省 Top = 枚举零值 ⇒ 依赖零值不会改变任何既有行为。
///       ★ 与 ButtonStyle::textAlignment 的 C30-3 情形**不同**：那里缺省 Center(1) ≠ 零值 Left(0)，
///         故必须显式注入主题缺省；本枚举缺省与零值一致，显式注入只是纪律、不承担正确性。
/// ★★ 前瞻契约：若将来把主题缺省改为 Center，**C30-3 立即触发** —— 必须同时改
///   DefaultTheme::GetTextBoxStyle() 的注入，不得只改缺省值了事。
enum class VerticalAlignment : std::uint8_t{
	Top    = 0,   ///< 顶对齐（缺省 = Phase 37 前行为，逐位不变）
	Center = 1,   ///< 垂直居中（仅当内容高 ≤ 文本区高时生效；超屏自动退化为 Top）
};

}
