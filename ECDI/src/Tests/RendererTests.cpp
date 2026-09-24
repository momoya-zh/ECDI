#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 防御性 undef（规范 10）：本文件含 Windows.h——防 DrawTextW 宏污染 ECDI 头声明
#endif

#include "Render/GDIBackend.h"
#include "ECDI/Render/Renderer.h"
#include "Render/RecordingBackend.h"
#include "ECDI/Render/PaintContext.h"
#include "Platform/Win32/Win32RenderContext.h"
#include "ECDI/Core/Point.h"
#include "ECDI/Core/Color.h"
#include "ECDI/Core/Font.h"
#include "ECDI/Core/Image.h"
#include "ECDI/Core/UTF8.h"

#include <cmath>    // std::abs（浮点 epsilon 比较）
#include <utility>

using namespace ECDI;

// 浮点近似统一用 EXPECT_NEAR（TestFramework.h）——kEpsilon 为本模块容差基准
constexpr float kEpsilon = 0.001f;

namespace {

void TestRendererForwarding()
{
    // ── 4.5 原 #1：Command → Renderer → RecordingBackend ──
    RecordingBackend backend;
    Renderer renderer(backend);

    CommandBuffer commands;
    commands.emplace_back(DrawRectCommand{ Rect{ 0, 0, 100, 100 }, Color::Red() });
    commands.emplace_back(DrawRectCommand{ Rect{ 10, 20, 30, 40 }, Color::Gray() });

    renderer.Execute(commands);

    EXPECT_EQ(backend.draws.size(), 2);
    EXPECT_NEAR(backend.draws[0].rect.x, 0.0f, kEpsilon);
    EXPECT_NEAR(backend.draws[0].rect.width, 100.0f, kEpsilon);
    EXPECT_NEAR(backend.draws[0].color.r, 1.0f, kEpsilon);
    EXPECT_NEAR(backend.draws[0].color.a, 1.0f, kEpsilon);
    EXPECT_NEAR(backend.draws[1].rect.x, 10.0f, kEpsilon);
    EXPECT_NEAR(backend.draws[1].rect.y, 20.0f, kEpsilon);
    EXPECT_NEAR(backend.draws[1].color.g, 0.5f, kEpsilon);
}

void TestUTF8Utility()
{
    // ── 5.5.1.1 原 #6：UTF-8 工具自测 ──
    EXPECT_EQ(EncodeUTF8(U'A'), "A");
    EXPECT_EQ(EncodeUTF8(U'中'), "\xE4\xB8\xAD");
    EXPECT_EQ(EncodeUTF8(U'😀'), "\xF0\x9F\x98\x80");

    const std::string s = "a中😀";
    EXPECT_EQ(CodepointIndexToByteOffset(s, 0), 0);
    EXPECT_EQ(CodepointIndexToByteOffset(s, 1), 1);
    EXPECT_EQ(CodepointIndexToByteOffset(s, 2), 4);
    EXPECT_EQ(CodepointIndexToByteOffset(s, 3), 8);
    EXPECT_EQ(ByteOffsetToCodepointIndex(s, 4), 2);
    EXPECT_EQ(ByteOffsetToCodepointIndex(s, 8), 3);
}

void TestRendererNewCommands()
{
    // ── Phase 8 新增 #1：Command → Renderer → RecordingBackend 参数原样转发 ──
    RecordingBackend backend;
    Renderer renderer(backend);

    CommandBuffer commands;
    commands.emplace_back(DrawLineCommand{ Point{ 1, 2 }, Point{ 30, 40 }, 2.5f, Color::Blue() });
    commands.emplace_back(DrawRoundedRectCommand{ Rect{ 5, 5, 50, 30 }, 4.0f, Color::Green() });

    Image img;
    img.width = 2;
    img.height = 1;
    img.stride = 8;
    img.pixels = { 0, 0, 255, 255, 0, 255, 0, 255 };   // premultiplied BGRA：红不透明 + 绿不透明
    commands.emplace_back(DrawImageCommand{ Rect{ 10, 10, 20, 10 }, img });

    // Push/Pop 夹着一个 DrawRect：验证状态命令被转发且顺序保持
    commands.emplace_back(PushClipCommand{ Rect{ 0, 0, 100, 100 } });
    commands.emplace_back(DrawRectCommand{ Rect{ 0, 0, 50, 50 }, Color::Red() });
    commands.emplace_back(PopClipCommand{});
    commands.emplace_back(DrawFocusRectCommand{ Rect{ 2, 3, 10, 10 }, 4.0f, Color::Black() });

    renderer.Execute(commands);

    // DrawLine：宽度 float 原样转发（不预取整——取整是 GDI 后端细节）
    EXPECT_EQ(backend.lineCalls.size(), 1);
    EXPECT_NEAR(backend.lineCalls[0].start.x, 1.0f, kEpsilon);
    EXPECT_NEAR(backend.lineCalls[0].end.y, 40.0f, kEpsilon);
    EXPECT_NEAR(backend.lineCalls[0].width, 2.5f, kEpsilon);
    EXPECT_NEAR(backend.lineCalls[0].color.b, 1.0f, kEpsilon);   // Blue

    // DrawRoundedRect
    EXPECT_EQ(backend.roundedRectCalls.size(), 1);
    EXPECT_NEAR(backend.roundedRectCalls[0].rect.width, 50.0f, kEpsilon);
    EXPECT_NEAR(backend.roundedRectCalls[0].cornerRadius, 4.0f, kEpsilon);
    EXPECT_NEAR(backend.roundedRectCalls[0].color.g, 1.0f, kEpsilon);   // Green

    // DrawImage（Image 值拷贝转发）
    EXPECT_EQ(backend.imageCalls.size(), 1);
    EXPECT_NEAR(backend.imageCalls[0].dest.width, 20.0f, kEpsilon);
    EXPECT_EQ(backend.imageCalls[0].image.width, 2);
    EXPECT_EQ(backend.imageCalls[0].image.height, 1);

    // Push/Pop 共列保序：Push(100x100) → (DrawRect) → Pop
    EXPECT_EQ(backend.clipOps.size(), 2);
    EXPECT_TRUE(backend.clipOps[0].isPush);
    EXPECT_NEAR(backend.clipOps[0].rect.width, 100.0f, kEpsilon);
    EXPECT_FALSE(backend.clipOps[1].isPush);
    // 裁剪内 DrawRect 照常转发（状态命令不影响其他转发）
    EXPECT_EQ(backend.draws.size(), 1);

    // DrawFocusRect（9.5 R4：cornerRadius 转发）
    EXPECT_EQ(backend.focusRectCalls.size(), 1);
    EXPECT_NEAR(backend.focusRectCalls[0].rect.x, 2.0f, kEpsilon);
    EXPECT_NEAR(backend.focusRectCalls[0].cornerRadius, 4.0f, kEpsilon);
}

void TestPaintContextNewCommands()
{
    // ── Phase 8 新增 #2：PaintContext → Command（Push/Pop 必须按序进缓冲）──
    CommandBuffer commands;
    RecordingBackend backend;   // 仅作 TextMeasurer 占位（构造需要），不参与断言
    PaintContext pc(commands, backend);

    pc.PushClip(Rect{ 0, 0, 80, 80 });
    pc.DrawLine(Point{ 0, 0 }, Point{ 10, 10 }, 1.0f, Color::Black());
    pc.DrawRoundedRect(Rect{ 1, 1, 20, 20 }, 3.0f, Color::Gray());
    pc.PopClip();
    pc.DrawFocusRect(Rect{ 5, 5, 12, 12 }, 2.0f, Color::Blue());
    pc.DrawImage(Rect{ 0, 0, 8, 8 }, Image{});   // 空图像也产生命令（no-op 判定在后端）

    // 顺序断言：Push → DrawLine → DrawRoundedRect → Pop → DrawFocusRect → DrawImage
    EXPECT_EQ(commands.size(), 6);
    EXPECT_TRUE(std::holds_alternative<PushClipCommand>(commands[0]));
    EXPECT_TRUE(std::holds_alternative<DrawLineCommand>(commands[1]));
    EXPECT_TRUE(std::holds_alternative<DrawRoundedRectCommand>(commands[2]));
    EXPECT_TRUE(std::holds_alternative<PopClipCommand>(commands[3]));
    EXPECT_TRUE(std::holds_alternative<DrawFocusRectCommand>(commands[4]));
    EXPECT_TRUE(std::holds_alternative<DrawImageCommand>(commands[5]));

    // 字段抽查：DrawLine 宽度原样进命令
    const auto& line = std::get<DrawLineCommand>(commands[1]);
    EXPECT_NEAR(line.width, 1.0f, kEpsilon);
    EXPECT_NEAR(line.start.x, 0.0f, kEpsilon);

    // 9.5 R4 字段抽查：DrawFocusRect 圆角半径进命令
    const auto& focus = std::get<DrawFocusRectCommand>(commands[4]);
    EXPECT_NEAR(focus.cornerRadius, 2.0f, kEpsilon);
}

void TestImageValueSemantic()
{
    // ── Phase 8 新增 #3：Command 持有独立 Image 拷贝（值语义）──
    Image img;
    img.width = 2;
    img.height = 1;
    img.stride = 8;
    img.pixels = { 0, 0, 255, 255, 0, 255, 0, 255 };   // premultiplied BGRA：红不透明 + 绿不透明

    CommandBuffer commands;
    RecordingBackend backend;   // 仅作 TextMeasurer 占位
    PaintContext pc(commands, backend);
    pc.DrawImage(Rect{ 0, 0, 2, 1 }, img);

    // DrawImage 后修改原图 → 命令内副本不受影响
    img.pixels[0] = 255;   // 像素 0 的 B 通道置高（原 0）
    img.pixels[1] = 255;   // 像素 0 的 G 通道置高（原 0）
    img.pixels[2] = 0;     // 像素 0 的 R 通道清零（原 255）

    const auto& cmd = std::get<DrawImageCommand>(commands[0]);
    EXPECT_EQ(cmd.image.pixels[0], 0);     // 像素 0 的 B 未变
    EXPECT_EQ(cmd.image.pixels[2], 255);   // 像素 0 的 R 未变（红未变）
    EXPECT_EQ(cmd.image.pixels[4], 0);     // 像素 1 的 B 未变
    EXPECT_EQ(cmd.image.pixels[5], 255);   // 像素 1 的 G 未变（绿未变）
}

void TestGDIBackendAlphaBlend()
{
    // ── Phase 8 像素级验证（方案 A）：GDIBackend 真实 GDI 输出 ──
    // 无窗口契约测试验证"命令转发"，本测试验证"渲染结果"：
    // 50% 透明红（premultiplied）叠纯蓝底 → AlphaBlend 期望 RGB(128, 0, 127)（±3 容差）。
    // 同时隐式验证：DrawRect 像素输出、top-down DIB 行序/stride 拷贝、AC_SRC_ALPHA 链路。

    // 1. 测试窗口：屏幕右下角短暂显示（2026-08-21 实测：屏幕外窗口的窗口 DC
    //    裁剪区域为空，GetPixel 恒返回 CLR_INVALID——GetPixel 受 DC 裁剪约束）；
    //    SW_SHOWNOACTIVATE 不抢焦点，测试后立即销毁。
    const int screenW = GetSystemMetrics(SM_CXSCREEN);
    const int screenH = GetSystemMetrics(SM_CYSCREEN);
    const wchar_t* kClassName = L"ECDI_TestGDIClass";
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    if (!RegisterClassW(&wc))
    {
        EXPECT_EQ(GetLastError(), ERROR_CLASS_ALREADY_EXISTS);   // 重复运行忽略
    }

    HWND hwnd = CreateWindowExW(0, kClassName, L"ECDI_PixelTest", WS_POPUP,
                                screenW - 210, screenH - 210, 200, 200,
                                nullptr, nullptr, wc.hInstance, nullptr);
    EXPECT_TRUE(hwnd != nullptr);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);

