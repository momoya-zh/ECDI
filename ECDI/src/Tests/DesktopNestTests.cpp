#include "RunAllTests.h"
#include "TestFramework.h"

#include "../../examples/DesktopNest/BoxModel.h"        // 2026-10-09：demo 移 examples/；测试文件留框架侧
#include "../../examples/DesktopNest/BoxView.h"
#include "../../examples/DesktopNest/BoxWindow.h"
#include "../../examples/DesktopNest/DesktopNest.h"
#include "../../examples/DesktopNest/MemberListPopup.h"

#include "ECDI/Application/Application.h"
#include "ECDI/Core/Rect.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include "ECDI/Window/Window.h"
#include "Platform/Win32/Win32PlatformWindow.h"   // GetHwndForTests（内部头——seam 不出实现层）

#include <Windows.h>   // SendMessageW / WM_CLOSE / WM_LBUTTONDOWN

#ifdef DrawText
#undef DrawText
#endif

#include <string>
#include <vector>

using namespace ECDI;
using namespace ECDI::Test;   // ★ 本项目测试文件惯例（先例 ModelProbeTests.cpp 同款）
using namespace ECDI::DesktopNest;

namespace {

/// @brief 浮点断言容差（几何量为 float——DIP 整数运算下误差应为 0，容差仅作舍入余量）
constexpr double kFloatEps = 0.001;

// ══════════════════════════════════════════════════════════════════
// M1：DesktopNest —— T-M1-1..T-M1-6（详设 §7.2 判据）
// 装置三层（详设 §7.1）：
//   D1 无头数据层  = BoxModel 直接构造
//   D2 无头树层    = 裸 Widget 测试根 + BoxView::Assemble + 合成事件直调
//   D3 真实窗口层  = DesktopNestApp::BuildFixture(false)（隐藏窗）+ GetHwndForTests + SendMessage
// ══════════════════════════════════════════════════════════════════

/// @brief D2 无头树宿主根（BoxView::Assemble 的 hostRoot——无 Window）
struct HeadlessHost {
	Widget root;

	HeadlessHost(){
		root.SetSize(Metrics::kWindowWidth, Metrics::kWindowHeight);
	}
};

/// @brief 通知计数器（每订阅者独立；每次操作前清零——T-M1-2 计数口径）
/// @details ★ 派发是**广播**（BoxModel::Notify 遍历全部 handler——C-M1-8② 快照派发），
///          「只认自己」由**消费者**（BoxView::OnModelChanged 的 `box != m_box`）过滤。
///          本计数器同款：只统计 `box == watch` 的变更（watch < 0 = 不过滤，统计全部）。
///          ⇒ 断言写的「某框收到几次」与生产路径的语义一致（T-M1-1② / T-M1-3①）。
struct ChangeCounter {
	BoxId watch = -1;   ///< 只统计该框的变更（< 0 = 不过滤）

	int collected = 0;
	int absorbed = 0;
	int memberDetached = 0;
	int released = 0;
	int activated = 0;
	int total = 0;

	void Reset(){
		collected = absorbed = memberDetached = released = activated = total = 0;
	}

	void operator()(BoxId box, ModelChange change){
		if (watch >= 0 && box != watch){
			return;
		}

		++total;
		switch (change){
		case ModelChange::Collected:      ++collected; break;
		case ModelChange::Absorbed:       ++absorbed; break;
		case ModelChange::MemberDetached: ++memberDetached; break;
		case ModelChange::Released:       ++released; break;
		case ModelChange::Activated:      ++activated; break;
		}
	}
};

/// @brief DIP → 物理像素（装置坐标契约——平台入站把物理像素折 DIP）
int ToPhysical(int dip, float scale){
	return static_cast<int>(static_cast<float>(dip) * scale + 0.5f);
}

/// @brief 向窗口合成一次鼠标左键按下 + 抬起（真实消息路径——D3）
void ClickAt(HWND hwnd, int dipX, int dipY, float scale){
	const LPARAM lparam = MAKELPARAM(ToPhysical(dipX, scale), ToPhysical(dipY, scale));

	::SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lparam);
	::SendMessageW(hwnd, WM_LBUTTONUP, 0, lparam);
}

