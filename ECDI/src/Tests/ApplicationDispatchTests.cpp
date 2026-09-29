#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 防御性 undef（规范 10）
#endif

#include "ECDI/Application/Application.h"
#include "Platform/Win32/Win32PlatformApplication.h"   // 唤醒注入缝（内部头——seam 不出实现层）

#include <atomic>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>

using namespace ECDI;   // ★ 本项目测试文件惯例（TrayTests.cpp:16 同款）

namespace {

// ══════════════════════════════════════════════════════════════════
// Phase 23：工作线程 → UI 线程投递 —— T23-1..T23-11
// ★ 本组**必须**用真实 `Application` + 真实消息泵：被测行为是「跨线程投递 + 泵内 drain」，
//   任何替身都到达不了 `Win32PlatformApplication::Run()` 的 dispatch 分支（C2 / C8）。
// ══════════════════════════════════════════════════════════════════

/// @brief 最小 Application（本组只测投递通路，不观测事件）
/// @details ★ 构造即记录**本线程**为 UI owner thread（= `Win32PlatformApplication` 的 `m_uiThreadId`）
///          ——终止装置用它把 `WM_QUIT` 投到**正确的**线程队列（见下方 `PostQuitTo`）。
struct TestApp : public Application {

	const DWORD uiThreadId = ::GetCurrentThreadId();

};

// ── 装置 ①：确定终止 ────────────────────────────────────────────────
// ★★ 为什么需要它：`Run()` 阻塞在 `GetMessageW`，若实现漏了唤醒，用例会**挂死整个套件**
//   （不是红字，是挂起）。补投一条 `WM_QUIT` 使「泵退出」**不依赖被测的唤醒是否成功**。
// ★★ 为什么目标线程**必须显式传入**（实施期修正 · 详设 v1.2 §7.1）：工作线程里调
//   `GetCurrentThreadId()` 得到的是**它自己**——而非 UI 线程；`PostThreadMessageW` 投给一个
//   没有消息队列的线程会**直接失败** ⇒ 终止消息根本没进 UI 队列 ⇒ `Run()` **永久阻塞**
//   （本组的 T23-2 / T23-3 / T23-8 / T23-9 都是在工作线程里补投）。故统一传 `app.uiThreadId`。
/// @param uiThreadId UI owner 线程 id（`TestApp::uiThreadId` —— 构造线程即 owner）
/// @details 与 `PostToUi` 的唤醒消息**同属 UI 线程队列且 FIFO** ⇒ 只要在**最后一次提交之后**
///          补投，泵就会先 drain 完所有工作、再取到 WM_QUIT 退出（顺序由队列保证，非时序假设）。
void PostQuitTo(DWORD uiThreadId){

	::PostThreadMessageW(uiThreadId, WM_QUIT, 0, 0);

}

// ── 装置 ②：残留消息清理（★ 防跨用例污染）───────────────────────────
// ★★ 为什么需要它：本组全部在**同一个测试主线程**上 `Run()` ⇒ **消息队列在用例之间共享**。
//   多投的 `WM_QUIT`（例如「预投终止 QUIT」+「`Exit()` 又投一次」）若未被消费，
//   **下一个用例的 `Run()` 会立即返回** ⇒ 后续用例假失败。
//   ⚠️ 单轮**必须有上限**——无上限的 `PM_REMOVE` 内层循环会因窗口持续重投 `WM_PAINT` 而
//   **永不退出**（`DesktopLayerTests.cpp:81-93` 的挂死教训，本 helper 沿用其 256 上限）。
void DrainStrayMessages(){

	MSG message{};

	int burst = 0;

	while (burst++ < 256 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)){

		// 不 Translate / 不 Dispatch：这里只做「清场」（到这一步测试对象已全部销毁）

	}

}

/// @brief 用例收尾（★ 每个用例最后一行调用——先清场，再让下个用例拿到干净队列）
struct TestScopeCleanup{

	~TestScopeCleanup(){ DrainStrayMessages(); }

};

