# ECDI 设计文档索引

> 本文档是 **`docs/dev/` 目录的入口**——文件名用 `README.md` 表达的就是**这个角色**（「开发文档集的入口」，不是「项目说明」那一层；项目说明在仓库根的 `README.md`，使用指南在 [`usage.md`](../usage.md)）。它承担四件事：**① 进度看板**（下方「开发进度」的四区）· **② 文档索引**（逐 Phase 段 + 各阶段文档表）· **③ 技术债务速览**（完整排期见 `roadmap-deferred.md`）· **④ 阅读导航**。设计文档随代码提交 git，从 Phase4 起为强制约定（职责确认 / 初步设计 / 详细设计 各阶段文档正常写入本目录）。
>
> ★ **只想「把框架用起来」的读者**请直接去 [**`usage.md`（使用指南）**](../usage.md)——各控件怎么用、布局逻辑、从零接一个窗口的完整路径都在那里；本文只回答「为什么这样设计」。

## 项目定位（ECDI = Everyone Can Do It）

**教学型框架**——目标是人人都能用、**学习成本尽可能低**（`ECDI/ECDI开发规范.md:1` 的命名本意）。这不是口号，而是**评审标尺**：任何设计分歧（选 A 还是选 B、要不要加 API、怎么命名）先回到这里判，再谈实现代价。

| 标尺 | 具体含义 |
|---|---|
| **命名即语义** | 公共 API 的名字直接说清用途；**一个概念只用一个词**——出现同义混用（如 `padding` / `inset` / `spacing` 交叉）即缺陷 |
| **不引入投机抽象** | 抽象在**第二个真实消费者**出现时才加；「暂时不需要」一律写进 [roadmap-deferred.md](roadmap-deferred.md)，而不是先写进代码 |
| **过程即教材** | 每个阶段交付 需求 → 初步设计 → 详细设计 三份文档（本目录）——**理由**与结论同等重要，评审时"为什么"比"是什么"更该写清 |
| **示例优先于描述** | 能跑的示例胜过一段文字：`examples/MinimalApp` 约 20 行跑起来；`examples/ModelProbe` 是真实工具 |
| **不强迫既有使用者改代码** | 新能力的默认值一律「与现状**逐位等价**」（近年各 Phase 的硬契约）⇒ 加能力 ≠ 改已有调用点 |
| **非必要不引入第三方** | 引入第三方 ⇔ **三条同时成立**：① 能力**不属于本项目知识域**（GUI 框架本身）② 自研的**验收标准无法独立建立** ③ 愿意承担**长期跟版（含安全）义务**。★ 「渲染四层管线 / 平台解耦」两条都不满足 ⇒ **必须自研**。★ 当前唯一第三方 = **FreeType**（字形栅格化 + hinting，见 [roadmap-deferred.md](roadmap-deferred.md) #44） |

---


## 开发进度（2026-11-15 更新）

> **当前规模锚点**：测试 **638** 用例 · 公共头文件 **102** · `docs/` **229** 篇文档（分批里程碑与演进史见 [roadmap-deferred.md §8](roadmap-deferred.md)）。

### ✅ 已完成

| 阶段 | 内容 | 状态 |
|---|---|---|
| Phase 1–2 | 项目初始化 + 基础类型 + 平台窗口抽象 | ✅ 完成 |
| Phase 3 | Widget 系统（树结构、事件冒泡、布局基类、绘制、焦点） | ✅ 完成（2026-08-11） |
| Phase 4 | 渲染抽象与 GDIBackend（GDI 双缓冲 + RenderCommand 命令模式） | ✅ 完成（2026-08-11） |
| Phase 5 | Text / Label / Button / Interaction / TextBox / IME + 架构审查 | ✅ 全部完成（2026-08-15） |
| Phase 6 | HorizontalLayout + CheckBox + RadioButton | ✅ 6.1/6.2 完成（2026-08-25） |
| Phase 7 | 平台抽象 + 测试体系（7.1/7.2/7.5） | ✅ 全部完成（2026-08-24） |
| Phase 8 | 渲染增强（R1 字体缓存 + R2 圆角矩形 + R3 多行文本） | ✅ 完成（2026-08-24） |
| Phase 8.5 | 文本系统 2.0（8.5.1 中文 IME + 8.5.2 选择与剪贴板 + 8.5.3 Undo/Redo） | ✅ 全部完成（2026-08-25） |
| Phase 8.6 | 渲染抗锯齿（CornerCoverageMask / PatchSurface / BlendPatch，$S=8$） | ✅ 实现通过（2026-09-11） |
| Phase 9 | 主题系统（Theme / DefaultTheme / ThemeStyle，Color/Font/Style） | ✅ 完成（2026-08-25） |
| Phase 9.5 | 收尾补充（Clip / Alpha / Key / Hover，4/4 收口） | ✅ 完成（2026-08-28） |
| Phase 9.6 | 动画系统（AnimationTimer / Animation<T> / ProgressBar / CollapsiblePanel） | ✅ 完成（2026-08-30） |
| Phase 9.7 | 自适应布局（Stretch 语义 / Panel 自动适配） | ✅ 完成（2026-09-02） |
| Phase 9.8 | AutoSize（GetPreferredSize / AutoSize / MeasureText） | ✅ 完成（2026-09-02） |
| Phase 10 | 库化（v0.1.0 头文件整合 + 构建安装导出 + ModelProbe 消费） | ✅ 完成（2026-09-06） |
| Phase 11 | 图片解码（Decode 模块 + WicImageDecoder，32bpp 预乘 BGRA 契约） | ✅ 完成（2026-09-07） |
| Phase 12 | WindowChrome 无边框与 DWM 集成（Borderless + NCHITTEST + DWM） | ✅ 收口（2026-09-14） |
| Phase 13 | CaptionBar 自绘标题栏与窗口控制按钮（NCHITTEST 委托树命中 + 状态同步） | ✅ 收口（2026-09-15） |
| Phase 14 | 托盘与拖入接缝（TrayIcon + FileDropEvent，`PlatformApplication` seam） | ✅ 收口（2026-09-17） |
| Phase 15 | 滚动容器（`ScrollView` + `ScrollBar` 两阶段裁剪与单源偏移） | ✅ 收口（2026-09-18） |
| Phase 16 | 桌面驻留层（`WindowLayer::Desktop` 去 `WS_MINIMIZEBOX` + 前景 hook 置底） | ✅ 收口 2026-09-21（218 用例，API +0） |
| Phase 17 | 布局内边距（`VerticalLayout` / `HorizontalLayout` 可选 `padding` 硬缩进） | ✅ 收口（226 用例） |
| Phase 18 | 窗口 / 根背景（`Window::SetBackgroundColor` → `BeginFrame(Color)`，API +2） | ✅ 收口（231 用例） |
| Phase 18.1 | 子节点内缩（透明 `Panel` 组合已满足，独立 `SetInset` 冗余） | ⏸️ 搁置 2026-09-22（见 `roadmap-deferred.md` #40） |
| Phase 19 | 鼠标事件维度补全（`IsButtonDown` 位掩码 + `KeyModifier` 状态，#41 / D-1） | ✅ 收口 2026-09-23（236 用例，API +2） |
| Phase 20 / 20.1 | Per-Monitor V2 DPI 感知与 `Renderer` 层缩放（对外 DIP、后端物理像素，#8 / D-2） | ✅ 收口 2026-09-24（252 用例） |
| Phase 21 | 系统图标 → `Image`（运行时 `HICON` / 路径 → 32bpp 预乘 BGRA，G-2 / D-3） | ✅ 收口 2026-09-27（261 用例，API +1） |
| Phase 22 | `Create` 的 DIP 尺寸契约（`ApplyStartupSize` seam 修 `Create`→`Show` 尺寸偏移，D-7） | ✅ 收口 2026-09-29（264 用例） |
| Phase 23 | 工作线程 → UI 投递（`Application::PostToUi` + `PostThreadMessageW` swap 排水，D-4） | ✅ 收口 2026-09-29（275 用例） |
| Phase 24 | `DrawLine` 线段抗锯齿（`Render/LineCoverage` $S=8$ 圆帽掩码共享 `RasterizeMask`，#43 / D-6） | ✅ 收口 2026-10-01（297 用例，API +0） |
| Phase 25 | 列表 / 网格容器（`ListLayout` / `GridLayout` + 不可见子负区停泊 `C-VIS`，#42 / D-5） | ✅ 收口 2026-10-02（307 用例，头 92→94，API +2） |
| Phase 26 | FreeType 文本栈 + GL 渲染后端（FreeType 2.14.3 零 patch · `FontEngine` / `GLRenderer` · D-8 窗口 DPI 基准统一） | ✅ 收口 2026-10-04（319 用例，API +3） |
| Phase 27 | 绘制命令构建层视口剔除（`PaintContext` 镜像裁剪栈 `m_cullStack` + `self` 单表达式，#49） | ✅ 收口 2026-10-04（330 用例，命令 −98.9%，API +2） |
| Phase 28 | 长文本测量与绘制优化（FT per-glyph advance memo + `TextWidget` 修订指纹 + GDI clip 前缀截断，#50） | ✅ 收口 2026-10-06（335 用例，API +0） |
| Phase 29 | 文本多行能力（`Label` / `TextWidget` opt-in `SetWordWrap` 共享 `TextLayout` + `FitText`，#52） | ✅ 收口 2026-10-07（349 用例，API +4） |
| Phase 30 | 文本与交互快修（`caretColor` + `Core/TextAlignment.h` / `Button` 对齐 + H/V 布局 `C-VIS`，#51） | ✅ 收口 2026-10-07（356 用例，头 94→95） |
| Phase 31 | 窗口几何与行高口径（`Window::SetBounds` / `GetBounds` 请求语义 + `LineHeight` 统一，`REQ-05`） | ✅ 收口 2026-10-08（366 用例；O10 已知差异已裁决记账） |
| Phase 32 | 窗口雾面衬底（`WindowBackdrop.h`，Acrylic 合成 + 32bpp DIB + 三层 alpha 修补 + 0.8 半透明白降级，#55/#57/#58） | ✅ 收口 2026-10-14（445 用例，头 95→96） |
| Phase 33 | 圆弧原语 `DrawArc` + 环形进度控件 `RingProgressBar`（覆盖度掩码双后端消费 + 跨仓 `PaintProbe` 验证，`REQ-08`） | ✅ 收口 2026-10-16（465 用例，头 96→98） |
| Phase 34 | 后端几何取整口径统一（`QuantizeEdge` floor + `ToPixelRect` / `ToPixelBox` 对齐 GL，#46） | ✅ 收口 2026-10-19（474/487 用例，头 96→96） |
| Phase 35 | 可嵌套点击控件事件消费与安全销毁（`Event::SetHandled()` 止冒泡 + `Widget::RequestRemove()` 延迟析构） | ✅ 收口 2026-10-19（487 用例，API +1） |
| Phase 36 | `Font` 表达力（`weight` + TTC `faceIndex` + `ResolveFileCandidates` 复合族名，#70 / `REQ-10`） | ✅ 收口 2026-10-24（497 用例，头 98，API +1 字段） |
| Phase 37 | 单行 `TextBox` 垂直对齐（`Core/VerticalAlignment.h` + `TextBoxStyle::verticalAlign`，#76 / `REQ-14`） | ✅ 收口 2026-10-26（513 用例，头 99→100） |
| Phase 38 | `TextBox` 编辑器软折行（`LineRange` 归属与 `LineDrawRange` 裁剪分立 + 10 维失效键，#77 / `REQ-15`） | ✅ 收口 2026-10-29（531 用例，public +0） |
| Phase 39 | UTF-8 码点索引 $O(N)$ 单趟重写（`DecodeCodepointAt` + `RecalculateLines` 非折行支，$O(N^2)\to O(N)$，#78） | ✅ 收口 2026-10-29（538 用例，API +1） |
| Phase 40 | 折行支逐行整串预备消除（`TextWidget` 懒建码点↔字节偏移表 + 5 参 `FitText` 重载，#83） | ✅ 收口 2026-10-29（546 用例，API +1） |
| Phase 41 | 帧级布局 / 重绘失效结算管线（双通道脏标记 + `SettleLayoutIfDirty` 帧首结算，#79） | ✅ 收口 2026-10-30（556 用例，API +1 `NeedsLayout`） |
| Phase 42 | Window 级浮层根 Overlay Root 与 Light Dismiss（`m_overlayRoot` 三层 Panel + ESC/焦点闭环，#81） | ✅ 收口 2026-10-30（572 用例，头 100→101，API +4） |
| Phase 43 | 滚动条双轴厚度（`VerticalBarThickness` / `HorizontalBarThickness` 分立，闭环 M5/M6，#84） | ✅ 收口 2026-11-01（584 用例，API +0） |
| Phase 44 | 段内内联样式（styled spans，`Widget/TextSpan.h` 原子 `SetStyledText` + 只读 TextBox，#88 / `REQ-20`） | ✅ 收口 2026-11-08（599 用例，头 101→102） |
| Phase 45 | styled spans 表达力扩展（`Font.italic` + `SpanStyle.italic/size` + `TextMeasurer::Ascent` 最大行框基线，#89） | ✅ 收口 2026-11-09（608 用例，头 102） |
| 专项小修（#91–#93） | GDI 混合字体基线对齐（`GetTextMetricsW` 基线偏移）与斜体/下伸部溢出防护（`clip` 兜底扩展） | ✅ 完成 2026-11-15（[专项设计稿](gdi-mixed-font-baseline-and-overhang-design.md)） |
| Phase 46 | GL/FreeType 字体能力对齐（合成粗体 `w>=551` / `B->O` 合成斜体 / 惰性 `FT_Get_Sfnt_Name` 中文族名解析 + `GLGlyphAtlas` 饱和观测，#94，收编 `#69` / `L45-1` / `C36-4`） | ✅ 收口 2026-11-15（Batch 0–2 落地；622 用例，头 102，MinGW/Clang 验证） |
| Phase 47 | GL 后端渲染缓存与自动化像素验证（`GLGlyphAtlas` 扩容淘汰 + `DrawImage` 128-bit 哈希纹理缓存 + WGL 离屏像素夹具，#95，关闭 `#75` ③） | ✅ 收口 2026-11-16（Batch 0–2 落地 6 文件；631 用例，头 102，MinGW/Clang 验证） |

