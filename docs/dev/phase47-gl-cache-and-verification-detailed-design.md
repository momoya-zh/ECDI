# Phase 47 GL 后端渲染缓存与自动化像素验证（#95）— 详细设计

> **版本**：`v1.1`（2026-11-16，✅ **已实施并验证** —— 吸收 `3×P1 + 5×P2` 全数修正并完成 Batch 0 → Batch 1 → Batch 2 落地，MinGW/Clang 零警告构建、`631` 用例实测通过，`0` WGL 守卫触发）
> **上游文档**：[phase47-gl-cache-and-verification-requirements.md](phase47-gl-cache-and-verification-requirements.md)（✅ `v1.2`，`D47-1`～`D47-4 = ⓐ`）· [phase47-gl-cache-and-verification-preliminary-design.md](phase47-gl-cache-and-verification-preliminary-design.md)（✅ `v1.3`，含 `O47-1`～`O47-3` 归档定标）
> **规模预算**：**6** 个内部/测试文件（`GLGlyphAtlas.h/.cpp`、`GLRenderer.h/.cpp`、`Tests/TestWindowHelpers.h`、`Tests/GLBackendTests.cpp`）· 公共头文件 **`102 -> 102`（+0）** · 注册测试 **`622 -> 631`（+9：Batch 0 `+3` / Batch 1 `+3` / Batch 2 `+3`）** · `docs/` **追踪 `220` → 提交后 `226`**（本详设净增 `+1`，另 `+5` 为工作区尚待提交之 Phase 46×3 + Phase 47 需求/初设）

---

## §0 核心不变量与状态同步纪律

1. **红线 1–3 与零公共头变更**：`ECDI/include/ECDI/**`（102 个公共头文件）、`CMakeLists.txt`、`third_party/` 与 `GDIBackend` / `GDITextMeasurer` 全部 `0` 修改。`GLGlyphAtlas.h` 严守零 `<Windows.h>` / 零 `<GL/gl.h>`；`Allocate(int w, int h)` 保留 `Page 0` 单页语义，既有 `T26-7`（`GLBackendTests.cpp:42-99`）与 `T46-8`（`FontWeightTests.cpp:320-350`）零改动全绿。
2. **双路径淘汰与句柄不变量**：
   - **路径 A（`BeginFrame` / 跨帧 Page-LRU）**：仅针对 `!protectedPage && allocated && lastUsedFrame < m_currentFrame`，调用 **`glDeleteTextures`** 真实释页、`allocated = false`、`texture = 0`、`++epoch`、`PurgePageSlots`、`++m_evictionCount`、清零 `m_pendingBoundaryEvict = false`。
   - **路径 B（极端单帧全池溢出就地重置）**：当全部非保护页均 `lastUsedFrame == m_currentFrame` 时，若 `m_ready` **先 `glFlush()`**、**禁止 `glDeleteTextures`**、使用 `glTexSubImage2D` 原位刷零纹理、更新 `lastResetFrame = m_currentFrame`、`++epoch`、`PurgePageSlots`、`++m_inFrameResetCount`、置 `m_pendingBoundaryEvict = true`。
3. **纹理绑定追踪脱耦防护（防 `Upload` 暗切纹理）**：
   - `GLGlyphAtlas::Upload(slot, a8)` 在多页模式下必须绑定 **`slot.texture`**（不再绑定单一 `m_texture`）。因该调用会改写当前 `GL_TEXTURE_BINDING_2D`，`GLRenderer` 侧所有会触发上传的方法（`DrawText` 调用 `GetOrCreate` 前若走 miss、或直接在 `CornerSlot` / `LineSlot` / `ArcSlot` 上传分支，及统一保守地在 `Upload` 发生的路径上）必须置 **`m_boundTexture = 0;`**，确保紧随其后的 `BindTexture(slot.texture)` / `BindTexture(m_solid.texture)` 不会因缓存的 `m_boundTexture` 误判跳过切换！

---

## §1 逐文件详细设计规约

