#include "RunAllTests.h"
#include "TestFramework.h"
#include "WindowProbe.h"

#include "ECDI/Layout/HorizontalLayout.h"
#include "ECDI/Layout/Layout.h"
#include "ECDI/Layout/VerticalLayout.h"
#include "ECDI/Widget/Label.h"
#include "ECDI/Widget/ScrollBar.h"
#include "ECDI/Widget/TextBox.h"
#include "ECDI/Widget/Widget.h"
#include "ECDI/Window/Window.h"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

using namespace ECDI;
using namespace ECDI::Test;

// ══════════════════════════════════════════════════════════════════════
// ★ Phase 41（#79）：帧级布局 / 重绘失效结算 —— 用例组
// ══════════════════════════════════════════════════════════════════════
// 需求稿 v1.2 / 初设 v1.3 / 详设 v1.1 的验收编号 A41-1..A41-10。
// ★ 批次进度：批 0 = `CountingLayout` + T41-10（量具校准）✅ · 批 1 = T41-1/2/4/5/6/8/9 ✅ ·
//   批 2 = T41-3（`SetText` 只走重绘）/ T41-7（绘制期伪置脏免疫）+ `FindFirstBar` ✅。
//   批 0 的职责是「先证明量具能测」——否则批 1 一旦红，无法区分「机制错」与「量具错」
//   （Phase 40 的 T40-6 假红即此类）。
// ★ `FindFirstBar` 与批 2 用例**同时**落地（它唯一的消费者是 T41-7）：提前放入会产生
//   匿名 namespace 内的未使用函数（`-Wunused-function`）。

namespace {

/// @brief 计数装饰器：转发给真 Layout —— **只加计数、不改行为**
/// @details ★ 为什么不是「重写一个假 Layout（只 SetPosition）」：那会**改变布局结果**，让
///          A41-2「布局结果逐位不变」这条红线失去判据（Phase 40 I-3「量具必须与被测行为同构」同族）。
class CountingLayout : public Layout{

public:

	explicit CountingLayout(std::unique_ptr<Layout> inner) : m_inner(std::move(inner)) {}

	void Arrange(Widget& parent) override {

		++m_calls;
		m_inner->Arrange(parent);

	}

	[[nodiscard]] std::size_t Calls() const noexcept { return m_calls; }

private:

	std::unique_ptr<Layout> m_inner;

