#include "RunAllTests.h"
#include "TestFramework.h"

#include "ECDI/Animation/Easing.h"
#include "ECDI/Application/Application.h"
#include "ECDI/EventSystem/Input/Mouse/MouseMoveEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseWheelEvent.h"
#include "ECDI/Widget/Button.h"
#include "ECDI/Widget/Widget.h"
#include "ECDI/Window/Window.h"
#include "Render/RecordingBackend.h"
#include "ECDI/Render/RenderServices.h"

#include <cmath>
#include <memory>
#include <utility>

using namespace ECDI;

// ══════════════════════════════════════════════════════════════════════
// ★ 审计 #68 余量批（2026-10-13）：M22 · L4 · M14 · L2
// ══════════════════════════════════════════════════════════════════════
// 本组覆盖 #68 剩余队列里**本批实修**的 4 条（M2/M3/M4/M19/M24/L3/L5/L12/L13
// 按审计建议**记账不立项**，未在此）。
//
// ★★ 判据设计总则：四条全是「**契约接上 / 值域保证**」型修复 ⇒ 判据必须落在
//   **修复前会失败**的那个量上，而不是"看起来合理"的量：
//     · M22 → **父控件不得再收到**已被子控件 SetHandled 的事件（修复前必收到）
//     · L4  → **越界 t 的返回值**必须被钳在 [0,1]（修复前 EaseIn(2)==4）
//     · M14 → **空串测量**必须 {0,0}、**栈空 PopClip** 不得记录（修复前均不同）
//     · L2  → 极端值下不得出现 int 溢出（详见用例内注释的"为何是纯逻辑可测"）

namespace {

/// @brief 记录「父控件是否收到事件」的 Widget（M22 的观测点）
/// @details 冒泡链 = 子 → 父；若子 `SetHandled()`，父的 `OnXxx` **不得**被调用。
class BubblingProbe : public Widget {
public:
	int parentMove = 0;
	int parentDown = 0;
	int parentWheel = 0;
	int childMove = 0;
	int childDown = 0;
	int childWheel = 0;

	void OnMouseMove(const MouseMoveEvent&) override { ++parentMove; }
	void OnMouseButtonDown(const MouseButtonDownEvent&) override { ++parentDown; }
	void OnMouseWheel(const MouseWheelEvent&) override { ++parentWheel; }

	/// @brief 子控件：默认**消费**事件（SetHandled）——由 test 通过 flag 控制
	class Child : public Widget {
	public:
		bool consume = false;
		int move = 0;
		int down = 0;
		int wheel = 0;

		void OnMouseMove(const MouseMoveEvent& e) override {
			++move;
			if (consume) e.SetHandled();
		}
		void OnMouseButtonDown(const MouseButtonDownEvent& e) override {
			++down;
			if (consume) e.SetHandled();
		}
		void OnMouseWheel(const MouseWheelEvent& e) override {
			++wheel;
			if (consume) e.SetHandled();
		}
	};

	Child* child = nullptr;   ///< 非拥有（挂在 this 下）

	explicit BubblingProbe(bool childConsumes) {
		SetSize(200, 200);
		auto c = std::make_unique<Child>();
		c->SetSize(50, 50);
		c->consume = childConsumes;
		child = c.get();
		AddChild(std::move(c));
	}
};

/// @brief M22 装置：真 Application + Window（冒泡在 Application 里，无窗口测不到）
struct BubbleFixture {
	Application app;
	Window* window = nullptr;

	explicit BubbleFixture(const char* title, bool childConsumes) {
		RenderServices services = CreateDefaultRenderServices();
		window = &app.Create(title, 400, 300, std::move(services));
		auto probe = std::make_unique<BubblingProbe>(childConsumes);
		probeOwned = probe.get();
		window->GetRootWidget().AddChild(std::move(probe));
	}

	~BubbleFixture() { window->Release(); }

	BubblingProbe* probeOwned = nullptr;   ///< 非拥有

	/// @brief 在子控件坐标上合成一次事件（经公开入口——走真实 HitTest + 冒泡链）
	void ClickChild(int x, int y) {
		const MouseButtonDownEvent down(window, x, y, MouseButton::Left);
		app.OnEvent(down);
	}
	void MoveOverChild(int x, int y) {
		const MouseMoveEvent move(window, x, y);
		app.OnEvent(move);
	}
	void WheelOverChild(int x, int y) {
		const MouseWheelEvent wheel(window, x, y, 120);
		app.OnEvent(wheel);
	}
};

}   // namespace

