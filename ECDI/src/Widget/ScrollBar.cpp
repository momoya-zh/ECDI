#include "ECDI/Widget/ScrollBar.h"

#include "ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonUpEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseMoveEvent.h"
#include "ECDI/Theme/DefaultTheme.h"

#include <algorithm>

namespace ECDI{

ScrollBar::ScrollBar(Orientation orientation)
	: m_orientation(orientation){

	// Widget 构造期虚分派是静态的——必须在此重新调用以覆盖 ScrollBar::ApplyTheme
	//（同 ProgressBar / Panel / Button 构造先例）
	ApplyTheme(GetDefaultTheme());

}

ScrollBar::~ScrollBar() = default;

// ── 范围模型（D11：由 ScrollView 单向驱动）────────────────────

void ScrollBar::SetRange(int contentExtent, int viewportExtent){

	m_contentExtent  = (std::max)(0, contentExtent);
	m_viewportExtent = (std::max)(0, viewportExtent);

	Invalidate();

}

int ScrollBar::GetMaxOffset() const noexcept{

	return (std::max)(0, m_contentExtent - m_viewportExtent);

}

void ScrollBar::SetOffset(int offset){

	const int o = (std::clamp)(offset, 0, GetMaxOffset());

	if (o == m_offset){

		return;   // 同值 no-op（避免容器↔条的无效往返）

	}

	m_offset = o;

	// ⚠️ 不调 NotifyOffset：本方法是**外部驱动**入口（容器调用），若回调会形成递归环
	Invalidate();

}

int ScrollBar::GetOffset() const noexcept{ return m_offset; }

ScrollBar::Orientation ScrollBar::GetOrientation() const noexcept{ return m_orientation; }

void ScrollBar::SetOnOffsetChanged(std::function<void(int)> callback){

	m_onOffsetChanged = std::move(callback);

}

// ── Phase 9：主题与样式（D7——Apply 只更新未 Override 属性）────

void ScrollBar::ApplyTheme(const Theme& theme){

	ScrollBarStyle defaults = theme.GetScrollBarStyle();
	m_style.trackColor.Apply(defaults.trackColor.value);
	m_style.thumbColor.Apply(defaults.thumbColor.value);
	m_style.thumbHoverColor.Apply(defaults.thumbHoverColor.value);
	m_style.thumbPressedColor.Apply(defaults.thumbPressedColor.value);
	m_style.cornerRadius.Apply(defaults.cornerRadius.value);
	m_style.thickness.Apply(defaults.thickness.value);

	Invalidate();

}

void ScrollBar::SetStyle(ScrollBarStyleOverride override){

	if (override.trackColor)        m_style.trackColor.Set(*override.trackColor);
	if (override.thumbColor)        m_style.thumbColor.Set(*override.thumbColor);
	if (override.thumbHoverColor)   m_style.thumbHoverColor.Set(*override.thumbHoverColor);
	if (override.thumbPressedColor) m_style.thumbPressedColor.Set(*override.thumbPressedColor);
	if (override.cornerRadius)      m_style.cornerRadius.Set(*override.cornerRadius);
	if (override.thickness)         m_style.thickness.Set(*override.thickness);

	Invalidate();

}

// ── 版式（范围模型——详设 §3.4；中间乘积一律 long long 防溢出）──

int ScrollBar::TrackLength() const noexcept{

	return (m_orientation == Orientation::Vertical) ? GetHeight() : GetWidth();

}

int ScrollBar::ThumbLength() const noexcept{

	const int track = TrackLength();

	if (track <= 0){

		return 0;

	}

	if (m_contentExtent <= 0){

		return track;   // 无内容：滑块占满（占满即"无可滚"的视觉表达）

	}

	// 比例映射：thumbLen = track × viewport / content（long long 承积——详设 §3.4）
	const long long len = static_cast<long long>(track) * m_viewportExtent
	                    / static_cast<long long>(m_contentExtent);

	// 下限 = min(kMinThumbLength, track)（轨道比最小滑块还短时不得越界）
	const int minLen = (std::min)(kMinThumbLength, track);

	return (std::clamp)(static_cast<int>(len), minLen, track);

}

int ScrollBar::ThumbStart() const noexcept{

	const int denom = TrackLength() - ThumbLength();

	if (denom <= 0){

		return 0;   // 除零保护（滑块占满 ⇒ 无需偏移映射）

	}

	const int maxOff = GetMaxOffset();

	if (maxOff <= 0){

		return 0;

	}

	return static_cast<int>(static_cast<long long>(denom) * m_offset / maxOff);

}

Rect ScrollBar::ThumbRectLocal() const noexcept{

	if (m_orientation == Orientation::Vertical){

		return Rect{ 0.0f,
		             static_cast<float>(ThumbStart()),
		             static_cast<float>(GetWidth()),
		             static_cast<float>(ThumbLength()) };

	}

	return Rect{ static_cast<float>(ThumbStart()),
	             0.0f,
	             static_cast<float>(ThumbLength()),
	             static_cast<float>(GetHeight()) };

}

int ScrollBar::OffsetFromThumbStart(int thumbStart) const noexcept{

	const int denom = TrackLength() - ThumbLength();

	if (denom <= 0){

		return 0;   // 除零保护

	}

	const int maxOff = GetMaxOffset();
	const int s = (std::clamp)(thumbStart, 0, denom);

	return static_cast<int>(static_cast<long long>(s) * maxOff / denom);

}

int ScrollBar::MainAxisPosFromClient(int clientX, int clientY) const noexcept{

	// ★ 坐标系换算（既有约定——TextBox.cpp:895）：事件坐标 = **窗口客户区绝对**，
	//   本控件内部一律用自身局部坐标（ThumbStart/TrackLength 同系）⇒ 先减绝对位置。
	//   GetAbsolutePosition 已是**视觉坐标**（Phase 15 含沿途内容偏移——详设 §3.2）。
	const Point abs = GetAbsolutePosition();

	const int localX = clientX - static_cast<int>(abs.x);
	const int localY = clientY - static_cast<int>(abs.y);

	return (m_orientation == Orientation::Vertical) ? localY : localX;

}

void ScrollBar::NotifyOffset(){

	if (m_onOffsetChanged){   // 可空——条可独立使用（D9）

		m_onOffsetChanged(m_offset);

	}

}

// ── 绘制（自绘 + 主题色；零新 RenderCommand）──────────────────

void ScrollBar::OnPaint(PaintContext& ctx, int x, int y){

	// 轨道：全条矩形
	ctx.DrawRect(Rect{ static_cast<float>(x),
	                   static_cast<float>(y),
	                   static_cast<float>(GetWidth()),
	                   static_cast<float>(GetHeight()) },
	             m_style.trackColor.value);

	// 滑块：相对矩形 + 最终坐标（三态色：拖拽 > hover > 常态）
	const Rect t = ThumbRectLocal();

	const Color thumb = m_dragging ? m_style.thumbPressedColor.value
	                  : m_hovered  ? m_style.thumbHoverColor.value
	                               : m_style.thumbColor.value;

	ctx.DrawRoundedRect(Rect{ static_cast<float>(x) + t.x,
	                          static_cast<float>(y) + t.y,
	                          t.width,
	                          t.height },
	                    m_style.cornerRadius.value,
	                    thumb);

}

// ── 交互三件套（详设 §3.4）──────────────────────────────────

void ScrollBar::OnMouseButtonDown(const MouseButtonDownEvent& event){

	const int pos   = MainAxisPosFromClient(event.GetMouseX(), event.GetMouseY());
	const int start = ThumbStart();
	const int len   = ThumbLength();

	if (pos >= start && pos < start + len){

		// ① 命中滑块：进入拖拽 + 记录抓取偏移（防拖拽瞬间跳动）
		m_dragging       = true;
		m_dragGrabOffset = pos - start;

	}
	else{

		// ② 命中轨道空白：翻页一屏（clamp 在 SetOffset 之外直接算，再交容器）
		const int delta = (pos < start) ? -m_viewportExtent : m_viewportExtent;
		const int next  = (std::clamp)(m_offset + delta, 0, GetMaxOffset());

		if (next != m_offset){

			m_offset = next;
			NotifyOffset();   // 用户操作 ⇒ 通知容器（唯一权威归一）

		}

		// 随后进入拖拽（连续感——同业界惯例）；抓取点取滑块中部
		m_dragging       = true;
		m_dragGrabOffset = (len > 0) ? (len / 2) : 0;

	}

	Invalidate();

}

void ScrollBar::OnMouseButtonUp(const MouseButtonUpEvent&){

	// Capture 由 Application 隐式接管（Down 捕获 / Up 释放）⇒ 拖出条外仍跟手
	if (m_dragging){

		m_dragging = false;
		Invalidate();

	}

}

void ScrollBar::OnMouseMove(const MouseMoveEvent& event){

	const int pos   = MainAxisPosFromClient(event.GetMouseX(), event.GetMouseY());
	const int start = ThumbStart();
	const int len   = ThumbLength();

	if (!m_dragging){

		// 仅更新 hover 视觉态（不影响偏移）
		const bool over = (pos >= start && pos < start + len);

		if (over != m_hovered){

			m_hovered = over;
			Invalidate();

		}

		return;

	}

	// 拖拽中：滑块起点 = 鼠标位置 − 抓取偏移 ⇒ 反推新偏移
	const int newOffset = OffsetFromThumbStart(pos - m_dragGrabOffset);

	if (newOffset != m_offset){

		m_offset = newOffset;
		NotifyOffset();   // 用户操作 ⇒ 通知容器
		Invalidate();

	}

}

}
