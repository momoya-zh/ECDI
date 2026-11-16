#include "RunAllTests.h"
#include "TestFramework.h"

#include <Windows.h>
#ifdef DrawText
#undef DrawText   // 防御性 undef（规范 10）：本文件含 Windows.h——防宏污染 ECDI 头声明
#endif

#include "ECDI/Core/Color.h"
#include "ECDI/Core/Image.h"
#include "ECDI/Core/Logger.h"
#include "ECDI/Core/Point.h"
#include "Platform/Win32/Win32FontSource.h"
#include "Platform/Win32/Win32RenderContext.h"
#include "Render/FontEngine.h"
#include "Render/GLGlyphAtlas.h"
#include "Render/GLRenderer.h"
#include "Tests/TestWindowHelpers.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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

// D2：空/负尺寸矩形在 DrawRoundedRect 中安全处理（对齐 GDI，防 clamp lo > hi UB）
void Test26GLRendererNegativeDimensionsAndDegenerate()
{
    const auto engine = std::make_shared<FontEngine>();
    GLRenderer renderer(engine);
    const Color white = Color::White();
    renderer.BeginFrame(white);
    // 负宽高与零宽高不崩、不触发 clamp 前置条件违反 UB
    renderer.DrawRoundedRect(Rect{ 10.0f, 10.0f, -5.0f, 20.0f }, 3.0f, white);
    renderer.DrawRoundedRect(Rect{ 10.0f, 10.0f, 20.0f, -5.0f }, 3.0f, white);
    renderer.DrawRoundedRect(Rect{ 10.0f, 10.0f, 0.0f, 20.0f }, 3.0f, white);
    renderer.DrawRoundedRect(Rect{ 10.0f, 10.0f, 20.0f, 0.0f }, 3.0f, white);
    renderer.DrawRoundedRect(Rect{ 10.0f, 10.0f, -10.0f, -10.0f }, 3.0f, white);
    renderer.EndFrame();
}

// ── Phase 47 Batch 0：GLGlyphAtlas 多页扩容、跨帧 Page-LRU 淘汰与极端帧内重置 ──

void Test47AtlasMultiPageExpansionAndWarmHit()
{
    FontEngine engine;
    engine.SetDpi(96);
    engine.SetFontSource(std::make_unique<Win32FontSource>());

    GLGlyphAtlas atlas(GLGlyphAtlas::kDefaultMaxPages);
    const Font fonts[2] = {
        { 22.0f, "Microsoft YaHei", 400, false },
        { 22.0f, "Microsoft YaHei", 700, true },
    };

    std::vector<GlyphKey> keys;
    keys.reserve(2800);
    for (const Font& f : fonts)
    {
        const FaceId fid = engine.FaceIdFor(f.family, f.weight, f.italic);
        EXPECT_TRUE(fid != 0);
        const int px = engine.PixelSize(f);
        for (int i = 0; i < 1400; ++i)
        {
            const char32_t cp = static_cast<char32_t>(0x4E00 + i);
            const std::uint32_t gi = engine.GlyphIndex(f, cp);
            if (gi != 0)
            {
                keys.push_back(GlyphKey{ fid, gi, px, 0 });
            }
        }
    }

    // Frame 1：灌入超出单页 1024^2 的工作集，验证无淘汰自动扩容至 >= 2 页
    atlas.BeginFrame();
    for (const GlyphKey& k : keys)
    {
        const GlyphSlot slot = atlas.GetOrCreate(k, engine);
        EXPECT_TRUE(slot.valid);
        EXPECT_TRUE(atlas.IsSlotAlive(slot));
    }
    EXPECT_TRUE(atlas.PageCount() >= static_cast<std::size_t>(2));
    EXPECT_EQ(atlas.EvictionCount(), static_cast<std::size_t>(0));
    EXPECT_EQ(atlas.InFrameResetCount(), static_cast<std::size_t>(0));

    // Frame 2：暖渲染复查——MissCount 与 FontEngine::RasterizeCount 增量必须均为 0
    const std::size_t warmMissBefore = atlas.MissCount();
    const std::size_t warmRastBefore = engine.RasterizeCount();
    atlas.BeginFrame();
    for (const GlyphKey& k : keys)
    {
        const GlyphSlot slot = atlas.GetOrCreate(k, engine);
        EXPECT_TRUE(slot.valid);
    }
    EXPECT_EQ(atlas.MissCount(), warmMissBefore);
    EXPECT_EQ(engine.RasterizeCount(), warmRastBefore);
}

