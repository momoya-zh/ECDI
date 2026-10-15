# Phase 32 · 窗口雾面衬底（Frosted Backdrop）—— 详细设计（v1.1）

> 来源：初设 `phase32-frosted-backdrop-preliminary-design.md` **v1.1（进详设）** §8 移交十项 + **探针 8**（`.workbuddy/tmp/dn_acrylic/probe8/`，零仓库侵入）。
> 状态：**v1.2（2026-10-14 · #58 关闭 ⇒ Phase 32 收口）**——v1.1 外部评审「有条件通过」五项已吸收 ⇒ 进实施；**批一–批四 + #57/#58 均已关闭**（§10.1–§10.5）；★ 本版新增 **§10.5**（#58 双触发量定案）与 **C32-12 修订**（nColor RGB = tint）；四链 445/445。
> 定位：把初设 D32-A..J 落到**函数级**——「三层修补语义 + 后缓冲 DIB 化 + 模式分支提交」的实现契约。

---

## §1 详设必答清单（初设 §8 ①–⑩）

| # | 移交项 | 本文档 |
|---|---|---|
| ① | AA 边缘量化判据（A2 端到端） | **D32-P** |
| ② | 重应用触发全集 | **D32-O** |
| ③ | 注入缝精确形态 | **D32-N** |
| ④ | T32 逐条精确断言与装置 | **§5** |
| ⑤ | 逐文件 △ 与批次内文件序 | **§4 / §6** |
| ⑥ | tint-内容冲突契约措辞 | **D32-Q** |
| ⑦ | A8 目视矩阵 | **§7** |
| ⑧ | 圆角/AA 修补语义探针裁决 + 覆盖率修补评估 | **§2 Q-A/Q-B + D32-K** |
| ⑨ | 降级 × 内容类型矩阵 | **§2 Q-C/Q-D + D32-K③** |
| ⑩ | 「尚未应用」查询语义 | **D32-M** |

---

## §2 详设期取证（探针 8，2026-10-11 实测）

> 方法：沿探针 6/7 脚手架。8 格（RC1/RC2/EMP/AL1/AL2/L1/DG1/SW1）+ 离屏 TC1。运行环境 Win11 26340，全部格 `centerTop=SELF`。产物：`probe8.cpp / probe8.log / probe8_after.bmp / probe8_after2.bmp`。

### 2.1 读数

| 格 | 配置 | 关键读数 | 结论 |
|---|---|---|---|
| **RC1** | 圆角（r=28）**不透明混合**语义：RGB = color×cov + tint×(1−cov)、A=255 | corner-vs-EMP = **0.00** · interiorErr = **0.00** · bgAbove-vs-EMP = **0.00** | 角外透材质、内部精确——**形状正确** |
| **RC2** | 圆角**预乘 alpha** 语义：RGB = color×cov、A = 255×cov | 同上三项全 **0.00** | **同样形状正确**，且边缘合成正确（见 Q-B） |
| **EMP** | 亚克力 + tint-A0 纯材质参照 | deltaAB 42.88 / 方差比 0.001 / FROSTED | 量具稳定（与探针 7 M0 逐位同族） |
| **AL1** | 全缓冲 A=128 灰 + **BitBlt** | 实测 (146.9,163.3,168.3) vs 预乘 over 预测 (147.0,163.5,168.4)——**误差 ≈0.1** | ★★ **中间 alpha 经 BitBlt 精确保真**（既不钳 255 也不清 0——排除了 SNAP-OPAQUE 与 ADDITIVE 两替代解释） |
| **AL2** | 同缓冲 + **AlphaBlend** | 与预测不符（R 恰 128.0 ⇒ 与目标 DC 既有内容混合——dst 垃圾参与） | 再证 **Acrylic 路径必须 BitBlt**（D32-G 不变）；AlphaBlend 只属 Degraded |
| **L1** | 3px 线 + 哨兵扫描 | FROSTED（var 0.455）/ deltaAB 40.18 | 线（部分覆盖）哨兵修补成立 |
| **DG1** | 降级白 204 + **AA 深字** + 哨兵(204) + AlphaBlend | 背景 MIXED、文字核心精确（抓屏 `probe8_after.bmp`） | 降级 × AA 文字成立（P5 的扩展） |
| **SW1** | 亚克力 → **state 0 移除** → 不透明清屏 | distFill = **0.00** · deltaAB = **0.00** ⇒ RESTORED-OPAQUE | **accent 可编程移除且无残留**——None 回退路径成立 |
| **TC1** | 离屏：AA 文字双底色差分，统计「GDI 写过但恰等于 tint」 | **collisions = 0 / 38400 touched（0.0000%）** | 哨兵碰撞 = 已知限制，本配置下**实测为零** |

★ **方法论勘误（如实入档）**：① probe7/8 的 `SentinelFixup` 首版通道映射错位（B 字节比了 R 值）——probe7 的黑/白哨兵**三通道对称**故未暴露，probe8 的 tint 哨兵（56,47,43）三通道不等才揭出，已修；教训 = **哨兵判据必须按缓冲字节序（B,G,R）映射**（C32-3 的实现红线）。② Q-A 首版角外采样区 {62,72}-{84,94} 大部分落在弧内（对弧心距离 < r）⇒ 读数无效；修正为 {61,71}-{67,77}（距离 ≥29.7 > 28）后得 0.00。

### 2.2 作答

- **Q-A（圆角两语义）**：两种语义**形状都正确**（角外 = 材质 0.00）。裁决依据转为**边缘合成正确性 + 与清屏色解耦**：预乘语义（RC2）的边缘 = color×cov 以 A=255×cov 合成——材质按比例透过边缘，且**不依赖「tint 恰为视觉背景」**；不透明语义（RC1）的边缘 = 对 tint 混色后整体不透明。**AL1 证明 BitBlt 保真后，RC2 无实现障碍** ⇒ **D32-K② 取预乘**。
- **Q-B（中间 alpha 保真）**：BitBlt 把缓冲 alpha **原样送达重定向表面**且 DWM 按预乘 over 合成——A 通道是一等公民（这同时是 K13/P1 机理的补全：此前只证过 0/255 两端）。
- **Q-C/Q-D（降级矩阵 + 线）**：线与降级 AA 文字均成立；矩阵余项（图像半透明像素/模式切换）由 T32-4/7 + 批三勘察承接。
- **Q-E（切换）**：`state=0` 可编程移除 accent；不透明清屏恢复**零残留**。