/// @brief 取框窗口的 HWND（D3 装置缝——未开窗返回 nullptr）
HWND HwndOf(DesktopNestApp& app, BoxId box){
	Window* window = app.GetEntry(box).window->GetWindow();

	if (window == nullptr){
		return nullptr;
	}

	return static_cast<Win32PlatformWindow&>(window->GetPlatformWindow()).GetHwndForTests();
}

// ── T-M1-1 收编：双方通知 + 派发期注销 + 徽标 ─────────────────────────

void TestM1ModelCollectNotifiesBoth(){

	BoxModel model;

	const BoxId a = model.AddBox("A");
	const BoxId b = model.AddBox("B");
	const BoxId c = model.AddBox("C");

	ChangeCounter counterA;
	ChangeCounter counterB;
	ChangeCounter counterC;

	// 按生产语义过滤（BoxView 只认自己的 box——C-M1-13）
	counterA.watch = a;
	counterB.watch = b;
	counterC.watch = c;

	model.AddHandler([&counterA](BoxId box, ModelChange change){ counterA(box, change); });
	model.AddHandler([&counterB](BoxId box, ModelChange change){ counterB(box, change); });
	const BoxModel::Subscription subC =
		model.AddHandler([&counterC](BoxId box, ModelChange change){ counterC(box, change); });

	// ① 数据断言
	model.Collect(a, b, Rect{ 100.0f, 100.0f, 220.0f, 320.0f });

	EXPECT_EQ(model.GetBox(a).members.size(), static_cast<std::size_t>(1));
	EXPECT_EQ(model.GetBox(a).members[0], b);
	EXPECT_EQ(static_cast<int>(model.GetBox(b).state), static_cast<int>(BoxState::Merged));
	EXPECT_NEAR(model.GetBox(b).placement.x, 100.0f, kFloatEps);
	EXPECT_NEAR(model.GetBox(b).placement.width, 220.0f, kFloatEps);

	// ★ E-2 回归锚（实施勘误 v1.4）：**首次**收编须同时确定 active——
	//   否则 members 非空而 active == -1 ⇒ 派生标题退化、Detach 前置失败。
	//   用独立模型作用域验证（不扰动下方流程的状态）。
	{
		BoxModel probe;
		const BoxId p = probe.AddBox("P");
		const BoxId q = probe.AddBox("Q");
		const BoxId r = probe.AddBox("R");

		EXPECT_EQ(probe.GetBox(p).active, -1);

		probe.Collect(p, q, Rect{});
		EXPECT_EQ(probe.GetBox(p).active, q);   // 首次收编确定 active

		probe.Collect(p, r, Rect{});
		EXPECT_EQ(probe.GetBox(p).active, q);   // 已有 active 保持（用户当前选中项）
		EXPECT_EQ(probe.GetBox(p).members.size(), static_cast<std::size_t>(2));

		probe.Detach(p);                        // active = q 被拆出
		EXPECT_EQ(probe.GetBox(p).members.size(), static_cast<std::size_t>(1));
		EXPECT_EQ(probe.GetBox(p).members[0], r);
		EXPECT_EQ(probe.GetBox(p).active, r);   // active 保有效指向
		EXPECT_EQ(static_cast<int>(probe.GetBox(q).state), static_cast<int>(BoxState::TopLevel));
	}

	// ② 通知次数（按生产语义过滤：每个消费者只认自己的 box——C-M1-13）
	EXPECT_EQ(counterA.collected, 1);   // A 侧：收编方
	EXPECT_EQ(counterA.absorbed, 0);
	EXPECT_EQ(counterB.absorbed, 1);    // B 侧：被收编方
	EXPECT_EQ(counterB.collected, 0);
	EXPECT_EQ(counterC.total, 0);       // C 侧：与自己无关（过滤器生效）

	// ③ 派发期注销实证：A 的 handler 内注销 C 的订阅 ⇒ C 本轮仍收到、后续不再收到
	model.RemoveHandler(subC);

	counterA.Reset();
	counterB.Reset();
	counterC.Reset();

	model.SetActive(a, b);

	EXPECT_EQ(counterA.activated, 1);
	EXPECT_EQ(counterC.total, 0);   // 已注销——后续不再通知

	// ④ 订阅边界（T-M1-1⑤）：派发期新注册者本轮不收到、下一轮起收到。
	// ★ 装置注记：用「注册时机标志」而非静态局部——每轮操作前显式重置（可重入、无跨用例状态）。
	ChangeCounter late;
	ChangeCounter trigger;
	bool registerDuringDispatch = false;
	bool lateRegistered = false;

	model.AddHandler([&trigger](BoxId box, ModelChange change){ trigger(box, change); });
	model.AddHandler([&late, &model, &registerDuringDispatch, &lateRegistered]
	                 (BoxId box, ModelChange change){
		late(box, change);

		if (registerDuringDispatch && !lateRegistered){

			lateRegistered = true;

			// 派发期注册的「迟到者」——本轮不得收到（快照派发语义）
			model.AddHandler([&late](BoxId lateBox, ModelChange lateChange){ late(lateBox, lateChange); });
		}
	});

	// 第一轮：触发派发，期间注册迟到者
	trigger.Reset();
	late.Reset();
	registerDuringDispatch = true;

	model.SetActive(a, b);

	EXPECT_EQ(trigger.activated, 1);
	EXPECT_EQ(late.activated, 1);   // 迟到者本轮不收到（late 计数仍为 1 = 只有外层 handler）

	// 第二轮：迟到者已注册（本轮起生效）
	registerDuringDispatch = false;
	trigger.Reset();
	late.Reset();

	model.SetActive(a, b);

	EXPECT_EQ(trigger.activated, 1);
	EXPECT_EQ(late.activated, 2);   // 外层 handler + 迟到者（下轮起收到）

	// ⑤ D2 无头树：Assemble 后入口按钮徽标 == members.size()
	{
		BoxModel treeModel;
		const BoxId t = treeModel.AddBox("T");
		const BoxId m1 = treeModel.AddBox("M1");
		const BoxId m2 = treeModel.AddBox("M2");

		treeModel.Collect(t, m1, Rect{});
		treeModel.Collect(t, m2, Rect{});

		HeadlessHost host;
		BoxView view(treeModel, t);

		view.Assemble(host.root);

		EXPECT_EQ(view.GetBadgeCount(), 2);
		EXPECT_EQ(view.GetTitleText(), std::string("M1"));   // 派生标题 = active 成员（C-M1-17）

		view.Disassemble();
	}
}

