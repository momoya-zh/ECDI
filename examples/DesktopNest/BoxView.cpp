#include "BoxView.h"   // .cpp 对应头（第一）

#include "BoxWindow.h"
#include "MemberListPopup.h"

#include "ECDI/Core/Font.h"
#include "ECDI/Core/Point.h"
#include "ECDI/Core/Size.h"
#include "ECDI/Render/PaintContext.h"
#include "ECDI/Widget/Label.h"

#include <memory>
#include <string>
#include <utility>

namespace ECDI::DesktopNest {

namespace {

/// @brief 入口按钮文本（K3——评审可驳：纯常量，改文案只动这里）
constexpr const char* kEntryButtonText = "Members";

/// @brief 折叠按钮文本（展开态 / 折叠态——ASCII 安全，条 48）
constexpr const char* kCollapseTextExpanded = "-";
constexpr const char* kCollapseTextCollapsed = "+";

/// @brief 内容区假行高（DIP）
constexpr int kContentRowHeight = 24;

/// @brief 内容区假行文本后缀（"<成员标题> · 假数据行 N" 的 ASCII 形态）
constexpr const char* kFakeRowSeparator = " - row ";

}   // namespace

// ── BoxRoot ─────────────────────────────────────────

void BoxRoot::OnPaint(PaintContext& ctx, int x, int y){

	// 框背景：圆角矩形铺满客户区（K1）——坐标 = 控件局部 + (x, y) 偏移
	// （PaintContext 命令存最终坐标——坐标转换在 Paint 阶段完成）
	const float w = static_cast<float>(GetWidth());
	const float h = static_cast<float>(GetHeight());

	ctx.DrawRoundedRect(
		Rect{ static_cast<float>(x), static_cast<float>(y), w, h },
		static_cast<float>(Metrics::kBoxRadius), Palette::BoxBackground());

	// 标题条分隔线（y = kCaptionHeight）
	const float lineY = static_cast<float>(y + Metrics::kCaptionHeight);

	ctx.DrawLine(Point{ static_cast<float>(x), lineY },
	             Point{ static_cast<float>(x) + w, lineY },
	             1.0f, Palette::TitleSeparator());
}

void BoxRoot::OnMouseButtonDown(const MouseButtonDownEvent& event){

	// C-M1-12 点外关闭判据（三判据 + 入口豁免）
	if (m_popup == nullptr || !m_popup->IsVisible()){

		return;   // ① 浮层未开——无判据
	}

	Widget* hit = HitTest(event.GetMouseX(), event.GetMouseY());

	if (hit == nullptr){

		return;   // ② 防御（RootWidget 全覆盖）
	}

	// ③ 浮层内（含空白）——不穿透、不关闭。
	// ★ 实施勘误（2026-10-09）：详设 §9⑥ 原写「用既有 Widget::Contains」，实测该方法为
	//   **private**（Widget.h:280——仅服务 AddChild 防环）⇒ 回到初设 §1.1.2 的手写上溯：
	//   hit 或其任一祖先 == popup ⇔ hit 在浮层子树内。
	for (Widget* node = hit; node != nullptr; node = node->GetParent()){

		if (node == m_popup){

			return;
		}
	}

	if (hit == m_entryButton){

		return;   // ④ 入口按钮豁免（P6——开/关由其点击回调权威切换）
	}

	// ⑤ 点外（含标题条其他按钮 / 内容区）⇒ 关闭
	m_popup->SetVisible(false);
	Invalidate();
}

// ── BadgeButton ─────────────────────────────────────

BadgeButton::BadgeButton(const std::string& text)
	: Button(text){

}

void BadgeButton::OnPaint(PaintContext& ctx, int x, int y){

	Button::OnPaint(ctx, x, y);   // 按钮本体（框架默认 ButtonStyle）

	if (m_badgeCount <= 0){

		return;
	}

	const float d = static_cast<float>(Metrics::kBadgeDiameter);
	const float bx = static_cast<float>(x) + static_cast<float>(GetWidth()) - d - 2.0f;
	const float by = static_cast<float>(y) + 2.0f;

	// 右上徽标圆
	ctx.DrawRoundedRect(Rect{ bx, by, d, d }, d / 2.0f, Palette::BadgeBackground());

	// 计数（居中偏移由控件算好——PaintContext 不做对齐）
	const std::string text = std::to_string(m_badgeCount);
	const Size size = ctx.MeasureText(Font(), text);

	ctx.DrawText(Point{ bx + (d - size.width) / 2.0f, by + (d - size.height) / 2.0f },
	             text, Palette::BadgeText(), Font());
}

// ── BoxView ─────────────────────────────────────────

BoxView::BoxView(BoxModel& model, BoxId box)
	: m_model(model), m_box(box){

	// 订阅一次注册（构造期）、析构期注销——重建不重建订阅（C-M1-16）
	m_subscription = m_model.AddHandler(
		[this](BoxId changed, ModelChange change){ OnModelChanged(changed, change); });
}

BoxView::~BoxView(){

	m_model.RemoveHandler(m_subscription);
}

void BoxView::Assemble(Widget& hostRoot){

	// 解装旧树（S3 幂等——宿主要换根时先摘旧根）
	if (m_root != nullptr){

		Disassemble();
	}

	const int width = hostRoot.GetWidth();
	const int height = hostRoot.GetHeight();

	// ① 根容器（判据宿主——满铺客户区）
	auto root = std::make_unique<BoxRoot>();

	m_root = root.get();

	m_root->SetPosition(0, 0);
	m_root->SetSize(width, height);

	// ② 标题条容器（裸 Widget——三部件 + 拖动空白）
	auto titleBar = std::make_unique<Widget>();

	m_titleBar = titleBar.get();

	m_titleBar->SetPosition(0, 0);
	m_titleBar->SetSize(width, Metrics::kCaptionHeight);

	// 入口按钮（左侧——K3）
	auto entry = std::make_unique<BadgeButton>(kEntryButtonText);

	m_entryButton = entry.get();

	m_entryButton->SetPosition(Metrics::kBarPad, 0);
	m_entryButton->SetSize(Metrics::kEntryButtonWidth, Metrics::kCaptionHeight);

	m_entryButton->SetOnClick([this]{ TogglePopup(); });   // P6 切换契约

	// 标题 Label（中段——派生标题，C-M1-17）
	auto title = std::make_unique<Label>(DerivedTitle());

	m_titleLabel = title.get();

	const int titleX = Metrics::kBarPad + Metrics::kEntryButtonWidth + Metrics::kBarPad;
	const int titleW = width - titleX - Metrics::kCollapseButtonSize;

	m_titleLabel->SetPosition(titleX, 0);
	m_titleLabel->SetSize(titleW > 0 ? titleW : 0, Metrics::kCaptionHeight);
	m_titleLabel->SetTextColor(Palette::TextForeground());

	// 折叠按钮（右侧——K2 唯一二态开关）
	auto collapse = std::make_unique<Button>(kCollapseTextExpanded);

	m_collapseButton = collapse.get();

	m_collapseButton->SetPosition(width - Metrics::kCollapseButtonSize, 0);
	m_collapseButton->SetSize(Metrics::kCollapseButtonSize, Metrics::kCollapseButtonSize);

	m_collapseButton->SetOnClick([this]{ ToggleCollapse(); });

	m_titleBar->AddChild(std::move(entry));
	m_titleBar->AddChild(std::move(title));
	m_titleBar->AddChild(std::move(collapse));

	// ③ 内容面板（折叠消费 CollapsiblePanel——K2；初始化约定：
	//    SetExpandDirection → SetPosition → SetSize，且默认收起 ⇒ 显式展开）
	auto content = std::make_unique<CollapsiblePanel>();

	m_contentPanel = content.get();

	m_contentPanel->SetExpandDirection(ExpandDirection::Down);
	m_contentPanel->SetPosition(0, Metrics::kCaptionHeight);
	m_contentPanel->SetSize(width, height - Metrics::kCaptionHeight);
	m_contentPanel->SetExpanded(true);   // M1 框默认展开（覆盖框架默认收起）

	// ④ 浮层（末位 AddChild——z 序最顶；常驻隐藏态）
	auto popup = std::make_unique<MemberListPopup>();

	m_popup = popup.get();

	m_popup->SetPosition(0, Metrics::kCaptionHeight);
	m_popup->SetSize(Metrics::kPopupWidth, 120);
	m_popup->SetVisible(false);

	m_popup->SetOnMemberSelected([this](BoxId member){ SelectMember(member); });
	m_popup->SetOnDetachRequested([this]{ RequestDetach(); });
	m_popup->SetOnCollectRequested([this](BoxId source){ RequestCollect(source); });

	// 装配顺序即 z 序：标题条 → 内容 → 浮层（浮层最顶）
	m_root->AddChild(std::move(titleBar));
	m_root->AddChild(std::move(content));
	m_root->AddChild(std::move(popup));

	hostRoot.AddChild(std::move(root));

	// 判据接线（C-M1-12）
	static_cast<BoxRoot*>(m_root)->SetPopup(m_popup);
	static_cast<BoxRoot*>(m_root)->SetEntryButton(m_entryButton);

	// 内容假行 + 徽标（首次呈现）
	RebuildContent();
	Rebuild();
}

void BoxView::Disassemble() noexcept{

	// 非拥有指针清空（C-M1-16 解装不变量——必须在旧窗清理点之前断链，C-M1-20(c)）
	m_root = nullptr;
	m_titleBar = nullptr;
	m_entryButton = nullptr;
	m_titleLabel = nullptr;
	m_collapseButton = nullptr;
	m_contentPanel = nullptr;
	m_popup = nullptr;
}

std::string BoxView::DerivedTitle() const{

	// C-M1-17 派生标题：members 空 ⇒ 框标题；否则 ⇒ active 成员标题
	const Box& box = m_model.GetBox(m_box);

	if (box.members.empty()){

		return box.title;
	}

	return m_model.GetBox(box.active).title;
}

void BoxView::RebuildContent(){

	if (m_contentPanel == nullptr){

		return;
	}

	Widget* content = m_contentPanel->GetContent();

	if (content == nullptr){

		return;
	}

	// 清旧行（RemoveChild 返 unique_ptr——离开作用域即销毁）
	while (content->GetChildCount() > 0){

		content->RemoveChild(content->GetChildAt(0));
	}

	const std::string title = DerivedTitle();

	for (int i = 0; i < Metrics::kFakeContentRows; ++i){

		auto row = std::make_unique<Label>(title + kFakeRowSeparator + std::to_string(i + 1));

		row->SetPosition(Metrics::kBarPad, i * kContentRowHeight);
		row->SetSize(m_contentPanel->GetWidth() - 2 * Metrics::kBarPad, kContentRowHeight);
		row->SetTextColor(Palette::SecondaryText());

		content->AddChild(std::move(row));
	}
}

void BoxView::Rebuild(){

	if (m_root == nullptr){

		return;   // 未装配（已解装态）——重建无对象可施
	}

	// ① 标题（派生标题 C-M1-17）
	if (m_titleLabel != nullptr){

		m_titleLabel->SetText(DerivedTitle());
		m_titleLabel->Invalidate();   // SetText 不请求重绘（架构边界约定）
	}

	// ② 徽标 = members.size()
	if (m_entryButton != nullptr){

		m_entryButton->SetBadgeCount(static_cast<int>(m_model.GetBox(m_box).members.size()));
		m_entryButton->Invalidate();
	}

	// ③ 折叠按钮文案按当前展开态
	if (m_collapseButton != nullptr && m_contentPanel != nullptr){

		m_collapseButton->SetText(m_contentPanel->IsExpanded() ? kCollapseTextExpanded
		                                                       : kCollapseTextCollapsed);
		m_collapseButton->Invalidate();
	}

	// ④ 内容区假行（标题随 active 成员）
	RebuildContent();

	// ⑤ 浮层（C-M1-19 R2）：可见 ⇒ 原位刷新；不可见 ⇒ 跳过（留待下次 Open 全量重建）
	if (m_popup != nullptr && m_popup->IsVisible()){

		m_popup->Refresh(m_model, m_box);
	}

	m_root->Invalidate();
}

void BoxView::OnModelChanged(BoxId box, ModelChange change){

	if (box != m_box){

		return;   // 订阅是广播——只认自己
	}

	switch (change){

	case ModelChange::Absorbed:
		// 协调器关原生窗口（C-M1-10②）——视图本地不动控件树
		if (m_onAbsorbed){

			m_onAbsorbed();
		}
		break;

	case ModelChange::Released:
		// 旧树随旧窗销毁（延迟到泵清理点，C-M1-20）——协调器负责重建窗口；
		// 此处**不得** Rebuild（无根可施，且旧树即将销毁）
		break;

	default:
		// Collected / MemberDetached / Activated ⇒ 重建（S3 幂等）
		Rebuild();
		break;
	}
}

void BoxView::OpenPopup(){

	if (m_popup == nullptr){

		return;
	}

	// 单浮层纪律：开新关旧（协调器中介）
	if (m_onPopupOpening){

		m_onPopupOpening();
	}

	// 全量重建（C-M1-19 R1：行只在 Open 时重建）+ 弹出几何（高度按内容）
	m_popup->Rebuild(m_model, m_box);

	const int memberCount = static_cast<int>(m_model.GetBox(m_box).members.size());
	const int joinCount = static_cast<int>(m_model.GetTopLevelBoxes().size()) - 1;
	const int popupHeight = 60 + memberCount * Metrics::kPopupRowHeight
	                        + (joinCount > 0 ? joinCount : 0) * Metrics::kPopupRowHeight;

	m_popup->SetSize(Metrics::kPopupWidth, popupHeight);
	m_popup->SetVisible(true);

	if (m_root != nullptr){

		m_root->Invalidate();
	}
}

void BoxView::ClosePopup(){

	if (m_popup == nullptr || !m_popup->IsVisible()){

		return;
	}

	m_popup->SetVisible(false);

	if (m_root != nullptr){

		m_root->Invalidate();
	}
}

void BoxView::TogglePopup(){

	if (IsPopupVisible()){

		ClosePopup();
	}
	else{

		OpenPopup();
	}
}

bool BoxView::IsPopupVisible() const noexcept{

	return (m_popup != nullptr) && m_popup->IsVisible();
}

int BoxView::GetBadgeCount() const noexcept{

	return (m_entryButton != nullptr) ? m_entryButton->GetBadgeCount() : -1;
}

std::string BoxView::GetTitleText() const{

	if (m_titleLabel == nullptr){

		return {};
	}

	return m_titleLabel->GetText();
}

void BoxView::ToggleCollapse(){

	if (m_contentPanel == nullptr){

		return;
	}

	const bool expanded = !m_contentPanel->IsExpanded();

	// C-M1-18 折叠时序：窗口几何瞬时（请求语义）**先行**
	if (m_window != nullptr){

		Rect bounds = m_window->GetBounds();

		if (bounds.width > 0.0f){

			// ★ 折叠目标高 = 「标题条 + 系统边框」（方案 A——C-M1-18）：
			//   `GetBounds()` 是总尺寸口径，而 K2 的「仅标题条」是客户区口径，
			//   且系统有最小窗口高 ⇒ 不能直接写 kCaptionHeight（实测会被钳到 39）。
			bounds.height = expanded ? static_cast<float>(Metrics::kWindowHeight)
			                         : static_cast<float>(m_window->CollapsedHeight());
			m_window->SetBounds(bounds);
		}
	}

	// 内容子树随后（CollapsiblePanel 内置 200ms 过渡；无 Window 时降级瞬时）
	m_contentPanel->SetExpanded(expanded);

	if (m_collapseButton != nullptr){

		m_collapseButton->SetText(expanded ? kCollapseTextExpanded : kCollapseTextCollapsed);
		m_collapseButton->Invalidate();
	}
}

void BoxView::SelectMember(BoxId member){

	// K4 纪律：纯 Model 操作（通知驱动重建——不在此处直接改控件）
	m_model.SetActive(m_box, member);
}

void BoxView::RequestDetach(){

	// C-M1-19 R3：先关浮层（通知到达时浮层已隐藏 ⇒ Rebuild 跳过浮层 ⇒ 被点行不销毁）
	ClosePopup();

	if (m_onAction){

		m_onAction(Action::Detach, m_box);
	}
}

void BoxView::RequestCollect(BoxId source){

	// C-M1-19 R3：先关浮层（同 RequestDetach）
	ClosePopup();

	if (m_onAction){

		m_onAction(Action::Collect, source);
	}
}

}   // namespace ECDI::DesktopNest