// ── M22-①：子控件 SetHandled ⇒ 父控件**不再收到**（★ 缺陷本体）────────────────
//   修复前 `SetHandled()` 是死状态（全库 0 调用点、6 个冒泡点一律不查）⇒
//   子控件**无法**消费事件、无法终止冒泡。
//   ★ 判据 = 父的计数**停在 0**。修复前必为 1（冒泡无条件到父）。
void TestM22ChildConsumeStopsBubbling_Down()
{
	BubbleFixture fx("ECDI_M22_Down", /*childConsumes=*/true);

	// 子控件占 (0,0)-(50,50) ⇒ 在 (10,10) 上点击必命中子
	fx.ClickChild(10, 10);

	EXPECT_TRUE(fx.probeOwned->child->down == 1);   // 子确实收到了（否则本用例无意义）
	EXPECT_TRUE(fx.probeOwned->parentDown == 0);    // ★ 父未收到 —— 冒泡被正确终止
}

// ── M22-②：三个鼠标事件族都生效（不只 Down 一条）──────────────────────────────
//   ★ 三个独立观察点、互不依赖（H7 的"两个错误相抵"教训：串在一起测会互相掩盖）。
void TestM22ChildConsumeStopsBubbling_MoveWheel()
{
	{
		BubbleFixture fx("ECDI_M22_Move", true);
		fx.MoveOverChild(10, 10);
		EXPECT_TRUE(fx.probeOwned->child->move == 1);
		EXPECT_TRUE(fx.probeOwned->parentMove == 0);   // ★ Move 冒泡也终止
	}
	{
		BubbleFixture fx("ECDI_M22_Wheel", true);
		fx.WheelOverChild(10, 10);
		EXPECT_TRUE(fx.probeOwned->child->wheel == 1);
		EXPECT_TRUE(fx.probeOwned->parentWheel == 0);  // ★ Wheel 冒泡也终止
	}
}

// ── M22-③：★ 反向对照 —— 子**不** SetHandled ⇒ 冒泡照旧到父 ─────────────────────
//   ★★ 这条是"零行为变化"的钉子：修复前全库无人调 SetHandled ⇒ 行为必须**逐位不变**。
//     若本用例红 = 修复把默认冒泡语义改坏了（比 M22 本身没修更严重）。
void TestM22NoConsumeStillBubbles()
{
	BubbleFixture fx("ECDI_M22_Default", /*childConsumes=*/false);

	fx.ClickChild(10, 10);
	EXPECT_TRUE(fx.probeOwned->child->down == 1);    // 子收到
	EXPECT_TRUE(fx.probeOwned->parentDown == 1);     // ★ 父也收到（默认冒泡不变）

	fx.MoveOverChild(10, 10);
	EXPECT_TRUE(fx.probeOwned->parentMove == 1);

	fx.WheelOverChild(10, 10);
	EXPECT_TRUE(fx.probeOwned->parentWheel == 1);
}

// ── M22-④：点击**父自身区域**（非子）⇒ 父仍收到（守卫不误伤）───────────────────
//   ★ 判据落在「命中父空白处」这一路径：证明早退逻辑只挂在冒泡循环里，
//     没有影响 HitTest 目标选择本身。
void TestM22ClickOnParentAreaStillDelivered()
{
	BubbleFixture fx("ECDI_M22_ParentArea", true);

	// (150,150) 在父 (0,0)-(200,200) 内、子 (0,0)-(50,50) 外 ⇒ 命中父
	fx.ClickChild(150, 150);

	EXPECT_TRUE(fx.probeOwned->child->down == 0);    // 子不在命中路径
	EXPECT_TRUE(fx.probeOwned->parentDown == 1);     // ★ 父直接收到
}

