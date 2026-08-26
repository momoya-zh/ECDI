# Phase 9.5 收尾补充 职责确认

> 状态：v1.1（2026-08-26）｜v1.1 = GPT 评审整合（9.5/9.6 边界明确化 + R4 前置定位 + R1 职责修正）
> 相关：roadmap-deferred.md（延期事项排期总表 §6——5 项原触发式/记账项）/ phase4-renderer-design.md（决策 28 局部重绘）/ phase6.1-horizontallayout-requirements.md（Layout 边界原则）/ phase5.6-ime-requirements.md（WM_MOVE 记账）/ phase5.4-interaction-requirements.md（hover 架构债务）/ phase5-architecture-review.md（R2 键盘入口 + InputManager 评估）/ 2026-08-26 动画讨论记录（9.6 立项意向）

## 0. 阶段定位：9.5 = 收尾基础设施，9.6 = 动效系统

> v1.1 明确（GPT 评审）：Phase 9.6 动效系统已立项，**9.5 与 9.6 边界必须分开**——不把 9.6 内容塞进 9.5，也不因 9.6 把 9.5 硬凑成"动画前置阶段"。

```text
Phase 8 渲染能力 → Phase 9 主题系统 → Phase 9.5 收尾基础设施 → Phase 9.6 动画系统 → Phase 10 v1.0
```

- **Phase 9.5 = 收尾基础设施 / 架构债务清理**：5 项独立技术债（Rendering/Layout/Platform-IME/Input/Application），各自评估落地或记账——保持"小而杂"
- **Phase 9.6 = GUI 动效系统 / 动画能力层 + 控件动效消费**：独立系统，职责确认另行评价（2026-08-26 立项意向已记录）
- **R4 是两者的正式衔接点**：9.5 产生 Hover 状态变化事实，9.6 消费做视觉过渡——R4 是 9.6 的**前置交互基础设施**，9.6 是 R4 的**第一个重要消费者**

## 0.1 背景与目标

Phase 9.5 = **v1.0 前的收尾补充**：5 个原"触发式 / 记账 / 推迟"项统一排到 9-10 之间择机完成。它们没有共同主题，是**独立小模块的集合**——每个按自身触发条件评估后落地或继续记账。本阶段是唯一"允许单项评审后判定不做的阶段"（YAGNI 标尺直接应用）。

另：**Phase 9.6 动画（2026-08-26 立项意向）** 的 hover 过渡消费依赖本阶段 R4（Hover/MouseEnter/Leave）——9.6 排在 9.5 之后，职责确认另行评价。

## 1. 范围总览

| # | 模块 | 来源 | 触发条件 | 建议顺序 |
|---|---|---|---|---|
| R1 | 局部更新/裁剪系统（Clip/Dirty Region/Partial Redraw） | Phase 4 决策 28 | 原"真实瓶颈出现时"，现排 9.5 | ② |
| R2 | LinearLayout 抽象 | 6.1 契约 | 原"Wrap/Grid/Flex 出现时" | ④（评估） |
| R3 | WM_MOVE 场景（移动中候选窗错位） | 5.6 记账 | 现排 9.5 | ③ |
| R4 | Hover / MouseEnter / Leave | 5.4 架构债务 | 现排 9.5；**9.6 前置交互基础设施**（v1.1：9.6 首个消费者） | ① |
| R5 | Shortcut System / 键盘入口统一 / InputManager | 架构回顾 R2 | 原"未来全局输入需求出现时" | ⑤（评估） |

## 2. R1 局部更新/裁剪系统（Clip/Dirty Region/Partial Redraw）

### 职责边界（v1.1 修正——Clip 栈消费层，非 Dirty Region 系统）
- **目的**：① **建立 Widget 绘制阶段的 Clip 生命周期规则**——PushClip/PopClip（Phase 8 能力）在 Paint 管线中的配对契约（绘制越界防护）；② **TextBox 文本裁切迁移到 Clip**（消化 8.5.2 O(n²) 字符串截断债务，代码 TODO 已标）
- **定位**：R1 是 **Clip 栈机制的消费层**（Phase 8 提供能力 → 9.5 建立使用规则），**不是完整的 Dirty Region 系统**（GPT v1.1 修正）
- **范围**：绘制管线 Clip 规则 + TextBox 裁切改造
- **明确不做（YAGNI）**：Dirty Region 区域追踪（Invalidate 保持全窗口 dirty）、脏区像素级追踪/多区域合并/复杂 InvalidateRect 语义

### 决策点
- **d1 绘制阶段 Clip 规则形态**：Paint 管线统一 PushClip(自身边界)（嵌套交集）vs 控件自行负责——倾向 **统一管线规则**（防越界绘制），但**具体形态留待详细设计再审**（嵌套交集语义 + 影响面广——GPT 提示详细设计阶段认真审）
- **d2 TextBox 裁切**：PushClip 替代字符串截断——倾向 **随 9.5 一起做**（已记账债务 + Phase 8 PushClip 已就绪）
- **d3 Invalidate 语义**：保持全窗口布尔，不引入区域追踪——**确定**（YAGNI，性能瓶颈未实证）

## 3. R2 LinearLayout 抽象

### 职责边界
- **目的**：VerticalLayout/HorizontalLayout 已同构（6.1 diff 同构 + 10 条设计契约）——是否抽方向参数化基类
- **明确不做**：Wrap/Grid/Flex 布局（触发条件未出现）

### 决策点
- **d1 是否真抽**（YAGNI 标尺）：两个子类各 ~50 行、diff 极小；第三用例（Wrap/Grid/Flex）未出现——倾向 **暂缓不抽**（抽象收益边际，记账；第三布局出现时再抽）
- **d2 若抽**：接口形状 = 方向枚举（Axis）+ 共享 Arrange 逻辑——初步设计细化，不在此展开

