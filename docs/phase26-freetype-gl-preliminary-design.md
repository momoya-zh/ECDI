# Phase 26 · FreeType 文本栈 + GL 渲染后端 —— 初步设计

> 来源：需求确认稿 `phase26-freetype-gl-requirements.md` **v1.2**（外部评审第一轮：「**基本通过，但建议先修 4 个关键边界，再进入 Preliminary Design**」；四项边界处置见需求稿 §6/§7）
> 状态：**v1.2**（2026-10-03）——✅ **评审通过，可进入详细设计**（外部评审第二轮：「**Preliminary Design：通过，可以进入 Detailed Design**」；★ 评审定了 **详设必答 D26-1..D26-5**，见 §9；★ 处置表见 §10）
> 定位：**接口草案 + 决策定案 + 生命周期契约**。★ 本 Phase 与 Phase 24 / 25 的关键区别 = **首次同时耦合「资源生命周期 + GPU context + 第三方库 + 公共 API 演进」** ⇒ 本稿**以边界与生命周期验证为重心**，不急着写完整类实现。

---

## 1. 需求 → 初设映射

| 需求条目 | 本稿落点 |
|---|---|
| **D1** GL 可注入第二后端 | §3-① 定案 **B**；§4 经 `CreateGLRenderServices()` 注入 |
| **D2 / D4** 测量 DPI 归属 | ★ §3-② 定案 **A″**：`TextMeasurer::Initialize(PlatformRenderContext)` + 窗口 DPI 基准 |
| **D3** `FontEngine` 边界 | §3-③ 三层拆分定案；§4 `FontEngine` 内部件 |
| **D5** 十个纯虚 | §3-④ 保留 **A** + operation-level 语义契约（§5 C-5） |
| **D6** FreeType 图集 | §3-⑤ Atlas 独立对象（key 含 DPI）；§4 `GLGlyphAtlas` |
| **D7** `--gl` | §3-⑥ 命令行切换 + 失败不静默回退 |
| **D8** vendor 集成 | §3-⑦ 源码编入；§5 CMake 设计 + 许可清单 |
| **R6** 性能基准 | §7 T26-9 + §8 O5（contract 细化留详设） |
| **性能方向 ②**（逐帧文本测量） | ★ §3-③ **测量结果缓存**（metrics/size cache，key 含 `text`）+ §7 T26-11 |
| **P1–P6** 初设必答 | §3-②/③/⑤/⑧ + §3-④/⑨ 逐条收敛（§1.1 对照） |
| **详设必答 D26-1..D26-5** | §9（评审第二轮给定：face identity / DIP→px 唯一路径 / cache key / quad 结构 / benchmark contract） |

### 1.1 评审四项关键边界 → 本稿收敛

| 边界 | 需求稿状态 | 本稿定案 |
|---|---|---|
| **D2 / D4 测量 DPI 归属** | 「A′ 倾向 + spike 验证」 | ★ **A″**：加 `TextMeasurer::Initialize(context)`（**对称于 `RenderingBackend::Initialize`**）——见 §3-② |
| **D3 `FontEngine` 边界** | 「初设收敛」 | ★ 三层拆分 + 边界清单（**不含 GPU 资源 / 不含 shaping**）——见 §3-③ |
| **WGL 生命周期** | 「初设必答 P4」 | ★ ownership / current / swap / resize / destroy / 线程 全定——见 §3-⑧ |
| **Atlas 生命周期** | 「初设必答 P3」 | ★ **CPU cache 与 GPU texture 分离**——见 §3-⑤ |

---

★ **不在本 Phase 范围**（性能方向 ③ = 绘制命令构建层的视口裁剪）：`ScrollView` 偏移 → 整树 `Invalidate` + `Widget::Paint` 遍历全部 children ⇒ **视口外行照样构建命令**；**换后端不消失**（GL 只省执行端）⇒ 已登记 `roadmap-deferred.md` **#49**，**顺延至 Phase 27**（用户 2026-10-03 定）。

## 2. 代码基线（B1–B15，全部 2026-10-03 带行号实测）

