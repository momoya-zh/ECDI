#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 防御性 undef（规范 10）：本文件含 Windows.h——防 DrawTextW 宏污染 ECDI 头声明
#endif

#include "Platform/Win32/Win32RenderContext.h"          // TestWindow::Handle() 取 HWND（经 GetRenderContext 三跳）
#include "ECDI/Platform/PlatformWindow.h"              // Handle() 需 PlatformWindow 完整类型（Window.h 仅前置声明）
#include "ECDI/Application/Application.h"
#include "ECDI/EventSystem/EventRouter.h"
#include "ECDI/EventSystem/Window/WindowStateChangedEvent.h"
#include "ECDI/Window/ChromeMode.h"
#include "ECDI/Window/Window.h"
#include "ECDI/Window/WindowLayer.h"
#include "ECDI/Window/WindowState.h"

#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

using namespace ECDI;

// ── 编译期契约（window-ownership.md §6.1）：Window 不得在框架外构造 ──────────
// 依据：std::is_constructible 在**中立上下文**求值 ⇒ 构造器为 private 时结果为 false。
// 实证（详设 §3）：MinGW g++ 16.1 与 clang++（LLVM）均返回 false。
// ⚠️ 该 trait 在中立上下文求值 —— 写在 Application 内部同样返回 false，
//    因此它**只能做负向断言**；友元权限的正向证据 = Application::Create 里
//    `new Window(*this, …)` 能编译（天然成立）。
static_assert(
	!std::is_constructible_v<Window, Application&, std::string, int, int>,
	"Window 不得在框架外构造——请使用 Application::Create()");

// 补充：拷贝/移动已 deleted，一并有编译期回归
static_assert(!std::is_copy_constructible_v<Window>, "Window 禁止拷贝");
static_assert(!std::is_move_constructible_v<Window>, "Window 禁止移动");

namespace {

/// @brief 测试窗口守卫（D-TST-1：T0–T3/T5/T6 不 Show——创建即有效，零闪窗；
/// T4/T7/T7a 自行 Show 并负责泵消息）
/// @details 每用例独立构造（D-TST-1 修订：caption/inset 配置各异，共享窗口会造成
/// 用例顺序依赖）。
/// ⚠️ 生命周期约束：本对象必须**先于** Application 析构（析构序 = 声明逆序 →
/// 用例内先声明 Application、后声明 TestWindow）。
struct TestWindow {

    /// ⚠️ **非拥有**指针（non-owning reference）——本对象**只是观察者 / 触发器**，
    /// 不承担 Window 对象的所有权：所有权始终归 Application（`unique_ptr` 唯一持有）。
    /// ⇒ 绝不可改用 `unique_ptr<Window>` 持有（会造成双重所有权 → 二次析构）。
    /// 推荐消费者模型见 `Application::Create` 的返回值用法：`Window& w = app.Create(...)`。
    Window* window = nullptr;

    TestWindow(Application& a, ChromeMode mode, int caption, int inset) {

        // 窗口**必须**经 Application::Create() 创建：登记 `Application::m_windows` 只发生在此。
        // 直接构造 Window 会绕过登记 —— 销毁时
        // `~Window → Release() → DestroyWindow() →（同步 WM_DESTROY）→ WindowDestroyedEvent
        // → Application::OnWindowDestroyed` 在册中找不到该窗口 ⇒ `Application.cpp:92` 断言。
        // （本文件 v1.0 正是这样崩的；`FRAMEWORK_ASSERT` 只在 `_DEBUG` 下存在，故只在 MSVC 构建暴露。）
        // ⚠️ B1 起「直接构造」已**不可编译**——构造器 private + `friend class Application`；
        //    下文保留该因果链，作为这条约束的由来记录。
        window = &a.Create("ECDI_ChromeTest", 800, 600);

        window->SetChromeMode(mode);
        window->SetCaptionHeight(caption);
        window->SetResizeInset(inset);
        // 800×600 窗口基准；不调用 Show()（T4/T7/T7a 自行 Show + 泵消息）。
        // ⚠️ 绝不可用 `unique_ptr<Window>` 持有本窗口：Application 的延迟销毁表
        //    （m_deferredDestroy）会对同一对象二次析构 = double delete。
    }

    ~TestWindow() {

        // 只销毁 HWND（WM_DESTROY → Application 回收对象）；本对象不得后于 Application 析构
        window->Release();

    }

