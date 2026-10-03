# Phase 26 · FreeType 文本栈 + GL 渲染后端 —— 详细设计（v1.4 · 实施规格）

> 来源：初设稿 `phase26-freetype-gl-preliminary-design.md` **v1.2 ✅ 评审通过**（外部评审第二轮：「**Phase 26 Preliminary Design：通过，可以进入 Detailed Design**」，并定下**详设必答 D26-1..D26-5**）
> 状态：**v1.4**（2026-10-03）——🚧 **实施中**（★ 评审已通过：外部评审第三轮「**通过（Implementation Ready）**」，处置见 §10；★ **批零 / 批一 / 批二 / 批三 / 批四 全部已落**，并完成**首轮 `--gl` 目视缺陷修复**——详见 §11 实施回填；★ **D26-5 性能读数待用户实跑**）
> 定位：**实施规格**。★ 评审要求「**详设不要再大改架构**」⇒ 本稿**只在初设骨架上把实现细节冻结**，不新增架构。★ 评审同时给了**硬约束：详设不得扩大 Phase 26**（范围锁见初设 §9；HarfBuzz / shaping / ligature / 字体 fallback / LRU atlas / SDF / 多线程栅格化 / Linux GL / Vulkan / viewport culling **一律不做**）。
> 结构：**§1 = 评审给定的五个必答（本稿核心，放最前）** → §2 基线（带行号实测）→ §3 逐文件改动 → §4 契约映射 → §5 盯防（可机检）→ §6 用例正文 → §7 影响面 → §8 批次 → §9 开放项 → §10 外部评审处置 → **§11 实施回填** → §12 修订记录。

---

## 1. 详设必答（D26-1..D26-5 评审给定 · §1.6 用户补充）

### 1.1 **D26-1** 字体实例 / face identity

**问题**：`Font → ? → FT_Face` 怎么映射；**不得用 `FT_Face*` 当稳定 key**（face 释放 / 重载后地址会变）。

**基线事实**：`Font` **只有两个字段**——`float size`（DIP）+ `std::string family`（UTF-8，空 = 系统默认）（`include/ECDI/Core/Font.h:15-19`，实测）。★ **无 weight / slant / style**。

**定案**：

| 层 | 定案 |
|---|---|
| **稳定 ID** | `using FaceId = int;` —— **内部件私有类型**，单调递增、**永不复用**（face 缓存条目被逐出时也不回收 id ⇒ 杜绝「旧 key 撞新 face」） |
| **身份来源** | face 缓存键 = **`family` 字符串**（规范化后；空串归一到默认 family）。★ 本 Phase **只有一个维度**（Font 无 style）——**如实登记**：若将来 `Font` 增 `weight`/`slant`，本键须扩维（开放项 **O1**） |
| **持有者** | `FontEngine` 持 `std::map<std::string, FaceEntry>`；`FaceEntry { FaceId id; FT_Face face; }` |
| **API** | `FaceId FontEngine::FaceIdFor(const std::string& family)` —— 命中缓存即返回；未命中 ⇒ `ResolveFile` → `FT_New_Face` → 分配**新** `FaceId` |
| **释放** | `FontEngine::~FontEngine`：逐 face `FT_Done_Face` → `FT_Done_FreeType` |

★ **`FT_Face` 从不外泄**：`FontEngine` 的所有对外 API 只接受 `Font`（或 `FaceId` + glyph index），**不返回 `FT_Face`**。⇒ 缓存键里出现的只有 `FaceId`（int），地址漂移问题被结构性消除。

### 1.2 ★★ **D26-2** DIP → pixel size 的**唯一转换路径**（评审点名「详设重点」）

**问题**：若测量侧与渲染侧**各自**把 `Font::size`（DIP）换算成像素，就可能在各自 `lround` 上分叉 ⇒ **重现 D-8 的「测宽 ≠ 渲宽」**。

**基线事实（本 Phase 的病灶本体）**：

| 链路 | 换算代码 | 基准 DPI 来源 | 证据 |
|---|---|---|---|
| **渲染链**（GDI） | `lfHeight = -lround(font.size * dpi / 96.0)` | `GetDpiForWindow(m_hwnd)`（**窗口 DPI**） | `src/Render/GDIBackend.cpp:810-819` |
| **测量链**（GDI） | `lfHeight = -lround(font.size * dpi / 96.0)` | `GetDeviceCaps(measureDC, LOGPIXELSX)`（**屏幕 DC**） | `src/Render/GDITextMeasurer.cpp:76,39` |

⇒ **同一公式、两个基准** = 审计 **D-8** 的根因（本机 DPI = **120**，125% ⇒ 天然可复现）。

**定案**：**唯一转换路径 = `FontEngine::PixelSize`**。

```cpp
/// @brief DIP 字号 → 物理像素（★ Phase 26 唯一转换路径 —— 测量与渲染共用）
/// @details 与既有 GDI 两条链**同一公式**（lround(size·dpi/96)，float 口径）。
///          ★ 不得改用 DpiConversion::DipToPixels —— 那是几何链路的 **int** 口径，
///          DpiConversion.h:21-22 明写「两条链路不得合并」（font.size 是 float）。
/// @return 像素高度（>= 1；dpi <= 0 按 96）
int FontEngine::PixelSize(const Font& font) const;
```

**三条纪律**：

1. ★ **两个消费者（`FreeTypeTextMeasurer` / `GLRenderer`）都调它**，**谁都不许自己 `lround`**——这正是「同源同基准」（初设 S6 / 评审 §8）的技术落点。
2. **`m_dpi` 由 `SetDpi` 注入**（`FontEngine` **不持 HWND**、不查平台）——初设 §3-③ 的边界：DPI 属注入量。
3. **GDI 侧的 D-8 闭合**走另一条路（**A″**，见 §3-△9）：`GDITextMeasurer::Initialize` 取窗口 DPI ⇒ 与 `GDIBackend` 同基准。★ **两条路不冲突**：`FontEngine::PixelSize` 服务于 **GL 侧那一对**；GDI 侧的病是「基准不同」，改了基准即闭合（公式本来就相同）。

### 1.3 **D26-3** Glyph cache key 定稿

**基线事实（spike 的 key）**：`struct Key { int face; char32_t cp; int px; }`（`.workbuddy/spike/glbackend/spike_gl_pipeline.cpp:387`）—— 用 **codepoint**、且 **px 已含 DPI**。

**定案（定稿 key）**：

```cpp
/// @brief 字形缓存键（★ 定稿 —— 每一维都落到具体值）
struct GlyphKey {
    FaceId        faceId;      ///< 1.6：稳定 face 身份（**不是 FT_Face***）
    std::uint32_t glyphIndex;  ///< 经 `FontEngine::GlyphIndex`（★ 内部即 FT_Get_Char_Index）——真实栅格化单位（≠ codepoint）
    int           pixelSize;   ///< 1.2：FontEngine::PixelSize —— ★ **DPI 经此入 key**
    std::uint8_t  hinting;     ///< rasterization policy（本 Phase 单一取值，见下）
    bool operator<(const GlyphKey&) const noexcept;   // 全字段字典序
};
```

**三处相对初设的**精确化**（评审 §7 要求）：

| # | 初设写法 | 定稿写法 | 理由 |
|---|---|---|---|
| ① | key 含 `size` **和** `dpi` | key 含 **`pixelSize`**（二者之合成） | `pixelSize = f(size, dpi)` 是**唯一决定 bitmap** 的量 ⇒ **DPI 自动隔离**（同 size 不同 dpi ⇒ 不同 px ⇒ 不同 key）。★ 这是「DPI 必须入 key」的**准确落法** |
| ② | key 含 `codepoint` | key 含 **`glyphIndex`** | 多个 codepoint 可映射到**同一 glyph**（如拉丁 `A` 与某变体）⇒ 用 glyph index 才能真正复用；`FT_Get_Char_Index` 是栅格化入口。★ **由 `FontEngine::GlyphIndex` 提供**（`GLRenderer` **不直调 FT**——C-4） |
| ③ | 「rasterization policy」是概念词 | **落到具体调用**：`FT_Load_Glyph(face, gi, FT_LOAD_DEFAULT \| FT_LOAD_TARGET_NORMAL)` + `FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL)`（8-bit 灰度 AA） | 评审 §7：「policy 最好不要只是概念词」⇒ 给出**确切 load flags / render mode** |

★ **`hinting` 字段本 Phase 恒为 `HintingMode::Normal`（单值）** —— 保留字段是为**语义自文档 + 零痛扩维**（若评审认为单值时不应占位，可去掉该维，`operator<` 同步删；**登记为开放项 O2**）。

★ **DPI 入 key 的证实路径**：`pixelSize` 变化 ⇒ key 变化 ⇒ 100% 与 150% 的同字形**不可能共用 bitmap**（评审 §五的硬要求）。

### 1.4 **D26-4** Atlas → draw quad 的完整数据结构

**定稿**：

```cpp
/// @brief 图集槽位 + 绘制所需全部度量（★ DrawText 的核心数据）
struct GlyphSlot {
    int   x = 0, y = 0, w = 0, h = 0;   ///< 图集内的像素矩形（UV 由此推导）
    float bearingX = 0.0f;              ///< FreeType bitmap_left（相对笔位）
    float bearingY = 0.0f;              ///< FreeType bitmap_top（相对基线，y 向上）
    float advance  = 0.0f;              ///< 笔位推进（物理像素——已由 pixelSize 决定）
    int   pixelSize = 0;                ///< 该槽位对应的像素字号（key 的一部分）
    bool  valid = false;                ///< false = 图集已满、本次分配失败（不崩，跳过）
};
```

**`DrawText` 的完整数据流**（`GLRenderer` 侧）：

```text
DrawText(pos, text, color, font)                    // pos = 物理像素（Renderer 已折）；font.size = DIP
  px = m_fontEngine->PixelSize(font)                // ★ D26-2 唯一路径
  faceId = m_fontEngine->FaceIdFor(font.family)     // ★ D26-1
  pen = pos.x
  for cp in Utf8Decode(text):
      gi = m_fontEngine->GlyphIndex(font, cp)       // ★ 修正（评审 §1）：经 FontEngine（C-4——FT_* 只在 FontEngine.cpp）
      key = GlyphKey{faceId, gi, px, HintingMode::Normal}
      slot = m_atlas->GetOrCreate(key, m_fontEngine)   // 命中 = 直接返回；miss = 栅格化 + Alloc + Upload + 缓存
      if slot.valid:
          DrawGlyphQuad(atlasTexture, slot,
                        pen + slot.bearingX,       // 左上角 x（笔位 + bitmap_left）
                        pos.y - slot.bearingY,     // ★ y 向上 ⇒ 基线减 bitmap_top
                        color)
      pen += slot.advance                           // 无论 valid 与否都推进（与 GDI 同构）
```

★ **Atlas 分配失败的语义边界（评审 §13）**：`valid=false` **只影响 glyph bitmap 的可见性、不影响 glyph advance 的排版位置**——后续字形**不会错位**。★ 实现时**不得**写成 `if (!valid) continue;`（那会连 `pen += advance` 一起跳过 ⇒ 整行错位）。

★ **与 GDI 的 `DrawText` 同构性**（operation-level 语义契约 C-5 的兑现）：GDI = `TextOutW(hdc, pos.x, pos.y, text)`（pos = 基线左端起点，逐字形内部推进）；GL = 同一起点语义 + 逐字形 `pen += advance`。★ **字形位图 y 向上** ⇒ 绘制时用「基线减 bitmap_top」（spike 的 `TexturedQuadYUp` 已实证，`:657`）。

### 1.5 **D26-5** benchmark contract（固定参数表）

**定案**：**本 Phase 不做自动化 benchmark 框架**（YAGNI）；基准走 **`ModelProbe --gl` + 计数打印**（人工执行、人工读数）。但**参数与指标固定成文**，保证两次测量可比：

| 维度 | 固定值 |
|---|---|
| **窗口** | **1280 × 720**（客户区） |
| **DPI** | **96 与 144** 两档（★ 本机为 120 —— 若本机跑，如实标注实际值） |
| **字体** | 系统字体（FreeType 路径的默认 face；GDI 路径同 family） |
| **字号** | **14 与 16 DIP** 两档 |
| **文本** | 固定大文本（**N = 2000 行**，每行约 **80 字符**，含中文） |
| **可视行** | **M ≈ 24 行**（720 / 行高） |
| **滚动速度** | **固定 DIP/帧**（如每帧 1 行 ⇒ 40 行/秒 @60fps 的等价） |
| **测量帧数** | **持续 300 帧** |
| **VSync** | **off**（否则 frame time 被刷新率钳制，掩盖差异） |
| **CPU 口径** | **进程 CPU 时间 / 帧**（`GetProcessTimes` 差值），不用「总 CPU%」 |