### 1.1 `ECDI/src/Render/GLGlyphAtlas.h`（Batch 0）

1. **`GlyphSlot`（`GLGlyphAtlas.h:16-23`）扩充**：
   ```cpp
   struct GlyphSlot{
       int   x = 0, y = 0, w = 0, h = 0;
       float bearingX = 0.0f;
       float bearingY = 0.0f;
       float advance  = 0.0f;
       int   pixelSize = 0;
       bool  valid = false;
       unsigned int  texture = 0;     ///< 所属页 GLuint（0 = 无效或无头逻辑页）
       std::uint32_t pageIndex = 0;   ///< 固定页槽索引 [0, maxPages)
       std::uint32_t epoch = 0;       ///< 页代际号（活跃页从 1 起，0 恒无效，与 Empty() 不对称）
   };
   ```
2. **`GLGlyphAtlas` 公共接口扩充（保持零 GL 头）**：
   - 常量：`static constexpr std::size_t kDefaultMaxPages = 8;`、`static constexpr std::size_t kMinMaxPages = 2;`。
   - 构造：`explicit GLGlyphAtlas(std::size_t maxPages = kDefaultMaxPages);`（`maxPages < kMinMaxPages` 钳位至 `2`；保留默认参数使既有 `GLGlyphAtlas atlas;` 零改动编译）。
   - 新增方法：
     - `void SetMaxPagesForTests(std::size_t maxPages);`（`< 2` 时静默钳位至 `2` 并 `Logger::Log(LogLevel::Warning, ...)`）
     - `void BeginFrame() noexcept;`
     - `GlyphSlot AllocatePaged(int w, int h);`
     - `bool IsSlotAlive(const GlyphSlot& slot) const noexcept;`
     - `void TouchSlot(const GlyphSlot& slot) noexcept;`
     - `std::size_t PageCount() const noexcept;`（当前 `allocated == true` 的页数）
     - `std::size_t EvictionCount() const noexcept;`
     - `std::size_t InFrameResetCount() const noexcept;`
   - 既有接口保留：`Initialize()`、`IsReady()`、`Texture()`（明确注释：返回受保护 `Page 0` 纹理 `m_pages.empty() ? 0 : m_pages[0].texture`）、`Allocate(w, h)`、`Upload(slot, a8)`、`GetOrCreate(key, engine)`、`MissCount()`、`UsedPixels()`、`AllocationFailureCount()`、`IsFull()`。
3. **私有成员与 `PageState`**：
   ```cpp
   struct PageState{
       unsigned int texture = 0;
       int shelfX = 0;
       int shelfY = 0;
       int shelfH = 0;
       std::size_t usedPixels = 0;
       std::uint32_t epoch = 1;
       std::uint64_t lastUsedFrame = 0;
       std::uint64_t lastResetFrame = 0;
       bool allocated = false;
       bool protectedPage = false;
       std::vector<GlyphKey> residentKeys;   ///< 仅记录归属该页的 m_slots 字形键（空间可控，O(页内槽) 清理）
   };
   ```
   - 替换原孤立 `m_texture / m_shelfX / m_shelfY / m_shelfH` 为 `std::vector<PageState> m_pages;`、`std::size_t m_maxPages`、`std::uint64_t m_currentFrame = 1`、`std::size_t m_evictionCount = 0`、`std::size_t m_inFrameResetCount = 0`、`bool m_pendingBoundaryEvict = false`。
   - 私有辅助：`bool EnsurePageTexture(PageState& page);`、`GlyphSlot AllocateOnPage(std::size_t pageIdx, int w, int h, bool recordFullFailure);`、`void PurgePageSlots(std::uint32_t pageIdx);`、`void EvictPage(std::size_t victimIdx);`、`void ResetPageInPlace(std::size_t victimIdx);`。

### 1.2 `ECDI/src/Render/GLGlyphAtlas.cpp`（Batch 0）