    /// @brief 取底层 HWND（全部窗口类用例的唯一几何/命中查询入口）
    /// @details Window 刻意不暴露句柄（7.1.1 边界：框架层零 Win32 类型），测试经
    /// 「抽象接口 → 平台上下文」两跳取回：
    ///   ① Window::GetPlatformWindow()        （返回 PlatformWindow&）
    ///   ② PlatformWindow::GetRenderContext() （返回 const PlatformRenderContext&）
    ///   ③ static_cast 到 Win32RenderContext 后 GetHandle()
    /// 本项目既有窗口测试全部裸 CreateWindowExW 自建句柄，无「由 Window 对象取 HWND」的先例。
    HWND Handle() const {

        return static_cast<const Win32RenderContext&>(
            window->GetPlatformWindow().GetRenderContext()).GetHandle();

    }
};

/// @brief 手动消息泵（T4/T7/T7a 用——处理至多 maxCount 条消息后返回）
void PumpMessages(int maxCount) {

    MSG msg{};

    for (int i = 0; i < maxCount && PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE); ++i) {

        TranslateMessage(&msg);
        DispatchMessageW(&msg);

    }
}

/// @brief 窗口局部坐标 → WM_NCHITTEST 查询（同步直达 WndProc——未显示窗口同样有效）
LRESULT HitTestLocal(HWND hwnd, int x, int y) {

    RECT wr{};
    GetWindowRect(hwnd, &wr);

    const POINT pt{ wr.left + x, wr.top + y };

    return SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(pt.x, pt.y));
}

/// @brief 客户区（局部坐标）→ 屏幕坐标（与 GetWindowRect 同坐标系后方可比较）
RECT ClientRectScreen(HWND hwnd) {

    RECT cr{};
    GetClientRect(hwnd, &cr);

    POINT tl{ cr.left, cr.top };
    POINT br{ cr.right, cr.bottom };

    ClientToScreen(hwnd, &tl);
    ClientToScreen(hwnd, &br);

    return RECT{ tl.x, tl.y, br.x, br.y };
}

/// @brief 捕获窗口状态事件的 Application（Application 继承 EventRouter，
/// 无法注入自定义 router——故子类化；OnWindowStateChanged 在 EventRouter 中为
/// protected 虚方法，override 必须留在 protected 区）
struct TestApp : public Application {

    std::vector<WindowState> seen;

protected:

    void OnWindowStateChanged(const WindowStateChangedEvent& e) override {

        seen.push_back(e.GetState());

    }
};

// ══════════════════════════════════════════════════════════════════
// T0：ChromeMode 一次确定（D-CHROME-1 回归锚）
// ══════════════════════════════════════════════════════════════════

void TestChromeModeDecidedOnce()
{
    Application app;
    TestWindow win(app, ChromeMode::Borderless, 32, 8);

    // 第二次（异值）与第三次（同值）都应被 Warning + 忽略
    win.window->SetChromeMode(ChromeMode::Normal);
    win.window->SetChromeMode(ChromeMode::Borderless);

    // 最终语义 = 第一次的 Borderless（客户区与窗口四边重合）
    const HWND hwnd = win.Handle();
    RECT wr{};
    GetWindowRect(hwnd, &wr);
    const RECT cs = ClientRectScreen(hwnd);

    EXPECT_EQ(cs.left,   wr.left);
    EXPECT_EQ(cs.top,    wr.top);
    EXPECT_EQ(cs.right,  wr.right);
    EXPECT_EQ(cs.bottom, wr.bottom);
}

// ══════════════════════════════════════════════════════════════════
// T1：Normal vs Borderless 客户区对照
// ══════════════════════════════════════════════════════════════════

void TestBorderlessClientEqualsWindow()
{
    Application app;
    TestWindow normal(app, ChromeMode::Normal, 0, 0);
    TestWindow borderless(app, ChromeMode::Borderless, 4, 4);

    // A（Normal）：客户区严格小于窗口（差 = 标题栏 + 边框）
    RECT wa{};
    GetWindowRect(normal.Handle(), &wa);
    RECT ca{};
    GetClientRect(normal.Handle(), &ca);

    EXPECT_TRUE((wa.right - wa.left) > (ca.right - ca.left));
    EXPECT_TRUE((wa.bottom - wa.top) > (ca.bottom - ca.top));

    // B（Borderless）：客户区与窗口矩形四边重合（坐标系转换后比较）
    RECT wb{};
    GetWindowRect(borderless.Handle(), &wb);
    const RECT cs = ClientRectScreen(borderless.Handle());

    EXPECT_EQ(cs.left,   wb.left);
    EXPECT_EQ(cs.top,    wb.top);
    EXPECT_EQ(cs.right,  wb.right);
    EXPECT_EQ(cs.bottom, wb.bottom);
}