### 🔄 当前

- **Phase 48**（`SpanStyle::background` 与混排选区按 run 对齐，#90）：Batch 1–2 已实施，AI 侧自检通过，待用户侧四链 Debug 终验与跨仓 `--md-probe` 回归（详见 [phase48-span-background-requirements.md](phase48-span-background-requirements.md) / [phase48-span-background-preliminary-design.md](phase48-span-background-preliminary-design.md) / [phase48-span-background-detailed-design.md](phase48-span-background-detailed-design.md)）。

### 🔲 未来

- **后续方向**：基础控件补齐 / 渲染能力增强 / 跨平台（Linux/Android 远期）→ 接近 1.0

### 📋 技术债务（记账）

> 完整延期排期与重启条件见 **[roadmap-deferred.md](roadmap-deferred.md)**；已收口项不再保留于本表。
> ⚠️ **「解决时机」说明**：原定「Phase 10 转库前」未纳入该次收口的历史接口项，统一顺延至 **v1.0 API 审查**；仅记录真实现存且未关闭的债务边界。

| 债务 | 位置 | 解决时机 |
|---|---|---|
| Invalidate 解耦（两层结构 Internal+API） | `TextBox` 编辑操作 | 待重审（是否仍需解耦） |
| **输入层抽象（TextInputInterface/TextInputContext）**——半抽象（`Window` 中介） | `Window::UpdateTextInputCaret` | **v1.0 API 审查** |
| 键盘入口不对称（`OnKeyDown` 走 `Window` / `OnKeyUp+CharInput` 直派，3 入口） | `Application` | 未来全局快捷键需求时统一（见 `phase5-architecture-review.md`） |
| 多窗口焦点语义（应用级 vs 窗口级焦点） | `Window` / `Application` | **v1.0 平台抽象审查**（见 `phase5-architecture-review.md`） |
| 编辑操作可见性（临时 public） | `TextBox` | **v1.0 API 审查** |
| **`RenderingBackend::DrawText` 命名与 Win32 宏冲突**（现以防御性 `#undef` 兜底） | `RenderingBackend` / GDI / Recording | **v1.0 API 审查改名** |
| **IME 结果吞字符 pending 计数超时兜底**（#86 已打通 `WM_IME_CHAR` 与无组合直插并绑定消费后武装；仅剩超时/异常序列边界未设限） | `Win32PlatformWindow` | 真实输入法异常兼容性需求出现时 |
| **Phase 8.6 抗锯齿遗留人工验收**——A7 性能基线 / A8 三链对比 / A9 `S=8` vs `S=16` / A4 关闭目视 | `GDIBackend` AA 路径 | 用户侧人工抽检（功能不阻塞） |
| **`m_running` / `Run`-`Exit` 语义未定义**——初值 `true` 使 `Exit()` 在 `Run()` 前调用也生效 | `Application.h:122` | 独立记账（见 `window-ownership.md` §7） |

## Phase3 Widget System

| 文档 | 内容 | 状态 |
|------|------|------|
| [phase3-architecture.md](phase3-architecture.md) | Phase3 总体架构：所有权、事件流、RootWidget 定位、唯一入口原则、子模块总览 | ✅ 完成态（2026-08-08 更新） |
| [phase3-layout-design.md](phase3-layout-design.md) | Layout 子模块详细设计：Layout 策略基类、VerticalLayout、Arrange 递归、unique_ptr 完整类型坑（v1.1） | ✅ 已实现并测试（2026-08-06） |
| [phase3-focus-design.md](phase3-focus-design.md) | Focus 子模块详细设计：CanFocus、MouseDown 获取、SetFocusedWidget 验证、点击空白保持、键盘不 Bubbling | ✅ 已实现（2026-08-07） |
| [phase3-paint-design.md](phase3-paint-design.md) | Paint 子模块详细设计：Paint/OnPaint 同构、HDC 前向声明、offset 累加、WM_PAINT 入口、颜色硬编码 | ✅ 已实现（2026-08-07） |

## Phase4 Renderer System（✅ 已实现，2026-08-11）

> 完成：`Widget → PaintContext → RenderCommand → Renderer → RenderingBackend → GDI`，替换 Phase3 的 GDI 临时桥梁（Widget 层 HDC 直连已移除）；GDIBackend 双缓冲，Window::PaintFrame 编排。

| 文档 | 内容 | 状态 |
|------|------|------|
| [phase4-renderer-design.md](phase4-renderer-design.md) | Phase4 详细设计：42 条决策记录（variant 命令 / PaintContext 门面 / Renderer 持引用 / GDIBackend 双缓冲全套 / Header 依赖规则 / 实现顺序） | ✅ 完成态（v1.7，2026-08-10） |
| [phase4-renderer-implementation.md](phase4-renderer-implementation.md) | Phase4 实现蓝图：文件树 / 类定义 / Commit 4.1-4.7 修改范围 / 验收标准 / 双层测试 | ✅ 已实现（v1.1，2026-08-11，4.1-4.7 全部落地并验证通过） |

## Phase5 文本 + 控件（✅ 已完成，2026-08-15）

| 文档 | 内容 | 状态 |
|------|------|------|
| [phase5.1-text-requirements.md](phase5.1-text-requirements.md) / [preliminary](phase5.1-text-preliminary-design.md) / [detailed](phase5.1-text-detailed-design.md) | 5.1 文本系统三件套（Font 纯描述 / TextMeasurer 独立接口 / DrawText 命令管线） | ✅ 已实现（2026-08-12） |
| [phase5.2-label-requirements.md](phase5.2-label-requirements.md) / [preliminary](phase5.2-label-preliminary-design.md) / [detailed](phase5.2-label-detailed-design.md) | 5.2 Label 三件套（第一个文本消费者） | ✅ 已实现（2026-08-13） |
| [phase5.3-button-requirements.md](phase5.3-button-requirements.md) / [preliminary](phase5.3-button-preliminary-design.md) / [detailed](phase5.3-button-detailed-design.md) | 5.3 Button 三件套（TextWidget 抽取 + 居中文本） | ✅ 已实现（2026-08-13） |
| [phase5.4-interaction-requirements.md](phase5.4-interaction-requirements.md) / [preliminary](phase5.4-interaction-preliminary-design.md) / [detailed](phase5.4-interaction-detailed-design.md) | 5.4 交互基础设施三件套（Invalidate/Capture/Focus/Tab/按下态） | ✅ 已实现（2026-08-13） |
| [phase5.5-textbox-requirements.md](phase5.5-textbox-requirements.md) / [preliminary](phase5.5-textbox-preliminary-design.md) / [detailed](phase5.5-textbox-detailed-design.md) | 5.5 TextBox 三件套（码点编辑/光标/点击定位/裁切） | ✅ 5.5.1 MVP 已实现（2026-08-13） |
| [phase5.5.2-selection-requirements.md](phase5.5.2-selection-requirements.md) / [preliminary](phase5.5.2-selection-preliminary-design.md) / [detailed](phase5.5.2-selection-detailed-design.md) | 5.5.2 Selection + 修饰键三件套 | ✅ 已实现（2026-08-14） |
| [phase5.6-ime-requirements.md](phase5.6-ime-requirements.md) / [preliminary](phase5.6-ime-preliminary-design.md) / [detailed](phase5.6-ime-detailed-design.md) | 5.6 IME 候选窗跟随三件套（I1-I5 / P1-P5 / v1.0.4：系统 caret + IMM 双通道，客户区坐标语义） | ✅ 已实现（2026-08-15） |
| [phase5-architecture-review.md](phase5-architecture-review.md) | Phase 5 收尾架构回顾（输入责任分布 + InputManager YAGNI 评估 + R1 冗余修复/R2-R4 记账） | ✅ 已实现（2026-08-15） |