1. **构造与析构**：
   - `GLGlyphAtlas(std::size_t maxPages)`：钳位 `m_maxPages = (maxPages < kMinMaxPages) ? kMinMaxPages : maxPages;`，初始化 `m_pages.emplace_back()` 设 `allocated = true, protectedPage = true, epoch = 1`（`Page 0` 默认存在）。
   - `~GLGlyphAtlas()`（`GLGlyphAtlas.cpp:20-29`）：遍历 `m_pages`，凡 `p.texture != 0` 均调 `glDeleteTextures(1, &tex); p.texture = 0;`。
2. **`EnsurePageTexture` 与 `Initialize`**：
   - `EnsurePageTexture(PageState& page)`：
     ```cpp
     while (glGetError() != GL_NO_ERROR) { }
     GLuint tex = 0;
     glGenTextures(1, &tex);
     if (tex != 0)
     {
         glBindTexture(GL_TEXTURE_2D, tex);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
         glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
         const std::vector<std::uint8_t> blank(static_cast<std::size_t>(kSize) * kSize * 2, 0);
         glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
         glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, kSize, kSize, 0,
                      GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, blank.data());
     }
     if (tex == 0 || glGetError() != GL_NO_ERROR)
     {
         if (tex != 0) { glDeleteTextures(1, &tex); }
         page.texture = 0;
         page.allocated = false;
         Logger::Log(LogLevel::Error, L"GLGlyphAtlas: texture creation failed");
         return false;
     }
     page.texture = tex;
     page.allocated = true;
     return true;
     ```
   - `Initialize()`：`m_ready = EnsurePageTexture(m_pages[0]);`。若失败仍维持 `m_ready == false`（与 `:50-54` 同日志同失败态）。
3. **`AllocateOnPage`、`Allocate`、`AllocatePaged`、`BeginFrame`、`EvictPage`、`ResetPageInPlace`**：
   - `Allocate(int w, int h)`：调用 `AllocateOnPage(0, w, h, /*recordFullFailure=*/true)`，**`UsedPixels()` 语义明确为当前全部活跃页（`allocated == true`）`usedPixels` 之总和**（仅启用 `Page 0` 时与原单页实现完全等价，保持 `T26-7`）。单页 `Page 0` 满时自增 `m_allocationFailures` 与置 `m_fullWarned = true`，不扩 `Page 1+`，100% 保持 `T26-7` 与 `T46-8` 行为。
   - `AllocatePaged(int w, int h)`：
     1. `if (w <= 0 || h <= 0 || w > kSize || h > kSize) return Empty();`
     2. 遍历现有页 `i = 0 .. m_pages.size() - 1` 若 `m_pages[i].allocated`：调 `AllocateOnPage(i, w, h, false)`，成功则 `UpdateTotalUsedPixels()` 并返回。
     3. 寻找可用复用槽或新增页（优先找首个 `!allocated` 复用固定 `pageIndex`；若无且 `m_pages.size() < m_maxPages` 则 `emplace_back()`）：
        - 清空该页 shelf、**复用的 `!allocated` 页不额外 `++epoch`（其 `epoch` 已在 `EvictPage` 或初始构造中确定），新增页 `epoch` 初始为 `1`**、设 `lastUsedFrame = m_currentFrame`、`lastResetFrame = m_currentFrame`；
        - 若 `m_ready`：调 `EnsurePageTexture(m_pages[i])`；失败则 `++m_allocationFailures` 并返回 `Empty()`；
        - 若 `!m_ready`（无头单测）：置 `m_pages[i].allocated = true; m_pages[i].texture = 0;`；
        - 调用 `AllocateOnPage(i, w, h, false)` 返回。
     4. 全池 `m_maxPages` 均 `allocated` 且无空隙：
        - 查找 `!protectedPage && allocated && lastUsedFrame < m_currentFrame` 中 `lastUsedFrame` 最小者 `lruIdx`：
          - 若找到：**执行 `EvictPage(lruIdx)`**（删纹理、递增 `epoch`、清 `m_slots`、`++m_evictionCount`、`m_pendingBoundaryEvict = false`），随后立即重新激活 `m_pages[lruIdx]`（若 `m_ready` 调 `EnsurePageTexture`，失败则 `++m_allocationFailures` 返回 `Empty()`；否则 `allocated = true`）再 `AllocateOnPage(lruIdx, w, h, false)`。
          - 若未找到（全部非保护页均 `lastUsedFrame == m_currentFrame`，即极端单帧全池溢出）：查找 `!protectedPage && allocated` 中 `lastResetFrame` 最小者（保证轮换均摊），调 `ResetPageInPlace(inFrameIdx)`（若 `m_ready` 先 `glFlush()` + `glTexSubImage2D` 刷零原纹理，递增 `epoch`、`PurgePageSlots`、`++m_inFrameResetCount`、`m_pendingBoundaryEvict = true`），再 `AllocateOnPage(inFrameIdx, w, h, false)`。
   - `BeginFrame() noexcept`：
     - `++m_currentFrame;`
     - 若 `m_pendingBoundaryEvict` 且统计当前 `PageCount() >= m_maxPages`：选 `!protectedPage && allocated && lastUsedFrame < m_currentFrame` 最小 `lastUsedFrame` 页执行 `EvictPage(lruIdx)`；最后**无条件清零 `m_pendingBoundaryEvict = false`**。
   - `PurgePageSlots(std::uint32_t pageIdx)`：遍历 `m_pages[pageIdx].residentKeys`，查 `auto it = m_slots.find(k)`，仅当 `it != m_slots.end() && it->second.pageIndex == pageIdx` 时 `m_slots.erase(it)`，最后 `m_pages[pageIdx].residentKeys.clear()`。