| # | 基线 | 证据 |
|---|---|---|
| **B1** | `TextMeasurer` 接口 = `MeasureText(font, text) -> Size`（DIP）+ `LineHeight(font) -> float`（DIP）；**无 DPI 形参、无窗口引用** | `include/ECDI/Render/TextMeasurer.h:22,26` |
| **B2** | `RenderingBackend` = **10 个纯虚**（BeginFrame/DrawRect/DrawText/DrawLine/DrawRoundedRect/DrawImage/PushClip/PopClip/DrawFocusRect/EndFrame）；★ **`Initialize(const PlatformRenderContext&)` 有默认空实现（非纯虚）** | `include/ECDI/Render/RenderingBackend.h:36,42-93` |
| **B3** | `RenderServices { unique_ptr<RenderingBackend> renderer; unique_ptr<TextMeasurer> measurer; }`（move-only） | `include/ECDI/Render/RenderServices.h:15-18` |
| **B4** | 注入通路：`Application::Create(..., RenderServices = CreateDefaultRenderServices())` → `Window(app,title,w,h,services)` | `Application.h:72` · `Window.h:238` |
| **B5** | `Window` 构造：`m_renderBackend(std::move(services.renderer))` → `m_textMeasurer(std::move(services.measurer))` → `m_renderer(*m_renderBackend)`；构造末尾 **`m_renderBackend->Initialize(m_platformWindow->GetRenderContext())`** | `Window.cpp:43-49` · `:91` |
| **B6** | `Renderer::Execute(commands, float scale = 1.0f)`；`ExecuteCommand(DrawTextCommand, scale)` **只折 `pos`、`font` 原样**（字号由后端按窗口 DPI 换算） | `Renderer.h:30` · `Renderer.cpp` DrawText 分支 |
| **B7** | 每帧：`PaintContext ctx(m_commands, *m_textMeasurer)` → `root.Paint` → `BeginFrame` → **`Execute(commands, GetDpiScale())`** → `EndFrame` | `Window.cpp:133-138` |
| **B8** | `Window::GetDpiScale()` → `m_platformWindow->GetDpiScale()`（**每帧取、不缓存** ⇒ 跨屏下一帧自动跟随） | `Window.cpp:136` · `:189-199` |
| **B9** | `RenderCommand` = variant<8 类型>；`DrawTextCommand { Point pos; string text; Color color; Font font; }` | `RenderCommand.h:26-32,93-96` |
| **B10** | `PlatformRenderContext` 空基类；`Win32RenderContext : PlatformRenderContext`（持 HWND，`GetHandle()`）——**类型安全容器** | `PlatformRenderContext.h:10-13` · `Win32RenderContext.h:13-26` |
| **B11** | ★ **D-8 根因**：测量链 `GetDC(nullptr)` + `GetDeviceCaps(LOGPIXELSX)`（**屏幕 DPI**）；渲染链 `GetDpiForWindow(m_hwnd)`（**窗口 DPI**） | `GDITextMeasurer.cpp:68,76` · `GDIBackend.cpp:812` |
| **B12** | ★ 调用链：`TextMeasurer` 是 `Window` 的 **`unique_ptr` 成员（窗口级独占）**；Paint 期经 `PaintContext`、非 Paint 期经 `window->GetTextMeasurer()`；**调用点 10+ 处** | `Window.h:321` · `TextWidget.cpp:109,139,140` · `TextBox.cpp:426,474,589,701,705,1286-1311` |
| **B13** | `TextMeasurer` 实现者清单（条 33）：`GDITextMeasurer` · `RecordingBackend`（兼实现）· `FakeTextMeasurer`×2 | `GDITextMeasurer.h:20` · `RecordingBackend.h:21` · `TextBoxTests.cpp:25` · `WidgetTests.cpp:391` |
| **B14** | CMake：单一静态库 `ECDI` + `GLOB_RECURSE` 自动入库；链接 `user32 imm32 msimg32 windowscodecs ole32 shlwapi dwmapi shell32`（**无 `opengl32`**） | `CMakeLists.txt:39-44,67-75` |
| **B15** | spike 图集：`LUMINANCE_ALPHA` **1024²** + **shelf 分配**（单调 bump，行溢出回卷，**不淘汰**）+ `m_glyphSlots` 复用 | `spike_gl_pipeline.cpp:500-510,763-772` |

---

## 3. 决策定案

### 3-① **D1 定案：GL = 可注入第二后端（B）**（评审已确认）

GDI 保持默认；GL 经 `CreateGLRenderServices()` 注入。零回归护栏 = 默认路径一行不改。

### 3-② ★★ **D2 / D4 定案：`TextMeasurer` 加 `Initialize(context)`，DPI 基准 = 窗口 DPI**

**为什么不是「方法加 DPI 形参」（A）**：`MeasureText` 有 **10+ 调用点**（B12），全部在**控件**里；控件**不应该知道 DPI**（DPI 是窗口/平台概念）⇒ 每条调用都传 DPI 是把平台知识泄漏给控件。

