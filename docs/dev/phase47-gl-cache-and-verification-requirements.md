# Phase 47 需求与职责确认 · GL 后端渲染缓存与自动化像素验证

> **状态**：**v1.2（`v1.1` 终审通过并顺手收净极端帧内重置测试覆盖及定案措辞，`D47-1`～`D47-4` 全数拍板 `ⓐ` 锁定；进入 `O47-1`～`O47-3` 实机探针定标与初步设计）**
> **源码基线**：HEAD `36bb02d` + Phase 46 全部已落地改动（四构建链 `622/622` 通过）
> **规模基线**：测试 **622** 用例｜Public 头 **102**｜设计文档 **223 → 224**（本需求稿落盘值；注：`roadmap-deferred.md`、`docs/dev/README.md`、`README.md`、`README.zh-CN.md` 同步登记 Phase 47 立项）
> **对应延后账本**：[roadmap-deferred.md](roadmap-deferred.md) **`#95`**（解除 Phase 46 `D46-0` 挂账容量边界，关闭 **`#75` ③** 满图集圆弧丢失，并将 **`#68` / `#62`** GL 侧人工目视检查分批转为自动化像素测试）
> **门禁声明（提交前执行）**：`git add` 入索引 → `python tools/doc_lint.py --select L04,L05,L06,L08,L09,L11,L13,L14 --quiet` 须 **0 defect, 0 style** + 阳性对照（`README.md` 锚点制造 1 条 `L13` 后逐字节还原归零）。

---

## §0 核心红线（不可退让）

| # | 红线 | 校验方式 |
|---|---|---|
| **红线 1** | **默认 GDI 链、公共头文件与既有测试零退化（或显式 `CB-*` 契约登记）**：`ECDI/include/ECDI/**` **102 个公共头**维持只读不扩增或零破坏（首选零改动）；`GDIBackend` / `GDITextMeasurer` 行为逐位不变；`GLRenderer` 初始化失败仍保持不静默回退 GDI 且仍验证更新区域防定时器饿死（`IsReady() == false`）；对既有 `GLBackend.AtlasShelfAllocation`（`T26-7`）与 `FontWeight.GlyphAtlasFullObservability`（`T46-8`）或按 `D47-1` 保持单页 `Allocate` 纯逻辑契约不变，或明确登记契约演进条款，严禁静默改红 | `ecdi_tests` 全绿 + `git diff -- ECDI/include/` 检查 |
| **红线 2** | **GPU 纹理生命周期与 WGL Context 同生死、零悬空句柄 / UV 跨页错槽**：一切 `GLuint` 纹理创建、上传、轮转清除与删除必须在**创建线程且仍 current 的 WGL context 有效期内**执行；`~GLRenderer()` 必须在 `wglMakeCurrent(..., nullptr)` / `wglDeleteContext` **之前**销毁全部图集页纹理与 `DrawImage` 缓存纹理（`GLRenderer.cpp:49-63`）。**淘汰时机与帧内一致性**：常规 Page-LRU 页回收只在帧边界（`BeginFrame` 入口，依各页 `lastUsedFrame`）执行；极端单帧超全池容量不得不帧内重置时，严禁 `glDeleteTextures` 制造同帧悬空句柄，须先确保先前图元消费完成（同上下文队列有序 / `glFlush`）再就地清空未占/最老非保护页并递增该页 `epoch`。一旦触发淘汰，`GLGlyphAtlas::m_slots` 与 `GLRenderer` 侧全部长寿缓存（`m_solid`、`m_cornerSlots`、`m_lineSlots`、`m_arcSlots`）**必须与淘汰同步失效或绑定代际校验**，绝对禁止携带旧页/旧代际 `(x, y)` 坐标画出脏图/错字 | 页轮转/淘汰压力测试 + `~GLRenderer()` 上下文前释放审计 + 错槽回归像素测试 |
| **红线 3** | **零新第三方依赖与头文件零平台/GL 污染**：不引入 GLEW / GLAD / stb 等外部库，直接消费仓内已有 Win32 SDK OpenGL 1.1（`ECDI PUBLIC opengl32`，`CMakeLists.txt:119-121`）；`GLGlyphAtlas.h` 与公共头继续严守「零 `<Windows.h>` / 零 `<GL/gl.h>`」不变量（`GLGlyphAtlas.h:32`），`GL_*` 与 WGL 仅出现在 `.cpp` 或测试内部单元 | `git diff -- CMakeLists.txt third_party/` 与头文件 include 检查 |

---

## §1 输入来源、历史先例与当前代码事实矩阵

### 1.1 问题输入与历史欠账