---

## §3 详设决策 D32-K..Q

### D32-K 修补语义三层定案（细化 D32-F；初设 §8⑧⑨ 关闭）

| 层 | 命令 | 修补 | 证据 |
|---|---|---|---|
| **K①** | `DrawRect`（实心、无 AA） | 矩形级 A=255 | probe7 Z1 误差 0.00 |
| **K②** | `DrawRoundedRect`（含 AA 覆盖度路径） | **覆盖率后处理 = 预乘改写**：绘制后沿 `CornerCoverageMask` 遍历其区域——`cov==1` ⇒ A=255（RGB 已是 color）；`0<cov<1` ⇒ **RGB=color×cov、A=255×cov**（覆写 GDI 的对背景混色）；`cov==0` ⇒ A=0 且 RGB=tint（GDI 若写过混色须还原） | probe8 RC2 全 0.00 + AL1 保真 |
| **K③** | `DrawText` / `DrawLine` / `DrawFocusRect` | 命令矩形 ∩ `GetClipBox` 内**哨兵扫描**（A=0 且 RGB≠清屏 ⇒ A=255）。★ **v1.1 定位（外部评审 P1-2）= 二值 alpha 近似修补**：A 改 255 的 AA 边缘像素以「不透明混色」呈现——**覆盖率未被重建**（0.3 覆盖率的边缘会以混色色整体盖在材质上），非通用 AA 重建算法；字形核心精确、字缝透材质、边缘近似 = 其保证边界 | probe7 Z1/T2 + probe8 L1；碰撞 = 已知限制（TC1：0/38400——证明无精确碰撞，**不证明覆盖率已恢复**） |
| **K④** | `DrawImage` | ★ **v1.1 范围决策（外部评审 P1-3——二选一钉死，消除契约自相矛盾）**：「**按源 alpha 与材质合成**」= **Phase 32 明确不支持（挂账 #56）**；本 Phase 验收 = **不透明图像像素精确** + 半透明像素按「先与清屏色合成、再不透明盖上」呈现（**已知限制**写入 API 契约与 §7 A8 目视项）。执行端现状 = Phase 8 链路（32bpp 顶降 DIB + AlphaBlend，`GDIBackend.cpp:633`）——批三验证不透明路径精确；手工预乘 over（K② 家族，解码位 32bpp 现成）= **#56 重启时的实现方向**。「保留源 alpha 禁矩形级」收窄为底线规则：**禁把图像矩形无差别 A=255** | 评审 P1-3 + deferred #56 |
| — | `PushClip`/`PopClip` | 无像素，不修补 | — |

★ **修补与裁剪的交含（C32-3 落地）**：修补矩形 = 命令矩形 ∩ `GetClipBox(memoryDC)`（D28-C 已证 GDIBackend 裁剪 = SaveDC 栈，`GetClipBox` 读有效裁剪）——不引入第二裁剪真相。

### D32-L 中间 alpha 的 BitBlt 保真（新契约）

**C32-9**：32bpp DIB 经 `BitBlt(SRCCOPY)` 提交后，重定向表面的 alpha = 缓冲 alpha（逐字节），DWM 按**预乘 over** 合成（AL1：A=128 实测误差 ≈0.1）。该事实是 K② 的可行性前提，也是 A1 机制验收的判读模型。★ 探针 8 之前 K13/P1 只证过 0/255 两端——本条把契约补全到 [0,255] 全域。

### D32-M 未应用查询语义（初设 §8⑩ 关闭；评审 P2）

- `WindowBackdropState { BackdropMode requested; BackdropMode actual; bool applied; }`——**轻量 applied 标记**（不新增公共枚举值）。
- `Show()` 前：`applied = false`、`actual = None`（**未应用 ≠ 应用失败**——调用方先查 `applied`）；`Show()` 后 `applied = true` 且 actual = 最近一次应用结果。
- `SetWindowBackdrop(Degraded, …)` ⇒ **完全忽略且不改变既有请求态**（Warning 一次）。
- ★ **v1.1 补（外部评审 P2-6）**：`PlatformWindow` 默认实现（no-op + `{None, None, false}`）的语义 = **「该平台未实现此能力」**，**不是**「该平台已成功应用 None」——写入 `WindowBackdrop.h` 契约注释；未来第二平台接入时调用方据 `applied` 区分二者。

### D32-N 失败注入缝（初设 §8③ 关闭）

两级注入，**零公共 API 污染**：
1. **Window 级状态机**：`TestPlatformWindow`（既有测试替身）override 新虚函数模拟 `actual = Degraded`——测 `Window` 的查询/转发逻辑，无需触碰 Win32。
2. **Win32 解析/调用级**：`Win32PlatformWindow.cpp` 文件内静态 `s_backdropInjection`（**internal linkage**，枚举 `enum class BackdropInjection { None, FailResolve, FailCall }`——**两类故障分开模拟**）+ `SetBackdropInjectionForTests(BackdropInjection)` 声明于同目录内部头 `Win32PlatformWindowInternal.h`（**新增内部头，不入公共头计数**）——测试 include 之（`WindowBackgroundTests.cpp:11` 先例）。grep 锚 = `backdropInjection`。★ **v1.1 注入 × 分支覆盖矩阵（外部评审 P2-4）**：`FailResolve` ⇒ 验证「解析失败**不调用未解析函数** + 直接进 Degraded」；`FailCall` ⇒ 验证「调用返回失败 ⇒ 状态翻转 + 无材质残留 + renderer 收到 Degraded 清屏语义」；「Acrylic→Degraded 后提交路径切换」由 T32-4 缓冲级 commit 分支断言覆盖；「解析成功但系统不支持」与 FailCall 同路径。★ **全局开关的串行假设**：注入标志为进程级——注册表模型串行执行，T32-5 注释钉明**不得**用于多窗口并行场景。
   - 拒绝的候选：公共静态 setter（污染公共面）/ friend（Phase 18 红线 6 同族禁令）/ 环境变量（非确定性）。