**四组对照**（★ 这是评审 §11/§12 要求的核心）：

| 组 | 状态 | 观测重点 |
|---|---|---|
| **GDI cold** | 首次显示大文本 | frame time（**首次显示大文本的初始化 / 绘制成本**）—— ★ 评审 §17：GDI 与 GL 的 cold 缓存模型**不同构**，勿写成「全部字形首栅格化」 |
| **GDI warm** | 持续滚动 | ★ 每帧重新 `GetTextExtent` 的次数（= 测量缓存的靶子） |
| **GL cold** | 首次显示大文本 | frame time / glyph 栅格化次数 / atlas 分配+上传次数 |
| **GL warm** | 持续滚动 | ★ **每帧 glyph 栅格化 ≈ 0 · atlas miss ≈ 0**；frame time 稳定 |

★ **指标**：`frame time` · `glyph 栅格化次数` · `atlas miss 次数` · `测量缓存命中/未命中次数`。
★ **验收口径（沿初设 §1.1 的校准）**：不看「GL CPU 更低」这类易被硬件偶然性左右的结论，而看 **warm 阶段「重复 glyph 不再栅格化」+「绘制提交不随字符数 × 行数线性膨胀」**。

### 1.6 ★ 补充（用户 2026-10-03 拍板纳入）：**默认 GDI 路径的测量缓存**

**问题（本稿 v1.0 的 §9 O8 提出）**：性能方向②（逐帧文本测量）的**动机发生在默认 GDI 路径**（`TextWidget` 每帧经 `GDITextMeasurer` 度量），而 D26-2 的测量缓存**落在 `FontEngine`（GL 路径）** ⇒ 「不切 GL 就拿不到方向②的收益」。

**定案（用户拍板：纳入）**：**给 `GDITextMeasurer` 加同款测量结果缓存** ⇒ **默认（GDI）路径立即受益**；两条链**各自独立实现**但**语义一致**。

| 维度 | 定制 |
|---|---|
| **缓存键** | `(text, font.size, font.family, dpi)` → `Size` —— ★ **含 DPI**（跨屏自动失效）；与既有 HFONT 缓存键同口径（`GDITextMeasurer.h:37`） |
| **位置** | 查缓存**在 `GetDC` 之前** ⇒ ★ **命中时连 `GetDC`/`ReleaseDC` 都省**（△9a 已使 dpi 来源变为 `GetDpiForWindow`，**不再需要 DC 取 dpi**） |
| **容量** | 上限 `kMaxMeasureCache`（如 **4096**）；达到上限 ⇒ **`clear()`**（O(1)，防无界增长——TextBox 反复编辑会不断产生新 `text` 键）★ LRU 不做（开放项 **O9**） |
| **`LineHeight`** | ★ **不加缓存**（调用频率远低于每行 `MeasureText`；且 HFONT 缓存已消掉 GDI 字体创建开销）——最小改动面 |
| **观测缝** | `GDITextMeasurer::MeasureCacheMissCount()`（内部件公开方法，**非公共 API**）——与 `FontEngine` 同款（§6 观测缝纪律） |
| **与 GL 侧关系** | 两条缓存**互不共享**（不同实现、不同键结构），但**语义一致**（同 `(text,font,dpi)` 重复测量 = 一次 map 查找）；★ **不合并**（GDI 与 FreeType 是两条独立测量链，合并会引入跨后端共享状态） |

★ **与 K1–K4 的对应**：本缓存直接消掉 §1.1 K1/K2 中「每帧 × 每一可视行」的 `GetDC` + `SelectObject` + `GetTextExtentPoint32W` —— 即性能方向②的靶心；★ 且**不依赖 GL**（批一即可生效，见 §8）。

---

## 2. 代码基线（B1–B18，全部 2026-10-03 带行号实测）

| # | 基线 | 证据 |
|---|---|---|
| **B1** | `Font { float size; std::string family; }`——**仅两字段**（无 weight/slant） | `include/ECDI/Core/Font.h:15-19` |
| **B2** | `TextMeasurer` = 2 纯虚：`MeasureText(font,text)->Size` + `LineHeight(font)->float`；**均为 DIP**；**无 DPI 形参、无窗口引用**；**无 `Initialize`** | `include/ECDI/Render/TextMeasurer.h:22,26` |
| **B3** | `RenderingBackend` = 10 纯虚 + **非纯虚 `Initialize(const PlatformRenderContext&) {}`**（默认空实现） | `include/ECDI/Render/RenderingBackend.h:36,42-93` |
| **B4** | `PlatformRenderContext` = **空基类**；`Win32RenderContext : PlatformRenderContext` 持 HWND（`GetHandle()`） | `include/ECDI/Platform/PlatformRenderContext.h:12-15` · `src/Platform/Win32/Win32RenderContext.h:13-26` |
| **B5** | `RenderServices { unique_ptr<RenderingBackend> renderer; unique_ptr<TextMeasurer> measurer; }`（move-only，**无 shared_ptr**） | `include/ECDI/Render/RenderServices.h:15-18` |
| **B6** | 注入通路：`Application::Create(..., RenderServices = CreateDefaultRenderServices())` → `Window(app,title,w,h,services)` | `include/ECDI/Application/Application.h:72` |
| **B7** | `Window` 构造：`m_renderBackend(std::move) → m_textMeasurer(std::move) → m_renderer(*m_renderBackend) → m_platformWindow(...)`；**构造体内** `m_renderBackend->Initialize(m_platformWindow->GetRenderContext())` | `src/Window/Window.cpp:43-60` |
| **B8** | 每帧：`PaintContext ctx(m_commands, *m_textMeasurer)` → `root.Paint` → `BeginFrame` → `Execute(commands, GetDpiScale())` → `EndFrame`；★ **`GetDpiScale()` 每帧取、不缓存** | `src/Window/Window.cpp:133-138` |
| **B9** | `Renderer::ExecuteCommand(DrawTextCommand)` = `m_backend.DrawText(ScalePoint(cmd.pos, scale), cmd.text, cmd.color, cmd.font)` —— **pos 折物理像素、font 原样（DIP）** | `src/Render/Renderer.cpp:62-66` |
| **B10** | `GDIBackend::Initialize` = `m_hwnd = static_cast<const Win32RenderContext&>(context).GetHandle();`（**A″ 的先例**，条 97） | `src/Render/GDIBackend.cpp:239-243` |
| **B11** | ★ **D-8 根因**：渲染链基准 = `GetDpiForWindow(m_hwnd)`（`GDIBackend.cpp:810`）；测量链基准 = `GetDeviceCaps(measureDC, LOGPIXELSX)`（`GDITextMeasurer.cpp:76`）——**同公式、异基准** | 见左 |
| **B12** | `GDITextMeasurer` 头注释自述「**纯测量零 hwnd**：GetDC(NULL) 临时屏幕 DC」；font 缓存键 = `(size, family, dpi)` | `src/Render/GDITextMeasurer.h:17,37` |
| **B13** | ★★ **双重继承测试替身**：`RecordingBackend : public RenderingBackend, public TextMeasurer`，**未声明自己的 `Initialize`** | `src/Render/RecordingBackend.h:21` |
| **B14** | `TextMeasurer` 实现者（条 33 全库 grep）= **4 个**：`GDITextMeasurer` · `RecordingBackend` · `FakeTextMeasurer`（`TextBoxTests.cpp:25`）· `FakeTextMeasurer`（`WidgetTests.cpp:391`） | grep 实测 |
| **B15** | `RenderingBackend` 实现者 = **2 个**：`GDIBackend` · `RecordingBackend` | grep 实测 |
| **B16** | `Initialize(` 全库**调用点** = `Window.cpp:60`（具体类型 `RenderingBackend&`）+ 5 处测试（`AntiAliasingTests.cpp:335` · `LineCoverageTests.cpp:322,426,435` · `RendererTests.cpp:216,280`，**全部是 `GDIBackend` 具象变量**）⇒ ★ **无一处 `RecordingBackend.Initialize(...)`** | grep 实测 |
| **B17** | CMake：`GLOB_RECURSE FRAMEWORK_SOURCES`（`ECDI/src/**` 自动入库）+ `add_library(ECDI STATIC ...)` + `target_link_libraries(ECDI PUBLIC user32 imm32 msimg32 windowscodecs ole32 shlwapi dwmapi shell32)`——★ **无 `opengl32`** | `CMakeLists.txt:39-75` |
| **B18** | 顶层**无 `third_party/`** 目录（vendor 是实施前置）；公共头实测 **94**；本机 `LOGPIXELSX = 120`（125%，⇒ D-8 天然可复现） | 实测 |

---

## 3. 逐文件改动（△1–△14；口径详见 §7 影响面）

### △1 · 新建内部件 `src/Render/FontEngine.h/.cpp`（FreeType 共享底层）

**职责**（初设 §3-③ 冻结的三层拆分中的最底层）：FT 库生命周期 · face 加载/缓存 · glyph metrics · 栅格化（A8）· **CPU 两级缓存**。**不含** GPU 资源 / shaping / 排版语义。

**公开面（内部件，非公共 API）**：

```cpp
class FontEngine {
public:
    FontEngine();
    ~FontEngine();                              // FT_Done_Face* → FT_Done_FreeType

    FontEngine(const FontEngine&) = delete;     // 资源类：禁拷贝（条 15 同族）
    FontEngine& operator=(const FontEngine&) = delete;

    void SetFontSource(std::unique_ptr<FontSource> source);   // D26-1：字体源注入（平台层提供）
    void SetDpi(int dpi);                       // D26-2：唯一 DIP→px 基准（<=0 按 96）

    int  PixelSize(const Font& font) const;     // ★ D26-2 唯一转换路径

    FaceId FaceIdFor(const std::string& family);            // ★ D26-1 稳定身份（不返回 FT_Face）

    /// @brief codepoint → glyph index（★ 修正：GLRenderer 经此取，**不直调 `FT_Get_Char_Index`**——C-4）
    /// @details 内部 = `FT_Get_Char_Index` ⇒ ★ **`FT_*` 仍只出现在 `FontEngine.cpp`**。
    std::uint32_t GlyphIndex(const Font& font, char32_t cp);

    Size   MeasureText(const Font& font, const std::string& text);   // 带 metrics cache
    float  LineHeight(const Font& font);                    // 带 cache

    /// 取字形（供 GL 侧栅格化）——★ 返回 CPU 位图（A8），**不碰 GL**
    /// @details miss ⇒ FT_Load_Glyph + FT_Render_Glyph(FT_RENDER_MODE_NORMAL) → A8 位图
    /// @param out  glyph 位图（宽高 / bearing / advance / A8 缓冲）——内部件私有结构
    bool Glyph(const Font& font, char32_t cp, GlyphBitmap& out);

    // ★ 观测计数（只读；服务 T26-3/T26-5 断言 + D26-5 基准指标）
    //   为什么需要：缓存命中与否的**返回值相同**（这正是缓存正确性）⇒ 无法凭返回值区分
    //   ⇒ 必须有一个观测缝（★ 内部件的公开方法，**非公共 API**）。
    std::size_t RasterizeCount() const noexcept;         ///< 累计 FT_Render_Glyph 次数
    std::size_t MeasureCacheMissCount() const noexcept;  ///< 累计 metrics cache 未命中次数

private:
    struct Impl;                                // pimpl（内部件也用它收敛 FT 头——见下）
    std::unique_ptr<Impl> m_impl;
};
```

★ **`GlyphBitmap`**（内部件私有值结构）：`{ int width, height; float bearingX, bearingY, advance; std::vector<std::uint8_t> a8; }`——A8 = 8-bit 灰度覆盖度（`FT_RENDER_MODE_NORMAL` 的 `FT_Bitmap.buffer`）。