Phase 46 为 GL/FreeType 字体栈补齐了合成粗体、合成斜体与中文族名解析，同时按 `D46-0` ⓐ 在图集方面明确留下容量耦合边界。面向生产级 GL 后端，当前仍有三大直接阻塞与技术债务集中在 [roadmap-deferred.md](roadmap-deferred.md) **`#95`**：

1. **单页 `1024×1024` 图集无扩容无淘汰导致字形与圆弧永久消失（`D46-0` / `#75` ③）**：
   - Phase 46 启用 4 态组合后，单族最多需要 4× 图集槽位（例如大号或多尺寸中文混合界面突破单页 `1024²` ≈ 2 MB）。目前图集满后不但所有新字形永久跳过绘制，且 `GetOrCreate` 把 `!slot.valid` **写进永久负缓存**，即使后续只渲染一小部分文字也永远不再恢复；
   - `DrawArc` 在图集满时先尝试二值化槽位、再失败则跳过绘制并打 Warning（即 `#75` ③ 登记的「满图集圆弧丢失」）。
   - 更隐蔽的架构耦合是：**图集不仅存字形，还被 `GLRenderer` 侧 `m_solid`、`m_cornerSlots`、`m_lineSlots`、`m_arcSlots` 四组独立缓存长寿持有 UV 坐标**；如果图集直接原地覆写/清空而未通知 `GLRenderer` 或未校验代际，**圆角/线段/圆弧与实心填充 (`m_solid`) 会立刻贴上别人家字的像素碎块**。
2. **`DrawImage` 每帧现建/现删纹理导致 GPU 驱动频繁分配、CPU 通道重排重复开销**：
   - `GLRenderer::DrawImage` 每帧对每张图执行 `rgba` 临时 vector 分配、逐像素 BGRA→RGBA 重排、`glGenTextures`、`glTexImage2D`、绘制完立即 `glDeleteTextures`。静态图标、头像或大图预览每帧滚动重绘都在反复建删纹理，而 `Image` 本身是无 ID 的值拷贝结构体（进出 `DrawImageCommand` 地址每帧变化）。
3. **GL 后端缺乏真实 WGL / 像素级自动化回归保障（`#68` / `#62` 遗留转机测缺口）**：
   - 当前 `GLBackendTests.cpp` 仅有 3 条不创建 WGL context 的逻辑/空句柄用例。所有真正进 GPU 光栅化的链路（文字、合成粗斜、中文族名、圆角 `M16` 防重叠、线段、圆弧、`M13` 退化裁剪、`L14` 负宽高、`DrawImage`、`L10` 降级）仍长期停留在 `ModelProbe --gl` 人工目视阶段，急需稳定可复现、无弹窗干扰的 WGL 离屏像素测试夹具。

### 1.2 源码事实矩阵（对当前源码逐行核验）

