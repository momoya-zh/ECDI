# ECDI 设计文档索引

> 本文档是 `docs/` 的索引。设计文档随代码提交 git，从 Phase4 起为强制约定（职责确认 / 初步设计 / 详细设计 各阶段文档正常写入本目录）。

## 开发进度（2026-09-17 更新）

> **当前规模锚点（防止各处历史数字误读）**：测试 **196** 用例（`GetTestRegistry().Add` 求和，**20 个含用例的测试文件**——`src/Tests/*.cpp` 共 23 个，其中 `RunAllTests.cpp` / `TestFramework.cpp` / `test_main.cpp` 为基础设施无用例）｜Public 头 **89**（`include/ECDI/**/*.h`，另 `Core/version.h` 为 CMake 生成头不计）｜设计文档 **116** 篇（`docs/**/*.md` 递归，含 `docs/model-probe/` 2 篇；顶层 114 篇）。下表各阶段状态栏内的数字为**该阶段实现时点值**，非当前值。

### ✅ 已完成

| 阶段 | 内容 | 状态 |
|------|------|------|
| Phase 1 | 基础窗口系统（WindowClass/Window/Application） | ✅ |
| Phase 2 | 事件系统（Win32 翻译 → 类型安全 Event → Router；CharInputEvent 码点模型） | ✅ |
| Phase 3 | Widget 系统（树/HitTest/Layout/Focus/Paint） | ✅ |
| Phase 4 | Renderer 系统（4.1-4.7：命令管线 + GDIBackend 双缓冲） | ✅ 2026-08-11 |
| Phase 5.1 | 文本系统（Font/TextMeasurer/DrawTextCommand） | ✅ 2026-08-12 |
| Phase 5.2 | Label（第一个文本消费者） | ✅ 2026-08-13 |
| Phase 5.3 | Button 完整化（TextWidget 抽取 + 居中文本） | ✅ 2026-08-13 |
| Phase 5.4 | 交互基础设施（Invalidate/Capture/Focus 通知/Tab/按下态） | ✅ 2026-08-13 |
| Phase 5.5.1 | TextBox MVP（码点编辑/光标/点击定位/裁切） | ✅ 2026-08-13 |
| Phase 5.5.2 | TextBox Selection + 修饰键（拖选/Shift+方向键/KeyModifier/Shift+Tab 反向） | ✅ 2026-08-14 |
| Phase 5.6 | IME 候选窗跟随光标（系统 caret + ImmSetCompositionWindow 双通道；微软拼音实测 ptCurrentPos 按客户区解释） | ✅ 2026-08-15 |
| Phase 6.1 | HorizontalLayout（布局系统完善——VerticalLayout 水平镜像，diff 同构 + 10 条设计契约） | ✅ 2026-08-15 |
| Phase 6.2 | CheckBox/Radio（StateWidget 行为基类 + 真实勾/圆绘制 + 同父互斥 + CheckBoxStyle/RadioStyle 进 Theme） | ✅ 2026-08-25 |
| Phase 7.1.1 | PlatformWindow 骨架（Window 零 Win32：PlatformWindow/PlatformWindowHost 契约 + Win32PlatformWindow 实现；翻译器/IME 平台代码下沉） | ✅ 2026-08-15 |
| Phase 7.1.2 | 翻译器契约改造（翻译器迁 Platform/Win32/ + 构造 Host& 派发；WM_IME 移出方案 B——翻译器纯翻译；Platform 零 Application/零 Window.h） | ✅ 2026-08-15 |
| Phase 7.1.3 | 输入层抽象（CaretGeometry{ rect, visible } 文本插入点模型升级——光标不是点是矩形；kCaretWidth 同源；CreateCaret 尺寸来自 rect） | ✅ 2026-08-16 |
| Phase 7.1.4 | Backend 注入（决策 35 代价解决：Window 持 unique_ptr\<RenderingBackend\> + unique_ptr\<TextMeasurer\>；GDIBackend 拆类 + GDITextMeasurer；RenderServices bundle + 工厂；PlatformRenderContext 句柄注入；Window.h 零具体后端零 Windows.h） | ✅ 2026-08-16 |
| Phase 7.1.5 | Application 解耦（**7.1 平台抽象闭环**：WindowClass::Instance 下沉 + PlatformApplication 消息泵抽象；Application.h/cpp 零 Win32；框架抽象头零 Windows.h） | ✅ 2026-08-16 |
| Phase 7.2 | 测试体系补强（**双子目标**：轻量自研测试框架——零第三方依赖，TestCase/Registry/Runner/Assert/Summary + EXPECT 5 宏；清历史欠账——P0 Selection 键盘路径 S1-S8/S10 + P1 Event/7.1 回归 FakeHost + 自测 F1-F5） | ✅ 2026-08-24 |
| Phase 7.5 | 事件回调（std::function 回调注册 API——Button::SetOnClick / TextBox::SetOnTextChanged；继承 override 基座 + 回调业务便利层两套并存，RaiseXxx 分离模式） | ✅ 2026-08-19 |
| Phase 8 | 渲染增强（能力层：DrawLine / DrawRoundedRect / DrawImage / PushClip / PopClip / DrawFocusRect——GDI/msimg32，无 GDI+） | ✅ 2026-08-24 |
| Phase 8.5.1 | 文本系统 2.0 核心升级（IME 组合串内嵌模型 B + 剪贴板 Ctrl+A/C/V/X + 光标闪烁 Timer + SetFont；Update≠Commit 双通道；三连修复：双写/组合层/候选窗） | ✅ 2026-08-24 |
| Phase 8.5.2 | 多行与滚动（行缓存 + 垂直滚动 + 双击选词 + Up/Down 跨行 preferred column；文本区原点统一三路） | ✅ 2026-08-24 |
| Phase 8.5.3 | Undo/Redo（快照模式 + 编辑前 Push + Composition 一次撤销 + Cancel 恢复） | ✅ 2026-08-25 |
| Phase 8.6 | 渲染抗锯齿（圆角覆盖度**两层拆分**——覆盖度生成 ⇄ 形状装配 + `S=8` 超采样 + 按半径缓存掩码 + 预乘 alpha 合成；正式修订 9.5「约束 2」；公共 API 与全部控件**零改动**） | ✅ 2026-09-11 |
| Phase 9 | 主题系统（**决策层落地**：StyleField\<T\> D7 契约 + Theme/DefaultTheme + TextStyle 单一视觉真相 + Button/TextBox/Panel 迁移 + cornerRadius 消费） | ✅ 2026-08-25 |
| Phase 9.5 | 收尾补充（R1 Clip 管线 + TextBox 横向滚动 / R4 Hover 状态机；R2 LinearLayout、R3 WM_MOVE、R5 Shortcut 关闭记账） | ✅ 2026-08-28 |
| Phase 9.6 | 动画系统（per-Window AnimationManager + 插值/Easing 四种 + CollapsiblePanel 四向折叠 + ProgressBar + Button S1 色过渡） | ✅ 2026-08-30 |
| Phase 9.7 | 自适应布局（SetStretch 权重分配 + spacing + fillCrossAxis + OnResized→Arrange 触发链；契约 10 修订） | ✅ 2026-09-02 |
| Phase 9.8 | AutoSize（GetPreferredSize/AutoSize + ResolveMeasurer 接缝 + 尺寸意图三分「后调用者赢」+ §3.5 交互冻结） | ✅ 2026-09-02 |
| Phase 10 | 库化 0.1.0（Public API 边界 + 9 头下沉 `src/` + install/export `ECDI::ECDI` + Public Header 自包含测试 + `MinimalApp` 外部消费者验收 + ExactVersion） | ✅ 2026-09-06 |
| Phase 11 | 图片解码（WIC 后端 `Decode` 模块——`DecodeFile`/`DecodeMemory`，输出**预乘 BGRA**；Public 头 80→81） | ✅ 2026-09-07 |

