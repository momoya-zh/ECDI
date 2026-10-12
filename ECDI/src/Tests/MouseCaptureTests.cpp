#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 防御性 undef（规范 10）
#endif

#include "Platform/Win32/Win32PlatformWindow.h"
#include "ECDI/Platform/PlatformWindowHost.h"

#include <string>

using namespace ECDI;

// ══════════════════════════════════════════════════════════════════════
// 审计 #60 / C3：真实鼠标捕获（`::SetCapture` / `::ReleaseCapture`）
// ══════════════════════════════════════════════════════════════════════
// ★ 缺陷：全库从未调用 Win32 捕获 API（审计全仓 grep 0 命中）——「隐式捕获」纯框架记账
//   ⇒ ① 拖拽/滚条**越出客户区即失效**（收不到 WM_MOUSEMOVE，**无自愈路径**，C3 核心）；
//      ② 丢 Up 后框架卡在按下态，唯一下次自愈出口 = 用户下一次任意点击。
// ★ 修法两半（同一次 `SetCaptureWidget` 调用的两条腿）：
//   · `Window::SetCaptureWidget` → `PlatformWindow::SetMouseCapture(bool)`（**OS 侧**）；
//   · `PlatformWindowHost::OnCaptureLost()` ← `WM_CAPTURECHANGED`/`CANCELMODE`/`KILLFOCUS`（**框架侧清理**）。
// ★ 本组装置（沿 DesktopLayerTests 先例）：直接栈上构造 `Win32PlatformWindow` + 最小 Host，
//   不经 Application/Window ⇒ 可直呼平台层与 Host 契约、免三层 static_cast。
//   ⚠️ **测试卫生**：`::SetCapture` 是**进程/线程级真实系统捕获**——本组每个用例结束前
//      **必须**恢复（`ReleaseCapture`），否则会污染同进程后续用例的鼠标消息路由。

namespace{

/// @brief 记录式 Host：观测 `OnCaptureLost` 是否被上报（其余纯虚空实现）
struct CaptureHost final : public PlatformWindowHost{

	int captureLostCount = 0;   ///< OnCaptureLost 调用次数（★ 本组唯一观测面）

	void OnPaint() override{}

	void OnResized(int, int) override{}

	void OnExitSizeMove() override{}

	Window* GetWindow() const noexcept override{ return nullptr; }   // 替身无真 Window

	void OnEvent(const Event&) override{}

	void OnIMEComposition() override{}

	void OnIMECompositionUpdate(const std::string&) override{}

	void OnIMECompositionCommit(const std::string&) override{}

	bool IsClientInteractiveAt(int, int) const noexcept override{ return false; }

	// ★ 覆盖默认空实现（审计 #60：捕获失效上报）——本组可观测点
	void OnCaptureLost() override{ ++captureLostCount; }

};

/// @brief 手动消息泵（DesktopLayerTests.cpp:48 同款——捕获变化消息要真的派发才到）
void PumpMessages(int maxCount){

	MSG msg{};

	for (int i = 0; i < maxCount && PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) != 0; ++i){

		TranslateMessage(&msg);

		DispatchMessageW(&msg);

	}

}

}   // namespace

// ── C3-①：`SetMouseCapture(true)` 真的建立系统捕获（★ 这是修复的核心事实）────────
// 判据 = `::GetCapture() == 本窗口句柄`（修复前 `SetMouseCapture` 根本不存在/为 no-op，
//        该断言必失败——这是"OS 层从未被告知"的直接反证）。
void Test60SetCaptureEstablishesSystemCapture()
{
	CaptureHost host;
	Win32PlatformWindow window(host, "ECDI_C3_CaptureOn", 200, 200);

	const HWND hwnd = window.GetHwndForTests();
	EXPECT_TRUE(hwnd != nullptr);

	// ★ 前置：此刻本线程不应持有捕获（若持有说明前序用例未清理——暴露测试卫生问题）
	EXPECT_TRUE(::GetCapture() == nullptr);

	window.SetMouseCapture(true);

	// ★★ 核心断言：OS 层**确实**知道本窗口持有捕获
	EXPECT_TRUE(::GetCapture() == hwnd);

	// 测试卫生：立即释放（不污染同进程后续用例）
	window.SetMouseCapture(false);
	EXPECT_TRUE(::GetCapture() == nullptr);
}

