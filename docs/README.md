# ECDI 设计文档索引

> 本文档是 `docs/` 的索引。设计文档随代码提交 git，从 Phase4 起为强制约定（职责确认 / 初步设计 / 详细设计 各阶段文档正常写入本目录）。

## 开发进度（2026-08-25 更新）

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
| Phase 9 | 主题系统（**决策层落地**：StyleField\<T\> D7 契约 + Theme/DefaultTheme + TextStyle 单一视觉真相 + Button/TextBox/Panel 迁移 + cornerRadius 消费） | ✅ 2026-08-25 |

### 🔄 当前

- 无进行中阶段（Phase 6.2 已收尾提交；下一阶段 Phase 9.5 收尾补充——职责确认待启动）

### 🔲 未来

- **Phase 9.5 收尾补充**：局部更新/裁剪系统（Clip/Dirty Region）+ LinearLayout 抽象 + WM_MOVE 场景 + Hover/MouseEnter/Leave + Shortcut System/键盘入口统一/InputManager（v1.0 前择机完成，详见 roadmap-deferred.md）
- **Phase 10** v1.0 收尾（转库静态库化 + API 审查；开源待定）

### 📋 技术债务（记账）

> 完整延期排期见 **[roadmap-deferred.md](roadmap-deferred.md)**（全部延期项 → 阶段总表）。

| 债务 | 位置 | 解决时机 |
|------|------|---------|
| Invalidate 解耦（两层结构 Internal+API） | TextBox 编辑操作 | Phase 7 API 审查 |
| 文本裁切用字符串截断（O(n²)）——8.5.2 多行版仍逐行截断 | TextBox::OnPaint | Phase 9.5 局部更新/裁剪系统（PushClip，代码 TODO 已标） |
| **输入层抽象（TextInputInterface/TextInputContext）**——5.6/7.1.3 的 UpdateTextInputCaret + CaretGeometry 是半抽象（Window 中介），完整契约层 + 跨平台 adapter 待转库前 | Window::UpdateTextInputCaret | Phase 10 转库前评估 |
| **DPI 感知**——框架当前无 DPI 缩放，IME 坐标用逻辑像素 | 全局 | Phase 10 评估 |
| 键盘入口不对称（OnKeyDown 走 Window / OnKeyUp+CharInput 直派，3 入口）——已回顾保持现状（Tab 拦截必需 Window），未来全局快捷键时统一 | Application | 未来全局输入需求出现时（详见 phase5-architecture-review.md） |
| 多窗口焦点语义（应用级 vs 窗口级焦点） | Window/Application | Phase 10 平台抽象收尾时评估（详见 phase5-architecture-review.md） |
| 编辑操作可见性（临时 public） | TextBox | Phase 10 API 审查 |
| **RenderingBackend::DrawText 命名与 Win32 宏冲突**（skill 13 历史遗留违反——现用防御性 undef 兜底，用户零负担） | RenderingBackend/RecordingBackend/GDIBackend | Phase 10 v1.0 API 审查改名（如 DrawTextContent）
| **光标色未主题化**（TextBox 光标 Color::Black 硬编码——Phase 9 迁移时 YAGNI 未纳入） | TextBox::OnPaint | Phase 9+（需求出现时进 TextStyle/TextBoxStyle） |
| **IME 结果 WM_CHAR 吞字符 pending 计数**（若某 IME 结果不走 WM_CHAR 会残留吞后续字符——注释已记） | Win32PlatformWindow | 真实输入法兼容性需求出现时 |

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

## Phase9 主题系统（✅ 已实现，2026-08-25）

| 文档 | 内容 | 状态 |
|------|------|------|
| [phase9-theme-system-requirements.md](phase9-theme-system-requirements.md) / [preliminary](phase9-theme-system-preliminary-design.md) / [detailed](phase9-theme-system-detailed-design.md) | Phase 9 三件套（StyleField D7 契约 + Theme/DefaultTheme + TextStyle 单一真相 + 控件迁移 + cornerRadius 消费） | ✅ 已实现（2026-08-25，v1.0-v1.4 四轮 GPT 评审收敛） |

## 文档约定

- 命名：`phaseX.Y-<module>-<type>.md`（子阶段编号 + 模块名 + 阶段类型；2026-08-25 全量规范化：Phase 5/6/7 按内容编号对齐，如 `phase5.3-button-requirements.md`、`phase6.2-checkboxradio-detailed-design.md`、`phase7.5-callback-requirements.md`；阶段级评审文档保留 `phaseN-<module>.md`）
- 五阶段法：职责确认 → 初步设计 → 详细设计 → 实现 → 测试，设计文档在实现前评审通过
- 文档内附修订记录（v1.0 → v1.1...），实现中发现的与文档出入必须回写
- 所有文档带 UTF-8 BOM（`ef bb bf`——MSVC 源码同规范）
