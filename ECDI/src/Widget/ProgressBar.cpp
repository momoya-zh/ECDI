#include "ECDI/Widget/ProgressBar.h"

#include "ECDI/Animation/AnimationManager.h"
#include "ECDI/Animation/Easing.h"
#include "ECDI/Theme/DefaultTheme.h"
#include "ECDI/Window/Window.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace ECDI{

ProgressBar::ProgressBar(){
	// Widget 构造期虚函数静态派发——必须在此重新调用以覆盖 ProgressBar::ApplyTheme
	//（同 Button/CollapsiblePanel 构造先例）
	ApplyTheme(GetDefaultTheme());
}

// ── 值（双层 API；状态分离——逻辑状态与呈现状态互不越界）──────

void ProgressBar::SetProgress(float progress){

	// clamp [0,1]（目标值域保证——呈现值经插值天然保持在合法区间内）
	const float target = (std::clamp)(progress, 0.0f, 1.0f);

	// no-op 判断键恒为 target ↔ m_progress（目标 vs 目标）——绝不用 m_displayProgress 判断：
	// 动画进行中它是中间态，拿它比较会把「目标未变」误判成「需要重启」
	if (std::fabs(target - m_progress) < kProgressEpsilon){
		return;
	}

	m_progress = target;   // 逻辑状态归 SetProgress（动画不产生状态——AnimateTo 只消费目标）

	if (AnimationManager* manager = ResolveAnimationManager()){
		AnimateTo(target);   // 有宿主：平滑过渡（from = 当前呈现值——替换式重启语义）
	}
	else{
		m_displayProgress = target;   // 无宿主（测试树/构造期）：瞬时到位
	}

	Invalidate();   // 动画首帧前的重绘请求（首帧前一帧延迟可接受；聚合契约接管后续帧）
}

void ProgressBar::SetPercent(int percent){
	SetProgress(static_cast<float>((std::clamp)(percent, 0, 100)) / 100.0f);
}

// ── 主题与样式（D7——Apply 只更新未 Override 属性）────────────

void ProgressBar::ApplyTheme(const Theme& theme){

	ProgressBarStyle defaults = theme.GetProgressBarStyle();
	m_style.trackColor.Apply(defaults.trackColor.value);
	m_style.fillColor.Apply(defaults.fillColor.value);
	m_style.cornerRadius.Apply(defaults.cornerRadius.value);

	Invalidate();   // 颜色/圆角即时生效——颜色不参与动画，无呈现值同步、无 Cancel 逻辑（§2.6）
}

void ProgressBar::SetStyle(ProgressBarStyleOverride override){

	if (override.trackColor)    m_style.trackColor.Set(*override.trackColor);
	if (override.fillColor)     m_style.fillColor.Set(*override.fillColor);
	if (override.cornerRadius)  m_style.cornerRadius.Set(*override.cornerRadius);

	Invalidate();
}

// ── 动画（消费 per-Window AnimationManager——只消费，不触碰）──

void ProgressBar::AnimateTo(float target){

	// ResolveAnimationManager 非空调用方保证（SetProgress 分支前置）——本方法不做 nullptr 分支
	ResolveAnimationManager()->Start(
		m_animToken,
		m_displayProgress,                       // from = 当前呈现值（替换式重启语义兑现点——动画中再设无跳变）
		target,
		std::chrono::milliseconds(kProgressTransitionMs),
		Easing::EaseOut,
		[this](float v){ m_displayProgress = v; });   // onValue 只写呈现值；不显式 Invalidate（Tick 聚合契约）
	                                            // 无 onFinished（RAII token；完成帧 onValue(final) 必达）
}

AnimationManager* ProgressBar::ResolveAnimationManager() const noexcept{
	// 运行期唯一路径（与 Button::AnimateBackgroundTo / CollapsiblePanel::SetExpanded 同构）——
	// 测试派生类 override 本方法注入替身（详设 §2.2 接缝，一次性例外）
	const Window* window = GetWindow();
	return window ? &const_cast<Window*>(window)->GetAnimationManager() : nullptr;
}

// ── 绘制（命令流 = PushClip → 轨道 → 填充 → PopClip；恒 4 条）──

void ProgressBar::OnPaint(PaintContext& ctx, int x, int y){

	const Rect bounds{ static_cast<float>(x), static_cast<float>(y),
	                   static_cast<float>(GetWidth()), static_cast<float>(GetHeight()) };

	// 圆角：绘制时计算（不存储——SetSize 自动跟随；0 高 → effective 0 → 降级 DrawRect）
	const float configured = m_style.cornerRadius.value;
	const float effectiveRadius = (configured > 0.0f) ? configured
	                                                  : static_cast<float>(GetHeight()) / 2.0f;

	// ① 轨道（全尺寸；圆角 > 0 走 DrawRoundedRect，= 0 降级 DrawRect——Button 同款分支）
	if (effectiveRadius > 0.0f){
		ctx.DrawRoundedRect(bounds, effectiveRadius, m_style.trackColor.value);
	}
	else{
		ctx.DrawRect(bounds, m_style.trackColor.value);
	}

	// ② 填充（圆角矩形——详设 v1.3 方案 D：与轨道同心 effectiveRadius，视觉无缝）
	//    0 进度 → 宽 0（命令仍发出——测试锚定该语义；后端空宽 no-op 无可见输出）
	//    低进度 fillWidth < 2×radius → 后端半径钳制自动缩圆角（DrawRoundedRect 契约）
	const float fillWidth = static_cast<float>(GetWidth()) * m_displayProgress;
	ctx.DrawRoundedRect(Rect{ bounds.x, bounds.y, fillWidth, bounds.height },
	                    effectiveRadius, m_style.fillColor.value);

}

}