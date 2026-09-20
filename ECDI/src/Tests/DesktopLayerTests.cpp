#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 防御性 undef（规范 10）
#endif

#include "Platform/Win32/Win32PlatformWindow.h"
#include "ECDI/Window/WindowLayer.h"

#include <string>

using namespace ECDI;

// ── 最小 Host 替身：本组用例不经 Application / Window ────────────────────────
// 直接栈上构造 Win32PlatformWindow（DropFilesTests.cpp:133 先例）⇒ 直呼内部方法与
// 观测缝，免去 Window → PlatformWindow → Win32PlatformWindow 的三层 static_cast。

namespace{

/// @brief 最小 Host 替身（PlatformWindowHost 的 9 个纯虚全部空实现）
/// @details 无真 Window / 无事件消费——本组只关心层级、样式位与钩子生命周期。
struct LayerHost final : public PlatformWindowHost{

	void OnPaint() override{}

	void OnResized(int, int) override{}

	void OnExitSizeMove() override{}

	Window* GetWindow() const noexcept override{ return nullptr; }   // 测试替身无真 Window

	void OnEvent(const Event&) override{}

	void OnIMEComposition() override{}

	void OnIMECompositionUpdate(const std::string&) override{}

	void OnIMECompositionCommit(const std::string&) override{}

	bool IsClientInteractiveAt(int, int) const noexcept override{ return false; }

};

/// @brief 手动消息泵（WindowChromeTests.cpp 同款）
/// @details 层级维护发生在 WM_WINDOWPOSCHANGING 上 ⇒ 必须真的泵一次消息才生效。
void PumpMessages(int maxCount){

	MSG msg{};

	for (int i = 0; i < maxCount && PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE); ++i){

		TranslateMessage(&msg);

		DispatchMessageW(&msg);

	}

}

/// @brief 带等待的消息泵（毫秒级）——定时器驱动的链路需要**真实时间流逝**
/// @details `PumpMessages` 是「有多少泵多少」即刻返回；而 `SetTimer` 的 `WM_TIMER`
/// 要到期才进队列 ⇒ 桌面跟随的多拍重试必须配「泵 + 小睡」的循环。
void PumpMessagesFor(int ms){

	const ULONGLONG end = GetTickCount64() + static_cast<ULONGLONG>(ms);

	MSG msg{};

	while (GetTickCount64() < end){

		while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)){

			TranslateMessage(&msg);

			DispatchMessageW(&msg);

		}

		Sleep(1);

	}

}


/// @brief z 序判据：`upper` 是否位于 `lower` **之上**
/// @details `GetWindow(h, GW_HWNDNEXT)` = 给定窗口**下方**的窗口 ⇒ 自 `upper` 沿链
/// 向下走，遇到 `lower` 即为真。⚠️ 迭代上限防御异常 z 序造成的死循环（正常链长远小于该值）。
bool IsAboveInZOrder(HWND upper, HWND lower){

	for (int guard = 0; guard < 4096; ++guard){

		upper = GetWindow(upper, GW_HWNDNEXT);

		if (upper == nullptr){ return false; }

		if (upper == lower){ return true; }

	}

	return false;

}

// ── 钩子观测缝的计数落点（生产恒 nullptr——见 SetDesktopHookObserverForTests）──
// ⚠️ T16-4 的 on == 1 同时是一条**环境断言**：它成立意味着 SetWinEventHook 在本机
//    真的成功了。若它失败（钩子为 nullptr），观测缝根本不会被调用 ⇒ 本用例响亮失败，
//    其含义是「Desktop 档在本环境完全不可用」——正是应当失败的场景（详设 §8 L6）。

int g_hookOn  = 0;

int g_hookOff = 0;

void CountingHookObserver(bool installed){ installed ? ++g_hookOn : ++g_hookOff; }

// ── T16-1：Bottom 档回归（★ 补 Phase 12 的空缺）────────────────────────────

