#include "RunAllTests.h"
#include "TestFramework.h"

#include "ECDI/Core/Color.h"
#include "ECDI/Core/Point.h"
#include "ECDI/Core/Size.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButton.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonDownEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseButtonUpEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseMoveEvent.h"
#include "ECDI/EventSystem/Input/Mouse/MouseWheelEvent.h"
#include "ECDI/Render/PaintContext.h"
#include "ECDI/Render/RenderCommand.h"
#include "ECDI/Theme/DefaultTheme.h"
#include "ECDI/Widget/Button.h"
#include "ECDI/Widget/Panel.h"
#include "ECDI/Widget/ScrollBar.h"
#include "ECDI/Widget/ScrollView.h"
#include "ECDI/Widget/Widget.h"
#include "Render/RecordingBackend.h"

#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace ECDI{

namespace{

constexpr float kEps = 0.001f;

/// 行背景色（唯一色值——命令流里按色检索，避开索引脆弱性）
constexpr Color kRowColor() noexcept{ return Color::FromRGBA8(7, 8, 9, 255); }

// ── 测试替身 ─────────────────────────────────────────────────

/// @brief 暴露 `ScrollView` 的 protected 成员 + `OnPaint` 观测点
/// @details `ScrollView` 把 `SetSize`/`OnMouseWheel` 的 override 声明在 protected
/// （详设 §2.5 草案如此；与 `TextBox`/`CollapsiblePanel`/`CaptionBar` 三个先例不一致——已记账）。
/// 测试经 using 暴露，与 `TestableTextBox` 的既有手法一致。
class TestableScrollView final: public ScrollView{

public:

	using ScrollView::SetSize;         ///< 尺寸（protected override——测试经 using 暴露）
	using ScrollView::OnMouseWheel;    ///< 滚轮（protected override）

	/// @brief 记录自身 `OnPaint` 收到的最终坐标（验「自身坐标不叠自身偏移」——C2）
	void OnPaint(PaintContext&, int x, int y) override{
		lastPaintX = x;
		lastPaintY = y;
		++paintCount;
	}

	int lastPaintX = -1;
	int lastPaintY = -1;
	int paintCount = 0;

};

/// @brief 暴露 `ScrollBar` 的输入三件套 + 样式只读（私有几何仍走命令流观测）
class TestableScrollBar final: public ScrollBar{

public:

	using ScrollBar::OnMouseButtonDown;
	using ScrollBar::OnMouseButtonUp;
	using ScrollBar::OnMouseMove;

	using ScrollBar::ScrollBar;   ///< 继承朝向构造