4. **`Upload` 与 `GetOrCreate`**：
   - `Upload(slot, a8)`：`if (!slot.valid) return;`；**若 `slot.texture == 0` 则输出 `Logger::Log(LogLevel::Warning, L"GLGlyphAtlas::Upload: invalid texture handle 0; skipped")` 并提前 `return`（禁止无日志静默跳过）**；校验尺寸后绑定 **`slot.texture`**（`glBindTexture(GL_TEXTURE_2D, slot.texture)`）上传 `GL_LUMINANCE_ALPHA`。
   - `GetOrCreate(key, engine)`：
     - 查 `m_slots.find(key)` 命中时：**同步调用 `TouchSlot(cached->second)`** 更新 `lastUsedFrame = m_currentFrame` 后直接返回！
     - 未命中：`++m_missCount;` → 调 `engine.GlyphByKey(key, bitmap)`。若 `width > 0 && height > 0`：调 **`AllocatePaged(bitmap.width, bitmap.height)`**；若 `region.valid`：调 `Upload(region, bitmap.a8)`，拷贝坐标与 `texture / pageIndex / epoch / valid=true` 到 `slot`，插入 `m_slots.emplace(key, slot)` 并 **`m_pages[slot.pageIndex].residentKeys.push_back(key)`**；若 `!region.valid`（极端分配失败），**不写入 `m_slots` 永久负缓存**直接返回 `slot`。若空白字形（`w == 0 || h == 0`）：`slot.valid = true; m_slots.emplace(key, slot);`。

### 1.3 `ECDI/src/Render/GLRenderer.h` & `GLRenderer.cpp`（Batch 1）