### 🔄 当前

- **Phase 12 WindowChrome（✅ 实施完成，跨工具链已确认）**——需求 **v1.2 ✅** → 初步设计 **v1.3 ✅**（三轮评审 PASS）→ 详细设计 **v1.5 ✅ 已实施**（v1.1 外部评审 → v1.2 内部复核 → v1.3 AI 核验 → v1.4 实施期回写 → **v1.5 实施后缺陷修复**：测试替身绕过 `Create()` 致 `Application.cpp:92` 断言，MSVC 构建暴露）；**`ecdi_tests` 183/183 通过**——**含一次带 `-D_DEBUG` 的构建**（`FRAMEWORK_ASSERT` 真正生效；不带 `_DEBUG` 的构建下该断言层被编译为空操作）。**后续**：`Application`/`Window` 所有权契约不对称（public 构造器 + 「必在册」断言）→ **B 已实施**：独立契约文档 [window-ownership.md](window-ownership.md)（初设 v1.1）+ [详设 v1.2](window-ownership-detailed-design.md)（`ecdi_tests` 183/183，MinGW + `-D_DEBUG`）。 交付：4 新头（Public 头 **81→85**）+ 7 个 Window 公共 API + `WindowStateChangedEvent`；无边框窗口（保留 `WS_OVERLAPPEDWINDOW` + 拦截 4 个 NC 消息）+ 最大化 `rcWork` 校正 + DWM 集成。**待办剩余**：手测矩阵（A7）、R10 `Desktop` spike（A8）——**四工具链构建 + 断言运行已全部确认**（2026-09-12 用户实测）。

