# ECDI 设计文档索引

> 本文档是 `docs/` 的索引。设计文档随代码提交 git，从 Phase4 起为强制约定（职责确认 / 初步设计 / 详细设计 各阶段文档正常写入本目录）。

## 项目定位（ECDI = Everyone Can Do It）

**教学型框架**——目标是人人都能用、**学习成本尽可能低**（`ECDI/ECDI开发规范.md:1` 的命名本意）。这不是口号，而是**评审标尺**：任何设计分歧（选 A 还是选 B、要不要加 API、怎么命名）先回到这里判，再谈实现代价。

| 标尺 | 具体含义 |
|---|---|
| **命名即语义** | 公共 API 的名字直接说清用途；**一个概念只用一个词**——出现同义混用（如 `padding` / `inset` / `spacing` 交叉）即缺陷 |
| **不引入投机抽象** | 抽象在**第二个真实消费者**出现时才加；「暂时不需要」一律写进 [roadmap-deferred.md](roadmap-deferred.md)，而不是先写进代码 |
| **过程即教材** | 每个阶段交付 需求 → 初步设计 → 详细设计 三份文档（本目录）——**理由**与结论同等重要，评审时"为什么"比"是什么"更该写清 |
| **示例优先于描述** | 能跑的示例胜过一段文字：`examples/MinimalApp` 约 20 行跑起来；`examples/ModelProbe` 是真实工具 |
| **不强迫既有使用者改代码** | 新能力的默认值一律「与现状**逐位等价**」（近年各 Phase 的硬契约）⇒ 加能力 ≠ 改已有调用点 |

---

## 开发进度（2026-09-22 更新）

> **当前规模锚点（防止各处历史数字误读）**：测试 **236** 用例（`GetTestRegistry().Add` 求和，**23 个含用例的测试文件**——`src/Tests/*.cpp` 共 26 个，其中 `RunAllTests.cpp` / `TestFramework.cpp` / `test_main.cpp` 为基础设施无用例）｜Public 头 **92**（`include/ECDI/**/*.h`，另 `Core/version.h` 为 CMake 生成头不计）｜设计文档 **134** 篇（`docs/**/*.md` 递归，含 `docs/model-probe/` 2 篇；顶层 **132** 篇）。下表各阶段状态栏内的数字为**该阶段实现时点值**，非当前值。

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
| Phase 12 | WindowChrome（无边框 NC 拦截 + 最大化 `rcWork` 校正 + DWM 集成 + R10 `WindowLayer`） | ✅ 2026-09-15 |
| Phase 13 | CaptionBar 自绘标题栏（`NCHITTEST` ↔ Widget 树委托 + 窗口状态查询 API） | ✅ 2026-09-15 |
| Phase 14 | 托盘与拖入接缝（应用级 `PlatformApplication` + 隐藏宿主窗口 + `WM_DROPFILES` + `Hide()` / 退出策略） | ✅ 2026-09-17 |
| Phase 15 | 滚动容器（`ScrollView` + 滚动条 + `ScrollContent` 偏移层 + `ClipsChildren` 命中门控） | ✅ 2026-09-18 |

### 🔄 当前

- **Phase 16 桌面驻留层（`WindowLayer::Desktop`）——需求确认 v1.4 ✅ · 初步设计 v1.3 ✅ · 详细设计 v1.10 ✅ · 已实施（批 A/B/C + A5 跟随修复 + T16-8）——用例 218 全绿（2026-09-20 实测），待验收 A5 手测五判据 + A7 收尾**：把 2026-09-15 spike 已取证的 E 路线（**紧贴桌面窗口正上方 + 前台事件钩子**）真正实现进 `Win32PlatformWindow`；**公共 API 净增 0**——本项目首个纯实现层 Phase。事实勘察用探针跑 **4 形态 × 3 轮 + 用户目视**，把真因钉到**单个样式位 `WS_MINIMIZEBOX`**（「显示桌面」只最小化可最小化窗口）⇒ **D1 取 A′：`WS_OVERLAPPEDWINDOW & ~WS_MINIMIZEBOX`**；另钉死不加 `WS_EX_NOACTIVATE` · 不加 tick 心跳 · 不缓存 `Progman` 句柄。初设把实现收敛为三处：**按档位分流的 `TargetInsertAfter`** · **`hook → 实例` 反查** · **重插前判在位**；并**勘误了需求稿 K13**（Phase 12 的 R10 实为零测试覆盖 ⇒ 本阶段补 T16-1）。详见下方 Phase16 段。

- **Phase 17 布局内边距（Layout padding）—— 需求确认 **v1.2** ✅ · 初步设计 **v1.1** ✅ · 详细设计 **v1.2** ✅ —— **已实现（A1–A6 实测通过 · 用例 226 全绿 · 断言特征串 10→11）****：`roadmap-deferred.md` §7.6 **#38** 的立项落地（2026-09-20 用户拍板**方案 B（框架侧）**）。**动机**：内容四边贴死客户区、鼠标拖选极易越出 GUI 边界。⚠️ **归因澄清**：贴边**不是 AutoSize 的缺陷**（AutoSize 的定义即「窗口尺寸跟随内容」⇒ 内容从 `(0,0)` 铺满是其必然结果），**修复落点在布局层**——**详设 L1 进一步记明**：当前 `AutoSize()` 仅用于叶子标签（`ModelProbe.cpp:718`）⇒ 本场景**无需改 AutoSize**。**路线**：`VerticalLayout` / `HorizontalLayout` 各加 `int padding = 0` 构造形参 ⇒ **root 一级配置即全局留白**；**详设**给出 `△1–△8` 八个行级改动点 · 契约 **C1–C6** · 测试 **T17-1..T17-8**（+85 断言）· 验收 **A1–A7**；**公共头 92→92 · 用例 218→226 · 断言特征串 10→11**；调用点 **41 处零改动**（实测构成：ModelProbe 9 · `examples/VisualTest` 1 · `src/Demo` 10（不参与构建）· 测试 20 · README 示例 1）。详见下方 Phase17 段。
- **Phase 18 窗口/根背景能力 —— 需求 v1.1 ✅ · 初设 v1.1 ✅ · 详设 v1.1 ✅（第二轮评审「🟢 通过，可进入实现」——见 §3.1 盯防清单 9 条红线）· ✅ **已实现并收口**（批 A/B · 四工具链 `ecdi_tests` **231 全绿** · **A1–A7 全过**）**：`roadmap-deferred.md` **§7.7 #39** 的立项落地（2026-09-21，**由 Phase 17 A6 实施实测派生**）。**动机**：Phase 17 的 **R3「root 一级即全局留白」在视觉上不可用**——root 无背景 ⇒ 让出的四边露出 Backend 清屏白（`#0f1115` 深色窗口上一圈白框），且标题栏被连带内缩。**缺口实测（K1–K4）**：客户区底色是 `GDIBackend.cpp:261` 的**硬编码 `WHITE_BRUSH`**，**无任何可配置入口**（无 `Window` API、无 `RenderingBackend` 接口位、root 亦是裸 `Widget`）。**路线已定**：**A 窗口级配置 + Backend 消费**——颜色存 `Window`（**唯一默认值来源**）· 每帧经 `Renderer::BeginFrame(const Color&)` 传入 · **Backend 不持状态、不认识 Window** · alpha 被忽略 · 清屏沿用决策 24。**★ 详设定出两处初设未覆盖的定案**：**N1 打通 `RenderServices` 注入通路**（`Application::Create` 追加带默认值尾形参——**Phase 7 已登记的延迟设计**，第一个真实消费者 = 端到端背景色测试）· **N2** 测试走 `Show()` + `PumpMessages`。**★ A6 视觉实测结论（2026-09-22）**：留白回到 root 一级后，**底色不再是白的**（能力达标），但**标题栏被连带内缩 + 异色描边** ⇒ **留白最终落回 page 一级**，并由此**派生 Phase 18.1**（见下）。★ **后续核查（2026-09-22）**：该形态还与**平台拖动区脱钩**——`Win32PlatformWindow.cpp:417` 的 caption 判据是 `y < captionHeight` 的**全宽带**、不看 `CaptionBar` 几何（环上那块"背景"**能拖窗口** · 标题栏**下缘 12px 拖不动**）⇒ **「chrome 贴边」升级为平台契约**。**公共 API +2**（★ 首个净增 API 的 Phase）· 用例 **226 → 231**。详见下方 Phase18 段。
- **Phase 18.1 子节点内缩（Child inset）—— ⏸️ 已搁置（2026-09-22 立项当日结论）**：**由 Phase 18 A6 目视实测派生**（用户原话：「底色和原有底色不同，标题栏能不能做到不内缩」）。**缺口 = 留白的归属**：`Layout::padding` 是**布局级单一 int、对全部子一视同仁**（`VerticalLayout.cpp:38/42/45/63`），而 `CaptionBar` 与 `page` **同属 root 布局的子** ⇒ root 级留白**必然连带内缩标题栏**；**Phase 18 只解掉了「露出来的是不是白的」**（清屏改用本帧底色），未解「标题栏跟着走」。**★ 定形态事实（F-1）**：「让出的窟窿开在谁身上」决定看不看得见窗口底色——root 是裸 `Widget` 不绘制 ⇒ 开在 root **露窗口底色**；page 自绘背景 ⇒ 开在 page **看不见底色**。**★ 决定"可不开"的事实**：`Panel` 默认**背景透明**且官方语义即「**隐形布局容器**」（`DefaultTheme.cpp:44-47` / `Panel.cpp:78`）⇒ **应用侧包一层透明 `Panel` 承接 padding = 零框架改动达成同一视觉**（Phase 17 判该路线代价过高所依据的「需 override `SetSize` 同步几何」**已因 9.7 + Phase 15 而消失**）。**四案并列**：**A** 子节点 inset · **B** 布局豁免 · **C** 应用侧零改动 · **D** 不做。**★ 结论：走 C（已落 demo 侧，框架零改动）⇒ 本阶段搁置。** 理由：① **要求分三层**——chrome 贴边 / 内容留白是**基本要求**（前者更是**平台契约**），**露窗口底色是可选效果**；前两条在 `af1f080` 已成，C 的增量只有第三条。② 路线 **A 与 C 等价**（同样只能达成第三条），却多一个近义词（`padding` vs `inset`），与「一个概念只用一个词」冲突。**重启条件 = `roadmap-deferred.md` §7.8 #40 的 R-1..R-4**（届时优先 B/E，而非 A）。详见下方 Phase18.1 段。
- **Phase 19 鼠标事件维度补全（Mouse event dimensions）—— 需求 v1.1 ✅ · 初步设计 v1.1 ✅ · 详细设计 v1.1 ✅（2026-09-23 三份均评审通过）→ ✅ **已实现并收口**（四工具链 **236** 全绿）**：**框架缺陷修复队列第 ① 位**（审计 `framework-defect-audit.md` §4 **D-1** → `roadmap-deferred.md` **§7.9 #41**）。**定性 = 契约错误（丢信息）**：`MouseEvent` 基类只带坐标（`MouseEvent.h:41-44`），而 `WM_MOUSEMOVE` 的 `wParam`（`MK_LBUTTON` 等**按键位** + `MK_SHIFT`/`MK_CONTROL`）**被翻译器整个丢弃**（`WindowMessageHandler.cpp:92-106` 只取坐标）⇒ 任何"按住拖动"都得自己维护布尔，**代码里已有两处**（`ScrollBar::m_dragging` · `TextBox::m_mouseDown`）。**形态由既有原则锁定**：Event 层「原始值不归一化」——`MouseWheelEvent.h:11` 已落款 ⇒ **照抄平台位、不做语义结论**；修饰键**复用键盘侧既有的 `KeyModifier`**（`KeyBoard/KeyEvent.h:23/29/31/33`）。★ **顺带解锁 #36**（横向滚轮卡在"无修饰键 / 轴"）。**R1–R7 · D0–D6 · N1–N6 · 验收 A1–A5 · 留给初设 Q1–Q6**。★ **初设已定案**（**Q3** 沿用 `isDoubleClick` 尾默认参先例 · **Q4** 走 `FakeHost` **真翻译路径** · **D2** 私有掩码 + 谓词 · **D3** 修饰键源 = `wParam`、**Alt 有意缺位**）。详见下方 Phase19 段。
- **Phase 20 DPI 感知（DPI awareness）—— 需求 v1.1 ✅ · 初设 v1.1 ✅ · 详设 **v1.3** ✅ · **五批实现完毕 + 崩溃修复 + ★★ 全阶段收口（验收通过）**（2026-09-24 立项）**：**框架缺陷修复队列第 ② 位**（审计 `framework-defect-audit.md` **§4 D-2** → `roadmap-deferred.md` **§7.9 #8**）。★ **不是引入新概念，而是让既有契约生效**——框架早在 **Phase 13** 就已声明「**公共 API 语义恒为 DIP**」（`Win32PlatformWindow.cpp:376`），并留下 **L6**（`WM_NCHITTEST` 物理像素 vs widget DIP 几何）明写「与 DPI 感知一并闭合」；但**进程未声明 DPI 感知**（全库 **0 命中**）⇒ 系统虚拟化下 `dpi` 恒 96 ⇒ `DipToPixels` 恒等 ⇒ **契约恰好与事实重合而从未被检验**。★ **倾向路线 B**（Per-Monitor V2 + DIP 契约贯彻全部平台边界）。★★ **关键连锁**：**感知声明与 DIP 语义贯通必须同批**（单独声明会让 150% 屏上窗口/字体**缩小 1/3**）。**K1–K12 · G1–G5 · N1–N5 · D0–D8 · R1–R9 · A1–A6 · Q1–Q6**。详见下方 Phase20 段。
- **Phase 20.1 渲染层 DPI 缩放（render DPI scaling）—— 需求 **v1.3 ✅ 已通过** · ✅ 初步设计 **v1.2 已通过** · ✅ 详细设计 **v1.3 已通过 + 已实施 + 验收通过**（2026-09-24 立项）**：★ **Phase 20 的缺口补完**（来源 = Phase 20 详设 **§14.5 的 `RG-1`**）——**不是新能力**，而是让 Phase 20 已声明的分层（**公共 API / 框架内部 = DIP** · **渲染后端 = 物理**）**在非 96 DPI 下真正成立**。★ **根因 = 初设 §2.1 两条规则打架**：**规则 2**「`Renderer` / `RenderingBackend` 里见到的**恒为物理像素**」vs **规则 3**「两者**只在平台边界那一条线上互换**」——而 `Renderer` / `Backend` **不在平台边界** ⇒ ★★ **「框架内部(DIP) → 渲染层(物理)」这条转换没有任何落点**。**实测证据**：`Renderer.cpp`（69 行）· `PaintContext.cpp`（84 行）**零 `Dpi`/`scale` 代码**；`GDIBackend` 直接 `static_cast<LONG>(rect.x)` 当像素；全库 `src/Render/` 的 DPI 代码**只在字体路径**（Phase 20 批五）⇒ ★ **字体已按 DPI 换算而几何未换算 ⇒ 尺度失调**。**现象**：主屏 125% / 跨屏 150% 下**窗口尺寸正确、布局与命中正确，只有绘制缩小约 1/3**（内容只占窗口左上 2/3）——用户原话「绘制变小了，但 GUI 并没有变小、按键控制仍然在原位」。**K1–K4 · G1–G4 · N1–N5 · 约束 C-1..C-5 · §4 折算量清单（8 类命令逐条）· Q1–Q6 · A1–A5**。★ **关键约束（写进需求）**：**契约 C2** 禁止渲染层调用 `DipToPixels` / `PixelsToDip`；**渲染几何是 `float`** ⇒ **不适用 C10 的整数对称舍入**（C10 只服务平台层 `int` 几何）。★ **倾向落点 = `Renderer::Execute` 入口**（最贴规则 2「Renderer 里见到的恒为物理」+ 可用既有 `RecordingBackend` **完全无头测**）。★ **对 Phase 20 的影响**：**A5（125%/150% 视觉）/ A6（跨屏）验收依赖本子阶段** ⇒ **✅ 已收口（2026-09-24）**：Phase 20 的 **A5 / A6 已随之判为通过**——用户四工具链 ModelProbe 目视：**100% 与改前一致 · 125% / 150% 内容充满窗口 · 跨屏完全正常**。详见下方 Phase20.1 段。
- **Phase 21 系统图标 → Image（icon to image）—— 需求确认 **v1.1 ✅ 已通过** · 初步设计 **v1.2 ✅ 已通过** · 详细设计 **v1.0 🚧 待评审**（2026-09-25 立项）**：**框架缺陷修复队列待做 ③**（审计 `framework-defect-audit.md` **§4 D-3** = `desktopnest-roadmap.md` **§5 G-2**，该表标记为 **desktop 唯一阻断项**）。★★ **精确边界（K1 + K2 勘明）**：**不是「框架不能显示图标」，而是「框架拿不到系统图标的像素」**——`.ico` **文件**走既有 `Decode::DecodeFile` 已能出图（WIC 原生支持 ICO，`Decode/ImageDecoder.h:12-14`），缺的是**运行时由系统给出的 `HICON` 句柄**（全库 `GetIconInfo` / `GetDIBits` / `SHGetFileInfo` **零命中**）。★ **定位 = `Decode` 的第三个来源**（既有两个 = 内存 / 文件）⇒ **产出既有的 `Image`**，**零新契约 · 零新接缝 · 渲染侧零改动**。★ **核心决策 D0** = `SHGetFileInfo` 放框架还是应用层（倾向**放框架**）。★ **难点 Q3 = 可测性**（shell 依赖 ⇒ 内核 / 外壳须分离）。**K1–K6 · R1–R5 · D0–D5 · N1–N5 · Q1–Q4 · A1–A5**。★ 初设 / 详设各带 **独立探针实测**（**P1–P13**，两轮）⇒ **§2.7 的不透明度判据被实测修正过**。详见下方 Phase21 段。

