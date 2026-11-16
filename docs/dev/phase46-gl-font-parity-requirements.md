# Phase 46 需求与职责确认 · GL 字体能力对齐（合成粗体 / 合成斜体 + 中文字体族名解析）

> **状态**：**v1.5（全数闸门 `D46-0~5` 与两项显式契约破坏 `CB-1`/`CB-2` 已由用户签字拍板，`O46-1`～`O46-3` 探针已归档；同步详设 v1.2 第 5 轮评审补 `Win32FontSource.h` 第 10 文件及 `T46-5/6` 守卫返回口径）**
> **源码基线**：HEAD `36bb02d`（`fix(#91,#92,#93): GDI mixed-font baseline alignment, descender overhang guard and AutoSize ceil (608 -> 615)`）
> **规模基线**：测试 **615** 用例（MinGW + Clang 双链 615/615 全绿）｜Public 头 **102**｜设计文档 **220 → 221**（需求稿落盘值；递归 `docs/dev/` **219 → 220**）
> **对应延后账本**：[roadmap-deferred.md](roadmap-deferred.md) **`#94`**（收编 **`#69`** 中文/本地化字体族名解析 + Phase 45 **`L45-1`** GL 合成斜切；Phase 47 图形缓存与 WGL 自动化像素验证另立 **`#95`**）
> **门禁声明（提交前执行）**：`git add` 入索引 → `python tools/doc_lint.py --select L04,L05,L06,L08,L09,L11,L13,L14 --quiet` 须 **0 defect** + 阳性对照（`README.md` 锚点制造 1 条 `L13` 后逐字节还原归零）。

---

## §0 核心红线（不可退让）

| # | 红线 | 校验方式 |
|---|---|---|
| **红线 1** | **既有英文 / 常规 / 真粗真斜体路径零回归（含两项显式契约破坏豁免条款）**：`Font{size, EnglishFamily, weight=400, italic=false}`、文件名直写（`directFile` 如 `"consola.ttf"`）以及本身拥有真粗体/真斜体面的家族（如 `Segoe UI`、`Consolas`）的选面、`advance`、`Ascent`、`LineHeight` 与光栅化输出**逐位不变**；除按闸门 `D46-2`（`CB-1`：作废 `I45-italic-fallback`，覆盖 `FontWeightTests.cpp:201, :208` 与 `WidgetTests.cpp:2931`）与闸门 `D46-4`（`CB-2`：作废 `P0-② ZH 边界钉`，覆盖 `FontWeightTests.cpp:131-132`）批准的**两项显式契约破坏登记**外，存量 615 条测试**零退化** | 既有 `FontWeightTests` / `WidgetTests` 全绿 + golden 回归相 |
| **红线 2** | **无条件测画同源、单点出口与整像素加粗下界（D26-2）**：`FreeTypeTextMeasurer` 与 `GLRenderer` 共用同一 `FontEngine` 实例；测量链（`AdvanceOf` / `MeasureText` / `FitText`）与光栅链（`GlyphByKey` / `Glyph`）在每次 `FT_Load_*` 后**无条件经由同一单一函数** `ApplySynthesis(...)` 施加合成粗体/斜切状态，禁止跨调用保留 `FT_GlyphSlot` 合成状态。且因 `AdvanceOf:192` 与 `GlyphByKey:756` 对 `advance.x` 执行 **`>> 6` 整数截断**，任何 `xstr < 64`（26.6 定点）都会被吞成 `0px` 增宽而导致墨迹外扩、笔位不动、字形互叠——故合成粗体**硬性要求 `xstr` 取整像素（`xstr >= 64`，即至少 `+1px`）** | 单一 `ApplySynthesis` 出口 + `xstr >= 64` + `MeasureText` 与逐字形 `Glyph::advance` 之和逐字相等 |
| **红线 3** | **零新第三方依赖与零源码 patch**：仅消费仓内已 vendored 的 **FreeType 2.14.3**，**零修改 `third_party/` 源文件、零第三方源码 patch、零 `CMakeLists.txt` 变更**（实核 `sources.ecdi.cmake:42` 已含 `ftsynth.c`，且 `ftbase.c:34` 已 `#include "ftsnames.c"`，两项 FreeType 能力均已编入构建） | `git diff -- CMakeLists.txt third_party/` 为空 |

---

## §1 输入来源、历史先例与当前代码事实矩阵

### 1.1 问题输入与历史欠账

经过 Phase 36（`Font.weight`）、Phase 44（styled spans）、Phase 45（`Font.italic` + 混字号基线）与 `#91`/`#92`/`#93` 的推进，GDI 后端现代排版能力已基本完整，而开启 GL 后端时存在三处落差：

