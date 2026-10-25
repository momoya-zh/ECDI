#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 防御性 undef（规范 10）
#endif

#include "Platform/Win32/Win32PlatformWindow.h"   // GetHwndForTests / SetMouseCapture 接线面
#include "Platform/Win32/Win32RenderContext.h"    // GLRenderer Initialize 的类型安全上下文
#include "Render/FontEngine.h"                    // GLRenderer 构造（共享文本引擎）
#include "Render/GLRenderer.h"
#include "ECDI/Platform/PlatformWindowHost.h"
#include "ECDI/Widget/Widget.h"
#include "ECDI/Widget/TextBox.h"
#include "ECDI/Window/Window.h"
#include "ECDI/Application/Application.h"
#include "ECDI/Core/Color.h"
#include "ECDI/Core/CursorShape.h"

#include <memory>

using namespace ECDI;

// ══════════════════════════════════════════════════════════════════════
// #72 平台面快修小轮：① GL 失败态更新区域验证 ② 光标能力 ③ WM_MOUSELEAVE
// ══════════════════════════════════════════════════════════════════════
// ★ 缺陷与修法（登记 deferred #72，2026-10-24 盲审对码 + 探针实证）：
//   ① GLRenderer::BeginFrame 在 !m_ready 时早退 ⇒ 跳过 BeginPaint 的更新区域验证
//      ⇒ WM_PAINT 队列一空即重生成（探针 4s 内 595571 帧）⇒ WM_TIMER 永不投递
//      ⇒ 一切定时器饿死（动画 0 tick）。修 = 失败路径补空白帧 BeginPaint/EndPaint 对。
//   ② 全库无 WM_SETCURSOR / ::SetCursor（窗口类恒 IDC_ARROW ⇒ TextBox 上也是箭头）。
//      修 = WM_SETCURSOR(HTCLIENT) → Host::OnQueryCursor → HitTest → Widget::GetCursor。
//   ③ 无 TrackMouseEvent / WM_MOUSELEAVE ⇒ 鼠标出窗后最后悬停控件永收不到 OnMouseLeave。
//      修 = 每 move 幂等武装 TME_LEAVE；WM_MOUSELEAVE → Host::OnMouseLeftWindow → 清 hover。
// ★ 装置沿 MouseCaptureTests 先例：栈上 Win32PlatformWindow + 最小 Host（不经 Application，
//   可直呼平台层契约）；① 用独立 scratch HWND（见用例内说明）。
//   ⚠️ 测试卫生：② 的 ::SetCursor 是进程级可见状态——用例尾恢复箭头。

namespace{

/// @brief 记录式 Host（② 的观测面 = OnQueryCursor；③ 的观测面 = OnMouseLeftWindow）
struct QuickfixHost final : public PlatformWindowHost{

	mutable int queryCount = 0;   ///< OnQueryCursor 调用次数（const 契约 ⇒ mutable）
	int leaveCount = 0;           ///< OnMouseLeftWindow 调用次数

	void OnPaint() override{}

	void OnResized(int, int) override{}

	void OnExitSizeMove() override{}

	Window* GetWindow() const noexcept override{ return nullptr; }   // 替身无真 Window

	void OnEvent(const Event&) override{}   // 翻译器派发终点（Event 携 null Window——安全）

	void OnIMEComposition() override{}

	void OnIMECompositionUpdate(const std::string&) override{}

	void OnIMECompositionCommit(const std::string&) override{}

	bool IsClientInteractiveAt(int, int) const noexcept override{ return false; }

	CursorShape OnQueryCursor(int, int) const noexcept override{

		++queryCount;

		return CursorShape::Arrow;

	}

	void OnMouseLeftWindow() override{ ++leaveCount; }

};

/// @brief scratch 窗口（① 专用——独立于框架窗口类；注册一次，进程内复用）
HWND CreateScratchWindow(const wchar_t* title)
{
	static const wchar_t* kClassName = L"ECDI_P72_Scratch";

	static bool registered = false;

	if (!registered)
	{
		WNDCLASSEXW wc{};

		wc.cbSize        = sizeof(wc);

		wc.lpfnWndProc   = DefWindowProcW;

		wc.hInstance     = GetModuleHandleW(nullptr);

		wc.lpszClassName = kClassName;

		wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);

		wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));

		RegisterClassExW(&wc);

		registered = true;
	}

	return CreateWindowExW(0, kClassName, title, WS_OVERLAPPEDWINDOW,

		CW_USEDEFAULT, CW_USEDEFAULT, 200, 200,

		nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
}

