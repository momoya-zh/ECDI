# Phase 47 GL 后端渲染缓存与自动化像素验证（#95）— 初步设计

> **版本**：`v1.3`（2026-11-16，吸收 `v1.2` 复审放行条件 `P1-1` 及全部 `P2`，已锁定并转入详细设计）
> **上游文档**：[phase47-gl-cache-and-verification-requirements.md](phase47-gl-cache-and-verification-requirements.md)（✅ `v1.2`，`D47-1`～`D47-4 = ⓐ` 全数锁定）
> **实测归档**：[o47_readout.txt](file:///d:/Project/ECDI/.workbuddy/spike/p47/o47_readout.txt)（[o47_probe.cpp](file:///d:/Project/ECDI/.workbuddy/spike/p47/o47_probe.cpp) + [o47_supp.cpp](file:///d:/Project/ECDI/.workbuddy/spike/p47/o47_supp.cpp)，MinGW GCC `-O2` exit 0）
> **规模预算**：**6** 个内部/测试文件（`GLGlyphAtlas.h/.cpp`、`GLRenderer.h/.cpp`、`Tests/TestWindowHelpers.h`、`Tests/GLBackendTests.cpp`）· 公共头文件 **`102 -> 102`（+0）** · 注册测试 **`622 -> 631`（+9，落于 `+7～12` 预算内）** · `docs/` **追踪 `220`（现状）→ 提交后 `225`（本初设净增 `+1`，另 `+4` 为尚在工作区的 Phase 46×3 + Phase 47 需求稿历史未提交项，随本轮一并提交）**

---

## §0 实机探针 `O47-1`～`O47-3` 定标结论与容差契约

> 读数摘自实机归档 `.workbuddy/spike/p47/o47_readout.txt`（NVIDIA GeForce RTX 4050 / OpenGL 4.6.0 & Microsoft GDI Generic 1.1.0）：

### 0.1 `O47-1`：WGL 像素读回载具与 `CaptureBackbufferRGBA` 契约（`:1-25`）

| 载具方案 | 样本 `bg(5,5)` / `red(50,40)` / `la128(128,28)` | 抢焦 | 耗时 | 结论 |
|---|---|---|---|---|
| **Mode 1：未 Show `WS_POPUP` + `SwapBuffers` 前读 `GL_BACK`** | `(25,51,204,255)` / `(255,0,0,255)` / `(140,153,230,191)` | `0` | 首帧 `29.6 ms` / 后续 ~`1.2 ms` | ✅ **主载具确立**：零偏离、零闪窗抢焦 |
| **Mode 1/2：`SwapBuffers` 之后读 `GL_BACK`** | `(0,0,0,0)` / `(0,0,0,0)` / `(0,0,0,0)` | `0` | — | ❌ 证实 `SwapBuffers` 后 backbuffer 必丢弃，**抓帧必在 `EndFrame()` 前** |
| **Mode 3：32bpp DIB (`PFD_DRAW_TO_BITMAP` Generic 1.1.0)** | `(26,51,204,255)` / `(255,0,0,255)` / `(140,153,229,191)` | 无窗 | `0.791 ms` | 实证 Generic vs GPU 存在 `±1/255` 舍入差，支撑容差模板 |
| **Mode 5：真实 `GLRenderer` 未 Show `WS_POPUP`** | `IsReady=1`，`bg=(25,51,204)`，`rect=(255,0,0)`，`textInk=1642` | `0` | — | ✅ 实证生产链可无头运行抓帧 |

- **`CaptureBackbufferRGBA` 严格契约**：
  - 签名：`bool CaptureBackbufferRGBA(int& outW, int& outH, std::vector<std::uint8_t>& outRgba) const;`
  - **前置守卫与失败返回**：仅当 `m_ready && m_inFrame && m_w > 0 && m_h > 0` 时抓帧；任一不满足（含 `BeginFrame` 前或 `EndFrame` 后 `m_inFrame == false`），**强制置 `outW = 0; outH = 0; outRgba.clear();`、记录 `Logger::Warning` 并返回 `false`**。
  - **单/双缓冲分支**：`glReadBuffer(m_doubleBuffered ? GL_BACK : GL_FRONT);` → `glFinish()` → `glPixelStorei(GL_PACK_ALIGNMENT, 1)` → `glReadPixels(..., GL_RGBA, GL_UNSIGNED_BYTE, ...)` 并原地翻转为 top-down（row 0 = 顶边）；若 `glGetError() != GL_NO_ERROR` 清空输出返回 `false`（注：本函数会修改 `GL_READ_BUFFER` 与 `GL_PACK_ALIGNMENT` 全局状态，调用方不得依赖其原值）。
  - **无 GPU CI 守卫**：`!renderer.IsReady()` 时按 Phase 46 同款**方案 ⓐ**（`Logger::Warning` + 提前返回；本机 `0` 守卫触发）。
- **测试窗共享辅助**：在 `ECDI/src/Tests/TestWindowHelpers.h`（header-only `inline`，`CMakeLists.txt` 零改动）提供 `CreateHiddenGLScratchWindow`（`WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE` + `WS_POPUP` + `CS_OWNDC`，客户区严格等于 `(w, h)`）。

### 0.2 `O47-2`：CJK 工作集容量、并发灵敏度与 `kDefaultMaxPages = 8`（`:26-35, :42-45`）

| 场景（`Microsoft YaHei` + 141 几何槽；含 1px 间隙与 shelf 尾部碎片） | 累加槽位数 | 实测 `1024²` 页数 | `LA8` 显存 |
|---|---|---|---|
| `1000 CJK × 4` 态 + 几何（`16px` / `20px`） | `4,141` | `2` / `3` 页 | `~4` / `~6 MiB` |
| `2500 CJK × 4` 态 + 几何（`16px` / `20px`） | `10,141` | `4` / `5` 页 | `~8` / `~10 MiB` |
| `3500 CJK × 4` 态 + 几何（`16px`） | `14,141` | `5` 页 | `~10 MiB` |
| **并发 3 字号（`12/16/20px`）× 4 态**：`500` / `1000` / `1500` 字 + 几何 | `6,141` / `12,141` / `18,141` | **`3` / `4` / `6` 页** | `~6` / `~8` / `~12 MiB` |

> 注：表中页数含行/页尾碎片，不可用槽数简单线性除法外推每页理论容量。

- **定标定案**：`kDefaultMaxPages = 8`（上限 `16 MiB`；按需从 `1` 页 = `2 MiB` 起步，`Page 0` 保护常驻）。`SetMaxPagesForTests(std::size_t maxPages)` 传入 `< 2` 时**静默钳位至 `2` 并打一条 `Logger::Warning`**，确保永保 `Page 0` 保护 + 至少 `1` 个可轮转数据页。

### 0.3 `O47-3`：`DrawImage` 有效行哈希性能与 `2 MiB` 旁路定标（`:37-41, :47-49`）

| 尺寸（有效载荷） | 每帧建删+swizzle | 缓存命中+全行哈希 | 提速 | 纯哈希耗时 |
|---|---|---|---|---|
| `32×32`（`4 KB`） | `127.2 us` | `62.5 us` | `2.0×` | `1.2 us` |
| `256×256`（`256 KB`） | `725.4 us` | `125.9 us` | `5.8×` | `75.6 us` |
| `512×512`（`1024 KB`） | `1519.8 us` | `855.2 us` | `1.8×` | `305.4 us` |
| `1024×1024`（`4096 KB`）* | `3864.5 us` / `8072.8 us` | `1393.3 us` / `1911.2 us` | `2.8×` / `4.2×` | `1303.9 us` / `1178.6 us` |

> \* `1024×1024` 前值为 `o47_supp.cpp`（`:49`，`N=30`）读数，后值为 `o47_probe.cpp`（`:41`，`N=30`）读数（受当时 GPU/驱动频率波动影响，pureHash 稳定在 `~1.18–1.30 ms`）。

- **全哈希不可采样替代 + `2 MiB` 旁路定标**：因 `T47-5 ②` 要求 `1px` 篡改必 miss，禁止跳行采样；`<= 512²`（`<= 1 MiB`）纯哈希仅 `<= 0.31 ms`；将默认单图旁路阈值钉为 **`kDefaultMaxSingleCachedImageBytes = 2 * 1024 * 1024`（`2 MiB`）**（`>= 1024²` 大图直通瞬态上传即删，零纯哈希堆积且不挤占热缓存）；默认总上限 **`128` 项 / `32 MiB`**。

---

## §1 核心架构设计与不变式修正

### 1.1 `GLGlyphAtlas` 有界多页池、双轨分配与双路径淘汰

1. **`GlyphSlot`、`PageState` 与 `Texture()` 多页语义**：
   - `GlyphSlot` 新增 3 字段（`Empty()` 默认 **`texture = 0, pageIndex = 0, epoch = 0`**：与活跃页起始 `epoch = 1` 刻意不对称，使空槽必然不存活）：
     ```cpp
     unsigned int texture = 0;   // GLuint 所属页纹理
     std::uint32_t pageIndex = 0;
     std::uint32_t epoch = 0;    // 页从 1 起递增，0 恒无效
     ```
   - `PageState`（私有结构）：
     `{ unsigned int texture = 0; int shelfX = 0; int shelfY = 0; int shelfH = 0; std::size_t usedPixels = 0; std::uint32_t epoch = 1; std::uint64_t lastUsedFrame = 0; std::uint64_t lastResetFrame = 0; bool allocated = false; bool protectedPage = false; std::vector<GlyphKey> residentKeys; }`
   - **`Texture()` 语义**：保留既有 `GLGlyphAtlas::Texture()`，语义明确为**返回受保护的 `Page 0` 纹理（`m_pages[0].texture`）**，供 `BeginFrame` 帧首默认绑定与兼容旧调用方。
   - **`IsSlotAlive(const GlyphSlot& slot) const noexcept`**：
     ```cpp
     if (!slot.valid || slot.pageIndex >= m_pages.size()) return false;
     const PageState& p = m_pages[slot.pageIndex];
     if (!p.allocated || p.epoch != slot.epoch) return false;
     return !m_ready || (slot.texture != 0 && slot.texture == p.texture);   // 带纹理时额外校验句柄一致
     ```
     注：`Empty()`（`valid == false && epoch == 0`）在任何模式下均立即返回 `false`。
2. **`Allocate` vs `AllocatePaged` 分工与建页失败处理**：
   - **`Allocate(int w, int h)`**：定位为 `Page 0`（`m_pages[0]`）专属单页分配特化（不扩 `Page 1+`、不淘汰，兼容 `T26-7` 与 `T46-8`），成功时**同步填充 `texture / pageIndex=0 / epoch`** 使 `m_solid = Allocate(1, 1)` 自带合法页句柄。
   - **`AllocatePaged(int w, int h)`**：供 `GetOrCreate()` 与 `GLRenderer`（`CornerSlot` / `LineSlot` / `ArcSlot`）调用：试现有活跃页 → 仍有可用槽位且 `EnsurePageTexture(m_pages[idx])` 成功则新开页 → 全满触发淘汰/重置。`GetOrCreate()` **命中既有槽位时必须同步调用 `TouchSlot(cached->second)`** 更新 `lastUsedFrame`（避免热字形页因长期命中却未 miss 而被 LRU 误淘汰）；miss 分配成功后将 `key` 推入对应 `m_pages[slot.pageIndex].residentKeys`（确认采用 `std::vector<GlyphKey>` 控空间开销），**且不再将有效尺寸的临时失败写入永久负缓存**。
   - **`EnsurePageTexture` 复用与失败路径**：`Initialize()` 建 `Page 0` 纹理与 `AllocatePaged()` 建 `Page 1+` 纹理**统一复用 `EnsurePageTexture(PageState& page)`**（消除重复代码）。先清 `glGetError()`，执行 `glGenTextures(1, &tex)` 与 `glTexImage2D(1024², GL_LUMINANCE_ALPHA)`；若 `tex == 0 || glGetError() != GL_NO_ERROR`，若 `tex != 0` 则 `glDeleteTextures(1, &tex)` 清理，`page.allocated` 保持 **`false`**、`page.texture = 0`，记录 `Logger::Error`、递增 **`++m_allocationFailures`**，使 `AllocatePaged` 确定性返回 `Empty()`。
3. **两条淘汰/重置路径与 `O(页内槽数)` 清理**：
   - **`BeginFrame()` 调用时机**：`GLRenderer::BeginFrame` 在 `if (!m_ready) return;` **之后**判 `if (m_atlas) m_atlas->BeginFrame();`。headless 下 `GLGlyphAtlas::BeginFrame()` 仅递增 `m_currentFrame` 与执行逻辑页状态重置，零 GL 调用。
   - **`PurgePageSlots(std::uint32_t pageIdx)`**：遍历 `m_pages[pageIdx].residentKeys`，对 `m_slots` 中仍在对应 `pageIdx` 的条目执行按 key `erase` 并 `residentKeys.clear()`（复杂度 **`O(页内槽数)`**；**仅清理 `m_slots` 字形条目，几何三槽由 `GLRenderer` 侧 `IsSlotAlive` 惰性失效**）。
   - **路径 A — 帧边界 / 跨帧过期 Page-LRU 回收（释放纹理）**：
     - 触发：`BeginFrame()` 发现 `m_pendingBoundaryEvict == true` 且全池达上限；或 `AllocatePaged()` 遇全池满且存在 `!protectedPage && allocated && lastUsedFrame < m_currentFrame` 页。
     - 动作：选最小 `lastUsedFrame` 非保护页 `victim` → 若 `m_ready && victim.texture != 0` 调用 **`glDeleteTextures(1, &victim.texture)`** → `victim.texture = 0; victim.allocated = false;`、清 shelf、`++victim.epoch`、`PurgePageSlots(victim)`、`++m_evictionCount`，并**清零 `m_pendingBoundaryEvict = false`**（防每帧重复误淘汰）。`PageCount()` 返回当前 `allocated == true` 页数。
   - **路径 B — 极端单帧全池溢出就地重置（禁删句柄）**：
     - 触发：`AllocatePaged()` 内全部 `!protectedPage` 页均满足 `lastUsedFrame == m_currentFrame`。
     - 动作：若 `m_ready`，**先 `glFlush()`**（不调 `glDeleteTextures`），选最早 `lastResetFrame` 非保护页 `victim`，`glTexSubImage2D` 在原 `GLuint` 刷零、清 shelf、`victim.lastResetFrame = m_currentFrame`、`++victim.epoch`、`PurgePageSlots(victim)`、`++m_inFrameResetCount`、置 `m_pendingBoundaryEvict = true`。

### 1.2 `GLRenderer` 绑定同步纪律、缓存校验与 `DrawImage` LRU

1. **绑定追踪与 `const` 调整**：
   - 维护 `GLuint m_boundTexture = 0;` 与私有非 `const` 方法 **`void BindTexture(unsigned int tex)`**（仅当 `tex != 0 && m_boundTexture != tex` 时 `glBindTexture(GL_TEXTURE_2D, tex); m_boundTexture = tex;`）。
   - `GLRenderer::BeginFrame`（`m_ready` 就绪后）：先 `m_atlas->BeginFrame()`，随后 `m_boundTexture = 0; BindTexture(m_solid.texture != 0 ? m_solid.texture : m_atlas->Texture());`。
   - **去掉私有辅助 `TexturedQuad`、`TexturedQuadUV`、`SolidSegment` 的 `const` 修饰**（`Shade` 保留 `const`），三者及 `DrawRect` / `DrawRoundedRect` 内部均走 `BindTexture(slot.texture)` / `BindTexture(m_solid.texture)`。
   - **`DrawImage` 保守失效退出纪律**：进入有效绘制分支后**立即置 `m_boundTexture = 0;`（保守失效，不依赖各分支中途绑定推断）**；退出时统一 `glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);` 并调用 **`BindTexture(m_solid.texture)`**（因进入已置 `0 != m_solid.texture`，必执行真实 `glBindTexture` 并重同步 `m_boundTexture`）。
2. **三组几何槽缓存的 `IsSlotAlive` 校验与重建**：
   - `GLRenderer::CornerSlot` / `LineSlot` / `ArcSlot` 查表命中时：**先检验 `if (m_atlas->IsSlotAlive(cached->second))`** → 存活则调 **`m_atlas->TouchSlot(cached->second)`** 更新该页 `lastUsedFrame` 后返回；**已失效则立即擦除旧条目并用 `AllocatePaged` 重建上传**（与 `m_solid` `Page 0` 常驻共同守住红线 2 零错槽）。
3. **`DrawImage` LRU 缓存与析构清理**：
   - 键 `ImageCacheKey{ int width, int height, std::uint64_t hashLo, std::uint64_t hashHi }`（**不含 `stride`**），双 64-bit FNV-1a + SplitMix64 扫每行前 `width * 4` 有效字节；默认 `128` 项 / `32 MiB` / 单图旁路 `2 MiB`（`SetImageCacheLimitsForTests(n, b)` 同步按 `(std::min)(b / 2, kDefaultMaxSingleCachedImageBytes)` 或显式阈值调整）。
   - `~GLRenderer()` 顺序：`ClearImageCache()` → `m_atlas.reset()` → `wglMakeCurrent(nullptr)` → `wglDeleteContext` → `ReleaseDC`。

### 1.3 接口与观测缝增量清单

| 类 | 新增 / 调整接口 | 备注 |
|---|---|---|
| `GLGlyphAtlas` | `explicit GLGlyphAtlas(std::size_t maxPages = kDefaultMaxPages)`<br>`void SetMaxPagesForTests(std::size_t maxPages)`（`< 2` 钳至 `2` + Warning）<br>`void BeginFrame() noexcept`<br>`GlyphSlot AllocatePaged(int w, int h)`<br>`bool IsSlotAlive(const GlyphSlot& slot) const noexcept`<br>`void TouchSlot(const GlyphSlot& slot) noexcept`<br>`std::size_t PageCount() const noexcept`<br>`std::size_t EvictionCount() const noexcept`<br>`std::size_t InFrameResetCount() const noexcept` | `Allocate(w, h)` 单页行为与 `AllocationFailureCount()` / `IsFull()` 兼容不变；`Texture()` 返回 `Page 0` 纹理 |
| `GLRenderer` | `std::size_t AtlasPageCount() const noexcept` / `AtlasEvictionCount() const noexcept` / `AtlasInFrameResetCount() const noexcept`<br>`std::size_t ImageCacheHitCount() const noexcept` / `ImageCacheMissCount() const noexcept` / `ImageCacheEvictionCount() const noexcept` / `ImageCacheEntryCount() const noexcept` / `ImageCacheBytes() const noexcept`<br>`void SetAtlasMaxPagesForTests(std::size_t maxPages)`<br>`void SetImageCacheLimitsForTests(std::size_t maxEntries, std::size_t maxBytes)`<br>`bool CaptureBackbufferRGBA(int& outW, int& outH, std::vector<std::uint8_t>& outRgba) const`<br>私有去 `const`：`TexturedQuad`、`TexturedQuadUV`、`SolidSegment` | 全部位于 `ECDI/src/Render/*.h`；公共头文件 **`102` 零变更** |
| `TestWindowHelpers` | `ECDI/src/Tests/TestWindowHelpers.h`（header-only `inline CreateHiddenGLScratchWindow`） | `ECDI/src/Tests/` 内部测试头，零构建改动 |

---

## §2 验收容差矩阵与测试映射（`622 → 631`，+9 用例）

- **统一像素断言容差模板**：
  - 实心矩形 / 清屏 / 不透明 `DrawImage`：单通道误差 **`<= 2 / 255`**（吸收 GPU vs Generic DIB ±1/255 舍入）；
  - 透明混合 / 抗锯齿覆盖度（`DrawLine`、`DrawRoundedRect`、`DrawImage` 渐变）：单通道误差 **`<= 4 / 255`**；
  - 中文粗/斜（`T47-6`）：包围盒非背景墨迹像素 **`> 0`**、粗体总墨迹像素 **`>= 1.05×`** 常规、斜体上下质心右偏 **`>= 0.8 px`**。
- **Batch 0 / 1 / 2 与 9 条新用例**：
  1. `GLBackend.AtlasMultiPageExpansionAndWarmHit`（`T47-2` 无淘汰前提下 `PageCount() >= 2` 且暖帧 `MissCount`/`RasterizeCount` 增量 `== 0`，**Batch 0** -> `623`）
  2. `GLBackend.AtlasPageLruEvictionAndEpochSafety`（`T47-3 ①②` + `IsSlotAlive(GlyphSlot{}) == false`、`m_solid` 存活、旧槽淘汰后可重分配，**Batch 0** -> `624`）
  3. `GLBackend.AtlasInFrameResetProtection`（`T47-3 ③` 单帧超全池触发 `InFrameResetCount() >= 1` + 下帧 `BeginFrame` `m_pendingBoundaryEvict` 回收与清零，**Batch 0** -> `625`）
  4. `GLBackend.DrawImageCacheHitAndPaddingInvariance`（`T47-5 ①③⑤` 值拷贝复用 + `stride` padding 不敏感 + 渐变混合精度，**Batch 1** -> `626`）
  5. `GLBackend.DrawImageMutationAndLruEviction`（`T47-5 ②④` `1px` 篡改防脏图 + 字节/条目 LRU + `>2 MiB` 旁路保护，**Batch 1** -> `627`）
  6. `GLBackend.DrawArcAfterSinglePageSaturation`（`T47-4` 单页满后 `DrawArc` 跨页仍可画，闭合 `#75` ③，**Batch 1** -> `628`）
  7. `GLBackend.WglPixelEvictionAndSlotReconstruction`（`T47-3` E2E + `CornerSlot`/`LineSlot`/`ArcSlot` `IsSlotAlive` 重建防错槽 + `CaptureBackbufferRGBA` 负向时机失败断言 `P1-4`，**Batch 2** -> `629`）
  8. `GLBackend.WglChineseBoldObliquePixels`（`T47-6` 中文 `"宋体"`/`"微软雅黑"` 粗/斜真像素实绘，**Batch 2** -> `630`）
  9. `GLBackend.WglAuditContractPixelRegressions`（`T47-7` `#62`/`#68` `M16`、`M13`、`L14`、`H2`、`DrawLine` 像素机测，**Batch 2** -> `631`）

---

## 3. 修订记录

- **v1.0（2026-11-16）**：`O47-1`～`O47-3` 探针定标与初设方案落盘（5 文件 / `+9` 用例）。
- **v1.1（2026-11-16）**：吸收初轮 `2×P0+4×P1+5×P2` 与补充探针（帧边界删纹理、统一恢复、`SolidSegment` 去 `const`、`Page 0` 保护/`Allocate` 分工、`2 MiB` 旁路、容差模板、6 文件）。
- **v1.2（2026-11-16）**：吸收复审 `2×P0+3×P1+5×P2` 并恢复 §1.3 接口清单：`Texture()` 明确定位于 `Page 0` 与 `BeginFrame` 绑定；补 `EnsurePageTexture` 失败路径与 `residentKeys` 清理；补回三槽 `IsSlotAlive` 重建；`DrawImage` 保守置 `m_boundTexture=0`；统一 `docs/` `220→225` 与若干 P2。
- **v1.3（2026-11-16）**：吸收放行终审 `P1-1` 与全数 `P2`：`IsSlotAlive` 加强为 `slot.texture == p.texture` 句柄一致校验；`GetOrCreate` 命中补调 `TouchSlot`；`Initialize` 与 `AllocatePaged` 共用 `EnsurePageTexture`；明示 `residentKeys` 为 `vector` 且 `PurgePageSlots` 仅清字形槽；`CaptureBackbufferRGBA` 注记 GL 全局状态修改，转入详细设计。
