#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 防御性 undef（规范 10）：本文件含 Windows.h——防宏污染 ECDI 头声明
#endif

#include "ECDI/Core/Color.h"
#include "ECDI/Core/Rect.h"
#include "ECDI/Window/WindowBackdrop.h"
#include "ECDI/Window/Window.h"
#include "ECDI/Application/Application.h"
#include "ECDI/Render/RenderServices.h"
#include "Platform/Win32/Win32RenderContext.h"
#include "Platform/Win32/Win32PlatformWindowInternal.h"   // Phase 32 D32-N：注入缝（测试专用——内部头先例）
#include "Render/GDIBackend.h"
#include "Render/RecordingBackend.h"

#include <cstddef>
#include <memory>
#include <type_traits>
#include <vector>

using namespace ECDI;

// ══════════════════════════════════════════════════════════════════════
// Phase 32 批一：缓冲地基（T32-1 默认零回归 / T32-4 Degraded 清屏+提交分支 /
// T32-7 模式切换无残留）——纯缓冲级断言，**零 DWM 依赖**（详设 §6 批一）。
// 装置先例：直构后端 = GLBackendTests（Win32RenderContext + Initialize）；
// 真窗口 = WindowBackgroundTests 同款（隐藏窗口 + BeginPaint 全客户区清屏）。
// ★ T32-4 的「DrawRect 后矩形 A=255」断言属 K① 修补（批三）——批一先落
//   「清屏语义 + 提交分支」两半（详设 §10 批一回填已记偏差）。
// ══════════════════════════════════════════════════════════════════════

namespace {

// 测试窗口（隐藏、普通 chrome——BeginPaint 取全客户区 DC；真实句柄是
// GDIBackend::Initialize（Win32RenderContext）的硬前提）
struct TestWindow
{
    HWND hwnd = nullptr;

    TestWindow()
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"ECDIWindowBackdropTests";
        RegisterClassW(&wc);   // 重复注册失败无害（ERROR_CLASS_ALREADY_EXISTS）

        hwnd = CreateWindowExW(0, wc.lpszClassName, L"WindowBackdropTests",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 300, 240,
            nullptr, nullptr, wc.hInstance, nullptr);
    }

    ~TestWindow()
    {
        if (hwnd) DestroyWindow(hwnd);
    }
};

// 位缓冲视图（top-down 32bpp：行距 = width×4，无行填充）
struct BitsView
{
    BYTE* base = nullptr;
    int width = 0;
    int height = 0;

    BYTE B(std::size_t i) const { return base[i * 4 + 0]; }
    BYTE G(std::size_t i) const { return base[i * 4 + 1]; }
    BYTE R(std::size_t i) const { return base[i * 4 + 2]; }
    BYTE A(std::size_t i) const { return base[i * 4 + 3]; }
    std::size_t Count() const { return static_cast<std::size_t>(width) * height; }
};

}   // namespace

