# Phase 9.6 动画系统 · 初步设计

> 版本 v1.1（2026-08-29，GPT 评审整合：初步设计通过，可进详细设计）。五阶段法第二步：方案空间 + 倾向，接口细节留详细设计。
> 优先级（requirements v1.1 §5 / GPT 评审锚定）：**先把 tick 路由通道设计干净，再谈 Animator API**——路由是结构性问题，API 是表面积。

## 0. 代码事实基础（2026-08-29 核实）

- **TimerEvent 自带来源窗口**：`GetWindow()`（Event 基类 `m_window`）——事件本身知道从哪个窗口来，路由无需查找表
- **`Application::OnTimer` 现状**（Application.cpp:127）：`FindFocusedWidget` → `target->OnTimer`；无焦点直接 return——动画 tick 分支须在此之前
- **`PlatformWindow` 契约能力齐备**（PlatformWindow.h）：`StartTimer/StopTimer`（8.5.1 通用能力，"ID 语义由调用方定义，平台不知道业务"）+ `Invalidate`（"请求重绘整个客户区，异步可合并"）——manager 所需两能力全部在契约上
- **Window 暴露 `GetPlatformWindow()`**（TextBox.cpp:87 先例：控件经 Window 直取平台能力启停 timer）
- **owner-held TimerId 先例**：`TextBox::kCaretBlinkTimer = 1`（TextBox.h:88，"TextBox 拥有 TimerId 语义"）
- **Win32 实现**：`SetTimer/KillTimer`，timerId 即 wParam 直传（Win32PlatformWindow.cpp:448-462）

## 1. 议题一：tick 路由通道（核心）

### 方案空间

- **方案 A · 保留 timerId 分支路由**：`Application::OnTimer` 顶部加分支——`timerId == kAnimationTick` → `event.GetWindow()->OnAnimationTick()`（Window 具体方法，纯转发给本窗口 manager）→ return；未命中走原焦点派发。Application 知识增量 = 一个常量 + 一次转发
- **方案 B · EventRouter 层拦截**：分发前分流动画 tick——EventRouter 职责是"翻译后事件的统一分发"，不宜懂业务 timerId；侵入分派语义，改动面更大
- **方案 C · manager 向 Application 注册回调**：Application 级注册表——违背 per-Window 决策；窗口销毁需反注册，生命周期复杂化

### 倾向：A（GPT 评审确认通过）

- TimerEvent 自带 `Window*`——结构性免查找（GPT 担心的"怎么找到对应 Window 的 manager"答案就在事件自身）
- `Application::OnTimer` 是 timer 语义唯一汇合点，分支路由 = 语义最自然处
- 改动最小：1 处分支 + 1 个 Window 转发方法；TimerEvent 派发语义零改动（8.5.1 模型原样保留）
- **GPT 评审（2026-08-29）**：「非常干净……这就是典型的'能通过已有对象关系解决，就不要新造全局管理结构'」——方案 A 定稿

### 附带决策点 d1：TimerId 分配约定

- 现状：owner-held 常量（TextBox=1），防撞靠约定
- 选项 a：**集中常量头**（如 `EventSystem/Window/TimerIds.h`：`kCaretBlink = 1` 迁移 + `kAnimationTick = 2`）——一处可见、防撞、文档化；代价：TextBox 常量迁移（`TextBox::kCaretBlinkTimer` 可保留为别名，不破坏既有引用）
- 选项 b：保持 owner-held + 预留段约定（框架保留 1–15，动画取 2）——零迁移，防撞靠纪律
- **倾向 a**：一次性小迁移换永久防撞；测试用例（TextBoxTests F13 多 timer 隔离）不受影响

## 2. 议题二：manager 能力接缝（GPT 收紧落地）

- **目标结构**：

```text
Window
├── AnimationManager      ← 组合；构造注入 PlatformWindow&
│     ├── Animation
│     ├── Animation
│     └── ...
└── PlatformWindow        ← Timer 归平台（StartTimer/StopTimer）
      └── Invalidate()    ← 重绘请求契约
```

- manager 消费的能力**恰好只有两个**且都在 `PlatformWindow` 契约上：启停 timer + `Invalidate`（每 tick 聚合一次重绘请求）——manager **类型上拿不到 `Window&`**，操作 Window 内部无从发生
- `Window::OnAnimationTick()`：路由到达入口，**纯转发** `m_animationManager.Tick()`——Window 不做任何动画逻辑
- 与 8.5.1 先例一致：TextBox 经 `window->GetPlatformWindow()` 直取平台能力；manager 只是注入时机前移（Window 构造时注入）
- **详细设计核实点**：Window 持有 PlatformWindow 的形态（值/unique_ptr/引用）与成员初始化顺序——manager 注入时 PlatformWindow 必须已可用

## 3. 议题三：时间源与推进模型

- WM_TIMER 无时间戳——插值需要 elapsed 时间源
- 选项 a：**`std::chrono::steady_clock`，每 tick 用真实 elapsed 推进**——倾向（平台无关标准库；帧率无关、抖动免疫；不引入虚拟时钟抽象，YAGNI）——**GPT 评审确认定稿**：WM_TIMER 是"大约 16ms 来一次"而非"严格每 16ms"，固定步进会让动画实际变慢、真实 elapsed 保持总时长正确；"Animation 300ms 是时间意义上的 300ms，不是 19 个 tick"
- 选项 b：固定步进（每 tick += 16ms 假设）——实现最简，但 WM_TIMER 实际分辨率约 15.6ms + 消息排队抖动 → 累积漂移，动画时长不可信
- tick 间隔常量（约 16ms）与真实 elapsed 解耦——间隔只决定推进频率，不参与插值

