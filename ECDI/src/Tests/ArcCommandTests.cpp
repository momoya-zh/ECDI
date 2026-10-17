#include "RunAllTests.h"
#include "TestFramework.h"

#include "ECDI/Render/PaintContext.h"
#include "ECDI/Render/RenderCommand.h"
#include "ECDI/Render/Renderer.h"
#include "Render/RecordingBackend.h"

#include <cstddef>
#include <string>
#include <variant>
#include <vector>

using namespace ECDI;

// ══════════════════════════════════════════════════════════════════════
// Phase 33 批二：命令层贯通（△4/△5/△6/△9/△10）
// ══════════════════════════════════════════════════════════════════════
// ★ 本组证的是**通路**，不是几何——几何在批一（`ArcCoverageTests.cpp`）。
//   三段：PaintContext 原样入命令 → Renderer 折叠规则（★ 两个角度**不折**）
//   → RecordingBackend 原样记录（含**未钳制**的 `sweep = 450`）。

namespace {

/// @brief 命令缓冲里第 `idx` 条是否是 `DrawArcCommand`（并把两个角度取回）
bool ArcAt(const CommandBuffer& cmds, std::size_t idx, DrawArcCommand& out)
{
    if (idx >= cmds.size())
    {
        return false;
    }
    const auto* p = std::get_if<DrawArcCommand>(&cmds[idx]);
    if (p == nullptr)
    {
        return false;
    }
    out = *p;
    return true;
}

} // namespace

// ── T33-10：`PaintContext::DrawArc` 原样进命令（6 字段逐字段相等）──────────────
void Test33_10CommandPassthrough()
{
    CommandBuffer cmds;
    RecordingBackend measurerBackend;   // 仅用作 TextMeasurer（本用例不测量）
    PaintContext ctx(cmds, measurerBackend);

    const Point center{ 12.5f, 34.25f };
    const float radius = 7.5f, start = 37.5f, sweep = -123.25f, width = 3.0f;
    const Color color = Color::Red();

    ctx.DrawArc(center, radius, start, sweep, width, color);

    EXPECT_EQ(cmds.size(), std::size_t(1));
    DrawArcCommand got{};
    EXPECT_TRUE(ArcAt(cmds, 0, got));
    EXPECT_NEAR(got.center.x, center.x, 1e-6);
    EXPECT_NEAR(got.center.y, center.y, 1e-6);
    EXPECT_NEAR(got.radius, radius, 1e-6);
    EXPECT_NEAR(got.startAngleDeg, start, 1e-6);
    EXPECT_NEAR(got.sweepAngleDeg, sweep, 1e-6);
    EXPECT_NEAR(got.width, width, 1e-6);

    // ★ 命令顺序 = 绘制顺序（与既有 8 个命令同族）
    ctx.DrawRect(Rect{ 0.0f, 0.0f, 4.0f, 4.0f }, Color::Blue());
    ctx.DrawArc(center, radius, 0.0f, 90.0f, 1.0f, color);
    EXPECT_EQ(cmds.size(), std::size_t(3));
    EXPECT_TRUE(std::holds_alternative<DrawRectCommand>(cmds[1]));
    DrawArcCommand second{};
    EXPECT_TRUE(ArcAt(cmds, 2, second));

    // ★ 契约层**不做**角度归一化：`sweep = 450` 原样入命令（钳制在执行端——C33-9）
    CommandBuffer raw;
    PaintContext ctx2(raw, measurerBackend);
    ctx2.DrawArc(center, radius, -30.0f, 450.0f, width, color);
    DrawArcCommand got2{};
    EXPECT_TRUE(ArcAt(raw, 0, got2));
    EXPECT_NEAR(got2.startAngleDeg, -30.0, 1e-6);
    EXPECT_NEAR(got2.sweepAngleDeg, 450.0, 1e-6);
}