## Phase6 布局与状态控件（✅ 6.1/6.2 完成，2026-08-15/25）

| 文档 | 内容 | 状态 |
|------|------|------|
| [phase6.1-horizontallayout-requirements.md](phase6.1-horizontallayout-requirements.md) / [preliminary](phase6.1-horizontallayout-preliminary-design.md) / [detailed](phase6.1-horizontallayout-detailed-design.md) | 6.1 HorizontalLayout 三件套（Layout 边界原则 / 4 决策点 / diff 同构 + 10 条设计契约） | ✅ 已实现（2026-08-15） |
| [phase6.2-checkboxradio-requirements.md](phase6.2-checkboxradio-requirements.md) / [preliminary](phase6.2-checkboxradio-preliminary-design.md) / [detailed](phase6.2-checkboxradio-detailed-design.md) | 6.2 CheckBox/Radio 三件套（StateWidget 契约 6 条 + 勾/圆渲染 Phase 8 消解 + CheckBoxStyle/RadioStyle 进 Theme + 同父互斥） | ✅ 已实现（2026-08-25，含绘制断言 S11-S14） |

## Phase7 平台抽象（✅ 7.1/7.2/7.5 完成，2026-08-16/24/19）

| 文档 | 内容 | 状态 |
|------|------|------|
| [phase7-platform-requirements.md](phase7-platform-requirements.md) / [preliminary](phase7-platform-preliminary-design.md) / [detailed](phase7-platform-detailed-design.md) | 7.1.1 PlatformWindow 骨架三件套（Window 零 Win32 契约） | ✅ 已实现（2026-08-15/16） |
| [phase7-messagehandler-requirements.md](phase7-messagehandler-requirements.md) / [preliminary](phase7-messagehandler-preliminary-design.md) / [detailed](phase7-messagehandler-detailed-design.md) | 7.1.2 翻译器契约改造三件套（纯翻译 → Event → Host） | ✅ 已实现（2026-08-15/16） |
| [phase7-textinput-requirements.md](phase7-textinput-requirements.md) / [preliminary](phase7-textinput-preliminary-design.md) / [detailed](phase7-textinput-detailed-design.md) | 7.1.3 输入层抽象三件套（CaretGeometry 插入点模型） | ✅ 已实现（2026-08-16） |
| [phase7-backend-requirements.md](phase7-backend-requirements.md) / [preliminary](phase7-backend-preliminary-design.md) / [detailed](phase7-backend-detailed-design.md) | 7.1.4 Backend 注入三件套（决策 35 闭环 + RenderServices） | ✅ 已实现（2026-08-16） |
| [phase7-application-requirements.md](phase7-application-requirements.md) / [preliminary](phase7-application-preliminary-design.md) / [detailed](phase7-application-detailed-design.md) | 7.1.5 Application 解耦三件套（7.1 平台抽象闭环） | ✅ 已实现（2026-08-16） |
| [phase7.2-testing-requirements.md](phase7.2-testing-requirements.md) / [preliminary](phase7.2-testing-preliminary-design.md) / [detailed](phase7.2-testing-detailed-design.md) | 7.2 第一版：无窗口单元测试体系三件套（Tests/ 目录 + RunAllTests） | ✅ 已实现（2026-08-17） |
| [phase7.2-test-system-requirements.md](phase7.2-test-system-requirements.md) / [preliminary](phase7.2-test-system-preliminary-design.md) / [detailed](phase7.2-test-system-detailed-design.md) | 7.2 补强版：测试体系补强三件套（双子目标：轻量框架 + 清历史欠账） | ✅ 已实现（2026-08-24） |
| [phase7.5-callback-requirements.md](phase7.5-callback-requirements.md) / [preliminary](phase7.5-callback-preliminary-design.md) / [detailed](phase7.5-callback-detailed-design.md) | 7.5 事件回调三件套（SetOnClick/SetOnTextChanged + RaiseXxx 分离） | ✅ 已实现（2026-08-19） |

## Phase8 渲染增强（✅ 已实现，2026-08-24）

| 文档 | 内容 | 状态 |
|------|------|------|
| [phase8-rendering-enhancement-requirements.md](phase8-rendering-enhancement-requirements.md) / [preliminary](phase8-rendering-enhancement-preliminary-design.md) / [detailed](phase8-rendering-enhancement-detailed-design.md) | Phase 8 三件套（能力层：DrawLine/DrawRoundedRect/DrawImage/PushClip/PopClip/DrawFocusRect——GDI/msimg32） | ✅ 已实现（2026-08-24） |

## Phase8.5 文本系统 2.0（✅ 8.5.1/8.5.2/8.5.3 完成，2026-08-24/25）

| 文档 | 内容 | 状态 |
|------|------|------|
| [phase8.5-text-system2.0-requirements.md](phase8.5-text-system2.0-requirements.md) / [preliminary](phase8.5-text-system2.0-preliminary-design.md) | 8.5 职责确认 v1.1 + 初步设计 v1.2（覆盖整个 8.5 的 8 项范围） | ✅ 已实现 |
| [phase8.5.1-text-system2.0-detailed-design.md](phase8.5.1-text-system2.0-detailed-design.md) | 8.5.1 核心升级（IME 组合串模型 B + 剪贴板 + Timer + SetFont） | ✅ 已实现（2026-08-24） |
| [phase8.5.2-text-system2.0-detailed-design.md](phase8.5.2-text-system2.0-detailed-design.md) | 8.5.2 多行与滚动（行缓存/滚动/双击/跨行） | ✅ 已实现（2026-08-24） |
| [phase8.5.3-text-system2.0-detailed-design.md](phase8.5.3-text-system2.0-detailed-design.md) | 8.5.3 Undo/Redo（快照 + Composition 一次撤销） | ✅ 已实现（2026-08-25） |

## Phase8.6 渲染抗锯齿（✅ 已实现，2026-09-11）

| 文档 | 内容 | 状态 |
|------|------|------|
| [phase8.6-render-antialiasing-requirements.md](phase8.6-render-antialiasing-requirements.md) | 职责确认（圆角/圆弧覆盖度 AA：**两层拆分**——覆盖度生成 ⇄ 形状装配；掩码按半径缓存 + `ScratchDIB` 职责独立；**正式修订 9.5「约束 2」**；公共 API 与全部控件零改动；11 决策点含 D1 约束修订 / D9 L1+L2 两层测试 / **D11 `a==1` 语义一致性（R3 升 P0）**） | ✅ v1.1 封版 |
| [phase8.6-render-antialiasing-preliminary-design.md](phase8.6-render-antialiasing-preliminary-design.md) | 初步设计（`CornerCoverageMask` **pixel-square coverage 离散模型 + 9 项定义** / **D8 选 A** + 「钳制已消除退化」证明 + 三形状无分支 / **D11 语义统一 + `a==1` 等价性证明** / **D3 定 S=8** / **canonical 几何原则** / effective 半径缓存键 / L1 七用例含归一化域面积守恒 + L2 五用例 / 3 新建 + 4 修改） | ✅ v1.1 封版 |
| [phase8.6-render-antialiasing-detailed-design.md](phase8.6-render-antialiasing-detailed-design.md) | 详细设计（**11 项待定项全部形成实施决策** / `CornerCoverageMask.h` 全文 + 生成算法 8 条规则 + **精确字节锚点表（R=1..4）** + 浮点精确性论证 / 缓存 `SetSamples` 必须清空 / `GDIBackend` 三条路径全文 + `PatchSurface` fail-safe 降级 / L1 **15 条**用例含 `tol=0.5·√R/S`（**参考实现实测重写**）+ L2 精确坐标期望值 / CMake 零改动已核实 / 验收 A1–A11 / 10 步实施顺序） | ✅ v1.4 已实现（**174/174 通过**：158 既有零回归 + 15 AA 新增 + 1 ModelProbe 回归；含 `PatchSurface` 行宽缺陷修复 + `DrawFocusRect` 四角弧心对齐修复） |

## Phase9 主题系统（✅ 已实现，2026-08-25）

| 文档 | 内容 | 状态 |
|------|------|------|
| [phase9-theme-system-requirements.md](phase9-theme-system-requirements.md) / [preliminary](phase9-theme-system-preliminary-design.md) / [detailed](phase9-theme-system-detailed-design.md) | Phase 9 三件套（StyleField D7 契约 + Theme/DefaultTheme + TextStyle 单一真相 + 控件迁移 + cornerRadius 消费） | ✅ 已实现（2026-08-25，v1.0-v1.4 四轮 外部评审收敛） |

## Phase9.5 收尾补充（✅ 2026-08-28）

| 文档 | 内容 | 状态 |
|------|------|------|
| [phase9.5-wrapup-requirements.md](phase9.5-wrapup-requirements.md) | 收尾需求总纲（R1-R5 立项 + 关闭记账裁决） | ✅ 完成态 |
| [phase9.5-r1-clip-preliminary-design.md](phase9.5-r1-clip-preliminary-design.md) / [detailed](phase9.5-r1-clip-detailed-design.md) | R1 Clip 管线（PushClip/PopClip + TextBox 横向滚动——替换逐行截断） | ✅ 已实现（2026-08-28） |

## Phase9.6 动画系统（✅ 2026-08-30）

