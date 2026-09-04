#pragma once

#include "ECDI/Animation/Easing.h"
#include "ECDI/Core/Color.h"

#include <algorithm>
#include <chrono>
#include <functional>

namespace ECDI{

/// @brief 动画基类（9.6；d6 值回调式——Animation 是纯「时间→值」计算器，不认识目标）
/// @details 契约：
/// - Advance 每次调用必触发 onValue（含到达终点的最后一帧——终值必须经过 onValue 应用到目标）
/// - 返回 true = 到达终点（onValue(final) 已执行）；Manager 负责随后调 onFinished 并移除
class Animation{
public:

	virtual ~Animation() = default;

	/// @brief 推进 elapsed 毫秒并应用当前插值
	/// @param elapsed 距上次推进的真实时长（Tick(elapsed) 参数化——manager 不持时钟）
	/// @return true = 到达终点（终值已经 onValue 应用）
	virtual bool Advance(std::chrono::milliseconds elapsed) = 0;

};

namespace Detail{

/// @brief 归一化进度计算（Float/Color 共用；elapsed 累加 → t = clamp(elapsed/duration)）
inline float NormalizeProgress(
	std::chrono::milliseconds& elapsedAccumulated,
	std::chrono::milliseconds elapsed,
	std::chrono::milliseconds duration) noexcept
{
	using namespace std::chrono;

	elapsedAccumulated += elapsed;

	if (duration <= milliseconds::zero()){

		return 1.0f;   // 零时长 = 立即完成（终值直达）

	}

	const double t = static_cast<double>(elapsedAccumulated.count())
		/ static_cast<double>(duration.count());

	return static_cast<float>(std::clamp(t, 0.0, 1.0));

}

}

/// @brief float 值动画（S2 展开高度等）
class FloatAnimation final: public Animation{
public:

	using ValueCallback = std::function<void(float)>;

	FloatAnimation(float from, float to,
	               std::chrono::milliseconds duration, Easing easing,
	               ValueCallback onValue)
		: m_from(from), m_to(to)
		, m_duration(duration), m_easing(easing)
		, m_onValue(std::move(onValue)){}

	bool Advance(std::chrono::milliseconds elapsed) override{

		const float t = Detail::NormalizeProgress(m_elapsed, elapsed, m_duration);

		const float eased = ApplyEasing(m_easing, t);

		m_onValue(m_from + (m_to - m_from) * eased);

		return t >= 1.0f;

	}

private:

	float m_from;						///< 起始值（替换式重启下 = 调用方传的当前呈现值）
	float m_to;							///< 目标值

	std::chrono::milliseconds m_duration;	///< 总时长（真实 elapsed 累加对比，非 tick 计数）
	Easing m_easing;					///< 缓动曲线

	std::chrono::milliseconds m_elapsed{0};	///< 已累计推进时长

	ValueCallback m_onValue;			///< 值回调（每 tick 调用；持有者自写属性）

};

/// @brief Color 值动画（S1 Button 状态色过渡；RGBA 四通道各自 lerp）
class ColorAnimation final: public Animation{
public:

	using ValueCallback = std::function<void(const Color&)>;

	ColorAnimation(Color from, Color to,
	               std::chrono::milliseconds duration, Easing easing,
	               ValueCallback onValue)
		: m_from(from), m_to(to)
		, m_duration(duration), m_easing(easing)
		, m_onValue(std::move(onValue)){}

	bool Advance(std::chrono::milliseconds elapsed) override{

		const float t = Detail::NormalizeProgress(m_elapsed, elapsed, m_duration);

		const float eased = ApplyEasing(m_easing, t);

		Color value;

		value.r = m_from.r + (m_to.r - m_from.r) * eased;
		value.g = m_from.g + (m_to.g - m_from.g) * eased;
		value.b = m_from.b + (m_to.b - m_from.b) * eased;
		value.a = m_from.a + (m_to.a - m_from.a) * eased;

		m_onValue(value);

		return t >= 1.0f;

	}

private:

	Color m_from;						///< 起始色（调用方传的当前呈现色）
	Color m_to;							///< 目标色

	std::chrono::milliseconds m_duration;	///< 总时长
	Easing m_easing;					///< 缓动曲线

	std::chrono::milliseconds m_elapsed{0};	///< 已累计推进时长

	ValueCallback m_onValue;			///< 值回调

};

}

