#pragma once

#include "ECDI/Animation/AnimationToken.h"
#include "ECDI/Theme/RingProgressBarStyle.h"
#include "ECDI/Widget/Widget.h"

namespace ECDI{

class AnimationManager;   // 前置声明（ResolveAnimationManager 返回指针）
class Theme;              // 前置声明（ApplyTheme 参数）

/// @brief 环形 determinate 进度控件（Phase 33——框架首个曲线原语的第一个控件消费者）
/// @details 继承 Widget **不继承 ProgressBar**（K14：后者的 OnPaint 是水平语义，继承会带上水平约束）。
///          状态分离同 ProgressBar：m_progress = 目标（逻辑）/ m_displayProgress = 呈现（动画驱动）；
///          动画不产生状态：SetProgress 只改目标并启动过渡，OnPaint 只读呈现值（C33-10①②）。
///          尺寸策略（C33-10③）：min(width, height) 作直径、圆心居中——宽高不等时不拉伸、不报错。
class RingProgressBar: public Widget{

public:

	RingProgressBar();   // 默认构造——构造体注入 RingProgressBarStyle（尺寸由使用者 SetSize）

	// ── 值（双层 API；C33-10①②）────────────────────

	/// @brief 设置目标进度（clamp [0,1]；**NaN ⇒ 拒绝**：值不变且不启动动画；同目标 no-op）
	/// @details ★ NaN 检查**先于 clamp**——std::clamp(NaN, ...) 的结果是实现定义的传播，必须先拒（C33-10①）。
	///          逻辑状态归本方法；呈现经动画趋近。无动画宿主（ResolveAnimationManager()==nullptr）→ 瞬时到位。
	void SetProgress(float progress);

	/// @brief 设置目标进度（百分比；clamp [0,100]——SetProgress(p/100.0f) 包装）
	void SetPercent(int percent);

	/// @brief 目标进度（逻辑状态只读查询——呈现值不对外，测试经派生类访问）
	[[nodiscard]] float GetProgress() const noexcept{ return m_progress; }

	// ── 主题与样式（D7 契约，同 ProgressBar 形态）─────────

	/// @brief 应用主题（RingProgressBarStyle 注入——按 ProgressBar 先例自行声明）
	void ApplyTheme(const Theme& theme);

	/// @brief 样式运行时覆盖（颜色/环宽即时生效——颜色不参与动画，无 Cancel 逻辑）
	void SetStyle(RingProgressBarStyleOverride override);

protected:

	void OnPaint(PaintContext& ctx, int x, int y) override;

	/// @brief 解析动画宿主（测试接缝——nullptr = 瞬时降级；默认经 GetWindow()；沿 ProgressBar 先例）
	virtual AnimationManager* ResolveAnimationManager() const noexcept;

	/// @brief 呈现值（视觉状态——OnPaint 唯一消费；protected——测试派生类可访问）
	float m_displayProgress = 0.0f;

	/// @brief RingProgressBar 专属样式（protected——测试派生类可访问，同 ProgressBar::m_style 先例）
	RingProgressBarStyle m_style;

private:

	/// @brief 启动到 target 的过渡动画（只启动动画、不改逻辑状态——m_progress 赋值归 SetProgress）
	void AnimateTo(float target);

	static constexpr int kTransitionMs = 200;               ///< 过渡时长（沿 B24 的 200ms 同族值）
	static constexpr float kEpsilon = 1e-6f;                ///< 同目标 no-op 判定阈（目标 vs 目标）

	float m_progress = 0.0f;                ///< 目标值（逻辑状态）
	AnimationToken m_animToken;             ///< 进度动画令牌（RAII；析构自动标脏——弱引用保护）

};

}
