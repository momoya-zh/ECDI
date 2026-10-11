# Phase 32 · 窗口雾面衬底（Frosted Backdrop）—— 详细设计（v1.0）

> 来源：初设 `phase32-frosted-backdrop-preliminary-design.md` **v1.1（进详设）** §8 移交十项 + **探针 8**（`.workbuddy/tmp/dn_acrylic/probe8/`，零仓库侵入）。
> 状态：**v1.0 待评审**（本文档钉死修补语义、逐文件 △、T32 精确断言与批次；实施回填留 §10）
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
| **K③** | `DrawText` / `DrawLine` / `DrawFocusRect` | 命令矩形 ∩ `GetClipBox` 内**哨兵扫描**（A=0 且 RGB≠清屏 ⇒ A=255） | probe7 Z1/T2 + probe8 L1；碰撞 = 已知限制（TC1：0/38400） |
| **K④** | `DrawImage` | **保留源 alpha**——禁矩形级（初设 v1.1 已钉）。执行端现状 = Phase 8 链路（32bpp 顶降 DIB + AlphaBlend，`GDIBackend.cpp:633`）：AlphaBlend 对缓冲的 dst-alpha 写入不可靠 ⇒ 批三实测后二选一：**(a)** 图像解码位若预乘 ⇒ 手工预乘 over 写入（沿 K② 家族）；**(b)** 退化为「合成结果 A=255 + 已知限制『半透明图像按不透明合成』」。**第一版按 (b) 兜底 + (a) 为目标**，批三以实测裁决（无真实半透明图像消费者前不追加机制——条 22） | 初设 v1.1 P1-1 + 本轮勘察 |
| — | `PushClip`/`PopClip` | 无像素，不修补 | — |

★ **修补与裁剪的交含（C32-3 落地）**：修补矩形 = 命令矩形 ∩ `GetClipBox(memoryDC)`（D28-C 已证 GDIBackend 裁剪 = SaveDC 栈，`GetClipBox` 读有效裁剪）——不引入第二裁剪真相。

### D32-L 中间 alpha 的 BitBlt 保真（新契约）

**C32-9**：32bpp DIB 经 `BitBlt(SRCCOPY)` 提交后，重定向表面的 alpha = 缓冲 alpha（逐字节），DWM 按**预乘 over** 合成（AL1：A=128 实测误差 ≈0.1）。该事实是 K② 的可行性前提，也是 A1 机制验收的判读模型。★ 探针 8 之前 K13/P1 只证过 0/255 两端——本条把契约补全到 [0,255] 全域。

### D32-M 未应用查询语义（初设 §8⑩ 关闭；评审 P2）

- `WindowBackdropState { BackdropMode requested; BackdropMode actual; bool applied; }`——**轻量 applied 标记**（不新增公共枚举值）。
- `Show()` 前：`applied = false`、`actual = None`（**未应用 ≠ 应用失败**——调用方先查 `applied`）；`Show()` 后 `applied = true` 且 actual = 最近一次应用结果。
- `SetWindowBackdrop(Degraded, …)` ⇒ **完全忽略且不改变既有请求态**（Warning 一次）。

### D32-N 失败注入缝（初设 §8③ 关闭）