### 🔲 未来

- **Phase 12 后能力路线**：基础控件补齐 / 渲染能力增强 / 跨平台（Linux/Android 远期）→ 接近 1.0
- **Phase 13 CaptionBar 自绘标题栏（✅ 已实现并验收 2026-09-14 ~ 09-15）**——Phase 12 R5 推迟项解锁（ModelProbe Borderless 缺关闭按钮实测 + DesktopNest = 二次用例）；核心 = **NCHITTEST ↔ Widget 树委托**（含 **D9「可交互」判定**——HitTest 命中 ≠ 应阻止拖拽：标题 Label 命中仍须 `HTCAPTION`）+ CaptionBar Widget + 状态查询 API。详见下方 Phase13 段。
- **Phase 14 托盘与拖入接缝（✅ 已实施并验收，2026-09-17）**——`Shell_NotifyIcon` 图标回调 + `WM_DROPFILES`；**R9「惯例而非抽象」的首次真正消费**（原「前置 = R9 平台消息扩展接缝」表述已过期——R9 定稿为不新建注册接口的三步惯例），并**首次为「应用级平台能力」定形态**（托盘是应用级、拖入是窗口级——两条通道分层形态不同）；另含**隐性前置 R12/R13**（`Hide()` + 退出策略）。**需求 v1.1 ✅ + 初设 v1.1 ✅ + 详设 v1.1 ✅（三轮外部评审全部通过）→ 编码已落地（23 文件 / +913−5，未提交）**（逐文件最小 diff 规格 + 状态机 11 态完整转移表含失败分支 + v4 翻译终版 + T14-1..11 测试规格 + `shell32` 注册；**实施前核实重大更正：O-6 载体 `PlatformWindowHost::GetWindow()` 早已存在——初设 §2.10 作废、影响面 7→6 头 / 3→2 替身**）。**A1/A2 ✅（2026-09-17）**：四工具链 `ecdi_tests` **195/195**（188 基线 + 7 = `TrayTests` 4 + `DropFilesTests` 3）；**MinGW 断言层经 `CMakeLists.txt` 补 `_DEBUG` 后为真绿**（此前整体为死代码）；**A3–A7 手测待做**（A7 需单独授权改 `examples/ModelProbe/`）。详见下方 Phase14 段。同时支撑 DesktopNest 桌面常驻方向（`desktopnest-roadmap.md`）
- **Phase 9.5 收尾补充**：~~局部更新/裁剪系统 + Hover/MouseEnter/Leave~~（✅ R1/R4 已落地 2026-08-28）；~~LinearLayout 抽象、WM_MOVE 场景、Shortcut System~~（✅ 关闭记账——二次用例未出现）；详见 roadmap-deferred.md

### 📋 技术债务（记账）

> 完整延期排期见 **[roadmap-deferred.md](roadmap-deferred.md)**（全部延期项 → 阶段总表）。
> ⚠️ **「解决时机」列已重审（2026-09-12）**：Phase 10（0.1.0）已收口，原定「Phase 10 转库前」的条目**均未纳入该次收口**——统一顺延至 **v1.0 API 审查**。另：roadmap-deferred 的总表内容主体停留在 v1.1，9.6/9.7/9.8/10/11/12 的延期项尚未批量回填（见该表头部已知缺口声明）。