void TestBottomLayerRegression(){

	LayerHost hostRef;

	LayerHost hostBot;

	Win32PlatformWindow refWindow(hostRef, "P16RefNormal", 200, 200);

	Win32PlatformWindow botWindow(hostBot, "P16BotLayer", 200, 200);

	refWindow.Show();

	botWindow.SetWindowLayer(WindowLayer::Bottom);

	botWindow.Show();

	PumpMessages(64);

	const HWND refHwnd = refWindow.GetHwndForTests();

	const HWND botHwnd = botWindow.GetHwndForTests();

	// 正对照（skill 条 75：先证前提成立）：两窗真的可见——否则 z 序判据无意义
	EXPECT_TRUE(IsWindowVisible(refHwnd) != FALSE);

	EXPECT_TRUE(IsWindowVisible(botHwnd) != FALSE);

	// ★ 被测量：`bot` 后显示、却被维护到 `ref` 之下 ⇒ Bottom 档维护确实在工作
	EXPECT_TRUE(IsAboveInZOrder(refHwnd, botHwnd));

}

// ── T16-2：Desktop 档移除 WS_MINIMIZEBOX，其余位逐位保持（D1 A′）───────────

void TestStyleStripsMinimizeBox(){

	LayerHost host;

	Win32PlatformWindow window(host, "P16StyleStrip", 200, 200);

	const HWND hwnd = window.GetHwndForTests();

	const LONG_PTR before = GetWindowLongPtrW(hwnd, GWL_STYLE);

	// 前提：初始确实带 WS_MINIMIZEBOX——否则「移除」无从谈起（正对照）
	EXPECT_TRUE((before & WS_MINIMIZEBOX) != 0);

	window.SetWindowLayer(WindowLayer::Desktop);

	const LONG_PTR after = GetWindowLongPtrW(hwnd, GWL_STYLE);

	EXPECT_TRUE((after & WS_MINIMIZEBOX) == 0);

	// ★ D1 A′ 的全部理由：只放弃「可最小化」这一条，其余位逐位保持
	const LONG_PTR keep = ~static_cast<LONG_PTR>(WS_MINIMIZEBOX);

	EXPECT_TRUE((after & keep) == (before & keep));

	EXPECT_TRUE((after & WS_CAPTION) != 0);

	EXPECT_TRUE((after & WS_THICKFRAME) != 0);

	EXPECT_TRUE((after & WS_SYSMENU) != 0);

	EXPECT_TRUE((after & WS_MAXIMIZEBOX) != 0);

}

// ── T16-3：样式往返（可逆）+ 同档位重复设置（幂等）────────────────────────

void TestStyleRoundTripAndIdempotent(){

	LayerHost host;

	Win32PlatformWindow window(host, "P16StyleRT", 200, 200);

	const HWND hwnd = window.GetHwndForTests();

	const LONG_PTR original = GetWindowLongPtrW(hwnd, GWL_STYLE);

	window.SetWindowLayer(WindowLayer::Desktop);

	EXPECT_TRUE((GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_MINIMIZEBOX) == 0);

	// 可逆：离档 ⇒ 逐位复原（配置期内允许 Desktop → Normal 转移）
	window.SetWindowLayer(WindowLayer::Normal);

	EXPECT_TRUE(GetWindowLongPtrW(hwnd, GWL_STYLE) == original);

	// 幂等：同档位连设两次，第二次被 `m_windowLayer == layer` 提前返回 ⇒ 读值不变
	window.SetWindowLayer(WindowLayer::Desktop);

	const LONG_PTR first = GetWindowLongPtrW(hwnd, GWL_STYLE);

	window.SetWindowLayer(WindowLayer::Desktop);

	EXPECT_TRUE(GetWindowLongPtrW(hwnd, GWL_STYLE) == first);

	window.SetWindowLayer(WindowLayer::Normal);

	EXPECT_TRUE(GetWindowLongPtrW(hwnd, GWL_STYLE) == original);

}

// ── T16-4：钩子生命周期（D11 四态）× 两窗口（必须拆——见 §2.5.5）──────────