void Test47AtlasPageLruEvictionAndEpochSafety()
{
    GLGlyphAtlas atlas(3);
    EXPECT_FALSE(atlas.IsSlotAlive(GlyphSlot{}));   // Empty() 不对称 epoch==0 必不存活

    // SetMaxPagesForTests(< 2) 静默钳位至 2 保 Page 0
    atlas.SetMaxPagesForTests(1);
    atlas.SetMaxPagesForTests(3);

    const GlyphSlot solid = atlas.Allocate(1, 1);
    EXPECT_TRUE(solid.valid);
    EXPECT_EQ(solid.pageIndex, static_cast<std::uint32_t>(0));
    EXPECT_TRUE(atlas.IsSlotAlive(solid));

    // 确定性逐帧填充：先填满 Page 0，再逐帧独占 Page 1 与 Page 2
    GLGlyphAtlas lruAtlas(3);
    const GlyphSlot lruSolid = lruAtlas.Allocate(1, 1);
    EXPECT_TRUE(lruSolid.valid);
    // 占满 Page 0 剩余格（Page 0 上已有 1x1 占位）
    while (true)
    {
        const GlyphSlot probe = lruAtlas.Allocate(255, 255);
        if (!probe.valid)
        {
            break;
        }
    }
    EXPECT_EQ(lruAtlas.PageCount(), static_cast<std::size_t>(1));

    // Frame 2：填入 16 格 255x255 ⇒ 独占 Page 1
    lruAtlas.BeginFrame();
    GlyphSlot p1Sample;
    for (int i = 0; i < 16; ++i)
    {
        const GlyphSlot s = lruAtlas.AllocatePaged(255, 255);
        EXPECT_TRUE(s.valid);
        EXPECT_EQ(s.pageIndex, static_cast<std::uint32_t>(1));
        if (i == 0)
        {
            p1Sample = s;
        }
    }
    EXPECT_EQ(lruAtlas.PageCount(), static_cast<std::size_t>(2));

    // Frame 3：填入 16 格 255x255 ⇒ 独占 Page 2
    lruAtlas.BeginFrame();
    GlyphSlot p2Sample;
    for (int i = 0; i < 16; ++i)
    {
        const GlyphSlot s = lruAtlas.AllocatePaged(255, 255);
        EXPECT_TRUE(s.valid);
        EXPECT_EQ(s.pageIndex, static_cast<std::uint32_t>(2));
        if (i == 0)
        {
            p2Sample = s;
        }
    }
    EXPECT_EQ(lruAtlas.PageCount(), static_cast<std::size_t>(3));
    EXPECT_EQ(lruAtlas.EvictionCount(), static_cast<std::size_t>(0));

    // Frame 4：仅 Touch Page 1 ⇒ Page 2 为最老过期页 ⇒ 触发跨帧 Page-LRU 淘汰 Page 2
    lruAtlas.BeginFrame();
    lruAtlas.TouchSlot(p1Sample);
    const GlyphSlot fresh = lruAtlas.AllocatePaged(255, 255);
    EXPECT_TRUE(fresh.valid);
    EXPECT_EQ(fresh.pageIndex, static_cast<std::uint32_t>(2));
    EXPECT_EQ(lruAtlas.EvictionCount(), static_cast<std::size_t>(1));
    EXPECT_EQ(lruAtlas.InFrameResetCount(), static_cast<std::size_t>(0));
    EXPECT_TRUE(lruAtlas.IsSlotAlive(lruSolid));    // Page 0 保护常驻
    EXPECT_TRUE(lruAtlas.IsSlotAlive(p1Sample));    // Page 1 被 Touch 保活
    EXPECT_FALSE(lruAtlas.IsSlotAlive(p2Sample));   // Page 2 旧代际 slot 失效

    // 再验证 GetOrCreate 跨页淘汰后可恢复重新分配（打破原永久负缓存）
    FontEngine engine;
    engine.SetDpi(96);
    engine.SetFontSource(std::make_unique<Win32FontSource>());
    GLGlyphAtlas glyphLru(3);
    EXPECT_TRUE(glyphLru.Allocate(1, 1).valid);
    while (glyphLru.Allocate(255, 255).valid) {}
    const Font font{ 24.0f, "Microsoft YaHei", 400, false };
    const FaceId fid = engine.FaceIdFor(font.family, font.weight, font.italic);
    const int px = engine.PixelSize(font);
    const std::uint32_t gi = engine.GlyphIndex(font, U'测');
    EXPECT_TRUE(gi != 0);
    const GlyphKey cjkKey{ fid, gi, px, 0 };

    glyphLru.BeginFrame();
    const GlyphSlot g1 = glyphLru.GetOrCreate(cjkKey, engine);
    EXPECT_TRUE(g1.valid && g1.pageIndex == 1);
    while (glyphLru.PageCount() < 3)
    {
        glyphLru.BeginFrame();
        for (int i = 0; i < 16; ++i)
        {
            EXPECT_TRUE(glyphLru.AllocatePaged(255, 255).valid);
        }
    }
    // 再过一帧只 Touch Page 2 不 Touch Page 1，再塞满触发淘汰 Page 1
    glyphLru.BeginFrame();
    for (int i = 0; i < 17; ++i)
    {
        EXPECT_TRUE(glyphLru.AllocatePaged(255, 255).valid);
    }
    EXPECT_TRUE(glyphLru.EvictionCount() >= static_cast<std::size_t>(1));
    EXPECT_FALSE(glyphLru.IsSlotAlive(g1));
    const GlyphSlot g2 = glyphLru.GetOrCreate(cjkKey, engine);
    EXPECT_TRUE(g2.valid);
    EXPECT_TRUE(glyphLru.IsSlotAlive(g2));
}