    {
        // 2. 真实帧：纯蓝底 + 中央 100x100 半透明红
        GDIBackend backend;
        backend.Initialize(Win32RenderContext(hwnd));
        // Phase 18：清屏色改为每帧输入 ⇒ 显式传白（等价于改动前的行为——决策 16 清屏白）
        backend.BeginFrame(Color::White());
        backend.DrawRect(Rect{ 0, 0, 200, 200 }, Color::Blue());

        Image img;   // 32x32 premultiplied 50% 纯红（B0 G0 R128 A128）
        img.width = 32;
        img.height = 32;
        img.stride = 32 * 4;
        img.pixels.assign(32 * 32 * 4, 0);
        for (int i = 0; i < 32 * 32; ++i)
        {
            img.pixels[i * 4 + 2] = 128;   // R（premultiplied：RGB 已乘 alpha）
            img.pixels[i * 4 + 3] = 128;   // A
        }
        backend.DrawImage(Rect{ 50, 50, 100, 100 }, img);
        backend.EndFrame();

        // 3. EndFrame 后从窗口表面读回像素（BitBlt 已提交到窗口 DC）
        HDC dc = GetDC(hwnd);
        const COLORREF corner = GetPixel(dc, 5, 5);        // 角落：纯蓝底
        const COLORREF center = GetPixel(dc, 100, 100);    // 中央：混合结果
        ReleaseDC(hwnd, dc);

        // 4. 断言（±3 容差——AlphaBlend 内部取整）
        //    out = src + dst*(1-A/255)：R = 128 + 0*0.5 = 128；B = 255*(127/255) ≈ 127
        EXPECT_TRUE(corner != CLR_INVALID);
        EXPECT_TRUE(center != CLR_INVALID);
        EXPECT_TRUE(GetRValue(corner) <= 3 && GetGValue(corner) <= 3 && GetBValue(corner) >= 252);
        EXPECT_TRUE(std::abs(GetRValue(center) - 128) <= 3);
        EXPECT_TRUE(GetGValue(center) <= 3);
        EXPECT_TRUE(std::abs(GetBValue(center) - 127) <= 3);
    }

