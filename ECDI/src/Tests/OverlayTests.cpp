#include "RunAllTests.h"
#include "TestFramework.h"
#include "WindowProbe.h"

#include "ECDI/Application/Application.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyCode.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyDownEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonUpEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseMoveEvent.h"
#include "ECDI/Layout/VerticalLayout.h"
#include "ECDI/Widget/Button.h"
#include "ECDI/Widget/Panel.h"
#include "ECDI/Widget/ScrollView.h"
#include "ECDI/Window/OverlayTypes.h"
#include "ECDI/Window/Window.h"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

using namespace ECDI;
using namespace ECDI::Test;

// ══════════════════════════════════════════════════════════════════════
// ★ Phase 42（#81）：Window 级浮层根（Overlay Root）与 Light Dismiss 用例组
// ══════════════════════════════════════════════════════════════════════
// 需求稿 v1.1 / 初设 v1.1 / 详设 v1.1 验收编号 T42-1..T42-16。
// 分批计划：
//   批 0：T42-1（零浮层负对照）、T42-2（ScrollView裁剪负对照）
//   批 1：T42-3（回落负对照）、T42-14（HoverTracker双根）、T42-15（Tooltip穿透）
//   批 2：T42-4（逃逸视口裁剪）、T42-5（主布局零挤压）、T42-6（Z-Order层次）、
//         T42-10（嵌套级联关闭）、T42-16（Phase 41失效结算）
//   批 3：T42-7（点外策略）、T42-8（豁免控件）、T42-9（ESC关闭）、
//         T42-11（Focus Trap与回焦）、T42-12（锚点翻转）、T42-13（脱树自动关闭）

