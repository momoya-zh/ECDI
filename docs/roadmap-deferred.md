# ECDI 延期事项排期总表（roadmap-deferred）

> 状态：v1.2（2026-09-11）｜确认排期
> 作用：汇总全部"记账/延期/TODO/推迟"决策 → 对应实现阶段。README 技术债表的完整展开。
> ⚠️ **已知缺口（2026-09-11 声明）**：本表内容主体停留在 v1.1（2026-08-15），**9.6 / 9.7 / 9.8 / 10 / 11 / 12 的延期项尚未批量补记**（含 9.6 的 Fade→PushOpacity / DrawArc / PushTransform / DoubleClick / Dirty Region 等记账项）。v1.2 仅补登记抗锯齿一项（#29）——**不要把本表当作完整清单**。

## 1. Phase 7 平台抽象 + 测试体系（v1.0 转库前硬性前置；2026-08-15 裁决拆 7.1/7.2）

### 1.1 Phase 7.1 平台抽象

| # | 延期项 | 来源 | 备注 |
|---|---|---|---|
| 1 | Window 平台抽离（PlatformWindow + Backend 注入） | MEMORY.md 正式决策 | 主任务；Platform×Backend 二维独立 |
| 2 | 输入层抽象（TextInputInterface/TextInputContext） | README 技术债 | UpdateTextInputCaret 半抽象 → 契约层 + Win32IME adapter（与 Renderer→Backend 同构） |
| 3 | Imm/caret 平台代码下沉 | 5.6 文档 | NotifyIMEComposition 的 Imm → PlatformWindow::SetIMECompositionPosition；WindowMessageHandler → Win32PlatformWindow |
| 4 | 编辑操作可见性（TextBox 临时 public → 两层结构） | README 技术债 | 公开高层 API（InsertText/Clear/SetCaret）+ protected 底层原语 |
| 5 | Invalidate 两层结构（Internal + API 解耦） | README 技术债 | 批量编辑解耦 |
| 6 | 多窗口焦点语义评估（应用级 vs 窗口级，R3） | 架构回顾 | 全局快捷键/IME 全局态需要时 |
| 7 | 系统 caret 多窗口场景 | 5.6 记账 | 懒创建 + 单窗口单 caret 评估 |
| 8 | DPI 感知评估 | README 技术债 | 框架当前无 DPI 缩放，IME 坐标逻辑像素 |

### 1.2 Phase 7.2 测试体系

| # | 延期项 | 来源 | 备注 |
|---|---|---|---|
| 28 | 测试体系（单元测试框架 + 历史欠账补测） | 5.5.2 详细设计 P8（评审 要求②"不彻底放弃断言"） | 补 Selection 单元测试（选中 cd + 输入"中" → ab中e 等外部行为断言）；Phase 10 转库前测试保障 |

## 2. Phase 7.5 事件回调（裁决：插 7-8 之间）

| # | 延期项 | 来源 | 备注 |
|---|---|---|---|
| 9 | std::function 回调注册（Button::SetOnClick / CheckBox::SetOnCheckedChanged 等） | 用户 2026-08-15 | 继承 override 基座 + 回调便利层并存；Phase 7 解耦后做（稳定接口） |

## 3. Phase 8 渲染增强（能力层）

| # | 延期项 | 来源 | 备注 |
|---|---|---|---|
| 10 | 文本裁切裁剪区域（PushClip/clipRect 替代字符串截断） | TextBox TODO | O(n²) 截断 → 裁剪区域 |
| 11 | 焦点框虚线框 | 5.4 文档 | 现 DrawRect 两命令实线 |
| 12 | 渲染浮点化/亚像素 | 5.3/5.4 决策 | DrawTextContent int 参数等 |
| 13 | 6.2 CheckBox/Radio（勾/圆） | 用户 2026-08-15 | 消费 DrawLine/DrawRoundedRect；设计已定稿存档 |

## 4. Phase 8.5 文本系统 2.0（裁决：插 8-9 之间）