| 文档 | 内容 | 状态 |
|------|------|------|
| [phase9.6-animation-requirements.md](phase9.6-animation-requirements.md) / [preliminary](phase9.6-animation-preliminary-design.md) / [detailed](phase9.6-animation-detailed-design.md) | 动画系统三件套（per-Window AnimationManager + 插值/Easing + 时钟与脏标记契约） | ✅ 已实现（2026-08-30） |
| [phase9.6-progressbar-requirements.md](phase9.6-progressbar-requirements.md) / [preliminary](phase9.6-progressbar-preliminary-design.md) / [detailed](phase9.6-progressbar-detailed-design.md) | ProgressBar 三件套（ResolveAnimationManager 接缝 + 主题化） | ✅ 已实现（2026-08-31） |
| [phase9.6-collapsiblepanel-requirements.md](phase9.6-collapsiblepanel-requirements.md) / [preliminary](phase9.6-collapsiblepanel-preliminary-design.md) / [detailed](phase9.6-collapsiblepanel-detailed-design.md) | CollapsiblePanel 三件套（四向折叠 + 单动画值驱动） | ✅ 已实现（2026-08-30） |
| [phase9.6-panel-container-semantics-detailed-design.md](phase9.6-panel-container-semantics-detailed-design.md) | Panel 容器语义详设（背景透明契约变更） | ✅ 已实现（v1.1） |

## Phase9.7 自适应布局（✅ 2026-09-02）

| 文档 | 内容 | 状态 |
|------|------|------|
| [phase9.7-adaptive-layout-requirements.md](phase9.7-adaptive-layout-requirements.md) / [preliminary](phase9.7-adaptive-layout-preliminary-design.md) / [detailed](phase9.7-adaptive-layout-detailed-design.md) | 窗口→尺寸分配三件套（SetStretch/spacing/fillCrossAxis/触发链；契约 10 修订） | ✅ 已实现（v1.2 详设同步实现状态；ModelProbe 消费验证） |

## Phase9.8 AutoSize（✅ 2026-09-02）

| 文档 | 内容 | 状态 |
|------|------|------|
| [phase9.8-autosize-requirements.md](phase9.8-autosize-requirements.md) | 需求确认（尺寸意图三分「后调用者赢」/ §3.5 交互冻结 / §3.6 垂直居中验证项 / §3.7 副作用边界） | ✅ v1.5 定稿（两轮评审收敛） |
| [phase9.8-autosize-preliminary-design.md](phase9.8-autosize-preliminary-design.md) / [detailed](phase9.8-autosize-detailed-design.md) | 初设 + 详设（GetPreferredSize/AutoSize 签名与 4 行冻结实现 + ResolveMeasurer 接缝 + FakeTextMeasurer） | ✅ 已实现（2026-09-02，151 测试全绿） |

## Phase10 库化（✅ 完成，2026-09-06）

| 文档 | 内容 | 状态 |
|------|------|------|
| [phase10-library-requirements.md](phase10-library-requirements.md) | 需求确认（Public API 三层判定 / 测试接缝稳定性边界 / 下沉 src/ / install-export / 自包含测试 / 外部消费者验收） | ✅ v1.1（外部评审 7 项采纳） |
| [phase10-library-preliminary-design.md](phase10-library-preliminary-design.md) | 初步设计（89→80 Public / 9 Internal 逐头审查 / 下沉 src/ + PRIVATE src / install-export 布局 / ExactVersion / 依赖方向单向律） | ✅ v1.2 定稿（评审「可进详设」） |
| [phase10-library-detailed-design.md](phase10-library-detailed-design.md) | 详细设计（分类修正：RenderServices/BackendFactory 升 Public / 9 头移动清单 / 18 文件引用改写 / install 全文 / MinimalApp 全文 / 验收清单 10 项） | ✅ v1.1 已实施（2026-09-06——9 头下沉、80 Public 头零平台泄漏、MinimalApp 就位；**Phase 11 后 Public 头为 81**） |

## Phase11 图片解码（✅ 完成，2026-09-07）

| 文档 | 内容 | 状态 |
|------|------|------|
| [phase11-image-decode-requirements.md](phase11-image-decode-requirements.md) | 需求确认（WIC COM 解码 / Decode 新模块 / 静态函数 API / PBGRA 零转换契约 / 仅解码 API 不含控件） | ✅ v1.1（外部评审通过） |
| [phase11-image-decode-preliminary-design.md](phase11-image-decode-preliminary-design.md) | 初步设计（头全文草案 / WIC 管线九步 / COM RAII per-call / 链接库 PUBLIC 传播 / 测试 7 用例） | ✅ v1.1（评审「修改后通过」） |
| [phase11-image-decode-detailed-design.md](phase11-image-decode-detailed-design.md) | 详细设计（6 开放点全收：initguid+IID_PPV_ARGS 零 uuid.lib / ComRAII 模板 / 溢出两步数学界 / SH 主案+IStream 预案 / 测试资产生成策略 / 验收 6 项） | ✅ v1.1 已实施（2026-09-07——Decode 模块 + WIC 后端 + 8 用例，158 全绿） |


## Phase12 WindowChrome（✅ 已实施并收口）

> 定位：Borderless 无边框模式、`WM_NCCALCSIZE` / `WM_NCHITTEST` 拦截、最大化工作区纠偏与 DWM 阴影/圆角集成（含桌面/雾面前置三件套）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase12-windowchrome-requirements.md](phase12-windowchrome-requirements.md) | 需求确认（无边框契约与 DWM 边界） | ✅ v1.2 已确认 |
| [phase12-windowchrome-preliminary-design.md](phase12-windowchrome-preliminary-design.md) | 初步设计（平台拦截边界与状态字段） | ✅ v1.3 已通过 |
| [phase12-windowchrome-detailed-design.md](phase12-windowchrome-detailed-design.md) | 详细设计（实施规格与验收点） | ✅ v1.5 已实施 |

## Phase13 CaptionBar 自绘标题栏（✅ 已实施并验收，2026-09-14 ~ 09-15）

> 定位：自绘标题栏、`NCHITTEST` 委托组件树命中（按钮优先、空白区域 `HTCAPTION`）与最大/还原态同步。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase13-captionbar-requirements.md](phase13-captionbar-requirements.md) | 需求确认（自绘标题栏与系统拖放语义） | ✅ 已确认 |
| [phase13-captionbar-preliminary-design.md](phase13-captionbar-preliminary-design.md) | 初步设计（委托链与状态查询接口） | ✅ 已通过 |
| [phase13-captionbar-detailed-design.md](phase13-captionbar-detailed-design.md) | 详细设计（最小 diff 规格与测试） | ✅ 已实施并验收 |

## Phase14 托盘与拖入接缝（✅ 已实施并验收，2026-09-17）

> 定位：应用级 `PlatformApplication` seam 承接隐藏宿主窗口、`NOTIFYICON_VERSION_4` 回调翻译、`TaskbarCreated` 自愈及窗口级 `WM_DROPFILES` 文件拖入。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase14-tray-and-drop-requirements.md](phase14-tray-and-drop-requirements.md) | 需求确认（F1–F4 勘察事实与约束） | ✅ 已确认 |
| [phase14-tray-and-drop-preliminary-design.md](phase14-tray-and-drop-preliminary-design.md) | 初步设计（`PlatformApplication` / `TrayIcon` 边界） | ✅ 已通过 |
| [phase14-tray-and-drop-detailed-design.md](phase14-tray-and-drop-detailed-design.md) | 详细设计（实现规格与端到端校验） | ✅ 已实施并验收 |

## Phase15 滚动容器（`ScrollView` + 滚动条）（✅ 已实施并验收 2026-09-18）

> 定位：两阶段内容偏移契约（`GetContentOffsetX/Y` 统一供绘制/命中/绝对坐标消费）、`ClipsChildren` 命中闸门、双轴视口与自绘 `ScrollBar`。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase15-scrollview-requirements.md](phase15-scrollview-requirements.md) | 需求确认（K1–K18 勘察与滚动语义边界） | ✅ 已确认 |
| [phase15-scrollview-preliminary-design.md](phase15-scrollview-preliminary-design.md) | 初步设计（`ScrollContent` 坐标根与单源偏移） | ✅ 已通过 |
| [phase15-scrollview-detailed-design.md](phase15-scrollview-detailed-design.md) | 详细设计（逐文件规格与 ModelProbe 迁移） | ✅ 已实施并验收 |

## Phase16 桌面驻留层（`WindowLayer::Desktop`）（✅ 已实施并收口 2026-09-21）

> 定位：通过去掉 `WS_MINIMIZEBOX` 抵抗「显示桌面」最小化并结合前景事件 hook 稳定锚定在桌面窗口之上，不改公开 API（218 用例）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase16-desktop-layer-requirements.md](phase16-desktop-layer-requirements.md) | 需求确认（`WindowLayer::Desktop` 桌面壁纸层语义） | ✅ v1.4 已确认 |
| [phase16-desktop-layer-preliminary-design.md](phase16-desktop-layer-preliminary-design.md) | 初步设计（Z 序锚定与 `Show Desktop` 事实勘察） | ✅ v1.3 已通过 |
| [phase16-desktop-layer-detailed-design.md](phase16-desktop-layer-detailed-design.md) | 详细设计（分批规格与 A1–A8 验收） | ✅ v1.11 已实施 |

## Phase17 布局内边距（Layout padding）（✅ 已实施 226 用例）

> 定位：为 `VerticalLayout` / `HorizontalLayout` 增加可选整型硬内缩 `padding`（不改变 `Window` 根控件与 `CaptionBar` 贴边平台契约）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase17-layout-padding-requirements.md](phase17-layout-padding-requirements.md) | 需求确认（K1–K4 现状与四点缩进边界） | ✅ v1.2 已确认 |
| [phase17-layout-padding-preliminary-design.md](phase17-layout-padding-preliminary-design.md) | 初步设计（主轴/交叉轴退化至 0 规则） | ✅ v1.1 已通过 |
| [phase17-layout-padding-detailed-design.md](phase17-layout-padding-detailed-design.md) | 详细设计（A1–A6 用例规格） | ✅ v1.2 已实施 |

## Phase18 窗口 / 根背景能力（Window/Root background）（✅ 已实施 231 用例）