namespace {

struct TestApp : public Application {
	using Application::OnMouseButtonDown;
	using Application::OnMouseMove;
	using Application::OnMouseButtonUp;
	using Application::FindTargetWidget;
};

/// @brief hover 状态探针控件
class HoverProbe final : public Button {
public:
	explicit HoverProbe(const std::string& text) : Button(text) {}
	int enter = 0;
	int leave = 0;
	bool isHovered = false;
protected:
	void OnMouseEnter() override {
		Button::OnMouseEnter();
		++enter;
		isHovered = true;
	}
	void OnMouseLeave() override {
		Button::OnMouseLeave();
		++leave;
		isHovered = false;
	}
};

// ── T42-1（负对照）：无浮层时主树行为零副作用 ─────────────────────────
void TestOverlayNegativeZeroOverlayBitIdentical() {
	WindowProbe probe("ECDI_P42_ZeroOverlay");
	probe.hwnd = ::FindWindowW(nullptr, L"ECDI_P42_ZeroOverlay");
	EXPECT_TRUE(probe.hwnd != nullptr);
	if (probe.hwnd == nullptr) return;
	probe.ShowAndSettle();

	// 主树挂载按钮
	auto btn = std::make_unique<Button>("Click Me");
	btn->SetPosition(20, 20);
	btn->SetSize(100, 40);
	probe.window->GetRootWidget().AddChild(std::move(btn));

	probe.PumpOneFrame();
	EXPECT_EQ(probe.window->GetRootWidget().GetChildCount(), static_cast<std::size_t>(1));
}

// ── T42-2（负对照）：普通子树内挂在 ScrollView 的控件超出视口被裁切 ───
void TestOverlayNegativeScrollViewClipIntact() {
	WindowProbe probe("ECDI_P42_ScrollClip");
	probe.hwnd = ::FindWindowW(nullptr, L"ECDI_P42_ScrollClip");
	EXPECT_TRUE(probe.hwnd != nullptr);
	if (probe.hwnd == nullptr) return;
	probe.ShowAndSettle();

	auto scroll = std::make_unique<ScrollView>();
	scroll->SetPosition(0, 0);
	scroll->SetSize(100, 100);

	auto child = std::make_unique<Button>("OverflowChild");
	child->SetPosition(50, 50);
	child->SetSize(100, 100); // 超出视口
	scroll->GetContentView().AddChild(std::move(child));

	probe.window->GetRootWidget().AddChild(std::move(scroll));

	probe.PumpOneFrame();
	// 祖先裁剪机制未受破坏，树结构完好
	EXPECT_TRUE(probe.window->GetRootWidget().GetChildCount() > 0);
}

// ── T42-3（回落负对照）：浮层未命中时回落探测主内容树 ─────────────────
void TestOverlayNegativeFallbackToContentRoot() {
	TestApp app;
	auto& window = app.Create("Negative_Fallback", 800, 600);

	auto baseBtn = std::make_unique<Button>("BaseBtn");
	baseBtn->SetPosition(10, 10);
	baseBtn->SetSize(60, 30);
	Button* baseRaw = baseBtn.get();
	window.GetRootWidget().AddChild(std::move(baseBtn));

	// 使用底阶原语挂载浮层
	auto overlayBtn = std::make_unique<Button>("OverlayBtn");
	overlayBtn->SetPosition(100, 100);
	overlayBtn->SetSize(60, 30);
	Button* overlayRaw = overlayBtn.get();
	window.GetOverlayRoot().AddChild(std::move(overlayBtn));

	// 命中测试同源回落
	EXPECT_EQ(app.FindTargetWidget(window, 20, 20), baseRaw);
	EXPECT_EQ(app.FindTargetWidget(window, 110, 110), overlayRaw);
	EXPECT_EQ(app.FindTargetWidget(window, 300, 300), &window.GetRootWidget()); // Overlay 根为 Panel 不自身拦截，回落到主树根
	EXPECT_EQ(app.FindTargetWidget(window, 900, 900), nullptr);

	window.Release();
}

// ── T42-14：浮层内控件 Hover 离开后正常收到 OnMouseLeave，状态不粘连 ──
void TestOverlayHoverTrackingAcrossDualRoots() {
	TestApp app;
	auto& window = app.Create("HoverTracker", 800, 600);

	auto btn = std::make_unique<HoverProbe>("HoverOverlayBtn");
	btn->SetPosition(50, 50);
	btn->SetSize(100, 40);
	HoverProbe* raw = btn.get();
	window.GetOverlayRoot().AddChild(std::move(btn));

	// 鼠标移入浮层按钮
	MouseMoveEvent moveIn(&window, 55, 55);
	app.OnMouseMove(moveIn);
	EXPECT_TRUE(raw->isHovered);
	EXPECT_EQ(raw->enter, 1);

	// 鼠标移出到空白区
	MouseMoveEvent moveOut(&window, 300, 300);
	app.OnMouseMove(moveOut);
	EXPECT_FALSE(raw->isHovered); // 离开浮层不粘连
	EXPECT_EQ(raw->leave, 1);

	window.Release();
}

// ── T42-15：Tooltip 纯视觉呈现，不参与命中，点击穿透至底层 ────────────
void TestOverlayTooltipLayerPassThrough() {
	TestApp app;
	auto& window = app.Create("TooltipNonInteractive", 800, 600);

	auto baseBtn = std::make_unique<Button>("BaseBtn");
	baseBtn->SetPosition(50, 50);
	baseBtn->SetSize(100, 40);
	Button* baseRaw = baseBtn.get();
	window.GetRootWidget().AddChild(std::move(baseBtn));

	// 弹出覆盖在按钮正上方的 Tooltip
	auto tip = std::make_unique<Button>("Tip");
	tip->SetPosition(50, 50);
	tip->SetSize(100, 40);
	OverlayOptions opt;
	opt.layer = OverlayLayer::Tooltip;
	window.ShowOverlay(std::move(tip), opt);

	// HitTest 直接穿透 Tooltip 命中下层 BaseBtn
	EXPECT_EQ(app.FindTargetWidget(window, 60, 60), baseRaw);

	window.Release();
}

// ── 批 2 浮层生命周期、分层编排与管线接缝 ─────────────────────────

// ── T42-4：浮层独立于主内容树，不受 ScrollView 等容器祖先裁剪限制 ─────
void TestOverlayEscapeScrollViewClip() {
	TestApp app;
	auto& window = app.Create("EscapeScrollViewClip", 800, 600);

	// 主树挂载 100x100 的 ScrollView
	auto scroll = std::make_unique<ScrollView>();
	scroll->SetPosition(0, 0);
	scroll->SetSize(100, 100);
	window.GetRootWidget().AddChild(std::move(scroll));

	// 浮层尺寸超出 ScrollView 边界 (50, 50, 200, 200)
	auto popup = std::make_unique<Button>("BigPopup");
	popup->SetPosition(50, 50);
	popup->SetSize(200, 200);
	Button* popupRaw = popup.get();

	EXPECT_TRUE(window.ShowOverlay(std::move(popup)));
	EXPECT_TRUE(window.HasActiveOverlay(OverlayLayer::Normal));

	// 在超出 ScrollView 区域 (150, 150) 处进行命中测试，依然能命中 BigPopup
	EXPECT_EQ(app.FindTargetWidget(window, 150, 150), popupRaw);
	// 在 ScrollView 内部重叠区 (60, 60) 同样命中 BigPopup（浮层在上方）
	EXPECT_EQ(app.FindTargetWidget(window, 60, 60), popupRaw);

	window.HideOverlay(popupRaw);
	EXPECT_FALSE(window.HasActiveOverlay());

	window.Release();
}

// ── T42-5：主内容树布局绝对零挤压（浮层不在主树布局槽内）───────────────
void TestOverlayZeroMainLayoutSqueeze() {
	TestApp app;
	auto& window = app.Create("MainLayoutNeverSquashed", 800, 600);

	auto layout = std::make_unique<VerticalLayout>(0, true);
	window.GetRootWidget().SetLayout(std::move(layout));

	auto content = std::make_unique<Panel>();
	content->SetSize(800, 600);
	Panel* contentRaw = content.get();
	window.GetRootWidget().AddChild(std::move(content));
	window.GetRootWidget().Arrange();

	const int initialH = contentRaw->GetHeight();

	// 弹出全屏模态遮罩
	auto mask = std::make_unique<Panel>();
	mask->SetSize(800, 600);
	Panel* maskRaw = mask.get();
	OverlayOptions opt;
	opt.layer = OverlayLayer::Modal;
	EXPECT_TRUE(window.ShowOverlay(std::move(mask), opt));

	// 主树重新排布，内容高度分毫不差
	window.GetRootWidget().Arrange();
	EXPECT_EQ(contentRaw->GetHeight(), initialH);

	// 结构校验：主树仅有 1 个子节点，模态遮罩在副树 ModalLayer 内
	EXPECT_EQ(window.GetRootWidget().GetChildCount(), static_cast<std::size_t>(1));
	EXPECT_EQ(maskRaw->GetParent(), window.GetOverlayRoot().GetChildAt(1)); // 0:Normal, 1:Modal, 2:Tooltip

	window.HideOverlay(maskRaw);
	window.Release();
}

// ── T42-6：严格三层拓扑与 Z-Order 层级判定（Modal 压 Normal）──────────
void TestOverlayZOrderHierarchy() {
	TestApp app;
	auto& window = app.Create("ZOrderHierarchy", 800, 600);

	auto normal = std::make_unique<Button>("Normal");
	normal->SetPosition(50, 50);
	normal->SetSize(100, 40);
	Button* normalRaw = normal.get();
	OverlayOptions optNormal;
	optNormal.layer = OverlayLayer::Normal;
	window.ShowOverlay(std::move(normal), optNormal);

	auto modal = std::make_unique<Button>("Modal");
	modal->SetPosition(50, 50);
	modal->SetSize(100, 40);
	Button* modalRaw = modal.get();
	OverlayOptions optModal;
	optModal.layer = OverlayLayer::Modal;
	window.ShowOverlay(std::move(modal), optModal);

	// Modal 压在 Normal 上方，同坐标命中 Modal
	EXPECT_EQ(app.FindTargetWidget(window, 60, 60), modalRaw);

	// 叠加 Tooltip（纯视觉穿透，不抢占命中）
	auto tip = std::make_unique<Button>("Tooltip");
	tip->SetPosition(50, 50);
	tip->SetSize(100, 40);
	Button* tipRaw = tip.get();
	OverlayOptions optTip;
	optTip.layer = OverlayLayer::Tooltip;
	window.ShowOverlay(std::move(tip), optTip);

	EXPECT_EQ(app.FindTargetWidget(window, 60, 60), modalRaw);

	// 关闭 Modal 后，同坐标命中 Normal
	window.HideOverlay(modalRaw);
	EXPECT_EQ(app.FindTargetWidget(window, 60, 60), normalRaw);

	window.HideOverlay(tipRaw);
	window.HideOverlay(normalRaw);
	EXPECT_FALSE(window.HasActiveOverlay());

	window.Release();
}

// ── T42-10：嵌套浮层 LIFO 语义与父浮层级联关闭 ───────────────────────
void TestOverlayNestedLifoAndCascadeClose() {
	TestApp app;
	auto& window = app.Create("NestedLifo", 800, 600);

	// 1. 测试同层 LIFO 无参关闭
	auto btn1 = std::make_unique<Button>("Btn1");
	window.ShowOverlay(std::move(btn1));

	auto btn2 = std::make_unique<Button>("Btn2");
	window.ShowOverlay(std::move(btn2));

	EXPECT_TRUE(window.HideOverlay(nullptr)); // 无参关闭顶层（最后加入的 Btn2）
	EXPECT_TRUE(window.HasActiveOverlay());
	EXPECT_TRUE(window.HideOverlay(nullptr)); // 再次关闭 Btn1
	EXPECT_FALSE(window.HasActiveOverlay());

	// 2. 测试显式父子级联关闭
	bool closedA = false;
	bool closedB = false;

	auto btnA = std::make_unique<Button>("A");
	Button* rawA = btnA.get();
	OverlayOptions optA;
	optA.onClosed = [&]{ closedA = true; };
	window.ShowOverlay(std::move(btnA), optA);

	auto btnB = std::make_unique<Button>("B");
	Button* rawB = btnB.get();
	OverlayOptions optB;
	optB.ownerOverlay = rawA;
	optB.onClosed = [&]{ closedB = true; };
	window.ShowOverlay(std::move(btnB), optB);

	// 关闭父浮层 A，子浮层 B 自动级联关闭且两者回调皆触发
	EXPECT_TRUE(window.HideOverlay(rawA));
	EXPECT_TRUE(closedA);
	EXPECT_TRUE(closedB);
	EXPECT_FALSE(window.HasActiveOverlay());

	window.Release();
}

// ── T42-16：Phase 41 双根布局失效与帧首双根结算管线接缝 ─────────────
void TestOverlayPhase41SettleIntegration() {
	WindowProbe probe("ECDI_P42_Phase41Settle");
	probe.hwnd = ::FindWindowW(nullptr, L"ECDI_P42_Phase41Settle");
	EXPECT_TRUE(probe.hwnd != nullptr);
	if (probe.hwnd == nullptr) return;
	probe.ShowAndSettle();

	auto panel = std::make_unique<Panel>();
	panel->SetLayout(std::make_unique<VerticalLayout>(5));
	auto child = std::make_unique<Button>("Child");
	Button* childRaw = child.get();
	panel->AddChild(std::move(child));

	probe.window->ShowOverlay(std::move(panel));

	// 修改浮层内尺寸置脏
	childRaw->SetSize(200, 50);
	EXPECT_TRUE(probe.window->GetOverlayRoot().NeedsLayout());

	probe.PumpOneFrame();
	EXPECT_FALSE(probe.window->GetOverlayRoot().NeedsLayout());
}

// ── 批 3 交互闭环：Light Dismiss、Focus Trap 与锚点跟随 ──────────────

// ── T42-7：Light Dismiss 点外关闭策略（None / PassThrough / Consume）───
void TestOverlayLightDismissPolicies() {
	TestApp app;
	auto& window = app.Create("LightDismiss", 800, 600);

	int downCount = 0;
	class DismissProbe final : public Button {
	public:
		explicit DismissProbe(int& counter) : Button("Probe"), m_counter(counter) {}
	protected:
		void OnMouseButtonDown(const MouseButtonDownEvent& e) override {
			Button::OnMouseButtonDown(e);
			++m_counter;
		}
	private:
		int& m_counter;
	};

	auto baseBtn = std::make_unique<DismissProbe>(downCount);
	baseBtn->SetPosition(20, 20);
	baseBtn->SetSize(80, 40);
	window.GetRootWidget().AddChild(std::move(baseBtn));

	// 1. PassThrough 策略：点击浮层外部，浮层关闭且底层正常收到 Down 事件
	auto pop1 = std::make_unique<Button>("Pop1");
	pop1->SetPosition(200, 200);
	pop1->SetSize(100, 50);
	OverlayOptions opt1;
	opt1.lightDismiss = LightDismissPolicy::PassThrough;
	window.ShowOverlay(std::move(pop1), opt1);

	MouseButtonDownEvent down1(&window, 30, 30, MouseButton::Left);
	app.OnMouseButtonDown(down1);
	EXPECT_FALSE(window.HasActiveOverlay());
	EXPECT_EQ(downCount, 1); // 穿透派发到底座按钮

	// 2. Consume 策略：点击浮层外部，浮层关闭但点击事件被拦截消费
	auto pop2 = std::make_unique<Button>("Pop2");
	pop2->SetPosition(200, 200);
	pop2->SetSize(100, 50);
	OverlayOptions opt2;
	opt2.lightDismiss = LightDismissPolicy::Consume;
	window.ShowOverlay(std::move(pop2), opt2);

	MouseButtonDownEvent down2(&window, 30, 30, MouseButton::Left);
	app.OnMouseButtonDown(down2);
	EXPECT_FALSE(window.HasActiveOverlay());
	EXPECT_EQ(downCount, 1); // 拦截消费，底座按钮未收到事件

	// 3. None 策略：点击浮层外部，浮层不关闭
	auto pop3 = std::make_unique<Button>("Pop3");
	pop3->SetPosition(200, 200);
	pop3->SetSize(100, 50);
	OverlayOptions opt3;
	opt3.lightDismiss = LightDismissPolicy::None;
	Button* pop3Raw = pop3.get();
	window.ShowOverlay(std::move(pop3), opt3);

	MouseButtonDownEvent down3(&window, 30, 30, MouseButton::Left);
	app.OnMouseButtonDown(down3);
	EXPECT_TRUE(window.HasActiveOverlay());
	EXPECT_EQ(downCount, 2); // 浮层不拦截，正常派发

	window.HideOverlay(pop3Raw);
	window.Release();
}

void TestOverlayExemptWidgetClick() {
	TestApp app;
	auto& window = app.Create("ExemptToggle", 800, 600);

	auto entryBtn = std::make_unique<Button>("Entry");
	entryBtn->SetPosition(20, 20);
	entryBtn->SetSize(80, 40);
	Button* entryRaw = entryBtn.get();
	window.GetRootWidget().AddChild(std::move(entryBtn));

	auto pop = std::make_unique<Button>("Pop");
	pop->SetPosition(200, 200);
	pop->SetSize(100, 50);
	OverlayOptions opt;
	opt.exemptWidget = entryRaw;
	opt.lightDismiss = LightDismissPolicy::PassThrough;
	window.ShowOverlay(std::move(pop), opt);

	// 点击豁免入口按钮不关闭浮层
	MouseButtonDownEvent down(&window, 30, 30, MouseButton::Left);
	MouseButtonUpEvent up(&window, 30, 30, MouseButton::Left);
	app.OnMouseButtonDown(down);
	app.OnMouseButtonUp(up);
	EXPECT_TRUE(window.HasActiveOverlay());

	// 点击其他非豁免空白区域 (500, 500) 正常触发关闭
	MouseButtonDownEvent outsideDown(&window, 500, 500, MouseButton::Left);
	MouseButtonUpEvent outsideUp(&window, 500, 500, MouseButton::Left);
	app.OnMouseButtonDown(outsideDown);
	app.OnMouseButtonUp(outsideUp);
	EXPECT_FALSE(window.HasActiveOverlay());

	window.Release();
}

// ── T42-9：ESC 键框架层拦截式关闭活动浮层 ───────────────────────────
void TestOverlayEscKeyDismiss() {
	TestApp app;
	auto& window = app.Create("CloseOnEsc", 800, 600);

	// 1. opt.closeOnEsc = false 场景：按 ESC 不关闭
	auto popNoEsc = std::make_unique<Button>("NoEsc");
	popNoEsc->SetPosition(100, 100);
	popNoEsc->SetSize(100, 50);
	Button* rawNoEsc = popNoEsc.get();
	OverlayOptions optNo;
	optNo.closeOnEsc = false;
	window.ShowOverlay(std::move(popNoEsc), optNo);

	KeyDownEvent escEvent1(&window, KeyCode::Escape, KeyModifier::None);
	window.HandleKeyDown(escEvent1);
	EXPECT_TRUE(window.HasActiveOverlay());
	window.HideOverlay(rawNoEsc);
	EXPECT_FALSE(window.HasActiveOverlay());

	// 2. opt.closeOnEsc = true 场景：按 ESC 自动关闭顶层浮层
	auto popEsc = std::make_unique<Button>("WithEsc");
	popEsc->SetPosition(100, 100);
	popEsc->SetSize(100, 50);
	OverlayOptions optEsc;
	optEsc.closeOnEsc = true;
	window.ShowOverlay(std::move(popEsc), optEsc);

	KeyDownEvent escEvent2(&window, KeyCode::Escape, KeyModifier::None);
	window.HandleKeyDown(escEvent2);
	EXPECT_FALSE(window.HasActiveOverlay());

	window.Release();
}

// ── T42-11：模态浮层 Focus Trap 闭环与关闭后焦点恢复 ─────────────────
void TestOverlayFocusTrapAndRestore() {
	TestApp app;
	auto& window = app.Create("FocusTrap", 800, 600);

	auto baseBtn = std::make_unique<Button>("BaseFocusable");
	baseBtn->SetPosition(10, 10);
	baseBtn->SetSize(80, 40);
	Button* baseRaw = baseBtn.get();
	window.GetRootWidget().AddChild(std::move(baseBtn));
	window.SetFocusedWidget(baseRaw);
	EXPECT_EQ(window.GetFocusedWidget(), baseRaw);

	// 弹出包含子按钮的模态浮层
	auto panel = std::make_unique<Panel>();
	auto popBtn1 = std::make_unique<Button>("Pop1");
	auto popBtn2 = std::make_unique<Button>("Pop2");
	Button* raw1 = popBtn1.get();
	Button* raw2 = popBtn2.get();
	panel->AddChild(std::move(popBtn1));
	panel->AddChild(std::move(popBtn2));

	OverlayOptions opt;
	opt.layer = OverlayLayer::Modal;
	window.ShowOverlay(std::move(panel), opt);

	// 打开浮层时自动转焦到浮层内首个可聚焦控件
	EXPECT_EQ(window.GetFocusedWidget(), raw1);

	// Tab 导航 Focus Trap：焦点在浮层内 raw1 -> raw2 -> raw1 闭环循环，绝不逃逸到 baseRaw
	KeyDownEvent tabEvent(&window, KeyCode::Tab, KeyModifier::None);
	window.HandleKeyDown(tabEvent);
	EXPECT_EQ(window.GetFocusedWidget(), raw2);
	window.HandleKeyDown(tabEvent);
	EXPECT_EQ(window.GetFocusedWidget(), raw1);

	// 关闭浮层，焦点精确恢复为之前的 baseRaw
	window.HideOverlay(nullptr);
	EXPECT_EQ(window.GetFocusedWidget(), baseRaw);

	window.Release();
}

// ── T42-12：锚点跟随与屏幕边缘自适应翻转（Flip-then-Clamp）───────────
void TestOverlayAnchorFollowAndFlip() {
	TestApp app;
	auto& window = app.Create("AnchorFlip", 800, 600);

	// 锚点控件位于视口底部边缘 (100, 550, 100, 40)
	auto anchor = std::make_unique<Button>("Anchor");
	anchor->SetPosition(100, 550);
	anchor->SetSize(100, 40);
	Button* anchorRaw = anchor.get();
	window.GetRootWidget().AddChild(std::move(anchor));

	auto pop = std::make_unique<Button>("Pop");
	pop->SetSize(100, 100);
	Button* popRaw = pop.get();

	OverlayOptions opt;
	opt.anchorWidget = anchorRaw;
	opt.placement = AnchorPlacement::Below;
	window.ShowOverlay(std::move(pop), opt);

	// 下方超限 (550 + 40 + 100 = 690 > 600)，自动翻转为 Above (550 - 100 = 450)
	EXPECT_EQ(popRaw->GetY(), 450);
	EXPECT_EQ(popRaw->GetX(), 100);

	window.HideOverlay(popRaw);
	window.Release();
}

// ── T42-13：锚点控件脱树时浮层自动安全关闭 ───────────────────────────
void TestOverlayDetachedAnchorAutoClose() {
	WindowProbe probe("ECDI_P42_AnchorDetach");
	probe.hwnd = ::FindWindowW(nullptr, L"ECDI_P42_AnchorDetach");
	EXPECT_TRUE(probe.hwnd != nullptr);
	if (probe.hwnd == nullptr) return;
	probe.ShowAndSettle();

	bool closed = false;
	auto anchor = std::make_unique<Button>("Anchor");
	Button* anchorRaw = anchor.get();
	probe.window->GetRootWidget().AddChild(std::move(anchor));

	auto pop = std::make_unique<Button>("Pop");
	OverlayOptions opt;
	opt.anchorWidget = anchorRaw;
	opt.onClosed = [&]{ closed = true; };
	probe.window->ShowOverlay(std::move(pop), opt);

	EXPECT_TRUE(probe.window->HasActiveOverlay());

	// 摘除锚点控件（保留 removed 避免立即析构，安全验证脱树生命周期）
	auto removed = probe.window->GetRootWidget().RemoveChild(anchorRaw);

	// 触发帧渲染管线，UpdateOverlayPositions 探测到 anchorWidget 脱树，自动触发关闭
	probe.PumpOneFrame();
	EXPECT_TRUE(closed);
	EXPECT_FALSE(probe.window->HasActiveOverlay());
}

} // namespace

