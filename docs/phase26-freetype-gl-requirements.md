# Phase 26 · FreeType 文本栈 + GL 渲染后端 —— 需求确认

> **版本：v1.2**（2026-10-03）｜**状态：待评审**（★ 已吸收外部评审第一轮，见 §7；四项关键边界转为初设必答，见 §6）
> **立项来源**（★ 多处登记 = 同一件事的多个视角，须同时对齐）：
> `roadmap-deferred.md` §7.9 **#44**（后端可替换性，**M-4 文本栅格化**）· `framework-defect-audit.md` §4 **D-8**（测量/渲染 DPI 基准不一致）· **★ 用户 2026-10-03 提出真实性能动机**：大文本 TextBox 滚动 CPU 暴增卡顿（§1.1）
> **前序**：Phase 25（列表/网格容器）已全链收口 ⇒ 框架侧**缺陷队列 ①–⑧ 全部出清、待做区清空**；本条是**已拍板未实施的框架侧最后大项**。

---

## 1. 背景

### 1.1 ★ 真实性能动机（用户 2026-10-03 提出）：大文本 TextBox 滚动 CPU 暴增

在测试项目中（ModelProbe 等），**大量内容在 TextBox 中滚动时 CPU 使用率暴增、明显卡顿**。本 Phase 的 GL 后端由此获得**第一个真实性能消费者**——不再只是路线图上的「既定方向」。

#### 机理（代码实证）

| # | 事实 | 证据 |
|---|---|---|
| K1 | TextBox 绘制 = **对每一可视行逐行 `ctx.DrawText`** | `TextBox.cpp:1297`（`ctx.DrawText(Point{ viewX, lineY }, lineText, ...)`） |
| K2 | 该调用落到 GDI = **每行一次 `SelectObject(HFONT)` + `TextOutW`** | `GDIBackend.cpp:385-396` |
| K3 | 滚动时整个客户区**全量重绘**（`InvalidateRect(nullptr)`） | `Win32PlatformWindow.cpp:322` |
| K4 | 文本测量/行缓存虽已惰性化（`TextBox.cpp:1183` `RecalculateLines`），但**绘制本身没有缓存** | `TextBox.cpp:1295-1297` |

⇒ **热点链**：滚动 → `Invalidate` → 每帧对全部可视行 `SelectObject`+`TextOutW` → 大文本（几千行）下 CPU 暴增。

#### 为什么 GL 后端能解

| # | 机制 | 说明 |
|---|---|---|
| G1 | **字形栅格化只做一次，缓存进图集** | FreeType 栅格化 → 纹理图集（spike S2/S3 已验证）→ 后续帧**零栅格化**，只做 UV 合成 |
| G2 | **绘制批量提交**（M-5 攒批友好） | `EndFrame` 一次提交，减少每帧 API 调用开销 |
| G3 | 与 `FreeTypeTextMeasurer` 同源同基准 | 文本栈整体替换，测量/渲染一致（S6） |

★ **本 Phase 的性能验收口径**（评审 §5 校准）：不看「GL CPU 更低」这类易被硬件偶然性左右的结论，而看 **warm-cache 滚动阶段「重复 glyph 不再发生 FreeType 光栅化」+「绘制提交不随字符数 × 行数线性膨胀」**；基准 contract 由初设建立（§2 R6 · §6 P6）。

### 1.2 这是什么：兑现 2026-10-01 已拍板的 FreeType 决策

| 账本 | 条号 | 原文要点 |
|---|---|---|
| 排期 | **#44 M-4** | 「**文本栅格化属后端职责**」——GL/Vulkan 后端必须**自己栅格化字形 + 维护纹理图集**；**与 `TextMeasurer` 成对替换**（`GLRenderer + FreeTypeTextMeasurer`） |
| 审计 | **D-8** | 测量链 `GDITextMeasurer` 用屏幕 DC 的 `LOGPIXELSX`（`GDITextMeasurer.cpp:76`）作基准，渲染链 `GDIBackend` 用 `GetDpiForWindow(m_hwnd)`（`GDIBackend.cpp:812`）——同一 `Font`（DIP）在两条链路按**不同 DPI** 换算 ⇒ 跨屏失配（例 120 vs 144 = **20%**） |
| 路线 | 用户 2026-09-23 明确 | **Windows = GDI（默认）→ Linux + 未来 Android = OpenGL / OpenGL ES → 等都能用了再学 Vulkan** |