// ── T32-1：默认零回归（A6①——None 模式清屏/绘制逐位同 Phase 18）──────────────
// 判据：① White 清屏后全缓冲 = BGRA(255,255,255,0)——RGB 精确 + **alpha 恒 0**
//       （GDI 写入不置 alpha 字节，K10/probe6 G5 实证）；② DrawRect 后矩形 =
//       绘制色、矩形外不变。缓冲 = 32bpp DIB（D32-E）⇒ 值即真相。
void Test32DefaultZeroRegression()
{
    TestWindow window;
    EXPECT_TRUE(window.hwnd != nullptr);

    GDIBackend backend;
    backend.Initialize(Win32RenderContext(window.hwnd));
    EXPECT_TRUE(backend.IsReady());

    RECT client{};
    GetClientRect(window.hwnd, &client);

    // ① White 清屏（None 路径 = CreateSolidBrush + FillRect 逐字保留）
    backend.BeginFrame(Color::White());

    // ★ 位指针必须在**首帧 BeginFrame 之后**捕获——EnsureBackBuffer 在 BeginFrame 内
    //   懒创建（首版把捕获放在其之前 = 解引用空指针，GDB 实证 SIGSEGV@base[i*4+2]）
    const BitsView bits{
        static_cast<BYTE*>(backend.DebugBackBufferBits()),
        client.right, client.bottom };
    EXPECT_TRUE(bits.base != nullptr);
    EXPECT_TRUE(bits.Count() > 0);

    for (std::size_t i = 0; i < bits.Count(); ++i)
    {
        EXPECT_EQ(bits.R(i), 255);
        EXPECT_EQ(bits.G(i), 255);
        EXPECT_EQ(bits.B(i), 255);
        EXPECT_EQ(bits.A(i), 0);        // ★ alpha 字节恒 0（非 255）——K10 语义
    }
    backend.EndFrame();

    // ② DrawRect（红色矩形）——矩形内 = 绘制色、矩形外 = 白，alpha 仍恒 0
    backend.BeginFrame(Color::White());
    backend.DrawRect(Rect{ 8.0f, 8.0f, 32.0f, 24.0f }, Color(1.0f, 0.0f, 0.0f));
    backend.EndFrame();

    const int rectX0 = 8, rectY0 = 8, rectX1 = 40, rectY1 = 32;
    for (int y = 0; y < bits.height; ++y)
    {
        for (int x = 0; x < bits.width; ++x)
        {
            const std::size_t i = static_cast<std::size_t>(y) * bits.width + x;
            const bool inside = (x >= rectX0 && x < rectX1 && y >= rectY0 && y < rectY1);
            EXPECT_EQ(bits.A(i), 0);
            if (inside)
            {
                EXPECT_EQ(bits.R(i), 255);   // 红 = (255,0,0) → BGRA 字节 (0,0,255)
                EXPECT_EQ(bits.G(i), 0);
                EXPECT_EQ(bits.B(i), 0);
            }
            else
            {
                EXPECT_EQ(bits.R(i), 255);
                EXPECT_EQ(bits.G(i), 255);
                EXPECT_EQ(bits.B(i), 255);
            }
        }
    }
}

// ── T32-4：Degraded 清屏语义 + 提交分支（缓冲级）────────────────────────────
// 判据：① SetBackdropSurface(Degraded, 0xCCCCCCCC) 后清屏 = **全缓冲四字节 204**
//       （预乘白 0.8——R-c 的字面形态，P4/P5 实证配置）；② 提交分支 = AlphaBlend
//       （DebugCommitUsesAlphaBlend 观测缝——映射 = GDIBackend 私有，D32-R）；
//       ③ None 下达后分支恢复 BitBlt。★ 「DrawRect 后矩形 A=255」随批三 K① 补齐。
void Test32DegradedClearAndCommitBranch()
{
    TestWindow window;
    EXPECT_TRUE(window.hwnd != nullptr);

    GDIBackend backend;
    backend.Initialize(Win32RenderContext(window.hwnd));

    RECT client{};
    GetClientRect(window.hwnd, &client);

    // ① 下达 Degraded（预乘白 0.8：AABBGGRR = 0xCCCCCCCC）
    backend.SetBackdropSurface(BackdropMode::Degraded, 0xCCCCCCCCu);
    EXPECT_TRUE(backend.DebugCommitUsesAlphaBlend());   // ② 提交分支 = AlphaBlend

    // 清屏：全缓冲四字节 = 204（背景色形参在衬底模式下不消费）
    backend.BeginFrame(Color::White());

    // ★ 位指针在首帧 BeginFrame 后捕获（同 T32-1 的勘误——首版空指针解引用）
    const BitsView bits{
        static_cast<BYTE*>(backend.DebugBackBufferBits()),
        client.right, client.bottom };
    EXPECT_TRUE(bits.base != nullptr);

    for (std::size_t i = 0; i < bits.Count(); ++i)
    {
        EXPECT_EQ(bits.B(i), 204);
        EXPECT_EQ(bits.G(i), 204);
        EXPECT_EQ(bits.R(i), 204);
        EXPECT_EQ(bits.A(i), 204);   // ★ alpha 也 = 204（衬底清屏写全四字节——T32-1 的 0 相对照）
    }
    backend.EndFrame();              // AlphaBlend 提交路径（隐藏窗口，可安全执行）

    // ③ None 下达 ⇒ 分支恢复 BitBlt（且下帧起走既有清屏路径）
    backend.SetBackdropSurface(BackdropMode::None, 0);
    EXPECT_TRUE(!backend.DebugCommitUsesAlphaBlend());
}

