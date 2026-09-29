#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 防御性 undef（规范 10）：本文件含 Windows.h——防 DrawTextW 宏污染 ECDI 头声明
#endif

#include "ECDI/Application/Application.h"
#include "ECDI/Platform/PlatformWindow.h"
#include "ECDI/Window/Window.h"
#include "Platform/Win32/DpiConversion.h"       // PixelsToDip：外框 DIP 换算（唯一真相源）
#include "Platform/Win32/Win32RenderContext.h"  // 三跳取 HWND

#include <string>

using namespace ECDI;

namespace {

// ══════════════════════════════════════════════════════════════════
// Phase 22：`Create` 的 DIP 尺寸契约（pre-show DIP geometry）—— T22-1..T22-3
// ★ 本组**必须**用真实窗口（`Application::Create` ⇒ `Win32PlatformWindow`）——
//   被测行为发生在「`Create` 返回」这一时刻，任何替身都到达不了该入口。
// ══════════════════════════════════════════════════════════════════

constexpr int kWinW = 800;
constexpr int kWinH = 600;

/// @brief 最小 Application（无事件观测需求——本组判据全在几何上）
struct TestApp : public Application {};

/// @brief 场景守卫（`CaptionFixture` 同款：本对象必须先于 Application 析构）
struct GeometryFixture {

	Window* window = nullptr;

	explicit GeometryFixture(Application& a) {

		window = &a.Create("ECDI_PreshowGeometry", kWinW, kWinH);   // ★ 第 4 参用默认值

	}

	~GeometryFixture() {

		window->Release();   // 只销毁 HWND（WM_DESTROY → Application 回收 Window 对象）

	}

	/// @brief 取底层 HWND（三跳——`CaptionBarTests.cpp:118-123` 同款）
	HWND Handle() const {

		return static_cast<const Win32RenderContext&>(
			window->GetPlatformWindow().GetRenderContext()).GetHandle();

	}

};

/// @brief 外框尺寸的 **DIP**（契约 C1 的判据量 = 「窗口总尺寸（含边框和标题栏）」）
/// @details ★ 必须用 `GetWindowRect`（外框）而不是 `GetClientSize`（客户区）——
///          契约说的是**总尺寸**（`Window.h:236-237`）。
Size OuterSizeDip(HWND hwnd) {

	RECT r{};

	GetWindowRect(hwnd, &r);

	const int dpi = GetDpiForWindow(hwnd);

	return Size{ static_cast<float>(PixelsToDip(r.right - r.left, dpi)),
	             static_cast<float>(PixelsToDip(r.bottom - r.top, dpi)) };

}

// ── T22-1 ★★ 时序稳定性契约：`Create` 后的几何 == `Show` 后的几何 ─────────────
// ★ 职责（评审 §6 采纳）：本用例**不**直接证明「`Create` 后已等于请求尺寸」，
//   它证明的是「**`Create` 与 `Show` 之间不发生几何变化**」。
// ★ 失败归因：**T22-1 失败 ⇒ 判「Create / Show 时序不稳定」**。
// ★ 环境行为：96 DPI 下两侧本就相等 ⇒ **恒真**（不误报）；非 96 DPI 下
//   改前 `Create` 后是 640×480 DIP、`Show` 后 800×600 ⇒ **必红**。
void Test22GeometryStableAcrossShow()
{
	TestApp app;
	GeometryFixture fx(app);

	const Size afterCreate = OuterSizeDip(fx.Handle());   // ★ Create 已返回（含 Phase 22 的落实）

	fx.window->Show();

	const Size afterShow = OuterSizeDip(fx.Handle());

	EXPECT_EQ(afterCreate.width, afterShow.width);
	EXPECT_EQ(afterCreate.height, afterShow.height);
}

// ── T22-2 幂等（★ platform seam contract test——非公共 API 行为测试）─────────
// ★ 定位（评审 §7 采纳）：本用例经 `Window::GetPlatformWindow()` **直调平台接口**，
//   属**接缝契约测试**，验的就是 `ApplyStartupSize()` 的**幂等性**（契约 C3）。
// ★★ **证明边界（详设 v1.1 / 评审 R-① 采纳）**：本用例证明的是「**几何结果幂等**」
//   ——连续两次调用前后尺寸逐项相等。★ 它**不直接观察 `WM_SIZE` 是否派发**；
//   「同尺寸 `SetWindowPos` 不派发 `WM_SIZE`」的证据在**需求稿 §1.7 的实测**（非本用例）。
void Test22ApplyStartupSizeIdempotent()
{
	TestApp app;
	GeometryFixture fx(app);

	const Size before = OuterSizeDip(fx.Handle());

	fx.window->GetPlatformWindow().ApplyStartupSize();

	const Size after1 = OuterSizeDip(fx.Handle());

	fx.window->GetPlatformWindow().ApplyStartupSize();

	const Size after2 = OuterSizeDip(fx.Handle());

	EXPECT_EQ(before.width, after1.width);
	EXPECT_EQ(after1.width, after2.width);
	EXPECT_EQ(after1.height, after2.height);
}

// ── T22-3 直接 DIP 契约（C1 的直接判据——条件式）───────────────────────────
// ★ 职责（评审 §6 采纳）：**直接**断言「`Create` 后外框 DIP == 请求值」。
// ★ 失败归因：**T22-3 失败 ⇒ 判「`Create` 阶段根本没有正确落实 DIP」**。
// ★ 条件式：96 DPI 机器上跳过（用例仍通过；契约由 G5 恒等保证——见 C2 的两层口径）。
void Test22CreateGeometryMatchesRequestAtNon96Dpi()
{
	TestApp app;
	GeometryFixture fx(app);

	const HWND hwnd = fx.Handle();

	if (GetDpiForWindow(hwnd) != 96) {

		const Size dip = OuterSizeDip(hwnd);

		EXPECT_EQ(dip.width, static_cast<float>(kWinW));
		EXPECT_EQ(dip.height, static_cast<float>(kWinH));

	}

}

}   // anonymous namespace

void ECDI::Test::RegisterPreshowGeometryTests()
{
	GetTestRegistry().Add("PreshowGeometry.GeometryStableAcrossShow",      &Test22GeometryStableAcrossShow);
	GetTestRegistry().Add("PreshowGeometry.ApplyStartupSizeIdempotent",    &Test22ApplyStartupSizeIdempotent);
	GetTestRegistry().Add("PreshowGeometry.CreateGeometryMatchesRequest",  &Test22CreateGeometryMatchesRequestAtNon96Dpi);
}
