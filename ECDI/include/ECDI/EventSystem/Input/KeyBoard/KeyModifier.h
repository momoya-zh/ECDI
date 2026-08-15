#pragma once

namespace ECDI{

/// @brief 键盘修饰键（位标志，可组合——Ctrl+Shift+A 未来自然支持）
enum class KeyModifier{
	None  = 0,
	Shift = 1,
	Ctrl  = 2,
	Alt   = 4,
};

/// @brief 位或（位标志惯例配套——m = m | KeyModifier::Shift 免 static_cast 噪音）
constexpr KeyModifier operator|(KeyModifier lhs, KeyModifier rhs){
	return static_cast<KeyModifier>(static_cast<int>(lhs) | static_cast<int>(rhs));
}

}