两级注入，**零公共 API 污染**：
1. **Window 级状态机**：`TestPlatformWindow`（既有测试替身）override 新虚函数模拟 `actual = Degraded`——测 `Window` 的查询/转发逻辑，无需触碰 Win32。
2. **Win32 解析/调用级**：`Win32PlatformWindow.cpp` 文件内静态 `s_backdropForceFail`（**internal linkage**）+ 文件内自由函数 `SetBackdropForceFailForTests(bool)` 声明于同目录内部头 `Win32PlatformWindowInternal.h`（**新增内部头，不入公共头计数**）——测试 include 之（`WindowBackgroundTests.cpp:11` 先例：测试可含库内内部头）。T32-5 用它。grep 锚 = `backdropForceFail`。
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
- **端到端（抓屏，批四探针）**：① 字形核心精确数 > 200（probe7/8 实测 682）；② 字带内**哨兵色残留 = 0**（字缝透材质）；③ **碰撞率基线**：TC1 型双底色差分 collisions = 0（本配置实测）——A2 判据按「三同时」落判。
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
| **△5** | `include/ECDI/Render/RenderingBackend.h` + `src/Render/Renderer.h` | `RenderingBackend` **+1 带默认实现 virtual**：`SetBackdropSurface(const BackdropSurfaceState&) {}`；`BackdropSurfaceState { bool active; std::uint32_t clearABGR; bool commitViaAlphaBlend; }`（**内部结构**，定义在 `src/Render/` 内部头 `BackdropSurface.h`——不入公共头）· `Renderer` +1 单行转发（**不持有状态**——Phase 18 红线 7 不破：状态存于 backend）· `GLRenderer`/`RecordingBackend` **零改动** |
| **△6** | `src/Render/GDIBackend.{h,cpp}` ★ | `EnsureBackBuffer` → **CreateDIBSection 32bpp top-down**（尺寸自检重建逐字保留；+`m_dibBits`）· `BeginFrame` 清屏三分支（None = 既有 FillRect 逐位不变；Acrylic = DWORD 填 tint-A0；Degraded = memset 204）· `EndFrame` 提交分支（BitBlt / AlphaBlend）· **每命令修补**（K①..K④；`GetClipBox` 交含）· `DrawRoundedRect` 雾面分支 = mask 后处理预乘改写（K②）· **内部观测缝** `DebugBackBufferBits() const`（内部头可达——`WindowBackgroundTests.cpp:11` 先例；零公共 API）· 失败容忍姿态沿决策 30 |
| **△7** | `src/Platform/Win32/Win32PlatformWindowInternal.h` **新内部头** | `SetBackdropForceFailForTests(bool)` 声明（D32-N；不入公共头计数） |
| **△8** | `src/Tests/WindowBackdropTests.cpp` **新测试文件** | T32-1..8（§5）；TEST_SOURCES 走 GLOB（CMakeLists:153）⇒ **CMake 0** |
| **△9** | `CMakeLists.txt` | **0 改动**（FRAMEWORK_SOURCES/PUBLIC_HEADERS/TEST_SOURCES 均 GLOB_RECURSE——核验过 :80/:136/:153） |

## §5 T32-1..8 精确断言（用例 366 → **374**）

| # | 用例 | 装置 | 断言（精确） |
|---|---|---|---|
| T32-1 | 默认零回归 | 直构 `GDIBackend`（隐藏窗口） | `BeginFrame(Color::White)` 后 bits：RGB=(255,255,255) 逐位、**A=0**；`DrawRect` 后矩形 RGB=绘制色——与 Phase 31 基线逐位同（A6①） |
| T32-2 | Acrylic 缓冲语义 | 同上 + `SetBackdropSurface({active, tint, false})` | 清屏后全缓冲 = (tintRGB, A=0)；`DrawRect` 后矩形内 A=255 且 RGB=绘制色；矩形外 (tint, 0) |
| T32-2b | 圆角预乘改写 | 同上 + `DrawRoundedRect`（有 mask 路径） | mask 域逐像素：A = 255×cov ±1 且 RGB = color×cov ±1；cov=0 处 = (tint, 0)（D32-P） |
| T32-3 | 文字哨兵不变式 | 同上 + NONAA `DrawText` | 文字矩形内全体像素：A∈{0,255} 且 A=255 ⇔ RGB≠tint；A=255 计数 > 0 且 A=0 计数 > 0（字缝） |
| T32-4 | Degraded 缓冲语义 | `SetBackdropSurface({true, 0xCCCCCC, true})` | 清屏后全缓冲 = 204；`DrawRect` 后矩形 A=255；commit 标志 = AlphaBlend 分支（内部 accessor） |
| T32-5 | 失败注入状态机 | **真窗口** + `SetBackdropForceFailForTests(true)` → `Show()` | `GetWindowBackdropState()` = `{Acrylic, Degraded, true}`；日志含降级 Warning |
| T32-6 | 配置期契约 | 真窗口 | Show 后 `SetWindowBackdrop` ⇒ Warning + 状态不变；`SetWindowBackdrop(Degraded,…)` ⇒ 状态不变（完全忽略） |
| T32-7 | 模式切换无残留 | 直构 backend | Acrylic 清屏 → bits=(tint,0)；切 None 清屏 → bits=(底色, A=0) **全缓冲无旧值**；再切回 Acrylic 同理（SW1 的缓冲级形态） |
| T32-8 | 结构性判据（A9②） | grep 脚本 | `WindowBackdrop.h` / `Window.h` 新声明无 Win32 类型；新虚函数全部带默认实现体（`{` 非纯虚）——沿 Phase 18 A4 形态 |