**为什么不是「`SetDpi` + 每帧同步」（A′）**：引入**时序依赖**（非 Paint 期的测量/命中在事件处理时发生，若只在 `PaintFrame` 同步则 DPI 可能过期）。

**★ 定案 A″——与 `RenderingBackend::Initialize` 完全对称**：

```cpp
// 公共头 TextMeasurer.h 追加（默认空实现 ⇒ B13 四个实现者零改动）
virtual void Initialize(const PlatformRenderContext& context) {}
```

- `Window` 构造时**同一处**初始化（紧随 B5 的后端 Initialize）：
  ```cpp
  m_renderBackend->Initialize(m_platformWindow->GetRenderContext());
  m_textMeasurer->Initialize(m_platformWindow->GetRenderContext());   // ★ 新增
  ```
- `GDITextMeasurer::Initialize` 取 HWND（`static_cast<const Win32RenderContext&>`，与 `GDIBackend.cpp:243` 同法）⇒ `MeasureText` 内改用 **`GetDpiForWindow(m_hwnd)`**（失败 fail-safe 96，与 `DpiConversion` 一致）⇒ **D-8 闭合**。
- **实时查询** ⇒ 跨屏自动跟随，与 B7/B8 的「每帧取、不缓存」一致，**零时序依赖**。

**收益**：① 调用点 **0 改动**；② 与平台解耦既有惯例同族（条 97）；③ 无新抽象、无 `std::function`；④ `Initialize` 是**非纯虚带默认实现** ⇒ 比加纯虚更安全（条 33 的风险面更小）。
**代价（如实登记）**：`GDITextMeasurer` 从「纯测量零 hwnd」变为**持 HWND** —— 这是它的设计特性变更，须在头注释写明理由（D-8）。

★ **`Initialize()` 的语义边界（评审 §14）**：它与 `RenderingBackend::Initialize` **只是结构对称、不是职责相同**——后者是「**必须**初始化」（10 个纯虚的实现依赖平台句柄），前者是「**可选**注入」（无需平台上下文的实现保持空实现即合法）。**不要**因为两处调用相邻就推断二者生命周期语义等价。

### 3-③ ★★ **D3 定案：三层拆分（`FontEngine` 边界）**

```text
                 FontEngine（内部件 src/Render/FontEngine.{h,cpp}）
                 ──────────────────────────────────────────────
                 FT 库初始化 · face 加载/缓存 · glyph lookup
                 · metrics（advance / bearing / line height）
                 · rasterize（A8 coverage）· CPU glyph cache
                        ↑                        ↑
                 FreeTypeTextMeasurer        GLRenderer ──▶ GLGlyphAtlas
                 （metrics → TextMeasurer 契约）      （GPU texture：图集/UV/quad）
```

**边界清单（冻结）**：

| 属 `FontEngine` | **不属** `FontEngine` |
|---|---|
| FreeType 库生命周期（`FT_Init_FreeType` / `FT_Done_FreeType`） | **GPU 资源**（texture / VBO / shader） |
| face 加载与缓存（按 family） | **排版语义**（shaping / kerning / 连字 / 复杂文种） |
| glyph metrics（advance / bearing / line height） | Window / DPI 的**持有**（DPI 经 `SetDpi` 注入，见 §3-②） |
| 栅格化（A8 coverage bitmap） | 任何 `Widget` / `RenderCommand` 知识 |
| ★ **CPU 缓存两类**：① **metrics/size cache**（key = **`text` + font + dpi** → `Size`，服务**测量链**）；② **glyph bitmap cache**（key = face + size + **dpi** + hinting + glyph index，服务**渲染链**） | |

★ **「同源」的准确含义**（评审 §16）：同源 = **同一 face / size / DPI / rasterization policy**；**不代表**共用 GPU atlas。本稿让 `FreeTypeTextMeasurer` 与 `GLRenderer` **共享同一个 `FontEngine` 实例**（见 §4 注入），但 **GPU texture 只在 `GLRenderer` 侧**。


★★ **为什么「测量缓存」是本 Phase 的关键收益（性能方向 ②）**：`TextWidget::DrawTextContent` **每帧**调 `ctx.MeasureText`（`TextWidget.cpp:109`；`Label::OnPaint` 每帧经此），而 `GDITextMeasurer` 每帧执行 `GetDC` + `SelectObject` + `GetTextExtentPoint32`（**只有 HFONT 缓存、无测量结果缓存**）⇒ 大文本滚动 = **每帧 × 每一可视行**重复昂贵的 GDI 测量调用。`FontEngine` 的 **metrics/size cache**（key = `text + font + dpi`）把这笔账降到**一次测量 + 每帧一次 map 命中** ⇒ **按调用次数计，其收益可能大于栅格化缓存**（栅格化经图集缓存后已降为「每字形一次」）。★ **边界如实**：它消掉的是「每帧重复执行**昂贵测量调用**」，**不消掉**「每帧调用 `MeasureText`」本身（剩下的只是一次查找，可忽略）；后者属 `TextWidget` 层，**不在本 Phase**。
### 3-④ **D5 定案：保留 A（十个纯虚全实现）** + operation-level 语义契约