// ══════════════════════════════════════════════════════════════════
// T2：NCHITTEST 九宫格 + resize 优先级覆盖 caption
// ══════════════════════════════════════════════════════════════════

void TestNCHitTestNineGrid()
{
    Application app;
    TestWindow win(app, ChromeMode::Borderless, 32, 8);

    const HWND hwnd = win.Handle();
    RECT wr{};
    GetWindowRect(hwnd, &wr);

    const int w = wr.right - wr.left;
    const int h = wr.bottom - wr.top;

    // 四角
    EXPECT_EQ(HitTestLocal(hwnd, 4, 4),           HTTOPLEFT);
    EXPECT_EQ(HitTestLocal(hwnd, w - 4, 4),       HTTOPRIGHT);
    EXPECT_EQ(HitTestLocal(hwnd, 4, h - 4),       HTBOTTOMLEFT);
    EXPECT_EQ(HitTestLocal(hwnd, w - 4, h - 4),   HTBOTTOMRIGHT);

    // 四边
    EXPECT_EQ(HitTestLocal(hwnd, 4, h / 2),       HTLEFT);
    EXPECT_EQ(HitTestLocal(hwnd, w - 4, h / 2),   HTRIGHT);
    EXPECT_EQ(HitTestLocal(hwnd, w / 2, h - 4),   HTBOTTOM);

    // ★ 测试目的：resize 优先级覆盖 caption——(w/2, 4) 同时落在 inset(4 < 8) 与
    //   caption(4 < 32) 两个区内，必须返回 HTTOP 而非 HTCAPTION
    EXPECT_EQ(HitTestLocal(hwnd, w / 2, 4),       HTTOP);

    // caption / client
    EXPECT_EQ(HitTestLocal(hwnd, w / 2, 20),      HTCAPTION);
    EXPECT_EQ(HitTestLocal(hwnd, w / 2, h / 2),   HTCLIENT);
}

// ══════════════════════════════════════════════════════════════════
// T3：NCHITTEST 模式隔离（双窗口对照——只断言差异，不绑定系统几何）
// ══════════════════════════════════════════════════════════════════

void TestNCHitTestModeIsolation()
{
    Application app;
    TestWindow normal(app, ChromeMode::Normal, 0, 0);
    TestWindow borderless(app, ChromeMode::Borderless, 4, 4);

    // 探测点动态生成：Normal 窗口顶部非客户区总高（不硬编码——主题/DPI 会变）
    const int sysNC = GetSystemMetrics(SM_CYCAPTION)
                    + GetSystemMetrics(SM_CYSIZEFRAME)
                    + GetSystemMetrics(SM_CXPADDEDBORDER);
    const int yProbe = sysNC - 2;   // 确定落在 Normal 的 caption 区

    RECT wn{};
    GetWindowRect(normal.Handle(), &wn);
    RECT wb{};
    GetWindowRect(borderless.Handle(), &wb);

    const LRESULT ra = HitTestLocal(normal.Handle(), (wn.right - wn.left) / 2, yProbe);
    const LRESULT rb = HitTestLocal(borderless.Handle(), (wb.right - wb.left) / 2, yProbe);

    // 断言只此一条：同一位置两模式语义不同（不校验具体 HT 值——
    // 测试目的是「Borderless 拦截生效」，而非「系统标题栏恰好多大」）
    EXPECT_NE(ra, rb);
}

// ══════════════════════════════════════════════════════════════════
// T4：最大化客户区 ⊆ rcWork（R4 不变量——D-COMP-1 无补偿验证）
// ══════════════════════════════════════════════════════════════════

void TestMaximizedClientWithinWorkArea()
{
    Application app;
    TestWindow win(app, ChromeMode::Borderless, 32, 8);

    win.window->Show();
    win.window->Maximize();
    PumpMessages(64);   // 断言前必须让 WM_NCCALCSIZE / WM_SIZE 处理完毕

    const HWND hwnd = win.Handle();
    const RECT cs = ClientRectScreen(hwnd);

    MONITORINFO mi{};
    mi.cbSize = sizeof(MONITORINFO);
    EXPECT_TRUE(GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi));

    // 不变量：ClientRectScreen ⊆ rcWork
    EXPECT_TRUE(cs.left   >= mi.rcWork.left);
    EXPECT_TRUE(cs.top    >= mi.rcWork.top);
    EXPECT_TRUE(cs.right  <= mi.rcWork.right);
    EXPECT_TRUE(cs.bottom <= mi.rcWork.bottom);
}

// ══════════════════════════════════════════════════════════════════
// T5：单位契约（captionHeight = 逻辑坐标 DIP——当前 DPI 1:1 精确可断言）
// ══════════════════════════════════════════════════════════════════