    DestroyWindow(hwnd);
}

// ── 9.5 Alpha Primitive 补强：DrawRect 半透明（Color.a < 1 → 预乘 DIB + AlphaBlend）──
void TestDrawRectAlpha()
{
    // 无窗口契约测试验证"命令转发"，本测试验证"渲染结果"：
    // 50% 透明红（Color.a = 0.5，未预乘输入）叠纯蓝底 → 期望 RGB(128, 0, 127)（±3 容差）。
    // 同时隐式验证：a == 1 走原 FillRect 路径不回归（角落纯蓝）。

    const int screenW = GetSystemMetrics(SM_CXSCREEN);
    const int screenH = GetSystemMetrics(SM_CYSCREEN);
    const wchar_t* kClassName = L"ECDI_TestGDIAlphaRect";
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    if (!RegisterClassW(&wc))
    {
        EXPECT_EQ(GetLastError(), ERROR_CLASS_ALREADY_EXISTS);
    }

    HWND hwnd = CreateWindowExW(0, kClassName, L"ECDI_AlphaRectTest", WS_POPUP,
                                screenW - 210, screenH - 210, 200, 200,
                                nullptr, nullptr, wc.hInstance, nullptr);
    EXPECT_TRUE(hwnd != nullptr);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);

    {
        GDIBackend backend;
        backend.Initialize(Win32RenderContext(hwnd));
        backend.BeginFrame(Color::White());
        backend.DrawRect(Rect{ 0, 0, 200, 200 }, Color::Blue());
        // 50% 半透明红（Color.a = 0.5——未预乘输入，GDIBackend 内部预乘）
        backend.DrawRect(Rect{ 50, 50, 100, 100 }, Color::FromRGBA8(255, 0, 0, 128));
        backend.EndFrame();

        HDC dc = GetDC(hwnd);
        const COLORREF corner = GetPixel(dc, 5, 5);        // 角落：纯蓝底（a==1 快速路径）
        const COLORREF center = GetPixel(dc, 100, 100);    // 中央：混合结果
        ReleaseDC(hwnd, dc);

        // out = src + dst*(1-A/255)：R = 128 + 0*0.5 = 128；B = 255*(127/255) ≈ 127
        EXPECT_TRUE(corner != CLR_INVALID);
        EXPECT_TRUE(center != CLR_INVALID);
        EXPECT_TRUE(GetRValue(corner) <= 3 && GetGValue(corner) <= 3 && GetBValue(corner) >= 252);
        EXPECT_TRUE(std::abs(GetRValue(center) - 128) <= 3);
        EXPECT_TRUE(GetGValue(center) <= 3);
        EXPECT_TRUE(std::abs(GetBValue(center) - 127) <= 3);
    }

    DestroyWindow(hwnd);
}