★ **这不是新需求，而是既定路径的落地**（条 91 的「到期兑现」）：Phase 7.1 预留的配对方向（`OpenGLRenderer + FreeTypeTextMeasurer`）在 `RenderServices.h:13`、`BackendFactory.h:9`、`phase7-backend-*` 三处有明确登记。

### 1.3 spike 已证明什么（探针产出，` .workbuddy/spike/glbackend/`）

| # | spike 结论 | 证据 | 对 Phase 26 的含义 |
|---|---|---|---|
| S1 | `GLBackend : RenderingBackend` 可完整实现**全部十个纯虚** | `spike_gl_pipeline.cpp:470-735` | **虚函数面零改动成立**（此前仅 `RecordingBackend` 替身） |
| S2 | 覆盖度两层拆分在 GL 侧复用成立（圆角/线段/字形 A8 → 图集） | `:767-869` 图集 + shelf 分配 | 覆盖度层已抽出、平台无关，GL 直接复用 |
| S3 | M-4 自包含文本栈实证（迷你 TTF，14/14 断言） | `:1304-1448` | 管线可行；**正式版改用 FreeType** |
| S4 | 度量一致：同一字号 `bbox` 宽与 GDI 逐档一致（≤1px） | `text-quality-compare.md` §2.1 | advance 口径可对齐，不引入布局偏移 |
| S5 | **中文墨量 +31%~+95%** ⇒ 自研（无 hinting）观感不可接受 | `text-quality-compare.md` §2.2/§三 | **hinting 必须「买」** ⇒ FreeType 决策依据 |
| S6 | `DrawText` 契约显式化：`pos` = 物理像素、`font.size` = DIP、字形度量须与 `TextMeasurer` **同源同基准** | spike 文档 ⑥ | D-8 修法 = 给测量链一个「目标 DPI」 |

### 1.4 为什么现在做（时机）

1. **★ 真实性能消费者已出现（2026-10-03）**：大文本 TextBox 滚动 CPU 暴增（§1.1）——GL 后端不再是无消费者的「路线储备」，而是**解决实际卡顿的手段**。
2. **D-8 是 v1.0 API 冻结的硬前置**：修它必然触碰 `TextMeasurer`（接口或生命周期）⇒ **公共 API 变更** ⇒ 必须排在 v1.0 冻结之前。
3. **GL spike 上下文还热**：探针、对照图、14/14 断言、`text-quality-compare.md` 全部留存，是现成的实现输入。
4. **框架侧缺陷队列已清空**：这是**已拍板未实施的最后大项**，做完框架侧就只剩应用侧 DesktopNest 与 v1.0 收口。

---

## 2. 范围内（Scope）

| # | 项 | 说明 |
|---|---|---|
| R1 | **vendor FreeType** 进仓库 | FTL 许可 + **许可/归属清单**（license file · NOTICE / third-party attribution · version pin · 上游归档 · local patches）；最小配置（`ftoption.h` 关 PNG/brotli/SVG）；**字体源**本 Phase 仅 Windows 系统字体目录（★ 该路径属**字体源层**，`FontEngine` 不将其作为自身抽象——未来可换 `/usr/share/fonts` / fontconfig）；**资源上限**：最大字体文件 / glyph bitmap / atlas 尺寸 / 单次栅格化（初设量化） |
| R2 | **`FreeTypeTextMeasurer`** 实现 `TextMeasurer` | 与 GDI 测量器对等的能力；**测量基准 = 窗口 DPI**（修 D-8） |
| R3 | **`GLRenderer`** 实现 `RenderingBackend` | 基于 spike 的 `GLBackend`；纹理图集、覆盖度复用；★ **WGL 上下文 ownership / current / swap / resize / destroy / `Initialize` 线程**为初设必答（**P4**） |
| R4 | **`RenderServices` 注入通路打通** | `Application::Create(..., services)` 已透出（`Application.h:72`）——GL 后端经此注入 |
| R5 | **D-8 修复** | 测量链与渲染链同基准（窗口 DPI） |
| R6 | **测试 + 性能基准** | FreeType 测量器单元测试 + GL 后端录制/像素断言（复用 spike 的 14/14 断言思路）；★ **性能基准在初设建立可重复 contract**（**P6**）——固定参数（行数/字数/字体/字号/DPI/窗口尺寸/滚动速度/#帧/CPU 口径/VSync）且**区分 cold / warm**；需求阶段**不写具体数字** |
| R7 | **ModelProbe 可选切换** | 通过注入 GL 后端验证（**不改变默认后端**，见 D1）；★ **显式 `--gl` 时初始化失败不静默 fallback**（明确报错退出）——否则性能对照可能实际跑的是 GDI |