| # | 位置（相对仓根 `d:\Project\ECDI\`） | 核实事实 |
|---|---|---|
| **K47-1** | `ECDI/src/Render/GLGlyphAtlas.h:16-103` · `GLGlyphAtlas.cpp:31-177` | `GLGlyphAtlas` 当前仅持单张 `m_texture`（`kSize = 1024`，`GL_LUMINANCE_ALPHA`，`GL_NEAREST`，`GL_REPEAT` 靠 1px 间隙防串色）。`Allocate()` 单调推进 `m_shelfX/Y/H`，满时递增 `m_allocationFailures`、置 `m_fullWarned = true` 打一次 Warning 并返回 `valid = false`。`GetOrCreate()`（`:174-175`）将 `!slot.valid` 一并写入 `m_slots`，导致失败结果永久负缓存。 |
| **K47-2** | `ECDI/src/Render/GLRenderer.h:121, 135-140` · `GLRenderer.cpp:129-135, 214, 291-300, 318-338, 374-388, 440-462, 800-897` | **共有 5 类消费者共用 `m_atlas`**：① `m_solid`（`Initialize` 分配 1×1，`DrawRect`/`DrawRoundedRect` 三带与降级方角/`SolidSegment`/`DrawFocusRect` 使用）；② `m_cornerSlots`（`CornerSlot`）；③ `m_lineSlots`（`LineSlot`）；④ `m_arcSlots`（`ArcSlot`）；⑤ `GLGlyphAtlas::m_slots`（`DrawText`）。`BeginFrame`（`:214`）每帧只在开头 `glBindTexture(GL_TEXTURE_2D, m_atlas->Texture())` 一次，除 `DrawImage` 临时切换后立即恢复外，其余所有 quad 均默认只绑定单张图集纹理。 |
| **K47-3** | `ECDI/src/Tests/GLBackendTests.cpp:42-99` · `FontWeightTests.cpp:320-350` | 存量 `GLBackend.AtlasShelfAllocation`（`T26-7`）与 `FontWeight.GlyphAtlasFullObservability`（`T46-8`）直接构造裸 `GLGlyphAtlas`（不调用 `Initialize()`，无 GL context）并向 `Allocate(1024, 16)` / `Allocate(255, 255)` 注入固定次数，**显式断言单页 `1024×1024` 满容后 `slot.valid == false && IsFull() && AllocationFailureCount() == fail`**。 |
| **K47-4** | `ECDI/include/ECDI/Core/Image.h:16-22` · `ECDI/src/Render/PaintContext.cpp:66-70` · `Renderer.cpp:103-107` | `Image` 为值语义 `{int width, int height, int stride, std::vector<uint8_t> pixels}`（32bpp top-down premultiplied BGRA），无唯一 ID/版本号；`PaintContext::DrawImage` 将 `image` 按值拷贝入 `DrawImageCommand`，故同图跨帧传递时 `image.pixels.data()` 指针地址不同；`RendererTests.cpp:200` 更存在「绘制后修改原 `Image` 像素」用例模式。 |
| **K47-5** | `ECDI/src/Render/GLRenderer.cpp:465-524` | `GLRenderer::DrawImage` 已通过 `dest <= 0`（L14）与 `BackendContract::ImageIsDrawable(image)`（#62 H3）校验，之后**每帧**分配 `rgba` buffer、转置 BGRA→RGBA、`glGenTextures`、`glTexImage2D`（`GL_NEAREST`）、premultiplied 混合（`GL_ONE, GL_ONE_MINUS_SRC_ALPHA`）发射 `Snap` quad，并立即 `glDeleteTextures`、恢复 `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA` 与 `m_atlas->Texture()` 绑定。 |
| **K47-6** | `ECDI/src/Render/GLRenderer.cpp:66-137, 168-200, 715-743` | `GLRenderer::Initialize` 强依赖 `Win32RenderContext` 的 `HWND`，固定请求 `PFD_DRAW_TO_WINDOW \| PFD_SUPPORT_OPENGL \| PFD_DOUBLEBUFFER`；`EndFrame()` 若 `m_doubleBuffered` 为真会执行 `SwapBuffers(m_dc)`（多数驱动上会丢弃 backbuffer 内容），且 Windows DWM 对完全隐藏（`SW_HIDE`）窗口可能因 Pixel Ownership Test 导致默认帧缓冲 `glReadPixels` 读黑——必须经探针实测验证。 |
| **K47-7** | `ECDI/src/Tests/GLBackendTests.cpp:24-27` · `PlatformQuickfixTests.cpp:129-195` · `CMakeLists.txt:119-121` | `ECDI` 库已 `PUBLIC opengl32` 链接（Windows SDK OpenGL 1.1，`wglCreateContext` / `glReadPixels` 现成可用）。`GLBackendTests` 尚无任何创建真实 WGL context 并读回 framebuffer 像素校验渲染结果的用例。 |
| **K47-8** | `ECDI/src/Render/GLRenderer.cpp:27-30, 526-550, 899-929` | `GLRenderer::PushClip`（`:537`）沿用 `lround`（已在 `#75` ① 记为与 GDI `floor` 的已知 1px 残差，配合 `DrawFocusRect` 口径不动），`ApplyScissor`（`:899-929`）已补齐 `#62 M13` 负尺寸钳零守卫。 |
| **K47-9** | `ECDI/src/Render/GLRenderer.h:84, 133` | `GLRenderer` 当前仅暴露 `IsReady()` 与 `AtlasMissCount()`；缺少图集页数/淘汰统计、`DrawImage` 纹理缓存命中/未命中/淘汰观测缝，以及可控像素读回观察面。 |

---

## §2 方案选型与核心架构决策（闸门 `D47-1`～`D47-4`）

### 2.1 `GLGlyphAtlas` 扩容与淘汰策略、槽位一致性和存量测试兼容性（`D47-1`）