| # | 延期项 | 来源 | 备注 |
|---|---|---|---|
| 14 | IME 组合串内嵌（B 方案） | 5.6 文档 | 6 问题论证（组合串/Selection/Backspace/Delete/取消/候选切换） |
| 15 | 剪贴板子系统（Ctrl+A/C/V/X + Copy-Cut-Paste） | 5.5.2 推迟 | 评审 用户预期一致性（Ctrl+A 是剪贴板时代入口） |
| 16 | TextBox 多行/滚动 | 5.5 记账 | EnsureCaretVisible/自动滚动 |
| 17 | TextBox 双击/三击/选词/选行（m_dragSelecting） | 5.5.2 记账 | |
| 18 | 光标闪烁 | TextBox 记账 | m_showCaret 闪烁状态另立 |
| 19 | AutoSize / GetPreferredSize | 架构债务 | v0.1 不做（YAGNI），文本系统 2.0 评估 |
| 20 | Undo-Redo | 文本系统 2.0 | |
| 26 | SetFont()（TextWidget m_font 一行接入） | 5.1-5.3 预留 | 用户 2026-08-15 定入 8.5 |

## 5. Phase 9 主题系统（决策层）

| # | 延期项 | 来源 | 备注 |
|---|---|---|---|
| 27 | 主题系统（样式硬编码 → ApplyTheme/SetStyle） | 路线图 | 消费 Phase 8 能力 |

## 6. Phase 9.5 收尾补充（裁决：插 9-10 之间；原触发式项，v1.0 前择机完成）

| # | 延期项 | 来源 | 备注 |
|---|---|---|---|
| 21 | 局部更新/裁剪系统（Clip/Dirty Region/Partial Redraw） | Phase 4 决策 28 | 管线 Clip 已随 R1 落地（2026-08-28 重做）；Dirty Region 仍记账 |
| 22 | LinearLayout 抽象 | 6.1 契约 | ✅ 评估关闭（2026-08-28）：确认不抽——抽象后代码不减反增、第三用例未出现；第三布局立项时重启 |
| 23 | WM_MOVE 场景（移动中候选窗错位） | 5.6 记账 | ✅ 评估关闭（2026-08-28）：确认不做——模型 B 已消除主要错位（组合串自绘随窗）、场景极罕见（打字与拖窗互斥）、EXITSIZEMOVE 归位足够 |
| 24 | Hover / DoubleClick / MouseEnter / Leave | 5.4 架构债务 | ✅ R4 已实现（2026-08-27，commit 7d97d6c/0a11c2d）；DoubleClick 记账 |
| 25 | Shortcut System / 键盘入口统一 / InputManager | 架构回顾 R2 + 触发条件 | ✅ 评估关闭（2026-08-28）：三子项全部不做——键盘入口不对称=设计决策（Tab 导航只需 KeyDown）；无全局快捷键消费场景；InputManager 收益未证。重启：框架级快捷键消费场景出现 |

## 7. Phase 8.6 渲染抗锯齿（新立——2026-09-11）

| # | 延期项 | 来源 | 备注 |
|---|---|---|---|
| 29 | 抗锯齿升级（圆角/圆弧覆盖度） | `phase9.5-alpha-primitive-detailed-design.md` §4「明确不做（YAGNI）」 | ✅ **已实现（2026-09-11）**——三件套齐备（`docs/phase8.6-render-antialiasing-{requirements,preliminary-design,detailed-design}.md`，详设 v1.4），`ecdi_tests` **174/174 通过**（158 既有零回归 + 15 AA 新增 + 1 ModelProbe 回归）；**正式修订 9.5「约束 2（圆角无抗锯齿）」**——改为「alpha 合成与几何抗锯齿正交」；两层拆分（覆盖度生成 ⇄ 形状装配）+ 掩码按 radius 缓存 + `PatchSurface` 复用；公共 API 与全部控件零改动（消费方 Button/CheckBox/Panel/ProgressBar/Radio/TextBox/焦点框自动获益）。斜线（`DrawLine` AA）**另起**——`DrawLine` 连 alpha 路径都没有 |

## 7.5 Phase 15 滚动容器（新立——2026-09-17）