**契约**：
- ★ **公共头 / 框架层零 FreeType**（评审 §15）：`FT_*` 只出现在 `FontEngine.cpp`（`#include <ft2build.h>`）。
- ★ **CPU 两级缓存**（初设 §3-③ · 性能方向②）：
  - **metrics cache**：key = `(text, family, size, dpi)` → `Size` —— 服务**测量链**（消掉「每帧 × 每行」的重复度量）；
  - **glyph bitmap cache**：key = `GlyphKey`（D26-3）→ `GlyphBitmap`（A8）—— 服务**渲染链**。
- **行为边界（如实）**：metrics cache 消掉的是「每帧重复执行**昂贵度量调用**」，**不消掉**「每帧调用 `MeasureText`」本身（剩下的是一次 map 查找）——后者属 `TextWidget` 层，不在本 Phase。

**`pimpl` 的理由**：`FT_Face` / `FT_Library` 是 FreeType 类型 ⇒ 若放头文件成员，`FontEngine.h` 就必须 include FreeType 头 ⇒ 让**所有** include 者（`FreeTypeTextMeasurer.h` / `GLRenderer.h`）被 FreeType 污染。pimpl 把 `FT_*` 关在 `.cpp` 里。

### △2 · 新建内部件 `src/Render/FontSource.h` + `src/Platform/Win32/Win32FontSource.h/.cpp`（★ 评审 §6 的落点）

**问题**：初设写「本 Phase 字体源仅 Windows 系统字体目录」，但**评审 §6 要求**：`FontEngine` **不把 `C:\Windows\Fonts` 作为自身抽象**——**字体发现 / 文件定位属平台或字体源层**。

**定案**：

```cpp
// src/Render/FontSource.h（内部头，平台无关）
class FontSource {
public:
    virtual ~FontSource() = default;
    /// @brief family（UTF-8，空 = 系统默认）→ 字体文件路径（UTF-8）；找不到返回空串
    virtual std::string ResolveFile(const std::string& family) const = 0;
};
```

```cpp
// src/Platform/Win32/Win32FontSource.{h,cpp}（内部件，平台实现）
class Win32FontSource : public FontSource {
public:
    /// @details 本 Phase（N3 Windows-only）：在系统字体目录内解析。
    ///          ★ 本 Phase 的解析 = 「family 视作文件名」直查（如 "consola.ttf"）；
    ///          空 family ⇒ 内置默认（含 CJK 的系统字体，见 L1）。
    ///          ★ 完整的 family↔文件名解析（EnumFontFamiliesExW）与多字体回退【不做】——见 §9 L1/L2。
    ///          ★★ **找不到时返回空串**（**不静默降级**）——由 `FontEngine` 回退默认 face **并写告警日志**，
    ///             避免用户以为指定字体已生效（评审 §11）。
    std::string ResolveFile(const std::string& family) const override;
};
```

★ **为什么是一个只有 1 个实现的接口**（答辩 YAGNI）：抽象的理由**不是「第二个实现」**，而是**分层**——`FontEngine`（`src/Render/`）**不得依赖** `src/Platform/Win32/`。这与 `PlatformRenderContext`（空基类 + `Win32RenderContext` 实现，同样只有 1 个实现）**同族**。★ 评审 §6 明确要求该下沉 ⇒ 不属「扩大」。

★ **构造点**：`CreateGLRenderServices()`（`src/Render/BackendFactory.cpp`）里 `make_unique<Win32FontSource>()` 注入——★ `GDIBackend.cpp` **既有先例**已 include 平台头（B10），故 Render→Platform 的内部 include 是**既有事实**，非本 Phase 新开的分层口子。

### △3 · 新建内部件 `src/Render/FreeTypeTextMeasurer.h/.cpp`

```cpp
/// @brief FreeType 文本测量器（TextMeasurer 的 GL 侧实现）
/// @details 与 GLRenderer **共享同一个 FontEngine 实例**（同源同基准——初设 S6）。
///          本类只管「FontEngine metrics → TextMeasurer 契约（DIP 返回值）」的映射。
class FreeTypeTextMeasurer : public TextMeasurer {
public:
    explicit FreeTypeTextMeasurer(std::shared_ptr<FontEngine> engine);
    void Initialize(const PlatformRenderContext& context) override;   // ★ A″：取 HWND → SetDpi(窗口 DPI)
    Size  MeasureText(const Font& font, const std::string& text) override;
    float LineHeight(const Font& font) override;
private:
    std::shared_ptr<FontEngine> m_engine;    // ★ 与 GLRenderer 共享（评审 §9/§10：保持 shared_ptr）
};
```

★ **`MeasureText` 的 DIP 口径**：`FontEngine` 内部按 `pixelSize` 度量 ⇒ 物理像素 ⇒ 返回时折回 DIP（`px * 96.0f / dpi`，float 口径，与既有测量链同族，**不得**用 `PixelsToDip` 的 int 口径）。★ 因「进 = 出」用同一 dpi，返回值对基准 DPI **近似不敏感**（沿 `TextMeasurer.h:19-21` 的既有契约表述）。

### △4 · 新建内部件 `src/Render/GLGlyphAtlas.h/.cpp`（GPU 字形图集）

```cpp
/// @brief GL 字形图集（★ GPU 资源——与 GL context 同生命周期，见 △5）
/// @details LUMINANCE_ALPHA 1024² + shelf 分配（沿 spike 已验证方案）；**无淘汰**（O3）。
///          key 含 DPI（经 pixelSize，D26-3）⇒ 跨 DPI 不共用 bitmap。
class GLGlyphAtlas {
public:
    bool Initialize();                       // glGenTextures + glTexImage2D（★ 必须在 current context 下）
    ~GLGlyphAtlas();                         // glDeleteTextures

    /// @brief key → 槽位（miss ⇒ 调 FontEngine 栅格化 + Alloc + Upload + 缓存）
    GlyphSlot GetOrCreate(const GlyphKey& key, FontEngine& engine);
    GLuint Texture() const noexcept;
    std::size_t AtlasMissCount() const noexcept;   ///< ★ 观测（D26-5 基准指标 + T26-7）
private:
    struct Slot { int x=0, y=0, w=0, h=0; bool valid=false; };
    Slot Alloc(int w, int h);                // shelf（沿 spike:763-772；图集满 ⇒ valid=false）
    void Upload(const Slot&, const std::vector<std::uint8_t>& a8, int w, int h);
    std::map<GlyphKey, GlyphSlot> m_slots;
    // ... shelf 游标 / texture id
};
```

★ **shelf 分配算法可无头测**（不碰 GL 的部分抽为纯逻辑，见 △13 测试策略）。
★ **图集满 ⇒ `valid=false` + 告警 + 跳过**（不崩）；**淘汰策略不做**（开放项 O3，沿评审 §17）。

### △5 · 新建内部件 `src/Render/GLRenderer.h/.cpp`（RenderingBackend 的 GL 实现）

```cpp
class GLRenderer : public RenderingBackend {
public:
    explicit GLRenderer(std::shared_ptr<FontEngine> engine);
    ~GLRenderer() override;                  // wglMakeCurrent(nullptr) → wglDeleteContext → ReleaseDC

    void Initialize(const PlatformRenderContext& context) override;   // ★ 见下（创建顺序不变量）
    bool IsReady() const noexcept;           // WGL 失败 ⇒ false；DrawXxx 全部 no-op

    void BeginFrame(const Color& background) override;   // GetClientRect → glViewport（per-frame 自省）
    void DrawRect(...) override;
    void DrawText(const Point& pos, const std::string& text, const Color& color, const Font& font) override;  // ★ D26-4
    void DrawLine(...) override;
    void DrawRoundedRect(...) override;
    void DrawImage(...) override;
    void PushClip(const Rect& rect) override;
    void PopClip() override;
    void DrawFocusRect(const Rect& rect, float cornerRadius, const Color& color) override;
    void EndFrame() override;                // SwapBuffers
private:
    std::shared_ptr<FontEngine> m_engine;
    std::unique_ptr<GLGlyphAtlas> m_atlas;   // ★ 归 GLRenderer（GPU 资源不离 FontEngine）
    HWND m_hwnd = nullptr; HDC m_dc = nullptr; HGLRC m_glrc = nullptr;
    bool m_ready = false;
};
```

★★ **`Initialize` 的创建顺序不变量（初设 §3-⑧ 定案）**：

```text
GLRenderer::Initialize(context)
  ① 取 HWND（static_cast<const Win32RenderContext&>，同 GDIBackend 先例 B10）
  ② GetDC → ChoosePixelFormat → SetPixelFormat → wglCreateContext → wglMakeCurrent
  ③ ★ 此刻 context 才 current ⇒ 此时才 m_atlas->Initialize()（glGenTextures）
  ④ m_ready = true
失败 ⇒ 任一步失败即 m_ready=false（不抛、不崩），DrawXxx 全部 no-op
```

★ **禁止**「构造期建 Atlas、`Initialize` 后补 context」的形态（后续微调构造顺序即炸）。

### △6 · 改公共头 `include/ECDI/Render/TextMeasurer.h`（+`Initialize`）

`TextMeasurer` 追加一个**带默认实现**的虚函数（**公共 API +1**）：

```cpp
class PlatformRenderContext;   // ★ 前置声明（与 RenderingBackend.h:19 同款）

class TextMeasurer {
public:
    virtual ~TextMeasurer() = default;

    /// @brief 平台上下文注入（Phase 26：与 RenderingBackend::Initialize **结构对称**）
    /// @details ⚠️ **结构对称 ≠ 职责相同**：本方法**可选**（默认空实现即合法——无需平台
    ///          上下文的实现者保持零改动）；`RenderingBackend::Initialize` 是**必须**的
    ///          （10 个纯虚的实现依赖平台句柄）。**不要**因两处调用相邻就推断生命周期语义等价。
    ///          GDITextMeasurer / FreeTypeTextMeasurer 覆盖它：取窗口 DPI ⇒ 测量基准 = 窗口 DPI
    ///          （闭合审计 D-8：测量链与渲染链同基准）。
    virtual void Initialize(const PlatformRenderContext& context) {}
    ...
};
```

★ **实现者零改动**（B14 的 4 个：默认空实现覆盖；★ 见盯防⑤ 的 `RecordingBackend` 歧义核查）。
★ **落点**：`#include` 块之后、`class TextMeasurer` 之前（顶层作用域，条 43）。

### △7 · 改公共头 `include/ECDI/Render/BackendFactory.h`（+`CreateGLRenderServices`）

```cpp
/// @brief GL 渲染服务工厂（Phase 26：Windows/WGL + FreeType）
/// @details 返回 { GLRenderer, FreeTypeTextMeasurer }，两者**共享同一 FontEngine**
///          （同源同基准——初设 S6）。与 CreateDefaultRenderServices 并列——
///          默认仍 GDI，本工厂用于可注入的第二后端（D1 定案 B）。
///          ⚠️ 初始化失败（无 WGL / 驱动异常）⇒ **由调用方明确报错退出**，不静默回退 GDI。
RenderServices CreateGLRenderServices();
```

**公共 API +1**。

### △8 · 改 `src/Render/BackendFactory.cpp`（+`CreateGLRenderServices` 实现）

```cpp
#include "Render/GLRenderer.h"
#include "Render/FreeTypeTextMeasurer.h"
#include "Render/FontEngine.h"
#include "Platform/Win32/Win32FontSource.h"      // ★ Render→Platform 内部 include（B10 先例）

RenderServices CreateGLRenderServices()
{
    auto engine = std::make_shared<FontEngine>();          // ★ 共享（评审 §9/§10：保持 shared_ptr）
    engine->SetFontSource(std::make_unique<Win32FontSource>());

    RenderServices services;
    services.renderer = std::make_unique<GLRenderer>(engine);
    services.measurer = std::make_unique<FreeTypeTextMeasurer>(engine);
    return services;
}
```

★ **`RenderServices` 结构不变**（B5 的 `unique_ptr` ×2）——共享只发生在 `FontEngine`（内部件），**公共结构零改动**。

### △9 · 改 `src/Render/GDITextMeasurer.h/.cpp`（+`Initialize` ★ **D-8 闭合** · + **测量结果缓存** ★ 用户 2026-10-03 纳入）

**头**：追加 `void Initialize(const PlatformRenderContext& context) override;` + 成员 `HWND m_hwnd = nullptr;`。

**cpp**：