### 🔲 未来

- **Phase 12 后能力路线**：基础控件补齐 / 渲染能力增强 / 跨平台（Linux/Android 远期）→ 接近 1.0

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
| [desktopnest-roadmap.md](desktopnest-roadmap.md) | DesktopNest 规划（跨框架/应用，不占 Phase 编号——阶段拆分与依赖链、置底 vs On Desktop 决策依据留档、框架侧 2 Phase、**残差登记 G-1~G-4**） | ✅ **v1.7 已回写**（框架侧 2 Phase 全部落地 · R-1/R-6/R10 已出清 · 判据①–⑥全通过 · **残差 G-1~G-4 已登记**——G-1 `Desktop` 档未实现为唯一阻断项；应用侧待需求确认） |

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
## Phase15 滚动容器（`ScrollView` + 滚动条）（✅ 已实现并验收 2026-09-18）

立项来自**首个真实消费者**——ModelProbe 的模型列表手搓了 35 行滚动容器。核心发现是**两个命中缺口**：`HitTest` 不认内容偏移、且不把子节点约束在父边界内（波及 hover / 按下 / 移动 / 滚轮 / 拖入 **5 条路径**）；另有 `Panel::ContainsPoint` 恒 false（**不能当边界门**）与 `VerticalLayout::Arrange` 每次重写子位置（「移动子控件」方案与布局系统**天然冲突**）。**R1 内核 + R2 滚动条同 Phase 交付**（用户拍板）。坐标语义在此立为**全框架级不变量**：`Paint` 减偏移 / `HitTest` 加回 / `GetAbsolutePosition` 减父偏移（三处同变换 ⇒ 默认 0 时逐位退化为现状）。