| # | 延期项 | 来源 | 备注 |
|---|---|---|---|
| 30 | **`HitTest` 父边界约束的通用化**（K4：子节点越界仍可命中，波及 hover/按下/移动/滚轮/拖入 5 条路径） | Phase 15 需求稿 §1.2 K4/K5 | Phase 15 以 **`ClipsChildren()` 门控**（需裁剪的容器声明式开启）局部收口，**不**通改全部容器命中语义（零回归风险无法在无消费者场景验证）。**已发现的真实实例（2026-09-18）**：`CaptionBar` 窗口宽 < 138px 时 `minButton.x < 0` 越界仍可命中（`CaptionBar.cpp:97`，全库无 `WM_GETMINMAXINFO`）——该缺陷**已经存在**，Phase 15 不处理。重启：第二个非滚动消费者（ListBox / 裁剪面板）出现时，把默认值从 false 翻成 true |
| 31 | **焦点滚入视口**（`PageUp/PageDown/Home/End` + Tab 聚焦时自动滚动） | Phase 15 需求稿 §4 | 与滚轮**驱动源不同**（焦点驱动 vs extent 驱动），未纳入 R1–R5。重启：ScrollView 内出现可聚焦控件集合时 |
| 32 | **平滑 / 惯性 / 弹性滚动** | Phase 15 需求稿 §4 | Phase 9.6 动画系统可支撑（`AnimationManager` per-Window tick），属体验增强 |
| 33 | **虚拟化（按需生成子控件）** | Phase 15 需求稿 §4 | 需内容模型抽象（数据源 → 可视行），YAGNI；大列表性能真实成为瓶颈时再评 |
| 34 | **TextBox 横向滚动条** | Phase 9.5 R1 详设 L102（v1.0 记账） | Phase 15 的 R2 只做**容器级**滚动条；TextBox 滚动是 **caret 驱动**（与 extent 驱动不同源）——若要给 TextBox 加条，须让 TextBox 内部视图接入 `ScrollBar`，是独立议题（Phase 15 §4 已显式划出） |
| 35 | **嵌套滚动停递**（内层滚到边界才向父级冒泡） | Phase 15 需求稿 D7（v1.1 降级） | 嵌套消费者**实测 = 0**（全库 `ScrollView` / `ScrollBar` 零实现）⇒ YAGNI。三种机制各有代价——① 改虚函数签名触及 5 个实现者（`Widget` / `TextBox` / `ModelListPanel` / `EventRouter` / `Application`）② Event 带 consumed **污染 Event 原则** ③ Application 认识容器（耦合）；代价表见 Phase 15 需求稿 D7。重启：出现第二个真实嵌套消费者 |
| 36 | **横向滚轮**（`WM_MOUSEHWHEEL`） | Phase 15 需求稿 K15/K16 · D8 | 平台层**未翻译** `WM_MOUSEHWHEEL`（`WindowMessageHandler.cpp:162` 只有 `WM_MOUSEWHEEL`）+ `MouseEvent` **无修饰键** ⇒ 横向只能靠滚动条拖动 + 显式 API。做须先扩平台翻译 + Event 维度（轴 / 修饰键） |
| 37 | **负向内容**（子控件坐标 < 0 的包围盒） | Phase 15 需求稿 D6 | extent 定义钳 0（`max(0, max(x + width))`）——布局系统（`VerticalLayout`）从 0 起算，负坐标不是正常布局产物 |

## 8. 修订记录

- v1.8（2026-09-18）Phase 15 需求稿 v1.1 回写同步：**修正 §7.5 编号撞号（v1.0 引入）**——原用 28–32，与 §1.2 的 `28`、§7 的 `29` 重复，统一顺延为 **30–34**；条 30 补 **`CaptionBar` 窄窗越界实例**（窗口宽 < 138px，既有缺陷）；新增条 35 嵌套滚动停递（D7 v1.1 降级）· 条 36 横向滚轮（K15/K16 实测：平台无翻译 + Event 无修饰键）· 条 37 负向内容（D6 extent 钳 0）。
- v1.7（2026-09-17）新增 §7.5 Phase 15 延期项（①②③④⑤ 五条）：K4 命中约束通用化（以 `ClipsChildren()` 门控局部收口）· 焦点滚入 · 平滑/惯性 · 虚拟化 · TextBox 横向滚动条（Phase 9.5 R1 记账的边界澄清——仅指 TextBox，非容器级）。
- v1.0（2026-08-15）总表定稿：全部延期项分组到阶段（7/7.5/8/8.5/9/9.5）；确认 SetFont 入 8.5、21-25 入 9.5。
- v1.1（2026-08-15）**Phase 7 拆 7.1/7.2**（用户决策）：测试体系入 7.2（新增 #28——5.5.2 P8 承诺的 Selection 单元测试补测；Phase 10 转库前测试保障）。
- v1.2（2026-09-11）新增 **§7 Phase 8.6 渲染抗锯齿**（条目 #29）——把 `phase9.5-alpha-primitive-detailed-design.md` §4 的「抗锯齿升级」正式纳入总表并立项（该延期项此前只存在于 9.5 详设清单，总表零记录，属登记遗漏）。本次**仅补登记 AA 一项**；9.6 / 9.7 / 9.8 / 10 / 11 / 12 的延期项批量回填仍待单独授权（见头部已知缺口声明）。