// ── T32-7：模式切换无残留（SW1 的缓冲级形态）────────────────────────────────
// 判据：Acrylic 清屏（tintRGB + A=0）→ None 清屏（背景色）→ 再切 Acrylic——
// 每次清屏后**全缓冲**逐位等于新模式期望值（旧模式像素零幸存；每帧整客户区
// 清屏 = 决策 29 完整重绘模型 ⇒ 无跨模式残留路径，D32-H）。
void Test32ModeSwitchNoResidue()
{
    TestWindow window;
    EXPECT_TRUE(window.hwnd != nullptr);

    GDIBackend backend;
    backend.Initialize(Win32RenderContext(window.hwnd));

    RECT client{};
    GetClientRect(window.hwnd, &client);

    // tint RGB(56,47,43) + A=0 → AABBGGRR = 0x00382F2B（探针 7 M0/M1 的 tint）
    const std::uint32_t tintABGR = 0x00382F2Bu;

    // ① Acrylic：全缓冲 = BGRA(43,47,56,0)
    backend.SetBackdropSurface(BackdropMode::Acrylic, tintABGR);
    backend.BeginFrame(Color::White());

    // ★ 位指针在首帧 BeginFrame 后捕获（同 T32-1 的勘误——首版空指针解引用）
    const BitsView bits{
        static_cast<BYTE*>(backend.DebugBackBufferBits()),
        client.right, client.bottom };
    EXPECT_TRUE(bits.base != nullptr);

    for (std::size_t i = 0; i < bits.Count(); ++i)
    {
        EXPECT_EQ(bits.B(i), 43);
        EXPECT_EQ(bits.G(i), 47);
        EXPECT_EQ(bits.R(i), 56);
        EXPECT_EQ(bits.A(i), 0);
    }
    backend.EndFrame();

    // ② None：全缓冲 = 背景色绿（0,255,0）+ A=0——**无 tint 残留**
    backend.SetBackdropSurface(BackdropMode::None, 0);
    backend.BeginFrame(Color(0.0f, 1.0f, 0.0f));
    for (std::size_t i = 0; i < bits.Count(); ++i)
    {
        EXPECT_EQ(bits.B(i), 0);
        EXPECT_EQ(bits.G(i), 255);
        EXPECT_EQ(bits.R(i), 0);
        EXPECT_EQ(bits.A(i), 0);
    }
    backend.EndFrame();

    // ③ 再切 Acrylic：tint 重新铺满（无 204/绿残留）
    backend.SetBackdropSurface(BackdropMode::Acrylic, tintABGR);
    backend.BeginFrame(Color::White());
    for (std::size_t i = 0; i < bits.Count(); ++i)
    {
        EXPECT_EQ(bits.B(i), 43);
        EXPECT_EQ(bits.G(i), 47);
        EXPECT_EQ(bits.R(i), 56);
        EXPECT_EQ(bits.A(i), 0);
    }
    backend.EndFrame();
}

// ══════════════════════════════════════════════════════════════════════
// Phase 32 批二：材质应用与状态机（T32-5 失败注入 / T32-6 配置期契约 / T32-8 结构性）
// 装置 = WindowBackgroundTests 先例（Application::Create + RenderServices 注入 +
// Show/泵驱动——PaintFrame 是 private），但后端注入**真实 GDIBackend**（观测缝
// DebugCommitUsesAlphaBlend 断言「renderer 确已收到 Degraded 语义」——评审 P2-4 的
// 分支覆盖项）。注入缝 = D32-N（进程级全局——**串行假设**，用后恢复 None）。
// ══════════════════════════════════════════════════════════════════════