| 债务 | 位置 | 解决时机 |
|------|------|---------|
| Invalidate 解耦（两层结构 Internal+API） | TextBox 编辑操作 | 原定节点（Phase 7）已过——**待重审**（是否仍需解耦） |
| ~~文本裁切用字符串截断（O(n²)）~~ | TextBox::OnPaint | ✅ 已解决——Phase 9.5 R1 Clip 管线落地（PushClip/PopClip），替换逐行截断 |
| **输入层抽象（TextInputInterface/TextInputContext）**——5.6/7.1.3 的 UpdateTextInputCaret + CaretGeometry 是半抽象（Window 中介），完整契约层 + 跨平台 adapter 待转库前 | Window::UpdateTextInputCaret | v0.1.0 收口**未纳入** → 顺延 **v1.0 API 审查** |
| **DPI 感知**——框架当前无 DPI 缩放，IME 坐标用逻辑像素 | 全局 | v0.1.0 收口**未纳入** → 顺延 **v1.0 评估** |
| 键盘入口不对称（OnKeyDown 走 Window / OnKeyUp+CharInput 直派，3 入口）——已回顾保持现状（Tab 拦截必需 Window），未来全局快捷键时统一 | Application | 未来全局输入需求出现时（详见 phase5-architecture-review.md） |
| 多窗口焦点语义（应用级 vs 窗口级焦点） | Window/Application | v0.1.0 收口**未纳入** → 顺延 **v1.0 平台抽象审查**（详见 phase5-architecture-review.md） |
| 编辑操作可见性（临时 public） | TextBox | v0.1.0 收口**未纳入** → 顺延 **v1.0 API 审查** |
| **RenderingBackend::DrawText 命名与 Win32 宏冲突**（skill 13 历史遗留违反——现用防御性 undef 兜底，用户零负担） | RenderingBackend/RecordingBackend/GDIBackend | **v1.0 API 审查改名**（如 DrawTextContent）——Phase 10 未改名，已确认仍为防御性 `#undef` |
| **光标色未主题化**（TextBox 光标 Color::Black 硬编码——Phase 9 迁移时 YAGNI 未纳入） | TextBox::OnPaint | Phase 9+（需求出现时进 TextStyle/TextBoxStyle） |
| **IME 结果 WM_CHAR 吞字符 pending 计数**（若某 IME 结果不走 WM_CHAR 会残留吞后续字符——注释已记） | Win32PlatformWindow | 真实输入法兼容性需求出现时 |
| **Phase 8.6 抗锯齿遗留验收**——A7 性能基线 / A8 其余三工具链（MSVC/Clang/ClangCL）/ A9 目视 `S=8` vs `S=16` 对比 / A4 AA 关闭人工抽查（均已实现但未人工验收） | GDIBackend AA 路径 | 用户在 VS/CLion 执行（功能不阻塞） |
| **跨半径角补丁测试缺口**——`PatchSurface` stride 缺陷（同 backend 先大后小半径）未被既有用例捕获（L2 每例独立 backend + 单一半径） | AntiAliasingTests L2 | AA 后续补测（已两次提议，未落地） |
| **`~Application` 残留活窗口 → 潜在析构序隐患**——成员析构逆序使 `m_deferredDestroy` **先于** `m_windows` 销毁；若有活窗口，`OnWindowDestroyed` 会向已结束生命周期的容器写入。实测（独立探针 N=1/2/3/5 窗口 × O0/-O2）**无可观测异常** ⇒ 记「**潜在析构序隐患**」，**不得**写成"已确认 UB"（UB 判据 = 是否访问已结束生命周期的对象，非"跑几次没崩"） | `Application` 成员析构序（`Application.h:116/118/120/122`） | **独立记账（Deferred）**——见 `window-ownership.md` §7 |
| **`m_running` / `Run`-`Exit` 语义未定义**——初值 `true` ⇒ `Exit()` 在 `Run()` 之前也生效（真发 `PostQuitMessage`）；其语义实为「`Exit()` 未被调用过」 | `Application.h:122` | **独立记账**（属 Application 生命周期，已从 B 范围剥离）——见 `window-ownership.md` §7 |

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

## Phase12 WindowChrome（🚧 需求 v1.2 ✅ → 初设 v1.3 ✅ → 详设 v1.5 ✅ 已实施 → 跨工具链确认中）