| 文档 | 内容 | 状态 |
|------|------|------|
| [phase15-scrollview-requirements.md](phase15-scrollview-requirements.md) | 需求确认（**K1–K18 现状勘察（全部带行号）**：`Paint` 已有偏移累加线程 + 每控件自推 `PushClip` ⇒ **滚动切割零新渲染需求**；**★ K3/K4 两个命中缺口**——`HitTest` 不认内容偏移、且不把子节点约束在父边界内（波及 hover/按下/移动/滚轮/拖入 **5 条路径**）；`Panel::ContainsPoint` 恒 false ⇒ **不能当边界门**；`VerticalLayout::Arrange` 每次重写子位置 ⇒ 「移动子控件」方案与布局系统**天然冲突**；`ModelListPanel` 手搓 35 行 = **首个真实消费者**；**K15/K16：`WM_MOUSEHWHEEL` 全库零处理 + `MouseEvent` 无修饰键 ⇒ 横向无滚轮入口**。**§1.4 设计不变量**（坐标语义三分 · 三处同变换 · **职责四层**）**R1–R5**：R1 内核（视口偏移接缝）/ R2 滚动条（**与 R1 同 Phase 交付**——用户拍板）/ R3 命中可见性约束 / R4 ModelProbe 接入 / R5 测试承载；**D1–D12 全部给倾向待拍板**（接缝形态 / 命中约束层级 / `GetAbsolutePosition` 是否认偏移 / 自身可命中 / 滚轮步长 / extent 来源 / 嵌套停递 / 横向同批 / 绑定关系 / 滚动条视觉 / clamp 归属 / 是否通改 K4）；非目标 12 项） ；**v1.1（2026-09-18）外部评审处置**：D3 升级「必须认偏移」· D2 `CaptionBar` 核实（opt-in 不误伤 + 语义定案「点检查」）· **D7 降级为记账** · D5 拆「框架默认 step ≠ 消费者行高」· D6 extent 钉二维包围盒 · D8 钉横向入口） | ✅ **v1.2 已通过**（二轮外部评审「可进初设」2026-09-18；**D1–D12 全部拍板**） |
| [phase15-scrollview-preliminary-design.md](phase15-scrollview-preliminary-design.md) | 初步设计（**§2 头全文草案 7 处**：`Widget.h` 两接缝增量——`GetContentOffsetX/Y` + `ClipsChildren()`，**均为非纯虚 ⇒ 零破坏**（无实现者需同步）· 新建 `ScrollView.h` / `ScrollBar.h` / `ScrollBarStyle.h`（Public 头 **89→92**）· `Theme.h` +1 纯虚（**实现者实测仅 `DefaultTheme` 一处、无测试替身**）· `DefaultTheme` 增量 · **构建零改动**（`file(GLOB_RECURSE ... CONFIGURE_DEPENDS)` 自动入库）。**§3 实现分解**：**坐标变换三处完全展开 + 数值自检**（`Paint` **减**偏移 / `HitTest` **加**回偏移 / `GetAbsolutePosition` **减**父偏移——符号相反但同源；**默认偏移 0 时三处逐位退化为现状** = 零回归的结构保证）· `ClipsChildren()` 判定序（**递归之前** / 独立矩形判定 / 禁用 `ContainsPoint`）· `ScrollView` 树结构（内容容器方案——内容与装饰分离）· `ScrollBar` 范围模型与拖拽反推（`maxOffset = max(0, content − viewport)`）· **双轴 viewport 定义**（占位 + 单轮判定 + 边界误差记账）· `extent` 更新时机（唯一入口 + 原子副作用链：extent→maxOffset→**clamp 现有 offset**→同步条→Invalidate）· R4 `ModelListPanel` 迁移映射；**§4 契约四组** · **§5 影响面 10 项 grep 实证** · §6 待定项 7 · §7 测试方向 7 组 · **§8 开放决策点 O1–O8**。**v1.1（2026-09-18）外部评审处置**：❗A **`ScrollBar` 偏移误伤**（评审指出 + **AI 数值复现确认**——v1.0 把偏移 override 在 `ScrollView` 自身 ⇒ 滚动条作为直接子被一起偏移，**视觉 / HitTest / `GetAbsolutePosition` 三处同错**）⇒ 偏移消费者**下沉**到内部 `ScrollContent`（**内容坐标空间的根**，`src/` 内部头不进 Public 头计数），`ScrollView` 自身偏移恒 0；并定 **`GetScrollOffsetX/Y()`** 命名（避虚接缝同名 ⇒ **name hiding**）· ⚠️B 双轴 viewport **单轮 → 两轮**（评审建议采纳；**但评审算法实测 2/6 反例、AI 修正版 0/6**，附严格不动点判据）。**响应外部评审的 7 个初设关注点**（其中 2 点已就地取证：`HitTest` **逆序** ⇒ 滚动条最后 `AddChild` 即天然优先命中 · `IsClientInteractiveAt` 走 `HitTest` ⇒ `ConsumesMouseInput` 保持 false 零破坏**且与 D2 耦合**）） | ✅ **v1.2 已通过**（三轮外部评审「可进详设」2026-09-18；**§9.1 记录一处判断澄清**——评审基线为 v1.0，其「content 容器已解决 ScrollBar 误伤」结论不成立，**v1.1 的偏移下沉才是真修复**） |
| [phase15-scrollview-detailed-design.md](phase15-scrollview-detailed-design.md) | 详细设计（**§1 实施总览 15 文件 → 实测 18**（Public 头 **89→92**）· **§2 逐文件最小 diff 规格**：`Widget.h` 三落点 A/B/C · `Widget.cpp` **4 处最小 diff**（`Paint` 减偏移 / `HitTest` 加偏移 + **裁剪门控** / `GetAbsolutePosition` 减父偏移 / `ContainsPoint` 抽出**非虚** `ContainsRect`）· `ScrollContent` 内部头 + `.cpp`（转发所属 ScrollView 的偏移）· `ScrollView`/`ScrollBar` 新建实现（构造树序 = 内容→两条 ⇒ 逆序 HitTest 天然条优先 / `SetSize` 原子重算 / `UpdateContentExtent` 原子链 / 拖拽反推 / `long long` 防溢出）· `ScrollBarStyle` · `Theme`+`DefaultTheme` · **R4 迁移**（删 35 行手搓 + 外层 Panel 承接样式）· **构建零改动**；**§3 常量与算法冻结**（含**详设补钉**：`ContainsPoint` 判据用 `m_geometry.width`（float）**非** `GetWidth()` ⇒ 抽非虚辅助共用，防门控与命中语义漂移）· **§4 契约 C1–C14** · **§5 测试 T15-1..14**（196→**210**）· **§6 验收 A1–A7**（A4 = 偏移层隔离，防 override 被搬回 ScrollView）· §7 影响面与回归清单（含 `HitTest` 测试直调 4 处）· §8 局限 **L1–L8**） **v1.1（2026-09-18）实现前两条修正**（v1.0 外部评审「通过，可进入实现」）：① **`SetSize`/`SetContentExtent` 顺序统一**为 `ApplyLayout → ClampOffset → SyncBars`（原两条路径顺序不一致——`ApplyLayout` 定条可见性 ⇒ 定 viewport ⇒ 定 `maxOffset`，`ClampOffset` 须在其后；典型失效：内容变短 + 横条消失 ⇒ offset 仍越界）· ② **`ModelProbe.h` 补入清单**（`m_list` 实测为 `Panel*` 非 `ModelListPanel*` ⇒ 保持指向外层 `Panel`，**新增 `ScrollView* m_scroll`**）⇒ 文件数 **14 → 15**，并补**迁移点行号表**（L51-85/371/380/789/798）。 **v1.2（2026-09-18）实现期回写**（**不改设计决策**）：§2.7 补 `GetThickness()` · **§2.8.1 新增「事件坐标系纪律」**（真缺陷 + 两层测试盲区）· §2.11 extent 改用显式 `SetContentExtent` · §2.5 `SetSize` 由 `protected` 改 **`public`** · §2.12 补记 `RunAllTests` 手工接线与 **`ECDI.vcxproj` 显式登记** ⇒ §1 由 15 → **18 文件** · §5/§6 补实测（**210** ✓ / **A2 四链 10/10**）· §8 新增 **L8**。 | ✅ **v1.2 已实现并验收**（2026-09-18） |

## Phase16 桌面驻留层（`WindowLayer::Desktop`）（✅ 需求确认 v1.4 · ✅ 初步设计 v1.3 · ✅ 详细设计 **v1.11** · ✅ **已实现并关闭（2026-09-21）——`ecdi_tests` 218 全绿 · A1–A8 已验收 · `desktopnest` G-1 出清**）

`desktopnest-roadmap.md` v1.7 §5 登记的 **G-1**——**全项目唯一「已取证但未落地」的能力**。Phase 12 立 `WindowLayer::Desktop` 时只定了语义（`D-DESK-1`：语义状态 ≠ 实现路径），实现按 `Bottom` 降级执行；2026-09-15 spike 已把路线实测清楚（**E 路线：紧贴桌面窗口正上方 + 前台钩子重插**）。本阶段把该路线**真正实现进 `Win32PlatformWindow`**。

⚠️ **公共 API 净增 0**——与 Phase 12/14/15 均不同，**本阶段是纯实现层工作**。

**★ v1.2 事实勘察的核心发现**：§8 的 9 项已由真机实测全部回答（探针 `.workbuddy/spike/desktop_layer_probe.cpp`，4 形态 × 3 轮 + 用户目视确认）。**「显示桌面」只最小化「可最小化窗口」** ⇒ 真因是 **`WS_MINIMIZEBOX`**，**不是 `WS_POPUP` 本身** ⇒ D1 的最优解从「换 `WS_POPUP`」变为「**只移除一个样式位**」（保留除「最小化」外的全部 Phase 12 红利）。另钉死：不加 `WS_EX_NOACTIVATE`（交互实测正常）· 不加 tick 心跳（钩子单独够用，tick 是 3 倍开销且零收益）· `GetShellWindow()` ≡ `FindWindowW("Progman")` · 多显示器下 `Progman` 唯一。

**★ v1.4 勘误 + 初设要点（2026-09-19）**：需求稿 K13 原称「Phase 12 已有 `Bottom` 档的 z 序用例」——**实核不成立**（`WindowChromeTests.cpp` 9 用例零 `WindowLayer` 覆盖）⇒ **R10 是「有实现、零自动测试覆盖」**，初设据此补 **T16-1** 的 `Bottom` 回归。初设 v1.0 的三处设计要点：① **`TargetInsertAfter` 按档位分流**（`nullptr` = **跳过哨兵**，绝不让 Desktop 档降级成 `HWND_BOTTOM`）；② **`hook → 实例` 反查**（`SetWinEventHook` 回调无 user-data 参数，故以 hook（回调首参）为键查表——**否**掉「进程级窗口列表 + 广播」方案）；③ **重插前先判在位**（消除 Win+D 瞬间的闪烁来源）。契约 C1–C10 · 开放点 O1–O5（`SWP_FRAMECHANGED` / `WS_EX_TOOLWINDOW` / C2 分支自动化 / 容器形态 / 四工具链）。

| [phase16-desktop-layer-requirements.md](phase16-desktop-layer-requirements.md) | 需求确认（**K1–K14 现状勘察（全部带行号）** · **F-1/F-2/F-3 三条会改形态的事实** · **R1–R12 四组** · **D0–D11 全部给倾向** · 非目标 8 项 · **§8 事实勘察 9 项（已完成）** · **§8.1 两条新约束**）；**v1.2（2026-09-19）勘察完成回写**：§8 整章重写为「事实勘察结果」（9 项逐条附实测判据）· **§1.3 F-1 收窄**（原「⚠️ 结构性」措辞**过强**——实测显示差异是**一位可修**的）· **D1 新增选项 A′ 并改为首选**（`WS_OVERLAPPEDWINDOW & ~WS_MINIMIZEBOX`）· **D2/D4 实测钉死** · D3 取 B（A′ 下**必需**）· **§8.1 登记「像素采样非主屏不可用」（G-4 直接表现）+「重插前先判在位」**）；**v1.3（2026-09-19）外部评审「方向已可进初设」+ 3 处自洽性修正**：状态行/§8 引言由「9 项全部实测回答」严谨化为「**9 项均已形成处置结论**」（P0–P2 真机闭环 · **§8-7/§8-8 顺延初设**）· §2「维持住」改为「**钩子 + 重插前位置判定 + 句柄即时重查**」（与 D4/D6 对齐）· **R9 去掉「句柄缓存」**（与 D6 对齐）· **新增 §8.2「留给初设的问题清单」**（8 项，含边界纪律） | ✅ **v1.3 定稿**（**可进初步设计**——9 项均已形成明确处置结论，P0–P2 由真机闭环；4 形态判决：`WS_OVERLAPPEDWINDOW` ❌ FAIL · **`& ~WS_MINIMIZEBOX`** ✅ PASS · `WS_POPUP` ✅ PASS） |
| [phase16-desktop-layer-preliminary-design.md](phase16-desktop-layer-preliminary-design.md) | 初步设计（**§1 设计输入与基线**：D0–D11 已定项汇总 · **B1–B11 代码基线（带行号）** · **§1.3 ★ 需求稿勘误**（K13 实核不成立）· §1.4 §8.2 八问答案索引；**§2 头文件改动**——**公共头净增 0**（**不新增 pure virtual ⇒ 3 个替身零同步**）+ `Win32PlatformWindow.h` 内部头增量（含 **`ResolveTarget` 纯函数**——public static，**零新文件**）；**§3 实现分解**——`TargetInsertAfter` 分流（含 `nullptr` 哨兵语义）· `ApplyDesktopStyle` 幂等可逆（**不需要 `SWP_FRAMECHANGED`**）· **`hook → 实例` 反查** · 重插前判在位 · `Release` 顶部脱钩（**修既有 `hwnd` 空判陷阱**）· 四态语义「零代码」对照；**§4 契约 C1–C12** · **§5 影响面**（含「工具」行）· **§6 开放决策点 O1–O5（O1 已关闭）+ §6.1 O1 实测结果** · **§7 测试 T16-1..7**） **v1.1**：外部评审处置——**O3 定为 C**（`ResolveTarget` 纯函数 ⇒ 最危险的 C2 分支由「代码审查」升级为 **T16-7 全自动**）· **O2 反转为「不加」`WS_EX_TOOLWINDOW`**（它改的是任务栏 / Alt+Tab / 激活 / 系统菜单语义，**不属本阶段需求**）· **新增 C11 / C12**（不可判 ⇒ 禁止无依据 z-order 操作 · **钩子独立于 `HWND`**）· 修 §5 方法计数（+10 → **+12**）与 §7 自洽（原「唯一不可自动 = C1」与 §3.7 矛盾）· **补 B10**。 **v1.2**：**O1 实测关闭——`ApplyDesktopStyle` 不需要 `SWP_FRAMECHANGED`**（三组 × 3 轮 Win+D 全 PASS，加与不加该标志行为完全一致；**框架先例** `SetChromeMode(Borderless)` 已在配置期派发过一次，`Win32PlatformWindow.cpp:800-804`）· **如实记录一处探针保真度缺陷**（v1–v5 漏了框架那一步 ⇒ 其 `non-client` 读数在框架里不会出现；**对 P0 / O1 结论均无影响**，已修于探针 **v5a**）· 新增 **B11**。 **v1.3**：**v5a 复测补记**——三点量具 **A（创建后）/ B（改样式后、Show 前）/ C（Show 后）** 全部 `non-client = 0 × 0`、Win+D **3/3 PASS** ⇒ O1 结论**不变**，且**决定性证据改出自「已对齐」的量具**；组 2 / 组 3 的早期 PASS **无需重跑**（依据 `iconic` / `rect` / 像素，与 NC 读数无关）。 | 🚧 **v1.3——O1 已关闭（v5a 复测确认），可进详细设计** |
| [phase16-desktop-layer-detailed-design.md](phase16-desktop-layer-detailed-design.md) | 详细设计（**§1 实施总览**：**7 文件**（4 改 + 1 新建 + 2 接线）· Public 头 **92 → 92** · 用例 **210 → 218** · 断言特征串 **10 → 10**；**§1.3 对初设的五处修正**——**D-1** `PlatformWindow.h` 的 `@details`（原「Bottom/Desktop 档持续维护普通窗口层底部位置」对 Desktop 已失实）⇒ Public 头改动 **2 处（仍均仅注释）** · **D-2** T16-5 改「真实 `SetWindowPos` + z 序邻居观察」（不把契约建立在未证实的 `DefWindowProc` 行为上）· **D-3** 测试落点 = 新建 `DesktopLayerTests.cpp`（样本直接构造 `Win32PlatformWindow`——`DropFilesTests.cpp:133` 先例）· **D-4** 新成员落点 = private 成员区末尾（不割裂 Phase 12 分组）· **D-5** **零新增 `FRAMEWORK_ASSERT`**（前置条件均由显式守卫闭合）；**§2 逐文件最小 diff**（含 `WM_WINDOWPOSCHANGING` 的**等价性核对表**（`Normal`/`Bottom` 逐位等价）· `SetWindowLayer` 的**三处差异表** · 九个新方法体全文）；**§3 关键行为冻结**（`ResolveTarget` 真值表 · Borderless+Desktop 调用序列 · 生命周期时序含**注销顺序不可交换** · `Bottom` 档成本不变）；**§4 契约 C1–C12 ⭢ 实现落点 ⭢ 测试**；**§5 测试 T16-1..T16-7**（含 T16-4 的**两窗口分工**、T16-7 的**顺序纪律**、**否定型断言必须配正对照**的新纪律）；**§6 验收 A1–A8**（**A4 = C2 不容降级**，含 `HWND_BOTTOM` 出现点的机检判据）；**§7 影响面**（`PlatformWindow` 3 个实现者**零同步** · `WM_DESTROY` **刻意不改**）· §7.1 ModelProbe 过期注释（**须单独授权**）；**§8 局限 L1–L9**（含 L1 的判据措辞纪律：**不得写「无闪烁」**）；**§9 三批实施顺序**（A 纯新增 / B 行为切换 / C 测试）） | 🚧 **详细设计 v1.10 · 已实施（批 A/B/C + A5 跟随修复 + T16-8 + 中断安全修正）——待验收 A1–A8**（批 A 纯新增 +327/0 删 · 批 B 行为切换 +49/−24 · 批 C 测试 `DesktopLayerTests.cpp` 562 行 · 用例 210→**218** · A5 修复：延后一拍 + 固定 4 拍重试 · A4 判据生效：`HWND_BOTTOM` 代码行仅剩 `ResolveTarget` 分支 1 处 · **v1.9**：T16-8 制造「不在位」由 `SetWindowPos(桌面, HWND_TOP)` 改为 `SetWindowPos(桌面, hwnd)`——只交换一格，原写法会把系统桌面抬到 z 序**最顶**，进程中断即把桌面永久留在顶层、须重启 explorer · §2.5.4 标题「七个用例」→「**八个**」 · **v1.10**：T16-8 改**用参照窗口 `other` 占位**制造「不在位」——原写法搬动 `Progman`，而 **shell 会异步修复**对系统桌面的 z 序改动、恰好抹掉链在 64ms 内的重插 ⇒ 用例**假失败**（实测 probe 在稳定期同一操作 `ok=1/eq=1`）；新写法**零系统级副作用**、无需还原 · 断言 51→**52** · 文件 562 行） |