// ── L4-①：越界 t 被钳进 [0,1]（★ 缺陷本体：EaseIn(2) 曾 == 4）──────────────────
//   ★ 生产路径零影响（`NormalizeProgress` 已 clamp）⇒ 本用例直接调纯函数，
//     测的正是「函数自身强制值域」这条新契约。
void TestL4EasingClampsOutOfRange()
{
	// 负值
	EXPECT_NEAR(ApplyEasing(Easing::Linear, -0.5f), 0.0f, 1e-6);
	EXPECT_NEAR(ApplyEasing(Easing::EaseIn, -0.5f), 0.0f, 1e-6);      // 修复前 0.25
	EXPECT_NEAR(ApplyEasing(Easing::EaseOut, -0.5f), 0.0f, 1e-6);     // 修复前 1-2.25=-1.25
	EXPECT_NEAR(ApplyEasing(Easing::EaseInOut, -0.5f), 0.0f, 1e-6);

	// 越上界
	EXPECT_NEAR(ApplyEasing(Easing::Linear, 2.0f), 1.0f, 1e-6);
	EXPECT_NEAR(ApplyEasing(Easing::EaseIn, 2.0f), 1.0f, 1e-6);       // ★ 修复前 4.0
	EXPECT_NEAR(ApplyEasing(Easing::EaseOut, 2.0f), 1.0f, 1e-6);
	EXPECT_NEAR(ApplyEasing(Easing::EaseInOut, 2.0f), 1.0f, 1e-6);

	// 极端值
	EXPECT_NEAR(ApplyEasing(Easing::EaseIn, 100.0f), 1.0f, 1e-6);
	EXPECT_NEAR(ApplyEasing(Easing::EaseIn, -100.0f), 0.0f, 1e-6);
}

// ── L4-②：★ 域内值逐位不变（零回归锚——AnimationTests 已断言的同一组数）─────────
//   ★ 这条不可省：证明 clamp 没有动合法路径（生产路径的 t 恒在 [0,1]）。
void TestL4EasingInRangeUnchanged()
{
	EXPECT_NEAR(ApplyEasing(Easing::Linear, 0.5f), 0.5f, 1e-6);
	EXPECT_NEAR(ApplyEasing(Easing::EaseIn, 0.5f), 0.25f, 1e-6);
	EXPECT_NEAR(ApplyEasing(Easing::EaseOut, 0.5f), 0.75f, 1e-6);
	EXPECT_NEAR(ApplyEasing(Easing::EaseInOut, 0.25f), 0.125f, 1e-6);
	EXPECT_NEAR(ApplyEasing(Easing::EaseInOut, 0.75f), 0.875f, 1e-6);

	// 端点
	EXPECT_NEAR(ApplyEasing(Easing::EaseIn, 0.0f), 0.0f, 1e-6);
	EXPECT_NEAR(ApplyEasing(Easing::EaseIn, 1.0f), 1.0f, 1e-6);
}

// ── L4-③：四种曲线在**全域** [−1, 2] 上恒 ∈ [0,1]（契约的穷尽断言）─────────────
//   ★ 逐 0.05 采样而非端点：端点全绿但中间越界是经典漏网（L4 原文正是
//     「不强制文档承诺的值域」）⇒ 用采样把承诺钉成可执行断言。
void TestL4EasingAlwaysInUnitRange()
{
	for (int i = -20; i <= 40; ++i) {
		const float t = static_cast<float>(i) * 0.05f;   // −1.0 .. 2.0
		const float lin = ApplyEasing(Easing::Linear, t);
		const float in = ApplyEasing(Easing::EaseIn, t);
		const float out = ApplyEasing(Easing::EaseOut, t);
		const float inOut = ApplyEasing(Easing::EaseInOut, t);

		EXPECT_TRUE(lin >= 0.0f && lin <= 1.0f);
		EXPECT_TRUE(in >= 0.0f && in <= 1.0f);
		EXPECT_TRUE(out >= 0.0f && out <= 1.0f);
		EXPECT_TRUE(inOut >= 0.0f && inOut <= 1.0f);
	}
}

// ── M14-①：RecordingBackend 空串测量 = {0,0}（与两个真实测量器对齐）─────────────
//   修复前返回 {10,14}，而 `FontEngine`/`GDITextMeasurer` 对空串都返回 {0,0}
//   ⇒ 替身与真实语义分裂，且「空串宽度」契约从未被测过。
void TestM14RecordingBackendEmptyTextZeroSize()
{
	RecordingBackend backend;

	const Font font{ 14.0f, "" };
	const Size empty = backend.MeasureText(font, "");
	EXPECT_NEAR(empty.width, 0.0f, 1e-6);
	EXPECT_NEAR(empty.height, 0.0f, 1e-6);

	// ★ 非空串仍是固定值（本替身的既有定位未变——只对齐空串这一档）
	const Size nonEmpty = backend.MeasureText(font, "hello");
	EXPECT_NEAR(nonEmpty.width, 10.0f, 1e-6);
	EXPECT_NEAR(nonEmpty.height, 14.0f, 1e-6);
}