1. **无真粗体面的字体不合成加粗（`C36-4` 遗留）**：`SimSun`、`SimHei`、`KaiTi`、`FangSong` 等常用中文字体无独立 Bold 面。GDI 遇高字重自动合成加粗（`Test36SimSunSyntheticBoldWidthBand` 在 20px 下测得 `"测试文本 test 123"` 宽度 `170 → 183`，比值 `1.076`）；GL 链则直接回落常规面。
2. **无真斜体面的字体不合成斜切（Phase 45 `L45-1` 账本）**：Phase 45 `O45-1` 已普查 512 个系统字体面，证实 **CJK 族真斜体面数量为 0**，`Tahoma` 亦无真斜体面。GDI 下 `lfItalic=TRUE` 触发斜切；GL 下 `FaceIdFor(..., italic=true)` 返回常规 `FaceId`，所有中文斜体在 GL 侧静默变正体。
3. **中文字体族名解析失败（`#69` 遗留）**：传 `"宋体"`、`"微软雅黑"`、`"黑体"` 等中文族名时，GL 链因只比对英文注册表键与英文 `nameID-1 (0x0409)` 而告警失败，随即通过 `fallbackToDefault()`（`FontEngine.cpp:303-316` → `Win32FontSource::DefaultFontFile` `:92-120` 首位 `simsun.ttc` face 0）回落到 **SimSun 宋体**（例如请求 `"微软雅黑"` 会错落成 `SimSun` 宋体）。

### 1.2 源码事实矩阵（对 HEAD `36bb02d` 逐行核验）

| # | 位置（相对仓根 `d:\Project\ECDI\`） | 核实事实 |
|---|---|---|
| **K46-1** | `third_party/freetype/sources.ecdi.cmake:27, 42` · `ftbase.c:34` · `ftsynth.c:50-177` | `ftsynth.c` 已在编译列表；`ftsnames.c` 已由 `ftbase.c:34` `#include` 编入（故 `FT_Get_Sfnt_Name` 已在 `FontEngine.cpp:75` 现成可用）。`FT_GlyphSlot_Oblique(slot)` 调用 `Slant(slot, 0x0366A, 0)`（约 `12°` 斜切，**不修改 advance**）。`FT_GlyphSlot_AdjustWeight(slot, xdelta, ydelta)` 计算 **26.6 定点像素单位（`64` = `1px`）** 的 `xstr = x_ppem * xdelta / 1024`、`ystr = y_ppem * ydelta / 1024`，且同时增加 `metrics.height += ystr`、`metrics.horiBearingY += ystr`、`advance.x += xstr`、`horiAdvance += xstr`；其自身 bitmap 分支（`:135-138`）亦按 `xstr &= ~63; if (xstr == 0) xstr = 1 << 6;` 取整像素。固定 `0x0AAA` 在 `16px` 下 `xstr = 42`（`0.66px`）、`20px` 下 `xstr = 53`（`0.83px`）。 |
| **K46-2** | `ECDI/src/Render/FontEngine.cpp:151-194` | `AdvanceOf()` 走 `FT_Load_Char(face, cp, FT_LOAD_DEFAULT \| FT_LOAD_TARGET_NORMAL \| FT_LOAD_NO_BITMAP)`（无 `FT_LOAD_NO_HINTING`），随后按 **`face->glyph->advance.x >> 6`（整数截断，`:185, :192`）** 输出物理像素 advance。由于 CJK 全宽字形原始 `advance.x` 恒为 `ppem * 64`，任何 `xstr < 64` 会被 **`>> 6` 截断彻底吞没为 `0px`**；且即使目标为 `64`，若 `xdelta = 65536 / ppem`（整数除法截断），如 `20px` 得 `3276 → xstr=63`、`12px` 得 `5461 → xstr=63` 仍全部被吞！必须向上取整 **`xdelta = (65536 + ppem - 1) / ppem`**（且 `ppem` 必取 `face->size->metrics.x_ppem` 与 `AdjustWeight` 同源，严禁取 DIP `Font.size`）。 |
| **K46-3** | `ECDI/src/Render/FontEngine.cpp:716-786` | `GlyphByKey()` 走 `FT_Load_Glyph(..., FT_LOAD_DEFAULT \| FT_LOAD_TARGET_NORMAL \| FT_LOAD_NO_BITMAP)` 后调 `FT_Render_Glyph(..., FT_RENDER_MODE_NORMAL)`，且同样按 `face->glyph->advance.x >> 6`（`:756`）写回 `GlyphBitmap::advance`，当前无任何合成调用。因无 `FT_LOAD_NO_HINTING`，outline 先经过 hinting 再入合成步骤。 |
| **K46-4** | `ECDI/src/Render/FontEngine.h:30-42` · `ECDI/src/Render/GLRenderer.cpp:254-301` | `GlyphKey` 为 `{faceId, glyphIndex, pixelSize, hinting}`。`GLRenderer::DrawText` 只传 `faceId` 进 `GetOrCreate`，并按 FreeType 实际增大后的 `slot.w` 发射 quad（因此图集自身不裁斜体右探出，极端贴边裁剪发生在控件裁剪区），`Ascent`/`LineHeight` 取自 `face->size->metrics`。 |
| **K46-5** | `ECDI/src/Render/FontEngine.cpp:9, 101-115, 139, 253-284, 396-430` | `FontEngine.cpp:9` 明确架构不变量「`FT_*` 仅出现在本编译单元（pimpl）」。`faces` 声明为 `std::map<std::pair<std::string, int>, FaceEntry>`，`EnsureFace(path, index)` 仅接受 `(path, index)` 两参；`facesById` 为 `map<FaceId, FT_Face>`。无真斜体时 `:412` 调 `FaceIdFor(family, weight, false)` 返回与常规完全相同的 `FaceId`。 |
| **K46-6** | `ECDI/src/Tests/FontWeightTests.cpp:131-132, 179-209` · `WidgetTests.cpp:2927-2932` | `Test45ItalicFaceSelection`（`:197-201` `EXPECT_EQ(simsunIta, simsunReg)` 及 `:208` 缓存复查）与 `Test45StyledSpansTwoChainConsistency`（`WidgetTests.cpp:2931` `EXPECT_EQ(idCjkReg, idCjkItalic)`）共 **3 处**锁定「无真斜体时回落常规 `FaceId`」(`I45-italic-fallback`)；另有 `Test36FaceIdPerFamilyAndWeight`（`FontWeightTests.cpp:131-132`）显式断言 `FaceIdFor("微软雅黑") == FaceIdFor("")`（`P0-② ZH 边界钉`）。 |
| **K46-7** | `ECDI/src/Tests/FontWeightTests.cpp:68-89` · `Font.h:23, 27` · `FontEngine.h:92` | `Test36SimSunSyntheticBoldWidthBand` 测得 `20px` 下 `"测试文本 test 123"` 宽 `170 → 183`（ratio `1.076`）。`Font.h:23, 27` 明文 GL 不合成粗/斜体，`FontEngine.h:92` 明文 `ZH 族名不解析`。 |
| **K46-8** | `ECDI/src/Platform/Win32/Win32FontSource.cpp:1-209, 251-361` | `ForEachFontRegistryKey` 仅读取 **`HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts`**（不含 `HKCU` 每用户安装目录）。系统内置中文字体注册表键名**绝大多数为英文**，无法匹配主流中文族名。 |
| **K46-9** | `ECDI/src/Render/FontEngine.cpp:65-85, 271, 396` | `ReadNameId1En()` 只读 `name_id == 1 && platform_id == 3 && language_id == 0x409`；`FaceIdFor()` 在 `:396` 比较 `e->nameId1En != family`，将所有中文族名请求拒之门外。 |
| **K46-10** | `ECDI/src/Render/GLGlyphAtlas.h:47-98` · `GLGlyphAtlas.cpp:72-81, 132-176` · `GLRenderer.cpp:291-301` | `GLGlyphAtlas` 为 `1024×1024` 单页 shelf 分配，无淘汰。满时 `Allocate()` 返回 `Empty()`（`valid=false`，仅在首次打 1 条 Warning）；`GetOrCreate()`（`:174`）将失败结果也写入 `m_slots` 永久缓存不再重试；`GLRenderer::DrawText`（`:291`）对 `!slot.valid` 跳过发射 quad 只推进 `pen`，表现为字符缺失。 |
| **K46-11** | `ECDI/include/ECDI/Core/Font.h:1-30` · `FontSource.h:1-61` · `FontEngine.h:1-188` | 上层 `Font` 与 `SpanStyle` 字段完备，无需新增任何公共头文件或公共 API 方法/字段。 |
| **K46-12** | `ECDI/src/Render/GLRenderer.cpp:502-523` · `GLBackendTests.cpp:24-27` | `DrawImage` 每帧现建/现删纹理、以及缺少真实 WGL 离屏像素测试夹具，连同 `GLGlyphAtlas` 扩容/淘汰归入 Phase 47（`#95`）统筹，容量边界见 `D46-0`。 |