## Phase17 布局内边距（Layout padding）（✅ 需求 v1.2 · ✅ 初设 v1.1 · ✅ 详设 v1.2 —— **已实现：A1–A6 实测通过 · `ecdi_tests` 226 全绿（2026-09-21）**）

`roadmap-deferred.md` §7.6 **#38「容器级内边距」**的立项落地（2026-09-20 用户拍板**方案 B（框架侧）**；#38 原文「⛔ 不得并入 Phase 16」的约束随 Phase 16 收口而解除）。**动机**：内容四边贴死客户区，鼠标拖选极易越出 GUI 边界。⚠️ **归因澄清**：贴边**不是 AutoSize 的缺陷**（AutoSize 的定义就是「窗口尺寸跟随内容」⇒ 内容从 `(0,0)` 铺满是它的必然结果），**修复落点在布局层**。**路线**：`VerticalLayout` / `HorizontalLayout` 各加 `int padding = 0` 构造形参，`Arrange` 改 **4 处**（主轴起点 / `remaining` / 跨轴尺寸 / 跨轴坐标）⇒ **root 一级配置即全局留白**。**规模预期**：公共头 **92 → 92**（只改既有签名、不新增头）· 现有调用点（生产示例 **20** / 测试 **20** / README 示例 **1**）**全部零改动**（追加可选参数）。

| [phase17-layout-padding-requirements.md](phase17-layout-padding-requirements.md) | 需求确认（**K1–K10 现状勘察（全部带行号）** · **R1–R10 四组** · **D0–D5 决策点**（D0 已由 Phase 9.7 的 F3 纪律锁定 = 构造参数注入、不设 setter） · 非目标 **N1–N7** · 测试方向 **T17-1..T17-8** · **§8 留给初设的 Q1–Q5**；含「贴边不是 AutoSize 缺陷」的归因澄清 + `main.cpp` 须单独授权的触角提醒） | ✅ **v1.2 定稿**（含收口回填） |
| [phase17-layout-padding-preliminary-design.md](phase17-layout-padding-preliminary-design.md) | 初步设计（**§1.2 代码基线 B1–B6（全部带行号）**——其中 **B1（`SetSize` 无钳制）** 给 Q4、**B3（`ArrangeInternal` 递归重算）** 给 Q3 提供了实证答案 · **§1.3 对评审 14 条逐条处置**（全采纳；第 5 条采纳目标但简化为单层 `max`，并**指正评审示例的一处算术勘误**：`510 ≠ 500`，正确分配 `146/146/148`）· **§1.4 Q1–Q5 全部收敛** · **§3.2 `Arrange` 四处最小 diff** · **§3.3 取值口径三项定案**（单层钳 / 跨轴钳 0 / 主轴起点不钳 = 硬 inset）· **§3.4 不变式精确化**（等式仅在非钳制区间成立，钳制区间退化为非负可用空间模型）· **契约 C1–C6** · **§7 测试口径 T17-1..T17-8**（既有 11 条 Layout 用例**一字不改**是硬约束） · 开放点 O1–O4（**O1 断言形态是唯一需在详设前收敛的一项**，影响断言特征串 10→11 与否））· **v1.1（第二轮评审处置）**：修正 **T17-5 判据**（`remaining` 是「给 stretch 子的可用空间」，**不是**「所有子的主轴尺寸总和」——fixed 子主轴尺寸**不被布局改写** ⇒ 原判据 `Σ == remaining == 0` 会被击穿；改用「fixed + stretch 混排」测死两者之别）· **重写 §3.4**（显式区分 `fixedTotal` / `remaining`，并更正「等式因 padding 才失效」的归因误导——Phase 9.7 的 `max(0,…)` 早已使其只在无 overflow 区间成立）· **冻结 O1**（`FRAMEWORK_ASSERT` + Release 钳 0 ⇒ 断言特征串 **10 → 11**）· T17-8 分离 Debug/Release 语义 | ✅ **v1.1 评审通过** |
| [phase17-layout-padding-detailed-design.md](phase17-layout-padding-detailed-design.md) | 详细设计（**§1.3 七处细化**——其中**两处是查出的初设漏项**：① `@param fillCrossAxis` 与 `.cpp:59` 的「跨轴坐标恒 0」在 `padding > 0` 后**失实**、必须改（skill 条 80）；② 调用点构成更正——新增 `examples/VisualTest` 这个初设未列出的消费者，`src/Demo` 10 处**不参与构建**） · **§2 逐文件最小 diff `△1–△8`**（明确「哪些行动 / 哪些行逐字不动」+ V/H 同构对照） · **§3 关键行为冻结**（`cross` 循环外求值 · 三条取值口径落点 · 溢出区间的 `fixedTotal`／`remaining` 精确区分 · 与 content offset／HitTest／resize 链／AutoSize／CaptionBar 的逐项「不动」声明） · **契约 C1–C6**（每条绑实现落点与验证） · **§5 测试规格**（逐用例精确数值，全部期望值经**目标形态模拟脚本复算通过**；T17-8 定 `#ifdef NDEBUG` 分支） · **验收 A1–A7**（含 A4 跨轴取值唯一入口 / A5 旧写法零残留两条**结构性判据**） · **§8 局限 L1–L5** · **§9 两批实施顺序 + 6 条盯防清单**） | ✅ **v1.2 已实现**（A1–A6 实测通过 · 226 全绿；★ A6 落点由 root 改 page 一级，见详设 §1.3-8） |


## Phase18 窗口/根背景能力（Window/Root background）（✅ 需求确认 **v1.1 评审通过**（2026-09-22）· ✅ **初步设计 v1.1 评审通过**（冻结 **O1–O4** · T18-5 语义收窄 · 用例口径 **226 + 5 = 231**）· ✅ **详细设计 v1.1 评审通过**（第二轮评审：**🟢 通过，可进入实现** · **§3.1 实现盯防清单 9 条** · 措辞纪律 · A1 标「设计目标」）· ✅ **已实现并收口（2026-09-22）：批 A/B · 四工具链 231 全绿 · A1–A7 全过 · 详设回写 v1.2**）

> **立项依据**：`roadmap-deferred.md` **§7.7 条目 #39**——**由 Phase 17 A6 实施实测派生**（root 级 padding 露白框）。

