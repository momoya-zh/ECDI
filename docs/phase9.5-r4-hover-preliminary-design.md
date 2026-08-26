# Phase 9.5 R4 Hover / MouseEnter / Leave 初步设计

> 状态：v1.1（2026-08-26）｜升级记录：v1.0 通过评审（GPT：无推翻项），v1.1 = 评审整合——**新增 §2.1 四条硬契约**（详细设计冻结）+ 状态机闭合 + 测试补 R4-SX
> 承接：phase9.5-wrapup-requirements.md v1.1（R4 = 9.6 前置交互基础设施——只产生 Hover 状态变化事实，不负责视觉过渡/动画/Hover Style）
> 相关：phase3-architecture.md（事件流/HitTest）/ phase5.4-interaction-requirements.md（Capture/Invalidate）/ phase7.2 测试体系（无窗口测试框架）

## 1. 方案总览：驱动（Application）+ 状态（Window）+ 响应（Widget）

```text
MouseMoveEvent → Application::OnMouseMove
   → FindTargetWidget(Window&, x, y)        （复用现有 HitTest）
   → hover 状态机（Window 持有 m_hoverWidget）
        ├─ 目标变化 → old.OnMouseLeave() / new.OnMouseEnter()
        └─ 验证 m_hoverWidget 仍在树（D4）
   → 派发 MouseMove 到目标（现有路径不变）
```

三件套职责分离：
- **驱动**：Application::OnMouseMove（HitTest 本属 Application 派发职责）
- **状态**：Window::m_hoverWidget（与 m_focusedWidget / m_captureWidget 同族）
- **响应**：Widget::OnMouseEnter() / OnMouseLeave() 虚方法

## 2. 关键决策点

### D1 hover 状态归属：Window（职责确认 d2 的归属细化）
- 职责确认 v1.1 表述"Application::OnMouseMove 跟踪 m_hoverWidget"——初步设计细化为 **Application 驱动 + Window 存储**
- 理由：① m_hoverWidget 与 m_focusedWidget/m_captureWidget（均在 Window）同族——**一致性**；② **Window 析构 → 成员自动析构 → 销毁兜底自动成立**（职责确认契约 2 无需显式清理代码）；③ HitTest 驱动仍在 Application（Application 与 Window 互为 friend，跨类访问无障碍）
- 范围不变（不新增/不删需求），仅实现归属细化——供评审确认

### D2 事件形态：Widget 虚方法 OnMouseEnter()/OnMouseLeave()，无参
- Enter/Leave = "状态变化事实"，非"移动事实"——与 MouseMoveEvent 区分（Event 原则：轻量、只表示已发生的事实）
- **无参**：第一个消费者（9.6 hover 过渡）只需进入/离开本身；坐标参数记账（Tooltip 定位需要时再加）
- **不进 EventSystem**（不建 MouseEnterEvent/MouseLeaveEvent 类型）：Widget 层虚方法响应即可，Event 类型记账

### D3 捕获期间冻结 + 释放边界（契约 A/B）
- 存在 captureWidget 时：MouseMove 直派捕获者（现有逻辑），**hover 状态机完全冻结**（契约 A）——按下移出不触发 Leave 视觉闪烁
- **释放边界（契约 B）**：ReleaseCapture 本身不主动重算 Hover；释放后的下一次 MouseMove 才恢复 HitTest——状态机唯一驱动 = MouseMove 输入

### D4 生命周期：派发时树有效性强判定 + 销毁自动兜底（契约 C）
- **树有效性判定规则（v1.1 细化，不新增公共 API）**：Hover 派发前沿 Parent 链向上验证目标仍属于**当前 Window 的 RootWidget**（复用 Focus 同款验证方式，但以文档形式冻结判定规则，非"复用一个不存在的新接口"）
- **契约 1 = 契约 C**：验证失败（目标脱树）→ `m_hoverWidget=nullptr`，**不派发 OnMouseLeave**（目标已不属当前树，避免脱离窗口的 Widget 执行 UI 状态变化）
- **契约 2（销毁兜底）**：由 D1 自动成立（Window 级成员，窗口销毁即析构）——无需显式代码
- **明确不做**：RemoveChild 通知回调（Widget→Application 反向耦合）、WeakPtr/智能引用（YAGNI）

### D5 派发顺序：先结算 Enter/Leave，再派发 MouseMove
- 同一 MouseMove 事件处理中：先更新 hover 状态机（派发 Enter/Leave），再派发 MouseMove 到目标——进入时 hover 先生效（9.6 动画从 Enter 启动，与同帧 MouseMove 无冲突）

## 2.1 四条硬契约（v1.1 新增，详细设计冻结，测试断言依据）