void ECDI::Test::RegisterOverlayTests() {
	// ── 批 0 负对照组（产品码零变动时即成立）────────────────────────────
	GetTestRegistry().Add("Overlay.Negative_ZeroOverlayBitIdentical", &TestOverlayNegativeZeroOverlayBitIdentical);
	GetTestRegistry().Add("Overlay.Negative_ScrollViewClipIntact", &TestOverlayNegativeScrollViewClipIntact);

	// ── 批 1 双根基础拓扑与命中同源 ───────────────────────────────────
	GetTestRegistry().Add("Overlay.Negative_FallbackToContentRoot", &TestOverlayNegativeFallbackToContentRoot);
	GetTestRegistry().Add("Overlay.HoverTrackingAcrossDualRoots", &TestOverlayHoverTrackingAcrossDualRoots);
	GetTestRegistry().Add("Overlay.TooltipLayerPassThrough", &TestOverlayTooltipLayerPassThrough);

	// ── 批 2 浮层生命周期、分层编排与结算管线 ─────────────────────────
	GetTestRegistry().Add("Overlay.EscapeScrollViewClip", &TestOverlayEscapeScrollViewClip);
	GetTestRegistry().Add("Overlay.ZeroMainLayoutSqueeze", &TestOverlayZeroMainLayoutSqueeze);
	GetTestRegistry().Add("Overlay.ZOrderHierarchy", &TestOverlayZOrderHierarchy);
	GetTestRegistry().Add("Overlay.NestedLifoAndCascadeClose", &TestOverlayNestedLifoAndCascadeClose);
	GetTestRegistry().Add("Overlay.Phase41SettleIntegration", &TestOverlayPhase41SettleIntegration);

	// ── 批 3 交互闭环：Light Dismiss、Focus Trap 与锚点跟随 ──────────────
	GetTestRegistry().Add("Overlay.LightDismissPolicies", &TestOverlayLightDismissPolicies);
	GetTestRegistry().Add("Overlay.ExemptWidgetClick", &TestOverlayExemptWidgetClick);
	GetTestRegistry().Add("Overlay.EscKeyDismiss", &TestOverlayEscKeyDismiss);
	GetTestRegistry().Add("Overlay.FocusTrapAndRestore", &TestOverlayFocusTrapAndRestore);
	GetTestRegistry().Add("Overlay.AnchorFollowAndFlip", &TestOverlayAnchorFollowAndFlip);
	GetTestRegistry().Add("Overlay.DetachedAnchorAutoClose", &TestOverlayDetachedAnchorAutoClose);
}