| 文档 | 内容 | 状态 |
|------|------|------|
| [phase18-window-background-requirements.md](phase18-window-background-requirements.md) | 需求确认（**§1.2 现状勘察 K1–K8 全部带行号实测**——其中 **K1**（`GDIBackend.cpp:261` 硬编码 `WHITE_BRUSH`）· **K2**（`RenderingBackend` 接口无背景入口）· **K3**（`Window` API 无背景入口）· **K4**（root 是裸 `Widget`）四条共同界定缺口：「客户区底色」**无任何可配置入口**；**K8** 记明库内测试替身 `RecordingBackend` 被 **10 个测试文件**依赖〔2026-09-22 实测复核〕⇒ 接口变动须同步）· **§1.3 锁定 Phase 4 四层不变量**（清屏属能力层、颜色属决策层）⇒ 约束 D1/Q2 · **技术路线三案**（A 窗口级配置 + Backend 消费〔倾向〕/ B 让根具备背景〔备选〕/ C 不做〔兜底〕）· **R1–R6**（**R3 是本项直接动机**：Phase 17 的 root 级留白在该能力落地后首次可用）· **D1–D7**（D1 落点 / D5 恢复默认 / D6 命名留初设；**D7** 处理「决策 16 注释将变假」）· **N1–N6** · **Q1–Q4**（**Q2 清屏形态**可能牵动 `RenderCommand` 变体 = 最大风险点） | ✅ **v1.1 评审通过**（2026-09-22 外部评审：「没有看到需求层面的阻塞问题」；**§1.3 纪律已冻结** · **Q1 升级为数据流五问** · **新增 Q5「唯一默认值来源」** · K8 数字勘误 12 → **10**） |
| [phase18-window-background-preliminary-design.md](phase18-window-background-preliminary-design.md) | 初步设计（**本稿核心 = 评审钉死的两件事**：**§2 数据流定案**（五问逐条）· **§2.4 唯一默认值来源**）——**§1.2 代码基线 B1–B11 全部带行号实测**，其中决定形态的五条：**B1** 帧编排顺序（命令收集在 `BeginFrame` **之前**）· **B2** `Renderer` 是**纯转发器** · ★ **B5** `RecordingBackend::BeginFrame()` 是**空实现**（帧边界在替身里零痕迹）· **B9** `BeginFrame()` 的**场景外调用点 6 处** · ★ **B10** GDI 画刷的既有模式 =**决策 24「每次创建/销毁、不缓存」**；**§2.2 两子方向对照后采纳 (b)**「颜色作 `BeginFrame` 输入」（理由序：状态归属正确 → 不给 Backend 生第二默认值 → 不出现绕过 `Renderer` 的第二入口；代价 = 4 实现/转发点 + **6 处测试调用点**）；**§2.4** 由 Q5 推出 **`BeginFrame` 形参不得写默认实参**；**§3.2 清屏沿用决策 24**（`CreateSolidBrush` + `FillRect` + `DeleteObject`，复用 `ToColorRef`）⇒ **零新概念**；**§3.3 alpha 被忽略**（`ToColorRef` 丢弃 alpha ⇒ 半透明按实色画）**必须写成契约**，否则易顺手接 `BlendAlphaSolid` 拖进 Phase 9 范畴；**契约 C1–C5** · **§3.1 改动点 △1–△8** · **§5 影响面如实记「公共 API +1」**（与 Phase 16「净增 0」不同）· **O1–O4**（O1 运行期改色是否隐含 `Invalidate` · O2 是否保留 `FRAMEWORK_ASSERT`（影响特征串 11/12）· O3 像素级 · O4 不给 getter）· **§7 测试 T18-1..T18-6** · **§8 验收 A1–A7**（**A4/A5 为结构性判据**：依赖零新增 / 默认值唯一来源）） | ✅ **初步设计 v1.1 评审通过**（第二轮评审处置：**冻结 O1–O4**——O1 内嵌 `Invalidate()`（依据职责契约 `TextBox.cpp:189`）· O2 不加断言（性质不同：契约违规 vs 平台资源失败）⇒ 特征串保持 **11** · O3 不做像素级 · O4 不加 getter；**数据流与 `BeginFrame(const Color&)` 签名冻结**；**T18-5 语义收窄**（`RecordingBackend` 证明不了「GDIBackend 未进 Blend」⇒ 只断言 alpha 原样传递，该保证交 **A4 结构性审查**）；**用例口径统一 226 + 5 = 231**；措辞「**请求**重绘」而非「立即生效」） |
| [phase18-window-background-detailed-design.md](phase18-window-background-detailed-design.md) | 详细设计（**本稿解决初设未覆盖的「测试装置」问题** + 把评审 12 项钉到行）——**§1.2 新增基线 B12–B16**：★ **B12** `Window::PaintFrame` 在 **private** 区（测试不能手动驱动帧）· ★ **B13** `Application::Create` **不接受 `RenderServices`**（测试拿不到 Window 内部 backend）· ★ **B14** 而 `Window` 构造注释明写「**测试/未来可注入其他后端**」⇒ **既定意图只有半个通路** · **B16** `Window` 配置 API 形制 = 纯转发平台层（`SetBackgroundColor` **不是**，它改自身成员）；**§1.3 / §2 新增两处定案**：**N1** `Application::Create` **追加带默认值的 `RenderServices` 尾形参**（手法与 Phase 17 追加 `padding` **同型** ⇒ 现有调用**逐字不动**；`Window` 构造已有同款先例）· **N2** 测试触发帧走 `Show()` + `PumpMessages`（**不新增任何 `ForTests` 缝**）；**§2.3** 可编译的测试装置骨架（`RecordingBackend` 双重继承 ⇒ 需**两个实例**，观察者用非拥有裸指针）；**§3 △1–△9 行级**（含 6 处既有调用点适配 + 新增测试文件须**手工登记两处**）；**§4.1 清屏点完整替换代码**（与现状只差 3 处）· **§4.2 alpha 忽略的精确定义**（★ 写出**实现红线**：连「`a == 0` 就跳过清屏」也不得做）· **§4.3 画刷失败行为**；**§5 契约 → 验证映射**（明确 **C2/C3/C4 各有一段「运行时测不到、只能源码级保证」**）· **§6 T18-1..T18-5 完整输入/期望**（含容差与断言层级）· **§7 A4/A5 结构性判据**（依赖零新增 / 唯一默认值来源）· **§8 影响面如实记「公共 API +2」**（`Window::SetBackgroundColor` + `Application::Create` 形参））  | ✅ **详细设计 v1.1 评审通过（第二轮评审 🟢 可进入实现，2026-09-22）**——v1.1 新增：**§1.4 第二轮评审 14 条逐条处置**（全采纳）· **§3.1 实现盯防清单 9 条红线**（帧编排**不得重排** / `SetBackgroundColor` 自带 `Invalidate` / 形参**不得带默认实参** / `a` **不得接入分支** / 画刷失败不 assert / 不新增 `ForTests` 缝 / `Renderer` 仍单行转发 / `Create` 只多传一个实参 / 特征串保持 11）· **§5-C1 + §6 措辞纪律**（「逐位相同」的范围**仅限参数取值**，非像素级）· **§6 / §7-A1 标「设计目标」**（以实测回填）· **B14 定性用词精确化**（「已登记的延迟设计，在第一个真实消费者出现后兑现」；一律写「打通注入通路」）。**v1.2（2026-09-22）实现后回填**：△8 勘误「6 处 → **3 处**」（6 = Begin + End 合计）· **§7.1 新增 A1–A7 实测回填** · A4-② 判据措辞精确化（「无 `BlendAlphaSolid`」只限清屏路径）· **A6 记形态被否的根因**（连带内缩 chrome + 与全宽 caption 带脱钩） |
## Phase18.1 子节点内缩（Child inset）（⏸️ **已搁置**——2026-09-22 立项当日结论：**路线 C 已落 demo 侧、框架零改动**；需求稿 v1.1 归档）

`roadmap-deferred.md` **§7.8 条目 #40「同一父布局下的留白归属」**的立项落地——**由 Phase 18 A6 目视实测派生**（用户 2026-09-22 原话：「正常，就是视觉上不太行，底色和原有底色不同，标题栏能不能做到不内缩」）。

**症状两条（只有 S2 是本阶段目标）**：**S1** 留白让出的 12px 用**窗口底色** `#222934`、与页面底色 `#0f1115` 不同 ⇒ 像套了一圈描边（✅ 已就地收口：底色改与页面同值，能力照走 `BeginFrame` 链路）；**S2** `CaptionBar` 与 `page` 同属 root 布局的子 ⇒ **标题栏被一起内缩 12px**（🚧 本阶段目标）。

**S2 的本质不是"参数没调对"，而是留白的归属**：开在**父**（`Layout::padding` 对全部子一律）还是开在**子**（子自缩、chrome 不受影响）。**Phase 18 只解掉了一半**——「露出来的是不是白的」已解决（`GDIBackend.cpp:260-266` 清屏改用本帧底色），「标题栏跟着走」**未**解决。

**★ 定形态事实（F-1）**：「让出的窟窿开在谁身上」决定看不看得见窗口底色——root 是**裸 `Widget`（不绘制）**⇒ 开在 root **露窗口底色**；page **自绘背景** ⇒ 开在 page **看不见底色**。

**★ 决定"可不开"的事实**：`Panel` 默认**背景透明**、官方语义即「**隐形布局容器**」（9.6 v1.1 变更；`DefaultTheme.cpp:44-47` / `Panel.cpp:78`）⇒ **应用侧包一层透明 `Panel` 承接 `padding`，零框架改动即可达成同一视觉**；Phase 17 判该路线代价过高所依据的「需 override `SetSize` 同步几何」**已因 9.7（布局）+ Phase 15（坐标不变量）而消失**。**故评审第一件事是判 D0（是否开 / 选哪条路线）。**

**★ 结论（2026-09-22 当日）：搁置。** 三条要求分层——**chrome 贴边**与**内容四周留白**是**基本要求**（前者更是**平台契约**：`Win32PlatformWindow.cpp:417` 的 `WM_NCHITTEST` 用 `y < captionHeight` 的**全宽顶部带**判 `HTCAPTION`，**不看 `CaptionBar` 几何** ⇒ chrome 内缩会让"画出来的"与"能拖的"脱钩），**留白处露窗口底色**是**可选效果**；前两条在 `af1f080` 已成，第三条由**路线 C**（应用侧透明 `Panel` 承接 `padding`）落实 ⇒ **无框架改动需要**，路线 A 降级为记账。**重启条件见 [roadmap-deferred.md](roadmap-deferred.md) §7.8 #40 的 R-1..R-4**（届时优先 B/E，而非 A）。

| 文档 | 内容 | 状态 |
|------|------|------|
| [phase18.1-child-inset-requirements.md](phase18.1-child-inset-requirements.md) | 需求确认（**K1–K10 现状勘察全部带行号**——K1 `padding` 是布局级单一 int、对全部子一律〔`VerticalLayout.cpp:38/42/45/63`〕· K2 `Layout` 抽象无 per-child 概念 · K3 同类实例状态 `SetStretch` 是**零副作用 setter**（inset 的形态先例）· K4 root 恒客户区全尺寸 · K5 未覆盖像素 = 窗口底色 · K6 `Panel` 默认透明 · K8 `SetSize` 虚 + `CaptionBar` override · K10 root 裸 `Widget` 不绘制；**F-1 定形态事实** · **技术路线 A/B/C/D 对照**（**A** 子节点 inset〔倾向：`Widget::SetInset` + `Layout` 基类**唯一消费 helper**，公共头 92→92，`inset=0` 逐位退化〕/ **B** 布局豁免〔否：语义窄 + 主轴游标边界复杂〕/ **C** 应用侧零改动组装〔**可立即验证**〕/ **D** 不做）· **R1–R8** · **D0–D6**（**D0** 是否开/选路线 · **D4** 主轴游标按**槽位**推进 · **D6** 用 setter 而非构造参数）· **N1–N6** · 验收 **A1–A4**（A1 目视主判据；A2 零 inset 逐位退化；A3/A4 结构性可机检）· 留给初设 **Q1–Q6**） | ⏸️ **v1.1 归档（搁置）** |

## Phase19 鼠标事件维度补全（Mouse event dimensions）（需求 **v1.1 ✅** · 初步设计 **v1.1 ✅** · 详细设计 **v1.1 ✅**——2026-09-22 立项 / **2026-09-23 三份均评审通过 ⇒ ✅ 已实现并收口**）

**框架缺陷修复队列第 ① 位**——`roadmap-deferred.md` **§7.9 #41** · 审计 `docs/framework-defect-audit.md` **§4 D-1**。

★ **初步设计已定案四项**（2026-09-23）：**D2** = **私有位掩码 + `IsButtonDown(MouseButton)` 谓词**（**零新公共类型**，不泄漏 `MK_*`）· **Q3** = 沿用 `MouseButtonDownEvent::isDoubleClick` 的**尾默认参先例**（测试 **22 处**构造点零改动；★ 注意**默认实参不继承**⇒ 6 个类都要带）· **Q4** = 测试走 **`FakeHost` + `Handle()` 真翻译路径**（基础设施**已在**`EventTests.cpp`，零新建；**手工构造不得充当 D-1 的验收证据**）· **D3** = 修饰键源取 **`wParam`**（**事件时刻权威** + **无头可断言**）；**Alt 有意缺位**（平台无 `MK_ALT`，且**理由已重写**——框架本就已在键盘侧用 `GetKeyState`）。