**批四库外探针（不入注册表）**：真实 `GDIBackend` + 真实 `RenderCommand` + 真实控件（**D6 阶段 B**）——复刻 probe7/8 判据（双区差分 / 控件误差 / 文字三同时 / K② mask 复核）打在框架真管线上；A8 可自动化部分（DPI 切换触发重应用）+ 目视矩阵（§7）。

## §6 批次（四批）

- **批一 = 缓冲地基**：△1△2△3△5△7 + △6 的 DIB 切换/清屏三分支/提交分支 + T32-1/4/7（纯缓冲级，无 DWM 依赖）——**零 DWM 风险先落地**。
- **批二 = 材质应用与状态机**：△4 全部 + T32-5/6/8。
- **批三 = 修补**：△6 的 K①..K④ + T32-2/2b/3（含 DrawImage 实测裁决 K④）。
- **批四 = 阶段 B 收口**：库外 E2E 探针 + A8 + 五处台账收口回写（§10）。

## §7 契约增补与 A8 目视矩阵

**新增契约**：C32-9（D32-L BitBlt 保真）· C32-10（K② 预乘改写公式）· C32-11（修补矩形 = 命令矩形 ∩ GetClipBox——**哨兵按 B,G,R 字节序比对**，probe8 勘误红线）· C32-12（nColor RGB=黑 + tint RGB 由清屏承载——C32-5 的修订版）。
**A8 目视矩阵**（人工）：跨屏拖动 / 跨 DPI（100%↔150%）/ 最大化-还原 / 远程会话——每项查「材质连续、圆角不错位、控件清晰、无残留」。

## §8 风险与移交实施

风险 **中低**：机理三层全部实测闭合；剩余 = K④ 图像路径的 AlphaBlend dst-alpha 不可靠性（批三实测裁决，(b) 兜底）与 DWM 跨环境行为（A8 人工）。移交实施 = §4 逐文件 + §5 断言 + §6 批次；实施回填落 §10。

## §9 修订记录

- **v1.0**（2026-10-11）详设初稿。**输入**：初设 v1.1 §8 十项 + **探针 8**（TC1 碰撞 0/38400 · RC1/RC2 角外/内部/背景三项全 0.00 · **AL1 中间 alpha BitBlt 精确保真**（预测误差 ≈0.1）· AL2 dst 垃圾参与再证 Acrylic 必 BitBlt · L1/DG1 线与降级 AA 成立 · SW1 state-0 移除零残留）。**§3** D32-K（三层修补：rect-A255 / **mask 预乘改写** / 哨兵；DrawImage 保留源 alpha 批三裁决）· D32-L（C32-9 BitBlt 保真全域 [0,255]）· D32-M（applied 标记）· D32-N（两级注入缝，internal linkage + 内部头）· D32-O（触发全集 + state-0 移除）· D32-P（判据量化）· D32-Q（tint 契约措辞）。**§4** △1..△9（公共头 95→96 · API +2 方法 +1 枚举 +1 值类型 +1 bool 字段 · 实现者 7 零改动 · CMake 0 · 用例 366→374）· **§5** T32 精确断言 · **§6** 四批。★ 方法论勘误入档：哨兵通道映射（probe8 揭出）+ 角外采样区修正。★ 未改框架代码。