| 候选方案 | 核心设计 | 优劣与约束评估 |
|---|---|---|
| **ⓐ 有界多页分页池 + Page-LRU 整页淘汰与代际防护（倾向推荐）** | 将 `GLGlyphAtlas` 由固定单页演进为**有界多页池**（初始 `1` 页 `1024²`，按需追加至上限 `kMaxPages` 经 `O47-2` 定标如 `4`～`8` 页 = ~8～16 MB）。**默认保护 `Page 0`**（含 `m_solid` 与首批常驻槽位）不参与普通数据页 LRU 淘汰。`GlyphSlot` 携带所属页纹理/页号与**页代际 `epoch`**（并由 `GLGlyphAtlas` / `GLRenderer` 提供同页/换页 `glBindTexture` 去重）。**淘汰时机**：常规 Page-LRU 淘汰**只在帧边界（`BeginFrame` 入口，按各页 `lastUsedFrame`）执行**，避免帧内刚绘即擦导致抖动；仅当极端单帧突发需求超过全池 `kMaxPages` 容量时，禁止删除 `GLuint` 产生悬空句柄，须按流水线有序重用（`glFlush` 后就地重置非保护页纹理内容，不删 `GLuint`）。无论帧间还是极端帧内淘汰，均递增该页 `epoch`、从 `m_slots` 精准清理旧页与失败负缓存条、`GLRenderer` 通过 `epoch`/页有效检查同步失效对应 `m_cornerSlots` / `m_lineSlots` / `m_arcSlots` 条目后再重新分配上传。 | ① 中等规模超单页场景（如 Phase 46 四态中文界面、大量圆弧/线段）**完全不触发淘汰**，warm 帧维持 `miss == 0`、`rasterizeCount == 0`；② 超出 `kMaxPages` 时有硬显存上限保护且绝不永久丢字、绝不因旧 `CornerSlot`/`ArcSlot` UV 错指已回收页而花屏、绝无帧内悬空句柄；③ **硬性契约兼容承诺**：保留单页无头 `Allocate(w, h)` 原签名与单页满容语义完全不变（100% 兼容 `T26-7` 与 `T46-8`，Zero `CB` 契约破坏），多页扩容与淘汰完全通过新的页池分配/管理路径承载。 |
| **ⓑ 纯单页整页清空重置（Single-Page Generation Reset）** | 维持单张 `1024²`，满时整页清空并递增 `generation` 使上下游缓存同步清空重建。 | 若活跃工作集仅略大于 `1024²`（如 3000+ 汉字四态），每一帧都会清空并全量重栅格化，退化为持续抖动（thrashing），丧失缓存意义。**不推荐为主方案**。 |
| **ⓒ 单页 Free-List 二维块合并 + 逐块 LRU** | 在单张 `1024²` 内维护动态空闲矩形列表做原位回收。 | 无法扩大物理工作集上限，且圆角/圆弧/大小字形混排碎片率极高、实现复杂易出边界缺陷。**不推荐**。 |

- **签字状态（`D47-1`）**：✅ **已于 2026-11-16 由用户拍板 ⓐ**（有界多页分页池 + `Page 0` 保护 + 帧边界 Page-LRU 整页淘汰/极端帧内就地重置不删句柄 + `epoch` 防错槽 + 硬性保持单页无头 `Allocate(w, h)` 原契约不变）。

### 2.2 `GLRenderer::DrawImage` 纹理缓存策略与显存上界（`D47-2`）

| 候选方案 | 核心设计 | 评估 |
|---|---|---|
| **ⓐ 零公共头改动：128-bit 行有效载荷内容哈希键（键不含 `stride`）+ 双上界 LRU 缓存（倾向推荐）** | 保持 `Image.h`（`102` 公共头）零改动。以 `(width, height, hash128(active_rows))` 为键——**明示：`stride` 本身不入键**（上传纹理时已按 `row = 0..height-1` 提取紧凑 `width * 4` RGBA，仅对每行前 `width * 4` 有效字节做双 64-bit 哈希，跳过 `stride - width * 4` 填充尾字节；因此同宽高、不同 `stride`、同有效像素的图像在键域等价并直接命中复用），缓存 `{GLuint texture, byteSize, lastUsedTick}`。命中直接绑定复用纹理并跳过 CPU RGBA 转置与 `glGenTextures/glTexImage2D/glDeleteTextures`。同时设条目上限（如 `128` 项）与字节上限（如 `32 MB`），并设置超大单图直通旁路（避免单张 4K 大图冲刷挤出全部小图标）。在 LRU 淘汰和 `~GLRenderer()`（context 删除前）调用 `glDeleteTextures`。 | ① 对 `PaintContext::DrawImage` 值拷贝、`RendererTests` 改图等全链路模式 100% 免疫陈旧指针与脏图，且与 `T47-5 ③` 同效；② **零 API / 公共头文件变更**；③ `O47-3` 验证热路径 hash 开销远小于原每帧建删纹理与转置拷贝成本。 |
| **ⓑ 扩展 `Image` 公共结构体加 `cacheId` 或 `shared_ptr` 不可变存储** | 需改 `ECDI/include/ECDI/Core/Image.h`，且调用方直接改写 `img.pixels` 会造成 ID 失同步风险。 | 侵入公共头且存在脆弱状态，**不建议**。 |

- **签字状态（`D47-2`）**：✅ **已于 2026-11-16 由用户拍板 ⓐ**（`Image.h` 零改动 + `(width, height, hash128)`，`stride` 不入键 + 条数/字节双上界 LRU + 超大图直通旁路）。