// ── T-M1-2 拆出：成员恢复 + 级联 + 流程级 + 循环 ─────────────────────

void TestM1ModelDetachRestoresMembership(){

	// ① 数据层
	{
		BoxModel model;
		const BoxId c = model.AddMergedBox("C", { "C1", "C2" });

		EXPECT_TRUE(model.CanDetach(c));

		const BoxId c1 = model.GetBox(c).members[0];
		const BoxId c2 = model.GetBox(c).members[1];

		model.Detach(c);

		EXPECT_EQ(model.GetBox(c).members.size(), static_cast<std::size_t>(1));
		EXPECT_EQ(model.GetBox(c).members[0], c2);
		EXPECT_EQ(static_cast<int>(model.GetBox(c1).state), static_cast<int>(BoxState::TopLevel));
		EXPECT_FALSE(model.CanDetach(c));   // members.size() == 1
	}

	// ② 级联纯函数
	{
		const Rect cascaded = CascadedPlacement(Rect{ 100.0f, 100.0f, 220.0f, 320.0f });

		EXPECT_NEAR(cascaded.x, 128.0f, kFloatEps);
		EXPECT_NEAR(cascaded.y, 128.0f, kFloatEps);
		EXPECT_NEAR(cascaded.width, 220.0f, kFloatEps);
		EXPECT_NEAR(cascaded.height, 320.0f, kFloatEps);
	}

	// ③④⑤ D3：流程级——真调 DesktopNestApp::Detach + 收编↔拆出两轮循环
	{
		DesktopNestApp app;
		const DesktopNestApp::Fixture fixture = app.BuildFixture(false);

		// ③ BoxWindow::Create → GetBounds 读回（Phase 31 请求语义）
		const Rect boundsA = app.GetEntry(fixture.a).window->GetBounds();

		EXPECT_TRUE(app.GetEntry(fixture.a).window->IsOpen());
		EXPECT_NEAR(boundsA.width, static_cast<float>(Metrics::kWindowWidth), kFloatEps);

		// ④ 流程级拆出：C 的当前成员（C1）恢复独立 + 窗口重建 + 落点
		const BoxId member = app.GetModel().GetBox(fixture.c).active;

		app.Detach(fixture.c);

		EXPECT_EQ(static_cast<int>(app.GetModel().GetBox(member).state),
		          static_cast<int>(BoxState::TopLevel));
		EXPECT_TRUE(app.GetEntry(member).window->IsOpen());
		EXPECT_EQ(app.GetModel().GetBox(fixture.c).members.size(), static_cast<std::size_t>(1));

		const Rect memberBounds = app.GetEntry(member).window->GetBounds();

		EXPECT_NEAR(memberBounds.width, static_cast<float>(Metrics::kWindowWidth), kFloatEps);

		// ⑤ 收编↔拆出循环两轮（通知计数恒 1 = 无重复订阅）
		// ★ E-6 修正（2026-10-09，行为探针 + MSVC 实测两轮发现）：`Detach` 要求
		//   `members.size() > 1`，且被收编方必须是**独立框**（窗口打开——C-M1-22(f) 守卫）
		//   ⇒ 循环前先收编 **C 与 C1**（④ 拆出后二者均为独立框、窗口已重建）作常驻成员，
		//   此后每轮「收编 B → 拆出 B」，A 始终保有 2 个成员 ⇒ `CanDetach` 恒成立。
		//   计数器须设 watch（生产语义 = 只认自己的 box——C-M1-13）。
		const BoxId c1 = app.GetModel().GetBox(fixture.c).members[0];   // ④ 之后 C 仅剩 C2

		app.Collect(fixture.a, fixture.c);
		app.Collect(fixture.a, member);   // ④ 拆出的成员（现为独立框）

		EXPECT_TRUE(app.GetModel().CanDetach(fixture.a));   // 循环前置：A 有 2 个成员
		(void)c1;

		for (int round = 0; round < 2; ++round){

			ChangeCounter counterC;
			ChangeCounter counterB;
			ChangeCounter counterA;

			counterC.watch = fixture.c;
			counterB.watch = fixture.b;
			counterA.watch = fixture.a;

			const BoxModel::Subscription sC =
				app.GetModel().AddHandler([&counterC](BoxId box, ModelChange change){ counterC(box, change); });
			const BoxModel::Subscription sB =
				app.GetModel().AddHandler([&counterB](BoxId box, ModelChange change){ counterB(box, change); });
			const BoxModel::Subscription sA =
				app.GetModel().AddHandler([&counterA](BoxId box, ModelChange change){ counterA(box, change); });

			// 收编：把 B 收进 A（B 是独立框——上一轮已拆出）
			app.Collect(fixture.a, fixture.b);

			EXPECT_EQ(counterC.total, 0);      // 与 C 无关（过滤生效）
			EXPECT_EQ(counterB.absorbed, 1);   // 被收编方恰好一次（无重复订阅）
			EXPECT_EQ(counterA.collected, 1);  // 收编方恰好一次
			EXPECT_FALSE(app.GetEntry(fixture.b).window->IsOpen());

			counterB.Reset();
			counterA.Reset();

			EXPECT_TRUE(app.GetModel().CanDetach(fixture.a));   // 前置成立（确实拆得出）

			// 拆出：把 B 从 A 拆出（B 的 placement 有记忆 ⇒ 恢复）
			// ★ 选中走 Model（协调器只暴露 Collect/Detach——选中是视图动作，
			//   生产路径 = BoxView::SelectMember → Model::SetActive）
			app.GetModel().SetActive(fixture.a, fixture.b);
			app.Detach(fixture.a);

			EXPECT_EQ(counterB.released, 1);       // 被拆成员恰好一次
			EXPECT_TRUE(app.GetEntry(fixture.b).window->IsOpen());

			app.GetModel().RemoveHandler(sC);
			app.GetModel().RemoveHandler(sB);
			app.GetModel().RemoveHandler(sA);
		}
	}
}

