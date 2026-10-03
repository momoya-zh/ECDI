# Phase 26 · FreeType 文本栈 + GL 渲染后端 —— 初步设计

> 来源：需求确认稿 `phase26-freetype-gl-requirements.md` **v1.2**（外部评审第一轮：「**基本通过，但建议先修 4 个关键边界，再进入 Preliminary Design**」；四项边界处置见需求稿 §6/§7）
> 状态：**v1.0**（2026-10-03）——**待评审**
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
| **P1–P6** 初设必答 | §3-②/③/⑤/⑧ + §3-④/⑨ 逐条收敛（§1.1 对照） |

### 1.1 评审四项关键边界 → 本稿收敛

| 边界 | 需求稿状态 | 本稿定案 |
|---|---|---|
| **D2 / D4 测量 DPI 归属** | 「A′ 倾向 + spike 验证」 | ★ **A″**：加 `TextMeasurer::Initialize(context)`（**对称于 `RenderingBackend::Initialize`**）——见 §3-② |
| **D3 `FontEngine` 边界** | 「初设收敛」 | ★ 三层拆分 + 边界清单（**不含 GPU 资源 / 不含 shaping**）——见 §3-③ |
| **WGL 生命周期** | 「初设必答 P4」 | ★ ownership / current / swap / resize / destroy / 线程 全定——见 §3-⑧ |
| **Atlas 生命周期** | 「初设必答 P3」 | ★ **CPU cache 与 GPU texture 分离**——见 §3-⑤ |

---

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
| CPU glyph cache（key = face + size + **dpi** + hinting + glyph index） | |

★ **「同源」的准确含义**（评审 §16）：同源 = **同一 face / size / DPI / rasterization policy**；**不代表**共用 GPU atlas。本稿让 `FreeTypeTextMeasurer` 与 `GLRenderer` **共享同一个 `FontEngine` 实例**（见 §4 注入），但 **GPU texture 只在 `GLRenderer` 侧**。

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

### 5-2 许可与资源上限（评审 §7 / §14）

| 项 | 定案 |
|---|---|
| **许可** | **FTL**（FreeType License，可商用、非 copyleft）；保留 `third_party/freetype/LICENSE.TXT` |
| **归属** | `third_party/freetype/README.ecdi.md` 记录：上游版本（pin）· 下载来源 + hash · 本地改动（`ftoption.h`）· 同步义务 |
| **最小配置** | `ftoption.h` 关 `FT_CONFIG_OPTION_USE_PNG` / `BROTLI` / `SVG` / `BZIP2` |
| **资源上限（初设建议值，详设冻结）** | 最大字体文件 **32 MB** · 最大单字形 bitmap **512×512** · 图集 **1024²** · **保留字节码步数上限**（FT 解释器 `max_steps`，如 100 万） |

### 5-3 契约（C1–C6）

| # | 契约 |
|---|---|
| **C-1** | 测量返回值单位**恒为 DIP**（不变；B1）——`Font::size` 亦为 DIP |
| **C-2** | 测量基准 DPI **= 渲染基准 DPI = 窗口 DPI**（D-8 的正面表述） |
| **C-3** | `TextMeasurer::Initialize` 与 `RenderingBackend::Initialize` **同源同序**（Window 构造内相邻调用） |
| **C-4** | 公共 API **零 Win32 类型**（`Initialize` 收 `PlatformRenderContext`，不含 HWND） |
| **C-5** | **operation-level 语义契约**：同一 `RenderCommand` 在两后端产生**同语义操作**（不承诺像素一致，§3-④） |
| **C-6** | 默认路径（GDI）**逐位零回归**（GL 只在显式注入时生效） |

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

---

## 8. 开放决策点（O1–O5，详设收敛）

| # | 开放项 | 说明 |
|---|---|---|
| **O1** | FreeType **具体版本 pin** | vendor 时定（2.13.x / 2.14.x），并记录 CVE 同步基线 |
| **O2** | 图集**淘汰策略** | v1 无淘汰（沿 spike）；重启条件 = 大字体/多字号场景图集常满 |
| **O3** | 「per-frame 自省 vs `OnTargetResized`」**成本对比** | 评审 §13 要求；详设给出数据后**冻结 N7** |
| **O4** | `FontEngine` 的 **face 缓存粒度** | 按 family 缓存 face；LRU / 上限待定 |
| **O5** | 性能 benchmark **contract 参数表** | R6/P6：行数/字数/字体/字号/DPI/窗口/滚动速度/#帧/CPU 口径/VSync 固定化 |

---

## 9. 修订记录

- **v1.0**（2026-10-03）初稿：需求 v1.2 → 初设映射 · 代码基线 **B1–B15**（全部带行号实测）· **决策定案 D1–D8**（★ **D2/D4 定案 A″：`TextMeasurer::Initialize(context)` + 窗口 DPI**——对称于 `RenderingBackend::Initialize`，调用点零改动 · **D3 三层拆分** · **WGL 生命周期全定** · **Atlas CPU/GPU 分离**）· 接口草案（公共头 2 处变更 + 4 个内部件）· 契约 **C1–C6** · CMake 设计 + 许可/资源上限 · 影响面（**公共头 94 → 94** · API +2 · 用例 307 → ~315+）· 测试大纲 **T26-1..T26-10** · 开放项 **O1–O5**。待评审。
