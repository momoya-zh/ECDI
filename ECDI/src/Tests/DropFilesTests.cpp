#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#include <shellapi.h>   // Phase 14：HDROP / DragQueryFileW / DragFinish（Shell 拖放 API）
#include <shlobj.h>     // Phase 14：DROPFILES 结构——⚠️ 该结构**不在** shellapi.h：
                        //   MinGW 定义在 shlobj.h:1528、MSVC 在 ShlObj_core.h:1915（经 ShlObj.h 传递）；
                        //   <Windows.h> 只自带 shellapi.h ⇒ 少这一行 MinGW 报 C2065/'DROPFILES' 未声明
#ifdef DrawText
#undef DrawText   // 防御性 undef（规范 10）
#endif

#include "Platform/Win32/Win32PlatformWindow.h"
#include "ECDI/EventSystem/Event.h"
#include "ECDI/EventSystem/EventType.h"
#include "ECDI/EventSystem/Window/DropFilesEvent.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace ECDI;

// ── 记录型 Host（PlatformWindowHost 全 override——捕获 OnEvent 事件值拷贝）──

namespace{

class DropHost final : public PlatformWindowHost{
public:

	std::vector<EventType> received;

	std::vector<std::string> lastPaths;   ///< DropFilesEvent 的值拷贝（event 为栈上局部——不可存指针）

	int lastX = -1;

	int lastY = -1;

	void OnPaint() override{}

	void OnResized(int, int) override{}

	void OnExitSizeMove() override{}

	bool IsClientInteractiveAt(int, int) const noexcept override{ return false; }

	Window* GetWindow() const noexcept override{ return nullptr; }   // 测试替身无真 Window

	void OnEvent(const Event& event) override{

		received.push_back(event.GetType());

		if (event.GetType() == EventType::DropFiles){

			const auto& e = static_cast<const DropFilesEvent&>(event);

			lastPaths = e.GetPaths();

			lastX = e.GetX();

			lastY = e.GetY();

		}

	}

	void OnIMEComposition() override{}

	void OnIMECompositionUpdate(const std::string&) override{}

	void OnIMECompositionCommit(const std::string&) override{}

};

// ── HDROP 测试构造（DROPFILES + 宽字符路径 + 双 \0 终止——真实内存布局）──────

HDROP MakeTestDrop(const wchar_t* path, int x, int y){

	const size_t pathBytes = (wcslen(path) + 2) * sizeof(wchar_t);   // 含双 \0 终止

	const size_t total = sizeof(DROPFILES) + pathBytes;

	HGLOBAL h = GlobalAlloc(GHND, total);

	if (h == nullptr){

		return nullptr;

	}

	auto* df = static_cast<DROPFILES*>(GlobalLock(h));

	if (df == nullptr){

		GlobalFree(h);

		return nullptr;

	}

	df->pFiles = sizeof(DROPFILES);

	df->pt.x = x;

	df->pt.y = y;

	df->fNC = FALSE;

	df->fWide = TRUE;

	wcscpy_s(reinterpret_cast<wchar_t*>(df + 1), wcslen(path) + 1, path);

	GlobalUnlock(h);

	return static_cast<HDROP>(h);

}

int g_finishCount = 0;

void CountingDragFinish(HDROP){

	++g_finishCount;

}

// ── T14-6/T14-7：事件内容 + HDROP 生命周期（R10——DragFinish 恰好一次）──────

void TestDropEventContentAndFinish(){

	DropHost host;

	Win32PlatformWindow window(host, "DropTest", 200, 200);

	g_finishCount = 0;

	window.SetDragFinishForTests(&CountingDragFinish);

	HDROP hDrop = MakeTestDrop(L"C:\\drop_test.txt", 42, 24);

	EXPECT_TRUE(hDrop != nullptr);

	if (hDrop == nullptr){

		return;

	}

	// 直接投递消息（绕过 shell 投递——事件内容与生命周期为本测试目标）
	SendMessageW(window.GetHwndForTests(), WM_DROPFILES, reinterpret_cast<WPARAM>(hDrop), 0);

	// T14-6：事件内容（UTF-8 路径 + 客户区落点）
	EXPECT_TRUE(host.received.size() == 1);

	EXPECT_TRUE(host.received[0] == EventType::DropFiles);

	EXPECT_TRUE(host.lastPaths.size() == 1);

	EXPECT_TRUE(host.lastPaths[0] == "C:\\drop_test.txt");

	EXPECT_TRUE(host.lastX == 42);

	EXPECT_TRUE(host.lastY == 24);

	// T14-7：DragFinish 恰好一次、且发生在事件抛出前（R10——经测试缝计数）
	EXPECT_TRUE(g_finishCount == 1);

	// 替身未真释放 —— 测试自清（防句柄泄漏）
	GlobalFree(hDrop);

}

// ── T14-8：门控行为（Show 前忽略不崩溃；Show 后可用且幂等）────────────────

void TestFileDropGating(){

	DropHost host;

	Win32PlatformWindow window(host, "DropGate", 200, 200);

	// Show 前 ⇒ Warning + 忽略（不崩溃、不产生事件）
	window.SetFileDropEnabled(true);

	window.Show();   // m_shown = true

	// 运行期 ⇒ 可用且幂等（真实 DragAcceptFiles 效果属系统行为——手测 A4 覆盖）
	window.SetFileDropEnabled(true);

	window.SetFileDropEnabled(false);

	EXPECT_TRUE(IsWindow(window.GetHwndForTests()));

}

// ── T14-10：Hide() 契约（隐藏 ≠ 销毁；不产生 WindowDestroyedEvent）────────

void TestHideContract(){

	DropHost host;

	Win32PlatformWindow window(host, "HideTest", 200, 200);

	window.Show();

	const HWND hwnd = window.GetHwndForTests();

	EXPECT_TRUE(IsWindow(hwnd));

	window.Hide();

	// 隐藏 ⇒ 不可见，但 HWND 存活（≠ Release）
	EXPECT_TRUE(IsWindow(hwnd));

	EXPECT_FALSE(IsWindowVisible(hwnd));

	// 隐藏不产生 WindowDestroyedEvent（m_windows 语义的根基——详设 §6.6）
	EXPECT_TRUE(std::find(host.received.begin(), host.received.end(),
		EventType::WindowDestroyed) == host.received.end());

	// 可再次显示
	window.Show();

	EXPECT_TRUE(IsWindowVisible(hwnd));

}

}

void ECDI::Test::RegisterDropFilesTests(){

	GetTestRegistry().Add("DropFiles.EventContentAndFinish",   &TestDropEventContentAndFinish);
	GetTestRegistry().Add("DropFiles.Gating",                  &TestFileDropGating);
	GetTestRegistry().Add("DropFiles.HideContract",            &TestHideContract);

}