namespace {

/// @brief 手动消息泵（仿 WindowBackgroundTests.cpp:37——上限防 WM_PAINT 洪水）
void PumpMessages(int maxCount)
{
    MSG msg{};
    for (int i = 0; i < maxCount && PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE); ++i)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

/// @brief 测试装置：Application + **真实 GDIBackend**（观测缝可达）
/// @details 成员声明序保证 app 最后析构（窗口先亡——WindowBackgroundTests.cpp:50 同约束）。
struct BackdropProbe
{
    Application app;
    GDIBackend* gdi = nullptr;    ///< **非拥有**（所有权已 move 进 RenderServices → Window）
    Window* window = nullptr;     ///< **非拥有**（所有权归 Application）

    explicit BackdropProbe(const char* title)
    {
        auto backendOwned = std::make_unique<GDIBackend>();
        gdi = backendOwned.get();
        RenderServices services{
            std::move(backendOwned),
            std::make_unique<RecordingBackend>()   // 测量占位（不参与断言）
        };
        window = &app.Create(title, 400, 300, std::move(services));
    }

    ~BackdropProbe() { window->Release(); }

    void ShowAndPump() { window->Show(); PumpMessages(64); }

    /// @brief Show + 泵 + **失效驱动第二帧**（★ Show 内部 UpdateWindow 的首帧发生在
    /// ApplyBackdrop 之前——衬底实际态需 Invalidate 后的下一帧才到达后端）
    void ShowApplyAndRepaint() { window->Show(); PumpMessages(64); window->Invalidate(); PumpMessages(64); }
};

const Color kBackdropTint{ 0.22f, 0.18f, 0.17f, 0.6f };   // 深雾 tint（α 0.6）

}   // namespace

// ── T32-5：失败注入状态机（D32-N 两类故障分开覆盖——外部评审 P2-4）──────────
// 判据：① FailResolve ⇒ 解析失败**不调用未解析函数**，直接 Degraded；
//       ② FailCall ⇒ 调用路径真实执行、结果强制 FALSE ⇒ 同落 Degraded；
//       ③ 两者的「renderer 收到 Degraded 提交语义」都经真实 GDIBackend 观测缝验证。
// ★ 注入标志为进程级全局（D32-N）——本用例串行使用并在末尾恢复 None（不得并行）。
void Test32FailureInjectionStateMachine()
{
    // ① FailResolve
    SetBackdropInjectionForTests(BackdropInjection::FailResolve);
    {
        BackdropProbe probe("ECDI_BdFailResolve");
        probe.window->SetWindowBackdrop(BackdropMode::Acrylic, kBackdropTint);
        probe.ShowApplyAndRepaint();

        const WindowBackdropState s = probe.window->GetWindowBackdropState();
        EXPECT_TRUE(s.requested == BackdropMode::Acrylic);
        EXPECT_TRUE(s.applied);
        EXPECT_TRUE(s.actual == BackdropMode::Degraded);      // 解析失败 ⇒ Degraded（绝不裸透）
        EXPECT_TRUE(probe.gdi->DebugCommitUsesAlphaBlend());  // renderer 确已收到 Degraded 语义
    }

    // ② FailCall
    SetBackdropInjectionForTests(BackdropInjection::FailCall);
    {
        BackdropProbe probe("ECDI_BdFailCall");
        probe.window->SetWindowBackdrop(BackdropMode::Acrylic, kBackdropTint);
        probe.ShowApplyAndRepaint();

        const WindowBackdropState s = probe.window->GetWindowBackdropState();
        EXPECT_TRUE(s.applied);
        EXPECT_TRUE(s.actual == BackdropMode::Degraded);
        EXPECT_TRUE(probe.gdi->DebugCommitUsesAlphaBlend());
    }

    SetBackdropInjectionForTests(BackdropInjection::None);   // ★ 恢复（串行假设的契约）
}