// ── T-M1-3 切换：仅属主通知 + 内容/标题重建 ──────────────────────────

void TestM1ModelSetActiveNotifiesOnlyOwner(){

	BoxModel model;
	const BoxId c = model.AddMergedBox("C", { "C1", "C2" });

	const BoxId c1 = model.GetBox(c).members[0];
	const BoxId c2 = model.GetBox(c).members[1];

	ChangeCounter counterC;
	ChangeCounter counterC1;
	ChangeCounter counterC2;

	// 按生产语义过滤（每个消费者只认自己的 box——C-M1-13）
	counterC.watch = c;
	counterC1.watch = c1;
	counterC2.watch = c2;

	model.AddHandler([&counterC](BoxId box, ModelChange change){ counterC(box, change); });
	model.AddHandler([&counterC1](BoxId box, ModelChange change){ counterC1(box, change); });
	model.AddHandler([&counterC2](BoxId box, ModelChange change){ counterC2(box, change); });

	// ① 仅属主反应（C-M1-13）：`Activated` 只发给属主 c
	//    ⇒ watch==c 的计数器 +1；watch==c1/c2 的计数器为 0（过滤掉——非自己的变更）
	model.SetActive(c, c2);

	EXPECT_EQ(counterC.activated, 1);
	EXPECT_EQ(counterC1.total, 0);       // 成员框不因属主切换而被通知（过滤后）
	EXPECT_EQ(counterC2.total, 0);
	EXPECT_EQ(model.GetBox(c).active, c2);

	// ②③ D2 无头树：内容整体换人 + 派生标题
	HeadlessHost host;
	BoxView view(model, c);

	view.Assemble(host.root);

	EXPECT_EQ(view.GetTitleText(), std::string("C2"));   // ③ 派生标题（C-M1-17）

	CollapsiblePanel* panel = view.GetContentPanel();

	EXPECT_TRUE(panel != nullptr);

	Widget* content = panel->GetContent();

	EXPECT_TRUE(content != nullptr);
	EXPECT_TRUE(content->GetChildCount() > 0);

	// ② 内容行文本含 active 成员标题
	const auto* firstRow = static_cast<const TextWidget*>(content->GetChildAt(0));

	EXPECT_TRUE(firstRow->GetText().find("C2") != std::string::npos);

	view.Disassemble();
}

