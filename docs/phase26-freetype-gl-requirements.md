# Phase 26 · FreeType 文本栈 + GL 渲染后端 —— 需求确认

> **版本：v1.0**（2026-10-03）｜**状态：待评审**
> **立项来源**（★ 多处登记 = 同一件事的多个视角，须同时对齐）：
> `roadmap-deferred.md` §7.9 **#44**（后端可替换性，**M-4 文本栅格化**）· `framework-defect-audit.md` §4 **D-8**（测量/渲染 DPI 基准不一致）· `desktopnest-roadmap.md` 间接（GL 后端为 Linux/Android 路线的前置）
> **前序**：Phase 25（列表/网格容器）已全链收口 ⇒ 框架侧**缺陷队列 ①–⑧ 全部出清、待做区清空**；本条是**已拍板未实施的框架侧最后大项**。

---

## 1. 背景

### 1.1 这是什么：兑现 2026-10-01 已拍板的 FreeType 决策

| 账本 | 条号 | 原文要点 |
|---|---|---|
| 排期 | **#44 M-4** | 「**文本栅格化属后端职责**」——GL/Vulkan 后端必须**自己栅格化字形 + 维护纹理图集**；**与 `TextMeasurer` 成对替换**（`GLRenderer + FreeTypeTextMeasurer`） |
| 审计 | **D-8** | 测量链 `GDITextMeasurer` 用屏幕 DC 的 `LOGPIXELSX`（`GDITextMeasurer.cpp:76`）作基准，渲染链 `GDIBackend` 用 `GetDpiForWindow(m_hwnd)`（`GDIBackend.cpp:812`）——同一 `Font`（DIP）在两条链路按**不同 DPI** 换算 ⇒ 跨屏失配（例 120 vs 144 = **20%**） |
| 路线 | 用户 2026-09-23 明确 | **Windows = GDI（默认）→ Linux + 未来 Android = OpenGL / OpenGL ES → 等都能用了再学 Vulkan** |

★ **这不是新需求，而是既定路径的落地**（条 91 的「到期兑现」）：Phase 7.1 预留的配对方向（`OpenGLRenderer + FreeTypeTextMeasurer`）在 `RenderServices.h:13`、`BackendFactory.h:9`、`phase7-backend-*` 三处有明确登记。

### 1.2 spike 已证明什么（探针产出，` .workbuddy/spike/glbackend/`）

| # | spike 结论 | 证据 | 对 Phase 26 的含义 |
|---|---|---|---|
| S1 | `GLBackend : RenderingBackend` 可完整实现**全部十个纯虚** | `spike_gl_pipeline.cpp:470-735` | **虚函数面零改动成立**（此前仅 `RecordingBackend` 替身） |
| S2 | 覆盖度两层拆分在 GL 侧复用成立（圆角/线段/字形 A8 → 图集） | `:767-869` 图集 + shelf 分配 | 覆盖度层已抽出、平台无关，GL 直接复用 |
| S3 | M-4 自包含文本栈实证（迷你 TTF，14/14 断言） | `:1304-1448` | 管线可行；**正式版改用 FreeType** |
| S4 | 度量一致：同一字号 `bbox` 宽与 GDI 逐档一致（≤1px） | `text-quality-compare.md` §2.1 | advance 口径可对齐，不引入布局偏移 |
| S5 | **中文墨量 +31%~+95%** ⇒ 自研（无 hinting）观感不可接受 | `text-quality-compare.md` §2.2/§三 | **hinting 必须「买」** ⇒ FreeType 决策依据 |
| S6 | `DrawText` 契约显式化：`pos` = 物理像素、`font.size` = DIP、字形度量须与 `TextMeasurer` **同源同基准** | spike 文档 ⑥ | D-8 修法 = 给测量链一个「目标 DPI」 |

### 1.3 为什么现在做（时机）

1. **D-8 是 v1.0 API 冻结的硬前置**：修它必然触碰 `TextMeasurer`（接口或生命周期）⇒ **公共 API 变更** ⇒ 必须排在 v1.0 冻结之前。
2. **GL spike 上下文还热**：探针、对照图、14/14 断言、`text-quality-compare.md` 全部留存，是现成的实现输入。
3. **框架侧缺陷队列已清空**：这是**已拍板未实施的最后大项**，做完框架侧就只剩应用侧 DesktopNest 与 v1.0 收口。

---

## 2. 范围内（Scope）

