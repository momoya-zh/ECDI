# ECDI 设计文档索引

> 本文档是 `docs/` 的索引。设计文档随代码提交 git，从 Phase4 起为强制约定（职责确认 / 初步设计 / 详细设计 各阶段文档正常写入本目录）。

## 开发进度（2026-08-15 更新）

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
| Phase 7.1.1 | PlatformWindow 骨架（Window 零 Win32：PlatformWindow/PlatformWindowHost 契约 + Win32PlatformWindow 实现；翻译器/IME 平台代码下沉） | ✅ 2026-08-15 |
| Phase 7.1.2 | 翻译器契约改造（翻译器迁 Platform/Win32/ + 构造 Host& 派发；WM_IME 移出方案 B——翻译器纯翻译；Platform 零 Application/零 Window.h） | ✅ 2026-08-15 |
| Phase 7.1.3 | 输入层抽象（CaretGeometry{ rect, visible } 文本插入点模型升级——光标不是点是矩形；kCaretWidth 同源；CreateCaret 尺寸来自 rect） | ✅ 2026-08-16 |
| Phase 7.1.4 | Backend 注入（决策 35 代价解决：Window 持 unique_ptr\<RenderingBackend\> + unique_ptr\<TextMeasurer\>；GDIBackend 拆类 + GDITextMeasurer；RenderServices bundle + 工厂；PlatformRenderContext 句柄注入；Window.h 零具体后端零 Windows.h） | ✅ 2026-08-16 |

### 🔄 当前

- **Phase 7 平台抽象 + 测试体系**——7.1.4 完成；**7.1.5 Application 解耦评估**（回看暴露面再决定）

### 🔲 未来

- **Phase 6.2 CheckBox/Radio（延期 Phase 8 后，2026-08-15 决策）**：状态控件 + RadioGroup 同父互斥；勾/圆依赖 Phase 8 渲染能力，不做填充版——设计已定稿存档（phase6-checkboxradio-*.md）
- **Phase 7** 平台抽象 + 测试体系（**7.1 PlatformWindow + Backend 注入**，输入层抽象 TextInputInterface 随本阶段；**7.2 测试体系**——补 Selection 等历史单元测试欠账）——**v1.0 转库前必须完成**
- **Phase 7.5 事件回调（2026-08-15 用户定：插 7-8 之间）**：std::function 回调注册 API（Button::SetOnClick / CheckBox::SetOnCheckedChanged 等），继承 override 基座 + 回调便利层两套并存——**Phase 7 解耦后做**（回调 API 依赖稳定接口）
- **Phase 8** 渲染增强（能力层：AlphaBlend + DrawRoundedRect + DrawLine + DrawImage）——**6.2 CheckBox/Radio 消费其勾/圆能力**
- **Phase 8.5 文本系统 2.0（2026-08-15 用户定：插 8-9 之间）**：IME 组合串内嵌 + 剪贴板子系统（Ctrl+A/C/V/X + Copy-Cut-Paste）+ TextBox 多行/滚动/双击/光标闪烁 + Undo-Redo + AutoSize 评估 + SetFont
- **Phase 9** 主题系统（决策层，消费 Phase 8；样式演进 ApplyTheme/SetStyle）
- **Phase 9.5 收尾补充（2026-08-15 用户定：插 9-10 之间）**：局部更新/裁剪系统（Clip/Dirty Region）+ LinearLayout 抽象 + WM_MOVE 场景 + Hover/MouseEnter/Leave + Shortcut System/键盘入口统一/InputManager（v1.0 前择机完成，详见 roadmap-deferred.md）
- **Phase 10** v1.0 收尾（转库静态库化 + API 审查；开源待定）

### 📋 技术债务（记账）

> 完整延期排期见 **[roadmap-deferred.md](roadmap-deferred.md)**（全部延期项 → 阶段总表）。