void Test47AtlasInFrameResetProtection()
{
    GLGlyphAtlas atlas(3);
    const GlyphSlot solid = atlas.Allocate(1, 1);
    EXPECT_TRUE(solid.valid);
    while (atlas.Allocate(255, 255).valid) {}

    // 同一帧内不调 BeginFrame，连续申请 50 个 255x255 大块（超过 2 个数据页共 32 格上限）
    GlyphSlot firstDataSlot;
    for (int i = 0; i < 50; ++i)
    {
        const GlyphSlot s = atlas.AllocatePaged(255, 255);
        EXPECT_TRUE(s.valid);
        EXPECT_TRUE(s.pageIndex == 1 || s.pageIndex == 2);
        if (i == 0)
        {
            firstDataSlot = s;
        }
    }
    EXPECT_TRUE(atlas.InFrameResetCount() >= static_cast<std::size_t>(2));
    EXPECT_EQ(atlas.EvictionCount(), static_cast<std::size_t>(0));
    EXPECT_TRUE(atlas.IsSlotAlive(solid));           // Page 0 保护绝不重置
    EXPECT_FALSE(atlas.IsSlotAlive(firstDataSlot));  // 同帧重置后旧 epoch 立即失效

    // 下一帧帧首 BeginFrame 消费 m_pendingBoundaryEvict 并仅回收 1 页，再下一帧不得误重复淘汰
    atlas.BeginFrame();
    EXPECT_EQ(atlas.EvictionCount(), static_cast<std::size_t>(1));
    EXPECT_EQ(atlas.PageCount(), static_cast<std::size_t>(2));
    atlas.BeginFrame();
    EXPECT_EQ(atlas.EvictionCount(), static_cast<std::size_t>(1));
}

// ── Phase 47 Batch 1：DrawImage 纹理缓存（命中/篡改/LRU/旁路）与超单页 DrawArc（#75 ③） ──

static Image MakeSolidBgraImage(int w, int h, int stride,
                                std::uint8_t b, std::uint8_t g, std::uint8_t r, std::uint8_t a)
{
    Image img;
    img.width = w;
    img.height = h;
    img.stride = stride;
    img.pixels.assign(static_cast<std::size_t>(stride) * static_cast<std::size_t>(h), 0xCC);
    for (int row = 0; row < h; ++row)
    {
        std::uint8_t* p = img.pixels.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(stride);
        for (int col = 0; col < w; ++col)
        {
            p[col * 4 + 0] = b;
            p[col * 4 + 1] = g;
            p[col * 4 + 2] = r;
            p[col * 4 + 3] = a;
        }
    }
    return img;
}

static void ExpectPixelNear(const std::vector<std::uint8_t>& rgba, int w, int x, int y,
                            std::uint8_t er, std::uint8_t eg, std::uint8_t eb, int tol)
{
    const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w)
                           + static_cast<std::size_t>(x)) * 4u;
    EXPECT_TRUE(idx + 3 < rgba.size());
    EXPECT_TRUE(std::abs(static_cast<int>(rgba[idx + 0]) - static_cast<int>(er)) <= tol);
    EXPECT_TRUE(std::abs(static_cast<int>(rgba[idx + 1]) - static_cast<int>(eg)) <= tol);
    EXPECT_TRUE(std::abs(static_cast<int>(rgba[idx + 2]) - static_cast<int>(eb)) <= tol);
}