// ── T32-6：配置期契约 + Degraded 忽略语义（D32-M）──────────────────────────
// 判据：① Degraded 请求（Show 前）⇒ 完全忽略且不改请求态；② Show 前 applied=false；
//       ③ Show 后 applied=true（注入 FailResolve ⇒ actual 确定性 Degraded，与机器解耦）；
//       ④ Show 后 SetWindowBackdrop ⇒ Warning + 请求态不变；⑤ Degraded 请求（Show 后）同忽略。
void Test32ConfigGateAndDegradedIgnore()
{
    SetBackdropInjectionForTests(BackdropInjection::FailResolve);   // actual 判定与机器能力解耦
    {
        BackdropProbe probe("ECDI_BdConfig");

        // ① Degraded 请求 ⇒ 完全忽略
        probe.window->SetWindowBackdrop(BackdropMode::Degraded, kBackdropTint);
        EXPECT_TRUE(probe.window->GetWindowBackdropState().requested == BackdropMode::None);

        // ② Show 前：applied=false、actual 无语义（None）
        EXPECT_TRUE(!probe.window->GetWindowBackdropState().applied);
        EXPECT_TRUE(probe.window->GetWindowBackdropState().actual == BackdropMode::None);

        // ③ 正常请求 + Show（FailResolve ⇒ Degraded 确定性）
        probe.window->SetWindowBackdrop(BackdropMode::Acrylic, kBackdropTint);
        EXPECT_TRUE(probe.window->GetWindowBackdropState().requested == BackdropMode::Acrylic);
        probe.ShowApplyAndRepaint();
        {
            const WindowBackdropState s = probe.window->GetWindowBackdropState();
            EXPECT_TRUE(s.applied);
            EXPECT_TRUE(s.actual == BackdropMode::Degraded);
        }

        // ④ Show 后 SetWindowBackdrop ⇒ Warning + 请求态不变（不重应用）
        probe.window->SetWindowBackdrop(BackdropMode::None, Color::Black());
        EXPECT_TRUE(probe.window->GetWindowBackdropState().requested == BackdropMode::Acrylic);

        // ⑤ Degraded 请求（Show 后）⇒ 同样忽略
        probe.window->SetWindowBackdrop(BackdropMode::Degraded, kBackdropTint);
        EXPECT_TRUE(probe.window->GetWindowBackdropState().requested == BackdropMode::Acrylic);
    }
    SetBackdropInjectionForTests(BackdropInjection::None);   // ★ 恢复
}

// ── T32-8：结构性判据（A9②——C++ 可表达部分；grep 判据 + 每公共头无 Windows.h
//    独立编译由既有 ecdi_public_header_test 目标承载）──────────────────────────
void Test32StructuralSignatures()
{
    // ① 公共查询签名钉死（Win32 类型无法混入——本断言即证）
    static_assert(std::is_same_v<decltype(&ECDI::Window::GetWindowBackdropState),
        ECDI::WindowBackdropState (ECDI::Window::*)() const noexcept>,
        "Window::GetWindowBackdropState signature must stay Win32-free");
    // ② 后端签名 = 既有公共枚举 + 原语（外部评审 P1-1 的解——不得回退为内部类型）
    static_assert(std::is_same_v<decltype(&ECDI::RenderingBackend::SetBackdropSurface),
        void (ECDI::RenderingBackend::*)(ECDI::BackdropMode, std::uint32_t)>,
        "RenderingBackend::SetBackdropSurface must stay primitives-only");
    // ③ 带默认实现（非纯虚）——TestPlatformWindow×2 与 RecordingBackend/GLRenderer
    //    的零改动编译是机制面证明；本断言把「GDIBackend 可实例化」显式化。
    static_assert(!std::is_abstract_v<ECDI::GDIBackend>);
    EXPECT_TRUE(true);   // 主断言在编译期
}

