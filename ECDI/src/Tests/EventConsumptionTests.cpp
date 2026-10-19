#include "RunAllTests.h"
#include "TestFramework.h"

#include "ECDI/Application/Application.h"
#include "ECDI/Widget/Widget.h"
#include "ECDI/Widget/Button.h"
#include "ECDI/Render/RenderServices.h"
#include "ECDI/Render/PaintContext.h"
#include "ECDI/Render/RenderCommand.h"
#include "Render/RecordingBackend.h"

#include "ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonUpEvent.h"
#include "ECDI/EventSystem/Input/KeyBoard/KeyDownEvent.h"

#include <functional>
#include <memory>
#include <string>
#include <variant>
#include <vector>

// ══════════════════════════════════════════════════════════════════════
// ★★ Phase 35：事件消费 + 嵌套点击 + 派发期安全销毁（回归组）
// ══════════════════════════════════════════════════════════════════════
// 需求稿（ECDI_fake_harness）验收 1..5 —— 语义见 docs/规范/事件消费与安全销毁规范.md。
// 本组走**生产路径**：`Application::OnEvent(MouseButtonDownEvent/UpEvent)`
//   → HitTest → 冻结链 → 冒泡（`Event::IsHandled()` 即停）⇒ 测的是真实行为。
// ⚠️ 测试卫生：每个用例自建 Application + Window（真实 HWND、不 Show），结束时 Release()；
//   直接调 `OnEvent`（Win32 消息泵不入循环）⇒ 不依赖 `Run()`。

using namespace ECDI;

namespace{

// ── 观测型控件 ──────────────────────────────────────────────────────

/// @brief 记录派发轨迹的可点击容器（**不**消费——用来验证「未消费则冒泡」）
class RecordingBox: public Widget{

public:

	std::string name;              ///< 本节点在轨迹里的名字
	std::vector<std::string>* log = nullptr;   ///< 轨迹收集器（探针生命周期内有效）

	int downCount = 0;

	int upCount = 0;

	bool consumeDown = false;      ///< 按下阶段是否消费（F2③ 对称性被试）

	bool consumeUp = false;        ///< 释放阶段是否消费

	void OnMouseButtonDown(const MouseButtonDownEvent& event) override{

		++downCount;

		if (log){ log->push_back(name + ":down"); }

		if (consumeDown){ event.SetHandled(); }

	}

	void OnMouseButtonUp(const MouseButtonUpEvent& event) override{

		++upCount;

		if (log){ log->push_back(name + ":up"); }

		if (consumeUp){ event.SetHandled(); }

	}

};

/// @brief 记录式按钮：先记轨迹、再走 Button 既有逻辑（Button 自己消费）
class RecordingButton: public Button{

public:

	std::string name;

	std::vector<std::string>* log;

	int downCount = 0;

	int upCount = 0;

	RecordingButton(const std::string& text, const std::string& n, std::vector<std::string>* l)
		: Button(text), name(n), log(l){}

	void OnMouseButtonDown(const MouseButtonDownEvent& event) override{

		++downCount;

		if (log){ log->push_back(name + ":down"); }

		Button::OnMouseButtonDown(event);

	}

	void OnMouseButtonUp(const MouseButtonUpEvent& event) override{

		++upCount;

		if (log){ log->push_back(name + ":up"); }

		Button::OnMouseButtonUp(event);

	}

};

/// @brief 计数 + 生效按钮：`OnClick` 里执行自定义动作（删自身 / 删祖先）
class ActionButton: public Button{

public:

	std::function<void()> action;

	int clicks = 0;

	explicit ActionButton(const std::string& text): Button(text){}

	void OnClick() override{

		++clicks;

		if (action){ action(); }

	}

};

/// @brief 键盘观测按钮：`OnKeyDown` 里执行动作（键盘派发期销毁的被试）
class KeyProbeButton: public Button{

public:

	std::function<void()> action;

	int* keys = nullptr;   // 对象在派发结束后即被析构 ⇒ 计数放在测试函数的栈上，避免读已析构成员