// ── T23-1：`Run()` 之前由工作线程提交（C1 / C2 / A1 / A2）─────────────
void Test23SubmitBeforeRun(){

	TestApp app;

	TestScopeCleanup cleanup;

	std::atomic<int> executed{0};

	std::atomic<bool> accepted{false};

	// ★ 实施期修正 ⑤（详设 §7.1）：线程身份用 **Win32 线程 ID（DWORD）**——
	//   MinGW win32-threads 模型下 `std::thread::id` 不可靠：worker 退出后其 id 数据块被
	//   主线程**首次** `get_id()` 复用 ⇒ 两者相等（探针实证，见详设 §7.1 ⑤）。
	DWORD submitThread{};

	DWORD executeThread{};

	std::thread worker([&]{

		submitThread = ::GetCurrentThreadId();

		// ★ 工作线程内**不做断言**（`TestContext` 无跨线程保护）；只记录，主线程断言
		accepted.store(app.PostToUi([&]{

			executeThread = ::GetCurrentThreadId();

			executed.fetch_add(1, std::memory_order_relaxed);

		}));

	});

	worker.join();

	PostQuitTo(app.uiThreadId);

	EXPECT_EQ(app.Run(), 0);

	EXPECT_TRUE(accepted.load());                        // C1 / A2

	EXPECT_EQ(executed.load(), 1);

	EXPECT_NE(submitThread, executeThread);              // A1：提交线程 ≠ 执行线程

	EXPECT_EQ(executeThread, app.uiThreadId); // A1：执行线程 = UI 消息线程（DWORD）

}

// ── T23-2 ★★：`Run()` 运行期间由工作线程提交（C2 / A1）─────────────────
// ★★ 设计要点：「提交确实发生在 Run 运行期间」由**结构**保证——工作线程在**UI callback G
//   内部**派生（此刻 UI 线程必然在 `Run()` 的 drain 栈上）⇒ 无需 `Sleep`、无需自旋等待。
void Test23SubmitWhileRunning(){

	TestApp app;

	TestScopeCleanup cleanup;

	std::atomic<int> executed{0};

	std::atomic<bool> accepted{false};

	DWORD submitThread{};

	DWORD executeThread{};

	const DWORD uiThread = app.uiThreadId;

	const bool gateAccepted = app.PostToUi([&]{

		// G：在 drain 内执行。此处派生工作线程 ⇒ 其提交时刻**晚于** Run 进入泵
		std::thread worker([&]{

			submitThread = ::GetCurrentThreadId();

			accepted.store(app.PostToUi([&]{

				executeThread = ::GetCurrentThreadId();

				executed.fetch_add(1, std::memory_order_relaxed);

			}));

			// ★ 提交**完成之后**再补投终止 QUIT ⇒ 队列序 = [唤醒 C, WM_QUIT]
			PostQuitTo(app.uiThreadId);

		});

		worker.join();   // G 内 join：本线程被占用期间 worker 完成提交（不需要 UI 线程参与）

	});

	EXPECT_TRUE(gateAccepted);

	EXPECT_EQ(app.Run(), 0);

	EXPECT_TRUE(accepted.load());

	EXPECT_EQ(executed.load(), 1);

	EXPECT_NE(submitThread, uiThread);

	EXPECT_EQ(executeThread, uiThread);

}

// ── T23-3：单源 FIFO（C4 / A4）──────────────────────────────────────
void Test23SingleSourceFifo(){

	TestApp app;

	TestScopeCleanup cleanup;

	char order[3] = { 0, 0, 0 };

	std::atomic<bool> allAccepted{false};

	std::thread worker([&]{

		bool ok = app.PostToUi([&]{ order[0] = 'A'; });

		ok = app.PostToUi([&]{ order[1] = 'B'; }) && ok;

		ok = app.PostToUi([&]{ order[2] = 'C'; }) && ok;

		allAccepted.store(ok);

		PostQuitTo(app.uiThreadId);

	});

	worker.join();

	EXPECT_EQ(app.Run(), 0);

	EXPECT_TRUE(allAccepted.load());

	EXPECT_TRUE(order[0] == 'A');

	EXPECT_TRUE(order[1] == 'B');

	EXPECT_TRUE(order[2] == 'C');   // ★ 只断言**单源**顺序；不对多线程交错作任何断言（C4 口径）

}