void TestHookLifecycle(){

	// ── 窗口 A：装 ×1 · Hide 不卸 · Show 不重装 · Release 必卸 ×1 ──
	{
		LayerHost host;

		Win32PlatformWindow window(host, "P16HookA", 200, 200);

		window.SetDesktopHookObserverForTests(&CountingHookObserver);

		window.SetWindowLayer(WindowLayer::Desktop);

		EXPECT_TRUE(g_hookOn == 1);

		EXPECT_TRUE(g_hookOff == 0);

		window.Show();

		PumpMessages(32);

		EXPECT_TRUE(g_hookOn == 1);   // ★ 装恰好一次

		EXPECT_TRUE(g_hookOff == 0);

		window.Hide();

		PumpMessages(32);

		EXPECT_TRUE(g_hookOn == 1);   // ★ Hide 不卸（钩子生命周期 ≠ 可见性生命周期）

		EXPECT_TRUE(g_hookOff == 0);

		window.Show();

		PumpMessages(32);

		EXPECT_TRUE(g_hookOn == 1);   // ★ Show 不重装（幂等）

		EXPECT_TRUE(g_hookOff == 0);

		window.Release();

		EXPECT_TRUE(g_hookOn == 1);

		EXPECT_TRUE(g_hookOff == 1);   // ★ 必卸，且不重复
	}

	// 出块析构会再调一次 Release ⇒ 幂等 ⇒ 计数不得变化
	EXPECT_TRUE(g_hookOn == 1);

	EXPECT_TRUE(g_hookOff == 1);

	// ── 窗口 B：离档 ⇒ 卸 ×1（配置期内——A 无法覆盖，因 Show 后切层被 C10 拒绝）──
	{
		LayerHost host;

		Win32PlatformWindow window(host, "P16HookB", 200, 200);

		window.SetDesktopHookObserverForTests(&CountingHookObserver);

		window.SetWindowLayer(WindowLayer::Desktop);

		EXPECT_TRUE(g_hookOn == 2);

		window.SetWindowLayer(WindowLayer::Normal);

		EXPECT_TRUE(g_hookOn == 2);   // ★ 离档不装

		EXPECT_TRUE(g_hookOff == 2);   // ★ 离档 ⇒ 卸

	}

}

// ── T16-5：Normal 档不受维护（含正对照——否则「不变」与「机制没跑」不可区分）──

void TestNormalLayerNotMaintained(){

	LayerHost hostRef;

	LayerHost hostNormal;

	LayerHost hostBottom;

	Win32PlatformWindow refWindow(hostRef, "P16T5Ref", 200, 200);

	Win32PlatformWindow normalWindow(hostNormal, "P16T5Normal", 200, 200);

	Win32PlatformWindow bottomWindow(hostBottom, "P16T5Bottom", 200, 200);

	const HWND refHwnd = refWindow.GetHwndForTests();

	const HWND normalHwnd = normalWindow.GetHwndForTests();

	const HWND bottomHwnd = bottomWindow.GetHwndForTests();

	refWindow.Show();

	PumpMessages(32);

	// ★ 正对照：先证明维护机制确实在工作（Bottom 档被压到参照之下）——
	//   否则下一条「Normal 在上」也可能只是「机制根本没跑」的另一种表现
	bottomWindow.SetWindowLayer(WindowLayer::Bottom);

	bottomWindow.Show();

	PumpMessages(64);

	EXPECT_TRUE(IsAboveInZOrder(refHwnd, bottomHwnd));

	// ★ 被测量：Normal 档不受维护 ⇒ 后显示者保持在上（契约 C3）
	normalWindow.Show();

	PumpMessages(64);

	EXPECT_TRUE(IsAboveInZOrder(normalHwnd, refHwnd));

}

// ── T16-6：配置期契约（Show 后拒绝切层——两个独立观测代理均不变）──────────