> 定位：打通 `Window::SetBackgroundColor` → `RenderingBackend::BeginFrame(Color)`，由 `Window` 持有唯一默认色，后端无状态消费（API +2）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase18-window-background-requirements.md](phase18-window-background-requirements.md) | 需求确认（清屏色源头与无状态边界） | ✅ v1.1 已确认 |
| [phase18-window-background-preliminary-design.md](phase18-window-background-preliminary-design.md) | 初步设计（`O1–O4` 开放项冻结） | ✅ v1.1 已通过 |
| [phase18-window-background-detailed-design.md](phase18-window-background-detailed-design.md) | 详细设计（A1–A7 验收与调用链） | ✅ v1.2 已实施 |

## Phase18.1 子节点内缩（Child inset）（⏸️ 已搁置 2026-09-22）

> **结论**：`CaptionBar` 贴边属平台拖曳契约，内容四周留白已可由透明 `Panel` 组合达成，框架零改动即已满足需求；独立 `SetInset` 搁置（重开条件见 [roadmap-deferred.md](roadmap-deferred.md) §7.8 #40）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase18.1-child-inset-requirements.md](phase18.1-child-inset-requirements.md) | 需求确认（三层需求拆解与搁置论证） | ⏸️ v1.1 已归档 |

## Phase19 鼠标事件维度补全（Mouse event dimensions）（✅ 已实施 2026-09-23 · 236 用例）

> 定位：从 `wParam` 完整翻译按下键位（私有位掩码 + `IsButtonDown`）与修饰键（`KeyModifier`），不破坏既有构造默认值（#41 / D-1）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase19-mouse-event-dimensions-requirements.md](phase19-mouse-event-dimensions-requirements.md) | 需求确认（#41 缺陷队列首项） | ✅ v1.1 已确认 |
| [phase19-mouse-event-dimensions-preliminary-design.md](phase19-mouse-event-dimensions-preliminary-design.md) | 初步设计（位掩码封装与 Alt 口径） | ✅ v1.1 已通过 |
| [phase19-mouse-event-dimensions-detailed-design.md](phase19-mouse-event-dimensions-detailed-design.md) | 详细设计（真翻译器测试与实现规格） | ✅ v1.1 已实施 |

## 框架缺陷审计与修复队列（✅ 队列 ①–⑩ 已闭环；明细见台账）

> 区分「违反已立契约/平台能力的真缺陷」与「纯架构能力缺口记账」，具体条目闭环现状已由 [roadmap-deferred.md](roadmap-deferred.md) 与各自 Phase 承接。

| 文档 | 内容 | 状态 |
|---|---|---|
| [framework-defect-audit.md](framework-defect-audit.md) | 缺陷审计与修复队列（§3 分界判据 · 缺陷队列 ①–⑩ 与记账清单 `A-1..A-10`） | ✅ 已同期更新（现为历史依据 + 判据规范） |
| [framework-implementation-audit.md](framework-implementation-audit.md) | 实现代码审计报告（32 项真伪筛查，批一–批九全量收口与残余边界记录） | ✅ 批九关闭 · 证据与判据归档 |

## Phase20 DPI 感知（DPI awareness）（✅ 已实施并收口 2026-09-24）

> 定位：启用 Per-Monitor V2，守住「公共 API 对外 DIP、平台边界换算、渲染层以下物理像素」原则，并在系统降级时通过真实读数回退保护（#8 / D-2）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase20-dpi-awareness-requirements.md](phase20-dpi-awareness-requirements.md) | 需求确认（K13 三入口与 `GetClientRect` 双口径） | ✅ v1.1 已确认 |
| [phase20-dpi-awareness-preliminary-design.md](phase20-dpi-awareness-preliminary-design.md) | 初步设计（DIP/物理边界与 `WM_DPICHANGED`） | ✅ v1.1 已通过 |
| [phase20-dpi-awareness-detailed-design.md](phase20-dpi-awareness-detailed-design.md) | 详细设计（五批规格与 `RG-1` 转交 Phase 20.1） | ✅ v1.3 已实施 |

## Phase20.1 渲染层 DPI 缩放（render DPI scaling）（✅ 已实施并收口 2026-09-24 · 252 用例）

> 定位：补齐 Phase 20 遗漏的 `Renderer` 入口 DIP → 物理像素折算边，使非 96 DPI 下的文字与几何绘出精确尺寸（闭环 `RG-1`）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase20.1-render-dpi-scaling-requirements.md](phase20.1-render-dpi-scaling-requirements.md) | 需求确认（`RG-1` 第二换算边定界） | ✅ v1.3 已确认 |
| [phase20.1-render-dpi-scaling-preliminary-design.md](phase20.1-render-dpi-scaling-preliminary-design.md) | 初步设计（命令流保 DIP、执行入口缩放到物理像素） | ✅ v1.2 已通过 |
| [phase20.1-render-dpi-scaling-detailed-design.md](phase20.1-render-dpi-scaling-detailed-design.md) | 详细设计（`Renderer` / `Window` 注入与全链测试） | ✅ v1.3 已实施 |

## Phase21 系统图标 → `Image`（✅ 已实施并收口 2026-09-27 · 261 用例）

> 定位：运行时 `HICON` / 文件路径转 `Image`（premultiplied BGRA）。前置探针推翻「按位深判透明度」，改为逐像素 alpha 存在性与 mask 回退，内核外壳分离以实现无头测试（G-2 / D-3）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase21-icon-to-image-requirements.md](phase21-icon-to-image-requirements.md) | 需求确认（`.ico` 文件已由 WIC 支持，范围收窄至运行时 `HICON`） | ✅ v1.1 已确认 |
| [phase21-icon-to-image-preliminary-design.md](phase21-icon-to-image-preliminary-design.md) | 初步设计（探针 P1–P13 与内核/外壳可测性分离） | ✅ v1.4 已通过 |
| [phase21-icon-to-image-detailed-design.md](phase21-icon-to-image-detailed-design.md) | 详细设计（11 步转换规格、RAII 守卫与 T21-1..9） | ✅ v1.2 已实施 |

## Phase22 `Create` 的 DIP 尺寸契约（✅ 已实施并收口 2026-09-29 · 264 用例）

> 定位：提取 `ApplyStartupSize` 在 `Create()` 尾部（安全脱离构造期 `WM_SIZE` 崩点）与 `Show()` 执行跨屏 DIP→物理换算，消除创建后至显示前的尺寸漂移（D-7）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase22-preshow-dip-geometry-requirements.md](phase22-preshow-dip-geometry-requirements.md) | 需求确认（非 96 DPI 下 `Create→Show` 偏移实测） | ✅ v1.1 已确认 |
| [phase22-preshow-dip-geometry-preliminary-design.md](phase22-preshow-dip-geometry-preliminary-design.md) | 初步设计（`ApplyStartupSize` 幂等 seam） | ✅ v1.1 已通过 |
| [phase22-preshow-dip-geometry-detailed-design.md](phase22-preshow-dip-geometry-detailed-design.md) | 详细设计（120 DPI 实测验证与四链全绿） | ✅ v1.3 已实施 |

## Phase23 工作线程 → UI 线程投递（✅ 已实施并收口 2026-09-29 · 275 用例）

> 定位：`Application::PostToUi` 经 `PlatformApplication` 以每应用队列 + `PostThreadMessageW` 唤醒、`swap` 锁外批量排水，安全退出且不引入线程池或重入（D-4 / G-3）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase23-worker-to-ui-dispatch-requirements.md](phase23-worker-to-ui-dispatch-requirements.md) | 需求确认（应用级边界，不泄漏 HWND） | ✅ v1.1 已确认 |
| [phase23-worker-to-ui-dispatch-preliminary-design.md](phase23-worker-to-ui-dispatch-preliminary-design.md) | 初步设计（`swap` 排水与线程唤醒契约） | ✅ v1.2 已通过 |
| [phase23-worker-to-ui-dispatch-detailed-design.md](phase23-worker-to-ui-dispatch-detailed-design.md) | 详细设计（8 文件改动与 11 项回归测试） | ✅ v1.3 已实施 |

## Phase 24 `DrawLine` 线段抗锯齿（✅ 已实施并收口 2026-10-01 · 297 用例）

> 定位：以 $S=8$ 圆帽覆盖度网格（`Render/LineCoverage`）与既有 `RasterizeMask` 复用完成任意角度与半透明线段 AA，轴对齐走回退，保留可控接缝余差声明（#43 / D-6）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase24-line-antialiasing-requirements.md](phase24-line-antialiasing-requirements.md) | 需求确认（线段几何收窄与 `DrawPolyline` 延后 #48） | ✅ v1.1 已确认 |
| [phase24-line-antialiasing-preliminary-design.md](phase24-line-antialiasing-preliminary-design.md) | 初步设计（圆帽胶囊、$S=8$ 与 `C-SEAM` 量化契约） | ✅ v1.2 已通过 |
| [phase24-line-antialiasing-detailed-design.md](phase24-line-antialiasing-detailed-design.md) | 详细设计（矩形化 `PatchSurface` 与三批验收） | ✅ v1.3 已实施 |

## Phase 25 列表 / 网格容器（✅ 已实施并收口 2026-10-02 · 307 用例）

> 定位：定位为纯布局排列（`ListLayout` / `GridLayout` 派生自 `Layout`），不设数据绑定/虚拟化；确立不可见子停泊自身负区 `(−w, −h)` 的 `C-VIS` 契约以修复 `UpdateContentExtent` 膨胀（#42 / D-5）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase25-list-container-requirements.md](phase25-list-container-requirements.md) | 需求确认（布局层定位与 #33 虚拟化划界） | ✅ v1.1 已确认 |
| [phase25-list-container-preliminary-design.md](phase25-list-container-preliminary-design.md) | 初步设计（`C-VIS` 停泊契约与不读父尺寸纪律） | ✅ v1.2 已通过 |
| [phase25-list-container-detailed-design.md](phase25-list-container-detailed-design.md) | 详细设计（ModelProbe 迁移与 T25-1..11） | ✅ v1.2 已实施 |

## Phase 26 FreeType 文本栈 + GL 渲染后端（✅ 已实施并收口 2026-10-04 · 319 用例）