// ── T23-4 ★★：callback 内提交**不重入**（C3 / R9 / A9）─────────────────
void Test23NoReentrancyFromCallback(){

	TestApp app;

	TestScopeCleanup cleanup;

	std::atomic<bool> aFinished{false};

	std::atomic<bool> bPosted{false};

	std::atomic<bool> bSawAFinished{false};

	std::atomic<int> bExecuted{0};

	const bool aAccepted = app.PostToUi([&]{

		bPosted.store(app.PostToUi([&]{

			// ★ 判据：B 执行时 A 是否已执行完**末尾语句**。
			//   若实现递归消费（bug），B 会在 `PostToUi(B)` 返回前跑 ⇒ aFinished == false ⇒ 响亮失败。
			bSawAFinished.store(aFinished.load());

			bExecuted.fetch_add(1, std::memory_order_relaxed);

		}));

		PostQuitTo(app.uiThreadId);   // 队列序 = [唤醒 B, WM_QUIT]（B 的唤醒先入队）

		aFinished.store(true);

	});

	EXPECT_TRUE(aAccepted);

	EXPECT_EQ(app.Run(), 0);

	EXPECT_TRUE(bPosted.load());

	EXPECT_EQ(bExecuted.load(), 1);      // B 最终确实执行了（正对照）

	EXPECT_TRUE(bSawAFinished.load());   // ★ 但只能在 A 返回之后

}

// ── T23-5 ★★：UI 线程提交同样是异步（D7 / R10 / A10）───────────────────
void Test23UiThreadSubmitIsAlsoAsync(){

	TestApp app;

	TestScopeCleanup cleanup;

	std::atomic<int> executed{0};

	const bool accepted = app.PostToUi([&]{ executed.fetch_add(1, std::memory_order_relaxed); });

	// ★★ 核心判据：提交调用返回时，callback **尚未执行**（不存在 UI 线程 fast path）
	EXPECT_TRUE(accepted);

	EXPECT_EQ(executed.load(), 0);

	PostQuitTo(app.uiThreadId);

	EXPECT_EQ(app.Run(), 0);

	EXPECT_EQ(executed.load(), 1);   // 只在后续消息泵阶段执行

}

// ── T23-6 ★：队列不因窗口销毁而停摆（C5 / A5 · 生命周期解耦）────────────
// ★ 修正（初设 v1.2 P-④）：原期望句「测试只验证队列不保存隐式窗口指针」**不可执行**
//   （无法从外部观测"队列里有没有指针"）⇒ 本用例改验**可观测的生命周期解耦**：
//   ① 窗口销毁后队列仍工作；② 值语义载荷在 UI 线程读到仍有效。
//   「队列元素类型不含 Window*/Application*」改由**结构审查**承担（详设 §4.3 机检 5 / 10）。
void Test23QueueSurvivesWindowDestroyed(){

	TestApp app;

	TestScopeCleanup cleanup;

	// ⚠️ **关键前提**：关闭"最后窗口关闭即退出"。否则 `Release()` ⇒ OnWindowDestroyed ⇒
	//    `Exit()` ⇒ `CloseUiDispatch()` 会（正确地）清空队列，本用例的载荷就被丢弃了
	//    ——那是 C6 的正常语义，不是缺陷；本用例要验的是"**关闭之前**队列不依赖窗口"。
	app.SetQuitOnLastWindowClosed(false);

	Window& window = app.Create("ECDI_DispatchLifecycle", 320, 240);

	window.Release();   // 只销毁 HWND（UI 线程）；Window 对象进入延迟销毁列表
	// ⚠️ 此后**不得再触碰 window 引用**——`ProcessDeferredDestroy()` 会在泵内释放该对象

	std::atomic<bool> payloadValid{false};

	{

		std::string payload = "decode-done:0x2A";   // ★ 局部：提交后立即出作用域

		const bool accepted = app.PostToUi([&payloadValid, payload]{

			// payload 是**副本**（值语义）⇒ 原对象已亡，队列里的副本必须仍然有效
			payloadValid.store(payload == "decode-done:0x2A");

		});

		EXPECT_TRUE(accepted);

	}

	PostQuitTo(app.uiThreadId);

	EXPECT_EQ(app.Run(), 0);

	EXPECT_TRUE(payloadValid.load());

}