### D32-O 重应用触发全集（初设 §8② 关闭）

| 触发 | 动作 | 落点 |
|---|---|---|
| `Show()` | 首次应用（`m_shown=true` 后、`UpdateWindow` 后） | `Show()` 尾部 |
| `WM_DPICHANGED`（既有 case `:706`） | 追加重应用调用 | 既有 case 内 |
| `WM_DWMCOMPOSITIONCHANGED`（**新增 case**） | 重应用（DWM 开关/主题变化后 accent 可能失效） | WndProc 新分支 |
| `Release()` | HWND 销毁 ⇒ actual 随对象消亡（新 HWND = 新状态，**不跨 HWND 泄漏**——C32-6） | 既有清理路径 |
- 其余（主题变更 `WM_THEMECHANGED` 等）：**第一版不挂**——accent 是 DWM 合成属性，composition-changed 已覆盖主失效源；出现真实失效场景再增（条 22）。

### D32-P AA 边缘验收判据（初设 §8① 关闭）

- **缓冲级（机检，T32-3）**：文字矩形内全体像素满足「A∈{0,255} 且 (A=255 ⇔ RGB≠清屏)」——哨兵语义的精确不变式。
- **端到端（抓屏，批四探针）**：① 字形核心精确数 > 200（probe7/8 实测 682）；② 字带内**哨兵色残留 = 0**（字缝透材质）；③ **碰撞率基线**：TC1 型双底色差分 collisions = 0（本配置实测）——A2 判据按「三同时」落判；④ ★ **v1.1 新增（外部评审 P1-2）AA 边缘观感矩阵**：多背景 × 多 tint × 多文字颜色下的抓屏目视——**哨兵不变式通过 ≠ 边缘视觉合格**，两者分开判。
- **K② 边缘（机检，T32-2b）**：mask 区域内 A = 255×cov ±1、RGB = color×cov ±1（对 mask 逐像素复核）。

### D32-Q tint 契约措辞（初设 §8⑥ 关闭）

API 文档（`WindowBackdrop.h` 注释）原文：「**tint 是雾面色调的唯一权威来源**：其 alpha 分量驱动 DWM 材质浓淡、RGB 分量作为缓冲清屏色。**已知限制**：内容像素 RGB 恰等于 tint RGB 时哨兵不可判（材质透过该像素）——建议 tint 与内容主色错开；AA 边缘混色亦可能碰触该值（实测基线 0/38400，见 probe8 TC1）。」

---

## §4 逐文件 △（9 处；★ = 本 Phase 改动对象）

| △ | 文件 | 内容 |
|---|---|---|
| **△1** | `include/ECDI/Window/WindowBackdrop.h` **新公共头（95→96）** | `enum class BackdropMode { None, Acrylic, Degraded }` + `struct WindowBackdropState { requested, actual, applied }` + D32-Q 契约注释；叶子头零依赖（BOM + 缺省值陷阱注释：`BackdropMode{}` = None = 正确缺省 ✓） |
| **△2** | `include/ECDI/Window/Window.h` + `src/Window/Window.cpp` | +成员（requested/tint）· `SetWindowBackdrop`（配置期闸门沿 `SetChromeMode` 同款「Show 后 Warning+忽略」；Degraded 请求 = 忽略不改状态；**转发平台**）· `GetWindowBackdropState`（requested 取本地、actual/applied 取平台）——状态真源分工 = 决策层在 `Window`、应用事实在平台（Phase 18 同构） |
| **△3** | `include/ECDI/Platform/PlatformWindow.h` | **+2 带默认实现 virtual**：`SetWindowBackdrop(BackdropMode, const Color&) {}`（no-op）· `GetWindowBackdropState() const { return {None, None, false}; }`——`TestPlatformWindow`×2 / 未来平台**零改动**（S7/C32-8） |
| **△4** | `src/Platform/Win32/Win32PlatformWindow.{h,cpp}` ★ | 存 mode/tint · `Show()` 尾部 `ApplyBackdrop()` · override 两虚函数 · **动态解析**（lazy + 缓存；`GetModuleHandleW(L"user32.dll")` + `GetProcAddress`——全仓首例，失败容忍沿 R6）· tint → `nColor = (a8<<24)`（**RGB 分量 = 黑**——D32-F：tint RGB 由缓冲清屏承载，防双重 tint；C32-5 修订）· `ApplyBackdrop`：None ⇒ 已应用过则 `state=0` 移除（SW1 证）；Acrylic ⇒ 设 policy + `actual=Acrylic`；解析/调用失败 ⇒ **立即切换 Degraded 语义**（通知 renderer，见 △5）`actual=Degraded` · `WM_DPICHANGED` 追加重应用 + 新增 `WM_DWMCOMPOSITIONCHANGED` case · 注入缝（D32-N） |
| **△5** | `include/ECDI/Render/RenderingBackend.h` + `src/Render/Renderer.h` | ★ **v1.1 重定（外部评审 P1-1——公共头签名不得引用内部类型，前置声明亦构成公共依赖）**：`RenderingBackend` **+1 带默认实现 virtual**：`SetBackdropSurface(BackdropMode actual, std::uint32_t clearABGR) {}`——**签名只用既有公共枚举 + 原语**：零新公共类型、零内部类型泄漏、零 GDI 名词（第二后端可实现，A9② 保持）；提交机制抉择（Acrylic→BitBlt / Degraded→AlphaBlend）= **GDIBackend 私有**（actual→提交映射在其内部）· v1.0 的内部头 `BackdropSurface.h` 与 `BackdropSurfaceState` **取消**（边界冲突源）· `Renderer` +1 同签名单行转发（不持有状态——Phase 18 红线 7 不破）· `GLRenderer`/`RecordingBackend` **零改动** |
| **△6** | `src/Render/GDIBackend.{h,cpp}` ★ | `EnsureBackBuffer` → **CreateDIBSection 32bpp top-down**（尺寸自检重建逐字保留；+`m_dibBits`）· `BeginFrame` 清屏三分支（None = 既有 FillRect 逐位不变；Acrylic = DWORD 填 tint-A0；Degraded = memset 204）· `EndFrame` 提交分支（BitBlt / AlphaBlend）· **每命令修补**（K①..K④；`GetClipBox` 交含）· `DrawRoundedRect` 雾面分支 = mask 后处理预乘改写（K②）· **内部观测缝** `DebugBackBufferBits() const`（内部头可达——`WindowBackgroundTests.cpp:11` 先例；零公共 API）· 失败容忍姿态沿决策 30 |
| **△7** | `src/Platform/Win32/Win32PlatformWindowInternal.h` **新内部头** | `SetBackdropForceFailForTests(bool)` 声明（D32-N；不入公共头计数） |
| **△8** | `src/Tests/WindowBackdropTests.cpp` **新测试文件** | T32-1..8（§5）；TEST_SOURCES 走 GLOB（CMakeLists:153）⇒ **CMake 0** |
| **△9** | `CMakeLists.txt` | **0 改动**（FRAMEWORK_SOURCES/PUBLIC_HEADERS/TEST_SOURCES 均 GLOB_RECURSE——核验过 :80/:136/:153） |

