#pragma once

namespace ECDI{

/// @brief 缓动曲线枚举（9.6 动画系统——四种一步到位，纯数学成本恒定）
/// @details enum + switch 而非函数指针：动画对象内存布局稳定、可比较、测试可枚举。
enum class Easing{

	Linear,		///< 线性：t
	EaseIn,		///< 慢→快：t²
	EaseOut,	///< 快→慢：1-(1-t)²
	EaseInOut	///< 慢→快→慢：对称组合

};

/// @brief 应用缓动曲线（纯函数；t ∈ [0,1] → eased ∈ [0,1]）
/// @param easing 缓动类型
/// @param t 归一化进度（调用方负责 clamp）
float ApplyEasing(Easing easing, float t) noexcept;

}