// ── T-M1-4 浮层纪律：判据 + 真实派发 + 重开 + 关闭路径 ───────────────

void TestM1PopupDiscipline(){

	// ①②③④ D2 无头树：判据（HitTest 不穿透 + 点外关闭）
	{
		BoxModel model;
		const BoxId a = model.AddBox("A");
		model.AddBox("B");

		HeadlessHost host;
		BoxView view(model, a);

		view.Assemble(host.root);

		Widget* root = view.GetRoot();

		EXPECT_TRUE(root != nullptr);

		// 浮层未开时：Down 不产生任何关闭动作（判据①）
		{
			const MouseButtonDownEvent event(nullptr, 150, 200, MouseButton::Left);

			root->OnMouseButtonDown(event);

			EXPECT_FALSE(view.IsPopupVisible());
		}

		// 开浮层
		view.OpenPopup();

		EXPECT_TRUE(view.IsPopupVisible());

		// ② 浮层外（内容区行）命中 ⇒ 不是浮层子树
		{
			Widget* hit = root->HitTest(150, 200);

			EXPECT_TRUE(hit != nullptr);
		}

		// ③ 浮层可见时 Down @ 内容区 ⇒ 浮层隐藏（判据⑤）
		{
			const MouseButtonDownEvent event(nullptr, 150, 200, MouseButton::Left);

			root->OnMouseButtonDown(event);

			EXPECT_FALSE(view.IsPopupVisible());
		}

		view.Disassemble();
	}

	// ⑤⑥⑦ D3 真实派发
	{
		DesktopNestApp app;
		const DesktopNestApp::Fixture fixture = app.BuildFixture(false);

		Window* windowA = app.GetEntry(fixture.a).window->GetWindow();

		EXPECT_TRUE(windowA != nullptr);

		const float scale = windowA->GetDpiScale();
		HWND hwndA = HwndOf(app, fixture.a);
		HWND hwndC = HwndOf(app, fixture.c);

		EXPECT_TRUE(hwndA != nullptr);
		EXPECT_TRUE(hwndC != nullptr);

		// ⑤ 入口按钮点击 ×2 = 开 → 关（P6 切换）
		ClickAt(hwndA, 56, 16, scale);

		EXPECT_TRUE(app.GetEntry(fixture.a).view->IsPopupVisible());

		ClickAt(hwndA, 56, 16, scale);

		EXPECT_FALSE(app.GetEntry(fixture.a).view->IsPopupVisible());

		// ⑤ 成员行点击 = 切换（C 的浮层；行未被销毁——派发安全 C-M1-19 R2）
		ClickAt(hwndC, 56, 16, scale);

		EXPECT_TRUE(app.GetEntry(fixture.c).view->IsPopupVisible());

		ClickAt(hwndC, 90, 72, scale);   // 成员行 1 中心（C2）

		EXPECT_EQ(app.GetModel().GetBox(fixture.c).active,
		          app.GetModel().GetBox(fixture.c).members[1]);
		EXPECT_TRUE(app.GetEntry(fixture.c).view->IsPopupVisible());   // 切换不关浮层

		// ⑥ 浮层重开生命周期（Refresh/Rebuild 分工）
		app.GetEntry(fixture.c).view->ClosePopup();
		app.GetEntry(fixture.c).view->OpenPopup();

		EXPECT_TRUE(app.GetEntry(fixture.c).view->IsPopupVisible());

		// ⑤ 「加入」行点击 = 收编全路径（A 的浮层 → 加入 B）
		ClickAt(hwndA, 56, 16, scale);   // 开 A 浮层

		EXPECT_TRUE(app.GetEntry(fixture.a).view->IsPopupVisible());

		// A 的加入行 0：成员数 n=0 ⇒ 实现布局 y = 52 + 24n + 24j（浮层局部），
		// 中心 = 52 + 12 = 64 ⇒ 客户区 y = kCaptionHeight + 64 = 96
		ClickAt(hwndA, 90, 96, scale);

		const BoxId source = app.GetModel().GetBox(fixture.b).state == BoxState::Merged
		                     ? fixture.b : -1;

		if (source >= 0){

			EXPECT_FALSE(app.GetEntry(fixture.b).window->IsOpen());   // 被收编框窗口关闭
			EXPECT_FALSE(app.GetEntry(fixture.a).view->IsPopupVisible());   // R3 先关浮层
		}

		// ⑦ 系统关闭路径（真实 WM_CLOSE；C-M1-22(c) 三项）
		{
			::SendMessageW(hwndC, WM_CLOSE, 0, 0);

			EXPECT_FALSE(app.GetEntry(fixture.c).window->IsOpen());
			EXPECT_TRUE(app.GetEntry(fixture.c).window->GetWindow() == nullptr);
			EXPECT_NEAR(app.GetEntry(fixture.c).window->GetBounds().width, 0.0f, kFloatEps);

			// 其余框不受影响
			EXPECT_TRUE(app.GetEntry(fixture.a).window->IsOpen());

			// 已关框不参与业务流程：安全拒绝 + 模型零改动
			const std::size_t membersBefore = app.GetModel().GetBox(fixture.c).members.size();

			app.Detach(fixture.c);

			EXPECT_EQ(app.GetModel().GetBox(fixture.c).members.size(), membersBefore);
		}
	}
}