> GPT 评审整合（v1.0 → v1.1，无推翻项，全部采纳为契约）。

- **契约 A（Capture 冻结）**：存在 Capture 时，MouseMove 不更新 Hover Target——hover 状态机**完全冻结**（不运行）
- **契约 B（Capture 释放不重算）**：`ReleaseCapture` 本身不触发 Hover 重算；释放后的**下一次 MouseMove** 才恢复正常 HitTest——防止 ReleaseCapture 隐式触发 Leave/Enter（状态机唯一驱动 = MouseMove 输入，无第二入口）
- **契约 C（失效 Hover 不补发 Leave）**：`m_hoverWidget` 脱离当前 Window Widget Tree 后，下一次 MouseMove 将其置空；**由于目标已不属当前树，不派发 OnMouseLeave**（区分：正常离开 A→null 派发 Leave；异常失效 A 脱树 → 置空不派发）
- **契约 D（Enter/Leave 顺序）**：目标从 A 切换至 B 时，严格执行 `A.OnMouseLeave()` → `B.OnMouseEnter()`，不得反序

## 3. hover 状态机（详细设计输入）

| 当前 hover | MouseMove 命中 | 动作 |
|---|---|---|
| null | A | hover=A；A.OnMouseEnter() |
| A | A | 无变化（不重复通知） |
| A | B | hover=B；A.OnMouseLeave() → B.OnMouseEnter()（契约 D 顺序） |
| A | null（未命中） | hover=null；A.OnMouseLeave()（正常离开） |
| A | 已脱树（契约 C） | hover=null；**不派发 Leave**（异常失效） |
| 捕获期间（契约 A） | 任意 | **不运行状态机**（完全冻结） |

目标切换次序（契约 D）：A→B 必须 `A.OnMouseLeave()` 先、`B.OnMouseEnter()` 后——测试断言依据。

## 4. 接口草图（详细设计细化签名）

```cpp
// Widget 新增两个虚方法（默认空实现——无 hover 需求的控件零感知）
virtual void OnMouseEnter();
virtual void OnMouseLeave();

// Window 新增：hover 状态与派发入口（签名详细设计定）
// m_hoverWidget — 非拥有指针（与 m_focusedWidget 同族）
```

## 5. 测试策略（Phase 7.2 框架，无窗口）

- TestWidget 记录 OnMouseEnter/Leave 调用序列（EXPECT 断言顺序）
- 场景：进入（null→A）/ 目标切换（A→B：Leave 先 Enter 后——契约 D）/ 离开（A→null）/ Z 序命中 / 未命中 / **捕获期冻结**（契约 A：Down 后移出无 Leave）/ **树移除后 hover 失效**（契约 C：RemoveChild 后 MouseMove 不崩溃、状态复位、不补发 Leave）/ 重复同目标 MouseMove 不重复通知 / **R4-SX：Capture 释放后的首次 MouseMove**（契约 B：A hover → Down 捕获 A → Move 到 B（无 Leave/Enter）→ Up 释放 → Move B → A.Leave + B.Enter）

## 6. 明确不做（YAGNI）

- SetOnMouseEnter/Leave 回调注册 API（第一个用例不需要）
- Enter/Leave 的 Bubbling（目标专属语义）
- Enter/Leave 坐标参数（Tooltip 需要时加）
- MouseEnter/Leave 事件类型进 EventSystem
- Tooltip 消费（记账）
- WeakPtr / 生命周期系统 / RemoveChild 通知回调
- **框架级 IsHovered() 接口（9.5 与 9.6 都不做）**：Hover 是**事件驱动的状态变化**（`OnMouseEnter` → 动画状态 = hovered；`OnMouseLeave` → normal），非查询型状态——避免 `Window::m_hoverWidget` 与 `Widget::m_hovered` 两套状态源同步问题；控件内自管理状态即可（9.6 动效消费时用事件驱动，不引入查询接口）

## 7. 修订记录

- v1.0（2026-08-26）初稿：方案总览（驱动/状态/响应三件套）+ D1-D5 + 状态机 + 测试策略。
- v1.1（2026-08-26）GPT 评审整合（12 项全部采纳，无推翻）：① 新增 §2.1 四条硬契约（A Capture 冻结 / B Release 不重算 / C 脱树不补发 Leave / D Leave→Enter 顺序）；② D3 补释放边界（契约 B）；③ D4 树有效性强判定规则（不新增公共 API）+ 区分正常离开/异常失效；④ 状态机补两行闭合（脱树行 + 捕获期行）；⑤ 测试补 R4-SX（Capture 释放后首次 MouseMove）；⑥ IsHovered() 明确 9.6 也不做（事件驱动状态变化，非查询型）。