### 2.3 WGL 自动化像素验证夹具与读回机制（`D47-3`）

- **问题**：`GLRenderer::Initialize` 当前仅支持带 `HWND` 的窗口双缓冲上下文；`EndFrame` 内触发 `SwapBuffers` 后 backbuffer 在部分显卡驱动下不可靠；纯隐藏 `WS_POPUP` 窗口在 DWM 开启环境下可能无法通过 Pixel Ownership Test 导致 `glReadPixels` 读黑；同时自动化测试在 CLion / CI 无界面或并发环境中不应抢占用户桌面焦点。
- **候选项**：
  - **ⓐ 真 `GLRenderer` 生产链 + 经 `O47-1` 实机定标的可靠无抢焦离屏/抓帧载具（倾向推荐）**：
    - `GLRenderer` 侧可增加内部只读帧缓冲抓帧/后处理观察能力（如仅供测试或包内使用的 `CaptureFramebufferRGBA(...)` / `EndFrame` 前 `glReadPixels` 读回缝，或视 `O47-1` 实测决定是否补充可测试的无双缓冲/FBO/离屏 DIB-WGL 支持）保证测试跑通真实 `GLRenderer::BeginFrame -> Draw* -> EndFrame`；
    - 经 `O47-1` 在本机实测对比「无激活 `WS_POPUP` 窗 + `SwapBuffers` 前读 `GL_BACK`」「FBO (`GL_EXT/ARB_framebuffer_object`)」「`PFD_DRAW_TO_BITMAP` 软光栅内存 DIB」三者的实测可靠性与环境要求后，由初步设计锁定首选主路径（以及无硬件驱动时的确定性守卫策略，沿 Phase 46 方案 ⓐ 无静默假绿纪律）。
  - **ⓑ 脱离 `GLRenderer` 在测试里手写另一套 GL 渲染检查**：测不到真实 `GLRenderer`（`Snap`、`TexturedQuad`、`DrawImage`、`m_atlas` 多页切换）的真实行为，**否决**。
- **签字状态（`D47-3`）**：✅ **已于 2026-11-16 由用户拍板 ⓐ**（真 `GLRenderer` 生产链 + 测试读回缝，经 `O47-1` 实测定标后在初设选定最稳且无抢焦载具）。

### 2.4 `#68` / `#62` / `#75` 自动化像素覆盖边界（`D47-4`）