## §5 T32-1..8 精确断言（用例 366 → **374**）

| # | 用例 | 装置 | 断言（精确） |
|---|---|---|---|
| T32-1 | 默认零回归 | 直构 `GDIBackend`（隐藏窗口） | `BeginFrame(Color::White)` 后 bits：RGB=(255,255,255) 逐位、**A=0**；`DrawRect` 后矩形 RGB=绘制色——与 Phase 31 基线逐位同（A6①） |
| T32-2 | Acrylic 缓冲语义 | 同上 + `SetBackdropSurface(BackdropMode::Acrylic, tintABGR)` | 清屏后全缓冲 = (tintRGB, A=0)；`DrawRect` 后矩形内 A=255 且 RGB=绘制色；矩形外 (tint, 0) |
| T32-2b | 圆角预乘改写 | 同上 + `DrawRoundedRect`（有 mask 路径） | mask 域逐像素：A = 255×cov ±1 且 RGB = color×cov ±1；cov=0 处 = (tint, 0)（D32-P） |
| T32-3 | 文字**哨兵修补**不变式（v1.1 更名定位） | 同上 + NONAA `DrawText` | 文字矩形内全体像素：A∈{0,255} 且 A=255 ⇔ RGB≠tint；A=255 计数 > 0 且 A=0 计数 > 0（字缝）。★ **本断言 = 哨兵算法的实现不变式，不承担 AA 边缘视觉正确性验收**（后者 = 批四端到端矩阵，D32-P④） |
| T32-4 | Degraded 缓冲语义 | `SetBackdropSurface(BackdropMode::Degraded, 0xCCCCCCCCu)` | 清屏后全缓冲 = 204；`DrawRect` 后矩形 A=255；提交分支断言（Acrylic→BitBlt / Degraded→AlphaBlend 的映射 = GDIBackend 私有，经内部 accessor 验证 Degraded 走 AlphaBlend） |
| T32-5 | 失败注入状态机 | **真窗口** + `SetBackdropForceFailForTests(true)` → `Show()` | `GetWindowBackdropState()` = `{Acrylic, Degraded, true}`；日志含降级 Warning |
| T32-6 | 配置期契约 | 真窗口 | Show 后 `SetWindowBackdrop` ⇒ Warning + 状态不变；`SetWindowBackdrop(Degraded,…)` ⇒ 状态不变（完全忽略） |
| T32-7 | 模式切换无残留 | 直构 backend | Acrylic 清屏 → bits=(tint,0)；切 None 清屏 → bits=(底色, A=0) **全缓冲无旧值**；再切回 Acrylic 同理（SW1 的缓冲级形态） |
| T32-8 | 结构性判据（A9②） | grep 脚本 | `WindowBackdrop.h` / `Window.h` 新声明无 Win32 类型；新虚函数全部带默认实现体（`{` 非纯虚）——沿 Phase 18 A4 形态 |

**批四库外探针（不入注册表）**：真实 `GDIBackend` + 真实 `RenderCommand` + 真实控件（**D6 阶段 B**）——复刻 probe7/8 判据（双区差分 / 控件误差 / 文字三同时 / K② mask 复核）打在框架真管线上；A8 可自动化部分（DPI 切换触发重应用）+ 目视矩阵（§7）。

## §6 批次（四批）

- **批一 = 缓冲地基**：△1△2△3△5△7 + △6 的 DIB 切换/清屏三分支/提交分支 + T32-1/4/7（纯缓冲级，无 DWM 依赖）——**零 DWM 风险先落地**。
- **批二 = 材质应用与状态机**：△4 全部 + T32-5/6/8。
- **批三 = 修补**：△6 的 K①..K④ + T32-2/2b/3（含 DrawImage 实测裁决 K④）。
- **批四 = 阶段 B 收口**：库外 E2E 探针 + **AA 边缘端到端观感矩阵**（多背景 × tint × 文字颜色——D32-P④；若现明显色偏 ⇒ 启动覆盖率重建评估，不自动默认全帧扫描）+ A8 + 五处台账收口回写（§10）。

## §7 契约增补与 A8 目视矩阵

**新增契约**：C32-9（D32-L BitBlt 保真）· C32-10（K② 预乘改写公式）· C32-11（修补矩形 = 命令矩形 ∩ GetClipBox——**哨兵按 B,G,R 字节序比对**，probe8 勘误红线）· C32-12（**nColor RGB = tint**（通道序 `AABBGGRR`）+ tint RGB 亦由清屏承载——C32-5 的修订版；★ **2026-10-14 #58 修订**：原「RGB=黑，防双重 tint」经实测**证否**，见 §10.5）。
**A8 目视矩阵**（人工）：跨屏拖动 / 跨 DPI（100%↔150%）/ 最大化-还原 / 远程会话——每项查「材质连续、圆角不错位、控件清晰、无残留」。

## §8 风险与移交实施

风险 **中低**：机理三层全部实测闭合；剩余 = K④ 图像路径的 AlphaBlend dst-alpha 不可靠性（批三实测裁决，(b) 兜底）与 DWM 跨环境行为（A8 人工）。移交实施 = §4 逐文件 + §5 断言 + §6 批次；实施回填落 §10。