// ── T23-7 ★★：`Exit()` 关闭后丢弃同批未执行工作（C6 / D6① / A5 / A10）───
void Test23ExitDropsPendingWork(){

	TestApp app;

	TestScopeCleanup cleanup;

	std::atomic<int> aExecuted{0};

	std::atomic<int> bExecuted{0};

	const bool aAccepted = app.PostToUi([&]{

		aExecuted.fetch_add(1, std::memory_order_relaxed);

		app.Exit();   // ★ 关闭 dispatch（CloseUiDispatch）+ PostQuitMessage（终止由它自带）

	});

	const bool bAccepted = app.PostToUi([&]{ bExecuted.fetch_add(1, std::memory_order_relaxed); });

	EXPECT_TRUE(aAccepted);

	EXPECT_TRUE(bAccepted);              // ★ 提交时二者都被接受（true 只承诺"已入队"）

	EXPECT_EQ(app.Run(), 0);

	EXPECT_EQ(aExecuted.load(), 1);      // 正在执行的不被强行中断

	EXPECT_EQ(bExecuted.load(), 0);      // ★ 同批剩余项被关闭态丢弃（逐项检查的判据）

	EXPECT_FALSE(app.PostToUi([]{ }));   // 关闭后拒绝新工作

}

// ── T23-8 ★★：callback 异常隔离（C7 / 详设 O4）──────────────────────
void Test23CallbackExceptionIsolated(){

	TestApp app;

	TestScopeCleanup cleanup;

	std::atomic<int> aExecuted{0};

	std::atomic<int> bExecuted{0};

	std::atomic<bool> allAccepted{false};

	std::thread worker([&]{

		bool ok = app.PostToUi([&]{

			aExecuted.fetch_add(1, std::memory_order_relaxed);

			throw std::runtime_error("intentional test exception");

		});

		ok = app.PostToUi([&]{ bExecuted.fetch_add(1, std::memory_order_relaxed); }) && ok;

		allAccepted.store(ok);

		PostQuitTo(app.uiThreadId);

	});

	worker.join();

	// ★ 若异常穿出 drain ⇒ 穿出 Run() ⇒ 穿出本用例 ⇒ Runner 记 "unhandled exception" FAIL。
	//   故本行返回 0 即证明**异常已被隔离在 dispatch 边界内**（C7）。
	EXPECT_EQ(app.Run(), 0);

	EXPECT_TRUE(allAccepted.load());

	EXPECT_EQ(aExecuted.load(), 1);

	EXPECT_EQ(bExecuted.load(), 1);   // ★ 单个 callback 失败不阻断后续（C7）

	// ★ O4 定案：**不断言日志**（`Logger` 无可注入 sink）；
	//   日志存在性 = 仅代码审查 + 运行期人工可见（VS 输出窗口 / DebugView）。

}

// ── T23-9 ★：真实消费者形态载荷（A6）────────────────────────────────
// ★ 载荷类型定义在**测试 TU 内部** ⇒ 结构上不可能进公共头（详设 §4.3 机检 5 同步核）
struct DecodeDonePayload{

	int         resultCode;

	int         width;

	int         height;

	std::string sourcePath;

};

void Test23ConsumerShapedPayload(){

	TestApp app;

	TestScopeCleanup cleanup;

	std::atomic<bool> matched{false};

	std::atomic<bool> accepted{false};

	std::thread worker([&]{

		const DecodeDonePayload payload{ 0, 64, 64, "C:/icons/sample.ico" };   // 模拟"解码完成"

		accepted.store(app.PostToUi([&matched, payload]{   // 值语义：载荷随 std::function 移动入队

			matched.store(payload.resultCode == 0 && payload.width == 64
				&& payload.height == 64 && payload.sourcePath == "C:/icons/sample.ico");

		}));

		PostQuitTo(app.uiThreadId);

	});

	worker.join();

	EXPECT_EQ(app.Run(), 0);

	EXPECT_TRUE(accepted.load());

	EXPECT_TRUE(matched.load());

}

// ── T23-10 ★★：唤醒失败就地回滚 + 拒绝路径（C9 / C1 / S3–S5）────────────
// ★ 本用例直接构造 **Win32PlatformApplication**（不经 `Application`、**不泵消息**）
//   ——先例 `TrayTests.cpp:52`；因为被测行为发生在「入队与唤醒」这一步，与消息泵无关。
// ★ 正对照（条 81）：**真实唤醒**路径返回 true 由 T23-1 承担（那里用的是真 API）。
int  g_postThreadMessageCalls  = 0;

BOOL g_postThreadMessageResult = TRUE;

BOOL FakePostThreadMessage(DWORD, UINT, WPARAM, LPARAM){

	++g_postThreadMessageCalls;

	return g_postThreadMessageResult ? TRUE : FALSE;

}