/// @brief hover 计数探针控件（⑤ 的观测面——#59 用例同款指纹法）
class HoverProbe final : public Widget{
public:
	int enter = 0;
	int leave = 0;
protected:
	void OnMouseEnter() override{ ++enter; }
	void OnMouseLeave() override{ ++leave; }
};

}

// ── ① GL 初始化失败 ⇒ 失败路径 BeginFrame 仍验证更新区域（风暴灭）──────────────────
// ★ 装置：预先给 scratch 窗口设「非 GL、无双缓冲」像素格式——窗口像素格式**只能设一次**
//   ⇒ GLRenderer 的 SetPixelFormat(OpenGL|双缓冲) 必失败 ⇒ m_ready=false 且 m_hwnd 有效
//   （Initialize 早退于像素格式段——与探针的 m_hwnd 赋值后 return 同构）。
// ★ 判别：InvalidateRect → BeginFrame/EndFrame → PeekMessage(PM_NOREMOVE, WM_PAINT) 必须为空。
//   修复前：早退不验证 ⇒ WM_PAINT 立即可见（红）；修复后：验证掉 ⇒ 队列空（绿）。
void Test72GLFailureStillValidatesUpdateRegion()
{
	const HWND hwnd = CreateScratchWindow(L"ECDI_P72_GLPF");

	EXPECT_TRUE(hwnd != nullptr);

	// ★ 必须 ShowWindow：隐藏窗口不投递 WM_PAINT（系统不为不可见窗口生成绘制消息）
	//   ⇒ 不显示则 PeekMessage 恒空、两态无判别（判别力验证轮抓出的首版假绿，如实记录）。
	ShowWindow(hwnd, SW_SHOWNORMAL);

	{
		const HDC dc = GetDC(hwnd);

		PIXELFORMATDESCRIPTOR plain{};

		plain.nSize      = sizeof(plain);

		plain.nVersion   = 1;

		plain.dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_GDI;   // ★ 刻意不含 OPENGL/DOUBLEBUFFER

		plain.iPixelType = PFD_TYPE_RGBA;

		plain.cColorBits = 24;

		plain.iLayerType = PFD_MAIN_PLANE;

		const int plainFormat = ChoosePixelFormat(dc, &plain);

		EXPECT_TRUE(plainFormat != 0);

		EXPECT_TRUE(SetPixelFormat(dc, plainFormat, &plain) != 0);

		ReleaseDC(hwnd, dc);
	}

	const auto engine = std::make_shared<FontEngine>();

	GLRenderer renderer(engine);

	renderer.Initialize(Win32RenderContext(hwnd));

	EXPECT_TRUE(!renderer.IsReady());   // ★ 前置闸门：强制失败必须生效（否则后续断言无意义）

	EXPECT_TRUE(InvalidateRect(hwnd, nullptr, FALSE) != 0);

	renderer.BeginFrame(Color::White());

	renderer.EndFrame();

	MSG msg{};

	const BOOL hasPaint = PeekMessageW(&msg, hwnd, WM_PAINT, WM_PAINT, PM_NOREMOVE | PM_NOYIELD);

	EXPECT_TRUE(hasPaint == FALSE);   // 修复前：不验证 ⇒ WM_PAINT 在队（红）

	// 清理：验证掉可能残留的失效区 + 销毁
	ValidateRect(hwnd, nullptr);

	DestroyWindow(hwnd);
}

// ── ②-a：光标自述缺省——Widget=Arrow / TextBox=IBeam / Host 默认=Arrow ──────────────
void Test72CursorDefaults()
{
	Widget plain;

	EXPECT_TRUE(plain.GetCursor() == CursorShape::Arrow);

	TextBox textBox("x");

	EXPECT_TRUE(textBox.GetCursor() == CursorShape::IBeam);

	// Host 默认实现（不 override 即语义正确——S7 先例的可测面）
	QuickfixHost host;

	EXPECT_TRUE(host.OnQueryCursor(0, 0) == CursorShape::Arrow);
}