void TestConfigTimeContract(){

	LayerHost host;

	Win32PlatformWindow window(host, "P16CfgTime", 200, 200);

	window.SetDesktopHookObserverForTests(&CountingHookObserver);

	window.SetWindowLayer(WindowLayer::Desktop);

	const HWND hwnd = window.GetHwndForTests();

	window.Show();

	PumpMessages(32);

	// 两个**独立**观测代理：样式位 + 钩子计数（任一路径被误开都会露出来）。
	// ⚠️ 基线必须在 **Show 之后**记录——`GWL_STYLE` 的整值含**系统动态位**
	//（`WS_VISIBLE` 由 `ShowWindow` 写入）：Show 前取的基线与 Show 后必然差一个位，
	// 整值比较会**假失败**（首轮实测即如此：仅样式断言失败、钩子计数两条全过，
	// 恰证明 `SetWindowLayer` 主体确实被拒——变的只有系统动态位）。
	const LONG_PTR styled = GetWindowLongPtrW(hwnd, GWL_STYLE);

	const int onBefore = g_hookOn;

	const int offBefore = g_hookOff;

	// 运行期切层 ⇒ Warning + 忽略（契约 C10）
	window.SetWindowLayer(WindowLayer::Normal);

	window.SetWindowLayer(WindowLayer::Bottom);

	PumpMessages(16);

	// ★ 比较屏蔽系统自管位（`WS_VISIBLE` / `WS_MINIMIZE` / `WS_MAXIMIZE`——它们不归
	// 本契约管）：若被拒路径错跑了 `ApplyDesktopStyle(false)`，`WS_MINIMIZEBOX` 会被
	// 补回 ⇒ 掩码比较照样抓得到。
	const LONG_PTR kDynamic = static_cast<LONG_PTR>(WS_VISIBLE | WS_MINIMIZE | WS_MAXIMIZE);

	EXPECT_TRUE((GetWindowLongPtrW(hwnd, GWL_STYLE) & ~kDynamic) == (styled & ~kDynamic));

	EXPECT_TRUE(g_hookOn == onBefore);

	EXPECT_TRUE(g_hookOff == offBefore);

}

// ── T16-7：ResolveTarget 真值表（纯函数——不需要 explorer、不需要 Show）────
// ⚠️ 顺序即判据（详设 §2.5.6）：先取得失效句柄并断言其确实失效，再创建 live 窗口；
//    反过来系统**可能回收** dead 的句柄值 ⇒ 「失效句柄」前提不成立（假阴性）。

void TestResolveTargetTruthTable(){

	// ① Normal + nullptr ⇒ 跳过哨兵
	EXPECT_TRUE(Win32PlatformWindow::ResolveTarget(WindowLayer::Normal, nullptr) == nullptr);

	// ② 正对照：造一个真实的失效句柄，并证明它确实失效了
	HWND dead = nullptr;

	{
		LayerHost host;

		Win32PlatformWindow tmp(host, "P16DeadTmp", 200, 200);

		dead = tmp.GetHwndForTests();

		EXPECT_TRUE(IsWindow(dead) != FALSE);

		tmp.Release();

		EXPECT_TRUE(IsWindow(dead) == FALSE);
	}

	// ③ Desktop + nullptr ⇒ 跳过（且绝不是 HWND_BOTTOM）
	const HWND r3 = Win32PlatformWindow::ResolveTarget(WindowLayer::Desktop, nullptr);

	EXPECT_TRUE(r3 == nullptr);

	EXPECT_TRUE(r3 != HWND_BOTTOM);

	// ④ ★ C2 的核心：Desktop + 失效句柄 ⇒ 跳过，绝不降级成 HWND_BOTTOM
	const HWND r4 = Win32PlatformWindow::ResolveTarget(WindowLayer::Desktop, dead);

	EXPECT_TRUE(r4 == nullptr);

	EXPECT_TRUE(r4 != HWND_BOTTOM);

	// ⑤ 有效句柄：Bottom 与 desktop 参数无关；Desktop 取其上一位
	LayerHost host;

	Win32PlatformWindow live(host, "P16Live", 200, 200);

	const HWND valid = live.GetHwndForTests();

	EXPECT_TRUE(IsWindow(valid) != FALSE);

	EXPECT_TRUE(Win32PlatformWindow::ResolveTarget(WindowLayer::Normal, valid) == nullptr);

	EXPECT_TRUE(Win32PlatformWindow::ResolveTarget(WindowLayer::Bottom, nullptr) == HWND_BOTTOM);

	EXPECT_TRUE(Win32PlatformWindow::ResolveTarget(WindowLayer::Bottom, valid) == HWND_BOTTOM);

	const HWND desktopTarget = Win32PlatformWindow::ResolveTarget(WindowLayer::Desktop, valid);

	EXPECT_TRUE(desktopTarget == GetWindow(valid, GW_HWNDPREV));

	EXPECT_TRUE(desktopTarget != HWND_BOTTOM);

}