// ── T32-2：Acrylic 缓冲语义 + DrawRect（K①）────────────────────────────────
// 判据：① 清屏 = 全缓冲 (tint, A=0)；② DrawRect(红) 后矩形内 = 绘制色 + **A=255**
// （K① 矩形级修补）、矩形外 = (tint, A=0)。哨兵字节序 = B,G,R（C32-11）。
void Test32AcrylicDrawRectSemantics()
{
    TestWindow window;
    GDIBackend backend;
    backend.Initialize(Win32RenderContext(window.hwnd));

    RECT client{};
    GetClientRect(window.hwnd, &client);

    backend.SetBackdropSurface(BackdropMode::Acrylic, 0x00382F2Bu);   // tint RGB(56,47,43) A=0
    backend.BeginFrame(Color::White());

    const BitsView bits{
        static_cast<BYTE*>(backend.DebugBackBufferBits()),
        client.right, client.bottom };
    EXPECT_TRUE(bits.base != nullptr);

    // ① 清屏：全缓冲 = BGRA(43,47,56,0)
    for (std::size_t i = 0; i < bits.Count(); ++i)
    {
        EXPECT_EQ(bits.B(i), 43);
        EXPECT_EQ(bits.G(i), 47);
        EXPECT_EQ(bits.R(i), 56);
        EXPECT_EQ(bits.A(i), 0);
    }

    // ② DrawRect(红) —— K① 矩形级 A=255
    backend.DrawRect(Rect{ 16.0f, 16.0f, 64.0f, 48.0f }, Color(1.0f, 0.0f, 0.0f));
    backend.EndFrame();

    for (int y = 0; y < bits.height; ++y)
    {
        for (int x = 0; x < bits.width; ++x)
        {
            const std::size_t i = static_cast<std::size_t>(y) * bits.width + x;
            const bool inside = (x >= 16 && x < 80 && y >= 16 && y < 64);
            if (inside)
            {
                EXPECT_EQ(bits.R(i), 255);
                EXPECT_EQ(bits.G(i), 0);
                EXPECT_EQ(bits.B(i), 0);
                EXPECT_EQ(bits.A(i), 255);   // ★ K①
            }
            else
            {
                EXPECT_EQ(bits.B(i), 43);
                EXPECT_EQ(bits.G(i), 47);
                EXPECT_EQ(bits.R(i), 56);
                EXPECT_EQ(bits.A(i), 0);
            }
        }
    }
}

// ── T32-2b：圆角矩形覆盖率预乘直写（K②）——sum(A)/255 ≈ 覆盖面积不变式 ──────
// 几何：Rect{40,40,160,120} R=20 ⇒ 覆盖面积 = 160×120 − (4−π)×20² ≈ 18856.6；
// 角外 = (tint, A=0)；带内 = 原色 A=255（a=1）；另抽 a=0.5 圆角矩形 ⇒ 带内 A≈128
//（预乘：材质按比例透过——K② 的 a 维统一）。
void Test32RoundedPremultipliedCoverage()
{
    TestWindow window;
    GDIBackend backend;
    backend.Initialize(Win32RenderContext(window.hwnd));

    RECT client{};
    GetClientRect(window.hwnd, &client);

    backend.SetBackdropSurface(BackdropMode::Acrylic, 0x00382F2Bu);
    backend.BeginFrame(Color::White());
    const BitsView bits{
        static_cast<BYTE*>(backend.DebugBackBufferBits()),
        client.right, client.bottom };
    EXPECT_TRUE(bits.base != nullptr);

    backend.DrawRoundedRect(Rect{ 40.0f, 40.0f, 160.0f, 120.0f }, 20.0f, Color(0.0f, 0.0f, 1.0f));
    backend.EndFrame();

    // ① 覆盖率 sum 不变式（bbox = 圆角矩形外接矩形）
    double coverageSum = 0.0;
    for (int y = 40; y < 160; ++y)
    {
        for (int x = 40; x < 200; ++x)
        {
            const std::size_t i = static_cast<std::size_t>(y) * bits.width + x;
            coverageSum += bits.A(i) / 255.0;
        }
    }
    const double expected = 160.0 * 120.0 - (4.0 - 3.14159265358979) * 20.0 * 20.0;
    EXPECT_TRUE(coverageSum > expected - 16.0);
    EXPECT_TRUE(coverageSum < expected + 16.0);

    // ② 带内点 = 原色 A=255；角外点 = (tint, A=0)
    {
        const std::size_t inner = static_cast<std::size_t>(100) * bits.width + 120;
        EXPECT_EQ(bits.B(inner), 255);
        EXPECT_EQ(bits.A(inner), 255);
    }
    {
        const std::size_t corner = static_cast<std::size_t>(41) * bits.width + 41;
        EXPECT_EQ(bits.B(corner), 43);
        EXPECT_EQ(bits.A(corner), 0);
    }

    // ③ a=0.5 圆角矩形（另一区域）——带内 A≈128、RGB≈预乘（K② 的 a 维）
    backend.BeginFrame(Color::White());
    backend.DrawRoundedRect(Rect{ 20.0f, 20.0f, 60.0f, 40.0f }, 8.0f, Color(1.0f, 1.0f, 1.0f, 0.5f));
    backend.EndFrame();
    {
        const std::size_t inner = static_cast<std::size_t>(40) * bits.width + 50;
        EXPECT_TRUE(bits.A(inner) >= 126 && bits.A(inner) <= 130);   // 255×0.5 ±1
        EXPECT_TRUE(bits.R(inner) >= 126 && bits.R(inner) <= 130);   // 预乘 RGB = 255×0.5
    }
}