**定性：契约错误（丢信息），不是缺能力**。平台层本来就有：`WM_MOUSEMOVE` 的 `wParam` 携带 `MK_LBUTTON`/`MK_RBUTTON`/`MK_MBUTTON`/`MK_XBUTTON1-2`（**按下的键**）与 `MK_SHIFT`/`MK_CONTROL`（**修饰键**）——而 `MouseEvent` 基类只定义了两个坐标成员（`MouseEvent.h:41-44`），翻译器也只取坐标就返回（`WindowMessageHandler.cpp:92-106`）。⇒ 任何"按住拖动"手势都得**自己维护布尔**，代码里**已有两处为此写的特例**（`ScrollBar::m_dragging`（置位 `:250`/`:268` · 消费 `:289-314` · 清位 `:277-286`）· `TextBox::m_mouseDown`）。

**形态由既有原则锁定（F-1）**：Event 层「**轻量、只表示已发生的事实、原始值不归一化**」——这条落款**现成存在**于 `MouseWheelEvent.h:11`（Wheel 做到了，Move 连原始值都没带）⇒ 本项**照抄平台位**，**不做** `IsDragging()` 之类语义结论。修饰键**复用键盘侧既有的 `KeyModifier`**（`KeyBoard/KeyEvent.h:10/23/29/31/33`，含 `HasModifier`/`IsShiftDown`/`IsCtrlDown`/`IsAltDown` 先例）——「一个概念只用一个词」。

**顺带解锁**：`roadmap-deferred` **#36**（横向滚轮——卡在"平台未翻译 `WM_MOUSEHWHEEL`"+"`MouseEvent` 无修饰键"）；**已定案不并入**（初设 §2.4 / D6）；**接缝** = 将来做 #36 时**倾向新增 `int deltaX = 0` 尾形参**（沿用同一套零破坏手法），**而非新增事件类型**——"滚轮"是一个概念，横向只是它的另一根轴。

★ **实现与实测（2026-09-23 收口）**：**四工具链 `ecdi_tests` 236 全绿**（231 + 5）· 断言特征串 **11 → 11** · 公共头 **92 → 92** · **公共 API +2**（`MouseEvent::IsButtonDown` / `HasModifier`）。**改动 = 8 文件**（6 公共头 + 翻译器 + `EventTests.cpp`；**无新文件**）。★ **本地已真跑通**（MinGW g++ 16.1 `-D_DEBUG`，70 TU 全量编译 + 链接 + 运行）。★★ **P1/P2/P3 实测完成**（独立探针，见详设 **§6.1**）：`WM_LBUTTONDOWN` 置 `MK_LBUTTON` ✓ · `WM_LBUTTONUP` **不含**（该位已清）✓ · **`WM_MOUSEMOVE` 按住时置位** ✓ ⇒ `IsButtonDown()` 在**按下 / 拖动 / 抬起**三阶段**全部可读**。★ **给消费者的边界**：**抬起那一刻该位已清** ⇒ 「刚才是否在拖」不能用 `IsButtonDown` 判断，须用 `GetButton()`。
| 文档 | 内容 | 状态 |
|------|------|------|
| [phase19-mouse-event-dimensions-requirements.md](phase19-mouse-event-dimensions-requirements.md) | 需求确认（**K1–K10 现状勘察全部带行号**——K3 ★ 平台有却被丢 · K5 键盘 `KeyModifier` 先例 · K6 ★ `MouseWheelEvent.h:11` 的「原始值不归一化」落款 · K7 两处手工状态 · K8 既有捕获机制 · K9 卡住 #36；**F-1 定形态事实** · **已排除的两个替代方案**（控件自维护布尔 / 消费侧 `GetKeyState`——后者查的是"查询时刻"而非"事件时刻"，消息积压时不等价）；**R1–R7**（R1 按键集合 · R2 修饰键 · R3 四类同源 · R4 原始值不归一化 · R5 零破坏 · R6 空=空而非未知 · R7 为 #36 留缝）；**D0–D6**（**D0 复用 `KeyModifier`**〔倾向〕· **D1 落基类**〔倾向〕· D3 **不做 Alt**〔`wParam` 不提供；**造成键鼠不对称，须显式声明**〕· **D4 不简化两处手工状态**〔它们另有捕获/拖选职责〕）；**N1–N6** · 验收 **A1–A5** · 留给初设 **Q1–Q6**（含 ★ **`MK_XBUTTON` 低位/高位陷阱**）） | ✅ **v1.1（评审通过 2026-09-23）** |
| [phase19-mouse-event-dimensions-preliminary-design.md](phase19-mouse-event-dimensions-preliminary-design.md) | 初步设计（**B1–B12 代码基线**——★ **B3 翻译器构造点 4→5**（漏 `WM_LBUTTONDBLCLK` 双击分支）· **B4 测试构造点 22 处**（6 文件；消费侧 0 处）· **B5 `isDoubleClick` 尾默认参先例**（Q3 的落款）· **B6 `TranslateModifier` 走 `GetKeyState` 且含 Alt** · **B8 `FakeHost` 翻译测试设施已在** · **B9 `GetKeyState` 源在无头测试里不可断言**（决定性）；**§1.3 对评审 12 条逐条处置**——含 **3 处纠正**（`Window*` 非引用 / **R5 口径须限定** / **D3 理由重写**）；**§2 四项定案**（D2 / D5「成套或不给」 / **D3 修饰键源 + Alt 边界 + 重启条件** / D6）；**§3.3 Q2 陷阱表**——**只读 `LOWORD`**、`GET_XBUTTON_WPARAM` 只喂 `GetButton()`；**§3.5 两条待实测平台语义**（**不阻塞**，只影响注释措辞）；**契约 C1–C6**（**C5 = 零破坏的精确口径**）；影响面（公共头 **92→92** · 公共 API **+2** · 用例 **231→240** · 断言特征串 **11→11**）；O1–O6 · **测试 T19-1..T19-9**（承载 = **真翻译路径**；含 T19-4 一次钉死 XBUTTON 两维、T19-7 钉死"Alt 恒 false"）· 验收 A1–A5 落地口径。**v1.1（评审通过 2026-09-23）采纳两条建议**：**新增 §3.6「构造参数的通道性质」**（消费层 / 构造通道 / 兼容通道**三层分工** + 不给命名类型的理由 + **升级触发条件**）· **§2.3 补两层区分与措辞红线**（「Event 模型**支持** `KeyModifier::Alt` 概念」vs「**当前 Win32 映射不产生** Alt」 ⇒ `HasModifier(Alt)` 恒 false 是**映射边界**，**不是**模型缺失）· **新增 §9「交给详细设计的四件事」**） | ✅ **v1.1（评审通过 2026-09-23）** |
| [phase19-mouse-event-dimensions-detailed-design.md](phase19-mouse-event-dimensions-detailed-design.md) | 详细设计（**行级实现规格**：**B13–B18 详设新增基线**——★ **B13 既有测试已按 LOWORD/HIWORD 约定写**（`:148` 传 `MK_LBUTTON`、`:159` 用 `MAKEWPARAM(MK_XBUTTON1, XBUTTON1)`）· ★★ **B14 `FakeHost` 无 `MouseButtonUp`/`MouseWheel` 分支且 `ReceivedEvent` 无 delta 字段** · ★ **B15 无需登记 `RunAllTests.*`**（无新文件）· ★★ **B16 位序与 `MK_*` 5 个里 3 个数值不同 ⇒ 禁止移位**；**§1.3 对初设的三处修正**（△C1 测试字段 **+2 → +3** · △C2 除补字段还需**新增 2 个 case** · △C3 登记范围收窄）；**§2.1 六份公共头全文草案**（含 ★ 默认实参不继承 ⇒ **6 类都要带**）+ **§2.2 平台层两个 helper 全文 + 5 个调用点逐点 diff** + ★ **§2.3 实现盯防清单 6 条**；**§3 位序与映射表**（含"前两个巧合相同最危险"的数值证据）；**§4 契约 C1–C6 验证映射**；**§5 测试实现规格**（装置改动完整片段 + **T19-1..T19-9 输入/期望表** + 注册草案 + ★ **用例数口径勘误：231→236**，初设"240"是 T 编号数）；**§6 P1/P2 实测方案**（两种方式 + **从根上回避的注释写法建议**）；**§7 实现顺序与 6 个检查点**（★ 步①"不改调用点即全绿"= 零破坏的最早信号）；§8 影响面 · §9 验收 · §10 文档收口清单。**v1.1（评审通过 2026-09-23）**：新增 **§1.5**（第二轮评审处置）· **§2.1 补 `KeyModifier::None` 边界的 `@note`** · **§2.3 盯防清单增第 7 条**（不得顺手改 `None` 语义）· 实测算例输出留档；该边界记账为审计 **§7 A-10**） | ✅ **v1.1（评审通过 2026-09-23）· 已实现** |

## 框架缺陷审计与修复队列（✅ 审计 **v1.3**，2026-09-22 新建 / 2026-09-23 增 A-10、D-6，并回填 #41 已修复）

**非阶段文档**（不占 Phase 编号，与 `window-ownership.md` / `desktopnest-roadmap.md` 同级）——**排期转向的产物**：用户 2026-09-22 拍板「**先做缺陷修复的优先级更高**」（demo 是"功能的用法示范"而非开发脚手架，能力未定型时写它 = 维护两份真相；**ModelProbe 继续承担「第一个真实消费者 + 回归载体」**）⇒ **DesktopNest 推迟**到框架缺失补齐之后。

**三层各司其职、不重叠**：**审计**（本文件：证据 + 判据 + 顺序 + 重启条件）→ **总账**（`roadmap-deferred.md` **§7.9**）→ **实施**（`phaseN-*` 三件套）。

| 文档 | 内容 | 状态 |
|------|------|------|
| [framework-defect-audit.md](framework-defect-audit.md) | 缺陷审计与修复队列（**§3 该修 / 记账的分界判据**——该修三条（违反原则 / **平台有却被中途丢掉** / 无替代路径）vs 记账两条（有兜底 / 无消费者）· **§4 缺陷清单 D-1..D-6**（全部带行号；**D-1 的加重情节** = 已有两处手工状态 ⇒ 越晚改越贵）· **§5 desktop 侧缺口 G-5/G-6/G-7 + 多窗口×`Desktop` 未验项**（与前四者**分栏不混**）· **§6 队列顺序与理由** · **§7 记账不动清单 A-1..A-10**（含逐条**重启条件**；**v1.1 新增 A-10**；**v1.2 新增 D-6** = 抗锯齿只覆盖圆角；**v1.3 回填 D-1 已修复**）） | ✅ **v1.3**（2026-09-23） |

## Phase20 DPI 感知（DPI awareness）（✅ 需求 v1.1 · ✅ 初设 v1.1 · ✅ 详设 **v1.3** · ✅ **五批实现完毕 + 崩溃修复 + ★★ 全阶段收口（验收通过）**，2026-09-24 立项）

**框架缺陷修复队列第 ② 位**——`roadmap-deferred.md` **§7.9 #8**（= §1.1 条 8「DPI 感知评估」的优先级提升）· 审计 `docs/framework-defect-audit.md` **§4 D-2**。

★ **本阶段的特殊性：不是引入新概念，而是让一个既有契约真正生效。** 框架早在 **Phase 13** 就在 `Win32PlatformWindow.cpp:376` 声明「**D-DPI-1：换算点唯一在此——公共 API 语义恒为 DIP**」，并留下 **L6**（`WM_NCHITTEST` 物理像素 vs widget DIP 几何的错位）——处置栏写着「**与「DPI 感知」技术债一并闭合**」（`phase13-captionbar-detailed-design.md:795/1051`）。但进程**未声明 DPI 感知**（全库 **0 命中** `SetProcessDpiAwareness`），系统虚拟化下 `dpi` 恒 96，于是 `DipToPixels` 恒等 ⇒ **契约恰好与事实重合，从未被检验**。

⇒ ★★ **现状不是「正确」，是「两条错误互相抵消」**。三条已知症状都由此而来：