spike S1 已证明可实现全部十个纯虚。★ 补 **operation-level semantic contract**（§5 C-5）：不要求像素一致，要求「**同一 `RenderCommand` ⇒ 同一语义操作**」——`DrawFocusRect` 须写清 GL 侧语义（框架已有「不依赖系统 `DrawFocusRect`」的定位，B9 的 `DrawFocusRectCommand` 是**自绘点线框**，GL 侧同样自绘 ⇒ 语义天然对齐）。

### 3-⑤ ★★ **D6 定案：FreeType 栅格化 → 图集（A）+ Atlas 独立对象**

- **`GLGlyphAtlas`（内部件）**：`LUMINANCE_ALPHA` 1024² + shelf 分配（沿 B15 spike 验证的方案）。
- **key = (face, size, DPI, hinting, glyph index)** —— ★ **DPI 必须入 key**（100% 与 150% 的同字形不能共用 bitmap）。
- ★ **CPU cache（`FontEngine`）与 GPU texture（`GLGlyphAtlas`）分离**：`FontEngine` **不持** GL texture；`GLGlyphAtlas` 归 `GLRenderer`（GPU 资源必须与 GL context 同生命周期）。
- **无淘汰**（spike 同款）；图集满 = 告警 + 跳过（不崩）。**淘汰策略登记为 O2**。

### 3-⑥ **D7 定案：`--gl` 命令行切换 + 失败不静默回退**

`ModelProbe --gl` ⇒ `CreateGLRenderServices()`；初始化失败（无 WGL / 版本不足 / 驱动异常）⇒ **明确报错退出**（不 fallback GDI），否则性能对照可能实际跑的是 GDI。默认启动仍 GDI。

### 3-⑦ **D8 定案：FreeType 源码 vendor 进 ECDI 静态库（A）**

见 §5-①（CMake 设计）与 §5-②（许可与资源上限）。

### 3-⑧ ★★ **WGL 生命周期定案（P4）**

| 维度 | 定案 |
|---|---|
| **ownership** | `GLRenderer` 持有 `HDC`（`GetDC(hwnd)`）+ `HGLRC`（`wglCreateContext`） |
| **创建** | `GLRenderer::Initialize(context)` 内（B2 的既有接缝）——从 `Win32RenderContext` 取 HWND → `ChoosePixelFormat` → `SetPixelFormat` → `wglCreateContext` → `wglMakeCurrent` |
| **current** | 保持 current（单窗口单 context；创建与绘制同一线程） |
| **线程** | 全在 **UI 线程**（`Window` 构造 → Initialize；`PaintFrame` → 绘制）——与 B5/B7 一致 |
| **resize** | **per-frame 自省**：`BeginFrame` 内 `GetClientRect` → `glViewport`（spike 已验证，M-2 窗口型等价） |
| **destroy** | `GLRenderer::~GLRenderer`：`wglMakeCurrent(nullptr)` → `wglDeleteContext` → `ReleaseDC` |
| **失败** | 任一 WGL 调用失败 ⇒ `m_ready=false` + 日志；`DrawXxx` 全部 no-op（防御，不崩） |

★ **N7 的收敛（评审 §13）**：**保留「不做 `OnTargetResized`」**，依据 = per-frame 自省（上表）；详设须给出「自省 vs 接缝」的成本对比（§8 O3）。

★ **创建顺序不变量（评审 §9）**：`GLRenderer::Initialize` 内**必须**按序 —— ① WGL context 就绪（`wglMakeCurrent` 成功）→ ② `GLGlyphAtlas` 初始化（`glGenTextures` / `glTexImage2D` **必须在 current context 下**）→ ③ `m_ready = true`。**禁止**「构造期建 Atlas、`Initialize` 后补 context」的形态（后续微调构造顺序即炸）。

### 3-⑨ **P5 定案：`RenderCommand` 零改动**