// ── T32-3：文字哨兵修补不变式（K③——实现不变式，非视觉验收）────────────────
// 判据（文字扫描带内）：① 全体像素 A∈{0,255}；② A=255 ⇔ RGB≠tint；③ A=255 计数 > 0
//（有字形）且 A=0 计数 > 0（有字缝）。★ AA 边缘的视觉正确性 = 批四端到端（D32-P④）。
void Test32TextSentinelInvariant()
{
    TestWindow window;
    GDIBackend backend;
    backend.Initialize(Win32RenderContext(window.hwnd));

    RECT client{};
    GetClientRect(window.hwnd, &client);

    backend.SetBackdropSurface(BackdropMode::Acrylic, 0x00382F2Bu);
    backend.BeginFrame(Color::White());
    const BitsView bits{
        static_cast<BYTE*>(backend.DebugBackBufferBits()),
        client.right, client.bottom };
    EXPECT_TRUE(bits.base != nullptr);

    Font font;
    font.size = 24.0f;
    font.family = "Segoe UI";
    backend.DrawText(Point{ 20.0f, 20.0f }, "AaBb 09", Color(1.0f, 1.0f, 1.0f), font);
    backend.EndFrame();

    int opaque = 0;
    int gaps = 0;
    int bad = 0;
    for (int y = 18; y < 90; ++y)
    {
        for (int x = 18; x < 220; ++x)
        {
            const std::size_t i = static_cast<std::size_t>(y) * bits.width + x;
            const bool isSentinel = (bits.B(i) == 43 && bits.G(i) == 47 && bits.R(i) == 56);
            if (bits.A(i) != 0 && bits.A(i) != 255) { ++bad; continue; }
            if (bits.A(i) == 255)
            {
                if (isSentinel) { ++bad; continue; }   // A=255 ⇔ RGB≠tint 的逆否
                ++opaque;
            }
            else if (isSentinel)
            {
                ++gaps;   // 字缝/背景 = 哨兵透材质
            }
        }
    }
    EXPECT_EQ(bad, 0);        // 不变式：无第三态、无 A=255 的哨兵色
    EXPECT_TRUE(opaque > 0);  // 有字形
    EXPECT_TRUE(gaps > 0);    // 有字缝（材质透出）
}

void ECDI::Test::RegisterWindowBackdropTests()
{
    GetTestRegistry().Add("WindowBackdrop.ZeroRegression", &Test32DefaultZeroRegression);              // T32-1
    GetTestRegistry().Add("WindowBackdrop.DegradedClearAndCommitBranch", &Test32DegradedClearAndCommitBranch); // T32-4
    GetTestRegistry().Add("WindowBackdrop.ModeSwitchNoResidue", &Test32ModeSwitchNoResidue);           // T32-7
    GetTestRegistry().Add("WindowBackdrop.FailureInjectionStateMachine", &Test32FailureInjectionStateMachine); // T32-5
    GetTestRegistry().Add("WindowBackdrop.ConfigGateAndDegradedIgnore", &Test32ConfigGateAndDegradedIgnore);   // T32-6
    GetTestRegistry().Add("WindowBackdrop.StructuralSignatures", &Test32StructuralSignatures);         // T32-8
    GetTestRegistry().Add("WindowBackdrop.AcrylicDrawRectSemantics", &Test32AcrylicDrawRectSemantics);       // T32-2
    GetTestRegistry().Add("WindowBackdrop.RoundedPremultipliedCoverage", &Test32RoundedPremultipliedCoverage); // T32-2b
    GetTestRegistry().Add("WindowBackdrop.TextSentinelInvariant", &Test32TextSentinelInvariant);              // T32-3
}