	explicit KeyProbeButton(const std::string& text, int* counter): Button(text), keys(counter){}

	void OnKeyDown(const KeyDownEvent&) override{

		++(*keys);

		if (action){ action(); }

	}

};

/// @brief 带背景色的裸行（Paint 后可按色查 DrawRect——"派发结束后控件树仍可绘制"）
class PaintedRow: public Widget{

public:

	Color color;

	explicit PaintedRow(Color c): color(c){}

	void OnPaint(PaintContext& ctx, int x, int y) override{

		ctx.DrawRect(Rect{ static_cast<float>(x), static_cast<float>(y),
			static_cast<float>(GetWidth()), static_cast<float>(GetHeight()) }, color);

	}

};

/// @brief 按色查首个 DrawRect 的矩形
bool FindRect(const CommandBuffer& commands, const Color& color, Rect& out){

	for (const auto& command : commands){

		if (const auto* rect = std::get_if<DrawRectCommand>(&command)){

			if (rect->color == color){ out = rect->rect; return true; }

		}

	}

	return false;

}

// ── 装置：真实 Application + 窗口 + 三层树 ──────────────────────────
//   树：root → row(RecordingBox) → inner(RecordingBox) → child(RecordingButton)
//   几何（同一父行上三处不同落点）：
//     row   : 0,0 300x80
//     inner : 0,0 140x80
//     child : 10,10 80x30
//     ⇒ (30,20) 命中 child；(100,60) 命中 inner（child 外）；(170,20) 命中 row 自身。
//   为什么父行用裸 Widget（RecordingBox）而不是 Button：验收 2 要验证
//   「父控件在子控件未消费时仍能响应」——Button 自己消费，做不到这种对照。
struct NestedProbe{

	Application app;

	Window* window = nullptr;

	RecordingBox* row = nullptr;

	RecordingBox* inner = nullptr;

	RecordingButton* child = nullptr;

	std::vector<std::string> log;

	explicit NestedProbe(const char* title){

		RenderServices services = CreateDefaultRenderServices();

		window = &app.Create(title, 400, 300, std::move(services));

		auto rowPtr = std::make_unique<RecordingBox>();

		row = rowPtr.get();

		row->name = "row";

		row->log = &log;

		row->SetSize(300, 80);

		row->SetPosition(0, 0);

		auto innerPtr = std::make_unique<RecordingBox>();

		inner = innerPtr.get();

		inner->name = "inner";

		inner->log = &log;

		inner->SetSize(140, 80);

		inner->SetPosition(0, 0);

		auto childPtr = std::make_unique<RecordingButton>("X", "child", &log);

		child = childPtr.get();

		child->SetSize(80, 30);

		child->SetPosition(10, 10);

		inner->AddChild(std::move(childPtr));

		row->AddChild(std::move(innerPtr));

		window->GetRootWidget().AddChild(std::move(rowPtr));

	}

	~NestedProbe(){

		window->Release();

	}

	/// @brief 一次完整点按（Down(x,y) → Up(x,y)）——经 Application 生产路径
	void Click(int x, int y){

		app.OnEvent(MouseButtonDownEvent(window, x, y, MouseButton::Left));

		app.OnEvent(MouseButtonUpEvent(window, x, y, MouseButton::Left));

	}

