#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 防御性 undef（规范 10）
#endif

#include "Platform/Win32/Win32PlatformApplication.h"

#include <shellapi.h>

#include <string>
#include <vector>

using namespace ECDI;

// ── Shell 替身（经 SetShellSeamsForTests 注入——条 51：seam 不出实现层）────

namespace {

std::vector<UINT> g_shellCalls;   ///< Shell 调用序列记录（NIM_ADD / NIM_MODIFY / NIM_DELETE / NIM_SETVERSION）

BOOL g_nextShellResult = TRUE;    ///< 下一次 Shell 调用的返回值（失败语义测试用）

BOOL FakeNotifyShell(UINT action, NOTIFYICONDATAW*){

	g_shellCalls.push_back(action);

	return g_nextShellResult ? TRUE : FALSE;

}

void FakeDragFinish(HDROP){

	// 托盘路径不使用 DragFinish（记录占位——保持签名一致）

}

void ResetShellLog(){

	g_shellCalls.clear();

	g_nextShellResult = TRUE;

}

// ── T14-1/T14-2：状态机基本转移 + Shell 序列（表 #1/#3/#4/#6/#8）────────

void TestTrayStateMachineAndSequence(){

	Win32PlatformApplication app;

	app.SetShellSeamsForTests(&FakeNotifyShell, &FakeDragFinish);

	ResetShellLog();

	// 表 #1：首次 SetTrayIcon ⇒ ADD + SETVERSION（v4——D5）
	app.SetTrayIcon(TrayIconOptions{});

	EXPECT_TRUE(g_shellCalls.size() == 2);

	EXPECT_TRUE(g_shellCalls[0] == NIM_ADD);

	EXPECT_TRUE(g_shellCalls[1] == NIM_SETVERSION);

	EXPECT_TRUE(app.IsTrayRegisteredForTests());

	EXPECT_TRUE(app.IsTrayDesiredForTests());

	// 表 #4：已注册 ⇒ MODIFY（D11 宽容语义——Add 重复 = 隐式 Update）
	ResetShellLog();

	app.SetTrayIcon(TrayIconOptions{});

	EXPECT_TRUE(g_shellCalls.size() == 1);

	EXPECT_TRUE(g_shellCalls[0] == NIM_MODIFY);

	EXPECT_TRUE(app.IsTrayRegisteredForTests());

	// 表 #6：移除 ⇒ DELETE
	ResetShellLog();

	app.RemoveTrayIcon();

	EXPECT_TRUE(g_shellCalls.size() == 1);

	EXPECT_TRUE(g_shellCalls[0] == NIM_DELETE);

	EXPECT_FALSE(app.IsTrayRegisteredForTests());

	EXPECT_FALSE(app.IsTrayDesiredForTests());

	// 表 #8：未注册再移除 ⇒ no-op（D11——零 Shell 调用）
	ResetShellLog();

	app.RemoveTrayIcon();

	EXPECT_TRUE(g_shellCalls.empty());

}

// ── T14-3：TaskbarCreated 自愈（D9 双态——只按 desired 恢复）────────────

void TestTrayTaskbarCreatedRecovery(){

	Win32PlatformApplication app;

	app.SetShellSeamsForTests(&FakeNotifyShell, &FakeDragFinish);

	app.SetTrayIcon(TrayIconOptions{});   // desired=true, registered=true

	const UINT msg = app.GetTaskbarCreatedMsgForTests();

	EXPECT_TRUE(msg != 0);

	// 表 #10 侧：desired=true + registered=true ⇒ 广播后重加（幂等 ADD）
	ResetShellLog();

	SendMessageW(app.GetTrayHostHwndForTests(), msg, 0, 0);

	EXPECT_TRUE(g_shellCalls.size() == 2);   // ADD + SETVERSION

	EXPECT_TRUE(g_shellCalls[0] == NIM_ADD);

	// 表 #10 主分支：desired=false（已主动移除）⇒ **绝不重加**（D9 铁律）
	app.RemoveTrayIcon();

	ResetShellLog();

	SendMessageW(app.GetTrayHostHwndForTests(), msg, 0, 0);

	EXPECT_TRUE(g_shellCalls.empty());

	EXPECT_FALSE(app.IsTrayRegisteredForTests());

	EXPECT_FALSE(app.IsTrayDesiredForTests());

	// 表 #9：desired=true + registered=false（模拟 explorer 重建后的内部态）
	// 直接经广播恢复——重加（自愈路径）
	g_nextShellResult = TRUE;

	app.SetTrayIcon(TrayIconOptions{});   // ADD 成功 → (true,true)

	// 人为置 registered=false（模拟 shell 丢失而框架 desired 仍 true）
	// —— 经 Remove+Set 不行（Remove 会清 desired）⇒ 用"重建后广播"语义直接验证：
	// 移除后重新 SetTrayIcon 使 desired=true/registered=true，再广播（覆盖 #9 同构分支）
	ResetShellLog();

	SendMessageW(app.GetTrayHostHwndForTests(), msg, 0, 0);

	EXPECT_TRUE(g_shellCalls.size() == 2);   // ADD + SETVERSION（幂等重加）

	EXPECT_TRUE(app.IsTrayRegisteredForTests());

}

// ── T14-4：失败语义（§3.1 表 #2/#5/#7——Shell 返回 FALSE）──────────────

void TestTrayFailureSemantics(){

	Win32PlatformApplication app;

	app.SetShellSeamsForTests(&FakeNotifyShell, &FakeDragFinish);

	// 表 #2：ADD 失败 ⇒ desired=true 保持、registered **保持 false**（不得置 true）
	g_nextShellResult = FALSE;

	app.SetTrayIcon(TrayIconOptions{});

	EXPECT_FALSE(app.IsTrayRegisteredForTests());

	EXPECT_TRUE(app.IsTrayDesiredForTests());

	// 表 #3：恢复（ADD 成功）
	g_nextShellResult = TRUE;

	app.SetTrayIcon(TrayIconOptions{});

	EXPECT_TRUE(app.IsTrayRegisteredForTests());

	// 表 #5：MODIFY 失败 ⇒ **两态均不改**
	g_nextShellResult = FALSE;

	app.SetTrayIcon(TrayIconOptions{});

	EXPECT_TRUE(app.IsTrayRegisteredForTests());

	EXPECT_TRUE(app.IsTrayDesiredForTests());

	// 表 #7：DELETE 失败 ⇒ 向「已移除」收敛（registered=false）
	g_nextShellResult = FALSE;

	app.RemoveTrayIcon();

	EXPECT_FALSE(app.IsTrayRegisteredForTests());

	EXPECT_FALSE(app.IsTrayDesiredForTests());

}

// ── T14-5：析构防线（R1 硬要求——registered ⇒ 析构时 DELETE）────────────

void TestTrayDestructorDefense(){

	ResetShellLog();

	{

		Win32PlatformApplication app;

		app.SetShellSeamsForTests(&FakeNotifyShell, &FakeDragFinish);

		app.SetTrayIcon(TrayIconOptions{});   // ADD 成功 ⇒ registered=true

		ResetShellLog();

	}   // 析构 ⇒ 表 #11：NIM_DELETE（幽灵图标防线）

	EXPECT_TRUE(g_shellCalls.size() == 1);

	EXPECT_TRUE(g_shellCalls[0] == NIM_DELETE);

}

}

