# Phase 9.6 动画系统 · 职责确认

> 版本 v1.0（2026-08-29）。五阶段法第一步：只记边界与决策点（含结论），不展开接口设计。
> 前置锚点：2026-08-26 立项意向，用户定「9.5 弄完再评价职责确认」；Phase 9.5 五项已于 2026-08-28 全部收口，触发条件达成。

## 0. 阶段定位

- **定位**：动效系统（与 9.5 收尾基础设施不互塞）——**表现层**：只消费已发生的状态事实，不产生状态
- **一句话定位（GPT 评审锚定）**：AnimationManager 是 Window 内的表现层基础设施——负责时间推进、插值、easing 和动画生命周期；**不负责状态、不负责控件逻辑、不负责渲染能力扩展**
- **方法论**：消费场景锚定 YAGNI——所有范围决策由「首个消费者是谁」反推
- **前置依赖**：R4 Hover 事实（2026-08-27 已落地）= S1 状态色过渡的硬前置

## 1. 消费场景（锚点）

- **S1 Button 状态色过渡**：pressed / hover / focus 的背景色 / 文字色渐变——消费 R4 Hover 事实与焦点状态
- **S2 展开/折叠高度动画**：**demo 容器承载**（demo 代码内、不入框架），后续作为 CollapsiblePanel 的实现参考；正式面板控件立项挂账
- **挂账**：ProgressBar 立项、spinner（需 DrawArc）、缩放（需 PushTransform 或 Rect 重绘）——均无当前消费者

## 2. 时钟与驱动模型（核心决策）

- **决策：per-Window 组合 AnimationManager**（用户定：**隔离性优先于跨窗口统一**；跨窗口动画场景出现时再评估上移）
- 每窗口**单一 tick timer**（约 16ms、一个 timerId）：tick 推进本窗口全部活动动画，**直接通知持有者、不经焦点链**——TimerEvent 焦点派发缺口自然规避
- 无活动动画即 StopTimer（空闲零开销）；Invalidate 统一聚合（本窗口每 tick 一次）
- **能力接缝（GPT 评审收紧，2026-08-29 核实成立）**：manager **只持 `PlatformWindow&`**——GPT 指出 manager 仅需"启动/停止 timer + 请求重绘"两个能力、不要拿着 Window 操作内部；代码核实两能力恰好全部落在 `PlatformWindow` 契约上（`StartTimer/StopTimer/Invalidate`）——manager 持 `PlatformWindow&` 即从**类型上拿不到 `Window&`**，结构性杜绝操作 Window 内部；不持具体值——决策 35 式耦合不重演
- **目标结构（GPT 评审）**：Window → { AnimationManager（动画列表）, PlatformWindow（Timer 归平台） }

## 3. 插值与 easing

- **插值类型（消费者最小集）**：`float` + `Color`（S1/S2 直接锁定）；Rect 形态（高度 float vs Rect 插值）留 S2 详细设计再定；Point 等无消费者不做
- **easing 四种一步到位**：Linear / EaseIn / EaseOut / EaseInOut——纯数学函数、成本恒定，避免反复动文件（用户定）

## 4. 边界原则（写死）

- **动画只平滑到达状态、不产生状态**——控件状态仍由事件驱动（R4 hover / 焦点事实），动画层与控件状态逻辑分离
- **Fade（透明度渐隐）直接关闭**：无消费者；PushOpacity 记账
- **DrawArc / PushTransform 继续记账**：无消费者不动

## 5. 遗留接口债（待初步设计）

- **per-Window manager 的 tick 到达通道**：`Application::OnTimer` 现直派焦点控件（8.5.1 模型）；管理器需一条绕开焦点派发的到达通道——倾向按保留 timerId 在 `Application::OnTimer` 开路由（timerId 命中 → 转交该窗口 manager；未命中 → 走原焦点派发），具体形态归初步设计，改动以 TimerEvent 派发语义最小化为原则
- **初步设计优先级（GPT 评审）**：先把 tick 路由通道设计干净，再谈 Animator API——路由是结构性问题，API 是表面积

## 6. 验收标准（后续阶段细化）

- S1：Button 状态切换颜色平滑过渡可观察；无动画活动时零 timer、零开销
- S2：demo 面板高度动画 + Layout / Invalidate 联动链正确
- Phase 7.2 测试框架覆盖：插值 / easing 纯数学用例 + 时钟推进逻辑（新增 Phase 同步补测试惯例）

## 7. 修订记录

- v1.0（2026-08-29）初稿：职责确认八条结论，用户逐条问答收敛——① 消费场景 = Button 色过渡 + 展开/折叠（demo 承载）；② 时钟 = per-Window 组合管理器（单一 tick timer、不经焦点链）；③ 插值最小集（float + Color）；④ easing 四种一步到位；⑤ Fade 关闭 / PushOpacity 记账；⑥ 边界 = 动画不产生状态；⑦ 遗留 tick 路由债（归初步设计）；⑧ DrawArc / PushTransform 继续记账。
- v1.1（2026-08-29）GPT 评审整合（✅ 职责确认通过）：① §0 新增一句话定位（表现层基础设施，三不负责）；② §2 能力接缝收紧——manager 只持 `PlatformWindow&`（启停 timer + 请求重绘两能力恰在契约上，类型上拿不到 `Window&`），附目标结构；③ §5 明确初步设计优先级 = tick 路由先行于 Animator API；④ 无拒绝项。