// ── T-M1-5 折叠几何（真实窗 + 无头内容可见性）───────────────────────

void TestM1FoldGeometry(){

	// ① D3 真实窗：几何瞬时
	// ★ E-8（2026-10-09，用户侧实测 + MSVC 探针）：折叠目标**不是** kCaptionHeight——
	//   `GetBounds()` 是总尺寸（含边框）口径，而 K2 的「仅标题条」是客户区口径；
	//   且系统有**最小窗口高**（实测请求 32 → 读回 39 = 32 + 7 边框）。
	//   ⇒ 折叠目标 = `BoxWindow::CollapsedHeight()`（请求探测出的真实下限，方案 A）。
	{
		DesktopNestApp app;
		const DesktopNestApp::Fixture fixture = app.BuildFixture(false);

		BoxView& view = *app.GetEntry(fixture.a).view;
		BoxWindow& window = *app.GetEntry(fixture.a).window;

		const int collapsedHeight = window.CollapsedHeight();

		EXPECT_TRUE(collapsedHeight >= Metrics::kCaptionHeight);   // ≥ 标题条（含边框）
		EXPECT_TRUE(view.GetContentPanel()->IsExpanded());
		EXPECT_NEAR(window.GetBounds().height,
		            static_cast<float>(Metrics::kWindowHeight), kFloatEps);

		view.ToggleCollapse();

		EXPECT_FALSE(view.GetContentPanel()->IsExpanded());
		EXPECT_NEAR(window.GetBounds().height,
		            static_cast<float>(collapsedHeight), kFloatEps);

		view.ToggleCollapse();

		EXPECT_TRUE(view.GetContentPanel()->IsExpanded());
		EXPECT_NEAR(window.GetBounds().height,
		            static_cast<float>(Metrics::kWindowHeight), kFloatEps);
	}

	// ② D2 无头：CollapsiblePanel 无 Window ⇒ 降级瞬时切换（内容可见性同步）
	{
		BoxModel model;
		const BoxId a = model.AddBox("A");

		HeadlessHost host;
		BoxView view(model, a);

		view.Assemble(host.root);

		CollapsiblePanel* panel = view.GetContentPanel();

		EXPECT_TRUE(panel->IsExpanded());
		EXPECT_TRUE(panel->GetContent()->IsVisible());

		view.ToggleCollapse();

		EXPECT_FALSE(panel->IsExpanded());
		EXPECT_FALSE(panel->GetContent()->IsVisible());

		view.ToggleCollapse();

		EXPECT_TRUE(panel->IsExpanded());
		EXPECT_TRUE(panel->GetContent()->IsVisible());

		view.Disassemble();
	}
}