## 3. 关键决策点（含倾向，D1…Dn）

| # | 决策点 | 选项 | 倾向 |
|---|---|---|---|
| **D1** | **GL 后端首期是否替换默认后端** | A：GL 成为**默认**后端（GDI 退役） · B：GL 作为**可注入第二后端**，GDI 保持默认 | **B**（默认不动，注入切换；零回归护栏最强） |
| **D2** | **D-8 修法形态** | A：`TextMeasurer` 方法加 DPI 形参 · **A′：`measurer` 持「窗口级 DPI 状态」**（构造/注入时给定） · B：持窗口引用 · C：`RenderServices` 注入携 DPI | ★ **倾向 A′**（`measurer` 已是窗口级独占对象——`Window.h:321`；调用点 10+ 处）；**B 排除**（生命周期耦合）；★ **须由初设 spike P1 验证调用链后冻结** |
| **D3** | **FreeType 封装层形态** | A：每个后端各自裸用 FT · B：共享 `FontEngine` 封装 | **B**（同源同基准——S6）；★ **边界须初设收敛（P2）**：`FontEngine` = FreeType 共享底层（face / size / glyph / metrics / rasterize / **CPU cache**），**不含 GPU 资源、不含排版语义**——防膨胀成「文本排版引擎」 |
| **D4** | **`TextMeasurer` 接口是否扩展** | A：方法加 DPI 形参 · A′：加 `SetDpi`（窗口级状态） · B：两者都上 | ★ **随 D2 一并由 P1 冻结**；★ 措辞澄清：**`source-compatible` ≠ `semantic-compatible`**——本 Phase 恰恰**要改测量语义**（D-8），**不得**用「默认值零破坏」表述 |
| **D5** | **GL 后端是否实现全部十个纯虚** | A：全部实现（含 DrawImage/DrawFocusRect） · B：部分默认空实现 | **A**（spike 已证明可行——S1）；★ 补 **operation-level semantic contract**：不要求像素一致，要求「同 `RenderCommand` ⇒ 同语义操作」（`DrawFocusRect` 尤须写清） |
| **D6** | **GL 文本栅格化是否走 FreeType 图集** | A：FreeType 栅格化 → 图集缓存 → UV 合成 · B：每帧直接栅格化 | **A**；★ **Atlas 为独立设计对象（P3）**：key = face + size + **DPI** + hinting + glyph index（**DPI 必须入 key**）；★ **CPU 缓存与 GPU texture 分离**（`FontEngine` 不持 GL texture） |
| **D7** | **ModelProbe 是否默认切 GL** | A：默认 GL · B：仅命令行切换 · C：不接 | **B**（默认 GDI 不动，加 `--gl` 切换验证；★ **性能对照正是通过该切换验证**；★ **`--gl` 初始化失败明确报错、不静默回退 GDI**） |
| **D8** | **FreeType 构建集成方式** | A：源码编入 ECDI 静态库 · B：独立静态库目标 · C：FetchContent/外部依赖 | **A**（全自包含，四链一致；vendor 义务最小化）；★ 补**许可/归属 checklist**（见 R1） |

## 4. 范围外（非目标）

| # | 非目标 | 理由 |
|---|---|---|
| N1 | **不做后端间视觉等价**（AA/点线/alpha 契约化） | `#44` R-4 口径：spike 已明确「不承诺视觉等价」。★ **澄清**：不做**视觉等价** ≠ 不做**视觉验收**——GL 后端仍须目视确认（atlas / baseline / DPI / alpha / 字形位置），纯单元测试会漏 |
| N2 | **不退役 GDI 后端** | 默认保持 GDI（D1 倾向 B）；GDI 仍是零回归基线 |
| N3 | **不做 Linux/Android 平台适配** | 本 Phase 只做 Windows + WGL；Linux/EGL 是后续 |
| N4 | **不做离屏渲染/导出/headless 渲染目标** | `#44` M-1：自省够用；Android/离屏才是真消费者 |
| N5 | **不引入着色器/材质/GPU 批处理抽象** | YAGNI；GL 后端内部可攒批（M-5） |
| N6 | **不引入 shaping engine**（HarfBuzz 等），不做复杂文种 shaping | ★ 措辞精确化：**FreeType 本身不是 shaping engine**；本 Phase 边界 = FreeType glyph metrics / rasterization，**不承诺** Arabic / Indic / emoji / 连字 / combining marks 的 shaping（未来如需另加 shaping 层——不是「ECDI 永远不做」） |
| N7 | **不实现 `OnTargetResized` 接缝** | spike 确认 per-frame 自省够用（M-2 窗口型等价）；★ **初设给出「per-frame 自省 vs `OnTargetResized`」成本/生命周期对比后再冻结**（**P4**） |