void Test23WakeFailureRollsBack(){

	Win32PlatformApplication app;   // ★ 构造即建立本线程消息队列（详设 V2）

	app.SetPostThreadMessageSeamForTests(&FakePostThreadMessage);

	int executed = 0;

	// ① 空工作 ⇒ false（S3）：★ 不进临界区、**不触发唤醒**
	g_postThreadMessageCalls = 0;

	g_postThreadMessageResult = TRUE;

	EXPECT_FALSE(app.PostToUi(std::function<void()>{}));

	EXPECT_EQ(g_postThreadMessageCalls, 0);

	// ② 唤醒失败 ⇒ false + **就地回滚**（S5）：队列被清空 + 进入关闭态
	//    ★ 正对照：先证明缝真的被调用过（否则"回滚"可能只是"压根没走到唤醒"）
	g_postThreadMessageResult = FALSE;

	EXPECT_FALSE(app.PostToUi([&executed]{ ++executed; }));

	EXPECT_EQ(g_postThreadMessageCalls, 1);

	EXPECT_EQ(executed, 0);          // 被回滚的工作**没有执行**

	// ③ 失败即关闭 ⇒ 后续提交一律 false（S4），且**不再触发唤醒**
	g_postThreadMessageCalls = 0;

	EXPECT_FALSE(app.PostToUi([&executed]{ ++executed; }));

	EXPECT_EQ(g_postThreadMessageCalls, 0);

	EXPECT_EQ(executed, 0);

}

// ── T23-11 ★★：`Run()` 的 owner-thread 前置条件（C10 / O1 / A5）──────────
void Test23RunOnOwnerThreadOnly(){

	TestApp app;   // owner = 本测试线程

	TestScopeCleanup cleanup;

	const DWORD owner = ::GetCurrentThreadId();

	// ① 非 owner 线程调用：被拒绝、**立即返回**（不进入跨线程消息泵——那会静默失效）
	int foreignResult = -1;

	std::thread foreign([&]{ foreignResult = app.Run(); });

	foreign.join();

	EXPECT_EQ(foreignResult, 0);   // ★ 沿用 0：框架无 Run 错误码约定（O1 / L4）

	// ② 正对照：owner 线程上 `Run()` **正常工作**（判据不是"永远拒绝"）
	std::atomic<int> executed{0};

	EXPECT_TRUE(app.PostToUi([&]{ executed.fetch_add(1, std::memory_order_relaxed); }));

	PostQuitTo(app.uiThreadId);

	EXPECT_EQ(app.Run(), 0);

	EXPECT_EQ(executed.load(), 1);

	EXPECT_EQ(owner, app.uiThreadId);   // 本用例确实在 owner 线程上运行

}

}   // anonymous namespace

void ECDI::Test::RegisterApplicationDispatchTests(){

	GetTestRegistry().Add("ApplicationDispatch.SubmitBeforeRun",         &Test23SubmitBeforeRun);

	GetTestRegistry().Add("ApplicationDispatch.SubmitWhileRunning",      &Test23SubmitWhileRunning);

	GetTestRegistry().Add("ApplicationDispatch.SingleSourceFifo",        &Test23SingleSourceFifo);

	GetTestRegistry().Add("ApplicationDispatch.NoReentrancyFromCallback",&Test23NoReentrancyFromCallback);

	GetTestRegistry().Add("ApplicationDispatch.UiThreadSubmitIsAsync",   &Test23UiThreadSubmitIsAlsoAsync);

	GetTestRegistry().Add("ApplicationDispatch.QueueSurvivesWindowGone", &Test23QueueSurvivesWindowDestroyed);

	GetTestRegistry().Add("ApplicationDispatch.ExitDropsPendingWork",    &Test23ExitDropsPendingWork);

	GetTestRegistry().Add("ApplicationDispatch.ExceptionIsolated",       &Test23CallbackExceptionIsolated);

	GetTestRegistry().Add("ApplicationDispatch.ConsumerShapedPayload",   &Test23ConsumerShapedPayload);

	GetTestRegistry().Add("ApplicationDispatch.WakeFailureRollsBack",    &Test23WakeFailureRollsBack);

	GetTestRegistry().Add("ApplicationDispatch.RunOwnerThreadOnly",      &Test23RunOnOwnerThreadOnly);

}