// ── ②-b：WM_SETCURSOR 客户区 → 问宿主 + 返回 TRUE；非客户区 → 不问（系统光标）──────
void Test72SetCursorClientQueriesHost()
{
	QuickfixHost host;

	Win32PlatformWindow window(host, "ECDI_P72_Cursor", 200, 200);

	const HWND hwnd = window.GetHwndForTests();

	EXPECT_TRUE(hwnd != nullptr);

	const LRESULT client = SendMessageW(hwnd, WM_SETCURSOR, reinterpret_cast<WPARAM>(hwnd),

		MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));

	EXPECT_EQ(client, static_cast<LRESULT>(TRUE));   // 已设光标 → 拦截类光标重设

	EXPECT_EQ(host.queryCount, 1);                   // 修复前：无 WM_SETCURSOR 处理 ⇒ 恒 0（红）

	host.queryCount = 0;

	const LRESULT caption = SendMessageW(hwnd, WM_SETCURSOR, reinterpret_cast<WPARAM>(hwnd),

		MAKELPARAM(HTCAPTION, WM_MOUSEMOVE));

	EXPECT_TRUE(host.queryCount == 0);               // 非客户区不问（边框/标题 = 系统光标）

	EXPECT_TRUE(caption != TRUE);                    // DefWindowProc 语义（值依系统态，不钉具体值）

	// 测试卫生：::SetCursor 是进程级可见状态 → 恢复箭头
	::SetCursor(::LoadCursorW(nullptr, IDC_ARROW));
}

// ── ③-a：WM_MOUSELEAVE → 宿主上报（平台层单元）──────────────────────────────────
void Test72MouseLeaveNotifiesHost()
{
	QuickfixHost host;

	Win32PlatformWindow window(host, "ECDI_P72_Leave", 200, 200);

	const HWND hwnd = window.GetHwndForTests();

	EXPECT_TRUE(hwnd != nullptr);

	// 先来一条 move（真实路径：HandleMessage 开头的 TME_LEAVE 武装 + 翻译器派发——
	// 替身 Host 的 OnEvent 为空，Event 携 null Window 安全）
	SendMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(10, 10));

	EXPECT_EQ(host.leaveCount, 0);

	SendMessageW(hwnd, WM_MOUSELEAVE, 0, 0);

	EXPECT_EQ(host.leaveCount, 1);   // 修复前：无 WM_MOUSELEAVE 处理 ⇒ 恒 0（红）
}

// ── ③-b：全链 WM_MOUSELEAVE → hover 清理 + Leave 派发（真 Window 集成）──────────────
// ★ Window 的 Host 实现（OnMouseLeftWindow）在 private 区——平台层经 PlatformWindowHost&
//   调用（分层律）；本用例走**同一条链**：SendMessage(WM_MOUSELEAVE) → 平台层 → 契约 → 实现。
// ★ 判别指纹（#59 hover 用例同款）：清空后再推**同一指针**必须重新派发 Enter——
//   若 hover 未清，HoverTracker 的「同目标短路」会吞掉（enter 恒 1 ⇒ 两态可区分）。
void Test72WindowLeaveClearsHover()
{
	Application app;   // 纯栈对象，不 Run（SendMessage 同步直入 WndProc，无需消息循环）

	Window& window = app.Create("ECDI_P72_WinLeave", 200, 150);

	auto probe = std::make_unique<HoverProbe>();

	HoverProbe* probePtr = probe.get();

	probePtr->SetPosition(0, 0);

	probePtr->SetSize(200, 150);

	window.GetRootWidget().AddChild(std::move(probe));

	window.UpdateHoverState(probePtr);

	EXPECT_EQ(probePtr->enter, 1);

	auto& platformWindow = static_cast<Win32PlatformWindow&>(window.GetPlatformWindow());

	const HWND hwnd = platformWindow.GetHwndForTests();

	EXPECT_TRUE(hwnd != nullptr);

	SendMessageW(hwnd, WM_MOUSELEAVE, 0, 0);

	EXPECT_EQ(probePtr->leave, 1);   // 正常离开语义：派发 Leave（#59 的脱树失效才不补发）

	window.UpdateHoverState(probePtr);

	EXPECT_EQ(probePtr->enter, 2);   // ★ hover 已清的指纹（未清则短路 ⇒ 恒 1）
}

// ── 注册（★ 必须在 ECDI::Test 内——全局作用域会定义出 ::Register… ⇒ LNK2019）────────

namespace ECDI::Test{

void RegisterPlatformQuickfixTests()
{
	GetTestRegistry().Add("Platform72.GLFailureValidatesUpdateRegion", &Test72GLFailureStillValidatesUpdateRegion);   // ①
	GetTestRegistry().Add("Platform72.CursorDefaults",                 &Test72CursorDefaults);                          // ②-a
	GetTestRegistry().Add("Platform72.SetCursorClientQueriesHost",     &Test72SetCursorClientQueriesHost);              // ②-b
	GetTestRegistry().Add("Platform72.MouseLeaveNotifiesHost",         &Test72MouseLeaveNotifiesHost);                  // ③-a
	GetTestRegistry().Add("Platform72.WindowLeaveClearsHover",         &Test72WindowLeaveClearsHover);                  // ③-b
}

}   // namespace ECDI::Test