```cpp
void GDITextMeasurer::Initialize(const PlatformRenderContext& context)
{
    // ★ Phase 26（D-8 闭合）：与 GDIBackend::Initialize 同法（B10 先例）——取窗口 HWND。
    m_hwnd = static_cast<const Win32RenderContext&>(context).GetHandle();
}

// MeasureText / LineHeight / GetOrCreateFont 内：
//   原：const int dpi = GetDeviceCaps(measureDC, LOGPIXELSX);      // 屏幕 DC 基准（D-8 病灶）
//   新：const int dpi = GetDpiForWindow(m_hwnd);                    // ★ 窗口 DPI（与 GDIBackend 同基准）
//       （dpi <= 0 —— 无窗口 / 失败 ⇒ 按 96 fail-safe，与 DpiConversion 一致）
```

★ **`GetDC(nullptr)` 测量 DC 保留**（仍是「帧无关测量」的载体，`GetTextExtentPoint32W` 仍需要它）——**只换基准 DPI 的来源**。
★ **代价如实登记**（沿初设 §3-②）：`GDITextMeasurer` 从「纯测量零 hwnd」变为**持 HWND** —— 头注释须改写并写明理由（D-8）；`GDITextMeasurer.h:17` 的原句「纯测量零 hwnd」**会静默变假** ⇒ 本 △ 含该注释订正（沿条 96）。
★ **`GetDeviceCaps` 的契约 C7 变化**：Phase 20 原文「`GetDeviceCaps` 在全库仅此一处」**本 △ 后归零** ⇒ 须在文档/注释同步（盯防项）。

#### △9b · 测量结果缓存（★ 用户 2026-10-03 拍板纳入 · §1.6 的落点）

**头**（`GDITextMeasurer.h`，追加 member + 观测方法）：

```cpp
// ★ Phase 26（§1.6）：测量结果缓存——消掉「每帧 × 每行」的 GetDC + GetTextExtentPoint32W。
//   键 = (text, size, family, dpi)——★ 含 DPI（跨屏自动失效），与 m_fontCache 同口径。
//   ★ `font.size`（float）在**生成 key 前不得额外 rounding**（评审 §8）——同一 `Font` 重复测量必须逐位命中。
std::map<std::tuple<std::string, float, std::string, int>, Size> m_measureCache;
static constexpr std::size_t kMaxMeasureCache = 4096;   // 上限 ⇒ 达上限 clear()（防无界增长）

/// @brief 测量缓存未命中计数（★ 观测缝——命中与否的**返回值相同**，无法凭返回值区分）
/// @details 内部件公开方法，**非公共 API**（服务 T26-12 断言 + D26-5 基准指标）。
std::size_t MeasureCacheMissCount() const noexcept { return m_measureCacheMissCount; }
private:
std::size_t m_measureCacheMissCount = 0;
```

**cpp**（`MeasureText` 的改造——★ 只有这一处方法变，`LineHeight` 不动）：

```cpp
Size GDITextMeasurer::MeasureText(const Font& font, const std::string& text)
{
    // ★ Phase 26（△9a）：dpi 来自窗口——**不再需要 GetDC 取 dpi** ⇒ 命中缓存时可完全跳过 GDI
    const int dpi = GetDpiForWindow(m_hwnd);
    const int effectiveDpi = (dpi > 0) ? dpi : 96;

    // ★ Phase 26（△9b）：查缓存（在 GetDC 之前）
    const auto key = std::make_tuple(text, font.size, font.family, effectiveDpi);
    auto it = m_measureCache.find(key);
    if (it != m_measureCache.end())
    {
        return it->second;                     // ★ 命中 ⇒ 零 GetDC / 零 SelectObject / 零 GetTextExtent
    }
    ++m_measureCacheMissCount;                 // ★ 观测缝

    // ── 以下为原路径（miss）：GetDC(nullptr) → GetOrCreateFont → SelectObject
    //    → GetTextExtentPoint32W → 折 DIP（float 口径）──
    ...
    if (m_measureCache.size() >= kMaxMeasureCache) { m_measureCache.clear(); }   // ★ 上限 ⇒ 清空
    m_measureCache.emplace(key, result);
    return result;
}
```

★ **`~GDITextMeasurer` 无需改动**（`m_measureCache` 是值类型 `std::map`，自动析构；只有 `m_fontCache` 的 `HFONT` 需要手工 `DeleteObject`）。
★ **缓存键含 `text`** ⇒ 大文本场景下条目数 ≈ **行数**（有界、合理——滚回时命中即收益）。

### △10 · 改 `src/Window/Window.cpp`（+1 行，★ A″ 的落点）

在 B7 的 `m_renderBackend->Initialize(...)` **紧随其后**加一行：

```cpp
m_renderBackend->Initialize(m_platformWindow->GetRenderContext());
m_textMeasurer->Initialize(m_platformWindow->GetRenderContext());   // ★ Phase 26：与后端同源同序（C-3）
```

★ **构造期回调安全论证（条 108）**：`GDITextMeasurer::Initialize` / `GLRenderer::Initialize` **只存句柄 / 建 context，不发任何消息**（无 `SetWindowPos` / `ShowWindow` 类调用）⇒ **不触发回调** ⇒ 不触碰「构造期回调读到未初始化成员」的 UB 路径。
★ ★ **顺序关键**：`m_renderBackend->Initialize` 在 `m_platformWindow` **构造之后**（构造函数体，非初始化列表）⇒ 此时 `GetRenderContext()` 已就绪 ✅。

### △11 · 改 `CMakeLists.txt`（C 语言 + 第三方 + 系统库）—— ★ **实际实现（批零）**

★ 相对 v1.0–v1.2 的设计，本节**有 4 处实测被迫的偏离**（逐条见 §11.2）：

```cmake
project(ECDI VERSION 0.1.0 LANGUAGES CXX C)                 # ★ +C（FreeType 是 C 源码）

include("third_party/freetype/sources.ecdi.cmake")          # ★ 显式 TU 列表（**不能 GLOB** —— §11.2 #3）
add_library(freetype OBJECT ${FREETYPE_SOURCES})           # ★ OBJECT（不是 STATIC —— 见下）
target_include_directories(freetype PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/include")   # ★ PRIVATE：OBJECT 库**不向消费者传播**
target_compile_definitions(freetype PRIVATE FT2_BUILD_LIBRARY)
if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")            # ★ MSVC 系（cl.exe / clang-cl / clang）
    target_compile_definitions(freetype PRIVATE _CRT_SECURE_NO_WARNINGS)  # ★ 关 CRT 弃用告警（§11.2 #6）
endif()
if(MSVC)
    target_compile_options(freetype PRIVATE /utf-8)        # 源码含非 ASCII（实测 15 文件）
endif()

add_library(ECDI STATIC ${FRAMEWORK_SOURCES} $<TARGET_OBJECTS:freetype>)   # ★ obj 并入 ECDI.a
target_include_directories(ECDI PRIVATE
    ...
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/freetype/include"    # ★ ECDI 自己加（OBJECT 不传播）
)
```

★ **为何 `OBJECT` 而非 `STATIC`**：`STATIC` 目标 + PRIVATE 链接会撞 `install(EXPORT)` 的「依赖不在 export set」（静态库的 PRIVATE 依赖仍是 link 接口）⇒ 用 `OBJECT` + `$<TARGET_OBJECTS>` 把 FreeType 的 obj **并入 `ECDI.a`** ⇒ 消费者只看到 `ECDI.a`、**无外部依赖**。
★ **为何源列表不用 GLOB**：`src/**/*.c` 共 210 个，其中 **165 个是「被其他 `.c` include」**的（如 `ftzopen.c` 由 `ftlzw.c` include）⇒ GLOB 逐个编译**必失败** ⇒ 用显式 TU 列表（`sources.ecdi.cmake`）。
★ **为何要 `_CRT_SECURE_NO_WARNINGS`**：MSVC 系走 MSVC UCRT 头 ⇒ FreeType 的 `ft_getenv` / `ft_strcpy` / `ft_strncpy` / `ft_strcat` 展开为被标记弃用的 CRT 函数 ⇒ 6 处 `_CRT_INSECURE_DEPRECATE` 告警。★ **上游零改动** ⇒ 构建层关闭（告警文本自身推荐）。
★ **`opengl32` 尚未加**（属 **批三** GL 后端）—— 批零只接通 FreeType。
★ **FreeType 不在 `GLOB_RECURSE FRAMEWORK_SOURCES`（`ECDI/src/**`）范围内** ⇒ 框架自动入库规则不受影响（B17）。
### △12 · 新建 `third_party/freetype/`（vendor，★ **实施前置**）

| 项 | 定案 |
|---|---|
| **位置** | `third_party/freetype/`（顶层；B18 实测当前**不存在**） |
| **来源** | 官方 `https://download.savannah.gnu.org/releases/freetype/`（或 GitHub 官方镜像） |
| **目录裁剪** | 保留 `include/` · `src/`（★ **排除 `src/tools/`**——实测 4 个自带 `main()`）· ★ **`builds/windows/`**（`ftsystem.c` + `ftdebug.c`）· `LICENSE.TXT` · `README` · ★ **`docs/FTL.TXT` + `docs/GPLv2.TXT`**（许可全文）· `sources.ecdi.cmake` / `README.ecdi.md`（本仓库新增） |
| **版本 pin** | ★ **批零冻结：`2.14.3`**（2026-03-22）· SHA256 `e61b31ab26358b946e767ed7eb7f4bb2e507da1cfefeb7a8861ace7fd5c899a1`（★ 与官方公布值**逐字符一致**，2026-10-03 核对）· 归档 **4,134,916** bytes。★ **选它的理由 = 安全**：官方 2.14.3 `CHANGES` 明写「A bunch of potential security problems have been found. **All users should update.**」（2.13.3 为 2024-08 旧线）⇒ 取**最新稳定维护版**。写入 `README.ecdi.md` |
| **许可** | **FTL**（FreeType License，BSD 风格——可商用、非 copyleft）⇒ 保留 ★ **`LICENSE.TXT` + `docs/FTL.TXT` + `docs/GPLv2.TXT`**（★ **三个都要**：`LICENSE.TXT` 正文明确指向 `docs/` 下两份全文，缺则**引用断链** —— 见 §11.3） |
| **归属清单** | `third_party/freetype/README.ecdi.md`：上游版本 + SHA256 · 下载来源 · ★ **零本地改动** · 目录裁剪 · **同步义务** |
| **最小配置** | ★ **零 patch**（实测）：PNG / BROTLI / BZIP2 / HARFBUZZ **默认已关**；`TT_CONFIG_OPTION_MAX_RUNNABLE_OPCODES` 默认 **`1000000L`**（正是要的步数上限）；SVG 默认开但**不引入外部依赖**（关它要改 2 处，不划算）⇒ **不改 FreeType 任何文件** |
| **资源上限** | 最大字体文件 **32 MB** · 最大单字形 bitmap **512×512** · 图集 **1024²** · 保留字节码步数上限（**数值待实施验证**，开放项 O6） |
| **★ 处置形态** | 每个上限落成 **`Limit → Reject/Fail → 明确错误码 + 日志`**（沿初设 §5-2；**不是**笼统「超过即失败」） |

### △13 · 新建测试 + 改注册（见 §6 用例正文）

| 文件 | 内容 |
|---|---|
| **新建** `src/Tests/FontEngineTests.cpp` | T26-1..T26-6（**纯 CPU，无窗口无 GL**——pixel size / metrics / key / 缓存命中 / 资源上限） |
| **新建** `src/Tests/TextMeasurerTests.cpp` | T26-10 / **T26-12**（**需窗口**——D-8 闭合 + GDI 测量缓存命中；建窗口 helper 沿 `LineCoverageTests.cpp:275-290` 的 `LineAAWindow` RAII 模式）★ **不放 `DpiTests.cpp`**——后者明确定位为「不经窗口、纯函数」（`DpiTests.cpp:17-21` 自述） |
| **新建** `src/Tests/GLBackendTests.cpp` | T26-7 / T26-8（atlas shelf 纯逻辑 + **初始化失败路径**；★ 成功渲染路径归人工 T26-9） |
| **改** `src/Tests/ModelProbeTests.cpp` | T26-11（`--gl` 参数解析，不真建 GL） |
| **改** `src/Tests/RunAllTests.h` + `.cpp` | ★ 注册**三个**新文件（新文件才需——条 99③） |

### △14 · 改 `examples/ModelProbe/ModelProbe.cpp`（+`--gl`）

- 解析命令行 `--gl` ⇒ 用 `CreateGLRenderServices()` 替代 `CreateDefaultRenderServices()`；
- ★ **初始化失败 ⇒ 明确报错退出**（不静默回退 GDI，D7 定案）；
- 默认（无参数）仍 GDI（**零回归**）。

