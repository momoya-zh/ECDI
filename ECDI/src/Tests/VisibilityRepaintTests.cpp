#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 防御性 undef（规范 10）：本文件含 Windows.h——防 DrawTextW 宏污染 ECDI 头声明
#endif

#include "ECDI/Application/Application.h"
#include "ECDI/Core/Color.h"
#include "Render/RecordingBackend.h"          // 测试替身（库内内部头——同 RendererTests.cpp:11）
#include "ECDI/Render/RenderServices.h"
#include "ECDI/Widget/Button.h"
#include "ECDI/Window/Window.h"

#include <cstddef>
#include <memory>
#include <utility>

using namespace ECDI;

// ══════════════════════════════════════════════════════════════════════
// ★ 审计 M9：`SetVisible` 不 `Invalidate`（与同族 setter 不一致）
// ══════════════════════════════════════════════════════════════════════
// **缺陷本体**（审计原文）：`Widget.h` 的 `SetVisible` / `SetEnabled` 只赋值、不请求重绘，
//   而同族 `SetStyle` / `ApplyTheme` 内建 `Invalidate()` ⇒ 应用侧改可见性会**静默不刷新**。
//   ★ 与条 65（`SetText` 陷阱）同族：非鼠标事件驱动的 UI 更新漏了 Invalidate ⇒
//     「点了毫无视觉反馈」，且现象**极像事件根本没到达**（会把排查引向错误方向）。
//
// ★★ **判据设计（为何用 `GetUpdateRect` 而不是帧计数）**：
//   首版想用 `RecordingBackend::frameBackgrounds.size()` 计数帧 ⇒ **实测证明无判别力**：
//   框架的 `WM_PAINT` 分支**只调 `m_host.OnPaint()`、不调 `BeginPaint`/`EndPaint`**
//   （`Win32PlatformWindow.cpp:792-795`）⇒ update region **从不被消费** ⇒ WM_PAINT
//   **持续泛滥**（probe 实测：泵 N 条消息恒定产出 ~N 帧，与是否 Invalidate 无关）。
//   ⇒ 帧计数在两态下相同，是**假绿判据**。
//   ★ 真正的判别量 = **平台侧的 update region 脏位**：`Widget::Invalidate()` →
//     `Window::Invalidate()` → `Win32PlatformWindow::Invalidate()` → `InvalidateRect(hwnd, nullptr, FALSE)`
//     ⇒ region 变脏、`GetUpdateRect` 返回非零 ⇒ **只读平台状态即可观测，无需注入替身**
//     （同 `MouseCaptureTests` 直接断言 `::GetCapture()` 的先例——「读平台状态」是
//      本项目已确立的测试手法，非新机制）。⇒ 每步前先 `ValidateRect` 清脏位，把
//     「是否变脏」变成可区分的二值。
//
// ★★ **「量具校准」用例不可省**：`WidgetStyleReachesBackend`（`SetStyle` ⇒ region 脏）
//   证明 `GetUpdateRect` **确实能观测到框架发出的 Invalidate**——否则若判据本身失效，
//   四条用例会一起假绿（条 76：量具不校准则判据无效）。

namespace {

/// @brief 手动消息泵（WindowBackgroundTests 同款——处理至多 maxCount 条消息后返回）
void PumpMessages(int maxCount) {

	MSG msg{};

	for (int i = 0; i < maxCount && PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE); ++i) {

		TranslateMessage(&msg);
		DispatchMessageW(&msg);

	}

}

/// @brief M9 装置：真 Application + 真 Window（注入 RecordingBackend 供正向对照观测）
/// @details 关键实测事实（探针取证，如实记账）：
///   ① `FindWindowW(nullptr, L"<title>")` 能按标题取到本窗口 HWND —— region 判据的前提；
///   ② `WM_PAINT` 泛滥（无 BeginPaint/EndPaint）⇒ **帧计数不可用作判据**，改用 region 脏位。
struct M9Probe {

	Application app;

	Window* window = nullptr;   ///< 非拥有（所有权归 Application）

	HWND hwnd = nullptr;        ///< 平台窗口句柄（FindWindowW 取——region 判据的观测面）

	explicit M9Probe(const char* title) {

		auto recorderOwned = std::make_unique<RecordingBackend>();
		RenderServices services{
			std::move(recorderOwned),
			std::make_unique<RecordingBackend>()   // 测量占位（本组不断言测量）
		};

		window = &app.Create(title, 400, 300, std::move(services));

	}

	~M9Probe() { if (window) window->Release(); }

	/// @brief 显示并泵若干帧（让窗口进入稳态——region 由 Show 的第一帧弄脏）
	void ShowAndSettle() {

		window->Show();
		PumpMessages(64);

	}

	/// @brief 清空 update region（测试侧家务——把「是否变脏」变成可区分二值）
	/// @note 必须显示后调用（未显示的窗口 Region 操作无意义）
	void ClearRegion() {

		::ValidateRect(hwnd, nullptr);

	}