void Test47DrawImageCacheHitAndPaddingInvariance()
{
    ECDI::Test::HiddenGLWindow win(L"ECDI_T47_ImgCache1", 128, 128);
    EXPECT_TRUE(win.Hwnd() != nullptr);

    auto engine = std::make_shared<FontEngine>();
    GLRenderer renderer(engine);
    renderer.Initialize(Win32RenderContext(win.Hwnd()));
    if (!renderer.IsReady())
    {
        Logger::Log(LogLevel::Warning,
                    L"[T47-5a guard] WGL initialization unavailable; skipping pixel assertions");
        return;
    }

    // 构造 32x32 premultiplied BGRA 水平渐变（RGB=(a,0,0) -> B=0, G=0, R=a, A=a）
    Image grad1;
    grad1.width = 32;
    grad1.height = 32;
    grad1.stride = 32 * 4;
    grad1.pixels.resize(static_cast<std::size_t>(grad1.stride) * 32u);
    for (int y = 0; y < 32; ++y)
    {
        for (int x = 0; x < 32; ++x)
        {
            const auto a = static_cast<std::uint8_t>(255 * x / 31);
            std::uint8_t* px = grad1.pixels.data() + static_cast<std::size_t>(y) * grad1.stride + x * 4;
            px[0] = 0; px[1] = 0; px[2] = a; px[3] = a;
        }
    }

    // Frame 1：miss 并读回预乘渐变精准度
    renderer.BeginFrame(Color{ 0.0f, 0.0f, 0.0f, 1.0f });
    renderer.DrawImage(Rect{ 10.0f, 10.0f, 32.0f, 32.0f }, grad1);
    std::vector<std::uint8_t> fb;
    int fw = 0, fh = 0;
    EXPECT_TRUE(renderer.CaptureBackbufferRGBA(fw, fh, fb));
    EXPECT_EQ(fw, 128);
    EXPECT_EQ(fh, 128);
    renderer.EndFrame();

    EXPECT_EQ(renderer.ImageCacheMissCount(), static_cast<std::size_t>(1));
    EXPECT_EQ(renderer.ImageCacheHitCount(), static_cast<std::size_t>(0));
    EXPECT_EQ(renderer.ImageCacheEntryCount(), static_cast<std::size_t>(1));
    ExpectPixelNear(fb, fw, 10, 20, 0, 0, 0, 4);
    ExpectPixelNear(fb, fw, 10 + 16, 20, static_cast<std::uint8_t>(255 * 16 / 31), 0, 0, 4);
    ExpectPixelNear(fb, fw, 10 + 31, 20, 255, 0, 0, 4);

    // Frame 2：新 vector 拷贝 + 增大 stride + 尾部 padding 写入随机垃圾，仍必须 hit（stride 不入键）
    Image grad2;
    grad2.width = 32;
    grad2.height = 32;
    grad2.stride = 32 * 4 + 20;
    grad2.pixels.resize(static_cast<std::size_t>(grad2.stride) * 32u);
    for (int y = 0; y < 32; ++y)
    {
        for (int x = 0; x < 32; ++x)
        {
            const auto a = static_cast<std::uint8_t>(255 * x / 31);
            std::uint8_t* px = grad2.pixels.data() + static_cast<std::size_t>(y) * grad2.stride + x * 4;
            px[0] = 0; px[1] = 0; px[2] = a; px[3] = a;
        }
        for (int tail = 32 * 4; tail < grad2.stride; ++tail)
        {
            grad2.pixels[static_cast<std::size_t>(y) * grad2.stride + tail] =
                static_cast<std::uint8_t>((y * 13 + tail * 7) ^ 0xA5);
        }
    }

    renderer.BeginFrame(Color{ 0.0f, 0.0f, 0.0f, 1.0f });
    renderer.DrawImage(Rect{ 10.0f, 10.0f, 32.0f, 32.0f }, grad2);
    renderer.EndFrame();

    EXPECT_EQ(renderer.ImageCacheMissCount(), static_cast<std::size_t>(1));
    EXPECT_EQ(renderer.ImageCacheHitCount(), static_cast<std::size_t>(1));
    EXPECT_EQ(renderer.ImageCacheEntryCount(), static_cast<std::size_t>(1));
}

void Test47DrawImageMutationAndLruEviction()
{
    ECDI::Test::HiddenGLWindow win(L"ECDI_T47_ImgCache2", 128, 128);
    EXPECT_TRUE(win.Hwnd() != nullptr);

    auto engine = std::make_shared<FontEngine>();
    GLRenderer renderer(engine);
    renderer.SetImageCacheLimitsForTests(3, 16u * 1024u);   // 条数 3 / 字节 16KB / 单图旁路 8KB
    renderer.Initialize(Win32RenderContext(win.Hwnd()));
    if (!renderer.IsReady())
    {
        Logger::Log(LogLevel::Warning,
                    L"[T47-5b guard] WGL initialization unavailable; skipping pixel assertions");
        return;
    }

    // ① 改写同宽高同 stride 某像素：不得命中旧缓存
    Image img = MakeSolidBgraImage(16, 16, 16 * 4, 0, 0, 255, 255);   // 红 (R=255)
    renderer.BeginFrame(Color::Black());
    renderer.DrawImage(Rect{ 0.0f, 0.0f, 16.0f, 16.0f }, img);
    renderer.EndFrame();
    EXPECT_EQ(renderer.ImageCacheMissCount(), static_cast<std::size_t>(1));

    // 原地修改像素为绿 (G=255)
    std::uint8_t* p8 = img.pixels.data() + (8 * 16 + 8) * 4;
    p8[0] = 0; p8[1] = 255; p8[2] = 0; p8[3] = 255;
    renderer.BeginFrame(Color::Black());
    renderer.DrawImage(Rect{ 0.0f, 0.0f, 16.0f, 16.0f }, img);
    std::vector<std::uint8_t> fb;
    int fw = 0, fh = 0;
    EXPECT_TRUE(renderer.CaptureBackbufferRGBA(fw, fh, fb));
    renderer.EndFrame();
    EXPECT_EQ(renderer.ImageCacheMissCount(), static_cast<std::size_t>(2));
    EXPECT_EQ(renderer.ImageCacheHitCount(), static_cast<std::size_t>(0));
    ExpectPixelNear(fb, fw, 8, 8, 0, 255, 0, 2);

    // ② 触发条目 LRU 淘汰：当前缓存有 2 项（红、改绿），再画蓝与黄
    const Image blueImg = MakeSolidBgraImage(16, 16, 16 * 4, 255, 0, 0, 255);
    const Image yellowImg = MakeSolidBgraImage(16, 16, 16 * 4, 0, 255, 255, 255);
    renderer.BeginFrame(Color::Black());
    renderer.DrawImage(Rect{ 20.0f, 0.0f, 16.0f, 16.0f }, blueImg);
    renderer.DrawImage(Rect{ 40.0f, 0.0f, 16.0f, 16.0f }, yellowImg);
    renderer.EndFrame();
    EXPECT_EQ(renderer.ImageCacheEntryCount(), static_cast<std::size_t>(3));
    EXPECT_EQ(renderer.ImageCacheEvictionCount(), static_cast<std::size_t>(1));

    // ③ 超过 8KB 的大图 (64x64x4 = 16KB) 走旁路，不逐出已有热缓存
    const Image bigBypass = MakeSolidBgraImage(64, 64, 64 * 4, 255, 255, 255, 255);
    const std::size_t hitsBefore = renderer.ImageCacheHitCount();
    const std::size_t evictBefore = renderer.ImageCacheEvictionCount();
    renderer.BeginFrame(Color::Black());
    renderer.DrawImage(Rect{ 0.0f, 30.0f, 64.0f, 64.0f }, bigBypass);
    renderer.DrawImage(Rect{ 40.0f, 0.0f, 16.0f, 16.0f }, yellowImg);   // 热项应仍命中
    renderer.EndFrame();
    EXPECT_EQ(renderer.ImageCacheEntryCount(), static_cast<std::size_t>(3));
    EXPECT_EQ(renderer.ImageCacheEvictionCount(), evictBefore);
    EXPECT_EQ(renderer.ImageCacheHitCount(), hitsBefore + 1);
}