	std::string Log() const{

		std::string s;

		for (const auto& part : log){ s += (s.empty() ? "" : ",") + part; }

		return s;

	}

};

// ── 验收 1 ★：子按钮 + 父按钮都有回调 ⇒ 点子按钮只触发子回调 ──────────
void Test35ChildClickDoesNotReachParent(){

	NestedProbe probe("ECDI_P35_1");

	int childClicks = 0;

	probe.child->SetOnClick([&]{ ++childClicks; });

	probe.Click(30, 20);   // ★ 落在子按钮上

	EXPECT_EQ(childClicks, 1);

	EXPECT_EQ(probe.child->downCount, 1);    // 正对照：子确实收到

	EXPECT_EQ(probe.child->upCount, 1);

	EXPECT_EQ(probe.row->downCount, 0);      // ★ 父连 Down 都没收到（不是"收到但没动作"）

	EXPECT_EQ(probe.row->upCount, 0);

	EXPECT_EQ(probe.inner->downCount, 0);

	EXPECT_EQ(probe.inner->upCount, 0);

	// 轨迹证明"消费即停"：只有 child 两条，没有任何祖先条目
	EXPECT_EQ(probe.Log(), std::string("child:down,child:up"));

}

// ── 验收 1 边界：消费不越界——父行空白处点击，父仍响应 ────────────────
void Test35ParentBlankAreaStillResponds(){

	NestedProbe probe("ECDI_P35_1b");

	int childClicks = 0;

	probe.child->SetOnClick([&]{ ++childClicks; });

	probe.Click(170, 20);   // ★ row 空白尾部（不在 inner/child 上）

	EXPECT_EQ(childClicks, 0);

	EXPECT_EQ(probe.row->downCount, 1);

	EXPECT_EQ(probe.row->upCount, 1);

	EXPECT_EQ(probe.Log(), std::string("row:down,row:up"));

}

// ── 验收 2 ★：子控件未消费 ⇒ 冒泡到祖先（文档约定：未消费 = 继续冒泡）──
void Test35UnconsumedBubblesToAncestors(){

	NestedProbe probe("ECDI_P35_2");

	probe.Click(100, 60);   // inner 内、child 外

	EXPECT_EQ(probe.inner->downCount, 1);

	EXPECT_EQ(probe.inner->upCount, 1);

	EXPECT_EQ(probe.row->downCount, 1);    // ★ 未消费 ⇒ 父收到（约定行为）

	EXPECT_EQ(probe.row->upCount, 1);

	EXPECT_EQ(probe.child->downCount, 0);

	// 顺序：目标先于祖先（内→外）
	EXPECT_EQ(probe.Log(), std::string("inner:down,row:down,inner:up,row:up"));

}

// ── 验收 2 变体：应用层只消费 Up ⇒ Down 仍冒泡（不对称态的可测语义）──────
void Test35AppLevelConsumesUpOnly(){

	NestedProbe probe("ECDI_P35_2b");

	probe.inner->consumeUp = true;   // ★ 应用层自己只消费 Up

	probe.Click(100, 60);

	EXPECT_EQ(probe.inner->downCount, 1);

	EXPECT_EQ(probe.row->downCount, 1);    // Down 未消费 ⇒ 父收到
	EXPECT_EQ(probe.row->upCount, 0);      // ★ Up 被消费 ⇒ 父收不到 Up

	EXPECT_EQ(probe.Log(), std::string("inner:down,row:down,inner:up"));

}

// ── 验收 2 变体：Down/Up 都消费（对称）⇒ 父两阶段都收不到 ────────────
void Test35AppLevelConsumesBothPhases(){

	NestedProbe probe("ECDI_P35_2c");

	probe.inner->consumeDown = true;

	probe.inner->consumeUp = true;

	probe.Click(100, 60);

	EXPECT_EQ(probe.row->downCount, 0);

	EXPECT_EQ(probe.row->upCount, 0);

	EXPECT_EQ(probe.Log(), std::string("inner:down,inner:up"));

}

// ── Button 自动消费：拖出后松开（无效点击）也消费 Up ⇒ 父不会收到孤立 Up ──
void Test35ButtonConsumesUpEvenWhenDragOut(){

	NestedProbe probe("ECDI_P35_2d");

	int childClicks = 0;

	probe.child->SetOnClick([&]{ ++childClicks; });

	// Down 在按钮内、Up 在按钮外（capture 保证 Up 仍派发给按钮）
	probe.app.OnEvent(MouseButtonDownEvent(probe.window, 30, 20, MouseButton::Left));

	probe.app.OnEvent(MouseButtonUpEvent(probe.window, 200, 20, MouseButton::Left));

	EXPECT_EQ(childClicks, 0);             // 拖出释放 ⇒ 不触发点击（5.4.5 I6）

	EXPECT_EQ(probe.child->upCount, 1);    // 正对照：按钮收到 Up

	EXPECT_EQ(probe.row->upCount, 0);      // ★ 但 Up 被按钮消费 ⇒ 父收不到

}

// ── 验收 3（自身）★：点击回调里删自己 ⇒ 不崩、派发后树/捕获/焦点仍有效 ──
void Test35RemoveSelfInClickCallback(){

	Application app;

	Window* window = &app.Create("ECDI_P35_3a", 400, 300, CreateDefaultRenderServices());

	auto owner = std::make_unique<ActionButton>("del");

	ActionButton* self = owner.get();

	self->SetSize(100, 40);

	self->SetPosition(10, 10);

	int postRemoveExecuted = 0;

	int clicksSeen = 0;

	self->action = [&]{

		// ★ 回调里请求移除自己——返回后 self 仍有效（只是不属于树）
		EXPECT_TRUE(self->RequestRemove());            // true = 已入延迟队列（未析构）

		EXPECT_TRUE(self->GetParent() == nullptr);     // 已脱树（命中/绘制不再包含它）

		EXPECT_EQ(self->GetWidth(), 100);              // 对象**仍存活**（延迟析构的直接证据）

		clicksSeen = self->clicks;

		++postRemoveExecuted;

	};

	window->GetRootWidget().AddChild(std::move(owner));

	app.OnEvent(MouseButtonDownEvent(window, 20, 20, MouseButton::Left));

	app.OnEvent(MouseButtonUpEvent(window, 20, 20, MouseButton::Left));

	// ⚠️ 派发结束后 `self` 已被析构（这正是延迟销毁的定义）——只读闭包里抓到的值，不读悬垂指针
	EXPECT_EQ(clicksSeen, 1);                        // 回调确实执行（析构被延迟到派发后）

	EXPECT_EQ(postRemoveExecuted, 1);

	// 派发结束后：捕获/焦点不悬垂
	EXPECT_TRUE(window->GetCaptureWidget() == nullptr);

	EXPECT_TRUE(window->GetFocusedWidget() == nullptr);

	// 后续事件照常工作（树没坏）——注意：`self` 已在上一轮派发结束后析构，不再访问。
	app.OnEvent(MouseButtonDownEvent(window, 20, 20, MouseButton::Left));

	app.OnEvent(MouseButtonUpEvent(window, 20, 20, MouseButton::Left));

	EXPECT_TRUE(window->GetCaptureWidget() == nullptr);

	window->Release();

}

// ── 验收 3（祖先子树）★：点击回调里删包含自己的祖先 ⇒ 不崩、树仍可命中 ──
void Test35RemoveAncestorInClickCallback(){

	Application app;

	Window* window = &app.Create("ECDI_P35_3b", 400, 300, CreateDefaultRenderServices());

	// 树：root → group(Widget) → btn(ActionButton)；group 与 btn 都在点击链上
	auto group = std::make_unique<PaintedRow>(Color::FromRGBA8(10, 20, 30, 255));

	PaintedRow* groupPtr = group.get();

	groupPtr->SetSize(300, 200);

	auto btn = std::make_unique<ActionButton>("boom");

	ActionButton* btnPtr = btn.get();

	btnPtr->SetSize(100, 40);

	btnPtr->SetPosition(10, 10);

	int clicks = 0;

	int postRemoveExecuted = 0;

	btnPtr->action = [&]{

		++clicks;

		// ★ 删**祖先**（包含自己）：返回后本按钮与祖先都已脱树，但都**未析构**
		EXPECT_TRUE(groupPtr->RequestRemove());

		EXPECT_TRUE(btnPtr->GetParent() == groupPtr);     // 子节点仍挂在（已脱树的）祖先下
		EXPECT_TRUE(groupPtr->GetParent() == nullptr);
		EXPECT_EQ(window->GetRootWidget().GetChildCount(), 0u);   // 已脱树 ⇒ 根下无子
		++postRemoveExecuted;

	};

	groupPtr->AddChild(std::move(btn));

	window->GetRootWidget().AddChild(std::move(group));

	app.OnEvent(MouseButtonDownEvent(window, 20, 20, MouseButton::Left));

	app.OnEvent(MouseButtonUpEvent(window, 20, 20, MouseButton::Left));

	EXPECT_EQ(clicks, 1);

	EXPECT_EQ(postRemoveExecuted, 1);   // 删除请求后回调栈继续跑完（未析构）

	// 派发结束后：捕获/焦点不悬垂
	EXPECT_TRUE(window->GetCaptureWidget() == nullptr);

	EXPECT_TRUE(window->GetFocusedWidget() == nullptr);

	// 根仍覆盖客户区：空树命中 ⇒ 后续事件不崩（命中测试仍有效）
	app.OnEvent(MouseButtonDownEvent(window, 20, 20, MouseButton::Left));

	app.OnEvent(MouseButtonUpEvent(window, 20, 20, MouseButton::Left));

	// 整个子树在派发结束后被一次性析构 ⇒ 此后不得再访问 groupPtr/btnPtr。
	window->Release();

}

// ── 验收 4 ★：延迟时机 = 最外层派发结束（析构计数直接证明）──────────
namespace{

int g_liveWidgets = 0;   ///< 存活计数（Tracked 的构造/析构）

/// @brief 存活计数器（只为一件事：`liveCount` 是否在派发结束后回落）
class TrackedWidget: public Widget{

public:

	TrackedWidget(){ ++g_liveWidgets; }

	~TrackedWidget() override{ --g_liveWidgets; }

};

}

void Test35DeferredUntilOutermostDispatchEnd(){

	Application app;

	Window* window = &app.Create("ECDI_P35_4", 400, 300, CreateDefaultRenderServices());

	const int baseline = g_liveWidgets;

	auto childOwned = std::make_unique<ActionButton>("c");

	ActionButton* child = childOwned.get();

	child->SetSize(80, 30);

	child->SetPosition(10, 10);

	auto ancOwned = std::make_unique<TrackedWidget>();

	TrackedWidget* anc = ancOwned.get();

	anc->SetSize(300, 80);

	// 派发链 = child → anc → root。在 child 的 OnClick 里删除**祖先 anc**：
	//   ① 冒泡随后仍要读 current->GetParent()（anc 的父 = root）——anc 已脱树但活着；
	//   ② 派发结束后（最外层）anc 才析构 ⇒ g_liveWidgets 回落。
	int ancestorStillAliveInside = 0;

	child->action = [&]{

		EXPECT_TRUE(anc->RequestRemove());

		ancestorStillAliveInside = (anc->GetWidth() == 300) ? 1 : 0;

	};

	anc->AddChild(std::move(childOwned));

	window->GetRootWidget().AddChild(std::move(ancOwned));

	// ⚠️ 本用例只数 `TrackedWidget`——ActionButton 是 Button、不计入 g_liveWidgets
	EXPECT_EQ(g_liveWidgets, baseline + 1);   // anc

	app.OnEvent(MouseButtonDownEvent(window, 20, 20, MouseButton::Left));

	EXPECT_EQ(g_liveWidgets, baseline + 1);   // Down 回调没有删除 ⇒ 仍在

	app.OnEvent(MouseButtonUpEvent(window, 20, 20, MouseButton::Left));

	EXPECT_EQ(ancestorStillAliveInside, 1);       // 回调内祖先存活（延迟的直接证据）

	// ★ 最外层派发结束 ⇒ 整个子树（anc + 其中的 child）已被析构
	EXPECT_EQ(g_liveWidgets, baseline);

	window->Release();

}

// ── 验收 4 变体：非派发期 RequestRemove ⇒ 立即脱树并析构（返回 false）────
void Test35ImmediateRemoveOutsideDispatch(){

	Application app;

	Window* window = &app.Create("ECDI_P35_4b", 400, 300, CreateDefaultRenderServices());

	const int before = g_liveWidgets;

	auto owned = std::make_unique<TrackedWidget>();

	TrackedWidget* ptr = owned.get();

	window->GetRootWidget().AddChild(std::move(owned));

	EXPECT_EQ(g_liveWidgets, before + 1);

	// 不在派发中 ⇒ 立即脱树 + 析构；返回 false（语义：false = 已析构）
	EXPECT_FALSE(ptr->RequestRemove());

	EXPECT_EQ(g_liveWidgets, before);

	window->Release();

}

// ── 验收 4 变体：键盘派发期删自己 ⇒ 不崩、焦点清理、后续键盘不崩 ──────
void Test35RemoveSelfInKeyDown(){

	Application app;

	Window* window = &app.Create("ECDI_P35_4d", 400, 300, CreateDefaultRenderServices());

	int keyEvents = 0;

	auto owned = std::make_unique<KeyProbeButton>("k", &keyEvents);

	KeyProbeButton* btn = owned.get();

	btn->SetSize(100, 40);

	btn->SetPosition(10, 10);

	int postRemoveExecuted = 0;

	int tailExecuted = 0;

	btn->action = [&]{

		const bool deferred = btn->RequestRemove();

		EXPECT_TRUE(deferred);

		++postRemoveExecuted;

		EXPECT_TRUE(window->GetFocusedWidget() == nullptr);   // 摘树即清焦点（#59 ForgetSubtree）

		++tailExecuted;

	};

	window->GetRootWidget().AddChild(std::move(owned));

	app.OnEvent(MouseButtonDownEvent(window, 20, 20, MouseButton::Left));   // 建立焦点

	EXPECT_TRUE(window->GetFocusedWidget() == btn);


	// ★ 走 Application 正式入口（唯一真实路径）：键盘派发必须与鼠标同款「开钉住区」
	app.OnEvent(KeyDownEvent(window, KeyCode::Space, KeyModifier::None));


	EXPECT_EQ(keyEvents, 1);

	EXPECT_EQ(postRemoveExecuted, 1);

	EXPECT_EQ(tailExecuted, 1);                      // ★ 回调体内 RequestRemove 之后的语句也执行了

	EXPECT_TRUE(window->GetFocusedWidget() == nullptr);

	// 键盘派发之后的连续派发不崩（焦点已清、树已空）
	app.OnEvent(KeyDownEvent(window, KeyCode::Space, KeyModifier::None));

	EXPECT_TRUE(window->GetFocusedWidget() == nullptr);

	window->Release();

}

// ── 验收 5 ★：派发结束后控件树仍可绘制（被删子树消失、兄弟子树照常）─────────
void Test35TreeStillPaintsAfterRemoval(){

	Application app;

	Window* window = &app.Create("ECDI_P35_5", 400, 300, CreateDefaultRenderServices());

	const Color groupColor = Color::FromRGBA8(10, 20, 30, 255);

	const Color siblingColor = Color::FromRGBA8(40, 50, 60, 255);

	auto group = std::make_unique<PaintedRow>(groupColor);

	PaintedRow* groupPtr = group.get();

	groupPtr->SetSize(100, 100);

	auto btn = std::make_unique<ActionButton>("x");

	ActionButton* btnPtr = btn.get();

	btnPtr->SetSize(40, 20);

	btnPtr->action = [&]{ groupPtr->RequestRemove(); };

	groupPtr->AddChild(std::move(btn));

	auto sibling = std::make_unique<PaintedRow>(siblingColor);

	sibling->SetSize(100, 100);

	sibling->SetPosition(150, 0);

	window->GetRootWidget().AddChild(std::move(group));

	window->GetRootWidget().AddChild(std::move(sibling));

	app.OnEvent(MouseButtonDownEvent(window, 20, 10, MouseButton::Left));

	app.OnEvent(MouseButtonUpEvent(window, 20, 10, MouseButton::Left));

	// 派发后整棵树重绘：被删子树不出现、兄弟子树照常出现（坐标正确）
	RecordingBackend backend;

	CommandBuffer commands;

	PaintContext ctx(commands, backend);

	window->GetRootWidget().Paint(ctx, 0, 0);

	Rect removedRect{};

	Rect siblingRect{};

	EXPECT_FALSE(FindRect(commands, groupColor, removedRect));   // ★ 已删除子树不绘制

	EXPECT_TRUE(FindRect(commands, siblingColor, siblingRect));

	EXPECT_NEAR(siblingRect.x, 150.0f, 0.001f);

	window->Release();

}

// ── 验收 5 回归：未使用消费 API 的既有界面行为不变（按钮无父点击容器）──
void Test35LegacyButtonBehaviourUnchanged(){

	Application app;

	Window* window = &app.Create("ECDI_P35_5b", 400, 300, CreateDefaultRenderServices());

	auto owner = std::make_unique<Button>("plain");

	Button* btn = owner.get();

	btn->SetSize(100, 40);

	btn->SetPosition(10, 10);

	int clicks = 0;

	btn->SetOnClick([&]{ ++clicks; });

	window->GetRootWidget().AddChild(std::move(owner));

	app.OnEvent(MouseButtonDownEvent(window, 20, 20, MouseButton::Left));

	EXPECT_TRUE(window->GetCaptureWidget() == btn);   // 隐式捕获照旧

	app.OnEvent(MouseButtonUpEvent(window, 20, 20, MouseButton::Left));

	EXPECT_EQ(clicks, 1);

	EXPECT_TRUE(window->GetCaptureWidget() == nullptr);   // Up 后释放捕获照旧

	window->Release();

}

}   // namespace