## 5. 与既有约束的对齐（skill 条号对照）

| 约束 | 条号 | 本 Phase 如何满足 |
|---|---|---|
| 非必要不引入第三方 | 95⑦ | FreeType 已拍板（三条判据同时成立：字体引擎非本知识域 / 自研验收无法独立建立 / 愿承担跟版义务） |
| 渲染四层两两不相识 | 16/19 | GL 后端只实现 `RenderingBackend` 操作级接口，不接触 `RenderCommand`/variant |
| 平台解耦 = 每个 Win32 API 唯一归属 | 91 | WGL 归属 GL 后端；`PlatformRenderContext` 类型安全取 HWND |
| 公共 API 零 Win32 类型 | 100 | `TextMeasurer` 只加 DIP 形参（int），不暴露 HWND/HDC |
| YAGNI：第二个消费者才抽象 | 22 | GL 是第一个真实第二后端；`FontEngine` 共享层因「同源同基准」而生（S6） |
| 加纯虚先列实现者 | 33 | `TextMeasurer` 实现者清单 = GDITextMeasurer / RecordingBackend / FakeTextMeasurer×2（已勘察） |
| 测试断言启用 + 四链 | 35/50 | 验证沿用四链 + `_DEBUG` 判据 |
| 报全绿写明缩放 | 109 | 本机 125% 为基线；GL 观感验收需目视 |

## 6. 初设必答（P1–P6，外部评审给定优先级）

| # | 必答项 | 收敛目标 |
|---|---|---|
| **P1** | **`TextMeasurer` API / DPI** | 勘察全部调用点，定 DPI 是「显式参数」还是「窗口级状态」（**D2 / D4 冻结**） |
| **P2** | **`FontEngine` 最小边界** | 定 face / size / glyph / metrics / rasterize / cache 哪些属共享底层（**D3 冻结**） |
| **P3** | **Glyph Atlas** | key（含 DPI / hinting / glyph index）· 分配 · 淘汰 · 尺寸 · 上传 · UV（**D6 冻结**） |
| **P4** | **WGL 生命周期** | HWND → WGL → Backend → Renderer 的 ownership / 线程 / resize / destroy（**R3 / N7 冻结**） |
| **P5** | **`DrawText` RenderCommand** | 确认 `DrawTextCommand`（`RenderCommand.h:26`）字段；判定 GL 是否**零改 `RenderCommand`** |
| **P6** | **性能基准 contract** | GDI / GL × cold / warm；指标 = CPU / frame time / **glyph 栅格化次数** / atlas miss（**R6 冻结**） |

## 7. 外部评审处置（第一轮，2026-10-03）

> 评审结论：**「Requirements 基本通过，但建议先修 4 个关键边界，再进入 Preliminary Design」**；核心路线 `GDI 默认 + GL 可注入 + FreeType + 同源测量/渲染 + Windows/WGL 首期` 评审赞成，**无需推翻**。★ 四项关键边界 = **D2/D4（测量 DPI 归属）· D3（`FontEngine` 边界）· WGL 生命周期 · Atlas 生命周期**——全部转为初设必答（§6）。