---

## 4. 契约（C-1–C-10）→ 落点 → 测试映射

| # | 契约 | 落点 | 测试 |
|---|---|---|---|
| **C-1** | 测量返回值**恒为 DIP**（不变；B2） | △3 | T26-2 |
| **C-2** | **测量基准 DPI = 渲染基准 DPI = 窗口 DPI**（D-8 的正面表述） | △9（GDI）/ △1+△3+△5（GL，经共享 FontEngine） | **T26-10** |
| **C-3** | `TextMeasurer::Initialize` 与 `RenderingBackend::Initialize` **同源同序**（Window 构造内相邻调用） | △10 | ★ **代码审查 / 结构性机检**（评审 §19：证「构造里两行相邻」**不值得写用例**——原映射到 T26-1 是错的，T26-1 测的是 `PixelSize`；**不新增测试**） |
| **C-4** | 公共 API **零 Win32 类型**（`Initialize` 收 `PlatformRenderContext`，不含 HWND）；**公共头零 FreeType** | △6/△7/△1 | 盯防③⑥ |
| **C-5** | **operation-level 语义契约**：同一 `RenderCommand` 在两后端产生**同语义操作**（不承诺像素一致） | △5 | T26-9 |
| **C-6** | 默认路径（GDI）**逐位零回归**（GL 只在显式注入时生效） | △8/△14 | T26-11 |
| **C-7** | **测量结果可缓存**（键含 DPI）——同一 `(text, font, dpi)` 重复测量**不触发底层度量调用**（FreeType 度量 / GDI `GetTextExtentPoint32W`）；DPI 变化 ⇒ 自动失效。★ **两条测量链各自独立缓存**（△1 GL 侧 · △9 GDI 侧） | △1 / △9 | T26-5 / **T26-12** |
| **C-8** | ★ **DIP→像素的唯一路径 = `FontEngine::PixelSize`**——GL 侧两消费者**都不得自行 `lround`** | △1/△3/△5 | T26-1 |
| **C-9** | `Initialize` **可选**（默认空实现即合法）；与 `RenderingBackend::Initialize` **结构对称、职责不同** | △6 | T26-1 |
| **C-10** | ★ **字体源可替换**：`FontEngine` 不直接持有 Windows 字体目录（路径解析经 `FontSource`）；★ **找不到 ⇒ 默认 face + 告警日志**（**不静默降级**——评审 §11） | △2 | T26-6 |

---

## 5. 盯防清单（12 条，全部可机检）

| # | 盯防 | 机检 |
|---|---|---|
| ① | **公共头零 FreeType**（`ft2build.h` 等） | `grep -r "ft2build\|FT_" include/ECDI/` = **0** |
| ② | **公共头零 GL 头**（`<GL/gl.h>` / `<windows.h>`） | `grep -r "GL/gl\|wgl" include/ECDI/` = **0** |
| ③ | **测量缓存键含 DPI**（非仅 text+font） | 代码审查 + T26-5 |
| ④ | **`FontEngine` 无 GPU 资源**（无 `GLuint` / `glGen*`） | `grep -n "GLuint\|glGen\|glBindTexture" src/Render/FontEngine.*` = **0** |
| ⑤ | ★★ **`RecordingBackend` 歧义核查**：其作用域内**无**非限定 `Initialize(...)` 调用（双重继承 ⇒ 两个 `Initialize` 同名） | `grep "RecordingBackend.*Initialize\|\.Initialize(" src/Tests/` 逐条判（B16 已实测 0） |
| ⑥ | **`src/Render/` 无 `Windows.h` 直用**（★ **豁免名单** = 后端实现头 `GDIBackend` / `GLRenderer` + **`GDITextMeasurer` / `FreeTypeTextMeasurer`**（Phase 26 起都经 `Win32RenderContext.h` 取 HWND —— D-8 / 平台接缝所致）） | `grep -l "Windows.h" src/Render/*.h` 逐条判豁免 |
| ⑦ | **`GetDeviceCaps` 全库归零**（D-8 后无「屏幕基准」残留） | `grep -rn "GetDeviceCaps" src/` = **0** |
| ⑧ | **`RenderCommand` / `Renderer` / `PaintContext` / `CommandBuffer` 零 diff**（P5 定案） | `git diff --stat` |
| ⑨ | ★ **GDI 测量缓存键含 DPI**（`(text, size, family, dpi)`，与 `m_fontCache` 同口径）+ **有容量上限** | 代码审查 + T26-12 |
| ⑩ | ★ **第三方许可文件完整**：`LICENSE.TXT` 正文引用到的文件**逐一在库内**（`docs/FTL.TXT` / `docs/GPLv2.TXT`） | 逐个 `test -f`（见 §11.3） |
| ⑪ | ★ **vendored FreeType 编译零告警**（MSVC 系尤须——CRT 弃用告警已由构建层宏关闭） | 构建日志 `grep -c warning` = **0** |
| ⑫ | ★★ **测量链与渲染链的 FreeType load flags 必须同源**（`FT_LOAD_DEFAULT \| FT_LOAD_TARGET_NORMAL \| FT_LOAD_NO_BITMAP`）——异源 ⇒ **advance 不同** ⇒ 测宽 ≠ 渲宽（D-8 同族；★ SimSun 12–16px 有 MONO 内嵌点阵，此条从「可选」变**必需**，见 §11.6 ⑥） | `grep -c "FT_LOAD_NO_BITMAP" src/Render/FontEngine.cpp` = **2**（`MeasureText` + `GlyphByKey`） |

---

## 6. 用例正文（T26-1..T26-12）

**口径**：自动化 **+11**（FontEngineTests **+6**：T26-1..T26-6 · TextMeasurerTests **+2**：T26-10 / **T26-12** · GLBackendTests **+2**：T26-7/T26-8 · ModelProbeTests **+1**：T26-11）· T26-9（GL 成功渲染）**人工**、不计入 ⇒ **307 → 318**（★ **已达成**）。 ★ **实际进度（2026-10-03）**：批一 **+2** ⇒ **309**（**四链全绿** · 用户实测）· 批二 **+6** ⇒ **315**（本地验证）· 批三 **+2** ⇒ **317**（**四链全绿** · 用户实测）· 批四 **+1** ⇒ **318**（本机）。 ★ **首轮 `--gl` 目视抓到的 3 处 GL 侧缺陷修复不新增用例**（纯渲染观感 —— 靠无头 GL 探针 + 人工目视）；★ 但**「单行 TextBox 无纵向滚动」是控件层语义变更**，**尚无用例锚定** ⇒ 登记为 **§9 O10**（建议补 **T26-13**）。

★ **观测缝（T26-3/T26-5/T26-12 的前提）**：断言「缓存命中」有个矛盾——★ **命中与不命中的返回值相同**（这正是缓存的正确性）⇒ **无法凭返回值区分**。⇒ 用**只读计数**：`FontEngine` 的 `RasterizeCount` / `MeasureCacheMissCount`（**GL 侧** · T26-3/T26-5）与 **`GDITextMeasurer::MeasureCacheMissCount`（GDI 侧** · T26-12）；★ 均为**内部件的公开方法，非公共 API**。★ 同款计数即 **D26-5 基准指标**（glyph 栅格化次数 / atlas miss / 测量缓存命中）的来源——**一处实现、两处消费**（沿条 94 的「先用现有积木」）。

| # | 文件 | 输入 | 期望 |
|---|---|---|---|
| **T26-1** | FontEngineTests | `SetDpi(96)` / `SetDpi(120)` / `SetDpi(144)`；`PixelSize(Font{14})` | `14 / 18 / 21`（`lround(14·dpi/96)`）；`SetDpi(0)` ⇒ 按 96 ⇒ `14`（fail-safe） |
| **T26-2** | FontEngineTests | 同 face/size/dpi 下两次 `MeasureText`；DPI 变化后再测 | 同参 ⇒ 同结果（稳定）；★ DPI 变 ⇒ 结果**近似不变**（`MeasureText` 返回值**恒为 DIP**、与 DPI 无关 —— ★ v1.0 原写「按比例变」**是错的**，见 §11.4） |
| **T26-3** | FontEngineTests | ★ **glyph bitmap cache 命中**（**栅格化链**）：同 `GlyphKey` 二次 `Glyph` | 第二次**不重复栅格化**（`RasterizeCount` 不增）；换 `pixelSize` ⇒ 重新栅格化（计数 +1） |
| **T26-4** | FontEngineTests | `GlyphKey` 唯一性：同字形不同 pixelSize / 不同 faceId / 不同 glyphIndex | **两两不等**；全同 ⇒ 相等（`operator<` 严格弱序自洽） |
| **T26-5** | FontEngineTests | 同 `(text, font, dpi)` 二次 `MeasureText`（C-7 的**测量链**正式用例） | 命中缓存（`MeasureCacheMissCount` 不增）；★ 非 96 DPI（120）下验证「DPI 变 ⇒ 失效重算」 |
| **T26-6** | FontEngineTests | `FontSource::ResolveFile`：空 family / 已知文件名 / 不存在名 | 空 ⇒ 默认路径非空；已知 ⇒ 存在；不存在 ⇒ **空串**（不崩）+ 上限拒绝（超 32 MB / 超 512² ⇒ 明确失败码） |
| **T26-7** | GLBackendTests | `GLGlyphAtlas` shelf 分配（**不碰 GL** 的纯逻辑部分） | 连续 `Alloc` 单调递增；行溢出回卷；越界 ⇒ `valid=false`（沿 spike:763-772） |
| **T26-8** | GLBackendTests | `GLRenderer::Initialize(无效 HWND)`（无 GL 环境可跑） | `IsReady() == false`；随后 `DrawRect/DrawText/...` 全部 **no-op 不崩**（防御语义） |
| **T26-9** | **人工** | `ModelProbe --gl`：观感（字形 / 基线 / DPI / alpha / 位置） | ★ **不要求与 GDI 像素一致**（N1），要求 GL 自身满足 Text/Render 契约 + 目视可接受 |
| **T26-10** | DpiTests | ★★ **D-8 闭合**：非 96 DPI（本机 120）下，同一 `Font` 的 `MeasureText` 宽度 vs 渲染 advance 之和 | **基准一致**（同窗口 DPI）⇒ 测宽 == 渲宽（±1px 内） |
| **T26-11** | ModelProbeTests | `--gl` 参数解析（不真建 GL） | 识别 `--gl` ⇒ 走 GL 工厂；无参数 ⇒ GDI 工厂（**默认零回归**） |
| **T26-12** | TextMeasurerTests | ★ **GDI 测量缓存命中**（§1.6 / △9b）：同一 `GDITextMeasurer` 对同 `(text, font)` 二次 `MeasureText` | 第二次命中 ⇒ `MeasureCacheMissCount` **不增**；换 `text` / `font` ⇒ 未命中（+1）；★ 需窗口（建 hidden window + `Initialize(Win32RenderContext(hwnd))`） |

★ **测试环境前提（如实标注）**：T26-1..T26-6 是**纯 CPU**（需系统字体文件，B18 本机有）；**T26-10 / T26-12 需窗口**（Win32，无需 GL 成功）；T26-7 / T26-8 无需 GL 成功（失败路径 + 纯逻辑）；**T26-9 需真实 GL 环境**（人工）；★★ **T26-10 需「窗口 DPI ≠ 屏幕 DPI」才具区分度**——**本机单屏（窗口 DPI == 屏幕 DPI == 120）⇒ 两链基准天然一致 ⇒ 本机无法区分修复前后**（★ **这正是 D-8 长期潜伏的原因**：单屏且 DPI 一致时不产生差异）。⇒ T26-10 在本机实为**零回归判据**（结果与「用窗口 DPI 复算的期望值」一致），**真区分需双屏 / 双 DPI 环境**——如实标注（沿条 76「量具不校准则判据无效」+ 条 109「报全绿须写明缩放」）。

---

## 7. 影响面