// ══════════════════════════════════════════════════════════════════════
// 注册
// ══════════════════════════════════════════════════════════════════════

void ECDI::Test::RegisterEventConsumptionTests(){

	GetTestRegistry().Add("Events.Consume.ChildClickDoesNotReachParent", &Test35ChildClickDoesNotReachParent);

	GetTestRegistry().Add("Events.Consume.ParentBlankAreaStillResponds", &Test35ParentBlankAreaStillResponds);

	GetTestRegistry().Add("Events.Consume.UnconsumedBubblesToAncestors", &Test35UnconsumedBubblesToAncestors);

	GetTestRegistry().Add("Events.Consume.AppLevelConsumesUpOnly", &Test35AppLevelConsumesUpOnly);

	GetTestRegistry().Add("Events.Consume.AppLevelConsumesBothPhases", &Test35AppLevelConsumesBothPhases);

	GetTestRegistry().Add("Events.Consume.ButtonConsumesUpEvenWhenDragOut", &Test35ButtonConsumesUpEvenWhenDragOut);

	GetTestRegistry().Add("Events.Deferred.RemoveSelfInClickCallback", &Test35RemoveSelfInClickCallback);

	GetTestRegistry().Add("Events.Deferred.RemoveAncestorInClickCallback", &Test35RemoveAncestorInClickCallback);

	GetTestRegistry().Add("Events.Deferred.DeferredUntilOutermostDispatchEnd", &Test35DeferredUntilOutermostDispatchEnd);

	GetTestRegistry().Add("Events.Deferred.ImmediateRemoveOutsideDispatch", &Test35ImmediateRemoveOutsideDispatch);

	GetTestRegistry().Add("Events.Deferred.RemoveSelfInKeyDown", &Test35RemoveSelfInKeyDown);

	GetTestRegistry().Add("Events.Deferred.TreeStillPaintsAfterRemoval", &Test35TreeStillPaintsAfterRemoval);

	GetTestRegistry().Add("Events.Consume.LegacyButtonBehaviourUnchanged", &Test35LegacyButtonBehaviourUnchanged);

}