| 文档 | 内容 | 状态 |
|------|------|------|
| [phase12-windowchrome-requirements.md](phase12-windowchrome-requirements.md) | 需求确认（无边框 NCCALCSIZE/HITTEST 拦截 / 保留 WS_OVERLAPPEDWINDOW / R9 能力式扩展点 / R10 WindowLayer Bottom+Desktop / 7 决策全拍板） | ✅ v1.2（外部评审通过——可进初设） |
| [phase12-windowchrome-preliminary-design.md](phase12-windowchrome-preliminary-design.md) | 初步设计（4 新头 81→85 / NC 消息归平台状态同步区非翻译器 / **配置期·运行期 API 对称生命周期**（判据 `m_shown`；运行期三方法 Show 前 Warning+忽略）/ 最大化 `rcWork` 唯一基准 + 补偿不变量 / R9「惯例非抽象」/ R10 spike 规格 / 决策 7 降级配置期） | ✅ v1.3（三轮外部评审——**PASS，可进详设**：v1.2 修 P0 补偿方向；v1.3 修 P1 运行期生命周期 + P1 T3 断言逻辑） |
| [phase12-windowchrome-detailed-design.md](phase12-windowchrome-detailed-design.md) | 详细设计（**9 开放决策点全收** + 平台实现全文 6 case + **10 方法**（7 override + 3 私有辅助）；`TestWindow::Handle()` 三跳取 HWND；dwmapi 双构建系统；测试 **9** 自动用例含 spike 全文） | ✅ **v1.5 已实施（2026-09-12）**：v1.1 外部评审 → v1.2 内部复核 → v1.3 AI 核验补正 → v1.4 **实施期回写 4 处缺口**（D-DWM-1 零兜底 / `NCCALCSIZE_PARAMS` / 2 个测试替身补 override / Handle() include）→ **v1.5 实施后缺陷修复**（`TestWindow` 改持非拥有 `Window*` + `Create()`——原直构窗口未登记，销毁时触发 `Application.cpp:92` 断言；**仅 MSVC 构建暴露**，因 `FRAMEWORK_ASSERT` 只在 `_DEBUG` 下存在）；`ecdi_tests` **183/183**（MinGW，含带 `-D_DEBUG` 的一次；MSVC/Clang/ClangCL 待用户确认） |
| [desktopnest-roadmap.md](desktopnest-roadmap.md) | DesktopNest 规划（跨框架/应用，不占 Phase 编号——阶段拆分与依赖链、置底 vs On Desktop 决策依据留档、框架侧 2 Phase） | 🚧 v1.6 待评审（R10 已出清 · 判据①–⑥全通过 · Phase 14 已立项） |

## Window 所有权与生命周期（✅ 初设 v1.1 → 详设 v1.2 **已实施**）

> **独立契约文档**——不属任何 Phase，故不用 `phaseN-*` 命名（阶段由文档头部 / §8 跟踪）。
> **来源**：Phase 12 WindowChrome 实施后 `Application.cpp:92` 断言（测试替身绕过 `Application::Create()`）；**A（测试替身止血）已关闭**，本节为 **B** 范围。

| 文档 | 内容 | 状态 |
|------|------|------|
| [window-ownership.md](window-ownership.md) | **契约主体（初步设计）**：三层归属（对象 / HWND / 注册表）· 三条闭环链（线性全链 + 源码锚点）· **5 条不变量** · 决策 B1–B5 + 关键陷阱（`make_unique` 与 `default_delete` 均无法访问私有成员）· **契约条款原文**（`Release ≠ delete Window`、回收依赖消息泵、断言与容错的双层含义、`PlatformWindowHost` 注释改写）· 验收（含 `static_assert` 编译期契约） | ✅ **v1.1 已实施（2026-09-12）**——契约落地，证据见详设 v1.2（183/183 + 编译期契约 + 静态检查） |
| [window-ownership-detailed-design.md](window-ownership-detailed-design.md) | **详细设计（实施规格）**：逐文件 diff 级改动（`Window.h` 访问权限布局前/后 + `friend` 粒度说明 / `Application.h` 删友元 + 双向访问关系复核表 / `Application.cpp` `Create` 全文 + B1-t 陷阱 / `PlatformWindowHost.h` 注释改写 / 测试 `static_assert` ×3）· 编译期契约测试方案（**双工具链实证** + 中立上下文性质 + 四工具链待验 + 兜底负向探针）· 注释落点清单 · 实施 6 步 · 验收 A1–A6 | ✅ **v1.2 已实施（2026-09-12）**：`ecdi_tests` **183 passed / 0 failed**（MinGW + `-D_DEBUG`，断言层生效）；**A1** 编译期契约（`static_assert` ×3 + 人工反例实测编译失败）/ **A4** 静态检查（`new Window` 代码 1 处、`make_unique<Window>` 代码 0 处）通过；**A2/A6 ✅ 全部通过**——**四工具链**（MinGW / MSVC / Clang / ClangCL）实测运行、**无断言错误**；附带 `ECDI/ECDI开发规范.md:143` 过期措辞（「m_application 指针」→引用）已修；3 处实测偏差（`static_assert` 落点 / 连带过期注释 / 排版）已记录 |