| 项 | 明细 |
|---|---|
| **新增内部件** | `FontEngine.{h,cpp}` · `FontSource.h` · `Win32FontSource.{h,cpp}` · `FreeTypeTextMeasurer.{h,cpp}` · `GLGlyphAtlas.{h,cpp}` · `GLRenderer.{h,cpp}` = **11 文件** |
| **新增第三方** | `third_party/freetype/`（vendor，△12） |
| **新增测试** | `FontEngineTests.cpp` · `TextMeasurerTests.cpp` · `GLBackendTests.cpp` = **3 文件** |
| **改公共头** | `TextMeasurer.h`（+`Initialize`）· `BackendFactory.h`（+`CreateGLRenderServices`）= **94 → 94**（零新增） |
| **改实现** | `BackendFactory.cpp` · `GDITextMeasurer.{h,cpp}` · `Window.cpp`（+1 行）· `CMakeLists.txt` |
| **改示例/测试** | `ModelProbe.{h,cpp}`（+`--gl` 解析 + 工厂选择）· ★ **`examples/ModelProbe/main.cpp`**（★ **真实落点**——`wWinMain` 与 `application.Create(...)` 都在此文件，详设 △14 原写 `ModelProbe.cpp` 是错的，见 §11.5 #7）· `ModelProbeTests.cpp` · `RunAllTests.{h,cpp}`（★ 注册 3 新文件；`DpiTests.cpp` **不再改**——T26-10 移入 `TextMeasurerTests.cpp`） |
| **零改动** | `RenderCommand.h` · `Renderer.{h,cpp}` · `PaintContext.{h,cpp}` · `CommandBuffer` · `Widget` 层 · `GDIBackend`（除既有）· `Window.h` |
| **公共 API** | **+3**（`TextMeasurer::Initialize` · `CreateGLRenderServices` · ★ **`RenderingBackend::IsReady`**——批四为 D7「`--gl` 失败**不静默回退**」新增；**带默认实现 `true`** ⇒ 既有实现者零改动，见 §11.5 #6） |
| **用例** | **307 → 318**（自动化 +11；口径见 §6） |

---

## 8. 批次顺序（五批 + 缺陷修复轮；批零为实施前置）

| 批 | 内容 | 出口判据 |
|---|---|---|
| **批零** | △12 vendor FreeType + △11 CMake（freetype 目标） | ✅ **已完成（2026-10-03）**：FreeType **2.14.3** vendor + CMake 接通；`freetype` 目标 **43 obj 编译通过**（`opengl32` 属批三，未加） |
| **批一** | △6 `TextMeasurer::Initialize` + △9 `GDITextMeasurer`（**D-8 闭合 + 测量缓存**）+ △10 `Window.cpp` +1 行 + T26-10 / **T26-12** | ★ **D-8 立即闭合 + GDI 测量缓存立即生效**（**不需 FreeType / 不需 GL**）——**最小、独立、可先落**。★ ✅ **已完成**：**四链 309/309 全绿**（用户实测） |
| **批二** | △1 `FontEngine` + △2 `FontSource` + △3 `FreeTypeTextMeasurer` + T26-1..T26-6 | ✅ **已完成**：**315/315**（clang + MinGW 本地验证）；★ 覆盖度积分与基线**逐位相同** = 零行为变化（条 103） |
| **批三** | △4 `GLGlyphAtlas` + △5 `GLRenderer` + △7/△8 工厂 + T26-7/T26-8 | 全绿（无 GL 环境亦可，靠失败路径 + 纯逻辑）。★ ✅ **已完成**：**四链 317/317 全绿**（用户实测） |
| **批四** | △14 `ModelProbe --gl` + T26-9 人工目视 + **性能基准（D26-5）** + 台账收口 | 四链全绿 + 目视 + 性能读数。★ 🚧 **代码已完成**（`--gl` + `IsReady` + 计数打印）：**本机 318/318** · **T26-9 目视已进行**并**抓到 3 处 GL 侧缺陷**（已修，见 §11.6）；★ **D26-5 性能读数待用户实跑** ⇒ **台账收口待做** |
| **批四·修** | ★ 首轮 `--gl` 目视的**缺陷修复轮**（GL 侧 3 处 + 同轮并行续修 4 项） | 探针复测 + 本机 `ecdi_tests` 全绿。★ ✅ **已完成**：本机 **318/318** · 构建 **0 告警** · 覆盖度积分与基线**逐位相同**（详见 **§11.6**） |

★ **批一独立先行**的理由：**D-8 的修复不需要 FreeType、不需要 GL** —— 它只是把测量基准从屏幕 DC 换成窗口 DPI（几行）。⇒ **立即可验、零风险**，且**先拿到一部分价值**。

---

## 9. 开放项 / 局限

| # | 项 | 说明 |
|---|---|---|
| **O1** | `Font` 增 `weight`/`slant` 时 face identity 扩维 | 本 Phase 的键**只有 family**（B1）；将来 `Core/Font.h` 若扩字段，`FaceId` 键与 `GlyphKey` 需同步扩维 |
| **O2** | `GlyphKey.hinting` 单值时是否占位 | 本 Phase 恒 `HintingMode::Normal`；若评审认为单值不应占 key 维 ⇒ 去掉（`operator<` 同步删） |
| **O3** | 图集**淘汰策略** | **保持不做**（评审 §17 认可）；重启条件 = 大字体 / 多字号场景图集常满 |
| **O4** | `opengl32` 用 PUBLIC 还是 PRIVATE | 本稿沿 B17 用 PUBLIC；★ 公共头零 GL（盯防②）⇒ 收紧为 PRIVATE 亦安全 |
| **O5** | ✅ **已解决**：FreeType 版本 pin | 批零冻结 **2.14.3**（2026-03-22）+ SHA256 `e61b31ab…`（与官方公布值逐字符一致）· 记入 `README.ecdi.md`（★ 含**安全理由**：官方 `CHANGES` 明写 2.14.3 为安全维护版「All users should update」）· CVE 同步基线 = 本版 |
| **O6** | 资源上限**具体数值** | 32 MB / 512² / 1024² / max_steps 为**建议值**，**实施时验证**（初设 §5-2 已标「待详设验证」；本稿进一步标「待实施验证」） |
| **O7** | 「per-frame 自省 vs `OnTargetResized`」成本对比 | 初设 §3-⑧ 已定「保留不做 `OnTargetResized`」（N7）；本稿沿用，**成本对比数据待实施期补** |
| **O8** | ✅ **已纳入**（用户 2026-10-03 拍板）：默认 GDI 路径的测量缓存 | 由 **§1.6 + △9b** 落地——`GDITextMeasurer` 加同款缓存（键含 DPI · **命中免 `GetDC`** · 上限清空）。★ 原为「待拍板」，**现转正**；批一即可生效（不依赖 GL） |
| **O9** | 测量缓存**淘汰策略** | 本 Phase = **上限 + `clear()`**（O(1)，防无界增长）；**LRU 不做**——重启条件 = 命中率因频繁编辑（每次新 `text` 键）明显下降 |
| **O10** | ★ **「单行 TextBox 无纵向滚动」是控件层语义变更，尚无用例锚定** | 首轮 `--gl` 目视暴露（行盒高于单行视口时，光标跟随把 `scrollOffsetY` 推 > 0 ⇒ 文字下偏）⇒ 单行恒置偏移 0 + 滚轮吞掉（**GDI / GL 同受影响**，见 §11.6 ⑦）。★ **建议补 T26-13**（单行 ⇒ `GetMaxScrollOffset` 不生效 / 滚轮被吞）；**本 Phase 未加**（本轮仅修观感，未扩用例集） |
| **L1** | family→文件名解析 = **「family 视作文件名」直查** | 本 Phase **不做**完整 `EnumFontFamiliesExW` 解析（评审 §6 只要求下沉，未要求完整解析）；GDI 侧仍按 family 名 ⇒ **两侧语义不完全对等**（N1 不承诺视觉等价） |
| **L2** | **无字体回退**（N6） | 默认 face 选**一个含 CJK 的系统字体**以覆盖拉丁 + 中文；**不做 fallback 机制**。★ **实施期订正**：默认解析顺序由 `msyh.ttc` 改为 **`simsun.ttc` 优先** —— GDI 空 family + `DEFAULT_CHARSET` 在中文系统**实际落到 SimSun**，而 MSYH 行盒大约 **30%** ⇒ 控件的框高/内缩按 GDI 观感调过，两侧不同源会让单行文字下偏被裁（见 §11.6 ⑤） |
| **L3** | `TextWidget` 层「每帧调 `MeasureText`」不消 | 本 Phase 消掉的是「重复执行**昂贵度量**」；调用本身仍在（剩下一次 map 查找）——归 `#49` / 后续 |

---

## 10. 外部评审处置（第三轮，2026-10-03）

> 评审结论：**「Phase 26 Detailed Design：通过（Implementation Ready，修 1 个接口矛盾后实施）」** —— ★ 原话：「**不是架构退回**，而是一个很具体的『详设代码路径目前无法按自己规定的封装边界实现』的问题」。★ 评审亦明确「**不会建议重新开一轮设计**」。

**已覆盖度回扫（条 121）**：评审 23 节里 **16 节是本稿已写对的**（§4 `GlyphIndex` 选择 · §5 `pixelSize` 合并 · §6 `PixelSize` 一刀切 + GDI 单独修 · §7 GDI 测量缓存 · §9 `clear()` · §10 `FontSource` · §12 `GLGlyphAtlas` 职责 + `std::map` 够用 · §14 WGL 生命周期 · §15 失败 no-op · §16 benchmark · §18 测试矩阵 + T26-10 限制说明 · §22 批次顺序 · §23「可以不改」清单全部已是本稿设计）⇒ **无需改动，不重复劳动**。

| 评审节 | 要点 | 处置 |
|---|---|---|
| §1 / §23① ★★ | **D26-4 自撞 C-4**：`GLRenderer::DrawText` 直接写 `FT_Get_Char_Index(face, cp)`，但 `GLRenderer` **拿不到、也不该拿 `FT_Face`** | ✅ **采纳（必须改）** → 新增 **`FontEngine::GlyphIndex(font, cp)`**（△1）；§1.4 数据流改经它；§1.3 key 注释与 ② 行同步 |
| §2 / §3 | 修法建议：加 `GlyphIndex()`（最小）或 `ResolveGlyph()` 返回 `GlyphKey`（更彻底但**不强烈建议**） | ✅ **采纳最小方案**（`GlyphIndex`）——符合本稿「不新增架构」 |
| §8 | `font.size`（float）作 key 可接受；★ 但**生成 key 前不得额外 rounding** | ✅ 采纳 → **△9b 注释** |
| §11 | `family` 找不到 ⇒ **不能静默加载别的字体**（否则用户以为指定成功） | ✅ 采纳 → **△2（返回空串 + 默认 face + 告警日志）** + **C-10** |
| §13 | `valid=false` 仍 `pen += advance` **正确**；建议明确「失败只影响可见性、不影响排版位置」 | ✅ 采纳 → **§1.4 补语义边界** |
| §17 | GDI cold 写「全部字形首栅格化」**不准确**（两后端 cold 模型不同构） | ✅ 采纳 → **§1.5 措辞** |
| §19 | `C-3 → T26-1` **映射不对**（T26-1 测的是 `PixelSize`） | ✅ 采纳 → **§4 改为「代码审查 / 结构性机检」**（不新增测试） |
| §21 | FreeType 版本应**一次冻结到补丁号 + SHA256**（不用范围描述） | ✅ 采纳 → **△12 / O5** |
| §20 | `opengl32` 可收紧 PRIVATE（评审明言不阻止实施） | ✅ 确认（保持 PUBLIC，登记 **O4** 不变） |
| §4 / §5 / §6 / §9 / §10 / §12 / §14 / §15 / §16 / §18 / §22 / §23 其余 | 确认本稿设计正确 / 稳定 | ✅ **确认（未改动）** |
| — | ★ 评审指出「**标题没改完全不影响**」 | ★ **自查发现真实遗漏**：文件标题仍是 **v1.0**（v1.1 升版时漏改）⇒ 本轮一并订正为 **v1.2** |

★ **最终判断**：**通过（Implementation Ready）** —— **1 项必须改（D26-4 接口矛盾）+ 6 项建议改（全部采纳）+ 16 节确认**；路线与架构**零退回**。

## 11. 实施回填（批零–批四 + 首轮 `--gl` 目视缺陷修复，2026-10-03）

> 本节记录**实现过程中与设计不符 / 设计未覆盖**的实测事实 —— 沿条 96（**文档会静默变假**）。
> ★ 设计意图（§1–§9）**未变**，本节差异均为**实现期被迫的具体化**（非架构改动）。

### 11.1 交付进度