	/// @brief update region 是否变脏（非零 = 有绘制请求待处理）
	/// @pre ClearRegion() 之后调用才有判别力
	bool RegionDirty() const {

		RECT rc{};
		return ::GetUpdateRect(hwnd, &rc, FALSE) != 0;

	}

};

}   // namespace

// ── M9-①：可见性变化 ⇒ 请求重绘（★ 缺陷本体）──────────────────────────────
//   修复前 `SetVisible` 只赋值 ⇒ region 保持干净 ⇒ 应用侧 `SetVisible(false)` 后
//   控件不消失（直到别的失效顺带触发一次重绘）——这正是 M9 的用户可见后果。
void TestM9SetVisibleRequestsRepaint() {

	M9Probe probe("ECDI_M9_Hide");

	probe.hwnd = ::FindWindowW(nullptr, L"ECDI_M9_Hide");
	EXPECT_TRUE(probe.hwnd != nullptr);          // 量具前提：拿得到 HWND

	probe.ShowAndSettle();

	auto owned = std::make_unique<Button>();
	owned->SetText("press");
	Button* button = owned.get();
	probe.window->GetRootWidget().AddChild(std::move(owned));

	probe.ClearRegion();
	EXPECT_TRUE(!probe.RegionDirty());            // 基线干净（否则后续断言无意义）

	// ★ 隐藏 ⇒ 必须请求重绘（修复前：region 不变脏 —— 本条即 M9 本体）
	button->SetVisible(false);
	EXPECT_TRUE(probe.RegionDirty());

	// ★ 再次显示 ⇒ 同样必须请求重绘（两个方向都要覆盖：
	//   只测「隐藏」会漏掉「显示也没重绘」这一半）
	probe.ClearRegion();
	button->SetVisible(true);
	EXPECT_TRUE(probe.RegionDirty());

}

// ── M9-②：★ 同值重复调用 ⇒ **不**请求重绘（防自激励——条 134 同族）───────────
//   ★★ 这条是**正确性守卫**，不是优化：`TextBox::SyncScrollBar` 在 `OnPaint` 起始
//   **每帧**调用 `m_vBar->SetVisible(need)`（`TextBox.cpp:562`）⇒ 若 `SetVisible`
//   无条件 Invalidate ⇒ **每帧请求重绘** ⇒ 自激励重绘循环（WM_PAINT 永不枯竭）。
//   ⇒ 修复若写成「直接加 Invalidate、不加同值守卫」，本条必红（且是**修复自身的缺陷**）。
//   ★ 同族先例：`TextWidget::SetWordWrap:102-105`（值比较 + 早退）。
void TestM9SetVisibleSameValueNoRepaint() {

	M9Probe probe("ECDI_M9_Same");

	probe.hwnd = ::FindWindowW(nullptr, L"ECDI_M9_Same");
	EXPECT_TRUE(probe.hwnd != nullptr);
	if (probe.hwnd == nullptr) return;            // 无 HWND ⇒ 本用例判据失效（不假装通过）

	probe.ShowAndSettle();

	auto owned = std::make_unique<Button>();
	Button* button = owned.get();
	probe.window->GetRootWidget().AddChild(std::move(owned));

	EXPECT_TRUE(button->IsVisible());             // 前置：当前可见

	// ★ 同值（true → true）⇒ 零失效
	probe.ClearRegion();
	button->SetVisible(true);
	EXPECT_TRUE(!probe.RegionDirty());

	// ★ 先真的隐藏（一次状态变化 = 一次脏），再同值隐藏 ⇒ 第二次不得再脏
	probe.ClearRegion();
	button->SetVisible(false);
	EXPECT_TRUE(probe.RegionDirty());             // 前置校验：状态变化确实产生了脏位
	probe.ClearRegion();
	button->SetVisible(false);                    // 同值（false → false）
	EXPECT_TRUE(!probe.RegionDirty());

}

// ── M9-③：★ 反向对照 —— 几何 setter `SetSize` **不**请求重绘（契约未扩大）───────
//   ★ 这条钉住范围边界：M9 只补「可见性」这一条，**没有**顺手把 Invalidate
//     推广到 `SetSize` / `SetPosition`（它们由 `Layout` 逐帧批量调用 ⇒ 加了会每帧重绘）。
//   ⇒ 若将来有人「顺手统一」，本条会红 —— 那就是范围蔓延的信号。
void TestM9SetSizeStillNoRepaint() {

	M9Probe probe("ECDI_M9_Size");

	probe.hwnd = ::FindWindowW(nullptr, L"ECDI_M9_Size");
	EXPECT_TRUE(probe.hwnd != nullptr);
	if (probe.hwnd == nullptr) return;

	probe.ShowAndSettle();

	auto owned = std::make_unique<Button>();
	Button* button = owned.get();
	probe.window->GetRootWidget().AddChild(std::move(owned));

	probe.ClearRegion();
	button->SetSize(120, 40);
	EXPECT_TRUE(!probe.RegionDirty());            // ★ 几何 setter 不请求重绘（既有契约保持）

	probe.ClearRegion();
	button->SetPosition(30, 30);
	EXPECT_TRUE(!probe.RegionDirty());

}