## Phase13 CaptionBar 自绘标题栏（✅ 已实现并验收，2026-09-14 ~ 09-15）

Phase 12 R5 推迟项解锁立项——Borderless 窗口的「看得见摸得着」标题栏（标题文本 + min/max/close 三按钮，矢量自绘）；核心缺口 = **NCHITTEST ↔ Widget 树委托**（R9 惯例第三次应用）。不动 B 契约 / 渲染四层 / NCCALCSIZE。**验收**：四构建 **188 passed / 0 failed**（183 既有零回归 + 5 新增）；ModelProbe `--borderless` 手测 6 项 + Normal 零回归通过。

| 文档 | 内容 | 状态 |
|------|------|------|
| [phase13-captionbar-requirements.md](phase13-captionbar-requirements.md) | 需求确认（R1–R8 + **D0–D9**：D1 独立 Widget / D2 Host 虚方法委托 / **D7 职责二分定案**（`captionHeight` 行为区 vs Bar 实体区 + 三层判定顺序）/ **D9「可交互」判定来源**（倾向 A 控件自声明，非纯虚 ⇒ 零破坏）/ D8 close 走关闭请求；非目标圈定 Snap Layouts 等） | ✅ **v1.1**（已实现并验收） |
| [phase13-captionbar-preliminary-design.md](phase13-captionbar-preliminary-design.md) | 初步设计（**头草案 6 处** + 命中委托链 + 三处初设新发现：`Window::RequestClose()` 入口缺失 / 第三个 Host 实现者 `FakeHost` / `ConsumesMouseInput` 坐标无关；**v1.1 评审 5 条确认**：`HitTest` 最深命中为前提 · `RequestClose` 同步派发 · T13-4 用 `RecordingBackend` 不加测试 API · 固定 `break → DefWindowProc → HTCLIENT` · §6 留详设） | ✅ **v1.1**（已实现并验收） |
| [phase13-captionbar-detailed-design.md](phase13-captionbar-detailed-design.md) | 详细设计（**逐文件最小 diff 规格**：2 新建 + 8 修改 + **3 处测试替身同步**；**初设→详设 6 处精化**：P1 `SetSize` 内重排 / P2 标题越界天然被自身 PushClip 裁切 / P3 标题前景色须构造注入 / P4 T13-4 命令缓冲直接断言零产品测试缝 / P5 命令路径走合成 Event + `Application::OnEvent` / P6 ModelProbe 须把 bar 加在 page 之前；**glyph 坐标表**；T13-1 **八态**含禁用落回拖拽；§4 **6 条已知局限**含 L6 坐标系；A1–A6 + R1–R6 + 最小回滚） | ✅ **v1.5 已实现并验收**：P0-1 替身返回类型 `void`→`WindowState` · P0-2 原 L4 移出局限 · P1-3 负坐标表述收紧 · P1-5 A6 实现者枚举化 · P1-2 L2 措辞精确化 · **P1-4 否决**（`m_closeButton` 被 `RelayoutChildren` 使用）· 补 `SetSize` 可重复调用契约 + T13-4 断言分层；**A1–A6 全通过** + `AntiAliasing.GDIRadiusZeroBitwise` flaky 根因（Window Ghosting）修复；**v1.5（2026-09-15）ModelProbe 默认形态翻转为自绘标题栏**（新增 `--native` 回退系统标题栏）——**A4 复跑命令随之变为 `modelprobe.exe --native`** |

## Phase14 托盘与拖入接缝（✅ 已实施并验收，2026-09-17）

让框架从「窗口框架」迈向「**桌面常驻应用框架**」——补上托盘图标（**应用级**）与文件拖入（**窗口级**）两条 shell 集成通道。本阶段真正的价值不是「多两个 API」，而是**第一次为「应用级平台能力」定形态**——R9 三步惯例此前只覆盖窗口级，而托盘是「一个应用一个图标位」的进程级语义。立项勘察得出四条**带出处**的事实，其中两条直接改形态：**message-only window 不接收广播消息**（MSDN + Raymond Chen ⇒ 承载窗口必须用普通隐藏顶层窗口，否掉"最干净"的方案）、**`TaskbarCreated` 只广播给顶层窗口**（MSDN ⇒ explorer 重建后托盘可**自愈**——与桌面层 A 路线被"**层级式**"杀死形成对照：**注册式可自愈、层级式不可**）。另发现两条**隐性前置**：当前「最后一个窗口关闭 ⇒ `Exit()`」（`Application.cpp:116`）且**无 `Window::Hide()`** ⇒ **「最小化到托盘」这一最基本的托盘用法当前走不通**（需求稿 R12/R13）。