// ── C3-②：`SetMouseCapture(false)` 释放系统捕获 ──────────────────────────────
void Test60ReleaseCaptureClearsSystemCapture()
{
	CaptureHost host;
	Win32PlatformWindow window(host, "ECDI_C3_CaptureOff", 200, 200);

	window.SetMouseCapture(true);
	EXPECT_TRUE(::GetCapture() == window.GetHwndForTests());

	window.SetMouseCapture(false);
	EXPECT_TRUE(::GetCapture() == nullptr);

	// 幂等：未持有时再释放是安全 no-op（不得崩、不得误伤他人捕获）
	window.SetMouseCapture(false);
	EXPECT_TRUE(::GetCapture() == nullptr);
}

// ── C3-③：释放**不误抢**别人的捕获（只在本窗口持有时才 ReleaseCapture）──────────
// ★ 判据：另一个窗口持有捕获时，本窗口调 `SetMouseCapture(false)` **不得**夺走它。
//   这是 `GetCapture() == m_hwnd` 守卫存在的理由。
void Test60ReleaseDoesNotStealOtherCapture()
{
	CaptureHost hostA;
	CaptureHost hostB;
	Win32PlatformWindow a(hostA, "ECDI_C3_OwnerA", 200, 200);
	Win32PlatformWindow b(hostB, "ECDI_C3_OwnerB", 200, 200);

	a.SetMouseCapture(true);
	EXPECT_TRUE(::GetCapture() == a.GetHwndForTests());

	// b 并未持有捕获 ⇒ 其释放请求必须**不生效**（不得把 a 的捕获放掉）
	b.SetMouseCapture(false);
	EXPECT_TRUE(::GetCapture() == a.GetHwndForTests());   // ★ a 仍持有

	// 卫生：清理
	a.SetMouseCapture(false);
	EXPECT_TRUE(::GetCapture() == nullptr);
}

// ── C3-④：捕获被**第三方夺走** ⇒ 平台层上报 `OnCaptureLost`（缺陷后果②的修复）────
// 语义：别的窗口调 `::SetCapture` 抢走捕获，系统发 `WM_CAPTURECHANGED` 到本窗口
//   ⇒ 平台层必须上报 Host，框架据此清登记（否则卡在按下态直到下次点击）。
void Test60CaptureChangedReportsToHost()
{
	CaptureHost host;
	Win32PlatformWindow window(host, "ECDI_C3_Lost", 200, 200);

	const HWND hwnd = window.GetHwndForTests();
	window.SetMouseCapture(true);
	EXPECT_TRUE(::GetCapture() == hwnd);
	EXPECT_EQ(host.captureLostCount, 0);   // 建立捕获本身**不算**失效

	// 第三方抢走：用一个临时窗口夺取捕获 ⇒ 系统向本窗口投递 WM_CAPTURECHANGED
	CaptureHost thiefHost;
	Win32PlatformWindow thief(thiefHost, "ECDI_C3_Thief", 200, 200);
	thief.SetMouseCapture(true);
	PumpMessages(64);                      // ★ WM_CAPTURECHANGED 需真的派发

	EXPECT_TRUE(::GetCapture() == thief.GetHwndForTests());   // 捕获确已易主
	EXPECT_TRUE(host.captureLostCount >= 1);                  // ★ 本窗口已被上报

	// 卫生
	thief.SetMouseCapture(false);
	EXPECT_TRUE(::GetCapture() == nullptr);
}