- **候选项**：
  - **ⓐ 核心功能像素断言 + `#62` 关键修复与大容量淘汰全覆盖，允许字体 AA/驱动舍入工程容差（倾向推荐）**：
    - 重点覆盖：① 多页扩容 + 满池 Page-LRU 淘汰重绘与代际不串图；② `#75` ③ 满图集下 `DrawArc` 仍可绘制；③ `DrawImage` 缓存命中/改像素无碰撞/LRU 淘汰 + premultiplied alpha 读回精确度；④ `DrawText` 中文族名/合成粗体/合成斜体真实像素墨迹边界；⑤ `#62` (`M16` 奇数圆角无中心二次混合、`M13` 负高 `PushClip` 隔离、`L14` 负尺寸 `DrawImage` 零写入、`H2` `.notdef` 推进不重叠、`DrawLine` 实画）。
    - 非目标：**不做 GL 与 GDI 之间亚像素/字体点阵逐比特相等比对**；**不重写 `#75` ① `PushClip` `lround` vs `floor` 口径**；**不在此 Phase 做 `Direct3D`/`GL3+ Core Profile`/shader 管线重写**。
  - **ⓑ 强制要求跨 GPU 厂牌与 GDI 0 像素误差比对**：字体微调与驱动三角光栅舍入差异必然导致虚假红灯。**否决**。
- **签字状态（`D47-4`）**：✅ **已于 2026-11-16 由用户拍板 ⓐ**（扩容/淘汰/图像缓存/中文粗斜/`#62`/`#75`③ 转机测；允许 AA/驱动舍入容差，保留 `#75`① 口径）。

---

## §3 待勘察项（`O47-1`～`O47-3`，作为进初步设计的事实门槛）

| # | 勘察主题 | 探针目标与输出读数 |
|---|---|---|
| **O47-1** | **Windows WGL 像素读回（`glReadPixels`）稳定性与无抢焦载体探针** | 在当前实机（MinGW / Clang、DWM 开启）实测并归档 4 种方式 `glClearColor` + 实心矩形/文本绘制后的 `glReadPixels` 读回一致性、耗时及焦距影响：① 未 `ShowWindow` 隐藏 `WS_POPUP` + `SwapBuffers` 前 `glReadPixels(GL_BACK)`；② `SW_SHOWNOACTIVATE` 无激活窗口（或屏外坐标）+ `SwapBuffers` 前 `glReadPixels(GL_BACK)`；③ `PFD_DRAW_TO_BITMAP` 32bpp 内存 DIB + Microsoft Generic OpenGL 1.1（注记其 `GL_LUMINANCE_ALPHA` 与 `glReadPixels` 精度相对硬件 ICD 的潜在细微差异，支撑 `D47-4 ⓐ` 相对断言+容差口径）；④ 探查当前 ICD 对 `GL_EXT/ARB_framebuffer_object` 与 `WGL_ARB_pbuffer` 的支持状况。 |
| **O47-2** | **CJK 多态工作集容量 & 图集页上限定标** | **以保护 `Page 0`（常驻 `m_solid` 与首批几何/热槽）不参与普通数据页 LRU 淘汰为默认前提**，测算 16/20/24px 常用中文字符集（1000 / 2500 / 3500 字 × 常规+粗+斜+粗斜 4 态）+ 典型几何掩码（圆角/线段/圆弧）的实际像素占用与单页 `1024²` 可容纳字形数，定标 `kMaxPages`（如 `4` 页 8MB vs `5～8` 页含保护余量）。 |
| **O47-3** | **`DrawImage` 128-bit 行有效哈希性能 vs 每帧建删纹理基准** | 对 `32×32`、`256×256`、`1024×1024` 图像对比：① 每帧 `vector rgba + BGRA→RGBA + glGenTextures + glTexImage2D + glDeleteTextures` 实测帧耗；② 128-bit 有效行哈希 + 命中绑定复用耗；验证含 `stride > width*4` padding 尾字节脏值下的防假 miss 与缓存上限设置。 |

---

## §4 范围、非目标与初步规模预算

### 4.1 明确纳入范围（In Scope）

1. **`GLGlyphAtlas` 有界扩容与淘汰**：按 `D47-1 ⓐ` 实现有界多页分页管理与帧边界 Page-LRU 淘汰机制（含极端单帧超全池时有序就地重置不删句柄），清除旧负缓存死锁并递增页代际 `epoch`，解决单页饱和后长期丢字与 **`#75` ③ 满图集圆弧丢失**；硬性保留单页无头 `Allocate(w, h)` 原契约不变（100% 兼容 `T26-7` / `T46-8`）；新增页数/帧间淘汰与帧内重置等只读观测缝。
2. **`GLRenderer` 槽位代际同步 & 多页纹理绑定**：改造 `TexturedQuad`/`TexturedQuadUV`/`SolidSegment` 及 `m_solid`/`m_cornerSlots`/`m_lineSlots`/`m_arcSlots` 以适配多页纹理按需切换与页淘汰后的槽位安全重建（零 UV 错图）。
3. **`GLRenderer::DrawImage` 纹理缓存**：按 `D47-2` 实现有效载荷哈希 + 容量/字节双上界 LRU 缓存与大图直通旁路，确保与 WGL context 同寿命释放及修改 `Image` 内容后不命中脏纹理，新增命中/未命中/淘汰观测缝。
4. **WGL 自动化像素测试夹具与验证集（`GLBackendTests`）**：按 `D47-3` / `D47-4` 建成真实 WGL 绘制 + `glReadPixels` 端到端测试套，将 `#68` / `#62` 核心 GL 校验及扩容/淘汰/图像缓存回归纳入自动化机测。

### 4.2 明确非目标（Out of Scope）

1. **升级现代 Core Profile GL / 着色器渲染管线（VBO / Shader）**：维持当前固定管线 GL 1.1 最小依赖面（兼容 `opengl32.lib` 四工具链与软件光栅）。
2. **`DirectWrite` / GPU 硬件解码 / SVG / 异步纹理后台线程流式上传**：`GLRenderer` 保持在单一 UI 线程内调用；半透明窗内 `DrawImage` 已知透明混合局限（`#56`）不在本 Phase 改写。
3. **`#75` ① GL `PushClip` `lround` vs GDI `floor` 口径统一 与 `#75` ② `DrawLine` 独立路径**：维持现网记账不强动 `PushClip`/`DrawFocusRect` 耦合关系；GL 与 GDI 间亚像素级字体/三角形栅格逐比特相等不做强制要求。
4. **修改公共头 API**：`ECDI/include/ECDI/**` **102 个公共头 0 改动**（已由 `D47-2 ⓐ` 锁定）。

### 4.3 规模与影响预算（初步估算，待初步/详细设计精算锁定）

| 维度 | 预算 | 备注 |
|---|---|---|
| **公共头文件（`include/ECDI/**`）** | **102 → 102**（+0） | `D47-2 ⓐ` 已锁定公共头与公共 API 零改动 |
| **预期源码改动范围** | `GLGlyphAtlas.h/.cpp` · `GLRenderer.h/.cpp` · `GLBackendTests.cpp`（及按 `O47-1` 需扩展内部只读观测/测试抓帧接口时少量关联内部测试/构建文件） | 严控在 `Render/` 与 `Tests/` 内部文件 |
| **CMake / 第三方** | **0 新增第三方依赖**（`opengl32` 早已 `PUBLIC` 链接于 `ECDI`） | 保持 MSVC / Clang / ClangCL / MinGW 四链零新增外部工具链要求 |
| **测试增量预估** | 约 **+7～12 条**（`622 → ~629..634`，详细设计阶段定锚） | 覆盖多页扩容、LRU 淘汰防错图、`#75` ③ 圆弧恢复、`DrawImage` 缓存命中/防碰撞/淘汰、`DrawText` 粗斜中文像素与 `#62` 转机测（按一族一用例多断言收敛，或按子项适当展开仍落在本预算内） |
| **设计文档增量** | **223 → 224**（本需求稿为第 `224` 篇；后续初设、详设依例单计） | — |

---

## §5 验收标准草案（`T47-*`）

> 以下为需求阶段验收轮廓；具体像素容差、工作集字数与页上限定值于 `O47-1`～`O47-3` 实测归档后在初步/详细设计阶段钉死。

| 用例编号 | 检验目标 | 核心断言 |
|---|---|---|
| **T47-1** | 存量图集单页/初始化失败回归守卫 | 保持既有 `GLBackend.AtlasShelfAllocation`、`GLBackend.InitFailureIsNoOp`、`GLBackend.NegativeDimensions`、`FontWeight.GlyphAtlasFullObservability` 契约全绿。 |
| **T47-2** | `GLGlyphAtlas` 多页扩容与暖帧收敛 | **在固定测试窗口 DPI 环境（防 `BeginFrame` 读入动态 DPI 改变 `pixelSize` 键）下**，注入超过单页 `1024²`（如 > 1 页等效槽位、<= `kMaxPages`）的 CJK 多字号/4 态字形混合请求：第二页自动创建（`PageCount() >= 2`）、全部字形获得合法 `valid == true` 槽位、`EvictionCount() == 0`，第二帧复绘 `MissCount()` 与 `FontEngine::RasterizeCount()` 增量均 `== 0`。 |
| **T47-3** | 满页上限 `kMaxPages` 触发 Page-LRU 淘汰、极端帧内就地重置与代际防错槽 | 覆盖两条淘汰路径与代际一致性：① **帧边界 Page-LRU**：跨帧注入超出 `kMaxPages * 1024²` 的大号块与几何槽位触发 `BeginFrame` Page-LRU 淘汰（`EvictionCount() >= 1`），被淘汰页原持有的字形随后再次请求时能重新分配上传画回非空白像素（**打破原永久负缓存**）；② **槽位防错槽**：`m_solid`（`Page 0` 保护）与在先缓存的 `CornerSlot`/`LineSlot`/`ArcSlot` 在页淘汰后仍画出正确纯色/圆角/线条（绝不因旧 UV 指向新字形而出现像素花块）；③ **极端帧内就地重置路径**：单帧注入超过 `kMaxPages * 1024²` 槽位强制触发帧内就地重置（不删 `GLuint`、配区分观测缝如 `InFrameResetCount() >= 1`），同帧先绘制图元与重置后新图元 `glReadPixels` 均正确、无悬空纹理错误。 |
| **T47-4** | `#75` ③ 满图集下 `DrawArc` 可画性验证 | 在逼近/越过单页 `1024²` 满容后继续调用 `DrawArc` 并经 `glReadPixels` 回读弧路径像素：弧线像素按预期覆盖画布、不再因单页耗尽整弧消失。 |
| **T47-5** | `GLRenderer::DrawImage` 纹理缓存命中、LRU 淘汰与修改防脏图 | ① 同 `Image`（模拟 `PaintContext` 跨帧值拷贝：`pixels.data()` 地址不同）连续绘制两帧：`ImageCacheMissCount()` 仅第 1 帧 `+1`，第 2 帧 `ImageCacheHitCount() += 1`；② 同宽高同 `stride` 仅修改某像素颜色后重绘：`MissCount` 递增且 `glReadPixels` 读回为新颜色（不串旧图）；③ 改变 `stride` padding 尾字节随机垃圾不影响有效载荷命中；④ 连续注入超出条目/字节预算的多图触发 LRU 淘汰后重绘仍正确，且 `~GLRenderer()` 后资源干净销毁；⑤ premultiplied BGRA 半透明梯度混合读回精度在容差内。 |
| **T47-6** | WGL 端到端真实像素读回：中文族名 + 合成粗/斜 | 经 `O47-1` 确定的无抢焦 WGL 夹具运行完整 `GLRenderer` 渲染帧：分别用 `"宋体"` / `"微软雅黑"` 的常规、合成粗体、`italic=true` 合成斜体绘制中文/ASCII 串，读回 framebuffer 验证：① 非背景墨迹像素数符合预期位置包围盒；② 粗体总覆盖墨量显著大于常规态；③ 斜体顶部行相比底部行呈右倾偏移且不丢字。 |
| **T47-7** | `#62` / `#68` GL 人工目视项转自动化像素回归 | 真实 WGL 帧缓冲回读验证：① `DrawRoundedRect` 半透明奇数边长矩形（`M16`）内部中心带交界无二次混合变深；② `PushClip` 负高区域（`M13`）后绘制矩形不向父盒漏画像素（严格 0 写入）；③ 负宽高 `DrawImage`（`L14`）不画出镜像鬼影（0 像素改写）；④ `DrawText` 遇 `.notdef` 码点后后续字符水平笔位正常推进不重叠（`H2`）；⑤ `DrawLine` 抗锯齿覆盖线与背景融合颜色正确。 |

---

## §6 闸门签字页（全数闸门已于 2026-11-16 由用户签字拍板 ⓐ）

| 闸门 | 议题 | 推荐倾向 | 用户签字结论 |
|---|---|---|---|
| **D47-1** | `GLGlyphAtlas` 扩容淘汰策略与存量兼容 | **ⓐ**（有界多页池 `kMaxPages` + 保护 `Page 0` + 帧边界 Page-LRU 整页淘汰/极端帧内就地重置不删句柄 + `epoch` 防错槽；硬性保留单页 `Allocate` 原契约兼容 `T26-7/T46-8`） | ✅ **已拍板 ⓐ** |
| **D47-2** | `GLRenderer::DrawImage` 缓存键与上界 | **ⓐ**（零公共头改动：`(width, height, hash128)`，`stride` 不入键 + 条数/字节双上界 LRU + 超大图旁路） | ✅ **已拍板 ⓐ** |
| **D47-3** | WGL 自动化像素验证夹具路线 | **ⓐ**（`GLRenderer` 真生产链 + 测试读回缝，经 `O47-1` 实测 DWM 隐藏/无激活窗 vs `FBO` vs `DIB-GL` 后在初设选定最稳载具） | ✅ **已拍板 ⓐ** |
| **D47-4** | 像素断言覆盖与容差边界 | **ⓐ**（扩容/淘汰/图像缓存/中文粗斜/`#62`/`#75`③ 转机测；允许 AA/驱动舍入容差，不强制 GL=GDI 逐比特相等，保留 `#75`① 口径） | ✅ **已拍板 ⓐ** |

---

## 7. 修订记录

- **v1.0（2026-11-15）**：Phase 47（`#95`）需求与职责确认初稿落盘：整理 `K47-1`～`K47-9` 当前代码事实矩阵（含 `GLGlyphAtlas` 五类消费者共页 UV 耦合与 `Image` 值拷贝地址漂移关键约束），提出 `D47-1`～`D47-4` 决策闸门与 `O47-1`～`O47-3` 探针门槛，制定初步预算与 `T47-1`～`T47-7` 验收轮廓。
- **v1.1（2026-11-16）**：吸收评审意见补钉三项核心语义与四项细化并记录用户全数拍板 **`D47-1`～`D47-4 = ⓐ`**：① 明确 `D47-1 ⓐ` **淘汰时机与帧内一致性**（常规只在 `BeginFrame` 帧边界按 `lastUsedFrame` 淘汰，极端单帧超全池时严禁 `glDeleteTextures` 制造悬空句柄，须 `glFlush` 有序就地重置并递增 `epoch`）；② 明确 `D47-2 ⓐ` 哈希键 **`stride` 不入键**；③ 删去 `D47-1 ⓐ` 兼容分支「亦或」歧义，硬性承诺保留单页 `Allocate` 原契约零破坏；④ 以保护 `Page 0` 为 `O47-2` 默认定标前提、补充 `T47-2` 固定 DPI 护栏、`O47-1 ③` DIB 精度备注，测试增量预算微调至 `+7～12` 条；⑤ §6 与 §2.1–2.4 全数登记用户 `ⓐ` 拍板结论。
- **v1.2（2026-11-16）**：吸收终审小修并放行实机探针：`T47-3` 补齐子断言 ③ 覆盖极端单帧就地重置路径与独立 `InFrameResetCount()` 观测缝口径；清理 `§4.1`、`§4.2`、`§4.3` 拍板前条件措辞与显式登记歧义，同步 `docs/dev/README.md` 节标题状态，启动 `O47-1`～`O47-3`。