1. **`GLRenderer.h` 改动**：
   - **头文件补齐（P1-1）**：新增 `#include <compare>`（保障 MinGW GCC `auto operator<=>` 的 `std::strong_ordering` 严格可见）。
   - 新增常量：
     - `static constexpr std::size_t kDefaultMaxImageCacheEntries = 128;`
     - `static constexpr std::size_t kDefaultMaxImageCacheBytes = 32u * 1024u * 1024u;`
     - `static constexpr std::size_t kDefaultMaxSingleCachedImageBytes = 2u * 1024u * 1024u;`
   - 新增公开查询 / 测试缝（详见初设 §1.3 全表）：`AtlasPageCount()`、`AtlasEvictionCount()`、`AtlasInFrameResetCount()`、`ImageCacheHitCount()`、`ImageCacheMissCount()`、`ImageCacheEvictionCount()`、`ImageCacheEntryCount()`、`ImageCacheBytes()`、`SetAtlasMaxPagesForTests(std::size_t)`、`SetImageCacheLimitsForTests(std::size_t, std::size_t)`、`CaptureBackbufferRGBA(int&, int&, std::vector<std::uint8_t>&) const`。
   - 私有成员与接口调整：
     - `TexturedQuad`、`TexturedQuadUV`、`SolidSegment` 去掉 `const`；新增 `void BindTexture(unsigned int tex);`。
     - **定义顺序约束**：`struct ImageCacheKey{ int width = 0; int height = 0; std::uint64_t hashLo = 0; std::uint64_t hashHi = 0; auto operator<=>(const ImageCacheKey&) const = default; };` 与 `struct CachedImageEntry{ GLuint texture = 0; std::size_t byteSize = 0; std::uint64_t lastUsedTick = 0; };` 必须定义于 `m_imageCache` 等成员**之前**。
     - 新增成员：`std::size_t m_pendingAtlasMaxPages = GLGlyphAtlas::kDefaultMaxPages;`、`GLuint m_boundTexture = 0;`、`std::map<ImageCacheKey, CachedImageEntry> m_imageCache;`、`std::size_t m_imageCacheBytes = 0;`、`std::size_t m_maxImageCacheEntries = kDefaultMaxImageCacheEntries;`、`std::size_t m_maxImageCacheBytes = kDefaultMaxImageCacheBytes;`、`std::size_t m_maxSingleCachedImageBytes = kDefaultMaxSingleCachedImageBytes;`、`std::uint64_t m_imageCacheTick = 0;`、`std::size_t m_imageCacheHits = 0;`、`std::size_t m_imageCacheMisses = 0;`、`std::size_t m_imageCacheEvictions = 0;`。
     - 私有方法：`void ClearImageCache();`、`void EvictImageCacheFor(std::size_t incomingBytes);`（LRU 驱逐规约：当 `m_imageCache.size() >= m_maxImageCacheEntries || m_imageCacheBytes + incomingBytes > m_maxImageCacheBytes` 且 `!m_imageCache.empty()` 时，循环淘汰 **`lastUsedTick` 最小**项，`if (m_boundTexture == victim.texture) m_boundTexture = 0;`、`glDeleteTextures(1, &victim.texture)`、`m_imageCacheBytes -= victim.byteSize`、`m_imageCache.erase(victimIt)`、`++m_imageCacheEvictions`）。