已实测（B2/B9/B6）：GL 后端只实现 `RenderingBackend` 操作级接口，**不接触 `RenderCommand`/variant**；`DrawTextCommand` 的四个字段（pos / text / color / font）经 `Renderer::ExecuteCommand` 展开为 `backend.DrawText(ScalePoint(pos,scale), text, color, font)`（B6）⇒ **GL 零改 `RenderCommand`**。`PaintContext` / `CommandBuffer` / `Renderer` 亦零改动。

---

## 4. 接口草案

### 4-1 公共头变更（仅 2 处，头计数不变）

**`include/ECDI/Render/TextMeasurer.h`** —— 追加一个**带默认实现**的虚函数（§3-②）：

```cpp
/// @brief 平台句柄注入（Phase 26：与 RenderingBackend::Initialize 对称）
/// @details 默认空实现——测量器无需平台句柄的实现者（RecordingBackend / 测试替身）零改动。
///          GDITextMeasurer / FreeTypeTextMeasurer 覆盖它：取窗口句柄 ⇒ 测量基准 = 窗口 DPI
///          （闭合审计 D-8：测量链与渲染链同基准）。
virtual void Initialize(const PlatformRenderContext& context) {}
/// ⚠️ 结构对称 ≠ 职责相同：本方法**可选**（默认空实现即合法）；`RenderingBackend::Initialize` 是**必须**的。
```

**`include/ECDI/Render/BackendFactory.h`** —— 追加一个工厂（§3-①）：

```cpp
/// @brief GL 渲染服务工厂（Phase 26：Windows/WGL + FreeType）
/// @details 返回 { GLRenderer, FreeTypeTextMeasurer }，两者共享同一内部 FontEngine。
///          与 CreateDefaultRenderServices 并列——默认仍 GDI，本工厂用于可注入的第二后端。
RenderServices CreateGLRenderServices();
```

### 4-2 内部件（`src/Render/`，不进公共 API）

| 文件 | 职责 | 关键接口（草案） |
|---|---|---|
| `FontEngine.h/.cpp` | FreeType 共享底层（§3-③） | `SetDpi(int)` · `MeasureText(font, text)->Size`(DIP) · `LineHeight(font)->float`(DIP) · `Rasterize(font, codepoint)->GlyphBitmap`(A8) |
| `FreeTypeTextMeasurer.h/.cpp` | `TextMeasurer` 实现（metrics 侧） | 持 `shared_ptr<FontEngine>`；`Initialize(ctx)` 取 HWND → `engine->SetDpi(GetDpiForWindow(hwnd))` |
| `GLRenderer.h/.cpp` | `RenderingBackend` 实现（GPU 侧） | 持 `shared_ptr<FontEngine>` + `GLGlyphAtlas`；`Initialize(ctx)` 建 WGL context（§3-⑧） |
| `GLGlyphAtlas.h/.cpp` | GPU 字形图集（§3-⑤） | `Alloc(w,h)->Slot` · `Upload(slot, coverage)` · key 含 DPI |

★ **注入路径**：`CreateGLRenderServices()`（内部实现 `BackendFactory.cpp`）创建**一个共享 `FontEngine`**（`shared_ptr`），分别构造 `GLRenderer` 与 `FreeTypeTextMeasurer` —— `RenderServices` 的公共结构（B3）**不变**。

---

## 5. 契约与第三方

### 5-1 CMake 设计（D8）

```cmake
# 第三方（FRAMEWORK 之外，独立静态库；PRIVATE 链接 ⇒ FreeType 不进公共 API）
add_library(freetype STATIC <third_party/freetype/src/**.c>)
target_include_directories(freetype PUBLIC third_party/freetype/include)
target_compile_definitions(freetype PRIVATE FT2_BUILD_LIBRARY)
target_link_libraries(ECDI PRIVATE freetype)
# GL 后端系统库
target_link_libraries(ECDI PUBLIC opengl32)   # ★ 现在没有（B14）
```

★ FreeType **不在** `GLOB_RECURSE`（B14）的 `ECDI/src/**` 范围内（放 `third_party/`）⇒ 框架自动入库规则不受影响。

★ **硬约束（评审 §15）**：**公共头（`include/ECDI/**`）绝不含 FreeType header**（`ft2build.h` 等）——一旦出现，`PRIVATE` 链接也救不了（使用者的 TU 会直接 include 到 FreeType）。当前设计把 FreeType 关在 `src/Render/FontEngine` 内部、公共 API 只有 `TextMeasurer` / `CreateGLRenderServices`，方向正确；★ 详设须把它列为**可机检的验收项**（`grep -r "ft2build" include/ECDI/` 必须 0 命中）。

### 5-2 许可与资源上限（评审 §7 / §14）