void Test47DrawArcAfterSinglePageSaturation()
{
    ECDI::Test::HiddenGLWindow win(L"ECDI_T47_ArcSat", 160, 160);
    EXPECT_TRUE(win.Hwnd() != nullptr);

    auto engine = std::make_shared<FontEngine>();
    engine->SetFontSource(std::make_unique<Win32FontSource>());
    GLRenderer renderer(engine);
    renderer.SetAtlasMaxPagesForTests(4);
    renderer.Initialize(Win32RenderContext(win.Hwnd()));
    if (!renderer.IsReady())
    {
        Logger::Log(LogLevel::Warning,
                    L"[T47-4 guard] WGL initialization unavailable; skipping pixel assertions");
        return;
    }

    // Frame 1：灌入大尺寸斜线段（每格 ~156x135 ≈ 21KB），迫使图集突破 Page 0 扩至 Page 1+
    renderer.BeginFrame(Color::Black());
    for (int i = 0; i < 70; ++i)
    {
        renderer.DrawLine(Point{ 2.0f, 2.0f },
                          Point{ 152.0f, 120.0f + static_cast<float>(i) * 0.4f },
                          2.0f, Color::White());
    }
    renderer.EndFrame();
    EXPECT_TRUE(renderer.AtlasPageCount() >= static_cast<std::size_t>(2));

    // Frame 2：清屏后调用 DrawArc（360 度黄环），验证不再因单页满容丢弧（闭合 #75 ③）
    renderer.BeginFrame(Color::Black());
    renderer.DrawArc(Point{ 80.0f, 80.0f }, 50.0f, 0.0f, 360.0f, 4.0f, Color{ 1.0f, 1.0f, 0.0f, 1.0f });
    std::vector<std::uint8_t> fb;
    int fw = 0, fh = 0;
    EXPECT_TRUE(renderer.CaptureBackbufferRGBA(fw, fh, fb));
    renderer.EndFrame();

    int arcYellowPixels = 0;
    for (int y = 20; y <= 140; ++y)
    {
        for (int x = 20; x <= 140; ++x)
        {
            const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(fw)
                                   + static_cast<std::size_t>(x)) * 4u;
            if (fb[idx + 0] > 180 && fb[idx + 1] > 180 && fb[idx + 2] < 60)
            {
                ++arcYellowPixels;
            }
        }
    }
    EXPECT_TRUE(arcYellowPixels >= 100);
}

// ── Phase 47 Batch 2：WGL 端到端像素验证（淘汰重建 / 中文粗斜 / #62 #68 回归） ──