2. **`GLRenderer.cpp` 关键实现点**：
   - `~GLRenderer()`（`:37-64`）：`ClearImageCache();` → `m_atlas.reset();` → `wglMakeCurrent` → `wglDeleteContext` → `ReleaseDC`。
   - `Initialize()`（`:120`）：`m_atlas = std::make_unique<GLGlyphAtlas>(m_pendingAtlasMaxPages);`，其余创建顺序保持 `Initialize()` → `m_solid = m_atlas->Allocate(1, 1);` → `m_atlas->Upload(m_solid, white)` → `m_ready = true;`。
   - `BeginFrame()`（`:139-218`）：`if (!m_ready) { ... return; }` 保持在前；在设置好 viewport / ortho / blend / `GL_TEXTURE_2D` 后，执行 `m_atlas->BeginFrame(); m_boundTexture = 0; BindTexture(m_solid.texture != 0 ? m_solid.texture : m_atlas->Texture());`。
   - `BindTexture(unsigned int tex)`：`if (tex != 0 && m_boundTexture != tex) { glBindTexture(GL_TEXTURE_2D, tex); m_boundTexture = tex; }`。
   - `DrawRect`、`DrawRoundedRect`、`SolidSegment`、`TexturedQuad`、`TexturedQuadUV`：将 `DrawRect` / `DrawRoundedRect` 改为先 `BindTexture(m_solid.texture)`（`CornerSlot` 后画四角前先按 `BindTexture(corner.texture)` 切换）；新增或重用 `BindTexture` 传参使 `DrawText`（每次 `GetOrCreate` 后无条件 **`m_boundTexture = 0;`** 再于 `slot.valid && slot.w > 0 && slot.h > 0` 时 `BindTexture(slot.texture)`）、`DrawLine`、`DrawArc`、`CornerSlot`、`LineSlot`、`ArcSlot`（三者内部调用 `AllocatePaged` + `Upload` 时置 **`m_boundTexture = 0;`**）绝不发生纹理状态错认。
   - `CornerSlot` / `LineSlot` / `ArcSlot`（`:800-897`）：
     ```cpp
     if (cached != map.end())
     {
         if (m_atlas->IsSlotAlive(cached->second))
         {
             m_atlas->TouchSlot(cached->second);
             return cached->second;
         }
         map.erase(cached);   // 页已淘汰/重置 ⇒ 擦除旧 UV 并重建
     }
     // 后续改用 m_atlas->AllocatePaged(...) 分配并 Upload，若发生上传置 m_boundTexture = 0
     ```
   - `DrawImage`（`:465-524`）：
     1. 保留 `dest <= 0` 与 `BackendContract::ImageIsDrawable(image)` 守卫；
     2. **立即置 `m_boundTexture = 0;`**（保守失效）；
     3. 计算 `byteSize = width * height * 4`；若 `byteSize > m_maxSingleCachedImageBytes`：走原一次性 BGRA→RGBA 上传 → `GL_QUADS` → `glDeleteTextures` 旁路（不进 LRU 统计也不逐出热项）；
     4. 否则算 `Hash128ActiveRows(image)` 构 `ImageCacheKey{width, height, h1, h2}`（**不含 `stride`**）：
        - 命中：`++m_imageCacheHits; it->second.lastUsedTick = ++m_imageCacheTick; BindTexture(it->second.texture);` → `glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);` → `GL_QUADS`；
        - 未命中：`++m_imageCacheMisses; EvictImageCacheFor(byteSize);` → BGRA→RGBA 上传建 `GLuint tex` → 若 `tex != 0` 写入 `m_imageCache` 并计 `m_imageCacheBytes += byteSize` → `BindTexture(tex)` 绘制；
     5. **统一退出路径**：`glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); BindTexture(m_solid.texture);`。
   - `CaptureBackbufferRGBA`：严格按初设 §0.1 契约实现（`!m_ready || !m_inFrame || m_w <= 0 || m_h <= 0` 清空并返回 `false` + `Logger::Warning`；单双缓冲分支读 `GL_BACK`/`GL_FRONT`；`glGetError()` 检查；行翻转成 top-down；注记该 `const` 方法会修改 `GL_READ_BUFFER` 与 `GL_PACK_ALIGNMENT` 全局 GL 状态，调用方不得依赖原值）。
   - `SetAtlasMaxPagesForTests`：`if (m_atlas) m_atlas->SetMaxPagesForTests(maxPages); else m_pendingAtlasMaxPages = (maxPages < 2 ? 2 : maxPages);`。
   - `SetImageCacheLimitsForTests(std::size_t maxEntries, std::size_t maxBytes)`：更新上下限；当 `maxBytes == 0` 时设 `m_maxSingleCachedImageBytes = 0`（全旁路模式），当 `maxBytes > 0` 时设 `m_maxSingleCachedImageBytes = (std::max<std::size_t>)(1, (std::min)(maxBytes / 2, kDefaultMaxSingleCachedImageBytes))`（确保小限额单测仍可测入缓存与淘汰），随后调 `EvictImageCacheFor(0)` 立即裁剪超出部分。

### 1.4 `ECDI/src/Tests/TestWindowHelpers.h` & `GLBackendTests.cpp`（Batch 0–2）