| 症状 | 来源 |
|---|---|
| 跨屏 reparent 后矩形 **×0.8**（420×280 → 336×224，`0.8 = 96/120` 逐位吻合） | Phase 16 spike 实测（`desktopnest-roadmap.md:253`） |
| `WM_NCHITTEST` 委托与 widget 几何**错位**（「两者仅在 100% DPI 下重合」） | Phase 13 **L6 / D6**（实测） |
| **无屏幕 / DPI 查询 API** ⇒ 应用想自己适配也拿不到数据 | `PlatformWindow` 接口无 DPI 方法（K12） |

★ **路线倾向 B**：声明 **Per-Monitor V2** 并把 DIP 契约贯彻到**全部平台边界**。**否决 A**（保持 unaware 只补查询——unaware 下查询到的也是虚拟化值）与 **C**（只在 chrome 生效——会让契约变成**半真**，比现状更糟）。

★★ **§3.2 的连锁关系（本阶段最重要的判断）**：**感知声明与 DIP 语义贯通必须同批完成**——单独声明感知会让 `Application::Create(w, h)` 的尺寸与 `Font::size` 被当作**物理像素**，在 150% 屏上**视觉缩小 1/3**，是**纯破坏性变更**。

**分层原则一句话**：**公共 API 与框架内部（Widget / 布局 / 事件）= DIP；平台边界 = 物理；渲染后端 = 物理**——换算只在边界的两个方向发生。

**零回归护栏 G5**：`dpi == 96 ⇒ px == dip`（`DipToPixels` 公式已保证）⇒ 100% DPI 下**逐位等价**。

| 文档 | 内容 | 状态 |
|------|------|------|
| [phase20-dpi-awareness-requirements.md](phase20-dpi-awareness-requirements.md) | 需求确认（**K1–K13 现状勘察全部带行号**——★ K1 DIP 契约已立 · K3 `DipToPixels` 的 DPI 来源与**预留替换点** · ★★ **K5 进程未声明感知 = 全部症状的根因** · ★★ **K6 = Phase 13 的 L6/D6 明写「一并闭合」**；**§1.3「两条错误互相抵消」**；**G1–G5** 目标（含逐位等价护栏）· **N1–N5** 非目标 · **路线 A/B/C**（倾向 **B**）· ★ **§3.2 连锁关系** · **§4 触面清单**（10 个面 + 「后端不需变」；★ **「窗口尺寸」= 3 个入口**）· ★ **§4.1 双空间共存**（同一 `GetClientRect`：平台侧 DIP / 后端物理，**不得合并成一个尺寸访问器**）· ★ **R3 = 架构约束**（非普通需求）· **D0–D8** 决策点 · **R1–R9** 需求 · **A1–A6** 验收（区分自动 / 人工）· **Q1–Q6** 留给初设（★ Q2「文本测量单位」是最实际的取舍）） | ✅ **v1.1 已通过**（2026-09-24） |
| [phase20-dpi-awareness-preliminary-design.md](phase20-dpi-awareness-preliminary-design.md) | 初步设计（**回答需求 Q1–Q6 —— 六项定案**：Q1 单位分层 · ★ **Q2 文本测量返回 DIP**（含自洽性论证）· ★ **Q3 感知声明落 `Application` 构造** · ★ **Q4 只加 `Window::GetDpiScale()`**（并**收敛 G4 范围**：屏幕枚举不做）· ★ **Q5 定案 A**（入参换 DIP）· ★ **Q6 DPI 为翻译器正常成员状态**。★ **B1–B18 代码基线全部带行号**——其中四条为**本稿新勘**：**两处字体缓存**（键不含 DPI）· **测量器用屏幕 DC 而渲染用窗口 DC** · **`TextMeasurer` 接口无 DPI 参数** · **翻译器无 DPI 来源**。§3 改动分解（含 **尺寸三入口**）· **契约 C1–C8** · O1–O7 · **T20-1..T20-8** · 验收 A1–A6 落地 · §9 交给详设的六件事） | ✅ **v1.1 已通过**（2026-09-24；待详设）——★ 评审指定详设必做三件：`PixelsToDip` 舍入 · `WM_DPICHANGED` 链 · **声明失败策略**（→ **§2.3.1「读取而非假定」**）；§9 六件事 → **七件事** |
| [phase20-dpi-awareness-detailed-design.md](phase20-dpi-awareness-detailed-design.md) | 详细设计（**△1–△25 逐文件行级改动** · ★★ **本稿最实质改进 = `DipToPixels` 改纯函数**——现签名收 `HWND` ⇒ **纯换算无法无头测试**（与初设 §9-① 直接矛盾）；改收 `int dpi` 并**移入 public 区**（沿用 `ResolveTarget` 先例）⇒ 同时达成「可无头测」与「DPI 来源/转换解耦」· ★ **§3（评审指定 ①）**：`PixelsToDip` 完整规格 + ★ **纯整数反例**证明双向恒等不可能 + **契约 C9** · ★ **§4（指定 ②）**：`WM_DPICHANGED` **只做三件事、绝不自行重布局**（避免两套 resize 路径）· ★ **§5（指定 ③）**：声明失败策略 + 感知级别诊断日志 · **B19–B28 基线**（含 **B24 = 既有 T4 用例天然是 G5 回归锚**）· **C1–C9** · **T20-1..T20-10** · **五批实现顺序**） | ✅ **v1.1 已通过**（2026-09-24；含 **C10 负数契约**）——★ 评审指定的三件已成章（§3 指定① / §4 指定② / §5 指定③） |

## Phase20.1 渲染层 DPI 缩放（render DPI scaling）（✅ 需求 **v1.3** · ✅ 初步设计 **v1.2 已通过** · ✅ 详细设计 **v1.3 已通过 + 已实施 + 验收通过**，2026-09-24 立项）

★ **Phase 20 的缺口补完**——来源 = Phase 20 详设 **§14.5 的 `RG-1`**（2026-09-24 用户跨屏 / 主屏 125% 实测；★ `RG-1` 与需求稿 §6 的 `R1` 需求条目**不属同一命名空间**）。

**现象**：窗口尺寸 OK · 布局(DIP) OK · 命中(DIP) OK · **绘制 ✗**（150% 下内容只占窗口左上约 2/3）——用户原话「绘制变小了，但 GUI 并没有变小、按键控制仍然在原位」。

★★ **根因 = 设计表述缺口（非实现笔误）**：Phase 20 初设 **§2.1 规则 2**（「`Renderer` / `RenderingBackend` 里见到的恒为**物理像素**」）与 **规则 3**（「两者**只在平台边界**那一条线上互换」）**互相打架**——渲染层**不在平台边界** ⇒ ★★ **「框架内部(DIP) → 渲染层(物理)」这条换算边没有落点**。**实测证据**：`Renderer.cpp` / `PaintContext.cpp` **零 DPI 代码**；`GDIBackend` 直接把几何当像素（`static_cast<LONG>(rect.x)`）；全库 `src/Render/` 的 DPI 代码**只在字体路径**（Phase 20 批五）⇒ **字体已按 DPI 换算而几何未换算 ⇒ 尺度失调**。

★ **元教训（可推广）**：**不要只问「哪个模块知道平台」，还要问「哪个模块第一次要求物理单位」**——转换落点由**单位契约**决定，不由目录名决定（对后续 Linux / OpenGL 直接有用）。

| 文档 | 内容 | 状态 |
|------|------|------|
| [phase20.1-render-dpi-scaling-requirements.md](phase20.1-render-dpi-scaling-requirements.md) | 需求确认（**K1–K4 证据** · **G1–G4 / N1–N5** · **约束 C-1..C-6** · ★ **§3.1 单位不变式 U1–U5**（本子阶段要冻结的链）· **§4 折算量清单**（8 类命令逐条；★ 明确 `Font::size` **不在其列**）· ★ **Q1–Q7** · **A1–A7** · **§8.1 元教训**） | ✅ **v1.3**（2026-09-24）· ★ v1.2 = 初设基线勘察的勘误两条 · ★ **v1.3 = 收尾（术语正名 `RG-1`）** |
| [phase20.1-render-dpi-scaling-preliminary-design.md](phase20.1-render-dpi-scaling-preliminary-design.md) | 初步设计（**B1–B11 代码基线全部带行号**——★ 新勘四条：**B2 调用点实测 3 个**（勘误需求稿的 4）· **B5 `BeginFrame` 的「单行转发」契约已被 Phase 18 冻结** · **B6 命令持有 `std::string` / `Image` ⇒ 否决「复制命令再折算」** · **B10 后端取整口径本就不统一**（`DrawRect` 截断 vs `PushClip` 用 `lround`）。★ **七项定案**：**Q1 落点 = `Renderer` 入口 + 形态 A2**（执行期瞬时转换、**不碰 `CommandBuffer`**）· ★★ **Q2 定案 `Execute(commands, scale = 1.0f)`——与需求稿倾向的 `BeginFrame` 相反**（理由：`BeginFrame` 契约已被 Phase 18 冻结为「单行转发、不存状态」；备选约 3 行可切换）· Q3「认识 `scale` ≠ 认识 DPI」· Q4 三个 helper（**不加 `operator*`**）· Q5 只折几何、**不折 `font.size`** · Q6 无头可测 · Q7 取整口径 = **既有近似**。★ **§2.5 G2 的浮点结构性论证**（`×1.0f` 精确 ⇒ **无需 `scale == 1` 特判**）· **§3.1 改动 △1–△5** · **C-7–C-11 新契约** · O1–O3 · **T20.1-1..5** · 交给详设五件事） | ✅ **v1.2 已通过**（2026-09-24 · 评审零阻塞项；★ v1.2 = 术语正名 `RG-1`） |
| [phase20.1-render-dpi-scaling-detailed-design.md](phase20.1-render-dpi-scaling-detailed-design.md) | 详细设计（**△1–△4 逐文件行级改动** · ★ **B12–B15 详设新增基线**——其中 **B13「零 include 成本」** 与 **B15「零容差新定义」** 合起来意味着本阶段**没有依赖引入成本**。★ **§1.3 对初设的一处修正 + 三处细化**：★★ **修正 A = 实测证伪初设 §9-③**（`scale = 1.5f` 与整数相乘**全部精确**——`1.5 = 3/2`、半整数亦然；真正的非精确在 `1.2f` / `1.1f`）⇒ ★ 并由此得出**更好的测试设计**：「**取奇数输入使期望值落在半整数上**」才能分辨「乘法」与「乘法后取整」，若全取偶数则期望值皆整数、**抓不到偷偷取整**。★ **§3 `scale` 的语义与合法范围冻结**（评审指定 ⑥/⑦）：**本次 `Execute` 调用的执行上下文参数**、**Renderer 不保存**、**不要求 `BeginFrame` 顺序**、**不认识 DPI**；范围 = **finite + positive**，★ **不加运行时检查**（含「为何不加 `assert`」的记录）。★ **§4「100% 零回归」三层表述**（评审指定 ③，冻结）：单值 / 几何字段 = **数学恒等（强）**，整条链 = **工程判断、须四工具链实测**。**§2.5 实现盯防清单 6 条**（① 折 `font.size` ⇒ ×2.25 · ② 默认实参写两遍 ⇒ 编译错误 · ③ 漏折 `width`/`cornerRadius` · ④ 漏折 `PushClip` · ⑤ 改 `CommandBuffer` · ⑥ `PopClip` 未用形参写法）· **§5 契约验证映射 + A4 的三条 grep 判据** · **§6 五用例完整输入/期望**（期望值已按 float32 脚本复算）· §7 三批实现顺序（先能力后行为）· §10 收口清单（含 **O1 记账**）） | ✅ **v1.3 已通过 + 已实施**（2026-09-24 · 评审认可可直接作为实现 checklist；★ v1.3 = 三批落地 + 四工具链 + **验收通过**，见 **§12 实施记录**） |

★ **对 Phase 20 的影响**：**A5（125%/150% 视觉）/ A6（跨屏）验收依赖本子阶段** ⇒ **✅ 已收口（2026-09-24）**：Phase 20 的 **A5 / A6 已随之判为通过**——用户四工具链 ModelProbe 目视：**100% 与改前一致 · 125% / 150% 内容充满窗口 · 跨屏完全正常**。