void Test47WglPixelEvictionAndSlotReconstruction()
{
    ECDI::Test::HiddenGLWindow win(L"ECDI_T47_EvictE2E", 160, 160);
    EXPECT_TRUE(win.Hwnd() != nullptr);

    auto engine = std::make_shared<FontEngine>();
    engine->SetFontSource(std::make_unique<Win32FontSource>());
    GLRenderer renderer(engine);
    renderer.SetAtlasMaxPagesForTests(3);
    renderer.Initialize(Win32RenderContext(win.Hwnd()));
    if (!renderer.IsReady())
    {
        Logger::Log(LogLevel::Warning,
                    L"[T47-3 E2E guard] WGL initialization unavailable; skipping pixel assertions");
        return;
    }

    // 负向调用时机契约（P1-4）：BeginFrame 之前必返回 false 且清空输出
    std::vector<std::uint8_t> negFb(16, 1);
    int nw = 9, nh = 9;
    EXPECT_FALSE(renderer.CaptureBackbufferRGBA(nw, nh, negFb));
    EXPECT_EQ(nw, 0);
    EXPECT_EQ(nh, 0);
    EXPECT_TRUE(negFb.empty());

    const Font yahei24{ 24.0f, "Microsoft YaHei", 400, false };

    // Frame 1：预热四类几何槽与汉字，并验证 EndFrame 后调用 CaptureBackbufferRGBA 返回 false
    renderer.BeginFrame(Color::Black());
    renderer.DrawRect(Rect{ 10.0f, 10.0f, 40.0f, 20.0f }, Color::Red());
    renderer.DrawRoundedRect(Rect{ 10.0f, 40.0f, 40.0f, 30.0f }, 8.0f, Color{ 1.0f, 1.0f, 0.0f, 1.0f });
    renderer.DrawLine(Point{ 60.0f, 15.0f }, Point{ 105.0f, 35.0f }, 3.0f, Color{ 0.0f, 1.0f, 1.0f, 1.0f });
    renderer.DrawArc(Point{ 125.0f, 35.0f }, 18.0f, 0.0f, 360.0f, 3.0f, Color::Green());
    renderer.DrawText(Point{ 60.0f, 65.0f }, "测", Color::White(), yahei24);
    renderer.EndFrame();
    EXPECT_FALSE(renderer.CaptureBackbufferRGBA(nw, nh, negFb));
    EXPECT_TRUE(negFb.empty());

    // Frame 2–3：用大斜线段占满 3 页池并跨帧触发 Page-LRU 淘汰（AtlasEvictionCount >= 1）
    int lineId = 0;
    for (int f = 0; f < 3 && renderer.AtlasEvictionCount() == 0; ++f)
    {
        renderer.BeginFrame(Color::Black());
        for (int k = 0; k < 55; ++k)
        {
            renderer.DrawLine(Point{ 1.0f, 1.0f },
                              Point{ 152.0f, 118.0f + static_cast<float>(lineId++) * 0.25f },
                              2.0f, Color::White());
        }
        renderer.EndFrame();
    }
    EXPECT_TRUE(renderer.AtlasEvictionCount() >= static_cast<std::size_t>(1));

    // Frame 4：单帧内突发 95 条大线段超 2 个数据页上限 ⇒ 触发极端帧内就地重置（不删句柄）
    renderer.BeginFrame(Color::Black());
    for (int k = 0; k < 95; ++k)
    {
        renderer.DrawLine(Point{ 1.0f, 1.0f },
                          Point{ 152.0f, 118.0f + static_cast<float>(lineId++) * 0.25f },
                          2.0f, Color::White());
    }
    renderer.EndFrame();
    EXPECT_TRUE(renderer.AtlasInFrameResetCount() >= static_cast<std::size_t>(1));

    // Frame 5：淘汰+重置后再绘全部同几何槽、m_solid 与汉字 '测'，验证无花块错图且全部重建画对
    renderer.BeginFrame(Color::Black());
    renderer.DrawRect(Rect{ 10.0f, 10.0f, 40.0f, 20.0f }, Color::Red());
    renderer.DrawRoundedRect(Rect{ 10.0f, 40.0f, 40.0f, 30.0f }, 8.0f, Color{ 1.0f, 1.0f, 0.0f, 1.0f });
    renderer.DrawLine(Point{ 60.0f, 15.0f }, Point{ 105.0f, 35.0f }, 3.0f, Color{ 0.0f, 1.0f, 1.0f, 1.0f });
    renderer.DrawArc(Point{ 125.0f, 35.0f }, 18.0f, 0.0f, 360.0f, 3.0f, Color::Green());
    renderer.DrawText(Point{ 60.0f, 65.0f }, "测", Color::White(), yahei24);

    std::vector<std::uint8_t> fb;
    int fw = 0, fh = 0;
    EXPECT_TRUE(renderer.CaptureBackbufferRGBA(fw, fh, fb));
    renderer.EndFrame();

    // 验证 m_solid 纯红中心与背景、圆角外切角落保持黑底不被错贴、内部中心为纯黄
    ExpectPixelNear(fb, fw, 25, 20, 255, 0, 0, 2);
    ExpectPixelNear(fb, fw, 10, 40, 0, 0, 0, 2);       // R=8 圆角左上角外切区必须保持背景黑
    ExpectPixelNear(fb, fw, 30, 55, 255, 255, 0, 2);   // 圆角矩形中心纯黄

    // 验证 '测' 字重新进图集成功渲染出足够白字墨迹
    int glyphWhitePx = 0;
    for (int y = 60; y < 105; ++y)
    {
        for (int x = 58; x < 100; ++x)
        {
            const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(fw)
                                   + static_cast<std::size_t>(x)) * 4u;
            if (fb[idx + 0] > 180 && fb[idx + 1] > 180 && fb[idx + 2] > 180)
            {
                ++glyphWhitePx;
            }
        }
    }
    EXPECT_TRUE(glyphWhitePx >= 20);
}