---

## §2 方案选型与架构分析

### 2.1 字体合成风格在管线中的落点与自由度分析

| 候选方案 | 核心思路 | 评估与必须配合的约束 |
|---|---|---|
| **方案 A（`FontEngine` 内部 `ftsynth` + 四元键虚拟 `FaceEntry`，倾向）** | 在 `FontEngine` 内部封装单一出口 `ApplySynthesis(slot, synthBold, synthItalic)`；`faces` 键扩为 `(path, index, synthBold, synthItalic)`（共享底层物理 `FT_Face`），`EnsureFace` 同步扩参。针对合成粗体：**显式引入按 `metrics.x_ppem` 向上取整计算整像素 `xdelta` 的设计自由度**：`xdelta = kPx * ((65536 + ppem - 1) / ppem)`（`ydelta = 0`，`kPx >= 1` 默认对齐整像素 `1px`，具体按 `O46-1` 校准）。 | ① 下游 `GlyphKey`、`GLGlyphAtlas`、`GLRenderer::DrawText`、`advanceCache`、`glyphCache`、`lineHeightCache`、`ascentCache` **零签名修改、天然隔离**；② **两大强制不变量**：(a) 多个 `FaceId` 共享同一物理 `FT_Face`（`facesById` 为多对一映射，析构按唯一物理句柄去重释放）；(b) 合成变换非持久状态，每次 `FT_Load_Char`/`FT_Load_Glyph` 后必须立即调用 `ApplySynthesis`。代价：须在 `D46-2` 显式登记 `I45-italic-fallback` 契约破坏。 |
| **方案 B（`FaceId` 保持纯物理面，`GlyphKey` 传递 `styleFlags`）** | 保持 `Test45ItalicFaceSelection` `EXPECT_EQ`；在 `GlyphKey`、各 cache 键与 `GLRenderer` 透传合成位。 | 侵入多处渲染与图集接口，扩散大。**不建议**。 |
| **方案 C（GPU 顶点倾斜 / 像素着色器加粗）** | 渲染 quad 时倾斜顶点，不走 FreeType。 | 破坏图集复用与测画同源（红线 2）。**否决**。 |

### 2.2 中文字体族名解析落点选择