| 文档 | 内容 | 状态 |
|------|------|------|
| [phase14-tray-and-drop-requirements.md](phase14-tray-and-drop-requirements.md) | 需求确认（**四条勘察事实 F1–F4**：message-only 不接收广播 / `TaskbarCreated` 广播语义 / UIPI 阻塞提权进程拖入 / 无 `Hide()` + 关窗即退出；**两条通道分层形态不同**的结构判断 + 三个硬骨头；**R1–R13 三组**——托盘 R1–R7（含**幽灵图标硬要求** + 与 `Window` 解耦）· 拖入 R8–R11（**`HDROP` 绝不出现在公共 API** + 走既有派发路径）· 常驻前置 R12–R13；**D0–D11 全部给倾向待拍板**（范围与顺序 / 挂载点 / 承载窗口 / 事件上行通道 / 图标来源 / 消息版本 / 菜单边界 / UIPI 立场 / 前置归属 / 自愈归属 / **菜单调用形态** / **状态机语义**）；非目标 9 项） | ✅ **v1.1 已实现**（外部评审「方向通过，可进初设」——6 项建议全部采纳含 1 项事实更正：R11 按实测链路重写 · UIPI 改平台约束验证 · R1 补生命周期归属 · D9 双态模型 · D3 sink 生命周期 · 新增 D10/D11；**R1–R13 已随 Phase 14 实施落地**） |
| [phase14-tray-and-drop-preliminary-design.md](phase14-tray-and-drop-preliminary-design.md) | 初步设计（**头全文草案 3 新增 + 6 修改**：`Application/TrayIcon.h` 86→87 · `EventSystem/Application/TrayEvent.h` 87→88 · `EventSystem/Window/DropFilesEvent.h` 88→89；`PlatformWindow` +`Hide`+拖入开关 · `PlatformApplication` +托盘三能力 +`std::function` sink · `Window` +2 透传 · `Application` +3 透传 +`SetQuitOnLastWindowClosed` · `EventType` +2 枚举 · `EventRouter` +2 虚方法。**§1.1 勘察基线 K1–K14 带行号证据**（`PlatformApplication` 零替身 / `PlatformWindow` 3 实现者 / `Run()` 的 `GetMessageW(nullptr)` 收全线程消息 / `WindowClass` 多实例 / HICON 先例 / `EventDispatcher` 不读 `GetWindow()`）；**§6 生命周期与销毁顺序**（懒创建宿主 HWND · sink 建立/清理时序 · `NIM_DELETE`→`DestroyIcon`→`DestroyWindow`→`UnregisterClassW` 四步不变量 · 双态正交表 · **`Hide`/`Release`/退出开关三角关系澄清**）；**+`shell32`（PUBLIC）**；§8 **O-1–O-8 开放决策点**（含 2 项待核实：v4 坐标打包 · `DropFilesEvent` 的 `Window*` 补填——本阶段唯一触碰既有事件链路处）；§9 D0–D11 兑现表） | ✅ **v1.1 已实现**（外部评审「通过，可进详设」——3 个必须收敛项全部处理：O-5 已核实附 MSDN 原文 + 新发现 WM_CONTEXTMENU 无坐标 ⇒ GetCursorPos 兜底 · O-6 拍板 C · 新增 §6.7 失败语义契约 · §8 收敛至唯一遗留 O-3；**§2.10（新增 `GetWindow()` 纯虚）未落地——载体在详设 §1.1 更正为既有 `PlatformWindowHost::GetWindow()`**） |
| [phase14-tray-and-drop-detailed-design.md](phase14-tray-and-drop-detailed-design.md) | 详细设计（**§1.1 实施前核实重大更正**：O-6 载体 = 既有 `PlatformWindowHost::GetWindow()`（`PlatformWindowHost.h:32-34`，7.1 时代即为「翻译器构造 Event」设计）——初设 §2.10 作废，`PlatformWindowHost.h` 零改动、影响面 7→6 头 / 3→2 替身；**§2 逐文件最小 diff**：3 新头（86→89） + 6 头修改 + `Win32PlatformWindow` / `Win32PlatformApplication` / `Application.cpp` 实现规格 + 替身 2 处 + **初设漏记补入**（`Widget.h` +`OnDropFiles` 虚方法——bubbling 调用点）；**§3.1 状态机 11 态完整转移表**（含失败分支：ADD 失败不得置 registered=true / MODIFY 失败两态不动 / DELETE 失败向已移除收敛）；§3.3 v4 翻译终版（含 ContextMenu 兜底）；§3.4 析构四步不变量 + `NotifyShell`/`DoDragFinish` 测试缝（函数指针形态保 `final`）；§3.5 `Application::OnDropFiles` 全文（同鼠标族 bubbling）；§3.6 R13 接线；§4 +`shell32`；§5 **T14-1..11** 测试规格；§8 验收 A1–A7（A5 = O-5 坐标实测收尾）） | 🚧 **v1.1 已实施，A 项验证中**（外部评审「通过，可进实现」——4 项收口：补 m_trayIconNeedsDestroy 成员 · 三个托盘方法实现骨架补齐 · 测试 seam 拍板函数指针 · HICON「成功才提交」契约。**实施结果（2026-09-17）**：23 文件 / +913−5 落地；**A1/A2 ✅** 四工具链 195/195（用例 = 188 + 7；**T14-9 实施期降级归手测 A4** ⇒ 原估 199 调整为 195）；**MinGW 断言层经 `CMakeLists.txt` 补 `_DEBUG` 修正为真绿**；**A3–A7 待手测**） |