1. **`TestWindowHelpers.h`**：header-only `inline` RAII 封装 `HiddenGLWindow`（构造创建未显示 `WS_POPUP | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`、`CS_OWNDC` 窗口，精准尺寸 `w × h`，析构 `DestroyWindow`）。
2. **`GLBackendTests.cpp` 注册新增 `+9` 用例（`622 → 631`）**：详细断言对照初设 §2 全部 9 项，覆盖无头与真实 WGL 双链，容差模板严格钉死为实色 `<= 2/255`、混合 `<= 4/255`、粗体墨量 `>= 1.05×` 常规、斜体上下质心右偏 `>= 0.8 px`，包含 `CaptureBackbufferRGBA` 帧外调用返 `false` 负向断言与 `!renderer.IsReady()` 方案 ⓐ 守卫（本机 `0` 守卫触发）。

---

## §2 分批实施步骤与验收门禁

1. **Batch 0（`GLGlyphAtlas` 多页引擎与淘汰逻辑，`622 → 625`）**：
   - 修改 `GLGlyphAtlas.h` / `GLGlyphAtlas.cpp`、新增 `TestWindowHelpers.h`、在 `GLBackendTests.cpp` 注册 `AtlasMultiPageExpansionAndWarmHit`、`AtlasPageLruEvictionAndEpochSafety`、`AtlasInFrameResetProtection`。
   - **闸门**：MinGW 构建零警告；`ecdi_tests` 全部 `625` 用例通过（包含无修改通过的 `T26-7` 与 `T46-8`）。
2. **Batch 1（`GLRenderer` 多页绑定 + `DrawImage` LRU + `CaptureBackbufferRGBA`，`625 → 628`）**：
   - 修改 `GLRenderer.h` / `GLRenderer.cpp`，在 `GLBackendTests.cpp` 注册 `DrawImageCacheHitAndPaddingInvariance`、`DrawImageMutationAndLruEviction`、`DrawArcAfterSinglePageSaturation`。
   - **闸门**：MinGW 构建零警告；`ecdi_tests` 全部 `628` 用例通过（本机 WGL 守卫 `0` 触发）。
3. **Batch 2（WGL 端到端像素验证与 `#62`/`#68` 转机测，`628 → 631`）**：
   - 在 `GLBackendTests.cpp` 注册 `WglPixelEvictionAndSlotReconstruction`、`WglChineseBoldObliquePixels`、`WglAuditContractPixelRegressions`。
   - **终闸**：MinGW + Clang 双链构建零警告且 `ecdi_tests` 全部 `631` 用例通过（本机 `0` WGL 守卫触发）；更新 `README.md`、`README.zh-CN.md`、`docs/dev/README.md`、`roadmap-deferred.md`（同步 `#95` 与 `#75` ③ 完成状态，`tests=631`），`git add` 入索引后执行 `python tools/doc_lint.py --select L04,L05,L06,L08,L09,L11,L13,L14 --quiet` 达成 `0 defect, 0 style` + `L13` 阳性对照。

---

## 3. 修订记录

- **v1.0（2026-11-16）**：Phase 47 详细设计初版落盘：完成 `GLGlyphAtlas`、`GLRenderer`、`TestWindowHelpers.h` 逐接口详设与 Batch 0→1→2 实施方案（`622 → 631`）。
- **v1.1（2026-11-16）**：吸收评审 `3×P1 + 5×P2` 放行实施：`GLRenderer.h` 增 `#include <compare>` 及定义顺序钉死；`Upload` 句柄 0 改为 `Logger::Warning`+早退；`AllocatePaged` 复用页 `epoch` 表述澄明；`DrawImage` 命中统一走 `BindTexture`；补 `EvictImageCacheFor` 按 `lastUsedTick` LRU 规约、`CaptureBackbufferRGBA` GL 状态说明、`SetImageCacheLimitsForTests` 下界及 `UsedPixels()` 活跃页总量语义。