| 候选方案 | 核心思路 | 评估 |
|---|---|---|
| **方案 1（英文快路径不动 + Miss 时惰性 `FT_Get_Sfnt_Name` 多语种 `nameID-1` 索引，倾向）** | 英文文件名直查与英文注册表路径保留 `nameID-1 EN` 首中早退快路径（`LocalizedScanCountForTests() == 0`）；仅 miss 时触发一次性惰性扫描，复用 FreeType `FT_Get_Sfnt_Name` 读取 `HKLM` 已安装系统字体（含 TTC 全子面）的多语言 `nameID-1` 与必要 `nameID-16`。层级分工由 **`D46-5`** 拍板（推荐 ⓒ：`FontSource` 仅枚举文件列表，`FT_*` 扫描全收拢进 `FontEngine.cpp`）。 | 消除 ~200+ 行自研 `ttcf` / `name` / UTF-16BE 解析器与边缘风险，自动支持 `HKLM` 系统注册的第三方中文字体。 |
| **方案 2（`Win32FontSource` 自研纯二进制 SFNT `name` 表头扫描）** | 手写解析 `ttcf`、`name` 表与 UTF-16BE 解码。 | 与 FreeType 已编译能力重复且有 TTC/编码边缘风险（见 `D46-5` ⓑ）。 |
| **方案 3（硬编码中英映射表）** | 静态表 `"宋体" -> "SimSun"`。 | 无法覆盖第三方中文字体。**否决**。 |
| **方案 4（调用 DirectWrite `IDWriteFontCollection` 按 locale 查族名+文件）** | 经 COM `dwrite.dll` 向系统查询。 | 引入系统 DLL 依赖与 COM 状态耦合，违反零不必要依赖原则。**否决**。 |

---

## §3 决策点（用户签字闸门 `D46-0`～`D46-5`）与待勘察项（`O46-1`～`O46-3`）

### 闸门 0（`D46-0`）：`GLGlyphAtlas` 容量耦合边界（P0-1）

- **问题**：合成样式使单族最多占用 4× 图集槽位（如单族 3000 汉字 @16px ≈ 86% 单页 1024²），满图集后 `GetOrCreate` 会缓存 `valid=false` 使字符永久跳过绘制。
- **候选项**：
  - **ⓐ 先在 Phase 46 补 `T46-8` 饱和可观测守卫（推荐），Phase 47（`#95`）紧接实施扩容/淘汰**：登记 Phase 46 交付期大规模多态 CJK 渲染为已知受限域，Phase 46 对图集满补齐可观测跳过计数/断言守卫，不提前改动 shelf 分配核心。
  - **ⓑ 将 `GLGlyphAtlas` 扩容或满图集重置/淘汰提前并入 Phase 46 一并交付**：消除窗口期退化风险，但增大本 Phase 修改面与测试面。
- **签字状态**：✅ **已于 2026-11-15 签字拍板 ⓐ**。

### 闸门 1（`D46-1`）：合成粗体阈值、`ceil` 整像素公式、`ydelta=0` 与 Hinting 顺序

1. **垂直口径**：按 GDI 纯水平加粗口径钉死 `FT_GlyphSlot_AdjustWeight(slot, xdelta, 0)`（**`ydelta = 0`**），防止超出 `Ascent` 线盒且避开 `#92` `EffectiveBottomOverhang` 无法吸收的顶部溢出。
2. **水平参数与红线 2 硬约束**：`AdvanceOf:192`/`GlyphByKey:756` 的 `>> 6` 截断与红线 2 要求 **`xstr >= 64`**。采用 **`xdelta = (65536 + ppem - 1) / ppem`**（**`ppem` 必取 `face->size->metrics.x_ppem` 与 `ftsynth.c:127` 同源**，严禁直接截断 `65536/ppem`——其在 `12/20/24/25px` 会算出 `xstr=63` 并被 `>> 6` 吞回 `+0px`）。
   - **ⓐ 采用 `xstr >= 64` 整像素对齐公式，并在 `12 / 16 / 20 / 24 / 32px` 五档按 `abs(FT_ratio - GDI_ratio) <= 0.02` 校验（倾向）**：若 `O46-1` 实测高字号（如 32px）下 GDI 阶跃到 `+2px`，则按实测阶跃律映射 `kPx(ppem)`。
   - **ⓑ 固定单一 em 比例（允许 `<25px` 被吞宽或高低字号发散）**：违背红线 2 整像素要求，**不推荐**。
3. **触发阈值**：Windows fake bold 通常始于 `600`（GDI `FW_SEMIBOLD`/`>550`），精确分界交 **`O46-1` 实测定值**。
4. **Hinting 顺序**：FreeType 声明先 hint 再 `AdjustWeight`/`Oblique` 会破坏网格微调。由 `O46-1` 出 `12/14/16px` A/B 灰度对照后裁：
   - **ⓐ 保持 `FT_LOAD_TARGET_NORMAL`，非目标如实记「合成态小字号微调不保证与 GDI 点阵对齐等价」（倾向）**；
   - **ⓑ 合成态启用 `FT_LOAD_NO_HINTING`**。
- **签字状态**：✅ **已于 2026-11-15 签字拍板 ⓐ**（`O46-1` 实测保留 `FT_LOAD_TARGET_NORMAL`）。

### 闸门 2（`D46-2`）：虚拟 `FaceEntry` 隔离与显式契约破坏登记（`CB-1`）