// ══ Phase 20.1：渲染层 DPI 缩放（T20.1-1..T20.1-5）═══════════════════════════════
// ★ 落点 = Renderer 入口；装置 = 既有 RecordingBackend（它记录的**正是后端收到的几何**）
//   ⇒ 本组**零窗口、零 GDI**。★ 断言口径由 Phase 20.1 详设 §6.2 冻结。

void TestRendererScaleIdentity()
{
    // T20.1-1：scale == 1.0f ⇒ 折算**恒等**（G2 的纯函数锚 / 契约 C-2）
    // ★ 一次走**默认实参**、一次走**显式 1.0f** ⇒ 同时验 C-9（默认值存在）与恒等
    RecordingBackend backend;
    Renderer renderer(backend);

    CommandBuffer commands;
    commands.emplace_back(DrawRectCommand{ Rect{ 1, 2, 3, 4 }, Color::Red() });

    renderer.Execute(commands);          // 默认实参（= 1.0f）
    renderer.Execute(commands, 1.0f);    // 显式实参

    EXPECT_EQ(backend.draws.size(), 2);
    // 两次结果一致，且与输入同值（折算恒等）
    EXPECT_NEAR(backend.draws[0].rect.x, 1.0f, kEpsilon);
    EXPECT_NEAR(backend.draws[0].rect.y, 2.0f, kEpsilon);
    EXPECT_NEAR(backend.draws[0].rect.width, 3.0f, kEpsilon);
    EXPECT_NEAR(backend.draws[0].rect.height, 4.0f, kEpsilon);
    EXPECT_NEAR(backend.draws[1].rect.x, backend.draws[0].rect.x, kEpsilon);
    EXPECT_NEAR(backend.draws[1].rect.y, backend.draws[0].rect.y, kEpsilon);
    EXPECT_NEAR(backend.draws[1].rect.width, backend.draws[0].rect.width, kEpsilon);
    EXPECT_NEAR(backend.draws[1].rect.height, backend.draws[0].rect.height, kEpsilon);
}