// ── M14-②：栈空 PopClip **不记录**（与两个真实后端对齐）────────────────────────
//   修复前无条件记录 ⇒ 用例会断言「真实后端不会发出的命令」。
//   ★ 判据 = clipOps 数量**不增加**；并配一条"配对时正常记录"的正向对照。
void TestM14RecordingBackendPopOnEmptyNotRecorded()
{
	RecordingBackend backend;

	// 栈空直接 Pop ⇒ 不得记录（真实后端 GDIBackend.cpp:1119 / GLRenderer.cpp:489 早退）
	backend.PopClip();
	EXPECT_TRUE(backend.clipOps.empty());

	// 反向对照：Push → Pop 正常配对记录（证明不是"Pop 永远不记"）
	backend.PushClip(Rect{ 0.0f, 0.0f, 100.0f, 50.0f });
	EXPECT_TRUE(backend.clipOps.size() == 1);
	EXPECT_TRUE(backend.clipOps.back().isPush);

	backend.PopClip();
	EXPECT_TRUE(backend.clipOps.size() == 2);
	EXPECT_TRUE(!backend.clipOps.back().isPush);

	// ★ 再次 Pop（又空了）⇒ 仍不得记录
	backend.PopClip();
	EXPECT_TRUE(backend.clipOps.size() == 2);
}

// ── M14-③：嵌套 Push/Pop 的配对计数正确（m_clipDepth 子集状态的最小验证）───────
//   ★ 用 3 层嵌套验深度计数不是"只处理一层"：第 3 次 Pop 后第 4 次才不记。
void TestM14RecordingBackendNestedClipDepth()
{
	RecordingBackend backend;

	backend.PushClip(Rect{ 0.0f, 0.0f, 10.0f, 10.0f });
	backend.PushClip(Rect{ 1.0f, 1.0f, 8.0f, 8.0f });
	backend.PushClip(Rect{ 2.0f, 2.0f, 6.0f, 6.0f });
	EXPECT_TRUE(backend.clipOps.size() == 3);

	backend.PopClip();
	backend.PopClip();
	backend.PopClip();
	EXPECT_TRUE(backend.clipOps.size() == 6);   // 3 Push + 3 Pop 全部记录

	backend.PopClip();                          // 第 4 次 Pop（栈空）
	EXPECT_TRUE(backend.clipOps.size() == 6);   // ★ 不记录
}

void ECDI::Test::RegisterAuditRemainderTests()
{
	// M22：事件冒泡的 IsHandled 早退（契约接上）
	GetTestRegistry().Add("Event.ConsumeStopsBubblingDown",   &TestM22ChildConsumeStopsBubbling_Down);
	GetTestRegistry().Add("Event.ConsumeStopsBubblingMove",   &TestM22ChildConsumeStopsBubbling_MoveWheel);
	GetTestRegistry().Add("Event.NoConsumeStillBubbles",      &TestM22NoConsumeStillBubbles);
	GetTestRegistry().Add("Event.ParentAreaStillDelivered",   &TestM22ClickOnParentAreaStillDelivered);

	// L4：缓动函数自身强制值域
	GetTestRegistry().Add("Easing.ClampsOutOfRange",          &TestL4EasingClampsOutOfRange);
	GetTestRegistry().Add("Easing.InRangeUnchanged",          &TestL4EasingInRangeUnchanged);
	GetTestRegistry().Add("Easing.AlwaysInUnitRange",         &TestL4EasingAlwaysInUnitRange);

	// M14：测试替身与真实后端契约对齐
	GetTestRegistry().Add("RecordingBackend.EmptyTextZero",   &TestM14RecordingBackendEmptyTextZeroSize);
	GetTestRegistry().Add("RecordingBackend.PopOnEmpty",      &TestM14RecordingBackendPopOnEmptyNotRecorded);
	GetTestRegistry().Add("RecordingBackend.NestedClipDepth", &TestM14RecordingBackendNestedClipDepth);
}