void Test47WglChineseBoldObliquePixels()
{
    ECDI::Test::HiddenGLWindow win(L"ECDI_T47_CjkPixels", 240, 140);
    EXPECT_TRUE(win.Hwnd() != nullptr);

    auto engine = std::make_shared<FontEngine>();
    engine->SetFontSource(std::make_unique<Win32FontSource>());
    GLRenderer renderer(engine);
    renderer.Initialize(Win32RenderContext(win.Hwnd()));
    if (!renderer.IsReady())
    {
        Logger::Log(LogLevel::Warning,
                    L"[T47-6 guard] WGL initialization unavailable; skipping pixel assertions");
        return;
    }

    auto renderMeasureLuma = [&](const Font& f, const char* txt, double& outCentroidDx) -> std::uint64_t {
        renderer.BeginFrame(Color::Black());
        renderer.DrawText(Point{ 30.0f, 20.0f }, txt, Color::White(), f);
        std::vector<std::uint8_t> fb;
        int fw = 0, fh = 0;
        EXPECT_TRUE(renderer.CaptureBackbufferRGBA(fw, fh, fb));
        renderer.EndFrame();

        int minY = fh, maxY = -1;
        std::uint64_t sumLuma = 0;
        for (int y = 15; y < 90; ++y)
        {
            for (int x = 20; x < 200; ++x)
            {
                const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(fw)
                                       + static_cast<std::size_t>(x)) * 4u;
                const std::uint8_t lum = fb[idx + 0];
                if (lum > 10)
                {
                    sumLuma += lum;
                    if (y < minY) minY = y;
                    if (y > maxY) maxY = y;
                }
            }
        }
        if (maxY > minY)
        {
            const int midY = (minY + maxY + 1) / 2;
            double twx = 0.0, tw = 0.0, bwx = 0.0, bw = 0.0;
            for (int y = minY; y <= maxY; ++y)
            {
                for (int x = 20; x < 200; ++x)
                {
                    const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(fw)
                                           + static_cast<std::size_t>(x)) * 4u;
                    const double w = static_cast<double>(fb[idx + 0]);
                    if (w > 10.0)
                    {
                        if (y < midY) { twx += static_cast<double>(x) * w; tw += w; }
                        else          { bwx += static_cast<double>(x) * w; bw += w; }
                    }
                }
            }
            outCentroidDx = (tw > 0.0 && bw > 0.0) ? ((twx / tw) - (bwx / bw)) : 0.0;
        }
        return sumLuma;
    };

    double dxReg = 0.0, dxBold = 0.0, dxObl = 0.0, dxYahei = 0.0;
    const std::uint64_t lumaReg  = renderMeasureLuma(Font{ 24.0f, "宋体", 400, false }, "国测", dxReg);
    const std::uint64_t lumaBold = renderMeasureLuma(Font{ 24.0f, "宋体", 700, false }, "国测", dxBold);
    const std::uint64_t lumaObl  = renderMeasureLuma(Font{ 24.0f, "宋体", 400, true  }, "测",   dxObl);
    const std::uint64_t lumaYahei = renderMeasureLuma(Font{ 24.0f, "微软雅黑", 700, true }, "测国", dxYahei);

    EXPECT_TRUE(lumaReg > 5000);
    EXPECT_TRUE(lumaBold >= static_cast<std::uint64_t>(static_cast<double>(lumaReg) * 1.05));
    EXPECT_TRUE(lumaObl > 2000);
    EXPECT_TRUE(dxObl >= 0.8);
    EXPECT_TRUE(lumaYahei > 5000);
}