// ── T-M1-6 拖动路由（命中测试——非实际拖动）──────────────────────────
// ★ 实施勘误（2026-10-09）：详设原写 `Window::IsClientInteractiveAt`，实测该方法在
//   `Window` 上是 **private**（Window.h:295——它实现 PlatformWindowHost 契约）。
//   公开入口 = 经基类引用调用：`Window : public PlatformWindowHost`（Window.h:44），
//   而该虚函数在基类是 **public 纯虚**（PlatformWindowHost.h:64）。
void TestM1DragRoute(){

	DesktopNestApp app;
	const DesktopNestApp::Fixture fixture = app.BuildFixture(false);

	Window* window = app.GetEntry(fixture.a).window->GetWindow();

	EXPECT_TRUE(window != nullptr);

	// 经基类接口调用（public 纯虚——平台层公共接缝）
	const PlatformWindowHost& host = *window;

	// 标题文本上 ⇒ false（caption 区返回 HTCAPTION——可拖动）
	EXPECT_FALSE(host.IsClientInteractiveAt(150, 16));

	// 入口按钮 / 折叠按钮 ⇒ true（HTCLIENT——按钮可点，不被拖动吃掉）
	EXPECT_TRUE(host.IsClientInteractiveAt(56, 16));
	EXPECT_TRUE(host.IsClientInteractiveAt(204, 16));
}

}   // namespace

// ★ 定义须**全限定**（`ECDI::Test::`）——声明在 RunAllTests.h 的 `ECDI::Test` 命名空间内；
//   写成全局作用域会定义出 `::RegisterDesktopNestTests`（另一个符号）⇒ 链接期 LNK2019
//   （本项目 32/33 个先例同款；`-fsyntax-only` 不解析符号 ⇒ 必须做链接级验证才发现）。
void ECDI::Test::RegisterDesktopNestTests(){

	TestRegistry& registry = GetTestRegistry();

	registry.Add("DesktopNest.ModelCollectNotifiesBoth", &TestM1ModelCollectNotifiesBoth);
	registry.Add("DesktopNest.ModelDetachRestoresMembership", &TestM1ModelDetachRestoresMembership);
	registry.Add("DesktopNest.ModelSetActiveNotifiesOnlyOwner", &TestM1ModelSetActiveNotifiesOnlyOwner);
	registry.Add("DesktopNest.PopupDiscipline", &TestM1PopupDiscipline);
	registry.Add("DesktopNest.FoldGeometry", &TestM1FoldGeometry);
	registry.Add("DesktopNest.DragRoute", &TestM1DragRoute);
}