// ── T16-8：桌面跟随链（A5 修复：延后一拍 + 有界重试）────────────────────────
// ⚠️ 覆盖边界：本用例验证「**消息 → 多拍重试 → 终点紧贴桌面窗口正上方**」这一机制
//    与收敛性；**不**覆盖「与外壳抬桌面抢时序」——那需要真实 Win+D ⇒ 属手测 A5 判据①。
//    本环境没有外壳在竞争 ⇒ 第 1 拍即可命中；**拍数不可观测**（无观测缝——
//    `follow ended but NOT above desktop (raise max steps)` 警告是其失败信号）。

void TestDesktopFollowChain(){

	LayerHost host;

	Win32PlatformWindow window(host, "P16Follow", 200, 200);

	const HWND hwnd = window.GetHwndForTests();

	window.SetWindowLayer(WindowLayer::Desktop);

	window.Show();

	PumpMessages(32);

	const HWND desktop = GetShellWindow();

	EXPECT_TRUE(desktop != nullptr);

	if (desktop == nullptr){ return; }

	// 正对照 A：配置期的初始插入已把窗口放在桌面窗口正上方
	EXPECT_TRUE(GetWindow(desktop, GW_HWNDPREV) == hwnd);

	// 制造「不在位」——把**桌面抬到普通带最顶**（复刻 Win+D 的机制：桌面被抬升，
	// 我们因此落到它之下）。⚠️ 变的是桌面窗口 ⇒ **不会**触发本窗口的
	// `WM_WINDOWPOSCHANGING`；若改成「压自己」，Desktop 分支会当场把 z 序纠正回来
	// ⇒ 压不下去（这正是 `ResolveTarget` 持续维护的语义）。
	SetWindowPos(desktop, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

	PumpMessages(16);

	// 正对照 B：确实已不在位（否则下一条断言可能只是「本来就位」的**假绿**——条 75）
	EXPECT_TRUE(GetWindow(desktop, GW_HWNDPREV) != hwnd);

	// 投递跟随消息。⚠️ 字面量与 `.cpp` 匿名 namespace 的 `kDesktopFollowMsg` 同源
	//（`WM_APP + 2`，测试无法引用 TU 内部常量）——若实现改号，本用例会失败 ⇒ 漂移保护。
	PostMessageW(hwnd, WM_APP + 2, 0, 0);

	// 4 拍 × 16ms ≈ 64ms ⇒ 留约 5× 余量
	PumpMessagesFor(300);

	// ★ 被测量：跟随链把窗口插回桌面窗口正上方
	EXPECT_TRUE(GetWindow(desktop, GW_HWNDPREV) == hwnd);

	// 还原：桌面回 z 序最底（系统常态——避免把副作用留给你）
	SetWindowPos(desktop, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

	PumpMessages(16);

}


}

void ECDI::Test::RegisterDesktopLayerTests(){

	GetTestRegistry().Add("DesktopLayer.BottomLayerRegression",         &TestBottomLayerRegression);
	GetTestRegistry().Add("DesktopLayer.StyleStripsMinimizeBox",        &TestStyleStripsMinimizeBox);
	GetTestRegistry().Add("DesktopLayer.StyleRoundTripAndIdempotent",   &TestStyleRoundTripAndIdempotent);
	GetTestRegistry().Add("DesktopLayer.HookLifecycle",                 &TestHookLifecycle);
	GetTestRegistry().Add("DesktopLayer.NormalLayerNotMaintained",      &TestNormalLayerNotMaintained);
	GetTestRegistry().Add("DesktopLayer.ConfigTimeContract",            &TestConfigTimeContract);
	GetTestRegistry().Add("DesktopLayer.ResolveTargetTruthTable",       &TestResolveTargetTruthTable);
	GetTestRegistry().Add("DesktopLayer.DesktopFollowChain",         &TestDesktopFollowChain);

}