| 债务 | 位置 | 解决时机 |
|------|------|---------|
| Invalidate 解耦（两层结构 Internal+API） | TextBox 编辑操作 | Phase 7 API 审查 |
| 文本裁切用字符串截断（O(n²)） | TextBox::OnPaint | Phase 8 渲染增强（PushClip/clipRect，代码 TODO 已标） |
| IME 组合串内嵌（B 方案）——5.6 只做候选窗跟随，组合串不内嵌（I1 决策 A） | TextBox 编辑系统 | 文本系统 2.0（未来） |
| **输入层抽象（TextInputInterface/TextInputContext）**——5.6 的 UpdateTextInputCaret 是半抽象（Window 中介），Phase 7 抽成契约层（Framework 坐标 Rect）+ Win32IME adapter（Client/IMM 内部转换），与 Renderer→Backend 同构（GPT 建议，YAGNI 等 Phase 7 第二锚点） | Window::UpdateTextInputCaret | Phase 7（随 PlatformWindow 下沉） |
| **DPI 感知**——框架当前无 DPI 缩放，IME 坐标用逻辑像素 | 全局 | Phase 7 评估 |
| 键盘入口不对称（OnKeyDown 走 Window / OnKeyUp+CharInput 直派，3 入口）——已回顾保持现状（Tab 拦截必需 Window），未来全局快捷键时统一 | Application | 未来全局输入需求出现时（详见 phase5-architecture-review.md） |
| 多窗口焦点语义（应用级 vs 窗口级焦点） | Window/Application | Phase 7 平台抽象时评估（详见 phase5-architecture-review.md） |
| 编辑操作可见性（临时 public） | TextBox | Phase 7 API 审查 |

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
| [phase5-text-requirements.md](phase5-text-requirements.md) / [preliminary](phase5-text-preliminary-design.md) / [detailed](phase5-text-detailed-design.md) | 5.1 文本系统三件套（Font 纯描述 / TextMeasurer 独立接口 / DrawText 命令管线） | ✅ 已实现（2026-08-12） |
| [phase5-label-requirements.md](phase5-label-requirements.md) / [preliminary](phase5-label-preliminary-design.md) / [detailed](phase5-label-detailed-design.md) | 5.2 Label 三件套（第一个文本消费者） | ✅ 已实现（2026-08-13） |
| [phase5-button-requirements.md](phase5-button-requirements.md) / [preliminary](phase5-button-preliminary-design.md) / [detailed](phase5-button-detailed-design.md) | 5.3 Button 三件套（TextWidget 抽取 + 居中文本） | ✅ 已实现（2026-08-13） |
| [phase5-interaction-requirements.md](phase5-interaction-requirements.md) / [preliminary](phase5-interaction-preliminary-design.md) / [detailed](phase5-interaction-detailed-design.md) | 5.4 交互基础设施三件套（Invalidate/Capture/Focus/Tab/按下态） | ✅ 已实现（2026-08-13） |
| [phase5-textbox-requirements.md](phase5-textbox-requirements.md) / [preliminary](phase5-textbox-preliminary-design.md) / [detailed](phase5-textbox-detailed-design.md) | 5.5 TextBox 三件套（码点编辑/光标/点击定位/裁切） | ✅ 5.5.1 MVP 已实现（2026-08-13） |
| [phase5-selection-requirements.md](phase5-selection-requirements.md) / [preliminary](phase5-selection-preliminary-design.md) / [detailed](phase5-selection-detailed-design.md) | 5.5.2 Selection + 修饰键三件套 | ✅ 已实现（2026-08-14） |
| [phase5-ime-requirements.md](phase5-ime-requirements.md) / [preliminary](phase5-ime-preliminary-design.md) / [detailed](phase5-ime-detailed-design.md) | 5.6 IME 候选窗跟随三件套（I1-I5 / P1-P5 / v1.0.4：系统 caret + IMM 双通道，客户区坐标语义） | ✅ 已实现（2026-08-15） |
| [phase5-architecture-review.md](phase5-architecture-review.md) | Phase 5 收尾架构回顾（输入责任分布 + InputManager YAGNI 评估 + R1 冗余修复/R2-R4 记账） | ✅ 已实现（2026-08-15） |

## Phase6 布局系统完善（✅ 6.1 完成，2026-08-15；6.2 延期 Phase 8 后）

| 文档 | 内容 | 状态 |
|------|------|------|
| [phase6-horizontallayout-requirements.md](phase6-horizontallayout-requirements.md) / [preliminary](phase6-horizontallayout-preliminary-design.md) / [detailed](phase6-horizontallayout-detailed-design.md) | 6.1 HorizontalLayout 三件套（Layout 边界原则 / 4 决策点 / diff 同构 + 10 条设计契约） | ✅ 已实现（2026-08-15） |
| [phase6-checkboxradio-requirements.md](phase6-checkboxradio-requirements.md) / [preliminary](phase6-checkboxradio-preliminary-design.md) | 6.2 CheckBox/Radio（StateWidget 契约 6 条 + 勾/圆渲染约束） | ⏸️ 延期 Phase 8 后（2026-08-15 决策，设计定稿存档） |

## 文档约定

- 命名：`phaseN-<module>-<type>.md`（如 `phase3-layout-design.md`）
- 五阶段法：职责确认 → 初步设计 → 详细设计 → 实现 → 测试，设计文档在实现前评审通过
- 文档内附修订记录（v1.0 → v1.1...），实现中发现的与文档出入必须回写
