#include "ECDI/Widget/ScrollView.h"

#include "ECDI/Core/ECDIAssert.h"
#include "ECDI/EventSystem/Input/Mouse/MouseWheelEvent.h"
#include "ECDI/Widget/ScrollBar.h"
#include "Widget/ScrollContent.h"   // 内部头（src 内相对 include——同 CaptionButton 先例）

#include <algorithm>
#include <memory>

namespace ECDI{

ScrollView::ScrollView(){

	// ① 内容节点：**偏移的消费者**——唯一进入内容坐标系的子树（详设 §3.3）
	auto content = std::make_unique<ScrollContent>(*this);
	m_content = content.get();
	AddChild(std::move(content));

	// ② 两条：**最后 AddChild ⇒ HitTest 逆序时条优先命中**（天然满足"条优先成为 target"）
	auto vbar = std::make_unique<ScrollBar>(ScrollBar::Orientation::Vertical);
	m_vBar = vbar.get();
	AddChild(std::move(vbar));

	auto hbar = std::make_unique<ScrollBar>(ScrollBar::Orientation::Horizontal);
	m_hBar = hbar.get();
	AddChild(std::move(hbar));

	// ③ 条 → 容器：用户操作驱动的偏移变化（`ScrollBar::SetOffset` 不回触发 ⇒ 无递归环）
	m_vBar->SetOnOffsetChanged([this](int offset){ SetContentOffset(GetScrollOffsetX(), offset); });
	m_hBar->SetOnOffsetChanged([this](int offset){ SetContentOffset(offset, GetScrollOffsetY()); });

	ApplyLayout();

}

ScrollView::~ScrollView() = default;   // 三个成员皆**非拥有**——生命周期归 m_children（C14）

// ── 内容 ─────────────────────────────────────────────────────

Widget& ScrollView::GetContentView() noexcept{ return *m_content; }

const Widget& ScrollView::GetContentView() const noexcept{ return *m_content; }

void ScrollView::UpdateContentExtent(){

	// D6：内容坐标系下二维包围盒右下角（负向钳 0）——**不是 Σ**（间隔会漏算）
	int w = 0;
	int h = 0;

	const size_t n = m_content->GetChildCount();

	for (size_t i = 0; i < n; ++i){

		const Widget* child = m_content->GetChildAt(i);

		w = (std::max)(w, child->GetX() + child->GetWidth());
		h = (std::max)(h, child->GetY() + child->GetHeight());

	}

	SetContentExtent(w, h);

}

void ScrollView::SetContentExtent(int width, int height){

	const int w = (std::max)(0, width);
	const int h = (std::max)(0, height);

	if (w == m_contentW && h == m_contentH){

		return;   // 同尺寸 no-op（避免无谓重排）

	}

	m_contentW = w;
	m_contentH = h;

	m_content->SetSize(m_contentW, m_contentH);   // 内容坐标空间的根尺寸 = 内容 extent

	// ★ 顺序冻结（详设 §2.6）：**不得调换**
	ApplyLayout();     // ① 条几何 + 可见性 ⇒ 定下 ViewportWidth/Height
	ClampOffset();     // ② 用**新** viewport 重算 maxOffset 并 clamp（防内容变短后 offset 悬空）
	SyncBars();        // ③ 条范围 + 可见性 + 滑块位置

	Invalidate();

}

Size ScrollView::GetContentExtent() const noexcept{

	return Size{ static_cast<float>(m_contentW), static_cast<float>(m_contentH) };

}

// ── 偏移（唯一权威 + clamp）──────────────────────────────────

void ScrollView::SetContentOffset(int x, int y){

	const int cx = (std::clamp)(x, 0, GetMaxOffsetX());
	const int cy = (std::clamp)(y, 0, GetMaxOffsetY());

	if (cx == m_offsetX && cy == m_offsetY){

		return;   // 同值 no-op

	}

	m_offsetX = cx;
	m_offsetY = cy;

	SyncBars();   // 滑块位置跟随（条自身不 clamp——D11 单一真相源）
	Invalidate();

}

int ScrollView::GetScrollOffsetX() const noexcept{ return m_offsetX; }

int ScrollView::GetScrollOffsetY() const noexcept{ return m_offsetY; }

int ScrollView::GetMaxOffsetX() const noexcept{

	return (std::max)(0, m_contentW - ViewportWidth());

}

int ScrollView::GetMaxOffsetY() const noexcept{

	return (std::max)(0, m_contentH - ViewportHeight());

}

// ── 配置 ─────────────────────────────────────────────────────

void ScrollView::SetScrollStep(int step) noexcept{

	FRAMEWORK_ASSERT(step >= 0);   // 负步长无合理语义

	if (step < 0){

		return;   // release 下忽略（不把契约建立在断言上）

	}

	m_step = step;

}

int ScrollView::GetScrollStep() const noexcept{ return m_step; }

void ScrollView::SetScrollBarVisible(bool visible){

	if (visible == m_barsVisible){

		return;

	}

	m_barsVisible = visible;

	// 可见性变化 ⇒ 条不再占位 ⇒ viewport 变 ⇒ 同一套顺序冻结链
	ApplyLayout();
	ClampOffset();
	SyncBars();

	Invalidate();

}

bool ScrollView::IsScrollBarVisible() const noexcept{ return m_barsVisible; }

ScrollBar* ScrollView::GetVerticalScrollBar() noexcept{ return m_vBar; }

ScrollBar* ScrollView::GetHorizontalScrollBar() noexcept{ return m_hBar; }

// ── 内部：双轴 viewport（两轮判定——详设 §3.3）────────────────

int ScrollView::BarThickness() const noexcept{

	// 厚度来自样式（主题可改）⇒ 单点可查（ScrollBar::GetThickness）
	return (m_vBar != nullptr) ? m_vBar->GetThickness() : 0;

}

bool ScrollView::NeedsVerticalBar() const noexcept{

	if (!m_barsVisible){

		return false;

	}

	const int t  = BarThickness();
	const int w0 = GetWidth();
	const int h0 = GetHeight();

	// 第一轮：用**未扣除**的视口初判
	const bool nV1 = m_contentH > h0;
	const bool nH1 = m_contentW > w0;

	// 第二轮：按第一轮结论扣除后再判——**单调升级**
	//（viewport 只会变小 ⇒ need 只会 false→true ⇒ 并集即不动点，最多 2 轮收敛）
	const int tH = h0 - (nH1 ? t : 0);

	return nV1 || (m_contentH > tH);

}

bool ScrollView::NeedsHorizontalBar() const noexcept{

	if (!m_barsVisible){

		return false;

	}

	const int t  = BarThickness();
	const int w0 = GetWidth();
	const int h0 = GetHeight();

	const bool nV1 = m_contentH > h0;
	const bool nH1 = m_contentW > w0;

	const int tW = w0 - (nV1 ? t : 0);

	return nH1 || (m_contentW > tW);

}

int ScrollView::ViewportWidth() const noexcept{

	return GetWidth() - (NeedsVerticalBar() ? BarThickness() : 0);

}

int ScrollView::ViewportHeight() const noexcept{

	return GetHeight() - (NeedsHorizontalBar() ? BarThickness() : 0);

}

// ── 内部：布局与同步（**顺序冻结**）──────────────────────────

void ScrollView::ApplyLayout(){

	const int t = BarThickness();

	// ① 条几何（**视口空间**——固定在视口，不受偏移）
	//    垂直条贴右、水平条贴底；交叉区不做缩角（记账 L2）
	if (m_vBar != nullptr){

		m_vBar->SetSize(t, GetHeight());
		m_vBar->SetPosition(GetWidth() - t, 0);

	}

	if (m_hBar != nullptr){

		m_hBar->SetSize(GetWidth(), t);
		m_hBar->SetPosition(0, GetHeight() - t);

	}

	// ② 内容节点几何 = 内容 extent（其子在**内容坐标系**里布局）
	if (m_content != nullptr){

		m_content->SetSize(m_contentW, m_contentH);
		m_content->SetPosition(0, 0);

	}

}

void ScrollView::ClampOffset(){

	// 不变式 C7：任意时刻 0 ≤ offset ≤ maxOffset
	m_offsetX = (std::clamp)(m_offsetX, 0, GetMaxOffsetX());
	m_offsetY = (std::clamp)(m_offsetY, 0, GetMaxOffsetY());

}

void ScrollView::SyncBars(){

	if (m_vBar != nullptr){

		const bool need = NeedsVerticalBar();

		m_vBar->SetVisible(need);                     // 不可见 ⇒ 不参与 HitTest（既有语义）
		m_vBar->SetRange(m_contentH, ViewportHeight());
		m_vBar->SetOffset(m_offsetY);                 // 外部驱动入口（不回调）

	}

	if (m_hBar != nullptr){

		const bool need = NeedsHorizontalBar();

		m_hBar->SetVisible(need);
		m_hBar->SetRange(m_contentW, ViewportWidth());
		m_hBar->SetOffset(m_offsetX);

	}

}

// ── 尺寸与滚轮 ───────────────────────────────────────────────

void ScrollView::SetSize(int w, int h){

	Widget::SetSize(w, h);   // 基类几何（必须显式调——override 会隐藏基类重载）

	// 顺序冻结（与 SetContentExtent 同一套）
	ApplyLayout();     // ① 条几何 + 可见性 ⇒ 定下 viewport
	ClampOffset();     // ②
	SyncBars();        // ③

	// 不 Invalidate：SetSize 由布局路径调用，重绘由调用方负责（既有契约）

}

void ScrollView::OnMouseWheel(const MouseWheelEvent& event){

	// 与 TextBox.cpp:452 同式（120 = WHEEL_DELTA 基准；高精度滚轮天然支持分数步长）
	// delta > 0 = 远离用户（向上滚）⇒ 内容上移 ⇒ offset 减小
	const int delta = static_cast<int>(static_cast<float>(event.GetDelta()) / 120.0f
	                                   * static_cast<float>(m_step));

	SetContentOffset(m_offsetX, m_offsetY - delta);   // 经唯一入口 ⇒ clamp + sync + Invalidate

}

}
