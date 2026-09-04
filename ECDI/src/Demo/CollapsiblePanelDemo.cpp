#include "CollapsiblePanelDemo.h"

#include "ECDI/Animation/AnimationManager.h"
#include "ECDI/Layout/VerticalLayout.h"
#include "ECDI/Widget/Button.h"
#include "ECDI/Widget/Label.h"
#include "ECDI/Window/Window.h"

#include <chrono>
#include <memory>
#include <utility>

namespace ECDI{

namespace Demo{

CollapsiblePanelDemo::CollapsiblePanelDemo(){

	// 垂直排列：标题按钮 + 内容区（高度动画期间 Layout 联动——S2 验证核心）
	SetLayout(std::make_unique<VerticalLayout>());

	auto toggle = std::make_unique<Button>("Collapse");

	// ⚠️ 必须显式 SetSize——VerticalLayout 只排位置不覆盖尺寸，无 SetSize 高度 0 会叠在 y=0
	//（2026-08-29 demo 接入 main 时踩坑：按钮 0×0 → 点击无命中 → 面板"点没反应"）
	toggle->SetSize(kContentWidth, 36);

	m_toggle = toggle.get();

	m_toggle->SetOnClick([this]{ Toggle(); });

	AddChild(std::move(toggle));

	auto content = std::make_unique<Label>("Demo content area");

	content->SetSize(kContentWidth, 40);

	AddChild(std::move(content));

	SetSize(kContentWidth, kExpandedHeight);

}

void CollapsiblePanelDemo::Toggle(){

	Window* window = GetWindow();

	if (window == nullptr){

		return;   // demo 需挂窗口（动画 manager 在 Window 上）——无窗口忽略

	}

	// 状态先行（边界原则：动画不产生状态——折叠状态在触发点翻转，动画只做表现层过渡）
	const bool collapsing = m_expanded;

	m_expanded = !m_expanded;

	const int targetHeight = collapsing ? kHeaderHeight : kExpandedHeight;

	// d7：from = 当前呈现高度（GetHeight()）——替换式重启语义兑现点（动画中途再点无跳变）；
	// d8：onFinished 做收尾表现（按钮文案）
	window->GetAnimationManager().Start(
		m_heightToken,
		static_cast<float>(GetHeight()),
		static_cast<float>(targetHeight),
		std::chrono::milliseconds(kToggleDurationMs),
		collapsing ? Easing::EaseIn : Easing::EaseOut,
		[this](float height){
			SetSize(GetWidth(), static_cast<int>(height));   // Geometry 改变
			Arrange();                                       // Layout 联动（子控件重排）
			Invalidate();                                    // 重绘请求（每 tick manager 亦聚合一次）
		},
		[this, collapsing]{
			m_toggle->SetText(collapsing ? "Expand" : "Collapse");

			Invalidate();
		});

}

}

}