> 定位：vendor FreeType 2.14.3 构建 `FontEngine` / `FreeTypeTextMeasurer` 与 WGL `GLRenderer`（1024² 字形图集），并统一测量链至窗口 DPI（闭环 D-8，M-4 落地；热滚动零重复栅格化）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase26-freetype-gl-requirements.md](phase26-freetype-gl-requirements.md) | 需求确认（双后端可选与同 DPI 测量/渲染对齐） | ✅ v1.2 已确认 |
| [phase26-freetype-gl-preliminary-design.md](phase26-freetype-gl-preliminary-design.md) | 初步设计（`FontEngine` 分离、WGL 所有权与图集键） | ✅ v1.2 已通过 |
| [phase26-freetype-gl-detailed-design.md](phase26-freetype-gl-detailed-design.md) | 详细设计（四轮修复记录：SimSun 基线/定时器饿死/裁剪量化与 O10/O11） | ✅ v1.8 已实施 |

## Phase 27 绘制命令构建层视口剔除（✅ 已实施并收口 2026-10-04 · 330 用例）

> 定位：基于 `Widget::Paint` 必推自身矩形为裁剪区的结构不变量，在 `PaintContext` 侧维护构建镜像裁剪栈 `m_cullStack` 与 `m_cullBase`，于命令构建前跳过视口外子树（−98.9% 命令数，#49）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase27-viewport-culling-requirements.md](phase27-viewport-culling-requirements.md) | 需求确认（D3 `OnPaint` 副作用审查前提与坐标同源原则） | ✅ v1.1 已确认 |
| [phase27-viewport-culling-preliminary-design.md](phase27-viewport-culling-preliminary-design.md) | 初步设计（12 处 `OnPaint` 审计清零与 float 严格相交） | ✅ v1.1 已通过 |
| [phase27-viewport-culling-detailed-design.md](phase27-viewport-culling-detailed-design.md) | 详细设计（单表达式 `self` 守卫、无公共开关 API、T27-1..12） | ✅ v1.2 已实施 |

## Phase 28 长文本测量与绘制优化（✅ 已实施并收口 2026-10-06 · 335 用例）

> 定位：基于真实消费测量数据分治三端热点——FT per-glyph advance memo 消除单字重复加载、`TextWidget` 显式修订指纹短路 `GetPreferredSize`、`GDIBackend::DrawText` 凭 `GetClipBox` + +1 守卫前截断（#50）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase28-long-text-cost-requirements.md](phase28-long-text-cost-requirements.md) | 需求确认（测量归因、机制/性能分离与三条红线） | ✅ v1.1 已确认 |
| [phase28-long-text-cost-preliminary-design.md](phase28-long-text-cost-preliminary-design.md) | 初步设计（Spike 0 实证、advance memo 与前景截断） | ✅ v1.1 已通过 |
| [phase28-long-text-cost-detailed-design.md](phase28-long-text-cost-detailed-design.md) | 详细设计（13 处 `MarkTextChanged` 入口、T28-4 硬门槛 11/11 逐位一致） | ✅ v1.5 已实施 |

## Phase 29 文本多行能力（`Label` / `TextWidget` word-wrap）（✅ 已实施并收口 2026-10-07 · 349 用例）

> 定位：默认关闭的结构性 opt-in 自动换行；引入 `TextMeasurer::FitText` 三链测量原语、D29-Ⅰ 贪心断行状态机（含最小禁则）及由 `GetPreferredSize` 与 `OnPaint` 共享的五元组指纹 `TextLayout`（#52）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase29-text-multiline-requirements.md](phase29-text-multiline-requirements.md) | 需求确认（四红线、`GetWidth()` 宽度源与 API 口径） | ✅ v1.1 已确认 |
| [phase29-text-multiline-preliminary-design.md](phase29-text-multiline-preliminary-design.md) | 初步设计（共享 `TextLayout`、`FitText` 默认二分体与断行规则） | ✅ v1.1 已通过 |
| [phase29-text-multiline-detailed-design.md](phase29-text-multiline-detailed-design.md) | 详细设计（状态机流、E1–E8 勘误 incl. GDI `floor` 舍入与 E2E 交付） | ✅ v1.4 已实施 |

## Phase 30 文本 / 交互快修小轮（✅ 已实施并收口 2026-10-07 · 356 用例）

> 定位：独立 `Core/TextAlignment.h` 与 Button 三态对齐、`TextBoxStyle::caretColor` 补齐、`Vertical/HorizontalLayout` 补 `C-VIS`（含 `gapCount` 全隐藏乘法守卫），以及 `CollapsiblePanel` 可见性同步回归钉住（#51）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase30-interaction-quickfix-requirements.md](phase30-interaction-quickfix-requirements.md) | 需求确认（3 改 + 1 钉收窄与红线） | ✅ v1.1 已确认 |
| [phase30-interaction-quickfix-preliminary-design.md](phase30-interaction-quickfix-preliminary-design.md) | 初步设计（`TextAlignment` 独立头与 `C-VIS` 映射） | ✅ v1.0 已通过 |
| [phase30-interaction-quickfix-detailed-design.md](phase30-interaction-quickfix-detailed-design.md) | 详细设计（全可见逐位不变专测与 `gapCount` 守卫） | ✅ v1.2 已实施 |

## DesktopNest 规划与 M1 观感里程碑（✅ 规划及 M1 三件套归档 —— 应用侧启动，框架改动为 0）

> 定位：DesktopNest 跨应用路线规划与 M1 应用侧组合验证桌面图标/侧栏/浮层规范，驱动后续 Phase 31 等框架接口缺口显性化。

| 文档 | 内容 | 状态 |
|---|---|---|
| [desktopnest-roadmap.md](desktopnest-roadmap.md) | DesktopNest 总规划（阶段拆分、置底 vs On Desktop 决策依据与残差 `G-1..G-7`） | ✅ 已归档 |
| [desktopnest-m1-requirements.md](desktopnest-m1-requirements.md) | 需求确认（应用侧视角与框架能力复用范围） | ✅ 已归档 |
| [desktopnest-m1-preliminary-design.md](desktopnest-m1-preliminary-design.md) | 初步设计（P1 浮层与桌面工作区组合） | ✅ 已归档 |
| [desktopnest-m1-detailed-design.md](desktopnest-m1-detailed-design.md) | 详细设计（实施规格与框架边界映射） | ✅ 已归档 |

## Phase 31 窗口几何与行高口径（✅ 已实施并收口 2026-10-08 · 366 用例）

> 定位：`Window::SetBounds`/`GetBounds` 请求语义，以及基于真实字形度量的统一 `LineHeight` 公式（消除 GDI `tmHeight` 与 FreeType `metrics.height` 分叉；`O10` 跨字体残差经 2026-10-17 裁决记为已知现状，另立 `#69`）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase31-window-geometry-and-line-height-requirements.md](phase31-window-geometry-and-line-height-requirements.md) | 需求确认（窗口几何接口与 `REQ-05` 行高统一） | ✅ v1.1 已确认 |
| [phase31-window-geometry-and-line-height-preliminary-design.md](phase31-window-geometry-and-line-height-preliminary-design.md) | 初步设计（请求回读语义与先定标准再对齐两链纪律） | ✅ v1.1 已通过 |
| [phase31-window-geometry-and-line-height-detailed-design.md](phase31-window-geometry-and-line-height-detailed-design.md) | 详细设计（全链 366/366、`REQ-05` 闭环与 `O10` §13.6 裁决） | ✅ v1.5 已实施 |

## Phase 32 窗口雾面衬底（Frosted Backdrop）（✅ 已实施并全链收口 2026-10-14 · 445 用例）

> 定位：整窗 Acrylic 衬底与控件不透明共存（非分层窗）。32bpp DIB 后缓冲 + 分层命令 alpha 修补（实心 A=255 / 圆角覆盖率预乘 / 文本线条哨兵）+ 0.8 预乘白降级；同期关闭 `#57`（`~Application` 容器析构 UB）与 `#58`（`DwmExtendFrameIntoClientArea` 左/右/底边为 0 与 `nColor` 携带 tint RGB）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase32-frosted-backdrop-requirements.md](phase32-frosted-backdrop-requirements.md) | 需求确认（探针 1–6、降级形态与 K13 控件不透明契约） | ✅ v1.2 已确认 |
| [phase32-frosted-backdrop-preliminary-design.md](phase32-frosted-backdrop-preliminary-design.md) | 初步设计（`PD32-3` DIB 切换、状态生命周期与失败降级） | ✅ v1.1 已通过 |
| [phase32-frosted-backdrop-detailed-design.md](phase32-frosted-backdrop-detailed-design.md) | 详细设计（三层修补、#56 半透明图像记账与 #57/#58 闭环） | ✅ v1.2 已实施 |

## Phase 33 圆弧绘制原语（`DrawArc`）与环形进度控件（`RingProgressBar`）（✅ 已实施并收口 2026-10-16 · 465 用例）

> 定位：首个曲线原语（圆心/半径/起扫角度/线宽/圆帽）及 `GenerateArcCoverage` 覆盖度通道，GDI 沿用 `PatchSurface`、GL 入图集并带二值回退，配合 `RingProgressBar` 与跨仓 `PaintProbe` 全链验证（`REQ-08`）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase33-draw-arc-requirements.md](phase33-draw-arc-requirements.md) | 需求确认（C33-1..10 几何/圆帽契约与面积解析域订正） | ✅ v1.3 已确认 |
| [phase33-draw-arc-preliminary-design.md](phase33-draw-arc-preliminary-design.md) | 初步设计（公共几何度量谓词、图集键与无曲线图元回退） | ✅ v1.2 已通过 |
| [phase33-draw-arc-detailed-design.md](phase33-draw-arc-detailed-design.md) | 详细设计（批一–批四全链 465/465、`E33-1..11` 与 R9 跨仓交付） | ✅ v1.2 已实施 |

## Phase 34 后端几何取整口径统一（✅ 已收口 2026-10-19 · 474 用例）

> 定位：将 `GDIBackend` 三种并存取整统一到 `BackendContract::QuantizeEdge`（`floor` 边界与两边界相减求尺寸，`ToPixelRect` / `ToPixelBox`），使裁剪区与绘制区及 GDI 与 GL 口径完全同源（#46）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase34-backend-quantization-requirements.md](phase34-backend-quantization-requirements.md) | 需求确认（K1–K6 现状勘察、范围守卫与豁免边界） | ✅ v1.4 已冻结 |
| [phase34-backend-quantization-preliminary-design.md](phase34-backend-quantization-preliminary-design.md) | 初步设计（`ToPixelRect` / `ToPixelBox` 与非目标 `DrawLine` bbox 豁免） | ✅ v1.15 已通过 |
| [phase34-backend-quantization-detailed-design.md](phase34-backend-quantization-detailed-design.md) | 详细设计（9 条判别测试与跨缩放实证闭环；T34-5 探针单列） | ✅ v1.2 已实施 |