- **候选项**：
  - **ⓐ 采纳方案 A（`faces` 扩四元键），并显式登记「契约破坏 `CB-1`：`I45-italic-fallback` 作废」（倾向）**：作废 Phase 45「GL 无真斜体时 `FaceIdFor` 回落返回常规 `FaceId`」的旧契约，原位更新 **3 处**存量测试断言：`FontWeightTests.cpp:201`（`EXPECT_NE`）、`FontWeightTests.cpp:208`（`simsunIta`）、`WidgetTests.cpp:2931`（`EXPECT_NE`）。
  - **ⓑ 保留 `I45-italic-fallback` 旧契约，走方案 B（修改 `GlyphKey` 与渲染链路传参）**。
- **签字状态**：✅ **已于 2026-11-15 签字拍板 ⓐ**（含 `CB-1` 三处断言更新）。

### 闸门 3（`D46-3`）：合成斜体倾角与水平左/右探出（Italic Overhang）边界

- **候选项**：
  - **ⓐ 沿用 `FT_GlyphSlot_Oblique`（`0x0366A` ~12°，零 advance 增量，倾向）**：位图切入图集按增大后 `bitmap.width` 完整保存（图集自身不截断）；排版沿用 0% 宽增，**声明边界**：`EffectiveBottomOverhang` 仅管垂直下伸、不扩水平排版盒，斜切与粗体左/右探出越界均发生于控件级裁剪区（`PushClip`），不纳入本 Phase。
  - **ⓑ 强制追加水平右探出排版扩宽**：造成 GL/GDI `AutoSize` 宽分歧。
- **签字状态**：✅ **已于 2026-11-15 签字拍板 ⓐ**（`O46-2` 已补定量读数）。

### 闸门 4（`D46-4`）：中文字体族名解析细则与显式契约破坏登记（`CB-2`）

- **候选项**：
  - **ⓐ 英文快路径不变 + Miss 时惰性扫描多语种 `nameID-1` 解析，并显式登记「契约破坏 `CB-2`：`P0-② ZH 边界钉` 作废」（倾向）**：依据 `O46-3` 确定多语种 ID（`platform_id == 3, name_id == 1` 全语种）；原位更新 **`FontWeightTests.cpp:131-132`** 原 `EXPECT_EQ(FaceIdFor("微软雅黑"), FaceIdFor(""))` 为 **`== FaceIdFor("Microsoft YaHei")`**。
  - **ⓑ 仅维持英文支持或硬编码白名单**。
- **签字状态**：✅ **已于 2026-11-15 签字拍板 ⓐ**（`CB-2` 已由用户补签确认）。

### 闸门 5（`D46-5`）：`FT_Get_Sfnt_Name` 扫描边界与 `FontEngine` pimpl 隔离

- **背景**：`ftbase.c:34` 已包含 `ftsnames.c`（Zero CMake 改动）。若在 `Win32FontSource.cpp` 直接包含 FreeType 会打破 `FontEngine.cpp:9`「`FT_*` 仅出现在本编译单元（pimpl）」不变量。
- **候选项**：
  - **ⓒ `FontSource` 只新增带默认空实现的虚函数枚举全量系统已注册字体文件，全部 `FT_New_Face(..., -1)` + `FT_Get_Sfnt_Name` 本地化索引扫描完全收拢在 `FontEngine.cpp` 内执行（倾向）**：既零自研 SFNT 二进制解析、零 CMake 改动，又严守 `FontEngine.cpp:9` pimpl 隔离；`Win32FontSource` 只暴露已有注册表枚举能力，既有 test `FontSource` 零改动；且 `LocalizedScanCountForTests()` 观测缝天然落在 `FontEngine` 侧。
  - **ⓐ 将 `FT_Get_Sfnt_Name` 封装在独立的 `Render` 内部 helper 中由平台层间接调用**（比 ⓒ 多出一份小上下文管理，无明显优势）。
  - **ⓑ 在 `Win32FontSource` 内自研纯二进制 SFNT `name` 表头扫描**（零 FreeType 依赖且仅读文件头数 KB，但需自担 `ttcf`/编码边缘维护风险）。
- **签字状态**：✅ **已于 2026-11-15 签字拍板 ⓒ**（`O46-3` 单趟冷扫实测 96～103 ms）。

### 待勘察项（`O46-1`～`O46-3`，作为进初设的事实门槛）

| # | 勘察主题 | 探针目标与输出读数 |
|---|---|---|
| **O46-1** | **GDI 五档字号粗体实测定律、阈值与 Hinting 对照** | 在 `12 / 16 / 20 / 24 / 32px` 五档对 `SimSun`、`Microsoft YaHei`、`SimHei`、`Consolas` × `weight ∈ {400, 500, 550, 560, 600, 700, 900}` 实测：① GDI 开始合成加粗的精确 `lfWeight` 阈值；② 纯 CJK 串（`"测试文本中文"`）、混合串（`"测试文本 test 123"`）及单字 `"测"` 在五档下的 GDI 宽度增量（实证是否恒 `+1px/字` 或高字号阶跃）；③ 对照 `FT_GlyphSlot_AdjustWeight(slot, ceil(65536/ppem), 0)` 是否**对涵盖测试串的字族**在五档全面满足 `abs(FT_ratio - GDI_ratio) <= 0.02`（缺 CJK 的 `Consolas` 改验真粗体零漂移）；④ 对比 12/14/16px 下 `FT_LOAD_TARGET_NORMAL` vs `FT_LOAD_NO_HINTING`。 |
| **O46-2** | **合成斜体右探出定量与粗斜叠加顺序** | 对 `SimSun`、`Consolas`、`Microsoft YaHei` 在 `16 / 20px` 实测 `FT_GlyphSlot_Oblique` 的 `bitmap_left`、`bitmap.width`、`advance >> 6` 与右侧 overhang 像素量；验证「先 `AdjustWeight` 再 `Oblique`」vs「先 `Oblique` 再 `AdjustWeight`」在 `(700, true)` 下的轮廓质量。 |
| **O46-3** | **Win32 注册表字体全量扫描耗时与中文族名全集实测** | 对本机 `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts` 全部已安装条目实测：`D46-5` ⓒ（`FT_New_Face(..., -1)` + `FT_Get_Sfnt_Name`）vs ⓑ（纯二进制读文件头）冷启动耗时与内存占用，核对全量中文族名/TTC 子面与 `nameID-16` 命中一致性。 |

