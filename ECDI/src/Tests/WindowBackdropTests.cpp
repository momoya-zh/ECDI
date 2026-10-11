#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 防御性 undef（规范 10）：本文件含 Windows.h——防宏污染 ECDI 头声明
#endif

#include "ECDI/Core/Color.h"
#include "ECDI/Core/Rect.h"
#include "ECDI/Window/WindowBackdrop.h"
#include "Platform/Win32/Win32RenderContext.h"
#include "Render/GDIBackend.h"

#include <cstddef>
#include <memory>
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

void ECDI::Test::RegisterWindowBackdropTests()
{
    GetTestRegistry().Add("WindowBackdrop.ZeroRegression", &Test32DefaultZeroRegression);              // T32-1
    GetTestRegistry().Add("WindowBackdrop.DegradedClearAndCommitBranch", &Test32DegradedClearAndCommitBranch); // T32-4
    GetTestRegistry().Add("WindowBackdrop.ModeSwitchNoResidue", &Test32ModeSwitchNoResidue);           // T32-7
}
