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

		// ★ 审计 M7 修复：进入展开跃迁**前**重取锚定边。
		//   此处取的是「当前几何」（v1.3 意图：Layout 排位后以当前为准）；收起态下
		//   锚定边 = 该方向的固定边，与 s 无关 ⇒ 取一次即恒定。
		CacheAnchorFromCurrent();

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

		// ★ 审计 M7 修复：进入折叠跃迁**前**重取锚定边（此刻几何是展开态 ⇒ 锚定边正确）
		CacheAnchorFromCurrent();

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

void CollapsiblePanel::SetExpandDirection(ExpandDirection dir) noexcept{

	if (m_direction == dir){

		return;

	}

	m_direction = dir;

	// ★ 审计 M7 修复：方向改变 ⇒ **锚定边含义随之改变**（Up 锚底边 / Left 锚右边 /
	//   其余锚左上角）。展开基准已建立时按新方向**重取锚点 + 重呈当前状态几何**——
	//   否则「先 SetSize（旧方向）后 SetExpandDirection（新方向）」这一序列（在头文件
	//   初始化约定之外、但 API 允许）会留下旧方向的形状，与锚点反推所用的轴不一致。
	//   ⚠️ 头文件声明的正常路径（SetExpandDirection → SetPosition → SetSize）抵达此处时
	//      `m_anchorValid == false` ⇒ 整块跳过，**零行为变化**。
	//   ⚠️ 用「当前状态对应的轴值」重呈：收起态 = 0；展开态 = AxisTarget()（对 Up/Down
	//      两者都还原到基准左上角 ⇒ 展开态切方向不移动面板）。
	if (m_anchorValid){

		CacheAnchorFromBasis();

		ApplyGeometry(m_expanded ? AxisTarget() : 0.0f);

	}

}

void CollapsiblePanel::Toggle(){

	SetExpanded(!m_expanded);

}

void CollapsiblePanel::SetSize(int w, int h){

	if (!m_expanded){
		// ★ Phase 41 雷区预警（**不修**——属 #80）：本分支把传入的 (w,h) 写进 m_expandedRect
		//   作为「展开基准」，而此时呈现几何往往还是 0；`VerticalLayout.cpp:96` 的 fillCrossAxis
		//   分支会调 `child->SetSize(cross, child->GetHeight())` ⇒ 收起态 GetHeight()==0
		//   ⇒ **基准高被抹成 0 ⇒ 此后永远展不开**。当前无实例（examples 均在无 Layout 容器里），
		//   修属 #80（动画驱动的连续几何）。
		// 收起态语义（2026-08-30 v1.2，phase9.6-panel-container-semantics v1.1 硬契约）：
		// SetSize 定义「展开基准」，实际呈现保持收缩——非动画轴保持给出值、动画轴呈现 0
		// （Down/Up→宽=w 高=0；Left/Right→高=h 宽=0）。位置取当前 GetX()/GetY()，
		// 故初始化顺序约定：SetExpandDirection → SetPosition → SetSize。
		m_expandedRect = Rect{static_cast<float>(GetX()), static_cast<float>(GetY()), static_cast<float>(w), static_cast<float>(h)};

		// ★ 审计 M7 修复：锚定边从**刚定义的展开基准**取（不是从当前呈现几何取）。
		//   此刻呈现还是「未 SetSize 前」的状态（GetWidth/GetHeight 多为 0），
		//   而锚定边的语义是「展开基准的那条边」——对 Up 就是 `y + h`（h = 展开高）。
		//   ⚠️ 顺序不可换：ApplyGeometry(0) 会改写位置，之后取就取到被污染的值。
		CacheAnchorFromBasis();

		ApplyGeometry(0.0f);

		return;
	}

	// 展开态改尺寸 = 重定展开基准（冻结点 2）：基准与锚定边同时更新
	Panel::SetSize(w, h);

	if (m_content){

		m_content->SetSize(w, h);

	}

	// ★ 审计 M7：展开态 SetSize 后位置未变 ⇒ 锚定边直接从当前几何取即可
	//   （此时 GetWidth/GetHeight 已是新尺寸，与 m_expandedRect 一致）
	CacheAnchorFromCurrent();

}

// ── 审计 M7 修复：锚定边缓存 ────────────────────────────────────────────────
// 语义 = 「锚定边此刻在哪」。锚定边按方向定义（与 ApplyGeometry 的推导式一一对应）：
//   Down  → 锚**上边** + 左边
//   Up    → 锚**底边** + 左边
//   Right → 锚**左边** + 上边
//   Left  → 锚**右边** + 上边
//
// ★★ 为什么必须缓存一次、而不是每帧从 GetX()/GetY() 现取：
//   `ApplyGeometry` 的公式是 **位置 = f(锚定边, s)**（纯函数）。原实现写成
//   「当前位置 + 增量」（`cy + (h0 - s)` / `cx + (w0 - s)`），而当前位置正是**上一次自己写过的值**
//   ⇒ 每帧把增量再叠一次 ⇒ **无界漂移**。Down/Right 未暴露只因它们的位置与 s 无关（增量为 0）。
//   实测（Python 复现审计 M7）：Up 方向 `s` 由 0 增到 400 时 `y` 从 600 漂到 2000、
//   底边从 600 漂到 2400。
//
// ★★ 为什么缓存「锚定边」而不是缓存「位置」：锚定边在 s 变化过程中**恒不变**——
//   这是本控件唯一的运动学不变量（Up: `y + h ≡ 锚定底边`；Left: `x + w ≡ 锚定右边`；
//   Down: `y ≡ 锚定上边`；Right: `x ≡ 锚定左边`）。⇒ 缓存它既不会读到自己的写入，
//   又能在**动画中途重入**（替换式重启）时依然给出正确基准。
//
// ★★ 为什么保留 v1.3 的「以当前几何为准」：v1.3 的原意是「Layout 容器排位后，
//   当前位置 ≠ SetSize 时刻的位置，须以当前几何锚定边才真正固定，否则展开会把面板
//   拉回陈旧位置、覆盖其他控件」。本修复**完整保留**该意图——`CacheAnchorFromCurrent`
//   仍从当前几何取值，只是把它从「每帧取」收成「跃迁前取一次」。