| 批 | 状态 | 实测 |
|---|---|---|
| **批零** | ✅ 完成 | vendor FreeType **2.14.3** + CMake 接通；`freetype` 目标 **43 obj 编译通过** |
| **批一** | ✅ 完成 | D-8 闭合 + GDI 测量缓存；**四链 309/309 全绿**（用户实测） |
| **批二** | ✅ 完成 | `FontEngine` / `FontSource` / `FreeTypeTextMeasurer` + **T26-1..T26-6**；**315/315**（clang + MinGW 本地验证，覆盖度积分与基线**逐位相同** = 零行为变化） |
| **批三** | ✅ 完成 | `GLGlyphAtlas` + `GLRenderer` + 工厂 + `opengl32` + **T26-7/T26-8**；**四链 317/317 全绿**（用户实测） |
| **批四** | 🚧 代码完成 | `ModelProbe --gl` + `RenderingBackend::IsReady` + 计数打印 + **T26-11**；**本机 318/318**；★ **T26-9 目视已进行**（抓到 3 处 GL 侧缺陷）· **D26-5 性能读数待用户实跑** |
| **批四·修** | ✅ 完成 | ★ 首轮 `--gl` 目视的**缺陷修复轮**（GL 侧 3 处 + 并行续修 4 项）—— 本机 **318/318** · 构建 **0 告警** · 覆盖度积分与基线**逐位相同**；**详见 §11.6** |

### 11.2 相对设计的偏离（全部实测被迫）

| # | 设计（v1.0–v1.2） | 实现（实测） | 原因 |
|---|---|---|---|
| 1 | FreeType **2.13.x** | **2.14.3** | 官方 2.14.3 `CHANGES` 明写**安全修复**「All users should update」⇒ 取最新稳定维护版（2.13.3 为 2024-08 旧线） |
| 2 | 改 `ftoption.h` 关 PNG / BROTLI / SVG / BZIP2 | **零本地 patch** | 实测前四者（含 HARFBUZZ）**默认已关**；`MAX_RUNNABLE_OPCODES` 默认 **`1000000L`**；SVG 默认开但**不引入外部依赖**（关它要改 2 处，不划算）⇒ **不改 FreeType 任何文件**（升级 = 整目录替换） |
| 3 | `GLOB_RECURSE` 取源 | **显式 TU 列表** `sources.ecdi.cmake` | `src/**/*.c` 共 210 个，**165 个被其他 `.c` include**（非独立 TU）⇒ GLOB **必失败**（实测 `ftzopen.c` 报 `FT_LOCAL` 未定义） |
| 4 | `add_library(freetype STATIC)` + `PRIVATE` 链接 | **`OBJECT` + `$<TARGET_OBJECTS>`** | `STATIC` + PRIVATE 撞 `install(EXPORT)`「依赖不在 export set」；obj 并入 `ECDI.a` ⇒ 消费者**零外部依赖** |
| 5 | 只 vendor `include/` + `src/` | **+ `builds/windows/`** | Windows 平台接口 `ftsystem.c`（宽字符 API）+ `ftdebug.c`（否则**链接期** `FT_Trace_Disable/Enable` undefined —— **编译期看不出来**） |
| 6 | （设计未覆盖）构建告警 | **`_CRT_SECURE_NO_WARNINGS`**（仅 MSVC 系） | FreeType 的 `ft_getenv`/`ft_strcpy`/`ft_strncpy`/`ft_strcat` 展开为被 MSVC UCRT 标弃用的 CRT 函数 ⇒ **6 处** `_CRT_INSECURE_DEPRECATE` 告警 ⇒ 构建层关闭（**上游零改动**） |
| 7 | 源列表 = 40 项 `BASE_SRCS` | **+2**（`builds/windows/ftsystem.c` + `ftdebug.c`） | 见 #5 |
| 8 | （设计未覆盖）`fthash.c` 等辅助许可 | 已随 `src/` 保留 | `LICENSE.TXT` 另引 `src/bdf/README` / `src/pcf/README` / `src/gzip/zlib.h` / `src/autofit/ft-hb-*` —— 实测**均在库内** ✓ |

### 11.3 ★ 许可证缺口（本轮修复）

- **现象**：`LICENSE.TXT` 正文明确指向 `docs/FTL.TXT` 与 `docs/GPLv2.TXT`，但 vendor 时**排除了整个 `docs/`** ⇒ **两个正式许可全文缺失**，`LICENSE.TXT` 的引用**断链**。
- **性质**：FTL 要求「源码 / 二进制分发须保留版权声明、条件与免责声明」⇒ 缺全文 = **不合规**。
- **修复**：补 vendor `third_party/freetype/docs/FTL.TXT`（**6,743** bytes）+ `docs/GPLv2.TXT`（**17,994** bytes）—— 取自**同一官方归档**（SHA256 复核一致）。
- ★ **纪律**：第三方许可完整性判据 = **以其正文「引用到的文件」逐一核对**，**不能只看入口文件**（`LICENSE.TXT`）在不在（已登记为盯防 ⑩）。

### 11.4 实现细节（设计已含，此处记录落地值）

- `FontEngine` 的 face 缓存键用**解析后的路径**（非 family 原串）⇒ 空 family 与显式默认文件名**复用同一 face**。
- ★ **T26-2 订正**：设计写「DPI 变 ⇒ 结果**按比例变**」**是错的** —— `MeasureText` 返回 **DIP** ⇒ DPI 变化时结果**近似不变**（这正是 `TextMeasurer.h` 的既有契约）。已按「**近似不变**」实现与断言。
- **编码细节**：`FT_New_Face` 的路径必须 **ANSI**（依据 `builds/windows/ftsystem.c:231` 用 `MultiByteToWideChar(CP_ACP, …)`）⇒ `FontSource::ResolveFile` 返回 ANSI，内部用宽 API 探测。

### 11.5 批三–批四的实现偏离（8 条，全部实测被迫）

| # | 设计（v1.3） | 实现（实测） | 原因 |
|---|---|---|---|
| 1 | `Glyph(font, cp, …)` 直接栅格化 | 拆出 **`GlyphByKey(const GlyphKey&, …)`**，`Glyph` 改为「建键 → 委派」 | 键**完全决定**要栅格化什么（face + glyphIndex + pixelSize + hinting）⇒ **渲染链不必知道 codepoint**（D26-3 ② 的直接兑现）；且键型缓存**单点**，避免两处实现漂移 |
| 2 | `DecodeUtf8` 在 `FontEngine.cpp` 匿名 namespace 内 | 上提为内部头 **`Render/Utf8Decode.h`** | 测量链与渲染链**共用同一解码**——各自解码会在非法 / 截断字节处的码点切分上不一致 ⇒ **测宽 ≠ 渲宽** |
| 3 | （设计未覆盖）图集成员形态 | `std::unique_ptr<GLGlyphAtlas>` | 把「**GPU texture 与 GL context 同生命周期**」**编码进成员顺序**（§3-⑧ 不变量的镜像：`Initialize` 内 context → atlas → ready；析构内 atlas → context → DC） |
| 4 | （设计未覆盖）图集纹理参数 | **不设 `GL_CLAMP_TO_EDGE`**（用默认 `GL_REPEAT`） | MSVC SDK 的 `<GL/gl.h>` **只到 GL 1.1**，该枚举（GL 1.2）**未定义**；槽位之间的 **1px 间隙**已足以防串色（沿 spike 实测结论） |
| 5 | （设计未覆盖）日志刷屏 | `FontEngine` 新增 **`familyCache`**（family 原串 → `FaceId`，**含失败结果**） | 解析失败的 family（如 `"Consolas"` 被当文件名找不到）会**每帧每标签**重复「探测文件 + 告警」⇒ 缓存失败结果以消除 |
| 6 | 公共 API **+2** | **+3** ⇒ 新增 **`RenderingBackend::IsReady()`**（**带默认实现 `return true`**） | D7 要求 `--gl` 初始化失败**明确报错退出**，但 `Window` **不暴露**渲染后端、`GLRenderer` 是内部件 ⇒ 需要一条**公共**就绪查询。★ 默认 `true` ⇒ 既有实现者（含测试替身）**零改动** |
| 7 | 改 `examples/ModelProbe/ModelProbe.cpp`（△14） | ★ **真实落点是 `examples/ModelProbe/main.cpp`** | `wWinMain` 与 `application.Create(kWindowTitle, 680, 780)` **都在 `main.cpp`**；换后端**必须**在 `Create` 前决定 services ⇒ 解析与工厂选择落在 `main.cpp`（`ModelProbe.cpp` 只放可测的纯函数） |
| 8 | （设计未覆盖）`RenderServices` 的完整类型 | `examples/ModelProbe/ModelProbe.h` **同时 include `RenderingBackend.h`** | 暴露「按值返回 `RenderServices`」的函数后，**使用者**（`ModelProbeTests.cpp` 调 `renderer->IsReady()`）需要完整定义 —— `RenderServices.h` 只有**前置声明** ⇒ 否则 `incomplete type` / `<memory>` 的 `sizeof` 报错 |

### 11.6 ★★ 首轮 `--gl` 目视缺陷修复（7 项，2026-10-03）

> **来源**：`--gl` **首轮真实目视** —— `ecdi_tests` 全绿但**观感**不对（用户报告）。
> ★★ **手段 = 无头 GL 探针**（**本轮新增的验证能力**）：屏幕外隐藏窗口（`WS_POPUP`）+ `GLRenderer` + **`glReadPixels` 读回真实像素**（★ 必须在 `SwapBuffers` **之前**）+ **同位置 GDI 对照**；★ 用**已构建的 `ECDI.lib` 直接 `clang++` 链接** ⇒ 免整库重建、秒级迭代。**`--gl` 无法自动目视时的唯一可靠路径**。

| # | 现象 | 根因 | 修法 |
|---|---|---|---|
| ① | ★★ **文字只显示下四分之一、整体上偏被裁切** | **框架 `pos.y` = 字符单元「顶边」**——`GDIBackend::DrawText` 用 `TextOutW`，其默认对齐 **`TA_LEFT \| TA_TOP`**（全库**从未** `SetTextAlign`）；GL 侧却把它**当基线**用（`pos.y - bitmap_top`）⇒ **整行上移一个 ascent**。★ 探针实测 **GL − GDI = −21 px**（20px 字号） | `FontEngine` 新增 **`Ascent(font)`**（`metrics.ascender / 64`，物理像素；与栅格化**同 face / 同 pixelSize** ⇒ D26-2 唯一路径不破）+ `GLRenderer::DrawText` 改 **`baseline = pos.y + Ascent`** ⇒ 探针复测 **Δ = +1 px** |
| ② | **半透明圆角矩形中心变深** | GL 用「竖带（全高）+ 横带（全宽）」两块，**中心区域重叠** ⇒ 半透明色被**二次混合**（探针实测出现「回」字形） | 改 **GDI 同款三条互不重叠的带**（中带全宽 + 上带 + 下带）⇒ 探针复测**均匀** |
| ③ | **圆角控件焦点框四角外凸** | GL 的 `DrawFocusRect` **忽略 `cornerRadius`** 画方框；GDI 侧沿「**4 直线 + 4 圆弧**」周界走 3/3 点划（`GDIBackend::DrawFocusRect`）⇒ 违反自订契约 **C-5**「同命令 ⇒ 同语义操作」 | 移植 GDI 的**周界算法**（直线 + 圆弧 + 弧长参数化）+ 新增 **`SolidSegment`**（任意朝向实心 quad，四顶点同 UV 取实心 texel）⇒ 探针复测**贴合圆角** |
| ④ | **字形「污染」**（笔画错相 / 粗细不一） | GDI `TextOutW` **只收整数坐标**（`static_cast<LONG>` 截断）⇒ 字形恒在**整数像素网格**；GL 用**分数** pen/baseline 会让 **hinted 位图在分数相位下采样** | **整数吸附**：`pen` 与 `baseline` 一律 `std::floor`（此后 advance / bearingX / bearingY 本身即整数 ⇒ **整行逐字对齐像素网格**） |
| ⑤ | ★ **GL 默认字体与 GDI 不同源** | GL 空 family → `msyh.ttc`；而 **GDI 空 family + `DEFAULT_CHARSET` 在中文系统实际落到 `simsun`（宋体）** —— MSYH 行盒大约 **30%**（实测 20px：MSYH `tmHeight=27`/ascent 22 vs **SimSun `tmHeight=20`/ascent 18**）⇒ 控件的框高 / 内缩都按 GDI 观感调过 ⇒ GL 单行文字**下偏并被裁** | `Win32FontSource::DefaultFontFile` 解析顺序改 **`simsun.ttc` 优先**（仍含 CJK，L2 意图保持） |
| ⑥ | ★ **SimSun 12–16px 的内嵌 MONO 点阵被错读** | SimSun 在 12–16px 有**内嵌点阵**（`FT_PIXEL_MODE_MONO`，1bpp），而 `GlyphByKey` 的位图拷贝按 **8bpp 灰度**写死 ⇒ 错读 = 「字形污染」。★ 改默认字体后此问题**从可选变必需** | **测量链 + 渲染链同加 `FT_LOAD_NO_BITMAP`**（强制矢量渲染）—— ★ **必须两链同源**，否则 bitmap advance ≠ vector advance ⇒ 测宽 ≠ 渲宽（登记为**盯防 ⑫**） |
| ⑦ | 单行 TextBox **文字下偏**、且**可被滚轮滚动** | 行盒高于单行视口时，**光标跟随**会把 `scrollOffsetY` 推 > 0 | 单行 ⇒ `EnsureCaretVisible` **恒置 `m_scrollOffsetY = 0`** + `OnMouseWheel` **单行直接吞掉**（★ **控件层语义变更，GDI / GL 同受影响**；无用例锚定 ⇒ 登记 **O10**） |

