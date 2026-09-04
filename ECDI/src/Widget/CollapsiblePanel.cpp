#include "ECDI/Widget/CollapsiblePanel.h"

#include "ECDI/Animation/AnimationManager.h"
#include "ECDI/Window/Window.h"

#include <chrono>
#include <memory>
#include <utility>

namespace ECDI{

CollapsiblePanel::CollapsiblePanel(){

	// 内容容器（裸 Widget——不画背景，避免与 Panel 背景叠加；树拥有所有权）
	auto content = std::make_unique<Widget>();

	m_content = content.get();

	AddChild(std::move(content));

	// 默认收起（2026-08-30 v1.2 变更）：内容初始隐藏；展开基准由首次收起态 SetSize 定义
	SetContentVisible(false);

}

void CollapsiblePanel::SetExpanded(bool expanded){

	// 幂等：同状态重复调用无副作用
	if (expanded == m_expanded){

		return;

	}

	Window* window = GetWindow();

	if (expanded){

		m_expanded = true;

		// 展开动画开始前显示内容（避免「空面板长高、内容突现」断层）
		SetContentVisible(true);

		Invalidate();

		if (window == nullptr){

			// 无窗口降级：瞬时展开（测试可测性——不依赖 AnimationManager/平台）
			ApplyGeometry(AxisTarget());

			Invalidate();

			return;

		}

		window->GetAnimationManager().Start(
			m_sizeToken,
			CurrentAxisValue(),
			AxisTarget(),
			std::chrono::milliseconds(kToggleDurationMs),
			Easing::EaseOut,
			[this](float s){
				ApplyGeometry(s);   // onValue 内不显式 Invalidate——manager Tick 聚合（d4 契约）
			});

	}
	else{

		// 展开基准记忆：仅「展开态 → 折叠」跃迁点记录（详细设计冻结点 2——
		// 动画中反复 Toggle 不会用中间尺寸污染基准）
		if (m_expanded){

			m_expandedRect = GetGeometry();

		}

		m_expanded = false;

		if (window == nullptr){

			// 无窗口降级：瞬时折叠
			ApplyGeometry(0.0f);

			SetContentVisible(false);

			Invalidate();

			return;

		}

		window->GetAnimationManager().Start(
			m_sizeToken,
			CurrentAxisValue(),
			0.0f,
			std::chrono::milliseconds(kToggleDurationMs),
			Easing::EaseIn,
			[this](float s){
				ApplyGeometry(s);
			},
			[this]{
				// 折叠动画结束后才隐藏内容——动画期间尺寸渐缩、父 Clip 天然裁切（视觉自然）
				SetContentVisible(false);

				Invalidate();
			});

	}

}

void CollapsiblePanel::Toggle(){

	SetExpanded(!m_expanded);

}

void CollapsiblePanel::SetSize(int w, int h){

	if (!m_expanded){
		// 收起态语义（2026-08-30 v1.2，phase9.6-panel-container-semantics v1.1 硬契约）：
		// SetSize 定义「展开基准」，实际呈现保持收缩——非动画轴保持给出值、动画轴呈现 0
		// （Down/Up→宽=w 高=0；Left/Right→高=h 宽=0）。位置取当前 GetX()/GetY()，
		// 故初始化顺序约定：SetExpandDirection → SetPosition → SetSize。
		m_expandedRect = Rect{static_cast<float>(GetX()), static_cast<float>(GetY()), static_cast<float>(w), static_cast<float>(h)};

		ApplyGeometry(0.0f);

		return;
	}

	// 冻结点 4：先面板、后容器（初始 SetSize / 动画 ApplyGeometry / 外部改尺寸三场景统一）
	Panel::SetSize(w, h);

	if (m_content){

		m_content->SetSize(w, h);

	}

}

void CollapsiblePanel::ApplyGeometry(float s){

	// 位置锚定参照 = 当前几何（2026-08-31 v1.3 变更：Layout 排位后当前位置 ≠ m_expandedRect 的
	// SetSize 时刻位置——绝对定位下两者相同；Layout 容器内以当前几何为准，锚定边才真正固定，
	// 否则展开会把面板拉回陈旧位置、覆盖其他控件）
	const int cx = GetX();
	const int cy = GetY();
	const float w0 = m_expandedRect.width;

	const float h0 = m_expandedRect.height;

	// 位置是尺寸轴值 s 的纯函数（锚定边固定不动——详细设计 §2.1）
	// 2026-08-30 v1.2：SetSize 直调 Panel::SetSize + 容器同步（不走 override 虚分派）——
	// 否则收起态 SetSize override 会以 (w,0) 重定义 m_expandedRect（递归 + 基准污染）
	switch (m_direction){

	case ExpandDirection::Down:   // top 锚定，位置不动
		SetPosition(cx, cy);
		Panel::SetSize(static_cast<int>(w0), static_cast<int>(s));
		break;

	case ExpandDirection::Up:     // bottom 锚定，y 随 s 推导
		SetPosition(cx, cy + static_cast<int>(h0 - s));
		Panel::SetSize(static_cast<int>(w0), static_cast<int>(s));
		break;

	case ExpandDirection::Right:  // left 锚定，位置不动
		SetPosition(cx, cy);
		Panel::SetSize(static_cast<int>(s), static_cast<int>(h0));
		break;

	case ExpandDirection::Left:   // right 锚定，x 随 s 推导
		SetPosition(cx + static_cast<int>(w0 - s), cy);
		Panel::SetSize(static_cast<int>(s), static_cast<int>(h0));
		break;

	}

	if (m_content){
		// 容器尺寸 = 展开基准的非动画轴 × 动画轴呈现值 s（与面板呈现一致）
		const bool vertical = (m_direction == ExpandDirection::Down || m_direction == ExpandDirection::Up);
		m_content->SetSize(vertical ? static_cast<int>(w0) : static_cast<int>(s),
		                   vertical ? static_cast<int>(s)  : static_cast<int>(h0));
	}

}

void CollapsiblePanel::SetContentVisible(bool visible) noexcept{

	if (m_content){

		m_content->SetVisible(visible);

	}

}

float CollapsiblePanel::CurrentAxisValue() const noexcept{

	return (m_direction == ExpandDirection::Down || m_direction == ExpandDirection::Up)
		? static_cast<float>(GetHeight())
		: static_cast<float>(GetWidth());

}

float CollapsiblePanel::AxisTarget() const noexcept{

	return (m_direction == ExpandDirection::Down || m_direction == ExpandDirection::Up)
		? m_expandedRect.height
		: m_expandedRect.width;

}

}