## ModelProbe Demo（✅ P1/P2 已实现，2026-09-01/11）

> 第一个真实消费者（Phase 10 起从框架移出至 `examples/ModelProbe/`）。**文档独立子目录 `docs/model-probe/`**，与框架 `phaseN-*` 区隔；2026-09-11 从仓库根 `model-probe-docs/` 移入 docs/ 体系（统一文档入口）。

| 文档 | 内容 | 状态 |
|------|------|------|
| [modelprobe-p1-preliminary-design.md](model-probe/modelprobe-p1-preliminary-design.md) | P1 初步设计（6 项框架能力：ChildProcess / GetExecutableDirectory / TextBox echo·只读·形态 / Button hover / Panel 形态 + demo 组装 ModelProbePage；9 开放点全部收敛） | ✅ v1.4（P1 已实现） |
| [modelprobe-p1-detailed-design.md](model-probe/modelprobe-p1-detailed-design.md) | P1 详细设计（ChildProcess 句柄继承矩阵 + 描边环几何冻结 + hover 三态 + 22 条测试用例 + **§10 P2 演进补记 7 项**） | ✅ v1.2（P1 已实现） |

- **代码**：`examples/ModelProbe/`（ECDI GUI 工具 + `probe.exe` Go 后端 RCDATA 资源嵌入 + `app.ico`）
- ⚠️ **P2 演进无独立设计文档**（未走五阶段法）——资源嵌入 / `SetSingleLine` / 二次查询修复等 7 项补记于详设 §10，**不可当作设计依据**

## 文档约定

- 命名：`phaseX.Y-<module>-<type>.md`（子阶段编号 + 模块名 + 阶段类型；2026-08-25 全量规范化：Phase 5/6/7 按内容编号对齐，如 `phase5.3-button-requirements.md`、`phase6.2-checkboxradio-detailed-design.md`、`phase7.5-callback-requirements.md`；阶段级评审文档保留 `phaseN-<module>.md`）
- 子目录：demo / 非框架文档放独立子目录（如 `docs/model-probe/`），与框架 `phaseN-*` 区隔但统一在 `docs/` 入口下
- **非阶段文档**（跨阶段的长期契约 / 规划类，如 `window-ownership.md`、`desktopnest-roadmap.md`）：**不用** `phaseN-*` 前缀——阶段由文档头部与 §修订记录跟踪，命名取 `<主题>.md`；在「开发进度」之外单列章节索引
- 五阶段法：职责确认 → 初步设计 → 详细设计 → 实现 → 测试，设计文档在实现前评审通过
- 文档内附修订记录（v1.0 → v1.1...），实现中发现的与文档出入必须回写
- 所有文档带 UTF-8 BOM（`ef bb bf`——MSVC 源码同规范）