// ── T33-11：`RecordingBackend` 记录**原值**（C33-9 的可机检证据）────────────────
void Test33_11RecordingBackendRawParams()
{
    RecordingBackend backend;
    backend.BeginFrame(Color::White());

    const Point center{ 5.0f, 6.0f };
    // ★★ 若替身在记录时调了 `NormalizeArc`，这里的 450 会被记成 360 ⇒ 本断言必红
    backend.DrawArc(center, 10.0f, -30.0f, 450.0f, 4.0f, Color::Green());
    backend.DrawArc(center, 10.0f, 0.0f, 90.0f, 2.0f, Color::Red());
    backend.EndFrame();

    EXPECT_EQ(backend.arcDraws.size(), std::size_t(2));
    EXPECT_NEAR(backend.arcDraws[0].sweepAngleDeg, 450.0, 1e-6);   // ★ 原值
    EXPECT_NEAR(backend.arcDraws[0].startAngleDeg, -30.0, 1e-6);   // ★ 原值
    EXPECT_NEAR(backend.arcDraws[0].radius, 10.0, 1e-6);
    EXPECT_NEAR(backend.arcDraws[0].width, 4.0, 1e-6);
    EXPECT_NEAR(backend.arcDraws[0].center.x, 5.0, 1e-6);
    EXPECT_NEAR(backend.arcDraws[1].sweepAngleDeg, 90.0, 1e-6);

    // ★ 不串味：弧记录不落进任何既有列（各列各司其职）
    EXPECT_EQ(backend.lineCalls.size(), std::size_t(0));
    EXPECT_EQ(backend.draws.size(), std::size_t(0));
    EXPECT_EQ(backend.roundedRectCalls.size(), std::size_t(0));
}

// ── T33-12：`Renderer` 的折叠规则——★★ 两个角度**逐位未变**（盯防①）──────────
void Test33_12RendererArcScaleFolding()
{
    RecordingBackend backend;
    Renderer renderer(backend);

    CommandBuffer cmds;
    RecordingBackend measurerBackend;
    PaintContext ctx(cmds, measurerBackend);

    const float start = 37.5f, sweep = -123.25f;
    ctx.DrawArc(Point{ 10.0f, 20.0f }, 6.0f, start, sweep, 3.0f, Color::Red());

    const float scale = 1.5f;
    renderer.Execute(cmds, scale);

    EXPECT_EQ(backend.arcDraws.size(), std::size_t(1));
    const RecordingBackend::ArcDraw& r = backend.arcDraws[0];

    // ① 三个**有量纲**的量：已折
    EXPECT_NEAR(r.center.x, 15.0, 1e-5);
    EXPECT_NEAR(r.center.y, 30.0, 1e-5);
    EXPECT_NEAR(r.radius, 9.0, 1e-5);
    EXPECT_NEAR(r.width, 4.5, 1e-5);

    // ② ★★ 两个**角度**：逐位未变（角度无量纲——折了即错）
    EXPECT_TRUE(r.startAngleDeg == start);
    EXPECT_TRUE(r.sweepAngleDeg == sweep);

    // ③ 反例锚：若有人把角度也折了，1.5× 下会是 56.25 / -184.875 ⇒ 上面两条必红
    EXPECT_TRUE(r.startAngleDeg != start * scale);
    EXPECT_TRUE(r.sweepAngleDeg != sweep * scale);

    // ④ scale = 1.0（默认）⇒ 恒等，逐位等于入参
    RecordingBackend b2;
    Renderer r2(b2);
    r2.Execute(cmds);
    EXPECT_EQ(b2.arcDraws.size(), std::size_t(1));
    EXPECT_TRUE(b2.arcDraws[0].center.x == 10.0f);
    EXPECT_TRUE(b2.arcDraws[0].startAngleDeg == start);
}

// （T33-13 `Theme.RingProgressBarStyleContract` 属批三——`RingProgressBarStyle` 尚未落盘）

namespace ECDI::Test {

void RegisterArcCommandTests()
{
    // Phase 33 批二：命令层贯通（T33-10..T33-12；全部无头、零窗口）
    GetTestRegistry().Add("ArcPaint.CommandPassthrough",   &Test33_10CommandPassthrough);
    GetTestRegistry().Add("RecordingBackend.ArcRawParams", &Test33_11RecordingBackendRawParams);
    GetTestRegistry().Add("Renderer.ArcScaleFolding",      &Test33_12RendererArcScaleFolding);
}

} // namespace ECDI::Test