void TestCaptionHeightUnit()
{
    Application app;
    TestWindow win(app, ChromeMode::Borderless, 32, 0);   // inset 0 —— 隔离变量

    const HWND hwnd = win.Handle();
    RECT wr{};
    GetWindowRect(hwnd, &wr);
    const int w = wr.right - wr.left;

    EXPECT_EQ(HitTestLocal(hwnd, w / 2, 31), HTCAPTION);   // 31 < 32
    EXPECT_EQ(HitTestLocal(hwnd, w / 2, 33), HTCLIENT);    // 33 >= 32
}

// ══════════════════════════════════════════════════════════════════
// T6：边界值 clamp（契约「<= 0 视为 0」）
// ══════════════════════════════════════════════════════════════════

void TestZeroBoundaryClamp()
{
    Application app;
    TestWindow zero(app, ChromeMode::Borderless, 0, 0);
    TestWindow negative(app, ChromeMode::Borderless, -5, -8);

    EXPECT_EQ(HitTestLocal(zero.Handle(), 400, 4),     HTCLIENT);   // 无 caption / inset 区
    EXPECT_EQ(HitTestLocal(negative.Handle(), 400, 4), HTCLIENT);   // 负值 clamp 到 0 → 同上
}

// ══════════════════════════════════════════════════════════════════
// T7：状态事件（API 路径 + 去重）
// ══════════════════════════════════════════════════════════════════

void TestStateEventFromApi()
{
    TestApp app;
    TestWindow win(app, ChromeMode::Borderless, 32, 8);

    win.window->Show();
    PumpMessages(64);
    app.seen.clear();   // Show 本身不改变 minimized/maximized 状态

    win.window->Minimize();
    PumpMessages(64);
    EXPECT_EQ(app.seen.size(), std::size_t(1));
    EXPECT_EQ(app.seen.back(), WindowState::minimized);

    win.window->Restore();
    PumpMessages(64);
    EXPECT_EQ(app.seen.back(), WindowState::restored);

    win.window->Maximize();
    PumpMessages(64);
    EXPECT_EQ(app.seen.back(), WindowState::maximized);

    // 去重：连续两次 Maximize 只产生 1 个事件
    const std::size_t before = app.seen.size();
    win.window->Maximize();
    PumpMessages(64);
    EXPECT_EQ(app.seen.size(), before);
}

// ══════════════════════════════════════════════════════════════════
// T7a：运行期契约边界（Show() 前拒绝——框架行为，不依赖系统）
// ══════════════════════════════════════════════════════════════════

void TestRuntimeApiRejectedBeforeShow()
{
    TestApp app;
    TestWindow win(app, ChromeMode::Borderless, 32, 8);

    const HWND hwnd = win.Handle();

    // 构造后未 Show：三个运行期 API 全部被框架拒绝（无事件、窗口仍不可见）
    win.window->Minimize();
    win.window->Maximize();
    win.window->Restore();
    PumpMessages(32);

    EXPECT_EQ(app.seen.size(), std::size_t(0));
    EXPECT_FALSE(IsWindowVisible(hwnd));

    // Show 之后恢复正常
    win.window->Show();
    PumpMessages(32);
    win.window->Maximize();
    PumpMessages(64);

    EXPECT_FALSE(app.seen.empty());
    EXPECT_EQ(app.seen.back(), WindowState::maximized);
}

} // anonymous namespace

void ECDI::Test::RegisterWindowChromeTests()
{
    GetTestRegistry().Add("WindowChrome.ChromeModeDecidedOnce",          &TestChromeModeDecidedOnce);
    GetTestRegistry().Add("WindowChrome.BorderlessClientEqualsWindow",   &TestBorderlessClientEqualsWindow);
    GetTestRegistry().Add("WindowChrome.NCHitTestNineGrid",              &TestNCHitTestNineGrid);
    GetTestRegistry().Add("WindowChrome.NCHitTestModeIsolation",         &TestNCHitTestModeIsolation);
    GetTestRegistry().Add("WindowChrome.MaximizedClientWithinWorkArea",  &TestMaximizedClientWithinWorkArea);
    GetTestRegistry().Add("WindowChrome.CaptionHeightUnit",              &TestCaptionHeightUnit);
    GetTestRegistry().Add("WindowChrome.ZeroBoundaryClamp",              &TestZeroBoundaryClamp);
    GetTestRegistry().Add("WindowChrome.StateEventFromApi",              &TestStateEventFromApi);
    GetTestRegistry().Add("WindowChrome.RuntimeApiRejectedBeforeShow",   &TestRuntimeApiRejectedBeforeShow);
}