## Phase 35 可嵌套点击控件的事件消费与安全销毁（✅ 已实现并冻结 2026-10-19 · 487 用例）

> 定位：规范 `Event::SetHandled()` 终止事件冒泡、`Button` Down/Up 成对自动消费及 `Widget::RequestRemove()` 脱树并由 `DispatchGuard` 延迟至最外层派发结束后安全析构（API 净增 `+1`）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [规范/事件消费与安全销毁规范.md](规范/事件消费与安全销毁规范.md) | 长期契约与实施规格（含 §6 维护者四问与友元访问桥说明） | ✅ 已冻结并实施 |

## Phase 36 `Font` 表达力（✅ 已实施并收口 2026-10-24 · 497 用例）

> 定位：补齐 `Font::weight`（`uint16_t`，默认 400）、TTC `faceIndex` 解析与复合注册表项查询，基于系统字体 `nameID-1` 精确分组与 `OS/2` 权重就近轻选规则统一双链匹配，合成粗体契约延后为 `C36-4`（#70 / `REQ-10`）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase36-font-expressiveness-requirements.md](phase36-font-expressiveness-requirements.md) | 需求确认（11 处缓存键勘察与默认路径逐位不变红线） | ✅ v1.1 已确认 |
| [phase36-font-expressiveness-preliminary-design.md](phase36-font-expressiveness-preliminary-design.md) | 初步设计（`ResolveFileCandidates` 兼容方案与真机探针结论） | ✅ v1.1 已通过 |
| [phase36-font-expressiveness-detailed-design.md](phase36-font-expressiveness-detailed-design.md) | 详细设计（`nameID-1` / `OS/2` 选面规则、五链 497/497 与库外验收） | ✅ v1.3 已实施 |

## Phase 37 单行 `TextBox` 垂直对齐（✅ 已实施并收口 2026-10-26 · 513 用例）

> 定位：新增 `Core/VerticalAlignment.h` 与 `TextBoxStyle::verticalAlign`，`Top`（默认）快路径结构性不读行数以保零回归，`Center` 下将 X/Y 文本起点统一至绘制、光标与命中三处消费（#76 / `REQ-14`）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase37-textbox-vertical-align-requirements.md](phase37-textbox-vertical-align-requirements.md) | 需求确认（单行定高居中诉求与既有 `padding` 解耦） | ✅ 已确认 |
| [phase37-textbox-vertical-align-preliminary-design.md](phase37-textbox-vertical-align-preliminary-design.md) | 初步设计（独立公共头与结构性同源起点） | ✅ 已通过 |
| [phase37-textbox-vertical-align-detailed-design.md](phase37-textbox-vertical-align-detailed-design.md) | 详细设计（三批落地、五链 513/513 与 harness 探针收敛） | ✅ v1.2 已实施 |

## Phase 38 `TextBox` 编辑器软折行（✅ 已实施并收口 2026-10-29 · 531 用例）

> 定位：将 `Label` 折行引擎接入 `TextBox` 编辑器，严格分离 `LineRange` 归属（光标/选区/IME）与 `LineDrawRange` 裁剪（绘制/禁则悬挂），以 10 维失效键取代布尔脏位并同步修正内边界 clip（#77 / `REQ-15`）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase38-textbox-soft-wrap-requirements.md](phase38-textbox-soft-wrap-requirements.md) | 需求确认（`LineRange` / `LineDrawRange` 双口径与默认关结构保证） | ✅ v1.2 已确认 |
| [phase38-textbox-soft-wrap-preliminary-design.md](phase38-textbox-soft-wrap-preliminary-design.md) | 初步设计（10 维失效键、clip 内边界与脚本驱动样本） | ✅ v1.2 已通过 |
| [phase38-textbox-soft-wrap-design-review.md](phase38-textbox-soft-wrap-design-review.md) | 外部设计评审记录（P0/P1/P2 收敛与对码验证依据） | ✅ 已归档 |
| [phase38-textbox-soft-wrap-detailed-design.md](phase38-textbox-soft-wrap-detailed-design.md) | 详细设计（五批六提交、四链 531/531 与实施勘误） | ✅ v1.5 已实施 |

## Phase 39 UTF-8 码点索引 $O(N)$ 单趟重写（✅ 已实施并收口 2026-10-29 · 538 用例）

> 定位：新增 `DecodeCodepointAt` 将 `RecalculateLines` 非折行支重写为单趟线行扫描（消除循环体内双重码点偏移重扫），并等价优化默认 `FitText` 二分逻辑（#78）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase39-utf8-index-requirements.md](phase39-utf8-index-requirements.md) | 需求确认（实证归因、SSO 分配澄清与范围界定） | ✅ 已确认 |
| [phase39-utf8-index-preliminary-design.md](phase39-utf8-index-preliminary-design.md) | 初步设计（单趟前进解码与等价变换论证） | ✅ 已通过 |
| [phase39-utf8-index-detailed-design.md](phase39-utf8-index-detailed-design.md) | 详细设计（批 0 采集、5 万随机串对拍、四链 538/538） | ✅ v1.3 已实施 |

## Phase 40 折行支逐行整串预备消除（✅ 已实施并收口 2026-10-29 · 546 用例）

> 定位：针对折行支每行重复扫描整串的 $O(L \cdot N)$ 问题，在 `TextWidget` 基类懒建码点↔字节偏移表并新增 5 参 `FitText` 重载，保持 4 参旧契约与自定义测量器零改动（#83）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase40-softwrap-perline-prep-requirements.md](phase40-softwrap-perline-prep-requirements.md) | 需求确认（K7/K8/K9 同源收敛与 4 参契约保护） | ✅ 已确认 |
| [phase40-softwrap-perline-prep-preliminary-design.md](phase40-softwrap-perline-prep-preliminary-design.md) | 初步设计（基类偏移表与 5 参重载方案） | ✅ 已通过 |
| [phase40-softwrap-perline-prep-detailed-design.md](phase40-softwrap-perline-prep-detailed-design.md) | 详细设计（两批落地、负对照与四链 546/546） | ✅ v1.4 已实施 |

## Phase 41 帧级布局 / 重绘失效结算管线（✅ 已实施并收口 2026-10-30 · 556 用例）

> 定位：建立「重绘 = 值真变、布局 = 值真变且有布局读取」双通道失效机制、`NotifyGeometryChanged` 与帧首 `SettleLayoutIfDirty` 结算，取消 `OnWindowResized` 重复全树重排（#79）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase41-frame-layout-invalidation-pipeline-requirements.md](phase41-frame-layout-invalidation-pipeline-requirements.md) | 需求确认（三处历史契约推翻声明与管道边界） | ✅ v1.2 已确认 |
| [phase41-frame-layout-invalidation-pipeline-preliminary-design.md](phase41-frame-layout-invalidation-pipeline-preliminary-design.md) | 初步设计（双通道脏标记、真源收缩与 `CollapsiblePanel` 记账） | ✅ v1.3 已通过 |
| [phase41-frame-layout-invalidation-pipeline-detailed-design.md](phase41-frame-layout-invalidation-pipeline-detailed-design.md) | 详细设计（三批落地、九条机检全中与四链 556/556） | ✅ v1.3 已实施 |

## Phase 42 Window 级浮层根 Overlay Root 与 Light Dismiss（✅ 已实施并收口 2026-10-30 · 572 用例）

> 定位：在 `Window` 增设独立 `m_overlayRoot` 与 Normal/Modal/Tooltip 三层栈以逃逸父容器裁剪，实现命中层级优先、点外关闭、ESC/焦点闭环与帧末延迟析构防 UAF（#81）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase42-window-overlay-root-requirements.md](phase42-window-overlay-root-requirements.md) | 需求确认（双根拓扑、Tooltip 穿透与 Light Dismiss 需求） | ✅ v1.1 已确认 |
| [phase42-window-overlay-root-preliminary-design.md](phase42-window-overlay-root-preliminary-design.md) | 初步设计（三层 Panel、`OverlayTypes.h` 与延迟销毁队列） | ✅ v1.1 已通过 |
| [phase42-window-overlay-root-detailed-design.md](phase42-window-overlay-root-detailed-design.md) | 详细设计（四批落地、I42-1..5 满分与四链 572/572） | ✅ v1.1 已实施 |

## Phase 43 滚动条双轴厚度（✅ 已实施并收口 2026-11-01 · 584 用例）

> 定位：将恒读竖条的 `ScrollView::BarThickness()` 拆分为私有的 `VerticalBarThickness()` / `HorizontalBarThickness()` 并修正 6 处几何站，同时关闭仓内审计 `M5`/`M6`（#84）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase43-scrollbar-axis-thickness-requirements.md](phase43-scrollbar-axis-thickness-requirements.md) | 需求确认（`M5`/`M6` 交叉引用与双轴厚度独立契约） | ✅ v1.3 已确认 |
| [phase43-scrollbar-axis-thickness-preliminary-design.md](phase43-scrollbar-axis-thickness-preliminary-design.md) | 初步设计（六站点切换与 `\|\|` 短路非屏蔽样本矩阵） | ✅ v1.2 已通过 |
| [phase43-scrollbar-axis-thickness-detailed-design.md](phase43-scrollbar-axis-thickness-detailed-design.md) | 详细设计（整函数替换、三臂负对照与 584/584） | ✅ v1.1 已实施 |

## Phase 44 段内内联样式（styled spans）（✅ 已实施并收口 2026-11-08 · 599 用例）

> 定位：引入 `Widget/TextSpan.h`（`TextSpan`/`SpanStyle`）与原子 `SetStyledText`，以多 run `DrawText` 复用双端执行体并接入 `TextBox` 只读选中复制通路（#88 / `REQ-20`）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase44-styled-spans-requirements.md](phase44-styled-spans-requirements.md) | 需求确认（`REQ-20` 核验与「外部消费者」口径澄清） | ✅ 已确认 |
| [phase44-styled-spans-preliminary-design.md](phase44-styled-spans-preliminary-design.md) | 初步设计（Schema A 复用命令流、`BuildRunsForRange` 与防重入护栏） | ✅ 已通过 |
| [phase44-styled-spans-detailed-design.md](phase44-styled-spans-detailed-design.md) | 详细设计（批一/批二落地、C44-8 身份守卫与 599/599） | ✅ v1.0 已实施 |

## Phase 45 styled spans 表达力扩展（✅ 已实施并收口 2026-11-09 · 608 用例）