## 4. 议题四：Animation 生命周期（粗粒度，API 留详细设计）

- 状态机：注册 → 活跃（每 tick 推进 + 应用值）→ 完成（移除；**活跃数归零 → StopTimer**——空闲零开销）
- **模式：一次性先行**（S1/S2 均一次性）；**循环模式挂账**——唯一潜在消费者 spinner 已挂账，消费者出现再评估（与 requirements §1 一致）
- 详细设计必答议题（记议题不展开）：
  - **目标悬挂**：Widget 销毁时进行中动画的处理——倾向弱关联（动画不延长目标生命周期；tick 时目标失效即终止）
  - **重复启动语义**：同目标同属性再启动 = 替换式重启（**新 from = 当前呈现值**，防跳变）——S1 hover 快速进出的真实场景；**GPT 评审明确赞成（2026-08-29）**：「from 应该是当前呈现值，而不是上一次动画的 target，否则快速 Hover Enter→Leave→Enter→Leave 很容易产生颜色跳变」——**此子议题视为已收敛**，详细设计不再重议
  - **值应用形态**：回调 setter / 持目标引用 / Widget 虚方法——按 S1/S2 消费形态在详细设计定

## 5. S1 / S2 消费形态（粗粒度）

- **S1 Button**：状态迁移事实（hover/pressed/focus 变化，R4 已产）→ 启动 Color 过渡（from = 当前显示色，to = 目标状态色）——动画消费状态事实、不改变状态（边界原则 §4）
- **S2 demo**：容器高度 float 过渡；每 tick 高度变化走 Layout 触发链（复用 5.4 Invalidate/Arrange 基础设施）——demo 验证布局联动，作 CollapsiblePanel 实现参考
- **S2 的定位（GPT 评审锚定）**：S2 不只是"让数字从 100 变成 200"，而是验证**动画系统能否成为现有 GUI 状态/布局/渲染体系的一个正常消费者**——高度变化 → Geometry 改变 → Layout → 子控件位置 → Invalidate → 重 Paint 全链路
- **Demo → CollapsiblePanel 顺序（GPT 评审确认）**：先基础设施 → 首个消费者 → 第二消费者验证 → 再按 demo 验证过的实现模式产品化正式控件——CollapsiblePanel 不是"为验证动画临时造的控件"，而是模式验证后的正式化

## 6. 决策点汇总

| # | 议题 | 倾向 | 定稿时机 |
|---|---|---|---|
| d1 | TimerId 分配约定 | 集中 `TimerIds.h`（TextBox 别名迁移） | 本阶段确认（GPT 未反对） |
| d2 | 路由方案 | A：`Application::OnTimer` 保留 id 分支 | ✅ GPT 评审确认（2026-08-29） |
| d3 | 时间源 | steady_clock 真实 elapsed | ✅ GPT 评审确认（2026-08-29） |
| d4 | 循环模式 | 挂账（spinner 消费者出现再评估） | 已定 |
| d5 | 目标悬挂 / 重复启动语义 | 弱关联 + 替换式重启（from = 当前呈现值） | 重启子项 ✅ GPT 赞成已收敛；悬挂子项详细设计 |
| d6 | 值应用形态 / 目标绑定 | —— | 详细设计（GPT：S2 是真实验证场，重点审） |

## 7. YAGNI 边界（不做清单）

- 不做跨窗口动画协调（per-Window 决策的直接推论）
- 不做动画队列 / 编排 / 时间线系统（无消费者）
- 不做速度曲线配置化、全局倍率（无消费者）
- 不做虚拟时钟抽象（测试如需确定性时间，详细设计时用注入时间源评估，不预设）

## 8. 修订记录

- v1.0（2026-08-29）初稿：四议题方案空间 + 倾向——路由通道三方案对比（倾向 A：TimerEvent 自带 Window* 免查找）；能力接缝结构图（manager 只持 PlatformWindow&）；时间源（倾向 steady_clock）；生命周期粗粒度 + 详细设计待答议题（悬挂/重启/值应用形态）；决策点汇总表。
- v1.1（2026-08-29）GPT 评审整合（✅ 初步设计通过，可进详细设计）：① d2 方案 A 确认定稿（「能通过已有对象关系解决，就不要新造全局管理结构」）；② d3 steady_clock 确认定稿（「300ms 是时间意义上的 300ms，不是 19 个 tick」）；③ d5 重复启动语义 GPT 明确赞成、视为已收敛（from = 当前呈现值）；④ §5 新增 S2 定位锚定（验证动画能否成为 GUI 体系的正常消费者）+ Demo→CollapsiblePanel 顺序确认（先基础设施→首消费者→第二消费者验证→再产品化）；⑤ 决策点汇总表状态同步；⑥ 无拒绝项。GPT 附加提示：详细设计重点审 Animation / AnimationManager / 目标绑定 / 重启 / 完成回调的具体结构，S2 布局联动链是真实验证场。