| 项 | 定案 |
|---|---|
| **许可** | **FTL**（FreeType License，可商用、非 copyleft）；保留 `third_party/freetype/LICENSE.TXT` |
| **归属** | `third_party/freetype/README.ecdi.md` 记录：上游版本（pin）· 下载来源 + hash · 本地改动（`ftoption.h`）· 同步义务 |
| **最小配置** | `ftoption.h` 关 `FT_CONFIG_OPTION_USE_PNG` / `BROTLI` / `SVG` / `BZIP2` |
| **资源上限（初设建议值，详设验证）** | 最大字体文件 **32 MB** · 最大单字形 bitmap **512×512** · 图集 **1024²** · **保留字节码步数上限**（FT 解释器 `max_steps`，如 100 万）。★ **处置形态（评审 §16）**：每个上限都须落成 **`Limit → Reject/Fail → 明确错误码 + 日志`**（不是笼统的「超过即失败」）；★ **数值待详设验证**（字体文件大小与 glyph 复杂度非线性相关） |

### 5-3 契约（C1–C6）

| # | 契约 |
|---|---|
| **C-1** | 测量返回值单位**恒为 DIP**（不变；B1）——`Font::size` 亦为 DIP |
| **C-2** | 测量基准 DPI **= 渲染基准 DPI = 窗口 DPI**（D-8 的正面表述） |
| **C-3** | `TextMeasurer::Initialize` 与 `RenderingBackend::Initialize` **同源同序**（Window 构造内相邻调用） |
| **C-4** | 公共 API **零 Win32 类型**（`Initialize` 收 `PlatformRenderContext`，不含 HWND） |
| **C-5** | **operation-level 语义契约**：同一 `RenderCommand` 在两后端产生**同语义操作**（不承诺像素一致，§3-④） |
| **C-6** | 默认路径（GDI）**逐位零回归**（GL 只在显式注入时生效） |
| **C-7** | **测量结果可缓存**（key 含 DPI）——同一 `(text, font, dpi)` 重复测量**不触发 FreeType 度量**；**DPI 变化 ⇒ 自动失效**（跨屏安全）。`TextMeasurer` 契约不变（返回值恒 DIP） |

---

## 6. 影响面（预估，详设精确化）

| 面 | 预估 |
|---|---|
| **公共头** | **94 → 94**（零新增；`TextMeasurer.h` / `BackendFactory.h` 各改一处） |
| **公共 API** | **+2**（`TextMeasurer::Initialize` 带默认实现 · `CreateGLRenderServices`） |
| **新增文件** | 内部件约 8（`FontEngine` / `FreeTypeTextMeasurer` / `GLRenderer` / `GLGlyphAtlas` 各 .h+.cpp） |
| **CMake** | +FreeType 静态库目标 + `opengl32`（约 10 行） |
| **第三方** | `third_party/freetype/`（vendor） |
| **用例** | **307 → ~315+**（见 §7） |
| **既有代码改动** | `Window.cpp`（+1 行 measurer Initialize）· `GDITextMeasurer`（基准改窗口 DPI）· `BackendFactory`（+1 工厂）· `ModelProbe`（+`--gl`） |

---

## 7. 测试方向（T26-x，大纲）

| # | 用例 | 断言要点 |
|---|---|---|
| **T26-1** | `TextMeasurer::Initialize` 默认空实现 | 既有实现者（`RecordingBackend` / `FakeTextMeasurer`）编译通过且行为不变 |
| **T26-2** | D-8 闭合 | 同一 `Font` 在**非 96 DPI** 下，测量宽 == 渲染宽（基准一致） |
| **T26-3** | `FontEngine` metrics | 同 face/size/dpi 下 advance 稳定；DPI 变化 ⇒ advance 按比例变 |
| **T26-4** | `GLGlyphAtlas` key | **同字形不同 DPI ⇒ 不同 slot**；同字形同 DPI ⇒ 复用（无二次栅格化） |
| **T26-5** | `GLRenderer` 十纯虚 | 录制/像素断言：接口驱动绘制（沿 spike S1 思路） |
| **T26-6** | WGL 生命周期 | context 创建/销毁无泄漏；失败路径 `m_ready=false` 且 `DrawXxx` no-op |
| **T26-7** | `RenderCommand` 零改动 | GL 后端输出与 GDI **同命令序列**（operation-level，C-5） |
| **T26-8** | `--gl` 失败不静默回退 | 初始化失败 ⇒ 明确错误（不落到 GDI 路径） |
| **T26-9** | ★ 性能基准（R6 / P6） | GDI vs GL × cold/warm；指标 = frame time / **glyph 栅格化次数** / atlas miss（contract 细化留详设 O5） |
| **T26-10** | 默认路径零回归（C-6） | 不注入 GL 时，GDI 路径逐位等价 |
| **T26-11** | ★ **测量缓存命中**（性能方向 ②） | 同 `(text, font, dpi)` 二次测量 ⇒ **不触发 FT 度量**（观测计数或等价）；DPI 变化 ⇒ 缓存失效重算（**非 96 DPI** 下验证） |