## Phase21 系统图标 → Image（icon to image）（✅ 需求确认 **v1.1 已通过** · ✅ 初步设计 **v1.2 已通过** · 🚧 详细设计 **v1.0 待评审**，2026-09-25 立项）

**框架缺陷修复队列待做 ③**——审计 `docs/framework-defect-audit.md` **§4 D-3** = `desktopnest-roadmap.md` **§5 G-2**（该表标记为 **desktop 唯一阻断项**）。

★★ **本阶段的精确边界（**K1 + K2** 勘明）**：**不是「框架不能显示图标」，而是「框架拿不到系统图标的像素」**——
`.ico` **文件**走既有 `Decode::DecodeFile` **已经能出图**（WIC 原生支持 ICO，`Decode/ImageDecoder.h:12-14`）；
缺的是**运行时由系统给出的 `HICON` 句柄**（文件类型图标 / 文件夹图标 / 资源图标）——
全库 `GetIconInfo` / `GetDIBits` / `SHGetFileInfo` **零命中**。

★ **定位**：`Decode` 的**第三个来源**（既有两个 = **内存** `DecodeMemory` / **文件** `DecodeFile`）⇒ 目标是**产出既有的 `Image`**：
**零新契约**（`Image` 已是 premultiplied BGRA 值类型）· **零新接缝**（`Decode` 本就由平台层实现）· **渲染侧零改动**。

★ **核心决策 D0**：**`SHGetFileInfo` 这一层放框架还是应用层**——倾向**放框架**（应用层只给路径、一行调用）；备选会让应用层**必须自己 `#include <shellapi.h>`** 并自管句柄。

★ **难点 = Q3 可测性**：`SHGetFileInfo` 依赖 shell ⇒ 内核（`HICON` → `Image`）与外壳（shell 调用）**必须分离**，否则本阶段无法做到**代码级闭环**（沿 Phase 19/20 的「真路径」纪律）。
★★ **初步设计 v1.0（2026-09-26）——★ 先做平台语义实测，再冻结设计**（需求把 **Q1/Q4** 标为「须实测后再冻结」，Q1 是第一优先级 ⇒ 按 skill 条 105 用**独立探针**取证，**零仓库侵入 · 全程无 COM · 无窗口**）：

| 探针 | 结论 |
|---|---|
| **P4** ★★ | **不加 `SHGFI_USEFILEATTRIBUTES` 时，不存在路径 `ret=0` 且 `hIcon=NULL`** ⇒ 该 flag 是「不存在也出图标」的**唯一途径**（**Q4**） |
| **P3** | **已存在路径上，加与不加该 flag 结果逐项相同**（`iIcon` 与 alpha 分布一致）⇒ 加它**无实测保真损失** |
| **P1 / P2** | 该 flag 在「**无 COM / 控制台 / 无 shell 交互**」下可用；**目录有独立 shell 语义**（`iIcon` 3 vs 普通文件 0 vs `.exe` 50） |
| **P5** ★★ | `GetIconInfo` 的 color **恒 32bpp**（11/11）；★ **1bpp mask 的置位数恰好等于 `alpha==0` 的像素数**（355/355 · 456/456 · 339/339），而 alpha **还带中间值**（抗锯齿）⇒ **用 mask 会丢 AA** |
| **P6** ★★ | **`GetDIBits(32bpp, top-down)` 保留 alpha**（自造 `0/64/128/255` 图标往返**逐点精确**）⇒ D5 的绕行风险**排除**；`SMALLICON` = 16×16 |
| **P7** | ★ **空路径 `L""` 不报错**（`ret=1` 且**返回了图标**）⇒ **入口必须自校验** |

★★ **两个由此锁定的定案**：**① Q1 + Q4 解耦** —— `USEFILEATTRIBUTES` **一律使用**（P4 证明非它不可、P3 证明无损失）⇒ **单一路径，不做两层递进**；**② D4 从「要实现一套合成算法」降级为「一个分支」** —— 32bpp 用 alpha、否则用 mask（P5 证明 mask **等价于「是否全透明」且更粗**）。

★ **Q3 的答案比需求预期更好**：**内核与外壳都可无头**（内核用 `CreateIconIndirect` **自造图标**；外壳用真实路径）⇒ 本阶段**可以代码级闭环**。

★★ **与既有文档的冲突（本稿点明）**：`desktopnest-roadmap.md` **§3/§4 判 `SHGetFileInfo`「❌ 不进框架」**，而本稿论证 **R1 + R2 联合迫使「放框架」**——理由是 **§4 判据缺第三问**（「留应用层会不会逼应用层 include Win32 头」），且其 §5 的 **G-2 只登记了内核**（`HICON` → `Image`）⇒ **§4 判据与 §5 缺口登记内部不自洽**。★ **回填建议见初设 §2.9（须授权）**。

★★ **为详设做的前置探针（P9–P13，第二轮独立探针）修正了初设的一条规则并定案 O3**：**① 判据由「位深」改为「alpha 是否携带信息」**——**P10 / P11 实测 `GetIconInfo` 对「无 alpha 的源」一律把 color 归一化为 32bpp 且 alpha ≡ 0**（传入 24bpp / 1bpp 皆然）⇒ 按原判据（32bpp ⇒ 用 alpha）会得到**全透明、不可见**的老式图标 ⇒ **正确判据是「alpha 全 0 ⇒ 用 mask」**。**② 另一条红利**：既然 `GetIconInfo` **归一化为 32bpp** ⇒ **内核无需处理调色板 / 低位深**，「老式图标兼容」由「一套调色板转换」压成「**一个判据 + 一个 mask 分支**」。**③ 排除 `DrawIconEx` 单路径方案**——它对 32bpp 源**直接产出预乘 BGRA**（很诱人），**但对老式图标输出 alpha 全 0** ⇒ 不能作单一路径。**④ P9** 证明多行方向正确（`biHeight = -h` 对）。

★★ **详细设计 v1.0（2026-09-26）**：**算法规格**（内核 11 步 / 外壳 6 步）· ★ **资源与 RAII 三栏冻结**（`UniqueIcon` / `IconBitmaps` / `ScreenDc`，含三条 ownership 事实）· **尺寸校验口径**（★ **不照抄 WIC 四域检查**，理由 = 图标尺寸来自 `GetObject` 的 `BITMAP` 且现实上界极小）· **盯防清单 10 条** · **逐文件改动 △1–△5** · **契约 C1–C12**（★ **C9 不透明度判据** / **C10 `(c*a+127)/255`** / **C11 恒 32bpp + top-down** / **C12 尺寸校验**）· **T21-1..T21-9 逐条输入与期望值**（内核用 `CreateIconIndirect` 自造图标、外壳用**测试 exe 自身路径**与 `GetTempPathW`）· **两批 + 收尾** · **O1–O6**。★ **构建级核实**：**`shell32` 已链**（`CMakeLists.txt:75`）⇒ **零 CMake 改动**；**用例 252 → 261**（+9）· **公共头 92 → 92 而公共 API +1**。

| 文档 | 内容 | 状态 |
|------|------|------|
| [phase21-icon-to-image-requirements.md](phase21-icon-to-image-requirements.md) | 需求确认（**K1–K6 现状勘察**（全部带行号）· **R1–R5** · **D0–D5 决策点** · **N1–N5 非目标** · **Q1–Q4**（★ **Q3 可测性**）· **A1–A5** · ★ §8 如实标注**消费者是未来的**） | ✅ **v1.1 已通过**（2026-09-25 · ★ 评审「通过，可进初步设计」；★ 采纳 **9 条**，含我方 **2 处疏漏**：**A4 口径** · **API 命名撞车**；新增 **N6** / **A6** 两处护栏） |
| [phase21-icon-to-image-preliminary-design.md](phase21-icon-to-image-preliminary-design.md) | **初步设计**：★ **平台语义实测 P1–P8**（独立探针）· **代码基线 B1–B10**（带行号）· **D0 与 `desktopnest-roadmap` §4 判据的冲突裁决** · **Q1+Q4 由 P4/P3 解开耦合**（`SHGFI_USEFILEATTRIBUTES` 一律使用）· **Q2 不做 COM 初始化** · **Q3 内核 / 外壳分离且两层都可无头** · **D4 由 P5 降级为一个分支**（32bpp 用 alpha、否则用 mask）· **D5 实测排除 `GetDIBits` 丢 alpha 的绕行风险** · **资源清单第一版**（含两个 ownership 事实）· **△1–△6 改动分解** · **契约 C1–C8** · **O1–O4** · **T21-1..T21-8** · **A1–A6 落地口径** | 🚧 **v1.0 待评审**（2026-09-26） |
| [phase21-icon-to-image-detailed-design.md](phase21-icon-to-image-detailed-design.md) | **详细设计（实施规格）**：**内核 11 步 / 外壳 6 步算法** · ★ **资源与 RAII 三栏**（`UniqueIcon` / `IconBitmaps` / `ScreenDc`）· **尺寸校验口径**（不照抄 WIC 的四域检查）· **盯防清单 10 条** · **△1–△5 逐文件改动**（含头全文草案）· **契约 C1–C12** · **T21-1..T21-9 逐条逐字节期望** · **两批 + 收尾** · **O1–O6** | 🚧 **v1.0 待评审**（2026-09-26） |

## Window 所有权与生命周期（✅ 初设 v1.1 → 详设 v1.2 **已实施**）

> **独立契约文档**——不属任何 Phase，故不用 `phaseN-*` 命名（阶段由文档头部 / §8 跟踪）。
> **来源**：Phase 12 WindowChrome 实施后 `Application.cpp:92` 断言（测试替身绕过 `Application::Create()`）；**A（测试替身止血）已关闭**，本节为 **B** 范围。

| 文档 | 内容 | 状态 |
|------|------|------|
| [window-ownership.md](window-ownership.md) | **契约主体（初步设计）**：三层归属（对象 / HWND / 注册表）· 三条闭环链（线性全链 + 源码锚点）· **5 条不变量** · 决策 B1–B5 + 关键陷阱（`make_unique` 与 `default_delete` 均无法访问私有成员）· **契约条款原文**（`Release ≠ delete Window`、回收依赖消息泵、断言与容错的双层含义、`PlatformWindowHost` 注释改写）· 验收（含 `static_assert` 编译期契约） | ✅ **v1.1 已实施（2026-09-12）**——契约落地，证据见详设 v1.2（183/183 + 编译期契约 + 静态检查） |
| [window-ownership-detailed-design.md](window-ownership-detailed-design.md) | **详细设计（实施规格）**：逐文件 diff 级改动（`Window.h` 访问权限布局前/后 + `friend` 粒度说明 / `Application.h` 删友元 + 双向访问关系复核表 / `Application.cpp` `Create` 全文 + B1-t 陷阱 / `PlatformWindowHost.h` 注释改写 / 测试 `static_assert` ×3）· 编译期契约测试方案（**双工具链实证** + 中立上下文性质 + 四工具链待验 + 兜底负向探针）· 注释落点清单 · 实施 6 步 · 验收 A1–A6 | ✅ **v1.2 已实施（2026-09-12）**：`ecdi_tests` **183 passed / 0 failed**（MinGW + `-D_DEBUG`，断言层生效）；**A1** 编译期契约（`static_assert` ×3 + 人工反例实测编译失败）/ **A4** 静态检查（`new Window` 代码 1 处、`make_unique<Window>` 代码 0 处）通过；**A2/A6 ✅ 全部通过**——**四工具链**（MinGW / MSVC / Clang / ClangCL）实测运行、**无断言错误**；附带 `ECDI/ECDI开发规范.md:143` 过期措辞（「m_application 指针」→引用）已修；3 处实测偏差（`static_assert` 落点 / 连带过期注释 / 排版）已记录 |

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