## §9 修订记录

- **v1.2**（2026-10-14）**#58 关闭 ⇒ Phase 32 全链收口**。① ★★ **新增 §10.5**（#58 双触发量定案：`DwmExtendFrameIntoClientArea` 左/右/底边必须为 0 · `nColor` RGB 必须携带 tint）。② ★ **C32-12 修订**：「nColor RGB = 黑」→「**nColor RGB = tint（通道序 `AABBGGRR`）**」——原「防双重 tint」的担忧经实测不成立（dwm nColor RGB 是**设定** tint，非叠加；RGB=tint 与 probe8 已知良好参照 `EMP` 格最大偏差仅 7.6/255）。③ ★ **初设稿 DWM tint 行加事后注记**（`phase32-frosted-backdrop-preliminary-design.md` D32-F 表内）——保留 v1.0/v1.1 历史决策记录，同时标明现行契约，避免后来者误引。④ ★ **`Win32PlatformWindow.cpp:136` 的过期注释订正**（原写「nColor = (alpha << 24)，RGB = 黑」，与修复后的代码不符——条 80）。⑤ ★ **E2E 探针三处判据缺陷如实登记**（J4a 亮字形方向写反 = 原有假绿 / J2 before 抓屏竞态 / J5·J6 底衬覆盖不足）——均为**库外探针缺陷，非框架回归**。⑥ ★ **验证**：MinGW `ecdi_tests` **445/445**（Debug，本机实测）· MSVC Debug+Release 445/445 · Clang / ClangCL 445/445（用户侧实测）· Acrylic 窗跨区差 **0.00 → 45.20** · 控件 Δ=0.0 · None 窗逐像素 A/B（噪声底 = 0）差异仅最外 1px 边带（≤15/255）。⑦ **`roadmap-deferred.md` v2.30** · `framework-defect-audit.md` v1.86 · 两份 README 索引同步。
- **v1.1**（2026-10-11）**外部评审吸收（v1.0 评审结论：有条件通过）⇒ 进实施**。★ 评审保留项（不推翻）：正交 API / DIB 统一 / mask 圆角 / 双提交 / 四批。★ **五项修订处置**：
  · **P1-1（公共接口 × 内部类型边界）** ✅ 采纳且取**更干净解**——公共虚函数签名改为 `SetBackdropSurface(BackdropMode actual, std::uint32_t clearABGR)`（**既有公共枚举 + 原语**：零新公共类型、零内部类型泄漏、零 GDI 名词，第二后端可实现）；内部头 `BackdropSurface.h` 与 `BackdropSurfaceState` **取消**；提交机制抉择（BitBlt/AlphaBlend）收归 GDIBackend 私有（actual→提交映射）。
  · **P1-2（文字 AA 保证范围）** ✅ 采纳——K③ 定位为「**二值 alpha 近似修补**」（A=255 的边缘像素以不透明混色呈现、覆盖率未重建，非通用 AA 重建算法）；T32-3 更名「文字哨兵修补不变式」= **实现不变式，不承担视觉验收**；AA 边缘端到端观感矩阵（多背景 × tint × 文字颜色）入批四（D32-P④）；现明显色偏 ⇒ 启动覆盖率重建评估（TC1 证明的是无精确碰撞，**不是**覆盖率已恢复——评审原话入档）。
  · **P1-3（DrawImage 契约自相矛盾）** ✅ 采纳——**范围决策钉死**：「按源 alpha 与材质合成」= **Phase 32 明确不支持（挂账 #56）**；本 Phase 验收 = 不透明图像像素精确 + 半透明像素按「先与清屏色合成、再不透明盖上」呈现（已知限制入 API 契约与 A8 目视项）；「保留源 alpha 禁矩形级」收窄为「禁整矩形无差别 A=255」的底线。
  · **P2-4（注入覆盖矩阵）** ✅ 采纳——注入升级为枚举 `{ None, FailResolve, FailCall }`（两类故障分开模拟）+ **注入 × 分支覆盖矩阵**（FailResolve ⇒ 不调用未解析函数；FailCall ⇒ 状态翻转 + 无残留 + renderer 收到 Degraded 语义；提交路径切换由 T32-4 缓冲级断言）+ 全局开关**串行测试假设**钉明。
  · **P2-5（T32-3 定位）** ✅ 采纳（并入 P1-2 处置）。**P2-6（默认实现语义）** ✅ 采纳——`PlatformWindow` 默认实现 = 「该平台未实现此能力」而非「已成功应用 None」，写入 `WindowBackdrop.h` 契约。**P2-7（§10 缺失）** ✅ 采纳——补 §10 实施回填模板（偏差/测试/验证/限制/台账五栏）。
  ★ 评审为静态文档评审；其指出的边界冲突真实成立（公共头签名引用内部类型，即使前置声明也构成公共依赖）——回源核验后采纳。

- **v1.0**（2026-10-11）详设初稿。**输入**：初设 v1.1 §8 十项 + **探针 8**（TC1 碰撞 0/38400 · RC1/RC2 角外/内部/背景三项全 0.00 · **AL1 中间 alpha BitBlt 精确保真**（预测误差 ≈0.1）· AL2 dst 垃圾参与再证 Acrylic 必 BitBlt · L1/DG1 线与降级 AA 成立 · SW1 state-0 移除零残留）。**§3** D32-K（三层修补：rect-A255 / **mask 预乘改写** / 哨兵；DrawImage 保留源 alpha 批三裁决）· D32-L（C32-9 BitBlt 保真全域 [0,255]）· D32-M（applied 标记）· D32-N（两级注入缝，internal linkage + 内部头）· D32-O（触发全集 + state-0 移除）· D32-P（判据量化）· D32-Q（tint 契约措辞）。**§4** △1..△9（公共头 95→96 · API +2 方法 +1 枚举 +1 值类型 +1 bool 字段 · 实现者 7 零改动 · CMake 0 · 用例 366→374）· **§5** T32 精确断言 · **§6** 四批。★ 方法论勘误入档：哨兵通道映射（probe8 揭出）+ 角外采样区修正。★ 未改框架代码。

---

## §10 实施回填（v1.1 建模板——批一..批四逐批回写，外部评审 P2-7）

