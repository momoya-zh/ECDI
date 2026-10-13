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

/// @brief 应用缓动曲线（纯函数）
/// @param easing 缓动类型
/// @param t 归一化进度
/// @return eased 值，**恒 ∈ [0,1]**（本函数自行 clamp —— 审计 L4 修复）
/// @details ★★ **契约变更（审计 L4，2026-10-13）**：修复前本函数**不 clamp**，
///          而头注释又承诺 `t ∈ [0,1] → eased ∈ [0,1]` ⇒ `EaseIn(2) == 4` 这类
///          越界**静默破坏承诺**（调用方若依赖该承诺做几何插值，会得到 >100% 的值）。
///          ⇒ 现由**函数自身强制值域**（`std::clamp(t, 0, 1)`）。
///          ★ **零行为变化**：生产路径的 t 恒在 [0,1]
///          （`Detail::NormalizeProgress` 已 clamp，见 `Animation.h:49`）⇒ 既有调用逐位无影响。
///          ★ 新增的 `default` 分支对未知枚举 `FRAMEWORK_ASSERT(false)`（不再静默回落 Linear
///          ——那会掩盖未来枚举，同 `EventRouter` M21 的穷尽性纪律）。
float ApplyEasing(Easing easing, float t) noexcept;

}