void TestRendererScaleRectAndClip()
{
    // T20.1-2：Rect 与 Clip 都折算，且**走同一个 helper**（A1 / A4）
    RecordingBackend backend;
    Renderer renderer(backend);

    CommandBuffer commands;
    commands.emplace_back(DrawRectCommand{ Rect{ 1, 2, 3, 4 }, Color::Red() });
    commands.emplace_back(PushClipCommand{ Rect{ 5, 6, 7, 8 } });

    renderer.Execute(commands, 2.0f);

    EXPECT_EQ(backend.draws.size(), 1);
    EXPECT_NEAR(backend.draws[0].rect.x, 2.0f, kEpsilon);
    EXPECT_NEAR(backend.draws[0].rect.y, 4.0f, kEpsilon);
    EXPECT_NEAR(backend.draws[0].rect.width, 6.0f, kEpsilon);
    EXPECT_NEAR(backend.draws[0].rect.height, 8.0f, kEpsilon);

    EXPECT_EQ(backend.clipOps.size(), 1);
    EXPECT_TRUE(backend.clipOps[0].isPush);
    EXPECT_NEAR(backend.clipOps[0].rect.x, 10.0f, kEpsilon);
    EXPECT_NEAR(backend.clipOps[0].rect.y, 12.0f, kEpsilon);
    EXPECT_NEAR(backend.clipOps[0].rect.width, 14.0f, kEpsilon);
    EXPECT_NEAR(backend.clipOps[0].rect.height, 16.0f, kEpsilon);
}