| 项 | 回写内容 |
|---|---|
| **偏差** | 实际实现与本设计的逐条偏差（含勘误编号 E-…） |
| **测试** | T32-1..8 结果 + 运行链（MinGW / Clang / ClangCL / MSVC）+ 用例总数 |
| **验证** | 库外 E2E 探针读数（D6 阶段 B）+ A8 目视矩阵结果 |
| **限制** | 已知限制清单的最终状态（文字 AA 近似 / DrawImage 半透明[#56] / tint 碰撞） |
| **台账** | 五处台账收口状态（本文件 §10 + framework-defect-audit + roadmap-deferred + docs/README + 根 README） |

### §10.5 #58 关闭（2026-10-14，真机视觉整合——**两个触发量，均单变量实测定案**）

> **背景**：批四 E2E 登记 #58「accent 调用成功但合成视觉 = 白」，**阻塞本 Phase 收口**。
> 本次定位并关闭。探针 = `.workbuddy/spike/p58/`（12 支，零仓库侵入），分级报告 = 该目录 `REPORT-58.md`。

| 项 | 内容 |
|---|---|
| **故障本体** | `SetWindowBackdrop(Acrylic)` 后框架窗**屏幕合成 = 整片纯白**（320×210 中 98.3% 为 `255,255,255`），而 Button **精确等于绘制色**。★ 三面同读证明**不是渲染链问题**：框架后缓冲 `BGRA(43,46,56,**0**)` ✅ / 窗口 DC `(43,46,56)` ✅ / **屏幕 `(255,255,255)`** ❌ ⇒ 缺口在「窗口形态 × DWM 合成」层。**与 GL 后端、与文字视觉效果无关**（`--gl` 未参与该链）。 |
| **为何探针 6/7/8 未撞上** | 三份源码 grep **零命中** `DwmExtendFrameIntoClientArea`——Phase 12 的 DWM 增强只存在于框架里 ⇒ 不是环境差异，而是**探针缺了这项**。 |
| **触发量①** | `ApplyDwmEnhancements` 的 `MARGINS{1,1,1,1}`：**左/右/底任一边非 0** ⇒ DWM 把客户区当**玻璃延伸区**合成 ⇒ **A=0 的背景像素被填成系统主题色**（浅色主题 = 纯白），而 A=255 控件照常精确。★ 同窗口内 A=0 背景带 + A=255 控件块验证：背景 `255,255,255` / 控件 `Δ=0.0`。★ **MARGINS 字段序 = `{cxLeftWidth, cxRightWidth, cyTopHeight, cyBottomHeight}`**（非 `{左,上,右,下}`——首版探针传反、作废重做）。 |
| **触发量②** | `ComposeAccentColor` 的 **nColor RGB = 黑**（原 C32-12 设计）：实测**证否**「防双重 tint」——RGB=0 不是中性而是「**60% 黑 tint**」，把材质压成单色。`p58r` 单变量（margins 固定 `{0,0,1,0}`）：`0x99000000` ⇒ 跨区差 **3.88**（不可见）/ `0x992B2F38` ⇒ **45.68**（可见）/ `0x99FFFFFF` ⇒ 0.00 纯白。⇒ **RGB 必须携带 tint**；且 RGB=tint 与 **probe8 已知良好参照 `EMP` 格**最大偏差仅 **7.6/255** ⇒ **不存在叠加**。 |
| **★ 通道序（易错点）** | nColor = **`AABBGGRR`（byte0 = R）**，与缓冲清屏色的 **`0x00RRGGBB`（byte0 = B，E-B2-4）互为镜像**。`p58s` 用**中性灰底衬 + 纯红两编码**定案：`0x99FF0000` ⇒ 偏**蓝** R−B=−136.3 / `0x990000FF` ⇒ 偏**红** R−B=+114.0 / 绿对照两序一致。★ **写错只表现为色调互换**，在「有/无材质」判据下**完全测不出** ⇒ 必须用颜色本身作判据。 |
| **修法** | ① `MARGINS margins{ 0, 0, 1, 0 };`（仅顶 1px——**材质可见 ∧ 阴影保留**；全 0 则四边阴影梯度归零）。② `ComposeAccentColor` 返回 `alpha \| tint.b<<16 \| tint.g<<8 \| tint.r`（表格内 `\|` 为转义竖线）。 |
| **契约修订** | ★ **C32-12 修订**：「nColor RGB = 黑」→「**nColor RGB = tint（通道序 `AABBGGRR`）；实测无双重 tint**」。原防双重 tint 的担忧经实测不成立（dwm nColor RGB 是**设定** tint，非叠加）。 |
| **保留项** | 系统阴影（实测 `{0,0,1,0}` 与 `{1,1,1,1}` 的四边阴影读数逐位相同）+ 客户区第 0 行 1px 玻璃带（**现状即有**，非本次引入）。 |
| **验证** | 本机 MinGW `ecdi_tests` **445/445**（Debug，退出码 0）；Acrylic 窗跨区差 **0.00（修复前）→ 3.88（只改①）→ 45.20（①②）**；控件像素 `Δ=0.0`；None 窗 A/B **逐像素**（噪声底实测 = **0**）差异 1125/67200、最大 **15/255**、**全部落在最外 1px 边带**（玻璃带由四边减到顶边一条——A1「无条件改」的如实交代）。 |
| **★ 附带（E2E 探针自身三处缺陷，非框架回归）** | **J4a**「亮字形」判据方向写反（Label 默认**黑字** ⇒ 修复前 `lum>140` 数到的**全是白背景 = 原有假绿**）；**J2** 的 `before` 抓屏未等 DWM 重合成（**逐字复刻**同几何独立复现 `varRatio=0.001` = **PASS**，e2e 自己跑出 17.122）；**J5/J6** 底衬仅 320×210 且位置固定 ⇒ maximize 后窗口已不在其上（`p58t` 底衬铺 1600×1000、窗口跨分界：基线 **41.41** → Restore 后 **41.41** ⇒ **材质保留，框架无需把 WM_SIZE 加入重应用触发集**）。 |
| **台账** | 本行 + deferred **v2.30**（#58 关闭 + #57/#58 两处串行错账归位）+ audit ⑨ 行 + docs/README + 根 README（同批） |
| **仍未做** | 其余三条工具链（Clang / ClangCL / MSVC）复跑（本机 MinGW 已全绿） |

### §10.4 批四（2026-10-11，库外 E2E + A8——**部分判定 + #58 关键发现** ⇒ ★ 已于 2026-10-14 由 §10.5 关闭）

| 项 | 内容 |
|---|---|
| **落地** | 库外探针 `.workbuddy/spike/p32/e2e.cpp`（MinGW g++ 链 HEAD `libECDI.a` + 纯公共头 + `CreateDefaultRenderServices`——T29-6 消费同构；真实 `Application/Window/Button/Label` + probe8 式底衬窗/抓屏/双区差分/区域统计）· A8 可自动化面（Maximize/Restore 循环 + 状态稳定断言）· AA 边缘 crop 存档（`p32_e2e_after.bmp`） |
| **测试** | **ClangCL / Clang 381/381 全绿**（MinGW 待 #57）· E2E：**J1/J3a/J4a/J5/J7 PASS；J2/J3b/J4b/J6 FAIL（被 #58 阻塞）** |
| **偏差** | ★ **E-B2-4（clear 布局）**：`ComposeTintClear` 的缓冲布局 = **0x00RRGGBB**（小端 BGRA：byte0 = 值低字节 = B）——首版按「AABBGGRR」组装成 (b<<16) ⇒ **R/B 互换**（E2E 的 SBS 抓屏 `clear=002B2E38` vs 期望 `00382E2B` 定位）；★ **发现 #58（视觉整合缺口）**：accent 调用真机成功（J1c）+ 下发链逐站验证到位（Window→renderer→backend 的 SYNCD/SBS 诊断）**但框架窗口的合成视觉 = 白**——同调用在手工 Win32 探针（6/7/8）上可见 ⇒ 差异在框架窗口形态（DPI 缩放尺寸/主题/窗口形状）的某处，**未定位** |
| **限制** | ① 视觉整合 = **#58**（雾面端到端视觉验收——A1/A2 的端到端面、AA 观感矩阵的通过判定一并挂起）⇒ ★ **已于 2026-10-14 关闭，见 §10.5**；② MinGW 链 = **#57** ⇒ ★ 同日修复关闭 |
| **台账** | 本行 + audit v1.82 + deferred v2.16（#58 登记）+ docs/README + 根 README（同批提交） |
| **批四追加（同日）** | ★ **#57 修复落地（Application 析构期容器改写 UB——MinGW 全套 381/381 解禁）**：机制 = 成员析构序（m_deferredDestroy 声明在后 ⇒ 先亡）× m_windows 析构中同步 WM_DESTROY → OnWindowDestroyed 向**已析构 vector** emplace_back（0xC0000374 直接来源）+ erase 收缩 finish 到析构循环指针之后 ⇒ 循环越尾（第二重 UB）；触发条件 = Application 析构时仍有存活窗口（M1 的 DesktopNestApp 按 C-M1-20 由基类持有 ⇒ 全套必现；生产正常退出 m_windows 已空不触发）。修复 = `m_shuttingDown` 旗标（~Application 第一件事置位）+ 显式排水（m_deferredDestroy/m_windows clear，OnWindowDestroyed 旗标直返）——**三链 381/381 全绿（MinGW 解禁）**；回归锚 = MinGW 全套（崩溃即失败，无需专用用例——UB 的失败形态就是崩溃）。详见 audit v1.83 |

### §10.3 批三（2026-10-11，每命令 alpha 修补）

| 项 | 内容 |
|---|---|
| **落地** | K①（`ForceRectAlphaFrosted`——DrawRect a=1 的 FillRect 后矩形级 A=255）· K②（`WriteRoundedRectPremultiplied`——三带 cov=1 + 四角 mask 索引变换的**预乘直写**，跳过 GDI 绘制；R=0 退化 = 全矩形预乘填充，DrawRect a<1 复用之）· K③（`FixupSentinelFrosted`——DrawText[经 `DrawTextImpl` 拆分，extent ±2px]/DrawLine[margin = 线宽+2]/DrawFocusRect[±2] 的哨兵扫描，按 B,G,R 字节序比对清屏色）· K④（DrawImage 尾部哨兵——首版合成结果按不透明呈现，#56 已知限制）· 全部挂点以 `m_backdropActual == None` 直返（A6①）· 裁剪交含 = C32-3（`GetClipBox` ∩ 命令矩形；圆角角的循环范围按裁剪钳制，canonical 索引不破坏） |
| **测试** | **ClangCL / Clang 381/381 全绿**（378 + T32-2/2b/3；MinGW 待 #57）· T32-2（Acrylic 清屏全缓冲 (tint,A=0) + DrawRect 红矩形内 RGB/A=255/255 矩形外 (tint,0)）· T32-2b（**覆盖率 sum 不变式**：Σ A/255 = 18857.04 vs 理论 18856.64[误差 0.4 = 掩码 S=8 采样误差；角区各 314.3 = 掩码自身 314.26] + 带内原色 A=255 + 角外 (tint,0) + a=0.5 预乘带内 A≈128）· T32-3（文字带内哨兵不变式：A∈{0,255} 且 A=255 ⇔ RGB≠tint、bad=0、字形/字缝计数均 > 0） |
| **偏差** | ★ **E-B2-3（toByte 输入域）**：K② 的 A 通道首版喂 `toByte(255×cov×a)`——**toByte 输入域是 [0,1]**，0..255 的输入被 clamp 成 1 ⇒ 非零像素全 255（角区 sum = nonzero 计数 329 而非 314.26——区域诊断 + 掩码直测定位）；修 = A 通道喂 `alphaFactor×cov`（[0,1] 域）。★ 实现注记：`WriteRoundedRectPremultiplied` 在衬底模式**跳过 GDI 绘制直接写 DIB**（缓冲处于清屏态，弧外不触碰即可） |
| **限制** | 同批二（真机 Acrylic 成功路径留批四 E2E；文字 AA 边缘视觉 = 批四端到端矩阵） |
| **台账** | 本行 + audit v1.81 + deferred v2.15 + docs/README + 根 README（同批提交） |

### §10.2 批二（2026-10-11，材质应用与状态机）

| 项 | 内容 |
|---|---|
| **落地** | △4（`Win32PlatformWindow`：`SetWindowCompositionAttribute` **动态解析**[lazy + 失败也缓存——全仓首个 GetProcAddress，沿 R6 失败容忍]· `ApplyBackdrop`[Acrylic 尽力 ⇒ 解析失败/调用失败/系统不支持一律 Degraded 绝不裸透；None ⇒ `state=0` 可编程移除（SW1 实证）]· `Show()` 尾部首应用 + `WM_DPICHANGED`（既有 case 内追加）/`WM_DWMCOMPOSITIONCHANGED`（新增 case）重应用 · C32-12 `nColor = alpha<<24`、RGB=黑）· △7（`Win32PlatformWindowInternal.h` 注入头：`BackdropInjection {None, FailResolve, FailCall}`）· `Window::SyncBackdropSurface`（PaintFrame 帧首把平台 actual 翻译为后端清屏语义[Acrylic = tint-A0 / Degraded = 白 204]，**变化才下发**——轮询式无新回调接缝） |
| **测试** | **ClangCL / Clang 378/378 全绿**（375 + T32-5/6/8；MinGW 待 #57）· T32-5（FailResolve ⇒ 解析失败不调用未解析函数直落 Degraded / FailCall ⇒ 调用真实执行结果强制 FALSE ⇒ 同落 Degraded；两者「renderer 确已收到 Degraded 提交语义」经**真实 GDIBackend 观测缝** `DebugCommitUsesAlphaBlend` 断言——评审 P2-4 分支覆盖）· T32-6（Degraded 请求完全忽略 / Show 前 applied=false / Show 后状态确定性 / Show 后设置请求态不变）· T32-8（签名 static_assert 钉死 + `!is_abstract_v<GDIBackend>`） |
| **偏差** | ★ **E-B2-1（注入 × 解析缓存的交互）**：`g_setWcaResolved` 是**进程级缓存**且首个窗口 `Show()` 即完成真实解析（批二把 ApplyBackdrop 挂进 Show ⇒ 更早注册的测试窗口就触发了缓存）⇒ 注入在其后设置必须**显式作用于缓存**（FailResolve ⇒ `g_setWca=null` + 视为已解析；FailCall/None ⇒ 重置解析标记），否则 FailResolve 永不生效（首版实测 actual=Acrylic——诊断计数器定位）。★ **E-B2-2（首帧时序）**：Show 内部 `UpdateWindow` 的首帧发生在 ApplyBackdrop **之前** ⇒ 测试需 `Invalidate()` 驱动第二帧才能让实际态到达后端（生产无此问题——任何后续失效都晚于应用）。★ **实现注记**：`SetBackdropInjectionForTests` 的定义不能包在嵌套 `namespace ECDI{}` 内（变 ECDI::ECDI:: ⇒ 链接 undef，首版实测） |
| **限制** | 真机 Acrylic 成功路径（actual=Acrylic + 真实 DWM 材质）未在本批验证——批四库外 E2E + 目视承接（设计如此：单测以注入保持确定性） |
| **台账** | 本行 + audit v1.80 + deferred v2.14 + docs/README + 根 README（同批提交） |

### §10.1 批一（2026-10-11，缓冲地基）

| 项 | 内容 |
|---|---|
| **落地** | △1（新公共头 `Window/WindowBackdrop.h`，95→96）· △2（`Window::SetWindowBackdrop`[Degraded 完全忽略 + `m_shown` 配置期闸门——**实现注记**：闸门在 Window 侧而非平台侧，因请求态/tint 是决策层状态，平台侧闸门会让 Window 请求态先被改写而平台已忽略 ⇒ 两层失同步]· `GetWindowBackdropState`）· △3（`PlatformWindow` +2 默认虚函数——`TestPlatformWindow`×2 **零改动**实证 S7）· △5（`RenderingBackend::SetBackdropSurface(BackdropMode, uint32)` 默认实现 + `Renderer` 单行转发）· △6（**DDB → 32bpp top-down DIB**[事务性重建保留]· 清屏三分支[None = 既有 FillRect 路径**逐字保留**]· 提交分支[Degraded→AlphaBlend / 其余→BitBlt]· `DebugBackBufferBits`/`DebugCommitUsesAlphaBlend` 观测缝）· RunAllTests 接线 |
| **测试** | ★ **勘误 E-B1-2（2026-10-11 同日订正）**：**ClangCL / Clang 375/375 全绿**（375 = 运行时注册数——grep 口径 369 + 循环注册 6）；**MinGW 链被 #57（既有 M1 堆损坏，非本批引入——pre-batch-1 worktree 复现）阻塞**，待 #57 修复后补跑。T32-1（None 清屏 RGB 精确 + alpha 恒 0 + DrawRect 内外精确）· T32-4（Degraded 全缓冲四字节 204 + AlphaBlend 分支 + None 恢复 BitBlt）· T32-7（Acrylic→None→Acrylic 三段全缓冲无残留） |
| **偏差** | ① T32-4 的「DrawRect 后矩形 A=255」断言属 K①（批三）——批一先落清屏+提交分支两半；② △7（注入内部头）随批二（与 △4 同文件族，批一无需空壳）；③ ★ **勘误 E-B1-1（用户 GDB 定位）**：首版三测试把 `DebugBackBufferBits()` 捕获放在 `BeginFrame` **之前**——DIB 尚未分配（懒创建）⇒ 解引用空指针（SIGSEGV@`base[i*4+2]`，即 BitsView::R）；修 = 位指针移到首帧分配后捕获；④ ★ **工具勘误**：批一验证初版用 `cmd & echo %ERRORLEVEL%` 读退出码——**%ERRORLEVEL% 在 cmd 解析期展开**（回显旧值）⇒ 三链「EXIT=0」假绿；改用 PowerShell `$LASTEXITCODE` 后真相浮出 |
| **限制** | 无新增（K①..K④ 修补未在本批） |
| **台账** | 本行 + audit v1.78 + deferred v2.12 + docs/README + 根 README（同批提交） |
