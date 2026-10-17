#include "ECDI/Widget/RingProgressBar.h"

#include "ECDI/Animation/AnimationManager.h"
#include "ECDI/Animation/Easing.h"
#include "ECDI/Theme/DefaultTheme.h"
#include "ECDI/Window/Window.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace ECDI{

RingProgressBar::RingProgressBar(){
	// Widget 构造期虚函数静态派发——必须在此重新调用以覆盖 RingProgressBar::ApplyTheme
	//（同 ProgressBar/Button/CollapsiblePanel 构造先例）
	ApplyTheme(GetDefaultTheme());
}

// ── 值（双层 API；C33-10①②）────────────────────────────

void RingProgressBar::SetProgress(float progress){

	// ★ C33-10①：NaN ⇒ 拒绝（值不变且不启动动画）——**先于 clamp**
	//（std::clamp(NaN, ...) 的结果是实现定义的传播，必须先拒，杜绝未定义状态）
	if (!std::isfinite(progress)){
		return;
	}

	// clamp [0,1]（沿 ProgressBar 先例；呈现值经插值天然保持在合法区间内）
	const float target = (std::clamp)(progress, 0.0f, 1.0f);

	// no-op 判断键恒为 target ↔ m_progress（目标 vs 目标）——绝不用 m_displayProgress 判断：
	// 动画进行中它是中间态，拿它比较会把「目标未变」误判成「需要重启」（ProgressBar 先例注释同源）
	if (std::fabs(target - m_progress) < kEpsilon){
		return;
	}

	m_progress = target;   // 逻辑状态归 SetProgress（动画不产生状态——AnimateTo 只消费目标）

	if (AnimationManager* manager = ResolveAnimationManager()){
		AnimateTo(target);   // 有宿主：平滑过渡（from = 当前呈现值——替换式重启语义）
	}
	else{
		m_displayProgress = target;   // 无宿主（测试树/构造期）：瞬时到位
	}

	Invalidate();   // 动画首帧前的重绘请求（聚合契约接管后续帧——沿 ProgressBar 先例）
}

void RingProgressBar::SetPercent(int percent){
	SetProgress(static_cast<float>((std::clamp)(percent, 0, 100)) / 100.0f);
}

// ── 主题与样式（D7——Apply 只更新未 Override 属性）────────

void RingProgressBar::ApplyTheme(const Theme& theme){

	RingProgressBarStyle defaults = theme.GetRingProgressBarStyle();
	m_style.trackColor.Apply(defaults.trackColor.value);
	m_style.fillColor.Apply(defaults.fillColor.value);
	m_style.thickness.Apply(defaults.thickness.value);

	Invalidate();   // 颜色/环宽即时生效——颜色不参与动画，无 Cancel 逻辑（§2.6 同族）
}

void RingProgressBar::SetStyle(RingProgressBarStyleOverride override){

	if (override.trackColor)  m_style.trackColor.Set(*override.trackColor);
	if (override.fillColor)   m_style.fillColor.Set(*override.fillColor);
	if (override.thickness)   m_style.thickness.Set(*override.thickness);

	Invalidate();
}

// ── 动画（消费 per-Window AnimationManager——只消费，不触碰）──

void RingProgressBar::AnimateTo(float target){

	// ResolveAnimationManager 非空调用方保证（SetProgress 分支前置）——本方法不做 nullptr 分支
	ResolveAnimationManager()->Start(
		m_animToken,
		m_displayProgress,                       // from = 当前呈现值（替换式重启语义兑现点）
		target,
		std::chrono::milliseconds(kTransitionMs),
		Easing::EaseOut,
		[this](float v){ m_displayProgress = v; });   // onValue 只写呈现值（Tick 聚合契约）
}

AnimationManager* RingProgressBar::ResolveAnimationManager() const noexcept{
	// 运行期唯一路径（与 ProgressBar 同构）——测试派生类 override 本方法注入替身
	const Window* window = GetWindow();
	return window ? &const_cast<Window*>(window)->GetAnimationManager() : nullptr;
}

// ── 绘制（C33-10③ 尺寸策略；恒 2 条 DrawArc 命令）─────────

void RingProgressBar::OnPaint(PaintContext& ctx, int x, int y){

	// D <= 0 ⇒ 早退（连圆心都无意义——详设 △12 唯一允许的控件层早退）
	const float D = static_cast<float>((std::min)(GetWidth(), GetHeight()));
	if (D <= 0.0f){
		return;
	}

	// ★ 环中线半径：R = D/2 - thickness/2。**R <= 0 不在此早退**——两条命令照发，
	//   由 NormalizeArc 按 C33-4 判定（radius <= 0 ⇒ 不画）。理由：NormalizeArc 才是
	//   C33-4 的唯一实现，不在控件层复制退化规则（否则两处规则会漂移——C33-6 风险）。
	const float R = D * 0.5f - m_style.thickness.value * 0.5f;

	// 圆心居中（宽高不等时取 min 边——不拉伸、不报错，C33-10③）
	const float cx = static_cast<float>(x) + static_cast<float>(GetWidth()) * 0.5f;
	const float cy = static_cast<float>(y) + static_cast<float>(GetHeight()) * 0.5f;

	// ① 轨道：整环（sweep = 360；12 点钟起 = 270°——D7：起点朝向由控件层折算，原语保持中立）
	ctx.DrawArc(Point{ cx, cy }, R, 270.0f, 360.0f,
	            m_style.thickness.value, m_style.trackColor.value);

	// ② 进度：0 进度 ⇒ sweep = 0 ⇒ 后端不画（命令仍发——命令流可断言，沿 ProgressBar 先例）
	ctx.DrawArc(Point{ cx, cy }, R, 270.0f, m_displayProgress * 360.0f,
	            m_style.thickness.value, m_style.fillColor.value);

}

}