	/// @brief 样式只读（protected 成员的中转——同 `TestableProgressBar::Style()` 先例）
	const ScrollBarStyle& Style() const noexcept{ return m_style; }

};

// ── 命令流检索辅助 ────────────────────────────────────────────

/// @brief 按色值查找首个 `DrawRectCommand`（找不到返回 nullptr）
const DrawRectCommand* FindRectByColor(const CommandBuffer& commands, const Color& color){
	for (const auto& command : commands){
		if (const auto* rect = std::get_if<DrawRectCommand>(&command)){
			if (rect->color == color){
				return rect;
			}
		}
	}
	return nullptr;
}

/// @brief 往内容节点加一行 Panel（**带背景**——供 Paint 坐标断言；返回非拥有指针）
/// @param parent 内容节点（`ScrollView::GetContentView()` 返回 `Widget&`）
/// @details ⚠️ `Panel::ContainsPoint()` **恒返回 false**（2026-08-30 定案：纯容器自身永不参与命中）
/// ⇒ **本函数的返回值永远不可能成为 `HitTest` 的目标**。命中类用例请用 `AddHitRow`。
Panel* AddPanelRow(Widget& parent, int width, int height, int x, int y){
	auto row = std::make_unique<Panel>();
	row->SetSize(width, height);
	row->SetPosition(x, y);
	// 圆角与边框**都清零**：行只发一条 DrawRect（背景），坐标断言才与几何逐位对应
	//（留边框会多画一圈描边环 + 背景四边内缩 ⇒ 断言偏移）
	row->SetStyle(PanelStyleOverride{ .background = kRowColor(), .cornerRadius = 0.0f, .borderWidth = 0.0f });
	Panel* raw = row.get();
	parent.AddChild(std::move(row));
	return raw;
}

/// @brief 往内容节点加一个**可命中**的叶子控件（裸 `Widget`——默认 `ContainsPoint` 为矩形判定）
/// @details 为什么不用 `Panel`：见 `AddPanelRow` 的说明。裸 `Widget` 不产生任何绘制命令，
/// 故同时适合需要"命令流只由被测控件产生"的用例。
/// @note 递归**不**受 `ContainsPoint` 门控（`Widget::HitTest` 先递归子节点）⇒ `Panel` 内的
/// 子控件依旧可命中（`ModelProbe` 行内 `CheckBox` 即依赖此点）；受影响的只是"`Panel` 自身当目标"。
Widget* AddHitRow(Widget& parent, int width, int height, int x, int y){
	auto row = std::make_unique<Widget>();
	row->SetSize(width, height);
	row->SetPosition(x, y);
	Widget* raw = row.get();
	parent.AddChild(std::move(row));
	return raw;
}

// ── T15-1 默认偏移与接缝默认值 ────────────────────────────────

void TestScrollViewDefaultOffsetIsZero(){
	ScrollView sv;

	EXPECT_EQ(sv.GetScrollOffsetX(), 0);
	EXPECT_EQ(sv.GetScrollOffsetY(), 0);
	EXPECT_EQ(sv.GetContentView().GetContentOffsetX(), 0);
	EXPECT_EQ(sv.GetContentView().GetContentOffsetY(), 0);
	EXPECT_EQ(sv.GetScrollStep(), ScrollView::kDefaultScrollStep);
	EXPECT_TRUE(sv.IsScrollBarVisible());
	EXPECT_TRUE(sv.ClipsChildren());
	EXPECT_FALSE(sv.ConsumesMouseInput());   // C12：空白区是容器（不阻断 HTCAPTION）
}

// ── T15-2 零回归锚：既有控件偏移恒 0 / 不裁剪命中 ──────────────

void TestWidgetDefaultOffsetAndClipFlag(){
	Widget widget;
	EXPECT_EQ(widget.GetContentOffsetX(), 0);
	EXPECT_EQ(widget.GetContentOffsetY(), 0);
	EXPECT_FALSE(widget.ClipsChildren());

	Panel panel;
	EXPECT_EQ(panel.GetContentOffsetX(), 0);
	EXPECT_EQ(panel.GetContentOffsetY(), 0);
	EXPECT_FALSE(panel.ClipsChildren());

	Button button;
	EXPECT_EQ(button.GetContentOffsetX(), 0);
	EXPECT_EQ(button.GetContentOffsetY(), 0);
	EXPECT_FALSE(button.ClipsChildren());
}

// ── T15-3 自身坐标与自身 clip 不受自身偏移影响（C2）────────────

void TestScrollViewOffsetDoesNotAffectOwnClip(){
	TestableScrollView sv;
	sv.SetSize(200, 100);
	sv.SetContentExtent(180, 400);
	sv.SetContentOffset(0, 120);
	EXPECT_EQ(sv.GetScrollOffsetY(), 120);   // 前置：偏移确实非 0

	RecordingBackend measurer;
	CommandBuffer commands;
	PaintContext ctx(commands, measurer);
	sv.Paint(ctx, 30, 40);

	// ① 自身 OnPaint 收到的是**视觉位置**（不含自身偏移）
	EXPECT_EQ(sv.paintCount, 1);
	EXPECT_EQ(sv.lastPaintX, 30);
	EXPECT_EQ(sv.lastPaintY, 40);

	// ② 首个命令 = 自身边界 clip：矩形 == 视口矩形（不叠偏移）
	EXPECT_TRUE(std::holds_alternative<PushClipCommand>(commands[0]));
	const auto& clip = std::get<PushClipCommand>(commands[0]);
	EXPECT_NEAR(clip.rect.x, 30.0f, kEps);
	EXPECT_NEAR(clip.rect.y, 40.0f, kEps);
	EXPECT_NEAR(clip.rect.width, 200.0f, kEps);
	EXPECT_NEAR(clip.rect.height, 100.0f, kEps);
}

// ── T15-4 内容子控件被偏移（Paint 减）；布局位置不变（C1/C3）────

void TestScrollViewContentIsOffset(){
	TestableScrollView sv;
	sv.SetSize(200, 100);
	Panel* row = AddPanelRow(sv.GetContentView(), 180, 20, 0, 40);
	sv.SetContentExtent(180, 400);
	sv.SetContentOffset(0, 30);

	RecordingBackend measurer;
	CommandBuffer commands;
	PaintContext ctx(commands, measurer);
	sv.Paint(ctx, 0, 0);

	// 行视觉 y = 0(视口) + 0(内容) + 40 − 30 = 10
	const DrawRectCommand* background = FindRectByColor(commands, kRowColor());
	EXPECT_TRUE(background != nullptr);
	if (background != nullptr){
		EXPECT_NEAR(background->rect.x, 0.0f, kEps);
		EXPECT_NEAR(background->rect.y, 10.0f, kEps);
	}

	// 布局位置**不随滚动变化**（C1）；内容节点转发权威偏移
	EXPECT_EQ(row->GetY(), 40);
	EXPECT_EQ(sv.GetContentView().GetContentOffsetY(), 30);
}

// ── T15-5 ★ 偏移层隔离回归锚（条不受内容偏移影响）─────────────

void TestScrollBarNotAffectedByContentOffset(){
	// 两套同构场景，唯一差别 = 内容偏移；条的三项观测必须逐位相同
	TestableScrollView base;
	base.SetSize(200, 100);
	AddPanelRow(base.GetContentView(), 180, 20, 0, 40);
	base.SetContentExtent(180, 400);

	TestableScrollView shifted;
	shifted.SetSize(200, 100);
	AddPanelRow(shifted.GetContentView(), 180, 20, 0, 40);
	shifted.SetContentExtent(180, 400);
	shifted.SetContentOffset(0, 30);
	EXPECT_EQ(shifted.GetScrollOffsetY(), 30);   // 前置：偏移确实非 0

	RecordingBackend measurer;
	CommandBuffer cbBase;
	CommandBuffer cbShifted;
	PaintContext ctxBase(cbBase, measurer);
	PaintContext ctxShifted(cbShifted, measurer);
	base.Paint(ctxBase, 0, 0);
	shifted.Paint(ctxShifted, 0, 0);

	// ① Paint：命令流尾部依次为 [PushClip(条), DrawRect(轨道), DrawRoundedRect(滑块), PopClip]
	//    （水平条在此场景不应出现；滑块位置随偏移是**其职责**，故不参与比较）
	EXPECT_EQ(cbBase.size(), cbShifted.size());
	const size_t n = cbBase.size();
	EXPECT_TRUE(n >= 5);

	const auto& clipBase    = std::get<PushClipCommand>(cbBase[n - 5]);
	const auto& clipShifted = std::get<PushClipCommand>(cbShifted[n - 5]);
	EXPECT_NEAR(clipBase.rect.x, 188.0f, kEps);   // 200 − 厚度 12
	EXPECT_NEAR(clipBase.rect.y, 0.0f, kEps);
	EXPECT_NEAR(clipBase.rect.width, 12.0f, kEps);
	EXPECT_NEAR(clipBase.rect.height, 100.0f, kEps);
	EXPECT_EQ(clipShifted.rect.x, clipBase.rect.x);
	EXPECT_EQ(clipShifted.rect.y, clipBase.rect.y);
	EXPECT_EQ(clipShifted.rect.width, clipBase.rect.width);
	EXPECT_EQ(clipShifted.rect.height, clipBase.rect.height);

	const auto& trackBase    = std::get<DrawRectCommand>(cbBase[n - 4]);
	const auto& trackShifted = std::get<DrawRectCommand>(cbShifted[n - 4]);
	EXPECT_EQ(trackShifted.rect.x, trackBase.rect.x);
	EXPECT_EQ(trackShifted.rect.y, trackBase.rect.y);
	EXPECT_EQ(trackShifted.rect.width, trackBase.rect.width);
	EXPECT_EQ(trackShifted.rect.height, trackBase.rect.height);

	// ② HitTest：同一点命中的仍是**本场景自己的**条（条在偏移层之外）
	//    ⚠️ 必须逐场景取自己的条——两个场景是各自独立的控件树，指针不可跨场景比
	ScrollBar* vBarBase    = base.GetVerticalScrollBar();
	ScrollBar* vBarShifted = shifted.GetVerticalScrollBar();
	EXPECT_TRUE(base.HitTest(190, 50) == vBarBase);
	EXPECT_TRUE(shifted.HitTest(190, 50) == vBarShifted);

	// ③ GetAbsolutePosition：条自身视觉位置不含内容偏移
	const Point absBase    = vBarBase->GetAbsolutePosition();
	const Point absShifted = vBarShifted->GetAbsolutePosition();
	EXPECT_NEAR(absBase.x, 188.0f, kEps);
	EXPECT_NEAR(absBase.y, 0.0f, kEps);
	EXPECT_NEAR(absShifted.x, absBase.x, kEps);
	EXPECT_NEAR(absShifted.y, absBase.y, kEps);
}

// ── T15-6 多层命中（视口 → 内容 → 行；HitTest 加回偏移）────────

void TestScrollViewHitTestWithOffset(){
	TestableScrollView sv;
	sv.SetPosition(0, 100);
	sv.SetSize(200, 200);
	Widget* row = AddHitRow(sv.GetContentView(), 180, 20, 0, 84);
	sv.SetContentExtent(180, 400);
	sv.SetContentOffset(0, 50);

	// 行视觉 y = 100(视口原点) + 0(内容) + 84 − 50 = 134
	// 视口局部点 (10, 34) → 内容局部 (10, 34+50=84) → 行局部 (10, 0) ⇒ 命中
	Widget* hit = sv.HitTest(10, 34);
	EXPECT_TRUE(hit == row);

	// 反例：同一行内、但视口局部 y=34 已越过行（行局部 y = 0 有效区 [0,20)）
	// 取 y=55 ⇒ 行局部 55+50−84 = 21 ⇒ 越界 ⇒ 命中内容节点而非行
	Widget* miss = sv.HitTest(10, 55);
	EXPECT_FALSE(miss == row);
	EXPECT_TRUE(miss == &sv.GetContentView());
}

// ── T15-7 ★ 裁剪命中（R3/D2）：容器外整棵子树不命中 ────────────

void TestScrollViewClipsChildrenRejectsOutside(){
	TestableScrollView sv;
	sv.SetSize(200, 200);
	Widget* row = AddHitRow(sv.GetContentView(), 180, 20, 0, 300);   // 内容 y=300：视口只有 0..200
	sv.SetContentExtent(180, 400);

	// ① 命中点在容器矩形之外 ⇒ **整棵子树**不命中（内容坐标系里正对行也无效）
	//    无门控时：内容局部 y=300 → 行局部 0 ⇒ 会误命中（K4 既有缺口）
	EXPECT_TRUE(sv.HitTest(10, 300) == nullptr);

	// ② 命中点在容器内、但落在子控件矩形之外 ⇒ 不命中该子（回落内容节点自身）
	Widget* inside = sv.HitTest(10, 150);
	EXPECT_FALSE(inside == row);
	EXPECT_TRUE(inside == &sv.GetContentView());

	// ③ 视口内的路径不受影响：offset 300 → clamp 200 ⇒ 行视觉 y = 100
	sv.SetContentOffset(0, 300);
	EXPECT_EQ(sv.GetScrollOffsetY(), 200);   // maxOffsetY = 400 − 200
	EXPECT_TRUE(sv.HitTest(10, 100) == row);
}

// ── T15-8 零回归：Panel 不裁剪命中（K4 既有语义保持）────────────

void TestPanelHitTestRegression(){
	Panel panel;
	panel.SetSize(100, 100);
	EXPECT_FALSE(panel.ClipsChildren());   // 门控关闭 ⇒ 旧语义（零回归）

	// 子用**可命中的叶子**（裸 Widget）：`Panel` 自身 `ContainsPoint` 恒 false、永不可命中
	// （见 `AddPanelRow` 说明）——若拿 Panel 当目标，本用例会因"目标类型选错"而失去判据
	Widget* child = nullptr;
	{
		auto inner = std::make_unique<Widget>();
		inner->SetSize(50, 50);
		inner->SetPosition(200, 200);   // 子越界（旧语义下仍可命中——本阶段**不通改**）
		child = inner.get();
		panel.AddChild(std::move(inner));
	}

	// ① 命中点落在 panel 矩形之外：(210,210) → 子局部 (10,10) ⇒ 旧语义命中越界子（K4 保持）
	EXPECT_TRUE(panel.HitTest(210, 210) == child);

	// ② Panel 自身永不参与命中（2026-08-30 定案：纯容器语义）——即便点在自身矩形内
	EXPECT_TRUE(panel.HitTest(10, 10) == nullptr);
}

// ── T15-9 滚轮步长与 clamp ───────────────────────────────────

void TestScrollViewWheelStepAndClamp(){
	TestableScrollView sv;
	sv.SetSize(200, 100);
	sv.SetContentExtent(180, 300);   // 视口高 100（无水平条）⇒ maxOffsetY = 200
	sv.SetScrollStep(28);
	EXPECT_EQ(sv.GetScrollStep(), 28);
	EXPECT_EQ(sv.GetMaxOffsetY(), 200);

	// delta > 0 = 远离用户 ⇒ 内容上移 ⇒ offset 减小（已在顶端 ⇒ 不动）
	sv.OnMouseWheel(MouseWheelEvent(nullptr, 0, 0, 120));
	EXPECT_EQ(sv.GetScrollOffsetY(), 0);

	// 一格（120）⇒ +28
	sv.OnMouseWheel(MouseWheelEvent(nullptr, 0, 0, -120));
	EXPECT_EQ(sv.GetScrollOffsetY(), 28);

	// 高精度滚轮：30 ⇒ 30/120×28 = 7（截断）
	sv.OnMouseWheel(MouseWheelEvent(nullptr, 0, 0, -30));
	EXPECT_EQ(sv.GetScrollOffsetY(), 35);

	// 两格（240）⇒ +56
	sv.OnMouseWheel(MouseWheelEvent(nullptr, 0, 0, -240));
	EXPECT_EQ(sv.GetScrollOffsetY(), 91);

	// 狂滚 ⇒ clamp 到 maxOffset
	sv.OnMouseWheel(MouseWheelEvent(nullptr, 0, 0, -12000));
	EXPECT_EQ(sv.GetScrollOffsetY(), 200);

	// 反向狂滚 ⇒ clamp 到 0
	sv.OnMouseWheel(MouseWheelEvent(nullptr, 0, 0, 12000));
	EXPECT_EQ(sv.GetScrollOffsetY(), 0);
}

// ── T15-10 内容变短 ⇒ offset 立即 clamp（顺序冻结链）──────────

void TestScrollViewContentShrinkClampsOffset(){
	TestableScrollView sv;
	sv.SetSize(200, 100);
	sv.SetContentExtent(180, 400);
	sv.SetContentOffset(0, 300);
	EXPECT_EQ(sv.GetMaxOffsetY(), 300);
	EXPECT_EQ(sv.GetScrollOffsetY(), 300);

	// 内容缩短 ⇒ ApplyLayout → ClampOffset 用**新** viewport 重算 ⇒ 立刻夹回
	sv.SetContentExtent(180, 150);
	EXPECT_EQ(sv.GetMaxOffsetY(), 50);
	EXPECT_EQ(sv.GetScrollOffsetY(), 50);

	// 缩到视口以内 ⇒ 归 0
	sv.SetContentExtent(180, 80);
	EXPECT_EQ(sv.GetMaxOffsetY(), 0);
	EXPECT_EQ(sv.GetScrollOffsetY(), 0);
}

// ── T15-11 extent = 二维包围盒（非 Σ）+ 负向钳 0 ───────────────

void TestScrollViewExtentIsBoundingBox(){
	TestableScrollView sv;
	sv.SetSize(200, 100);
	AddHitRow(sv.GetContentView(), 50, 20, 0, 0);
	AddHitRow(sv.GetContentView(), 50, 20, 0, 30);

	sv.UpdateContentExtent();
	EXPECT_NEAR(sv.GetContentExtent().width, 50.0f, kEps);
	EXPECT_NEAR(sv.GetContentExtent().height, 50.0f, kEps);   // max(y+h) = 30+20（非 Σ = 40）

	// 空内容 ⇒ 0
	TestableScrollView empty;
	empty.SetSize(200, 100);
	empty.UpdateContentExtent();
	EXPECT_NEAR(empty.GetContentExtent().width, 0.0f, kEps);
	EXPECT_NEAR(empty.GetContentExtent().height, 0.0f, kEps);

	// 负向内容钳 0（v1 不支持负向 extent——记账）
	TestableScrollView negative;
	negative.SetSize(200, 100);
	AddHitRow(negative.GetContentView(), 50, 20, -100, -100);
	negative.UpdateContentExtent();
	EXPECT_NEAR(negative.GetContentExtent().width, 0.0f, kEps);
	EXPECT_NEAR(negative.GetContentExtent().height, 0.0f, kEps);
}

// ── T15-12 滑块几何（比例映射 + 最小长度 + 退化保护）───────────

void TestScrollBarThumbGeometry(){
	RecordingBackend measurer;

	TestableScrollBar bar(ScrollBar::Orientation::Vertical);
	bar.SetSize(12, 100);
	bar.SetRange(400, 100);   // thumbLen = 100×100/400 = 25；maxOffset = 300
	EXPECT_EQ(bar.GetMaxOffset(), 300);
	EXPECT_EQ(bar.GetOffset(), 0);

	{
		CommandBuffer commands;
		PaintContext ctx(commands, measurer);
		bar.Paint(ctx, 0, 0);
		// [0] PushClip(自身) [1] DrawRect(轨道) [2] DrawRoundedRect(滑块) [3] PopClip
		EXPECT_EQ(commands.size(), size_t{ 4 });
		const auto& thumb = std::get<DrawRoundedRectCommand>(commands[2]);
		EXPECT_NEAR(thumb.rect.x, 0.0f, kEps);
		EXPECT_NEAR(thumb.rect.y, 0.0f, kEps);        // offset 0 ⇒ 顶端
		EXPECT_NEAR(thumb.rect.width, 12.0f, kEps);
		EXPECT_NEAR(thumb.rect.height, 25.0f, kEps);
	}

	// 中位：thumbStart = (100−25) × 150/300 = 37
	bar.SetOffset(150);
	{
		CommandBuffer commands;
		PaintContext ctx(commands, measurer);
		bar.Paint(ctx, 0, 0);
		const auto& thumb = std::get<DrawRoundedRectCommand>(commands[2]);
		EXPECT_NEAR(thumb.rect.y, 37.0f, kEps);
	}

	// 无可滚（content == viewport）⇒ 滑块占满
	bar.SetRange(100, 100);
	EXPECT_EQ(bar.GetMaxOffset(), 0);
	{
		CommandBuffer commands;
		PaintContext ctx(commands, measurer);
		bar.Paint(ctx, 0, 0);
		const auto& thumb = std::get<DrawRoundedRectCommand>(commands[2]);
		EXPECT_NEAR(thumb.rect.height, 100.0f, kEps);
	}

	// 无内容 ⇒ 占满
	bar.SetRange(0, 100);
	{
		CommandBuffer commands;
		PaintContext ctx(commands, measurer);
		bar.Paint(ctx, 0, 0);
		const auto& thumb = std::get<DrawRoundedRectCommand>(commands[2]);
		EXPECT_NEAR(thumb.rect.height, 100.0f, kEps);
	}

	// 比例极小 ⇒ 钳到最小可抓取长度（min(24, track) = 24）
	bar.SetSize(12, 1000);
	bar.SetRange(100000, 100);
	{
		CommandBuffer commands;
		PaintContext ctx(commands, measurer);
		bar.Paint(ctx, 0, 0);
		const auto& thumb = std::get<DrawRoundedRectCommand>(commands[2]);
		EXPECT_NEAR(thumb.rect.height, 24.0f, kEps);
	}
}

// ── T15-13 拖拽反推 + 除零保护 + 回调单向 ─────────────────────

void TestScrollBarDragInvertsOffset(){
	TestableScrollBar bar(ScrollBar::Orientation::Vertical);
	bar.SetSize(12, 100);
	bar.SetRange(400, 100);   // thumbLen 25、denom 75、maxOffset 300 ⇒ offset = 4 × thumbStart

	std::vector<int> notified;
	bar.SetOnOffsetChanged([&notified](int value){ notified.push_back(value); });

	// ① 按下滑块内（y=10 < 25）⇒ 进入拖拽，抓取偏移 = 10
	bar.OnMouseButtonDown(MouseButtonDownEvent(nullptr, 6, 10, MouseButton::Left));
	EXPECT_EQ(notified.size(), size_t{ 0 });   // 命中滑块不通知（只改抓取状态）

	// 拖到 y=50 ⇒ 滑块起点 40 ⇒ offset = 40×300/75 = 160
	bar.OnMouseMove(MouseMoveEvent(nullptr, 6, 50));
	EXPECT_EQ(bar.GetOffset(), 160);
	EXPECT_EQ(notified.size(), size_t{ 1 });
	EXPECT_EQ(notified.back(), 160);

	// 拖到底 ⇒ clamp 到 maxOffset
	bar.OnMouseMove(MouseMoveEvent(nullptr, 6, 90));
	EXPECT_EQ(bar.GetOffset(), 300);

	bar.OnMouseButtonUp(MouseButtonUpEvent(nullptr, 6, 90, MouseButton::Left));

	// ② 松开后再移动：不再拖拽（仅 hover 视觉，不动偏移）
	bar.OnMouseMove(MouseMoveEvent(nullptr, 6, 10));
	EXPECT_EQ(bar.GetOffset(), 300);

	// ③ denom <= 0 除零保护：内容 == 视口 ⇒ 滑块占满、反推恒 0
	TestableScrollBar full(ScrollBar::Orientation::Vertical);
	full.SetSize(12, 100);
	full.SetRange(100, 100);
	full.OnMouseButtonDown(MouseButtonDownEvent(nullptr, 6, 50, MouseButton::Left));
	full.OnMouseMove(MouseMoveEvent(nullptr, 6, 90));
	EXPECT_EQ(full.GetOffset(), 0);

	// ④ SetOffset（外部驱动）**不**触发回调（防容器↔条递归——C6/D11）
	const size_t before = notified.size();
	bar.SetOffset(40);
	EXPECT_EQ(notified.size(), before);
	EXPECT_EQ(bar.GetOffset(), 40);
}

// ── T15-14 主题注入 + 覆盖后 ApplyTheme 不覆盖（D7）────────────

void TestScrollBarThemeAndOverride(){
	TestableScrollBar bar(ScrollBar::Orientation::Vertical);
	const ScrollBarStyle defaults = GetDefaultTheme().GetScrollBarStyle();

	EXPECT_EQ(bar.Style().trackColor.value, defaults.trackColor.value);
	EXPECT_EQ(bar.Style().thumbColor.value, defaults.thumbColor.value);
	EXPECT_EQ(bar.Style().thumbHoverColor.value, defaults.thumbHoverColor.value);
	EXPECT_EQ(bar.Style().thumbPressedColor.value, defaults.thumbPressedColor.value);
	EXPECT_EQ(bar.Style().thickness.value, defaults.thickness.value);
	EXPECT_EQ(bar.GetThickness(), 12);   // 厚度参与 viewport 计算 ⇒ 必须可查（§3.3）

	const Color custom = Color::FromRGBA8(10, 20, 30);
	bar.SetStyle(ScrollBarStyleOverride{ .thumbColor = custom, .thickness = 20 });
	EXPECT_EQ(bar.Style().thumbColor.value, custom);
	EXPECT_EQ(bar.GetThickness(), 20);

	// D7：overridden 字段不被主题覆盖；未覆盖字段仍随主题
	bar.ApplyTheme(GetDefaultTheme());
	EXPECT_EQ(bar.Style().thumbColor.value, custom);
	EXPECT_EQ(bar.Style().thickness.value, 20);
	EXPECT_EQ(bar.Style().trackColor.value, defaults.trackColor.value);
}

} // anonymous namespace