---

## §4 范围、非目标、容量耦合边界与预算

### 4.1 明确纳入范围（In Scope）

1. **`FontEngine` 合成粗体**：扩维 `faces` 四元键与 `EnsureFace`；实现单一出口 `ApplySynthesis`，对 `AdvanceOf` 与 `GlyphByKey` 同步施加 `FT_GlyphSlot_AdjustWeight(slot, xdelta, 0)`（强制整像素 `xstr >= 64`、`ydelta = 0`）。
2. **`FontEngine` 合成斜体**：无真斜体面按 `D46-3` 施加 `FT_GlyphSlot_Oblique`，支持 `(synthBold && synthItalic)` 叠加。
3. **中文字体族名解析（`#69` 闭合）**：英文快路径零损耗；按 `D46-5` 落点（倾向 ⓒ：`FontSource` 新增带默认实现的枚举虚函数、`FontEngine` 惰性构建本地化索引并暴露 `LocalizedScanCountForTests()`）支持中文/本地化族名精确解析。
4. **容量耦合守卫、契约登记与单测**：在 `GLGlyphAtlas` 补齐饱和可观测守卫（或按 `D46-0` ⓑ 调整），更新 `Font.h:23, 27` 与 `FontEngine.h:92` 注释，登记并更新 `CB-1`（3 处）与 `CB-2`（1 处）存量契约断言，补全 `T46-1`～`T46-8`。

### 4.2 明确非目标与已知边界（Out of Scope）

1. **`GLGlyphAtlas` 扩容/淘汰、`DrawImage` 纹理缓存与 WGL 离屏像素测试夹具**：排入 **Phase 47（`#95`）**；除非 `D46-0` 拍板取 ⓑ 提前并入图集扩容，否则 Phase 46 交付期存在 4× 槽位放大下满 1024² 图集受限边界。
2. **真粗体族超阈/中间档分歧维持 Phase 36 规则不变**：① 超出族内已有最粗真字重且高于阈值的档位（如 `Segoe UI` 已有 `700`，`900 vs 700`）不二次合成加粗；② `551..699` 档（如 `Microsoft YaHei` @ 560/600）GL 侧由 `PickNearestTier`（`FontWeightTests.cpp:98` 钉死）选 `700` 真粗体面，而 GDI 在 `400` 上合成加粗（`O46-1` 实测 `175 vs 166`），此既有档位差异不在 Phase 46 改写。
3. **左/右探出水平越界（斜体 + 粗体）改写排版宽或 `EffectiveBottomOverhang`**（斜切左/右 `1～4px` 与合成粗体右探出 `<= 1px` 同归控件 `PushClip` 裁剪）。
4. **跨链子像素级像素等价**（若 `D46-1` 保留 hinting，如实记「合成态小字号微调不保证与 GDI 点阵对齐等价」），以及 **纯拉丁字体（如 `Consolas`/`Tahoma`）遇到 CJK 文本时「GDI 字体回退 vs GL `.notdef`」的既有跨后端宽差**。
5. **非 Windows 字体源（Linux/Android）及 Windows `HKCU` 每用户字体目录**（`ForEachFontRegistryKey` 沿用仅扫描 `HKLM` 系统字体键）。

### 4.3 规模与影响预算

| 维度 | 预算 | 备注 |
|---|---|---|
| **公共头文件（`include/ECDI/**`）** | **102 → 102**（+0） | 仅更新 `Font.h` 两处注释 |
| **公共 API** | **+0 方法 / +0 字段 / +0 类型** | 复用既有 `Font{size, family, weight, italic}` |
| **预期源码改动范围** | `FontEngine.h/.cpp` · `FontSource.h` · `Win32FontSource.h/.cpp` · `GLGlyphAtlas.h/.cpp` · `Font.h`（注释） · `FontWeightTests.cpp` · `WidgetTests.cpp` | 共 **10** 个内部/测试文件（`Win32FontSource.h` 在 `src/Platform`，公共头 `102` 不变）；`GLGlyphAtlas` 仅增观察缝/单测；`GLRenderer.cpp` 0 改动；`WidgetTests.cpp` 仅原位更新 `:2931`（`CB-1`） |
| **CMake / 第三方** | **0 改动** | `ftsynth.c` 与 `ftsnames.c`（经 `ftbase.c:34`）均已编入构建 |
| **测试增量预估** | **+7**（**`615 → 622`**，确定值；若 `D46-0` 选 ⓑ 则初设另行改锚） | 新增 `T46-1, 2, 3, 5, 6, 7, 8` 共 **7 条**，`T46-4` 原位更新 `CB-1`（3 处）与 `CB-2`（1 处）（+0） |
| **设计文档增量** | **220 → 221**（递归 `docs/dev/` **219 → 220**） | 本需求稿维持 `221` 计数 |