---

## 8. 开放决策点（O1–O5，详设收敛）

| # | 开放项 | 说明 |
|---|---|---|
| **O1** | FreeType **具体版本 pin** | vendor 时定（2.13.x / 2.14.x），并记录 CVE 同步基线 |
| **O2** | 图集**淘汰策略** | v1 无淘汰（沿 spike）；重启条件 = 大字体/多字号场景图集常满 | ★ **评审 §17：保持不做**（第一个 GL 后端不引入 LRU / 淘汰 / 碎片整理——那会把本 Phase 变成「字体 GPU 资源管理器」）。
| **O3** | 「per-frame 自省 vs `OnTargetResized`」**成本对比** | 评审 §13 要求；详设给出数据后**冻结 N7** | ★ 评审：详设验证。
| **O4** | `FontEngine` 的 **face 缓存粒度** | 按 family 缓存 face；LRU / 上限待定 |
| **O5** | 性能 benchmark **contract 参数表** | R6/P6：行数/字数/字体/字号/DPI/窗口/滚动速度/#帧/CPU 口径/VSync 固定化 | ★ **评审 §17：详设必须解决**（→ **D26-5**）。

---

## 9. 详设必答（D26-1..D26-5，外部评审第二轮给定）

| # | 必答项 | 收敛目标 |
|---|---|---|
| **D26-1** | **字体实例 / face identity** | `Font → FontFaceId → FT_Face` 的映射；★ **不得用 `FT_Face*` 当稳定 key**（face 释放 / 重载后地址会变） |
| **D26-2** | ★★ **DIP → pixel size 的唯一转换路径** | `Font::size`（DIP）→ 窗口 DPI → **physical px** → FreeType；★ **由 `FontEngine` 统一计算并下发**，**禁止** Measurer 与 Renderer 各自 round（否则重现 D-8 的「测宽 ≠ 渲宽」） |
| **D26-3** | **Glyph cache key 定稿** | face identity · pixel size · DPI · hinting mode · glyph index · **rasterization policy（须落到具体值，不是概念词）** |
| **D26-4** | **Atlas → draw quad 数据结构** | `GlyphSlot { atlas rect, UV, bearing, advance, pixel size }` → `DrawText` = glyph lookup → slot → quad → batch（`GLRenderer` 的实现核心） |
| **D26-5** | **benchmark contract 固定参数表** | 窗口尺寸 · DPI（96 / 144）· 字体 · 字号（14 / 16 DIP）· 固定大文本 · 行数 · 可视行 · 滚动速度 · #帧 · **VSync off**；分 **GDI cold / GDI warm / GL cold / GL warm** 四组（R6 / P6 / O5） |

★ **范围锁（评审 §18，硬约束）**：**详设不得扩大 Phase 26** —— 明确**不做**：HarfBuzz / shaping / ligature / 字体 fallback / LRU atlas / SDF(MSDF) / 多线程 glyph 栅格化 / Linux GL / Wayland / Vulkan / viewport culling（= **#49**，已顺延 Phase 27）。本 Phase 的最小闭环 = **Windows + WGL + FreeType + GL glyph atlas + 第二 Renderer**。

## 10. 外部评审处置（第二轮，2026-10-03）

> 评审结论：**「Phase 26 Preliminary Design：通过，可以进入 Detailed Design」**（原话：不是「勉强通过」）；★ 评审建议**详设不要再大改架构**，把实现细节冻结即可；★ 并**明确建议不要回头改需求稿**。