## 4. R3 WM_MOVE 场景

### 职责边界
- **目的**：窗口移动中 IME 候选窗错位（5.6 记账）
- **现状**：WM_EXITSIZEMOVE 已销毁+重建文本插入点（IME 归位，8.5.1 落地）——缺的是**移动过程中**的行为
- **范围**：平台层处理（Win32PlatformWindow），框架层零感知
- **明确不做**：移动中实时跟随（复杂且场景罕见——IME 组合中拖窗）

### 决策点
- **d1 处理位置**：PlatformWindowHost 新增 OnMove 回调（框架契约）vs Win32PlatformWindow 内部自闭环——倾向 **平台层内部处理**（框架契约零改动，平台细节不渗透）
- **d2 方案**：移动结束后归位（现状已够）vs 移动中实时更新——倾向 **现状保留 + 记账**（EXITSIZEMOVE 归位已覆盖主要场景；评审确认后本项可能判定"记账不做"）

## 5. R4 Hover / MouseEnter / Leave

### 职责边界（v1.1 明确——9.6 前置输入，只产生事实）
- **目的**：产生可靠的 **Hover 状态变化事实**（Enter/Leave 通知）——**9.6 动效系统的前置交互基础设施**
- **职责边界（写死）**：**不负责视觉过渡、不负责动画、不负责 Hover Style**——只负责 Hover 目标状态追踪与 Enter/Leave 事实通知（Event 原则：轻量、只表示"已发生的事实"，语义判断推迟消费者）
- **范围**：事件形态 + 派发驱动
- **明确不做**：视觉过渡/动画（归 9.6）；MouseEnter/Leave 回调注册 API（SetOnMouseEnter——第一个用例不需要，记账）；Enter/Leave 的 Bubbling（目标专属语义）

### 决策点
- **d1 事件形态**：Widget 虚方法 OnMouseEnter()/OnMouseLeave()——倾向采纳（状态变化事实，与 MouseMove 移动事实区分；Widget 层响应即可，Event 类型记账）
- **d2 派发驱动**：Application::OnMouseMove 跟踪 hover 目标——HitTest → 比较 m_hoverWidget（非拥有指针）→ 目标变化 → old 收 OnMouseLeave / new 收 OnMouseEnter
- **d3 捕获期间语义**：捕获时（Down→Up）hover 目标跟随捕获者 vs 冻结——倾向 **冻结**（按下移出不触发 Leave 视觉闪烁；简单确定）
- **d4 消费方（正式依赖）**：**9.6 动画 = R4 的第一个重要消费者**（hover 过渡）——本阶段只产事件不消费；9.6 职责确认时此依赖为硬前置

## 6. R5 Shortcut System / 键盘入口统一 / InputManager

### 职责边界
- **目的**：全局快捷键（Ctrl+S 等）——Application 级注册；键盘入口统一评估
- **现状**：键盘入口 3 处不对称（OnKeyDown 走 Window::HandleKeyDown / OnKeyUp+CharInput 直派）；5.5 架构回顾结论 = 保持现状（Tab 拦截必需 Window），未来全局快捷键时统一
- **明确不做**：InputManager 独立类（无独立职责）；快捷键冲突解决框架（先简单优先级）

### 决策点
- **d1 Shortcut 注册 API**：Application::RegisterShortcut(KeyCode+modifier, callback)——倾向采纳（全局语义自然归属 Application；KeyDown 先查快捷键表、未命中再派发焦点控件）
- **d2 冲突优先级**：焦点控件优先 vs 全局优先——倾向 **焦点控件优先**（TextBox Ctrl+C/V/X/A 先消费，未处理才走全局——避免快捷键吃掉编辑键）
- **d3 键盘入口统一**：回顾结论保持现状（Tab 拦截必需 Window）——倾向 **不统一**（无新需求支撑，YAGNI；记账）
- **d4 InputManager**：不引入（快捷键表放 Application 内即可）——倾向记账

## 7. 依赖关系

- **R4 → 9.6 动画（正式依赖，v1.1 定位升级）**：R4 产生 Hover 状态变化事实 → 9.6 消费做视觉过渡——9.6 的 hover 动效以 R4 为**硬前置**
- **R1 → TextBox 裁切债务**：PushClip 消化 O(n²)（Phase 8 能力已就绪）
- **R3 独立**（平台层自闭环）；**R2/R5 评估型**（可能判定记账）

## 8. 验收标准（后续阶段细化）

- R4：Button hover 进入/离开视觉变化可观察（9.6 消费前用 demo 验证）；MouseMove 目标切换正确
- R1：TextBox 长文本无裁切错误（PushClip 区域断言）；子控件越界绘制被裁剪
- R3：窗口移动后 IME 候选窗归位正确（回归）
- R2/R5：评审结论（做/记账）记录在案

## 9. 修订记录

- v1.0（2026-08-26）初稿：5 项范围 + 决策点 + 倾向；R2/R5 标注"评估型"（YAGNI 可能判定不做）；R3 标注"可能记账"。
- v1.1（2026-08-26）GPT 评审整合：① §0 新增 9.5/9.6 阶段边界（9.5 = 收尾基础设施，9.6 = 动效系统，不互塞）；② R4 定位升级 = **9.6 前置交互基础设施**，职责写死"只产生 Hover 事实、不负责视觉过渡/动画/Hover Style"，依赖提升为正式（9.6 首个消费者）；③ R1 职责修正 = **Clip 栈消费层**（绘制阶段 Clip 生命周期规则 + TextBox 裁切迁移），明确非 Dirty Region 系统；d1 形态留待详细设计再审；④ 无拒绝项。
