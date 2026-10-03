#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 防御性 undef（规范 10）：本文件含 Windows.h——防宏污染 ECDI 头声明
#endif

#include "ECDI/Core/Point.h"
#include "Platform/Win32/Win32RenderContext.h"
#include "Render/FontEngine.h"
#include "Render/GLGlyphAtlas.h"
#include "Render/GLRenderer.h"

#include <cstddef>
#include <memory>
#include <string>

using namespace ECDI;

// ══════════════════════════════════════════════════════════════════════
// Phase 26 批三：GL 后端（T26-7 图集 shelf 分配 · T26-8 初始化失败路径）
// ══════════════════════════════════════════════════════════════════════
// ★★ **本组不需要 GL 环境**：
//   - T26-7 只测 `GLGlyphAtlas::Allocate`——**纯逻辑、不碰 GL**（详设 §3-④ 使然）；
//   - T26-8 走 **初始化失败路径**（无效 HWND ⇒ 早退），失败后全部操作 no-op ⇒ 也不碰 GL。
//   真实的成功渲染路径 = 人工用例 **T26-9**（`ModelProbe --gl` 目视，详设 §6）。

namespace {

/// @brief 无效句柄上下文（T26-8 的输入）——`nullptr` 使 `Initialize` 走**确定性早退**
Win32RenderContext InvalidContext()
{
    return Win32RenderContext(static_cast<HWND>(nullptr));
}

}   // namespace

// ── T26-7：图集 shelf 分配（★ 纯逻辑，沿 spike:763-772 的分配语义）─────────────────
// ★ 判据：① 同行内 x 单调递增；② 行溢出 ⇒ 回卷到下一行（x 归零、y 增大）；
//         ③ 尺寸非法 / 图集满 ⇒ `valid == false`（**不崩**——详设 §9 O3：本 Phase 无淘汰）。
void Test26AtlasShelfAllocation()
{
    // ① 同行单调递增
    {
        GLGlyphAtlas atlas;
        EXPECT_TRUE(!atlas.IsReady());              // 未 Initialize ⇒ 未就绪（且未创建 textures）

        const GlyphSlot a = atlas.Allocate(10, 8);
        const GlyphSlot b = atlas.Allocate(12, 8);
        const GlyphSlot c = atlas.Allocate(8, 8);

        EXPECT_TRUE(a.valid && b.valid && c.valid);
        EXPECT_EQ(a.w, 10);                         // 宽度 == 请求宽（与请求一致）
        EXPECT_EQ(a.h, 8);
        EXPECT_EQ(a.y, b.y);                        // 同高 ⇒ 同一 shelf 行
        EXPECT_EQ(b.y, c.y);
        EXPECT_TRUE(b.x > a.x);                     // ★ 单调递增（含 1px 间隙）
        EXPECT_TRUE(c.x > b.x);
        EXPECT_TRUE(atlas.UsedPixels() > static_cast<std::size_t>(0));
    }

    // ② 尺寸非法 ⇒ 分配失败（含**越界**——宽度大于图集自身）
    {
        GLGlyphAtlas atlas;
        EXPECT_TRUE(!atlas.Allocate(0, 8).valid);
        EXPECT_TRUE(!atlas.Allocate(8, 0).valid);
        EXPECT_TRUE(!atlas.Allocate(-1, 8).valid);
        EXPECT_TRUE(!atlas.Allocate(8, -1).valid);
        EXPECT_TRUE(!atlas.Allocate(GLGlyphAtlas::kSize + 1, 8).valid);
        EXPECT_TRUE(!atlas.Allocate(8, GLGlyphAtlas::kSize + 1).valid);
    }

    // ③ 行溢出 ⇒ 回卷到下一行
    {
        GLGlyphAtlas atlas;
        const GlyphSlot wide = atlas.Allocate(GLGlyphAtlas::kSize, 4);   // 占满整行
        EXPECT_TRUE(wide.valid);
        EXPECT_EQ(wide.x, 0);
        EXPECT_EQ(wide.y, 0);

        const GlyphSlot next = atlas.Allocate(4, 4);
        EXPECT_TRUE(next.valid);
        EXPECT_EQ(next.x, 0);                       // ★ 回卷到行首
        EXPECT_TRUE(next.y > wide.y);               // ★ 落到下一行
    }

    // ④ 图集满 ⇒ `valid == false`（不崩、不越界）
    {
        GLGlyphAtlas atlas;
        bool sawFull = false;
        for (int i = 0; i < 400; ++i)
        {
            const GlyphSlot slot = atlas.Allocate(GLGlyphAtlas::kSize, 16);
            if (!slot.valid) { sawFull = true; break; }
        }
        EXPECT_TRUE(sawFull);                       // 1024 / (16 + 1) ≈ 60 行 ⇒ 必满
    }
}

// ── T26-8：`GLRenderer` 初始化失败 ⇒ 全部 no-op（不崩、不静默回退）─────────────────
// ★ 判据：无效 HWND ⇒ `Initialize` 早退 ⇒ `IsReady() == false`；其后**全部十个操作**可安全
//   调用（no-op）。★ 详设 D7：失败**不得静默回退 GDI**——由 `IsReady()` 暴露给调用方决定。
void Test26GLRendererInitFailureIsNoOp()
{
    const auto engine = std::make_shared<FontEngine>();   // 共享引擎（文本栈同源）
    GLRenderer renderer(engine);

    EXPECT_TRUE(!renderer.IsReady());                     // 构造后未初始化
    renderer.Initialize(InvalidContext());
    EXPECT_TRUE(!renderer.IsReady());                     // ★ 无效句柄 ⇒ 仍不就绪

    const Color white = Color::White();
    const Font font{ 14.0f, "" };

    // ★ 全部十个操作都必须**安全**（no-op）——本用例「不崩」即通过
    renderer.BeginFrame(white);
    renderer.DrawRect(Rect{ 0.0f, 0.0f, 10.0f, 10.0f }, white);
    renderer.DrawText(Point{ 0.0f, 12.0f }, "x", white, font);
    renderer.DrawLine(Point{ 0.0f, 0.0f }, Point{ 10.0f, 10.0f }, 1.0f, white);
    renderer.DrawRoundedRect(Rect{ 0.0f, 0.0f, 10.0f, 10.0f }, 3.0f, white);
    renderer.DrawImage(Rect{ 0.0f, 0.0f, 10.0f, 10.0f }, Image{});
    renderer.PushClip(Rect{ 0.0f, 0.0f, 5.0f, 5.0f });
    renderer.PopClip();
    renderer.DrawFocusRect(Rect{ 0.0f, 0.0f, 10.0f, 10.0f }, 0.0f, white);
    renderer.EndFrame();

    EXPECT_EQ(renderer.AtlasMissCount(), static_cast<std::size_t>(0));   // 未初始化 ⇒ 零 miss
}

void ECDI::Test::RegisterGLBackendTests()
{
    GetTestRegistry().Add("GLBackend.AtlasShelfAllocation", &Test26AtlasShelfAllocation);      // T26-7
    GetTestRegistry().Add("GLBackend.InitFailureIsNoOp",    &Test26GLRendererInitFailureIsNoOp); // T26-8
}