| 评审节 | 要点 | 处置 |
|---|---|---|
| §1–§6 | 四大边界闭合（DPI / `FontEngine` / Atlas / WGL）· 测量缓存 · 「同源」定义解释清楚 | ✅ **确认（本稿已写对，零改动）** |
| §7 | Atlas key 的 `face` 需稳定 ID，不能用 `FT_Face*` | ✅ 采纳 → **D26-1** |
| §8 | ★ **DIP→px 转换路径必须唯一**（避免两次 round 重现 D-8）；建议 `FontEngine` 统一算 | ✅ **采纳 → D26-2**（★ 详设重点） |
| §9 | GL context 与 `GLGlyphAtlas` 的**创建顺序**须成不变量 | ✅ 采纳 → **§3-⑧ 补不变量** + D26-4 前置 |
| §10 | `shared_ptr<FontEngine>` 所有权**保持现状**（不建议复杂化） | ✅ **确认（不改）** |
| §11 | 性能指标（frame time + rasterization + atlas miss）方向正确 | ✅ 确认（未改动） |
| §12 | **cold / warm 定义须彻底冻结** | ✅ 采纳 → **D26-5** |
| §13 | 性能方向 ③ 顺延 Phase 27 **非常正确** | ✅ 确认（未改动） |
| §14 | `Initialize()` 语义须写清（**结构对称 ≠ 职责相同**） | ✅ **采纳 → §3-② + §4-1 草案注释** |
| §15 | CMake：须确认**公共头绝不含 FreeType header** | ✅ **采纳 → §5-1 硬约束 + 机检项** |
| §16 | 资源限制写成 `Limit → Reject/Fail → 错误码/日志` | ✅ **采纳 → §5-2** |
| §17 | O1–O5 判断（O2 保持不做 / O5 必须详设解决） | ✅ 采纳 → **§8 标注** |
| §18 | ★ 详设**不得再扩大 Phase 26** | ✅ **采纳 → §9 范围锁** |
| — | 需求稿是否回头改 | ✅ **评审明示不必** —— 本轮**只改初设稿** |

★ **最终判断**：**通过，可进入 Detailed Design**——**13 节确认 / 采纳，零否决**；本稿由此升 **v1.2**。

## 11. 修订记录

- **v1.2**（2026-10-03）**吸收外部评审第二轮 ⇒ 通过，可进入详设**。① ★ 状态行改「**评审通过，可进入详细设计**」；新增 **§9 详设必答（D26-1..D26-5）** + **范围锁** 与 **§10 外部评审处置**（13 节确认 / 采纳，零否决）；原 §9 修订记录顺延为 §11。② **§3-② / §4-1**：补 `Initialize()` **语义边界**（与 `RenderingBackend::Initialize` **结构对称、职责不同**——前者可选、后者必须）。③ **§3-⑧**：补 **GL context 与 Atlas 创建顺序不变量**（context ready → Atlas init → ready）。④ **§5-1**：补**硬约束** —— 公共头绝不含 FreeType header（+ 机检 `grep`）。⑤ **§5-2**：资源限制改写为 **`Limit → Reject/Fail → 错误码 + 日志`**，数值标注「待详设验证」。⑥ **§8**：O2 标「保持不做」· O3 标「详设验证」· O5 标「**详设必须解决**」。⑦ ★ 评审明示**不必回头改需求稿** ⇒ 本轮只改本稿。
- **v1.1**（2026-10-03）**补测量缓存（性能方向 ②）+ 登记方向 ③**。① ★★ §3-③ 的 CPU 缓存**明确分两类**：metrics/size cache（key = `text + font + dpi`，测量链）+ glyph bitmap cache（key = face + size + dpi + hinting + glyph index，渲染链）——★ 依据 = 测试项目卡顿**三方向评估之 ②**（逐帧文本测量；调用次数 = 每帧 × 每行 ⇒ **按次数计收益可能大于栅格化缓存**）；★ 边界如实（消掉重复执行、不消掉每帧调用本身，后者属 `TextWidget` 层不在本 Phase）。② 新增 **契约 C-7**（测量结果可缓存、键含 DPI）· 测试 **T26-11**（缓存命中）。③ ★ **性能方向 ③**（命令构建层的视口裁剪）已登记 `roadmap-deferred.md` **#49 · 顺延至 Phase 27**（用户 2026-10-03 定），**不在本 Phase 范围**——本稿 §1.1 后加交叉引用。④ 无其他设计变动。
- **v1.0**（2026-10-03）初稿：需求 v1.2 → 初设映射 · 代码基线 **B1–B15**（全部带行号实测）· **决策定案 D1–D8**（★ **D2/D4 定案 A″：`TextMeasurer::Initialize(context)` + 窗口 DPI**——对称于 `RenderingBackend::Initialize`，调用点零改动 · **D3 三层拆分** · **WGL 生命周期全定** · **Atlas CPU/GPU 分离**）· 接口草案（公共头 2 处变更 + 4 个内部件）· 契约 **C1–C6** · CMake 设计 + 许可/资源上限 · 影响面（**公共头 94 → 94** · API +2 · 用例 307 → ~315+）· 测试大纲 **T26-1..T26-10** · 开放项 **O1–O5**。待评审。