void Test47WglAuditContractPixelRegressions()
{
    ECDI::Test::HiddenGLWindow win(L"ECDI_T47_Audit", 200, 160);
    EXPECT_TRUE(win.Hwnd() != nullptr);

    auto engine = std::make_shared<FontEngine>();
    engine->SetFontSource(std::make_unique<Win32FontSource>());
    GLRenderer renderer(engine);
    renderer.Initialize(Win32RenderContext(win.Hwnd()));
    if (!renderer.IsReady())
    {
        Logger::Log(LogLevel::Warning,
                    L"[T47-7 guard] WGL initialization unavailable; skipping pixel assertions");
        return;
    }

    const Image redImg = MakeSolidBgraImage(20, 20, 20 * 4, 0, 0, 255, 255);

    renderer.BeginFrame(Color::Black());

    // ① M16：奇数边长 21x21 半透明圆角矩形（0.5 alpha 红），中心横竖带交界不得二次混合变亮/变暗
    renderer.DrawRoundedRect(Rect{ 10.0f, 10.0f, 21.0f, 21.0f }, 20.0f, Color{ 1.0f, 0.0f, 0.0f, 0.5f });

    // ② M13：父裁剪盒内嵌套负高 PushClip，DrawRect 严禁向父盒漏画
    renderer.DrawRect(Rect{ 50.0f, 10.0f, 40.0f, 40.0f }, Color::Green());
    renderer.PushClip(Rect{ 50.0f, 10.0f, 40.0f, 40.0f });
    renderer.PushClip(Rect{ 55.0f, 20.0f, 30.0f, -6.0f });
    renderer.DrawRect(Rect{ 50.0f, 10.0f, 40.0f, 40.0f }, Color::Red());
    renderer.PopClip();
    renderer.PopClip();

    // ③ L14：负宽高 DrawImage 不得画出镜像鬼影
    renderer.DrawImage(Rect{ 110.0f, 10.0f, -20.0f, 20.0f }, redImg);
    renderer.DrawImage(Rect{ 110.0f, 10.0f, 20.0f, -20.0f }, redImg);

    // ④ H2：.notdef 推进连续性（"测" + U+1F600 + "测"）
    renderer.DrawText(Point{ 10.0f, 70.0f }, "测\xF0\x9F\x98\x80测", Color::White(),
                      Font{ 20.0f, "SimSun", 400, false });

    // ⑤ DrawLine AA：青色实画线读回精准度
    renderer.DrawLine(Point{ 10.0f, 130.0f }, Point{ 90.0f, 130.0f }, 2.0f, Color{ 0.0f, 1.0f, 1.0f, 1.0f });

    std::vector<std::uint8_t> fb;
    int fw = 0, fh = 0;
    EXPECT_TRUE(renderer.CaptureBackbufferRGBA(fw, fh, fb));
    renderer.EndFrame();

    // 检验 M16：中心 (20,20) 与外围带 (20,15) 红分量均接近 128 且相互之差 <= 4/255
    const std::size_t idxCenter = (20u * static_cast<std::size_t>(fw) + 20u) * 4u;
    const std::size_t idxBand = (15u * static_cast<std::size_t>(fw) + 20u) * 4u;
    EXPECT_TRUE(std::abs(static_cast<int>(fb[idxCenter]) - 128) <= 4);
    EXPECT_TRUE(std::abs(static_cast<int>(fb[idxCenter]) - static_cast<int>(fb[idxBand])) <= 4);

    // 检验 M13：绿色区内部保持纯绿 (0,255,0)
    ExpectPixelNear(fb, fw, 70, 25, 0, 255, 0, 2);

    // 检验 L14：负尺寸 DrawImage 四象限周边均为背景黑 (0,0,0)
    ExpectPixelNear(fb, fw, 100, 15, 0, 0, 0, 2);
    ExpectPixelNear(fb, fw, 118, 15, 0, 0, 0, 2);
    ExpectPixelNear(fb, fw, 118, 4,  0, 0, 0, 2);

    // 检验 H2：第二个 '测' 右端墨迹 X 必定落在 > 10 + 20 + 14 = 44 处（无与首字重叠）
    int rightmostX = 0;
    for (int y = 65; y < 105; ++y)
    {
        for (int x = 10; x < 120; ++x)
        {
            const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(fw)
                                   + static_cast<std::size_t>(x)) * 4u;
            if (fb[idx + 0] > 80 && x > rightmostX)
            {
                rightmostX = x;
            }
        }
    }
    EXPECT_TRUE(rightmostX >= 45);

    // 检验 DrawLine AA：青线中心 (50,130) 读回青色 (0,255,255)
    ExpectPixelNear(fb, fw, 50, 130, 0, 255, 255, 4);
}

void ECDI::Test::RegisterGLBackendTests()
{
    GetTestRegistry().Add("GLBackend.AtlasShelfAllocation", &Test26AtlasShelfAllocation);      // T26-7
    GetTestRegistry().Add("GLBackend.InitFailureIsNoOp",    &Test26GLRendererInitFailureIsNoOp); // T26-8
    GetTestRegistry().Add("GLBackend.NegativeDimensions",   &Test26GLRendererNegativeDimensionsAndDegenerate);
    // ── Phase 47 Batch 0：GLGlyphAtlas 多页池、LRU 淘汰与帧内就地重置 ──
    GetTestRegistry().Add("GLBackend.AtlasMultiPageExpansionAndWarmHit", &Test47AtlasMultiPageExpansionAndWarmHit);
    GetTestRegistry().Add("GLBackend.AtlasPageLruEvictionAndEpochSafety", &Test47AtlasPageLruEvictionAndEpochSafety);
    GetTestRegistry().Add("GLBackend.AtlasInFrameResetProtection",        &Test47AtlasInFrameResetProtection);
    // ── Phase 47 Batch 1：DrawImage LRU 缓存 + 饱和后 DrawArc 闭合 #75 ③ ──
    GetTestRegistry().Add("GLBackend.DrawImageCacheHitAndPaddingInvariance", &Test47DrawImageCacheHitAndPaddingInvariance);
    GetTestRegistry().Add("GLBackend.DrawImageMutationAndLruEviction",       &Test47DrawImageMutationAndLruEviction);
    GetTestRegistry().Add("GLBackend.DrawArcAfterSinglePageSaturation",      &Test47DrawArcAfterSinglePageSaturation);
    // ── Phase 47 Batch 2：WGL 端到端像素回归 ──
    GetTestRegistry().Add("GLBackend.WglPixelEvictionAndSlotReconstruction", &Test47WglPixelEvictionAndSlotReconstruction);
    GetTestRegistry().Add("GLBackend.WglChineseBoldObliquePixels",         &Test47WglChineseBoldObliquePixels);
    GetTestRegistry().Add("GLBackend.WglAuditContractPixelRegressions",      &Test47WglAuditContractPixelRegressions);
}