// ── M9-④：★ 量具校准 —— `SetStyle` 确实会让 region 变脏 ─────────────────────
//   ★ 没有这条，前三条可能一起假绿（若 `GetUpdateRect` 根本观测不到框架的 Invalidate，
//     「不变脏」在修复前后都成立，三条全绿但毫无意义）。同族先例：审计原文即
//     「与同族 setter 不一致」中的那个同族 = `SetStyle`（内建 Invalidate）。
void TestM9SetStyleDoesRepaint_GaugeCalibration() {

	M9Probe probe("ECDI_M9_Gauge");

	probe.hwnd = ::FindWindowW(nullptr, L"ECDI_M9_Gauge");
	EXPECT_TRUE(probe.hwnd != nullptr);
	if (probe.hwnd == nullptr) return;

	probe.ShowAndSettle();

	auto owned = std::make_unique<Button>();
	Button* button = owned.get();
	probe.window->GetRootWidget().AddChild(std::move(owned));

	probe.ClearRegion();

	// `SetStyle` 是同族中**已内建 Invalidate** 的那个（`Button.cpp:79` 起）⇒ region 必须变脏
	button->SetStyle(ButtonStyleOverride{ .foreground = Color::White() });
	EXPECT_TRUE(probe.RegionDirty());             // 量具校准：判据能观测到 Invalidate

	// ★ 反向确认：同款 override 再设一次 ⇒ 由 overridden 旗标短路（不进 Apply 逻辑）
	//   ——但 `SetStyle` 本体的 Invalidate 仍会执行（与 M9-② 的同值守卫**形态不同**，
	//     记录这个不对称：M9 的同值守卫是新增的，`SetStyle` 的无条件是既有行为）。
	probe.ClearRegion();
	button->SetStyle(ButtonStyleOverride{ .foreground = Color::White() });
	EXPECT_TRUE(probe.RegionDirty());             // SetStyle 无同值守卫（既有行为，如实记账）

}

// ── M9-⑤：`SetEnabled` **故意不**请求重绘（范围边界，防将来误「补全」）──────────
//   ★ 为何故意不加（审计原文把 `SetVisible` / `SetEnabled` 并列，本批只修前者）：
//     实测 `IsEnabled()` **无任何绘制消费者**——全库只 gate 命中（`Widget.cpp:142`）、
//     焦点收集（`Window.cpp:43`）、键派发（`Window.cpp:677`）；而 `IsVisible()` 是
//     **绘制门控**（`Widget.cpp:135` / `:281` 的 Paint 前置）。
//   ⇒ 给 `SetEnabled` 加 Invalidate = 语义为空的重绘请求。本条钉住这个判断：
//     若将来 `OnPaint` 开始消费 disabled 视觉，本条会红 ⇒ 那就是该加 Invalidate 的信号。
void TestM9SetEnabledNoRepaintByDesign() {

	M9Probe probe("ECDI_M9_Enabled");

	probe.hwnd = ::FindWindowW(nullptr, L"ECDI_M9_Enabled");
	EXPECT_TRUE(probe.hwnd != nullptr);
	if (probe.hwnd == nullptr) return;

	probe.ShowAndSettle();

	auto owned = std::make_unique<Button>();
	Button* button = owned.get();
	probe.window->GetRootWidget().AddChild(std::move(owned));

	probe.ClearRegion();
	button->SetEnabled(false);
	EXPECT_TRUE(!probe.RegionDirty());            // 有意：无绘制消费者 ⇒ 无谓重绘

	// ★ 状态本身必须真的变了（否则「不重绘」是因为 setter 坏了 —— 假绿陷阱）
	EXPECT_TRUE(!button->IsEnabled());
	probe.ClearRegion();
	button->SetEnabled(true);
	EXPECT_TRUE(button->IsEnabled());
	EXPECT_TRUE(!probe.RegionDirty());

}

void ECDI::Test::RegisterVisibilityRepaintTests() {

	GetTestRegistry().Add("Widget.SetVisibleRequestsRepaint",      &TestM9SetVisibleRequestsRepaint);
	GetTestRegistry().Add("Widget.SetVisibleSameValueNoRepaint",   &TestM9SetVisibleSameValueNoRepaint);
	GetTestRegistry().Add("Widget.SetSizeStillNoRepaint",          &TestM9SetSizeStillNoRepaint);
	GetTestRegistry().Add("Widget.SetStyleRepaintsGauge",          &TestM9SetStyleDoesRepaint_GaugeCalibration);
	GetTestRegistry().Add("Widget.SetEnabledNoRepaintByDesign",    &TestM9SetEnabledNoRepaintByDesign);

}