---

## §5 验收标准与测试计划（草案 `T46-*`）

> 全部用例可在无 WGL 上下文的 `ecdi_tests` 中直接运行，直测 `Win32FontSource`、`FontEngine`、`FreeTypeTextMeasurer`、`GLGlyphAtlas`。

| 用例编号 | 检验目标 | 核心断言 |
|---|---|---|
| **T46-1** | 合成粗体五档双串校准与测画同源 | 分两相独立检验：**① 纯 CJK 串**（`"测试文本中文"`）与 **② 混合串**（`"测试文本 test 123"`），在 **`12 / 16 / 20 / 24 / 32px`** 五档下各自比对 GDI 实测宽度比：对**字形完整覆盖测试串的字族**（`SimSun`、`SimHei`、`Microsoft YaHei`）须满足 **`abs(FT_ratio - GDI_ratio) <= 0.02`** 且 `xstr >= 64` 整像素无吞宽；对缺 CJK 字形的纯拉丁族 **`Consolas`** 改验真粗体面改前/改后**逐位零漂移**；`FreeTypeTextMeasurer::MeasureText` 与逐字符 `Glyph::advance` 之和完全相等。 |
| **T46-2** | 合成斜体定量倾角 & 真斜体零重复倾斜 | 对无真斜体面的 `SimSun` / `Microsoft YaHei`（完整标定集 **`{'测', '国', 'H', 'W'}`**）与 `Tahoma`（标定拉丁集 **`{'H', 'W', 'A', 'I'}`**）请求 `italic=true`：`20px` 下灰度位图上、下半部质心 X 满足 **顶部重心右偏 >= 1.2px**（`O46-2` 实测 `SimSun`/`YaHei` `1.28～1.99px`、`Tahoma` `1.37～1.95px`），常规 face 同一标定集 `abs(dx) <= 0.4px`，`MeasureText` 宽度增量 = `0`；`Segoe UI` / `Consolas` 真斜体面不二次倾斜。 |
| **T46-3** | 四态隔离、缓存防投毒与「粗 + 斜」叠加 | `SimSun` 的 `(400,F)` / `(700,F)` / `(400,T)` / `(700,T)` 产出 4 个互异 `FaceId`；物理句柄安全共存不重复释放；`(700,T)` 总墨量 > `(400,T)` 且具备斜切质心错位；四态交替下 `familyCache`、`measureCache`、`ascentCache`、`lineHeightCache`、`advanceCache`、`glyphCache` 六组缓存零投毒。 |
| **T46-4** | **契约破坏原位更新（`CB-1` + `CB-2`）** | 原位更新 **+0 用例**：① `CB-1`：改 `FontWeightTests.cpp:201` 与 `WidgetTests.cpp:2931` 为 `EXPECT_NE`、`:208` 为 `simsunIta`；② `CB-2`：改 `FontWeightTests.cpp:132` 为 `FaceIdFor("微软雅黑") == FaceIdFor("Microsoft YaHei")` 且 `!= 默认`。 |
| **T46-5** | 中文字体族名硬性端到端解析 | 在中文 Windows / 存在 `simsun.ttc`、`msyh.ttc`、`simhei.ttf` 的环境中请求 **`"宋体"`、`"微软雅黑"`、`"黑体"` 必须硬命中且与英文原名测量及字形逐位等价**（**禁止遇到中文系统仍静默跳过**；`TestRunner` 无独立 Skip 态，仅非中文 CI 缺对应字体文件时执行**文件存在性守卫 + `Logger::Warning` 留痕 + 分段提前返回**，并在实跑汇总中明确标注「守卫性空转」条目）；另验证 TTC 子面 **`"新宋体"`** 命中 `simsun.ttc#1`（注：`msyh.ttc#1` 在 Windows 中 `0x0804`/`0x0409` 的 `nameID-1` 均为 `"Microsoft YaHei UI"`，不存在中文 `nameID-1` 记录，在 `n1-only` 索引下查询 `"微软雅黑 UI"` **预期 miss 并告警回退**）。 |
| **T46-6** | 中文族名 × 粗斜组合与未知兜底 | `"微软雅黑" @ 700, italic=true` 选真粗体面 + 合成斜切；`"宋体" @ 700, italic=true` 合成粗 + 斜；未知中文名 `"不存在字体XYZ"` 安全回落 `SimSun` 且告警不崩（仅非中文 CI 缺失 `msyh.ttc`/`simsun.ttc` 时触发同 `T46-5` 文件存在性守卫性提前返回 + `Logger::Warning` 留痕，中文 Windows 上必须实跑全部断言）。 |
| **T46-7** | 英文/文件名直写零冷扫描与红线 1 守卫 | 通过观测缝 `LocalizedScanCountForTests()`（归属随 `D46-5` ⓒ 置 `FontEngine`）验证查询 `"consola.ttf"`、`"Consolas"`、`"SimSun"`、`""` 全程 **`scanCount == 0`**（零冷扫描触发），且英文 `@400` 常规输出零漂移。 |
| **T46-8** | `GLGlyphAtlas` 饱和可观测守卫（`D46-0` ⓐ） | 显式填满 `GLGlyphAtlas` 1024² 容量后继续请求新字形：断言 `Allocate` 返回 `valid == false`、图集跳过计数/饱和状态可通过观测缝确定性观测到（非静默无痕迹），且不越界崩溃。 |