// ── C3-⑤：我方的主动释放**不得**被当成"捕获失效"上报（★ 可判别断言）──────────
// ★ 为什么需要它：`::ReleaseCapture` 同样会让系统投递 `WM_CAPTURECHANGED`（系统消息
//   **无法区分**"我方主动释放"与"被第三方夺走"——它不带这个因果）⇒ 若不抑制，
//   「一次主动释放」会产生「一次伪失捕上报」，令 `OnCaptureLost` 失去诊断价值
//   （框架侧虽幂等无害，但计数不再可用）。抑制由平台层的 `m_releasingCapture` 作用域
//   守卫提供（见 `Win32PlatformWindow::SetMouseCapture`）。
// ★ 判别力：本用例断言 `== 0`，而 **C3-④（被夺走）断言 `>= 1`** ⇒ 两者取值相反、
//   互为对照 ⇒ 两态可区分（不是"两边都不发生"的假绿）。
void Test60OwnReleaseDoesNotReportLost()
{
	CaptureHost host;
	Win32PlatformWindow window(host, "ECDI_C3_OwnRelease", 200, 200);

	window.SetMouseCapture(true);
	EXPECT_TRUE(::GetCapture() == window.GetHwndForTests());
	EXPECT_EQ(host.captureLostCount, 0);

	window.SetMouseCapture(false);
	PumpMessages(64);   // ★ 泵掉我方释放引发的 WM_CAPTURECHANGED

	EXPECT_TRUE(::GetCapture() == nullptr);     // 释放确已生效
	EXPECT_EQ(host.captureLostCount, 0);        // ★ 主动释放**不**算失效（与 C3-④ 互为反例）
}

// ── C3-⑥：`WM_KILLFOCUS` 也触发失捕上报（Alt+Tab 路径）─────────────────────────
void Test60KillFocusReportsLost()
{
	CaptureHost host;
	Win32PlatformWindow window(host, "ECDI_C3_KillFocus", 200, 200);

	const HWND hwnd = window.GetHwndForTests();
	window.SetMouseCapture(true);
	EXPECT_TRUE(::GetCapture() == hwnd);
	EXPECT_EQ(host.captureLostCount, 0);

	// 直接投递 WM_KILLFOCUS（等价 Alt+Tab 切走——无需真切前台，避免干扰用户桌面）
	::SendMessageW(hwnd, WM_KILLFOCUS, 0, 0);
	EXPECT_TRUE(host.captureLostCount >= 1);   // ★ 已上报

	window.SetMouseCapture(false);
}

// ★ 定义须**全限定**（`ECDI::Test::`）——声明在 `RunAllTests.h` 的 `ECDI::Test` 命名空间内；
//   写成全局作用域会定义出 `::RegisterMouseCaptureTests`（另一个符号）⇒ 链接期 LNK2019
//   （本项目 32/33 个先例同款；`-fsyntax-only` 不解析符号 ⇒ 必须做链接级验证才发现）。
//   ⚠️ 本文件顶部有 `using namespace ECDI;` ⇒ 成员定义**必须**写全限定名，
//      否则 GCC 报 "should have been declared inside 'ECDI::Test'"。
void ECDI::Test::RegisterMouseCaptureTests()
{
	GetTestRegistry().Add("MouseCapture.EstablishesSystemCapture", &Test60SetCaptureEstablishesSystemCapture);
	GetTestRegistry().Add("MouseCapture.ReleaseClearsSystemCapture", &Test60ReleaseCaptureClearsSystemCapture);
	GetTestRegistry().Add("MouseCapture.ReleaseDoesNotStealOther",  &Test60ReleaseDoesNotStealOtherCapture);
	GetTestRegistry().Add("MouseCapture.CaptureChangedReportsLost", &Test60CaptureChangedReportsToHost);
	GetTestRegistry().Add("MouseCapture.OwnReleaseIsSafe",          &Test60OwnReleaseDoesNotReportLost);
	GetTestRegistry().Add("MouseCapture.KillFocusReportsLost",      &Test60KillFocusReportsLost);
}