void ECDI::Test::RegisterScrollViewTests()
{
	GetTestRegistry().Add("ScrollView.DefaultOffsetIsZero",              &TestScrollViewDefaultOffsetIsZero);
	GetTestRegistry().Add("Widget.DefaultOffsetAndClipFlag",             &TestWidgetDefaultOffsetAndClipFlag);
	GetTestRegistry().Add("ScrollView.OffsetDoesNotAffectOwnClip",       &TestScrollViewOffsetDoesNotAffectOwnClip);
	GetTestRegistry().Add("ScrollView.ContentIsOffset",                  &TestScrollViewContentIsOffset);
	GetTestRegistry().Add("ScrollBar.NotAffectedByContentOffset",        &TestScrollBarNotAffectedByContentOffset);
	GetTestRegistry().Add("ScrollView.HitTestWithOffset",                &TestScrollViewHitTestWithOffset);
	GetTestRegistry().Add("ScrollView.ClipsChildrenRejectsOutside",      &TestScrollViewClipsChildrenRejectsOutside);
	GetTestRegistry().Add("Panel.HitTestRegression",                     &TestPanelHitTestRegression);
	GetTestRegistry().Add("ScrollView.WheelStepAndClamp",                &TestScrollViewWheelStepAndClamp);
	GetTestRegistry().Add("ScrollView.ContentShrinkClampsOffset",        &TestScrollViewContentShrinkClampsOffset);
	GetTestRegistry().Add("ScrollView.ExtentIsBoundingBox",              &TestScrollViewExtentIsBoundingBox);
	GetTestRegistry().Add("ScrollBar.ThumbGeometry",                     &TestScrollBarThumbGeometry);
	GetTestRegistry().Add("ScrollBar.DragInvertsOffset",                 &TestScrollBarDragInvertsOffset);
	GetTestRegistry().Add("ScrollBar.ThemeAndOverride",                  &TestScrollBarThemeAndOverride);
}

}