---

## §6 闸门签字页（全数闸门已于 2026-11-15 由用户签字拍板完毕）

| 闸门 | 议题 | 推荐倾向与附加条件 | 用户签字结论 |
|---|---|---|---|
| **D46-0** | `GLGlyphAtlas` 容量耦合边界 | **ⓐ**（Phase 46 补 `T46-8` 饱和可观测守卫，Phase 47 紧接实施扩容/淘汰） | ✅ **已拍板 ⓐ** |
| **D46-1** | 粗体阈值、`ceil` 整像素 `xdelta(ppem)`、`ydelta=0` 与 hinting | **ⓐ**（`ydelta=0` + `xstr>=64` 经 `(65536+ppem-1)/ppem` 对 `metrics.x_ppem`；五档校验与 hinting 由 `O46-1` 定） | ✅ **已拍板 ⓐ** |
| **D46-2** | 虚拟 `FaceEntry` & **显式契约破坏 `CB-1`** | **ⓐ**（`faces` 扩四元键 + 作废 **`I45-italic-fallback`**，更新 `FontWeightTests.cpp:201, :208` 与 `WidgetTests.cpp:2931`） | ✅ **已拍板 ⓐ**（`CB-1` 3 处确认） |
| **D46-3** | 合成斜体倾角与控件级右裁剪边界 | **ⓐ**（`FT_GlyphSlot_Oblique` 零 advance；图集完整储存、控件 `PushClip` 裁斜体/粗体左/右探出） | ✅ **已拍板 ⓐ** |
| **D46-4** | 中文字体族名解析细则 & **显式契约破坏 `CB-2`** | **ⓐ**（英文零扫描快路径 + Miss 时多语种 `nameID-1` 解析；作废 **`P0-② ZH 边界钉`**，改 `FontWeightTests.cpp:131-132`） | ✅ **已拍板 ⓐ**（`CB-2` 补签确认） |
| **D46-5** | `FT_Get_Sfnt_Name` 扫描边界与 pimpl 隔离 | **ⓒ**（`FontSource` 仅枚举文件、FreeType 扫描收拢 `FontEngine.cpp`，Zero CMake 改动，耗时看 `O46-3`） | ✅ **已拍板 ⓒ** |

---

## §7 修订记录

- **v1.0（2026-11-15）**：Phase 46 需求初稿落盘，建立 `K46-1`～`K46-12` 事实矩阵、闸门 `D46-1`～`D46-4` 与勘察 `O46-1`～`O46-3`，并单列 Phase 47（`#95`）。
- **v1.1（2026-11-15）**：吸收一轮评审（P0-1 / P1×4 / P2×11）：补图集容量耦合 `D46-0` 与 `T46-8`、`faces` 四元键双不变量、`ydelta=0`、`>> 6` 吞宽成因与五档校验、`I45-italic-fallback` 显式契约破坏及 DWrite 否决，核实 `ftsnames.c` 已随 `ftbase.c:34` 编入。
- **v1.2（2026-11-15）**：吸收二轮验算复核（P0-A/B + P1-C + `D46-5 ⓒ` + P2×5）：`ceil(65536/x_ppem)` 上取整、红线 2 `xstr>=64` 整像素、`D46-5 ⓒ` 守住 pimpl、`T46-1` 双串五档及预算表收敛；用户签字拍板 `D46-0~4: ⓐ`、`D46-5: ⓒ`。
- **v1.3（2026-11-15）**：吸收初设 Qwen4 评审（P1-1/2/3 + P2 关联项）：红线 1 与 `D46-4` 补登第 2 项契约破坏 `CB-2`（`FontWeightTests.cpp:131-132`）待补签；`CB-1` 补全 `FontWeightTests.cpp:208` 与 `WidgetTests.cpp:2931`；`T46-1` 比率校准限定覆盖测试串字族（`Consolas` 改验真粗体零漂移）并收敛 `T46-2`～`T46-4` 及非目标边界（`551..699` 档、`HKCU` 盲区、`FontEngine.h:92`）。
- **v1.4（2026-11-15）**：吸收 Qwen4 二轮评审（`P1-A ⓑ` + `P2-1/2/4`）并登记用户 `CB-2` 补签确认：`T46-2` 门槛调至 `>= 1.2px` 对齐全集 `{'测', '国', 'H', 'W'}` + `Tahoma` `{'H', 'W', 'A', 'I'}`；同步 §3 五处过期签字行、`O46-1 ③` 范围与 `T46-5` `"微软雅黑 UI"` 预期 miss 说明。
- **v1.5（2026-11-15）**：同步详设第 5 轮评审修订：§4.3 改动范围补列 `Win32FontSource.h`（共 10 个内部/测试文件，公共头文件 `102` 不变）；`T46-5`/`T46-6` 统一钉死非中文 CI 缺文件时的「文件存在性守卫性提前返回 + `Logger::Warning` 留痕」机制（零修改测试框架）。