> 定位：沿 Phase 44 run 管线扩展 `Font.italic` / `SpanStyle.italic` / `SpanStyle.size` 与 `TextMeasurer::Ascent` 最大行框共享基线，消除跨字体/混字号基线错位（#89 / `REQ-20`）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase45-styled-spans-expressiveness-requirements.md](phase45-styled-spans-expressiveness-requirements.md) | 需求确认（五道闸门签字与斜体/混字号边界） | ✅ 已确认 |
| [phase45-styled-spans-expressiveness-preliminary-design.md](phase45-styled-spans-expressiveness-preliminary-design.md) | 初步设计（`Ascent` 虚接口与 `EffectiveLineBoxFont`） | ✅ 已通过 |
| [phase45-styled-spans-expressiveness-detailed-design.md](phase45-styled-spans-expressiveness-detailed-design.md) | 详细设计（三批落地、T45-5 双相保真与 608/608） | ✅ v1.2 已实施 |

## Phase 46 GL/FreeType 字体能力对齐（✅ 需求 v1.5 · 初设 v1.2 · 详设 v1.2，已实施闭环 2026-11-15）

> 定位：补齐 FreeType 路径的合成粗体、合成斜体及本地化 `FT_Get_Sfnt_Name` 中文字体族名解析（收编 `#69` / `L45-1` / `C36-4`；#94）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase46-gl-font-parity-requirements.md](phase46-gl-font-parity-requirements.md) | 需求确认（`D46-0~5` + `CB-1`/`CB-2` 全数签字，含 10 文件预算与 `T46-5/6` 守卫口径） | ✅ v1.5 已锁定 |
| [phase46-gl-font-parity-preliminary-design.md](phase46-gl-font-parity-preliminary-design.md) | 初步设计（吸收 Qwen4 二轮 `P1-A ⓑ` + `P2×5`：`>= 1.2px`、96～103 ms 归档、放行详设） | ✅ v1.2 通过 |
| [phase46-gl-font-parity-detailed-design.md](phase46-gl-font-parity-detailed-design.md) | 详细设计（10 文件规约、方案 ⓐ 守卫、Batch 0→1→2 落地、`615→622`） | ✅ v1.2 已实施（MinGW/Clang 验证） |

## Phase 47 GL 后端渲染缓存与自动化像素验证（✅ 已收口，2026-11-16）

> 定位：解决 `GLGlyphAtlas` 单页满容与 `#75` ③ 圆弧丢失、`DrawImage` 每帧建删纹理与 WGL 自动化像素验证缺口（#95）。

| 文档 | 内容 | 状态 |
|---|---|---|
| [phase47-gl-cache-and-verification-requirements.md](phase47-gl-cache-and-verification-requirements.md) | 需求确认（`K47-1~9`、`D47-1~4 = ⓐ` 全锁定、`T47-1~7` 含极端帧内重置与 `O47-1~3` 探针） | ✅ v1.2 终审通过 |
| [phase47-gl-cache-and-verification-preliminary-design.md](phase47-gl-cache-and-verification-preliminary-design.md) | 初步设计（`O47-1~3` 实机定标、多页图集双路径淘汰、`DrawImage` LRU、+9 用例） | ✅ v1.3 通过放行 |
| [phase47-gl-cache-and-verification-detailed-design.md](phase47-gl-cache-and-verification-detailed-design.md) | 详细设计（6 文件逐接口规约、`BindTexture` 唯一入口、`IsSlotAlive` 重建与 Batch 0→1→2） | ✅ v1.1 已实施验证（MinGW/Clang，631 用例） |

## 专项设计（GDI 混合字体基线与下伸部防护，✅ 2026-11-15）

> 定位：解决 `#91 / #92 / #93` 单行 `TextBox` / 文本混合字体的 GDI 基线偏移对齐与斜体/下伸部边界防裁切，作为轻量专项独立收口。

| 文档 | 内容 | 状态 |
|---|---|---|
| [gdi-mixed-font-baseline-and-overhang-design.md](gdi-mixed-font-baseline-and-overhang-design.md) | 专项设计与验证记录（`#91 / #92 / #93`） | ✅ 已实施并验证 |

## Window 所有权与生命周期（✅ 初设 v1.1 → 详设 v1.2 已实施）

> **独立契约文档**——不属任何 Phase，故不用 `phaseN-*` 命名（阶段由文档头部 / §8 跟踪）；#57 `~Application` 析构序缺陷已于 Phase 32 期间修复闭环。

| 文档 | 内容 | 状态 |
|---|---|---|
| [window-ownership.md](window-ownership.md) | 契约主体（初步设计：三层归属、三条闭环链、#57 修复闭环、Deferred 清单） | ✅ v1.1 → 已实施 |
| [window-ownership-detailed-design.md](window-ownership-detailed-design.md) | 详细设计（实施规格：逐文件 diff、218 用例全绿验证） | ✅ v1.2 已实施 |

## 滚动条的摆位与外观（scrollbar-placement-and-appearance）（✅ **v1.6 已收口**，2026-09-30 新建 / 2026-10-01 实施 · 四链复核 · 缺陷修复 · 观感变更）

> **来源**：ModelProbe 的 JSON 预览区（`examples/ModelProbe/ModelProbe.cpp:434`）——勘察后确认它**不是手搓滚动**（`TextBox` 自带 Phase 8.5.2 滚轮滚动 + 光标跟随 + `Ctrl+A/C` 复制），真正缺的是**滚动条**。
> **定位**：**跨阶段的框架能力扩展**——非新 Phase ⇒ **不用** `phaseN-*` 命名（同 `window-ownership.md` 先例），阶段由文档头部与 §7 修订记录跟踪。

| 文档 | 内容 | 状态 |
|------|------|------|
| [scrollbar-placement-and-appearance.md](scrollbar-placement-and-appearance.md) | **需求 + 设计 + 验收合并稿**（原名 `textbox-scrollbar.md`）：现状勘察 **K1–K16** · 范围 **△1–△10**（`TextBox` 组合 `ScrollBar` · `OnPaint` 惰性同步 + 值变化守卫 · 文本区让位 · **ScrollView 条内缩 + 视口随收** · **轨道默认透明**）· 决策 **D1–D14** · ★ **范围外含「否决换 `ScrollView`」的三条理由** · ★ **§7 实现约束 C-1–C-8** · 验收 **T1–T11 / A1–A2** | ✅ **v1.6 已收口**（四链 283/283 + 手势隔离修复 + 两侧条内缩 + 轨道默认透明 · 本机 286/286） |

## ModelProbe Demo（✅ P1/P2 已实现，2026-09-01/11）

> 第一个真实消费者（Phase 10 起从框架移出至 `examples/ModelProbe/`）。**文档独立子目录 `docs/dev/model-probe/`**，与框架 `phaseN-*` 区隔；2026-09-11 从仓库根 `model-probe-docs/` 移入 docs/ 体系（统一文档入口）。

| 文档 | 内容 | 状态 |
|------|------|------|
| [modelprobe-p1-preliminary-design.md](model-probe/modelprobe-p1-preliminary-design.md) | P1 初步设计（6 项框架能力：ChildProcess / GetExecutableDirectory / TextBox echo·只读·形态 / Button hover / Panel 形态 + demo 组装 ModelProbePage；9 开放点全部收敛） | ✅ v1.4（P1 已实现） |
| [modelprobe-p1-detailed-design.md](model-probe/modelprobe-p1-detailed-design.md) | P1 详细设计（ChildProcess 句柄继承矩阵 + 描边环几何冻结 + hover 三态 + 22 条测试用例 + **§10 P2 演进补记 7 项**） | ✅ v1.2（P1 已实现） |

- **代码**：`examples/ModelProbe/`（ECDI GUI 工具 + `probe.exe` Go 后端 RCDATA 资源嵌入 + `app.ico`）
- ⚠️ **P2 演进无独立设计文档**（未走五阶段法）——资源嵌入 / `SetSingleLine` / 二次查询修复等 7 项补记于详设 §10，**不可当作设计依据**

## 规范（跨阶段的长期契约）

| 文档 | 内容 | 状态 |
|------|------|------|
| [规范/事件消费与安全销毁规范.md](规范/事件消费与安全销毁规范.md) | 事件消费与安全销毁（Phase 35 的实现契约）：**§1 三层「消费」对照**（命中消费 / 事件消费 / 移除请求互不代替）· **§2 消费语义**（`BubbleAlongChain` 三终止条件 + `Button` Down/Up **都**自动消费 + 不消费即冒泡 + 嵌套可点击控件红线）· **§3 `Widget::RequestRemove()`**（派发中只摘树、派发结束才析构；`RemoveChild` 不延迟的分工）· **§4 延迟边界 = 整次派发（最外层）** · **§5 验收对照 13 例** · **§6 维护者四问答复**；回归测试 `ECDI/src/Tests/EventConsumptionTests.cpp` | ✅ **已实现并冻结（2026-10-19）**——全套 **487/487** 通过 |

## 文档约定`phaseX.Y-<module>-<type>.md`（子阶段编号 + 模块名 + 阶段类型；2026-08-25 全量规范化：Phase 5/6/7 按内容编号对齐，如 `phase5.3-button-requirements.md`、`phase6.2-checkboxradio-detailed-design.md`、`phase7.5-callback-requirements.md`；阶段级评审文档保留 `phaseN-<module>.md`）
- 子目录：demo / 非框架文档放独立子目录（如 `docs/dev/model-probe/`），与框架 `phaseN-*` 区隔但统一在 `docs/dev/` 入口下
- **非阶段文档**（跨阶段的长期契约 / 规划类，如 `window-ownership.md`、`desktopnest-roadmap.md`）：**不用** `phaseN-*` 前缀——阶段由文档头部与 §修订记录跟踪，命名取 `<主题>.md`；在「开发进度」之外单列章节索引
- **`usage.md` = 使用指南**（2026-10-18 新增，非阶段文档，位于 `docs/` 根目录）：面向第三方开发者的「怎么用」入口，内容以 `include/ECDI/**` 公共头合约为准；设计结论仍归各 Phase 三件套，使用文档不改写设计理由
- 五阶段法：职责确认 → 初步设计 → 详细设计 → 实现 → 测试，设计文档在实现前评审通过
- 文档内附修订记录（v1.0 → v1.1...），实现中发现的与文档出入必须回写
- 所有文档带 UTF-8 BOM（`ef bb bf`——MSVC 源码同规范）