| # | 项 | 说明 |
|---|---|---|
| R1 | **vendor FreeType** 进仓库 | FTL 许可；最小配置（`ftoption.h` 关 PNG/brotli/SVG）；只加载 `C://Windows//Fonts` 系统字体；保留字节码步数上限 |
| R2 | **`FreeTypeTextMeasurer`** 实现 `TextMeasurer` | 与 GDI 测量器对等的能力；**测量基准 = 窗口 DPI**（修 D-8） |
| R3 | **`GLRenderer`** 实现 `RenderingBackend` | 基于 spike 的 `GLBackend`；WGL 上下文、纹理图集、覆盖度复用 |
| R4 | **`RenderServices` 注入通路打通** | `Application::Create(..., services)` 已透出（`Application.h:72`）——GL 后端经此注入 |
| R5 | **D-8 修复** | 测量链与渲染链同基准（窗口 DPI） |
| R6 | **测试** | FreeType 测量器单元测试 + GL 后端录制/像素断言（复用 spike 的 14/14 断言思路） |
| R7 | **ModelProbe 可选切换** | 通过注入 GL 后端验证（**不改变默认后端**，见 D1） |

## 3. 关键决策点（含倾向，D1…Dn）

| # | 决策点 | 选项 | 倾向 |
|---|---|---|---|
| **D1** | **GL 后端首期是否替换默认后端** | A：GL 成为**默认**后端（GDI 退役） · B：GL 作为**可注入第二后端**，GDI 保持默认 | **B**（默认不动，注入切换；零回归护栏最强） |
| **D2** | **D-8 修法形态** | A：`TextMeasurer` 方法加 DPI 形参 · B：测量器生命周期改为窗口级（持有窗口引用）· C：`RenderServices` 注入时携带目标 DPI | **A**（最小侵入；默认值兼容既有调用） |
| **D3** | **FreeType 封装层形态** | A：每个后端各自裸用 FT · B：共享 `FontEngine` 封装（加载/缓存/栅格化） | **B**（`GLRenderer` 与 `FreeTypeTextMeasurer` 同源同基准——S6 要求） |
| **D4** | **`TextMeasurer` 接口是否扩展** | A：仅加 DPI 形参（默认值） · B：加 `SetDpi`/生命周期方法 | **A**（最小 API 面；默认值零破坏） |
| **D5** | **GL 后端是否实现全部十个纯虚** | A：全部实现（含 DrawImage/DrawFocusRect） · B：部分默认空实现 | **A**（spike 已证明可行——S1） |
| **D6** | **GL 文本栅格化是否走 FreeType 图集** | A：FreeType 栅格化 → 图集缓存 → UV 合成 · B：每帧直接栅格化 | **A**（spike 已验证图集方案，M-5 攒批友好） |
| **D7** | **ModelProbe 是否默认切 GL** | A：默认 GL · B：仅命令行切换 · C：不接 | **B**（默认 GDI 不动，加 `--gl` 切换验证） |
| **D8** | **FreeType 构建集成方式** | A：源码编入 ECDI 静态库 · B：独立静态库目标 · C：FetchContent/外部依赖 | **A**（全自包含，四链一致；vendor 义务最小化） |

## 4. 范围外（非目标）

| # | 非目标 | 理由 |
|---|---|---|
| N1 | **不做后端间视觉等价**（AA/点线/alpha 契约化） | `#44` R-4 口径：spike 已明确「不承诺视觉等价」 |
| N2 | **不退役 GDI 后端** | 默认保持 GDI（D1 倾向 B）；GDI 仍是零回归基线 |
| N3 | **不做 Linux/Android 平台适配** | 本 Phase 只做 Windows + WGL；Linux/EGL 是后续 |
| N4 | **不做离屏渲染/导出/headless 渲染目标** | `#44` M-1：自省够用；Android/离屏才是真消费者 |
| N5 | **不引入着色器/材质/GPU 批处理抽象** | YAGNI；GL 后端内部可攒批（M-5） |
| N6 | **不做复杂文种/kerning/连字优化** | spike 局限如实记录；FreeType 能力之上不做额外排版引擎 |
| N7 | **不实现 `OnTargetResized` 接缝** | spike S 确认 per-frame 自省够用（M-2 窗口型等价） |

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

## 6. 修订记录

- **v1.0**（2026-10-03）初稿：立项三处登记对齐 · spike 六结论（S1–S6）· 范围 R1–R7 · 决策点 D1–D8（含倾向）· 非目标 N1–N7 · 既有约束对齐。待评审。
