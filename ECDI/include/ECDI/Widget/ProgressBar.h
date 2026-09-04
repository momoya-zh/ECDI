#pragma once

#include "ECDI/Animation/AnimationToken.h"
#include "ECDI/Theme/ProgressBarStyle.h"
#include "ECDI/Widget/Widget.h"

namespace ECDI{

class AnimationManager;   // 前置声明（ResolveAnimationManager 返回指针）
class Theme;              // 前置声明（ApplyTheme 参数）

/// @brief 水平 determinate 进度条（9.6 第二动画消费者——纯视觉控件）
/// @details 继承 Widget（不继承 TextWidget/Panel——零文本、无子容器语义）。
/// 状态分离：m_progress = 目标（逻辑）/ m_displayProgress = 呈现（视觉，动画驱动）。
/// 动画不产生状态：SetProgress 只改目标并启动过渡；OnPaint 只读呈现值。
class ProgressBar: public Widget{

public:

	ProgressBar();   // 默认构造——构造体注入 ProgressBarStyle（尺寸由使用者 SetSize）

	// ── 值（双层 API）────────────────────────────────

	/// @brief 设置目标进度（clamp [0,1]；默认平滑过渡；同目标 no-op）
	/// @details 逻辑状态归本方法；呈现经动画趋近。无动画宿主（ResolveAnimationManager()==nullptr）→ 瞬时到位。
	void SetProgress(float progress);

	/// @brief 设置目标进度（百分比；clamp [0,100]——SetProgress(p/100.0f) 包装）
	void SetPercent(int percent);

	/// @brief 目标进度（逻辑状态只读查询——呈现值不对外，测试经派生类访问）
	[[nodiscard]] float GetProgress() const noexcept{ return m_progress; }

	// ── Phase 9：主题与样式（D7 契约）────────────────

	/// @brief 应用主题（ProgressBarStyle 注入——Widget 层无此虚函数（在 TextWidget），按 Panel 先例自行声明）
	void ApplyTheme(const Theme& theme);

	/// @brief 样式运行时覆盖（颜色/圆角即时生效——颜色不参与动画，无 Cancel 逻辑）
	void SetStyle(ProgressBarStyleOverride override);

protected:

	void OnPaint(PaintContext& ctx, int x, int y) override;

	/// @brief 解析动画宿主（测试接缝——nullptr = 瞬时降级；默认经 GetWindow()）
	/// @details 一次性例外（详设 §2.2）：ProgressBar 的被测命题 = 动画中间态，无窗口不可观测——
	/// 测试派生类 override 返回替身 manager。**不得扩散到其他动画控件**（默认无窗口降级即够）。
	virtual AnimationManager* ResolveAnimationManager() const noexcept;

	/// @brief 呈现值（视觉状态——OnPaint 唯一消费；protected——测试派生类可访问，同 TextBox::m_style 先例）
	float m_displayProgress = 0.0f;

	/// @brief ProgressBar 专属样式（protected——测试派生类可访问，同 TextBox/Button/Panel 先例）
	ProgressBarStyle m_style;

private:

	/// @brief 启动到 target 的过渡动画（只启动动画、不改逻辑状态——m_progress 赋值归 SetProgress）
	void AnimateTo(float target);

	static constexpr int kProgressTransitionMs = 200;       ///< 过渡时长（内部常量，不开放）
	static constexpr float kProgressEpsilon = 1e-6f;        ///< 同目标 no-op 判定阈（目标 vs 目标）

	float m_progress = 0.0f;                ///< 目标值（逻辑状态）
	AnimationToken m_animToken;             ///< 进度动画令牌（RAII；析构自动标脏——弱引用保护）

};

}