void TestRendererScaleAllGeometryFields()
{
    // T20.1-3：覆盖**全部几何字段**（Rect / Point / width / cornerRadius）——A7
    // ★ scale 取 1.5f 且输入**刻意取奇数** ⇒ 期望值落在**半整数**（1.5 / 4.5）
    //   ⇒ 若实现偷偷 lround，本用例**必失败**（同时也是契约 C-8「不得取整」的回归锚）
    // ★ 另验：DrawImage 的**源 image payload 不被 DPI 折算改动**（评审 §10 修正后的准确目的）
    RecordingBackend backend;
    Renderer renderer(backend);

    Image img;                              // 与 TestImageValueSemantic 同款的非对称像素
    img.width = 2;
    img.height = 1;
    img.stride = 8;
    img.pixels = { 0, 0, 255, 255, 0, 255, 0, 255 };

    CommandBuffer commands;
    commands.emplace_back(DrawLineCommand{ Point{ 1, 2 }, Point{ 3, 4 }, 2.0f, Color::Black() });
    commands.emplace_back(DrawRoundedRectCommand{ Rect{ 2, 4, 6, 8 }, 4.0f, Color::Gray() });
    commands.emplace_back(DrawImageCommand{ Rect{ 8, 16, 32, 48 }, img });
    commands.emplace_back(DrawFocusRectCommand{ Rect{ 1, 1, 2, 2 }, 3.0f, Color::Blue() });

    renderer.Execute(commands, 1.5f);

    // DrawLine：start / end / **width** 三个几何量都要折
    EXPECT_EQ(backend.lineCalls.size(), 1);
    EXPECT_NEAR(backend.lineCalls[0].start.x, 1.5f, kEpsilon);
    EXPECT_NEAR(backend.lineCalls[0].start.y, 3.0f, kEpsilon);
    EXPECT_NEAR(backend.lineCalls[0].end.x, 4.5f, kEpsilon);
    EXPECT_NEAR(backend.lineCalls[0].end.y, 6.0f, kEpsilon);
    EXPECT_NEAR(backend.lineCalls[0].width, 3.0f, kEpsilon);            // ★ 最易漏的一个

    // DrawRoundedRect：rect / **cornerRadius**
    EXPECT_EQ(backend.roundedRectCalls.size(), 1);
    EXPECT_NEAR(backend.roundedRectCalls[0].rect.x, 3.0f, kEpsilon);
    EXPECT_NEAR(backend.roundedRectCalls[0].rect.y, 6.0f, kEpsilon);
    EXPECT_NEAR(backend.roundedRectCalls[0].rect.width, 9.0f, kEpsilon);
    EXPECT_NEAR(backend.roundedRectCalls[0].rect.height, 12.0f, kEpsilon);
    EXPECT_NEAR(backend.roundedRectCalls[0].cornerRadius, 6.0f, kEpsilon);   // ★ 最易漏

    // DrawImage：**只折 dest**；★ 源 payload 逐字节不变
    EXPECT_EQ(backend.imageCalls.size(), 1);
    EXPECT_NEAR(backend.imageCalls[0].dest.x, 12.0f, kEpsilon);
    EXPECT_NEAR(backend.imageCalls[0].dest.y, 24.0f, kEpsilon);
    EXPECT_NEAR(backend.imageCalls[0].dest.width, 48.0f, kEpsilon);
    EXPECT_NEAR(backend.imageCalls[0].dest.height, 72.0f, kEpsilon);
    EXPECT_EQ(backend.imageCalls[0].image.width, 2);
    EXPECT_EQ(backend.imageCalls[0].image.height, 1);
    EXPECT_EQ(backend.imageCalls[0].image.stride, 8);
    EXPECT_TRUE(backend.imageCalls[0].image.pixels == img.pixels);   // 内容/尺寸/stride 均未被改动

    // DrawFocusRect：rect / cornerRadius
    EXPECT_EQ(backend.focusRectCalls.size(), 1);
    EXPECT_NEAR(backend.focusRectCalls[0].rect.x, 1.5f, kEpsilon);
    EXPECT_NEAR(backend.focusRectCalls[0].rect.y, 1.5f, kEpsilon);
    EXPECT_NEAR(backend.focusRectCalls[0].rect.width, 3.0f, kEpsilon);
    EXPECT_NEAR(backend.focusRectCalls[0].rect.height, 3.0f, kEpsilon);
    EXPECT_NEAR(backend.focusRectCalls[0].cornerRadius, 4.5f, kEpsilon);
}

void TestRendererScaleTextPositionOnly()
{
    // T20.1-4：★ **只折 pos，字号不折**（Q5 / G3）——本子阶段最危险的回归点
    // 若此处也乘 scale ⇒ 与后端按 DPI 的换算**叠加** = 双重缩放（1.5 × 1.5 = 2.25）
    RecordingBackend backend;
    Renderer renderer(backend);

    Font font;
    font.size = 14.0f;

    CommandBuffer commands;
    commands.emplace_back(DrawTextCommand{ Point{ 5, 7 }, "AB", Color::Black(), font });

    renderer.Execute(commands, 2.0f);

    EXPECT_EQ(backend.textDraws.size(), 1);
    EXPECT_NEAR(backend.textDraws[0].pos.x, 10.0f, kEpsilon);
    EXPECT_NEAR(backend.textDraws[0].pos.y, 14.0f, kEpsilon);
    EXPECT_NEAR(backend.textDraws[0].font.size, 14.0f, kEpsilon);   // ★ **未变**（不双重缩放）
    EXPECT_EQ(backend.textDraws[0].text, "AB");                   // 文本亦未变
}

