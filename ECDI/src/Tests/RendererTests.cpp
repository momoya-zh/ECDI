#include "RunAllTests.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 防御性 undef（规范 10）：本文件含 Windows.h——防 DrawTextW 宏污染 ECDI 头声明
#endif

#include "ECDI/Core/ECDIAssert.h"
#include "ECDI/Render/GDIBackend.h"
#include "ECDI/Render/Renderer.h"
#include "ECDI/Render/RecordingBackend.h"
#include "ECDI/Render/PaintContext.h"
#include "ECDI/Platform/Win32/Win32RenderContext.h"
#include "ECDI/Core/Point.h"
#include "ECDI/Core/Color.h"
#include "ECDI/Core/Font.h"
#include "ECDI/Core/Image.h"
#include "ECDI/Core/UTF8.h"

#include <cmath>    // std::abs（浮点 epsilon 比较）
#include <utility>

using namespace ECDI;

// 浮点比较辅助（GPT 建议：避免浮点 == 直接比较）
constexpr float kEpsilon = 0.001f;
inline bool FloatEq(float a, float b) { return std::abs(a - b) < kEpsilon; }

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

    FRAMEWORK_ASSERT(backend.draws.size() == 2);
    FRAMEWORK_ASSERT(FloatEq(backend.draws[0].rect.x, 0.0f) && FloatEq(backend.draws[0].rect.width, 100.0f));
    FRAMEWORK_ASSERT(FloatEq(backend.draws[0].color.r, 1.0f) && FloatEq(backend.draws[0].color.a, 1.0f));
    FRAMEWORK_ASSERT(FloatEq(backend.draws[1].rect.x, 10.0f) && FloatEq(backend.draws[1].rect.y, 20.0f));
    FRAMEWORK_ASSERT(FloatEq(backend.draws[1].color.g, 0.5f));
}