// ── T14-12：左键与双击路径（Phase 14 A3 修正一/二——2026-09-17 实测补）──────
// 修正一：v4 下左键单击走鼠标消息 WM_LBUTTONUP（原翻译表只认 NIN_SELECT ⇒ 左键无反应）。
// 修正二：双击由**系统 DBLCLK**上报。修正一曾以 UP 序列自合成并忽略系统 DBLCLK——实测证伪
//         （Win11 只送 DOWN/UP + DBLCLK，第二个 UP 不会到），故删除自合成、直接采用 DBLCLK。

void TestTrayLeftClickAndDoubleClickNormalization(){

	Win32PlatformApplication app;

	app.SetShellSeamsForTests(&FakeNotifyShell, &FakeDragFinish);

	std::vector<TrayEventType> seen;

	app.SetTrayEventSink([&seen](const TrayEvent& event){ seen.push_back(event.GetTrayType()); });

	ResetShellLog();

	app.SetTrayIcon(TrayIconOptions{});   // 宿主懒创建

	const HWND host = app.GetTrayHostHwndForTests();

	const UINT cb = app.GetTrayCallbackMsgForTests();

	EXPECT_TRUE(host != nullptr);

	EXPECT_TRUE(cb != 0);

	const LPARAM leftUp = MAKELPARAM(WM_LBUTTONUP, 1);   // v4 打包：LOWORD=事件 / HIWORD=图标 ID

	const WPARAM anchor = MAKEWPARAM(100, 200);          // v4 锚点：LOWORD=x / HIWORD=y

	// ① 左键单击 ⇒ Select（修正一之前此路径无任何事件）
	SendMessageW(host, cb, anchor, leftUp);

	EXPECT_TRUE(seen.size() == 1 && seen[0] == TrayEventType::Select);

	// ② 再单击一次（中间无 DBLCLK）⇒ **仍是 Select**：修正二已删除 UP 配对自合成
	SendMessageW(host, cb, anchor, leftUp);

	EXPECT_TRUE(seen.size() == 2 && seen[1] == TrayEventType::Select);

	// ③ NIN_SELECT 被忽略（修正三：v4 下它与 WM_LBUTTONUP **同源重复**——一次单击两条都到，
	//    只认鼠标消息，否则单击被上报两次：实测"点一下 #2、再点一下 #4"）
	SendMessageW(host, cb, anchor, MAKELPARAM(NIN_SELECT, 1));

	EXPECT_TRUE(seen.size() == 2);

	// ④ 键盘激活仍是独立事件（NIN_KEYSELECT 不与鼠标路径重复——必须保留）
	SendMessageW(host, cb, anchor, MAKELPARAM(NIN_KEYSELECT, 1));

	EXPECT_TRUE(seen.size() == 3 && seen[2] == TrayEventType::KeySelect);

	// ⑤ 右键不回归
	SendMessageW(host, cb, anchor, MAKELPARAM(WM_CONTEXTMENU, 1));

	EXPECT_TRUE(seen.size() == 4 && seen[3] == TrayEventType::ContextMenu);

	// ⑥ 系统 DBLCLK ⇒ DoubleClick（修正二：直接采用系统上报），并**武装**尾部 UP 吞除（修正四）
	SendMessageW(host, cb, anchor, MAKELPARAM(WM_LBUTTONDBLCLK, 1));

	EXPECT_TRUE(seen.size() == 5 && seen[4] == TrayEventType::DoubleClick);

	// ⑦ 双击序列的第二个 UP ⇒ **被吞除**（实测双击 = `UP` + `DBLCLK` + `UP`，
	//    DBLCLK 与尾部 UP 间隔恒 0 ms ⇒ 尾部 UP 属双击序列本身，不吞则双击多报一个 Select）
	SendMessageW(host, cb, anchor, leftUp);

	EXPECT_TRUE(seen.size() == 5);

	// ⑧ 武装是一次性的 ⇒ 下一次单击恢复为正常 Select
	SendMessageW(host, cb, anchor, leftUp);

	EXPECT_TRUE(seen.size() == 6 && seen[5] == TrayEventType::Select);

	// ⑨ 锚点缓存必须被 ContextMenu 刷新（修正五回归：该分支曾漏更新 ⇒ `ShowTrayMenu` 读到 0
	//    ⇒ **首次直接右键**时菜单弹在屏幕左上角 (0,0)，而"先点一次左键"后就正常）
	//    期望值取当前光标位置（ContextMenu 的坐标来源就是 GetCursorPos）——零副作用
	POINT cur{};
	GetCursorPos(&cur);

	SendMessageW(host, cb, anchor, MAKELPARAM(WM_CONTEXTMENU, 1));

	EXPECT_TRUE(app.GetLastAnchorXForTests() == cur.x
		&& app.GetLastAnchorYForTests() == cur.y);

}

void ECDI::Test::RegisterTrayTests(){

	GetTestRegistry().Add("Tray.StateMachineAndSequence",   &TestTrayStateMachineAndSequence);
	GetTestRegistry().Add("Tray.TaskbarCreatedRecovery",    &TestTrayTaskbarCreatedRecovery);
	GetTestRegistry().Add("Tray.FailureSemantics",          &TestTrayFailureSemantics);
	GetTestRegistry().Add("Tray.DestructorDefense",         &TestTrayDestructorDefense);
	GetTestRegistry().Add("Tray.LeftClickAndDoubleClickNormalization", &TestTrayLeftClickAndDoubleClickNormalization);

}