★ **复核（本机 clang 临时构建，含全部改动）**：`ECDI.lib` + `ecdi_tests` **0 error / 0 warning** · **318 / 318** · `AntiAliasing` 覆盖度积分与基线**逐位相同**（零行为变化）· ★ **探针复测**（GDI 参照同步改 SimSun）：`Ascent=18` vs GDI `tmAscent=17` ⇒ **Δ = +1 px**（**两引擎 ascent 的系统性差，N1 明确允许**），**x 范围 20..38 与字高 16 与 GDI 完全一致**，字形干净无污染。

★ **纪律沉淀**（本轮新增）：**「全绿」不等于「观感正确」** —— `ecdi_tests` 全绿时 GL 观感仍可整体错位（断言只覆盖数值契约，不覆盖「绘制原点语义」）。⇒ ★ **新增能力型后端必须补一条「与既有后端同位置对照」的像素级验证路径**（本轮的**无头 GL 探针**即其形态）。

## 12. 修订记录

- **v1.4**（2026-10-03）**实施回填（批三–批四 + 首轮 `--gl` 目视缺陷修复）**（非评审驱动；详见 **§11**）。① ★★ 新增 **§11.6（7 项缺陷）** —— `--gl` **首轮真实目视**暴露（`ecdi_tests` 全绿但**观感**不对）：**① 文字基线**（框架 `pos.y` = **字符单元顶边**，GL 当基线用 ⇒ 整行上移一个 ascent，探针实测 **−21px**）· **② 半透明圆角矩形中心二次混合**（双带重叠 → 改 GDI 三条带）· **③ 焦点框忽略 `cornerRadius`**（→ 移植 GDI 的「4 直线 + 4 圆弧」周界 + 新增 `SolidSegment`）· **④ 文字整数吸附**（分数相位会让 hinted 位图错相）· **⑤ 默认字体同源**（GL 用 MSYH 而 GDI 实际落 SimSun，行盒差 ~30%）· **⑥ `FT_LOAD_NO_BITMAP`**（SimSun 12–16px 的 MONO 内嵌点阵被 8bpp 拷贝错读）· **⑦ 单行 TextBox 无纵向滚动**。② ★★ 新增 **§11.5（8 条实现偏离）**：`GlyphByKey` / `Utf8Decode` 上提 / 图集 `unique_ptr` / 无 `GL_CLAMP_TO_EDGE`（GL 1.1 头）/ `familyCache` / **公共 API +2 → +3**（`RenderingBackend::IsReady`）/ **△14 真实落点是 `main.cpp`** / `RenderServices` 完整类型。③ **§7 影响面订正**（API **+3** · 示例/测试补 `main.cpp`）· **§5 盯防 11 → 12 条**（新增 **⑫ 测量链与渲染链 load flags 同源**）。④ **§8 批次补状态**（批三 ✅ 四链 317 · 批四 🚧 代码完成本机 318 · **新增「批四·修」行**）· **§6 口径**改「**已达成 318**」并登记 **O10**（单行语义无用例锚定 ⇒ 建议 **T26-13**）。⑤ **§9 L2 订正**（默认 face `msyh.ttc` → **`simsun.ttc`**，对齐 GDI 解析）。⑥ 新增 **§11.1 交付进度**三行（批三 / 批四 / 批四·修）。⑦ ★★ **新增验证手段 = 无头 GL 探针**（屏幕外窗口 + `glReadPixels` + **同位置 GDI 对照**；用已构建 `ECDI.lib` 直接链接），并沉淀纪律「**全绿 ≠ 观感正确**」。
- **v1.3**（2026-10-03）**实施回填（批零–批二）+ 许可缺口修复**（非评审驱动；详见 **§11**）。① ★★ **许可证缺口**：vendor 时排除了整个 `docs/` ⇒ `LICENSE.TXT` 正文引用的 **`docs/FTL.TXT` / `docs/GPLv2.TXT` 缺失**（引用断链、不合规）⇒ 已从官方归档补齐，并登记**盯防 ⑩**（判据 = 以正文引用逐一对账，不能只看入口文件）。② ★ **版本 pin 落地 2.14.3**（含安全理由）· **零本地 patch**（默认已关 PNG/BROTLI/BZIP2/HARFBUZZ）· **显式 TU 列表**（GLOB 必失败：165/210 非独立 TU）· **`OBJECT` + `$<TARGET_OBJECTS>`**（避 `install(EXPORT)` 冲突）· **`builds/windows/`**（`ftsystem.c` + `ftdebug.c`）· **`_CRT_SECURE_NO_WARNINGS`**（MSVC 系 CRT 弃用告警 6 处）—— 逐条见 **§11.2**。③ **△11 重写**为实际 CMake 实现；**△12** 更新版本 / 许可 / 最小配置 / 目录裁剪。④ **盯防 9 → 11 条**（⑥ 扩豁免名单至 `GDITextMeasurer` / `FreeTypeTextMeasurer`；新增 ⑩ 许可完整 · ⑪ FreeType 零告警）。⑤ ★ **§6 T26-2 订正**（`MeasureText` 返回 DIP ⇒ DPI 变结果**近似不变**，v1.0 的「按比例变」是错的）。⑥ **§8 批次补状态**（批零/批一/批二 ✅；批三/批四 ⬜）· **§6 口径补实际进度**（309 / 315）· **O5 标 ✅ 已解决**。⑦ 新增 **§11 实施回填**，原 §11 修订记录顺延 **§12**。
- **v1.2**（2026-10-03）**吸收外部评审第三轮 ⇒ 通过（Implementation Ready）**。① ★★ **必须改**：修 **D26-4 的封装矛盾**——新增 **`FontEngine::GlyphIndex(font, cp)`**（△1），`GLRenderer` **不再直调 `FT_Get_Char_Index`**（§1.4 数据流改经它；§1.3 key 注释与 ② 行同步）⇒ **`FT_*` 仍只出现在 `FontEngine.cpp`**（C-4 兑现）。★ 评审亦指出此接口本身**不泄漏 FreeType**（返回 `uint32_t`）。② **6 项建议改全部采纳**：**△9b** 加「`font.size` 生成 key 前不得额外 rounding」· **△2 + C-10** 加「`family` 找不到 ⇒ 返回空串 + 默认 face + **告警**（不静默降级）」· **§1.4** 补 **Atlas 失败语义边界**（`valid=false` 只影响可见性、不影响 advance 排版）· **§1.5** GDI cold 措辞改「初始化 / 绘制成本」（两后端 cold 模型**不同构**）· **§4 C-3** 映射改为「**代码审查 / 结构性机检**」（原映射到 T26-1 是错的；**不新增测试**）· **△12 / O5** FreeType 版本**一次冻结到补丁号 + SHA256**。③ ★ **自查发现遗漏**：文件标题仍 **v1.0**（v1.1 升版时漏改）⇒ 订正 **v1.2**。④ 新增 **§10 外部评审处置（第三轮）**（含 **已覆盖度回扫**：16 节本稿已写对）+ 原 §10 修订记录顺延为 **§11**；导航行同步。⑤ ★ 评审明示「**不会建议重新开一轮设计**」⇒ **路线与架构零退回**；**可进入实施**（★ 评审亦建议**先落批一**——D-8 + GDI 测量缓存，不依赖 FreeType / GL）。
- **v1.1**（2026-10-03）**纳入用户拍板的 GDI 测量缓存**。★ 用户 2026-10-03 拍板（回应本稿 v1.0 §9 O8）：把**默认 GDI 路径的测量缓存**纳入本 Phase。① ★★ 新增 **§1.6**（补充必答）——`GDITextMeasurer` 测量结果缓存：键 `(text, size, family, dpi)`（**含 DPI**）· 查缓存**在 `GetDC` 之前**（★ 命中免 `GetDC`/`ReleaseDC`）· 上限 4096 ⇒ `clear()`（防无界增长）· **`LineHeight` 不加**（最小面）· 观测缝 `MeasureCacheMissCount` · 与 GL 侧缓存**语义一致但互不共享**。② **△9 扩展为 9a（基准改窗口 DPI = D-8 闭合）+ 9b（测量结果缓存）**。③ **△13 / §7**：测试文件 **2 → 3**（新增 **`TextMeasurerTests.cpp`** 承载 T26-10 / T26-12——★ **不放 `DpiTests.cpp`**，后者自述定位为「不经窗口、纯函数」）。④ **C-7 泛化**（泛到「底层度量调用」，含 GDI `GetTextExtentPoint32W`；两条链各自缓存）。⑤ **盯防 8 → 9 条**（新增 ⑨：GDI 缓存键含 DPI + 容量上限）。⑥ **§6 加 T26-12**（GDI 测量缓存命中）；口径 自动化 **+10 → +11** ⇒ **307 → 317 → 318**；观测缝补 GDI 侧计数。⑦ **§8 批一**加 T26-12（★ 批一 **不需 FreeType、不需 GL** ⇒ D-8 与 GDI 测量缓存**同时立即生效**）。⑧ **§9 O8 转正**（待拍板 → ✅ 已纳入）+ 新增 **O9**（缓存淘汰）。⑨ ★ **T26-10 的诚实标注**：**本机单屏**（窗口 DPI == 屏幕 DPI == 120）⇒ **两链基准天然一致 ⇒ 本机无法区分修复前后**（★ **这正是 D-8 长期潜伏的原因**）⇒ 本机实为**零回归判据**，**真区分需双屏 / 双 DPI 环境**。
- **v1.0**（2026-10-03）初稿（实施规格）。**§1 = 评审给定五必答**（D26-1 face identity 用 `FaceId`+family 键 · ★★ **D26-2 `FontEngine::PixelSize` 唯一 DIP→px 路径**（评审点名的详设重点）· **D26-3 key 定稿**（`pixelSize` 代 size+dpi、`glyphIndex` 代 codepoint、hinting 落到具体 load flags）· **D26-4 `GlyphSlot` + DrawText 完整数据流** · **D26-5 benchmark contract 固定参数表** · ★ **观测缝**（只读计数：`RasterizeCount` / `MeasureCacheMissCount` / `AtlasMissCount`——测试断言与基准指标共用））。**基线 B1–B18**（带行号实测；★ **B11 = D-8 根因**（两链同公式异基准）· **B13/B16 = 双重继承 + 零 `RecordingBackend.Initialize` 调用**（歧义不触发）· **B18 = 无 `third_party/` + 本机 DPI 120**）。**△1–△14**（新建 11 内部件 + 2 测试 + vendor；★ **△2 `FontSource` = 评审 §6 的落点**；★ **△9 = D-8 闭合**）。**契约 C1–C10 全映射** · **盯防 8 条**（含 ★ **`GetDeviceCaps` 归零** · **公共头零 FreeType/GL**）· **用例 T26-1..T26-11**（自动化 +10 ⇒ 307 → 317）· **五批**（★ **批一 = D-8 独立先行**）· **开放项 O1–O8 / 局限 L1–L3**（★ **O8 = 默认 GDI 路径的测量缓存待拍板**）。**待评审。**