void TestUTF8Utility()
{
    // ── 5.5.1.1 原 #6：UTF-8 工具自测 ──
    FRAMEWORK_ASSERT(EncodeUTF8(U'A') == "A");
    FRAMEWORK_ASSERT(EncodeUTF8(U'中') == "\xE4\xB8\xAD");
    FRAMEWORK_ASSERT(EncodeUTF8(U'😀') == "\xF0\x9F\x98\x80");

    const std::string s = "a中😀";
    FRAMEWORK_ASSERT(CodepointIndexToByteOffset(s, 0) == 0);
    FRAMEWORK_ASSERT(CodepointIndexToByteOffset(s, 1) == 1);
    FRAMEWORK_ASSERT(CodepointIndexToByteOffset(s, 2) == 4);
    FRAMEWORK_ASSERT(CodepointIndexToByteOffset(s, 3) == 8);
    FRAMEWORK_ASSERT(ByteOffsetToCodepointIndex(s, 4) == 2);
    FRAMEWORK_ASSERT(ByteOffsetToCodepointIndex(s, 8) == 3);
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
    commands.emplace_back(DrawFocusRectCommand{ Rect{ 2, 3, 10, 10 }, Color::Black() });

    renderer.Execute(commands);

    // DrawLine：宽度 float 原样转发（不预取整——取整是 GDI 后端细节）
    FRAMEWORK_ASSERT(backend.lineCalls.size() == 1);
    FRAMEWORK_ASSERT(FloatEq(backend.lineCalls[0].start.x, 1.0f) && FloatEq(backend.lineCalls[0].end.y, 40.0f));
    FRAMEWORK_ASSERT(FloatEq(backend.lineCalls[0].width, 2.5f));
    FRAMEWORK_ASSERT(FloatEq(backend.lineCalls[0].color.b, 1.0f));   // Blue

    // DrawRoundedRect
    FRAMEWORK_ASSERT(backend.roundedRectCalls.size() == 1);
    FRAMEWORK_ASSERT(FloatEq(backend.roundedRectCalls[0].rect.width, 50.0f));
    FRAMEWORK_ASSERT(FloatEq(backend.roundedRectCalls[0].cornerRadius, 4.0f));
    FRAMEWORK_ASSERT(FloatEq(backend.roundedRectCalls[0].color.g, 1.0f));   // Green

    // DrawImage（Image 值拷贝转发）
    FRAMEWORK_ASSERT(backend.imageCalls.size() == 1);
    FRAMEWORK_ASSERT(FloatEq(backend.imageCalls[0].dest.width, 20.0f));
    FRAMEWORK_ASSERT(backend.imageCalls[0].image.width == 2 && backend.imageCalls[0].image.height == 1);

    // Push/Pop 共列保序：Push(100x100) → (DrawRect) → Pop
    FRAMEWORK_ASSERT(backend.clipOps.size() == 2);
    FRAMEWORK_ASSERT(backend.clipOps[0].isPush && FloatEq(backend.clipOps[0].rect.width, 100.0f));
    FRAMEWORK_ASSERT(!backend.clipOps[1].isPush);
    // 裁剪内 DrawRect 照常转发（状态命令不影响其他转发）
    FRAMEWORK_ASSERT(backend.draws.size() == 1);

    // DrawFocusRect
    FRAMEWORK_ASSERT(backend.focusRectCalls.size() == 1);
    FRAMEWORK_ASSERT(FloatEq(backend.focusRectCalls[0].rect.x, 2.0f));
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
    pc.DrawFocusRect(Rect{ 5, 5, 12, 12 }, Color::Blue());
    pc.DrawImage(Rect{ 0, 0, 8, 8 }, Image{});   // 空图像也产生命令（no-op 判定在后端）

    // 顺序断言：Push → DrawLine → DrawRoundedRect → Pop → DrawFocusRect → DrawImage
    FRAMEWORK_ASSERT(commands.size() == 6);
    FRAMEWORK_ASSERT(std::holds_alternative<PushClipCommand>(commands[0]));
    FRAMEWORK_ASSERT(std::holds_alternative<DrawLineCommand>(commands[1]));
    FRAMEWORK_ASSERT(std::holds_alternative<DrawRoundedRectCommand>(commands[2]));
    FRAMEWORK_ASSERT(std::holds_alternative<PopClipCommand>(commands[3]));
    FRAMEWORK_ASSERT(std::holds_alternative<DrawFocusRectCommand>(commands[4]));
    FRAMEWORK_ASSERT(std::holds_alternative<DrawImageCommand>(commands[5]));

    // 字段抽查：DrawLine 宽度原样进命令
    const auto& line = std::get<DrawLineCommand>(commands[1]);
    FRAMEWORK_ASSERT(FloatEq(line.width, 1.0f) && FloatEq(line.start.x, 0.0f));
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
    FRAMEWORK_ASSERT(cmd.image.pixels[0] == 0 && cmd.image.pixels[2] == 255);   // 像素 0 红未变
    FRAMEWORK_ASSERT(cmd.image.pixels[4] == 0 && cmd.image.pixels[5] == 255);   // 像素 1 绿未变
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
        FRAMEWORK_ASSERT(GetLastError() == ERROR_CLASS_ALREADY_EXISTS);   // 重复运行忽略
    }

    HWND hwnd = CreateWindowExW(0, kClassName, L"ECDI_PixelTest", WS_POPUP,
                                screenW - 210, screenH - 210, 200, 200,
                                nullptr, nullptr, wc.hInstance, nullptr);
    FRAMEWORK_ASSERT(hwnd != nullptr);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);

    {
        // 2. 真实帧：纯蓝底 + 中央 100x100 半透明红
        GDIBackend backend;
        backend.Initialize(Win32RenderContext(hwnd));
        backend.BeginFrame();
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
        FRAMEWORK_ASSERT(corner != CLR_INVALID && center != CLR_INVALID);
        FRAMEWORK_ASSERT(GetRValue(corner) <= 3 && GetGValue(corner) <= 3 && GetBValue(corner) >= 252);
        FRAMEWORK_ASSERT(std::abs(GetRValue(center) - 128) <= 3);
        FRAMEWORK_ASSERT(GetGValue(center) <= 3);
        FRAMEWORK_ASSERT(std::abs(GetBValue(center) - 127) <= 3);
    }

    DestroyWindow(hwnd);
}

} // anonymous namespace

void ECDI::Test::RunRendererTests()
{
    TestRendererForwarding();
    TestUTF8Utility();
    TestRendererNewCommands();
    TestPaintContextNewCommands();
    TestImageValueSemantic();
    TestGDIBackendAlphaBlend();
}