| 评审条 | 要点 | 处置 |
|---|---|---|
| §1 | `FontEngine` 是「FreeType 封装」还是「文本渲染引擎」？须拆三层，防膨胀成排版系统 | ✅ 采纳 → **D3 补边界 + P2** |
| §2 | D2/D4 不宜直接拍 A；**不赞成 B**；先调查调用链再定 DPI 归属 | ✅ 采纳 → **D2 重述为 A′ + P1**（★ 证据支持：`measurer` 已是窗口级对象） |
| §3 | 「默认值兼容」措辞：`source-compatible ≠ semantic-compatible` | ✅ 采纳 → **D4 措辞修正** |
| §4 | R6 性能验收不够（缺参数、cold/warm）；需求阶段不写数字 | ✅ 采纳 → **R6 + P6** |
| §5 | 「GL 能解 CPU」需谨慎：真验收 = warm 重复 glyph 零光栅化 + 提交不线性膨胀 | ✅ 采纳 → **§1.1 验收口径改写** |
| §6 | R1 的「字体源仅 Windows 系统目录」应下沉到字体源层 | ✅ 采纳 → **R1 措辞** |
| §7 | ftoption 方向对；「字节码步数上限」应为明确资源约束 | ✅ 采纳 → **R1 补资源上限** |
| §8 | Glyph Atlas 应是独立设计对象（key 含 DPI；CPU/GPU 分离） | ✅ 采纳 → **D6 + P3** |
| §9 | D6 图集与 D3 `FontEngine` 明确分层 | ✅ 采纳 → **D3 结构澄清** |
| §10 | D5 赞成；`DrawFocusRect` 需 operation-level 语义 | ✅ 采纳 → **D5 补语义契约** |
| §11 | N1 对；但不要扩大成「不做视觉验收」 | ✅ 采纳 → **N1 澄清** |
| §12 | WGL 上下文生命周期必须初设解决 | ✅ 采纳 → **R3 + P4** |
| §13 | N7 `OnTargetResized` 不做需初设验证 | ✅ 采纳 → **N7 标注** |
| §14 | D8 vendor 赞成；补许可/归属 checklist | ✅ 采纳 → **R1 / D8 补 checklist** |
| §15 | 「同源同基准」最赞成 | ✅ 确认（未改动） |
| §16 | 「同源」≠「同一缓存」 | ✅ 采纳 → **D3 / §1.1 澄清** |
| §17 | D1 保持 GDI 默认 | ✅ 确认（未改动） |
| §18 | D7 `--gl` 合适 | ✅ 确认（未改动） |
| §19 | `--gl` 失败不应静默 fallback | ✅ 采纳 → **D7 / R7** |
| §20 | N6 措辞：FreeType 非 shaping engine | ✅ 采纳 → **N6 改写** |
| §21 | 初设重点做 6 个 spike（P1–P6） | ✅ 采纳 → **§6** |

★ **最终判断（评审 21 条）**：**13 项 ✅ 确认 + 8 项 ⚠️ 需初设收敛**——8 项 ⚠️ = D2 先 spike · `FontEngine` 边界 · Atlas 生命周期 · WGL ownership · 性能验收标准化 · 字体路径下沉 · `OnTargetResized` 先验证；**全部采纳，路线无推翻**。

## 8. 修订记录

- **v1.2**（2026-10-03）**吸收外部评审（第一轮）**。① ★ 新增 **§6 初设必答（P1–P6）** 与 **§7 外部评审处置**（21 条逐条 + 最终判断：13 ✅ + 8 ⚠️ 全采纳）。② ★ **D2 重述**：由「拍 A」改为「**A′ 倾向（`measurer` 持窗口级 DPI）+ 初设 spike P1 验证后冻结**」，**排除 B**（持窗口引用）；补证据 = `TextMeasurer` 已是窗口级独占对象（`Window.h:321`）。③ **D3/D6 补充**：`FontEngine` 边界（不含 GPU 资源/排版语义）· Atlas 独立设计对象（key 含 DPI）· CPU cache 与 GPU texture 分离。④ **D4 措辞**：`source-compatible ≠ semantic-compatible`——本 Phase 要改测量语义，不用「默认值零破坏」。⑤ **D5 补** operation-level semantic contract · **R1/D8 补**许可与资源上限 · **D7/R7 补** `--gl` 失败不静默 fallback。⑥ **R3 补** WGL 生命周期初设必答 · **R6 改为**需求不写数字、初设建立 benchmark contract（cold/warm）。⑦ **N1 澄清**（≠ 不做视觉验收）· **N6 措辞**（不引入 shaping engine）· **N7**（初设成本对比后冻结）。
- **v1.1**（2026-10-03）**新增真实性能动机（用户提出）**。① ★ 新增 **§1.1**「大文本 TextBox 滚动 CPU 暴增」——含机理（K1–K4，代码实证）与 GL 后端解法（G1–G3）；本 Phase 的 GL 后端由此获得**第一个真实性能消费者**。② 头部立项来源补登记。③ §1.4 时机条补「性能消费者已出现」并顺延原三条。④ **R6 扩展为「测试 + 性能对比」**（同一大文本 TextBox 在 GDI vs GL 后端下的滚动 CPU/帧率对照）；**D7 倾向 B 补充「性能对照正是通过该切换验证」**。⑤ 章节重排：1.1 性能动机 / 1.2 是什么 / 1.3 spike / 1.4 时机。
- **v1.0**（2026-10-03）初稿：立项三处登记对齐 · spike 六结论（S1–S6）· 范围 R1–R7 · 决策点 D1–D8（含倾向）· 非目标 N1–N7 · 既有约束对齐。待评审。