	std::size_t m_calls = 0;

};

// ── 装置：取 TextBox 的滚动条（批 2）────────────────────────────────

/// @brief 取 `TextBox` 的滚动条（`TextBox` 唯一的子节点就是条——`TextBox.cpp:44-47`「最后 AddChild」）
/// @details ★ 用 `dynamic_cast` 而不是按索引取：索引依赖「条是第几个子」，而「唯一子节点」是
///          **结构事实**留在了 `TextBox.cpp:44-47` 的注释里，语义更好读。
ScrollBar* FindFirstBar(Widget& parent) {

	for (std::size_t i = 0; i < parent.GetChildCount(); ++i) {

		if (auto* bar = dynamic_cast<ScrollBar*>(parent.GetChildAt(i))) {

			return bar;

		}

	}

	return nullptr;

}

// ── T41-10 量具校准（批 0）──────────────────────────────────────────/// @brief 同一棵树跑两遍：裸 `VerticalLayout` vs `CountingLayout(VerticalLayout)`
/// @details 两个判据一次钉死：
///   ① **计数有效**——包装后 `Calls()` 随 `Arrange()` 次数线性增长（否则 T41-4/6/7 的
///      「增量 == N」全是假绿：量具根本不数）；
///   ② **包装透明**——两棵树的子几何**逐位相同**（这是 A41-2 在装置层的保证）。
///   ★ 没有这条，后续所有计数类用例都可能一起假绿（同 `VisibilityRepaintTests` 的
///     「量具校准」纪律：判据本身不校准则判据无效）。
void TestT41CountingLayoutGauge() {

	auto build = [](Widget& root, std::unique_ptr<Layout> layout) {

		root.SetSize(200, 200);
		root.SetLayout(std::move(layout));

		for (int i = 0; i < 3; ++i) {

			auto child = std::make_unique<Widget>();
			child->SetSize(50, 20 + i * 10);
			root.AddChild(std::move(child));

		}

	};

	Widget bare;
	build(bare, std::make_unique<VerticalLayout>());

	Widget wrapped;
	auto counting = std::make_unique<CountingLayout>(std::make_unique<VerticalLayout>());
	CountingLayout* counter = counting.get();
	build(wrapped, std::move(counting));

	bare.Arrange();
	wrapped.Arrange();

	EXPECT_EQ(counter->Calls(), static_cast<std::size_t>(1));

	// ★ 逐位相同 —— A41-2「布局结果不变」在**装置层**的保证
	EXPECT_EQ(bare.GetChildCount(), wrapped.GetChildCount());

	for (std::size_t i = 0; i < bare.GetChildCount(); ++i) {

		const Widget* a = bare.GetChildAt(i);
		const Widget* b = wrapped.GetChildAt(i);

		EXPECT_EQ(a->GetX(), b->GetX());
		EXPECT_EQ(a->GetY(), b->GetY());
		EXPECT_EQ(a->GetWidth(), b->GetWidth());
		EXPECT_EQ(a->GetHeight(), b->GetHeight());

	}

	wrapped.Arrange();
	EXPECT_EQ(counter->Calls(), static_cast<std::size_t>(2));   // 计数随调用次数线性增长

}

// ── T41-1（A41-3）：只改可见性 ⇒ 下一帧自动重排，兄弟自动前移 ────────────────
void TestT41SetVisibleSchedulesArrange() {

	WindowProbe probe("ECDI_P41_Vis");
	probe.hwnd = ::FindWindowW(nullptr, L"ECDI_P41_Vis");
	EXPECT_TRUE(probe.hwnd != nullptr);
	if (probe.hwnd == nullptr) return;
	probe.ShowAndSettle();

	auto panelOwned = std::make_unique<Widget>();
	Widget* panel = panelOwned.get();
	panel->SetSize(200, 300);
	panel->SetLayout(std::make_unique<VerticalLayout>());

	Widget* buttons[3]{};

	for (int i = 0; i < 3; ++i) {

		auto b = std::make_unique<Widget>();
		b->SetSize(100, 40);
		buttons[i] = b.get();
		panel->AddChild(std::move(b));

	}

	probe.window->GetRootWidget().AddChild(std::move(panelOwned));
	probe.PumpOneFrame();     // 首帧结算：y = 0 / 40 / 80

	EXPECT_EQ(buttons[0]->GetY(), 0);
	EXPECT_EQ(buttons[1]->GetY(), 40);
	EXPECT_EQ(buttons[2]->GetY(), 80);

	// ★ 被测量：**只改可见性**，不写任何 Arrange（修复前：兄弟停在原处）
	buttons[1]->SetVisible(false);
	EXPECT_TRUE(probe.window->GetRootWidget().NeedsLayout());   // 布局通道已置脏（父有 Layout）

	probe.PumpOneFrame();

	EXPECT_EQ(buttons[2]->GetY(), 40);          // ★ 第三个自动前移（修复前停在 80）
	EXPECT_EQ(buttons[1]->GetX(), -100);        // C-VIS-4：停泊自身 bbox 负区
	EXPECT_EQ(buttons[1]->GetY(), -40);
	EXPECT_TRUE(!probe.window->GetRootWidget().NeedsLayout());

}

// ── T41-2（A41-4）：只改尺寸 ⇒ 下一帧自动重排，兄弟自动右移 ─────────────────
void TestT41SetSizeSchedulesArrange() {

	WindowProbe probe("ECDI_P41_Size2");
	probe.hwnd = ::FindWindowW(nullptr, L"ECDI_P41_Size2");
	EXPECT_TRUE(probe.hwnd != nullptr);
	if (probe.hwnd == nullptr) return;
	probe.ShowAndSettle();

	auto panelOwned = std::make_unique<Widget>();
	Widget* panel = panelOwned.get();
	panel->SetSize(300, 100);
	panel->SetLayout(std::make_unique<HorizontalLayout>());

	auto b0 = std::make_unique<Widget>();
	b0->SetSize(50, 20);
	Widget* left = b0.get();
	panel->AddChild(std::move(b0));

	auto b1 = std::make_unique<Widget>();
	b1->SetSize(100, 20);
	Widget* right = b1.get();
	panel->AddChild(std::move(b1));

	probe.window->GetRootWidget().AddChild(std::move(panelOwned));
	probe.PumpOneFrame();

	EXPECT_EQ(left->GetX(), 0);
	EXPECT_EQ(right->GetX(), 50);

	left->SetSize(80, 20);              // ★ 只改尺寸，不写 Arrange
	probe.PumpOneFrame();

	EXPECT_EQ(right->GetX(), 80);       // ★ 兄弟自动右移（修复前停在 50）

}

// ── T41-3（A41-5）：`SetText` 只走重绘通道，**不置布局脏**（D41-5 严格判据）────────
void TestT41SetTextRequestsRepaint() {

	WindowProbe probe("ECDI_P41_SetText");
	probe.hwnd = ::FindWindowW(nullptr, L"ECDI_P41_SetText");
	EXPECT_TRUE(probe.hwnd != nullptr);
	if (probe.hwnd == nullptr) return;
	probe.ShowAndSettle();

	// ★★ 门控前置（v1.1 订正，P1-2）：给**根**挂 Layout ⇒ `label->ParentHasLayout()` 为真。
	//   否则「不置脏」可能只是被 D41-4 门控拦下（**假绿**），而不是 D41-5「SetText 不走布局通道」。
	//   ——这条前置是本用例**唯一**让 D41-5 与 D41-4 可区分的手段。
	probe.window->GetRootWidget().SetLayout(std::make_unique<VerticalLayout>());

	auto owned = std::make_unique<Label>();
	Label* label = owned.get();
	label->SetSize(120, 24);
	probe.window->GetRootWidget().AddChild(std::move(owned));

	probe.PumpOneFrame();     // 结算建树期失效 ⇒ 根脏归零

	EXPECT_TRUE(!probe.window->GetRootWidget().NeedsLayout());

	// ── 左值重载
	const std::string text = "phase41";
	probe.ClearRegion();
	label->SetText(text);

	EXPECT_TRUE(probe.RegionDirty());                             // ★ 重绘通道（修复前：静默不刷新）
	EXPECT_TRUE(!probe.window->GetRootWidget().NeedsLayout());    // ★ 布局通道**不参与**（D41-5 严格判据）

	// ── 右值重载（★ 两个重载都要覆盖：D41-5 的落点是 `TextWidget.cpp` 的**两个**函数体）
	probe.ClearRegion();
	label->SetText(std::string("phase41-rv"));

	EXPECT_TRUE(probe.RegionDirty());
	EXPECT_TRUE(!probe.window->GetRootWidget().NeedsLayout());

}

// ── T41-4（A41-6）：空闲帧不重排（**有帧**但不脏）────────────────────────────
void TestT41IdleFramesDoNotArrange() {

	WindowProbe probe("ECDI_P41_Idle");
	probe.hwnd = ::FindWindowW(nullptr, L"ECDI_P41_Idle");
	EXPECT_TRUE(probe.hwnd != nullptr);
	if (probe.hwnd == nullptr) return;

	auto counting = std::make_unique<CountingLayout>(std::make_unique<VerticalLayout>());
	CountingLayout* counter = counting.get();
	probe.window->GetRootWidget().SetLayout(std::move(counting));

	for (int i = 0; i < 3; ++i) {

		auto child = std::make_unique<Widget>();
		child->SetSize(60, 30);
		probe.window->GetRootWidget().AddChild(std::move(child));

	}

	probe.ShowAndSettle();

	const std::size_t baseline = counter->Calls();
	EXPECT_TRUE(baseline >= 1);       // 前置：建树期的失效确实被结算过（否则「不增」无意义）

	for (int i = 0; i < 3; ++i) {

		probe.PumpOneFrame();         // ★ 真有帧（不是空泵——空泵一帧都跑不出）

	}

	EXPECT_EQ(counter->Calls(), baseline);   // ★ 空闲帧不重排

}

// ── T41-5（A41-7）：无窗口 ⇒ 置脏是纯内存写，一次都不结算 ───────────────────
void TestT41HeadlessSetDoesNotArrange() {

	Widget root;
	root.SetSize(200, 200);

	auto counting = std::make_unique<CountingLayout>(std::make_unique<VerticalLayout>());
	CountingLayout* counter = counting.get();
	root.SetLayout(std::move(counting));

	auto owned = std::make_unique<Widget>();
	Widget* child = owned.get();
	child->SetSize(50, 50);
	root.AddChild(std::move(owned));

	EXPECT_TRUE(root.NeedsLayout());          // AddChild ⇒ 置脏（父有 Layout）
	EXPECT_EQ(counter->Calls(), static_cast<std::size_t>(0));

	child->SetSize(60, 40);                   // 门控：父有 Layout ⇒ 置脏
	EXPECT_TRUE(root.NeedsLayout());
	EXPECT_EQ(counter->Calls(), static_cast<std::size_t>(0));   // ★ 无帧首 ⇒ 不结算

	child->SetVisible(false);                 // 同上
	EXPECT_TRUE(root.NeedsLayout());

	child->SetPosition(10, 10);               // 同上
	EXPECT_TRUE(root.NeedsLayout());

	// ★ 反向：没有 Layout 参与时，一律不置脏
	Widget leaf;
	leaf.SetSize(10, 10);
	EXPECT_TRUE(!leaf.NeedsLayout());         // 无父、无 Layout

	Widget orphan;                            // 构造了就必须断言，否则是死装置
	orphan.AddChild(std::make_unique<Widget>());
	EXPECT_TRUE(!orphan.NeedsLayout());       // 子集合变了，但 orphan 无 Layout ⇒ 不置脏

	EXPECT_EQ(counter->Calls(), static_cast<std::size_t>(0));

}

// ── T41-6（A41-8）：一次 WM_SIZE 恰一次 Arrange ─────────────────────────────
void TestT41ResizeArrangesOnce() {

	WindowProbe probe("ECDI_P41_Resize");
	probe.hwnd = ::FindWindowW(nullptr, L"ECDI_P41_Resize");
	EXPECT_TRUE(probe.hwnd != nullptr);
	if (probe.hwnd == nullptr) return;

	auto counting = std::make_unique<CountingLayout>(std::make_unique<VerticalLayout>());
	CountingLayout* counter = counting.get();
	probe.window->GetRootWidget().SetLayout(std::move(counting));

	auto child = std::make_unique<Widget>();
	child->SetSize(60, 30);
	probe.window->GetRootWidget().AddChild(std::move(child));

	probe.ShowAndSettle();

	const std::size_t baseline = counter->Calls();

	::SetWindowPos(probe.hwnd, nullptr, 0, 0, 520, 360, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	probe.PumpOneFrame();
	probe.PumpOneFrame();     // 再一帧：确保「残留脏」也会被暴露

	// ★ 判据：`OnResized` 同步 Arrange 并清脏 ⇒ 帧首不再排（若清脏作用域写错，这里会 ≥ 2）
	EXPECT_EQ(counter->Calls() - baseline, static_cast<std::size_t>(1));
	EXPECT_TRUE(!probe.window->GetRootWidget().NeedsLayout());

}

// ── T41-7（A41-9 + D41-4）：绘制期的条 toggle 不触发重排 ──────────────────────
void TestT41PaintTimeBarToggleDoesNotArrange() {

	WindowProbe probe("ECDI_P41_Bar");
	probe.hwnd = ::FindWindowW(nullptr, L"ECDI_P41_Bar");
	EXPECT_TRUE(probe.hwnd != nullptr);
	if (probe.hwnd == nullptr) return;

	// 装置：计数器挂在**根**上（TextBox 的直接父），条 toggle 的门控落在 TextBox 这一级
	auto counting = std::make_unique<CountingLayout>(std::make_unique<VerticalLayout>());
	CountingLayout* counter = counting.get();
	probe.window->GetRootWidget().SetLayout(std::move(counting));

	auto owned = std::make_unique<TextBox>();
	TextBox* box = owned.get();
	box->SetSize(160, 40);            // ★ 高度刻意压小：让 10 行内容必然溢出
	probe.window->GetRootWidget().AddChild(std::move(owned));

	probe.ShowAndSettle();

	ScrollBar* bar = FindFirstBar(*box);
	EXPECT_TRUE(bar != nullptr);
	if (bar == nullptr) return;

	// ── 段 0 前置校验：空文本 ⇒ 条已被首帧 SyncScrollBar 翻成**不可见**
	//    （默认 visible=true，不先确认「翻下去」，「翻上来」就无从判定）
	EXPECT_TRUE(!bar->IsVisible());

	const std::size_t baseline = counter->Calls();

	// ── 段 1 状态翻转校准 + 段 2 计数判据
	// ★★ v1.1 订正（P1-2）：**必须先 ClearRegion**——`ShowAndSettle` 弄脏的 region 在
	//   「无 BeginPaint」的平台层**永不 validate** ⇒ 不清则下面那条 RegionDirty 断言**恒真**（假绿）。
	probe.ClearRegion();

	box->SetText("1\n2\n3\n4\n5\n6\n7\n8\n9\n10");

	EXPECT_TRUE(probe.RegionDirty());          // SetText 的重绘通道（A41-5 同源）

	probe.PumpOneFrame();
	probe.PumpOneFrame();
	probe.PumpOneFrame();

	EXPECT_TRUE(bar->IsVisible());             // ★ 校准：条真的翻成可见（否则段 2 是假绿）
	EXPECT_EQ(counter->Calls(), baseline);     // ★ 判据：条 toggle 只走重绘通道

	// ── 段 3 正对照（同装置）：父**有** Layout 的可见性变化 ⇒ 必须多排一次
	box->SetVisible(false);
	EXPECT_TRUE(probe.window->GetRootWidget().NeedsLayout());
	probe.PumpOneFrame();
	EXPECT_EQ(counter->Calls() - baseline, static_cast<std::size_t>(1));

}

// ── T41-8（A41-10）：离线搭好的子树，挂入后首帧即排 ────────────────────────
void TestT41OfflineSubtreeArrangesOnAttach() {

	WindowProbe probe("ECDI_P41_Attach");
	probe.hwnd = ::FindWindowW(nullptr, L"ECDI_P41_Attach");
	EXPECT_TRUE(probe.hwnd != nullptr);
	if (probe.hwnd == nullptr) return;
	probe.ShowAndSettle();

	// ── 离线搭树：panel 是**临时根**，脏都写在它身上
	auto panelOwned = std::make_unique<Widget>();
	Widget* panel = panelOwned.get();
	panel->SetSize(200, 200);
	panel->SetLayout(std::make_unique<VerticalLayout>());

	auto b0 = std::make_unique<Widget>();
	b0->SetSize(100, 30);
	Widget* first = b0.get();
	panel->AddChild(std::move(b0));

	auto b1 = std::make_unique<Widget>();
	b1->SetSize(100, 50);
	Widget* second = b1.get();
	panel->AddChild(std::move(b1));

	EXPECT_TRUE(panel->NeedsLayout());    // 离线期：脏落在临时根上
	EXPECT_EQ(second->GetY(), 0);         // 尚未排过（口径锁定，证明下面不是「本来就对」）

	probe.window->GetRootWidget().AddChild(std::move(panelOwned));   // ★ 子树脏合并

	EXPECT_TRUE(probe.window->GetRootWidget().NeedsLayout());

	// ★★ **非根查询**——panel 自己的脏已被 AddChild 清成 false，只有**上溯到真根**才可能为 true
	//   ⇒ 这是 `NeedsLayout()` 里 `while` 循环的唯一覆盖点（缺它，写成 `return m_layoutDirty;`
	//   也能让全套用例全绿）。
	EXPECT_TRUE(panel->NeedsLayout());

	probe.PumpOneFrame();

	EXPECT_EQ(first->GetY(), 0);
	EXPECT_EQ(second->GetY(), 30);        // ★ 挂入后首帧即排（修复前停在 0）
	EXPECT_TRUE(!panel->NeedsLayout());   // 结算后非根同样读得到「已干净」

}

// ── T41-9（D41-3.2）：子树 Arrange 不清根脏；根 Arrange 才清 ────────────────
void TestT41SubtreeArrangeKeepsRootDirty() {

	// ★ 无窗口可测：`Arrange()` 是公开 API
	Widget root;
	root.SetSize(200, 200);

	auto childOwned = std::make_unique<Widget>();
	Widget* child = childOwned.get();
	child->SetSize(100, 100);
	child->SetLayout(std::make_unique<VerticalLayout>());
	root.AddChild(std::move(childOwned));

	EXPECT_TRUE(root.NeedsLayout());      // 置脏寄存在**根**
	EXPECT_TRUE(child->NeedsLayout());    // ★ 非根查询：child 自己的标志是 false，须**上溯**才为真

	child->Arrange();                     // ★ 子树 Arrange
	EXPECT_TRUE(root.NeedsLayout());      // ★ 不清（兄弟分支可能仍脏）——D41-3.2
	EXPECT_TRUE(child->NeedsLayout());    // 同上（子树 Arrange 不清根脏）

	root.Arrange();                       // ★ 根 Arrange
	EXPECT_TRUE(!root.NeedsLayout());     // 完成即清
	EXPECT_TRUE(!child->NeedsLayout());   // 非根同步读到「已干净」

}

}   // namespace

void ECDI::Test::RegisterLayoutInvalidationTests() {

	// ★ 按批次逐行打开（详设 §4）：批 0 只有量具，其余在批 1 / 批 2 施工时取消注释。
	GetTestRegistry().Add("Layout.CountingLayoutGauge", &TestT41CountingLayoutGauge);   // 批 0

	// ── 批 1（机制 + 契约 + 翻转）────────────────────────────────────
	GetTestRegistry().Add("Layout.SetVisibleSchedulesArrange",     &TestT41SetVisibleSchedulesArrange);
	GetTestRegistry().Add("Layout.SetSizeSchedulesArrange",        &TestT41SetSizeSchedulesArrange);
	GetTestRegistry().Add("Layout.IdleFramesDoNotArrange",         &TestT41IdleFramesDoNotArrange);
	GetTestRegistry().Add("Widget.HeadlessSetDoesNotArrange",      &TestT41HeadlessSetDoesNotArrange);
	GetTestRegistry().Add("Window.ResizeArrangesOnce",             &TestT41ResizeArrangesOnce);
	GetTestRegistry().Add("Widget.OfflineSubtreeArrangesOnAttach", &TestT41OfflineSubtreeArrangesOnAttach);
	GetTestRegistry().Add("Layout.SubtreeArrangeKeepsRootDirty",   &TestT41SubtreeArrangeKeepsRootDirty);

	// ── 批 2（同族收敛）──────────────────────────────────────────────
	GetTestRegistry().Add("Text.SetTextRequestsRepaint",             &TestT41SetTextRequestsRepaint);
	GetTestRegistry().Add("Layout.PaintTimeBarToggleDoesNotArrange", &TestT41PaintTimeBarToggleDoesNotArrange);

}