/// @brief 由**展开基准**（`m_expandedRect`）推导锚定边
/// @details 用于「首次 SetSize」这一唯一时刻：那时当前呈现几何尚未建立
///          （`GetWidth/GetHeight` 多为 0），而基准刚刚由调用方给出 ⇒ 只有基准能给出
///          正确的锚定边（Up 的底边 = `basis.y + basis.height`）。
void CollapsiblePanel::CacheAnchorFromBasis() noexcept{

	switch (m_direction){

	case ExpandDirection::Down:
		m_anchorX = static_cast<int>(m_expandedRect.x);
		m_anchorY = static_cast<int>(m_expandedRect.y);
		break;

	case ExpandDirection::Up:
		m_anchorX = static_cast<int>(m_expandedRect.x);
		m_anchorY = static_cast<int>(m_expandedRect.y + m_expandedRect.height);
		break;

	case ExpandDirection::Right:
		m_anchorX = static_cast<int>(m_expandedRect.x);
		m_anchorY = static_cast<int>(m_expandedRect.y);
		break;

	case ExpandDirection::Left:
		m_anchorX = static_cast<int>(m_expandedRect.x + m_expandedRect.width);
		m_anchorY = static_cast<int>(m_expandedRect.y);
		break;

	}

	m_anchorValid = true;

}

void CollapsiblePanel::CacheAnchorFromCurrent() noexcept{

	const int x = GetX();
	const int y = GetY();
	const int w = GetWidth();
	const int h = GetHeight();

	switch (m_direction){

	case ExpandDirection::Down:    // 上边 / 左边固定
		m_anchorX = x;
		m_anchorY = y;
		break;

	case ExpandDirection::Up:      // 底边固定 / 左边固定
		m_anchorX = x;
		m_anchorY = y + h;
		break;

	case ExpandDirection::Right:   // 左边 / 上边固定
		m_anchorX = x;
		m_anchorY = y;
		break;

	case ExpandDirection::Left:    // 右边固定 / 上边固定
		m_anchorX = x + w;
		m_anchorY = y;
		break;

	}

	m_anchorValid = true;

}

void CollapsiblePanel::ApplyGeometry(float s){

	const float w0 = m_expandedRect.width;

	const float h0 = m_expandedRect.height;

	// ★ 审计 M7 修复：位置是 s 的**纯函数**（由缓存锚定边 + s 整值推导）。
	//   原实现读 `GetX()/GetY()`（= 上一次自己写过的位置）再叠加 `h0 - s` / `w0 - s`，
	//   对 Down/Right（位置与 s 无关）无害，对 **Up/Left 逐帧累加 ⇒ 无界漂移**
	//   （实测 s=0→400 时 y 从 600 漂到 2000、底边从 600 漂到 2400）。
	//   ⇒ 锚定边在 s 变化过程中恒不动，故「位置 = 锚定边 − s」（Up/Left）
	//     或「位置 = 锚定边」（Down/Right）——永不读自身先前写入。
	// ⚠️ 锚点必须已建立：唯一未建立的入口是「从未 SetSize 过」（w0/h0 皆为 0），
	//   此时任何位置推导都没有意义——惰性补建一次，行为与首帧一致。
	if (!m_anchorValid){

		CacheAnchorFromCurrent();

	}

	const int cx = m_anchorX;
	const int cy = m_anchorY;

	// 2026-08-30 v1.2：SetSize 直调 Panel::SetSize + 容器同步（不走 override 虚分派）——
	// 否则收起态 SetSize override 会以 (w,0) 重定义 m_expandedRect（递归 + 基准污染）
	switch (m_direction){

	case ExpandDirection::Down:   // top 锚定，位置不动
		SetPosition(cx, cy);
		Panel::SetSize(static_cast<int>(w0), static_cast<int>(s));
		break;

	case ExpandDirection::Up:     // bottom 锚定：y = 锚定底边 − s
		SetPosition(cx, cy - static_cast<int>(s));
		Panel::SetSize(static_cast<int>(w0), static_cast<int>(s));
		break;

	case ExpandDirection::Right:  // left 锚定，位置不动
		SetPosition(cx, cy);
		Panel::SetSize(static_cast<int>(s), static_cast<int>(h0));
		break;

	case ExpandDirection::Left:   // right 锚定：x = 锚定右边 − s
		SetPosition(cx - static_cast<int>(s), cy);
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