void TestRendererScaleLeavesBufferIntact()
{
    // T20.1-5：★★ A2 / A6 / C-10 的**唯一机器证据**——折算发生了，但**命令缓冲一字不动**
    // ① 数量 / 类型 / 顺序不变（A6）  ② 源缓冲几何逐位不变（C-10）  ③ 后端收到的是物理（C-7）
    RecordingBackend backend;
    Renderer renderer(backend);

    CommandBuffer commands;
    commands.emplace_back(PushClipCommand{ Rect{ 0, 0, 100, 100 } });
    commands.emplace_back(DrawRectCommand{ Rect{ 10, 20, 30, 40 }, Color::Red() });
    commands.emplace_back(PopClipCommand{});

    renderer.Execute(commands, 2.0f);

    // ① 数量 / 类型 / 顺序不变（A6）
    EXPECT_EQ(commands.size(), 3);
    EXPECT_TRUE(std::holds_alternative<PushClipCommand>(commands[0]));
    EXPECT_TRUE(std::holds_alternative<DrawRectCommand>(commands[1]));
    EXPECT_TRUE(std::holds_alternative<PopClipCommand>(commands[2]));

    // ② ★★ 源缓冲**逐位不变**（A2 / C-10）
    const auto& clip = std::get<PushClipCommand>(commands[0]);
    EXPECT_NEAR(clip.rect.x, 0.0f, kEpsilon);
    EXPECT_NEAR(clip.rect.y, 0.0f, kEpsilon);
    EXPECT_NEAR(clip.rect.width, 100.0f, kEpsilon);
    EXPECT_NEAR(clip.rect.height, 100.0f, kEpsilon);
    const auto& rectCmd = std::get<DrawRectCommand>(commands[1]);
    EXPECT_NEAR(rectCmd.rect.x, 10.0f, kEpsilon);
    EXPECT_NEAR(rectCmd.rect.y, 20.0f, kEpsilon);
    EXPECT_NEAR(rectCmd.rect.width, 30.0f, kEpsilon);
    EXPECT_NEAR(rectCmd.rect.height, 40.0f, kEpsilon);

    // ③ 后端收到的**已是物理**——与 ② 并列 ⇒ 证明「折算了、但没改缓冲」
    EXPECT_EQ(backend.draws.size(), 1);
    EXPECT_NEAR(backend.draws[0].rect.x, 20.0f, kEpsilon);
    EXPECT_NEAR(backend.draws[0].rect.y, 40.0f, kEpsilon);
    EXPECT_NEAR(backend.draws[0].rect.width, 60.0f, kEpsilon);
    EXPECT_NEAR(backend.draws[0].rect.height, 80.0f, kEpsilon);
    EXPECT_EQ(backend.clipOps.size(), 2);
    EXPECT_TRUE(backend.clipOps[0].isPush);
    EXPECT_NEAR(backend.clipOps[0].rect.width, 200.0f, kEpsilon);
    EXPECT_FALSE(backend.clipOps[1].isPush);
}

} // anonymous namespace

void ECDI::Test::RegisterRendererTests()
{
    GetTestRegistry().Add("Renderer.Forwarding", &TestRendererForwarding);
    GetTestRegistry().Add("Renderer.UTF8Utility", &TestUTF8Utility);
    GetTestRegistry().Add("Renderer.NewCommands", &TestRendererNewCommands);
    GetTestRegistry().Add("Renderer.PaintContextNewCommands", &TestPaintContextNewCommands);
    GetTestRegistry().Add("Renderer.ImageValueSemantic", &TestImageValueSemantic);
    GetTestRegistry().Add("Renderer.GDIBackendAlphaBlend", &TestGDIBackendAlphaBlend);
    GetTestRegistry().Add("Renderer.DrawRectAlpha", &TestDrawRectAlpha);
    GetTestRegistry().Add("Renderer.ScaleIdentity", &TestRendererScaleIdentity);
    GetTestRegistry().Add("Renderer.ScaleRectAndClip", &TestRendererScaleRectAndClip);
    GetTestRegistry().Add("Renderer.ScaleAllGeometryFields", &TestRendererScaleAllGeometryFields);
    GetTestRegistry().Add("Renderer.ScaleTextPositionOnly", &TestRendererScaleTextPositionOnly);
    GetTestRegistry().Add("Renderer.ScaleLeavesBufferIntact", &TestRendererScaleLeavesBufferIntact);
}