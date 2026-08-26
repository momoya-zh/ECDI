# Phase 9.5 R4 Hover / MouseEnter / Leave 详细设计

> 状态：v1.2（2026-08-26）｜升级记录：v1.1 有条件通过（GPT：确认 RemoveChild 所有权语义），v1.2 = 补强 **Widget 所有权模型契约**——`m_hoverWidget` 与 `m_focusedWidget`/`m_captureWidget` 同族，框架已假设 Widget 不通过随机 RemoveChild 静默销毁
> 承接：phase9.5-r4-hover-preliminary-design.md v1.1（四条硬契约 A/B/C/D + 状态机）
> 相关：phase3-architecture.md（事件流/HitTest）/ phase5.4-interaction-requirements.md（Capture/Invalidate）/ phase7.2 测试体系（无窗口测试框架）

## 1. 接口变更清单（3 文件）

### 1.1 Widget.h（新增 2 个虚方法）

```cpp
// ── Hover（9.5 R4）────────────────────────────────
/// @brief 鼠标移入通知（Hover 状态变化事实——9.6 动画前置）
/// @details 默认空实现——无 hover 需求的控件零感知；事件驱动语义：进入 = 状态变化事实，非坐标事实
virtual void OnMouseEnter();

/// @brief 鼠标移出通知（Hover 状态变化事实）
/// @details 默认空实现；契约 C：脱树导致的失效不派发此方法
virtual void OnMouseLeave();
```

### 1.2 Window.h（新增 1 个成员 + 2 个方法）

```cpp
private:
    // ── Hover 状态（9.5 R4）────────────────────────
    Widget* m_hoverWidget = nullptr;   ///< 当前 Hover 目标（非拥有指针——与 m_focusedWidget/m_captureWidget 同族）

    /// @brief 验证 Widget 是否仍属于当前 Window 的树（派发前调用——契约 C 验证）
    /// @details 沿 Parent 链上溯，只有最终可达当前 Window 的 RootWidget 才视为属于当前 Window
    /// @param widget 待验证目标
    /// @return true=仍在树；false=已脱树
    bool IsWidgetInTree(Widget* widget) const noexcept;

    /// @brief 更新 Hover 状态机（Application::OnMouseMove 调用——唯一入口）
    /// @param newTarget HitTest 命中的新目标（nullable）
    /// @pre newTarget == nullptr 或 IsWidgetInTree(newTarget) == true（调用方保证——HitTest 结果必然属于当前 Window Tree）
    void UpdateHoverState(Widget* newTarget);
```

### 1.3 Application.cpp（OnMouseMove 改造）

```cpp
void Application::OnMouseMove(const MouseMoveEvent& event) {
    Window& window = *event.GetWindow();

    // 契约 A：Capture 存在 → hover 状态机完全冻结（MouseMove 直派捕获者）
    if (window.GetCaptureWidget() != nullptr) {
        Widget* target = window.GetCaptureWidget();
        // 现有 Bubbling 派发逻辑（不变）
        while (target != nullptr) {
            target->OnMouseMove(event);
            target = target->GetParent();
        }
        return;   // 冻结：不进入 hover 状态机
    }

    // 1. HitTest
    Widget* newTarget = FindTargetWidget(window, event.GetMouseX(), event.GetMouseY());

    // 2. 无目标 → 视为"未命中"（hover 置空）
    // ⚠️ 不改变既有 MouseMove 语义：现有代码在 target==nullptr 时直接 return（不派发）
    // R4 保持这一行为——仅追加 hover 状态机更新，不引入新的 MouseMove 接收者
    if (newTarget == nullptr) {
        window.UpdateHoverState(nullptr);   // 触发 Leave（正常离开）
        return;
    }

    // 3. 更新 Hover 状态机（契约 D：Leave→Enter 顺序）
    window.UpdateHoverState(newTarget);

    // 4. MouseMove 派发（现有 Bubbling 逻辑不变）
    Widget* current = newTarget;
    while (current != nullptr) {
        current->OnMouseMove(event);
        current = current->GetParent();
    }
}
```

## 2. 算法伪代码（冻结为文档）

### 2.1 UpdateHoverState（Window 核心逻辑）

```
输入：newTarget（HitTest 结果，nullable）

if m_hoverWidget == newTarget:
    return;   // 同目标不重复通知

oldHover = m_hoverWidget;
m_hoverWidget = newTarget;   // 先更新（Leave 里控件可能查 hover 状态）

// 契约 D：Leave → Enter 严格顺序
if oldHover != nullptr 且 IsWidgetInTree(oldHover):
    oldHover->OnMouseLeave();   // 正常离开派发

if newTarget != nullptr:
    newTarget->OnMouseEnter();   // 进入派发

// 注意：newTarget 由调用方保证在树内（Application 已做 HitTest）
```

### 2.2 IsWidgetInTree（树有效性验证）

```
输入：widget
前置：widget != nullptr

// 沿 Parent 链上溯，检查是否可达 RootWidget
Widget* current = widget;
while current != nullptr:
    if current == &m_rootWidget:
        return true;   // 到达 RootWidget = 仍在树
    current = current->GetParent();

return false;   // 上溯到 null 仍未到 RootWidget = 已脱树
```

### 2.3 契约 A/B/C 代码化

- **契约 A**：Capture 存在时直接 return，不进入 `UpdateHoverState`——`Application::OnMouseMove` 顶部判断（见 1.3）
- **契约 B**：`ReleaseCapture` 不调用 `UpdateHoverState`——`SetCaptureWidget(nullptr)` 无任何 hover 逻辑；下一 MouseMove 自然恢复
- **契约 C**：`UpdateHoverState` 内部 `if oldHover != nullptr 且 IsWidgetInTree(oldHover)` 才派发 Leave；脱树 → 条件 false → 不派发

## 3. 完整状态机（含捕获期与脱树）

| 当前 m_hoverWidget | 输入 | 动作 | 契约 |
|---|---|---|---|
| null | HitTest=A（在树） | hover=A；A.OnMouseEnter() | - |
| A | HitTest=A（同目标） | 无动作 | - |
| A | HitTest=B（在树） | hover=B；A.OnMouseLeave() → B.OnMouseEnter() | D |
| A | HitTest=null | hover=null；A.OnMouseLeave() | 正常离开 |
| A | A 已脱树（下次 MouseMove） | hover=null；不派发 Leave | C |
| A | Capture 存在 | 完全冻结 | A |
| A | Capture 释放后首次 MouseMove | 恢复 HitTest（B 在树 → A.Leave → B.Enter） | B |

**关键**：脱树路径**不调用** Leave（契约 C）；正常离开**调用** Leave——两者语义不同。

## 4. 生命周期契约（详细设计冻结）

### 4.1 销毁自动兜底（契约 2）
- `m_hoverWidget` 是 Window 成员（裸指针，非智能指针）；Window 生命周期结束后，`m_hoverWidget` 随 Window 一并消失
- 由于 Window 不再参与事件派发，不需要单独清理——**无显式清理代码**

### 4.2 树移除（契约 1 = 契约 C）
- `RemoveChild` 不通知 Application（避免 Widget→Application 反向耦合）
- 依赖"下次 MouseMove 时 `IsWidgetInTree` 验证失败 → 置空不派发"
- 极端：RemoveChild 后立即 hover 目标悬空，但无 MouseMove 不会访问 → 安全

### 4.3 回调重入边界（v1.1 新增）
- **不定义 Enter/Leave 回调期间 Widget Tree 修改的行为**——该场景不属于 R4 v1.0 契约（YAGNI）
- 实现不对 Enter/Leave 回调提供重入/树结构突变保护；控件应避免在回调内修改自身或他人的树结构

### 4.4 Widget 所有权模型契约（v1.2 新增，写死）

> **事实依据**：`Widget::RemoveChild` 返回 `std::unique_ptr<Widget>`——对象**不会立即析构**，所有权回到调用者手里。这是 ECDI 所有权模型的核心契约。

- `m_hoverWidget` 与 `m_focusedWidget` / `m_captureWidget` 同族——**都是 Window 内的非拥有指针**，生命周期模型完全一致
- 现有框架已经假设"Widget 不会在框架不知情的情况下被销毁"——Widget 销毁走 Window/Application 生命周期，不走随机 RemoveChild
- R4 **没有引入新的风险类别**——悬空指针风险 = 现有 Focus/Capture 同款风险
- **契约**：调用者持有 `RemoveChild` 返回的 `unique_ptr` 期间，Widget 对象保持存活；R4 的 `IsWidgetInTree` 验证在下次 MouseMove 时正确判为脱树（`m_parent == nullptr` → 上溯不到 RootWidget → false）

## 5. 测试用例表（Phase 7.2 框架，无窗口）

| 用例 | 操作序列 | 期望 | 契约 |
|---|---|---|---|
| R4-S1 进入 | MouseMove 到 A（null→A） | A.OnMouseEnter() 调用 1 次 | - |
| R4-S2 离开 | A hover → MouseMove 未命中 | A.OnMouseLeave() 调用 1 次 | 正常离开 |
| R4-S3 切换 | A hover → MouseMove 到 B | A.OnMouseLeave() → B.OnMouseEnter()（严格顺序，Leave 先于 Enter） | D |
| R4-S4 重复 | A hover → MouseMove 到 A（同目标） | 无 Enter/Leave 调用 | - |
| R4-S5 Z 序 | A,B 重叠（B 在上）→ MouseMove 到重叠区 | B.Enter（HitTest 返回最上层） | - |
| R4-S6 捕获冻结 | A hover → MouseDown A（Capture=A）→ MouseMove 到 B | 无 Leave/Enter 调用（状态机冻结） | A |
| R4-S7 捕获释放后恢复 | R4-S6 后 → MouseUp（ReleaseCapture）→ MouseMove B | A.OnMouseLeave() → B.OnMouseEnter()（释放后首次 MouseMove 恢复） | B |
| R4-S8 脱树不补发 | A hover → RemoveChild A → MouseMove 到 B | 无 A.OnMouseLeave() 调用（脱树不补发）；B.OnMouseEnter() | C |
| R4-S9 脱树后 HitTest A | A hover → RemoveChild A → MouseMove 到 A 原位置 | A.OnMouseLeave() **不调用**（A 已脱树）；视命中情况决定 B.Enter 或无操作 | C |

> R4-S5 强调：**R4 不定义 Z 序规则，直接消费既有 HitTest 结果**——Z 序由 Widget 子节点顺序和 Paint/HitTest 语义决定，R4 不参与。

**顺序测试强化（R4-S3）**：期望事件序列 = `["A.OnMouseLeave", "B.OnMouseEnter"]`（严格顺序，非仅计数）——测试应 push 事件名到 vector 后断言序列，而非仅断言 LeaveCount==1 && EnterCount==1。

## 6. 明确不做（v1.0 YAGNI，与初步设计 §6 一致）

- SetOnMouseEnter/Leave 回调 API
- Enter/Leave 的 Bubbling
- Enter/Leave 坐标参数
- MouseEnter/Leave 事件类型进 EventSystem
- Tooltip 消费
- WeakPtr / 生命周期系统 / RemoveChild 通知回调
- 框架级 IsHovered()（9.5 与 9.6 都不做）

## 7. 修订记录

- v1.0（2026-08-26）初稿：3 文件接口变更清单 + 算法伪代码 + 完整状态机 + 10 测试用例。
- v1.1（2026-08-26）GPT 评审整合（8 条全采纳，无推翻）：① 明确 MouseMove null 分支**不改变既有语义**（现有代码 target==nullptr 时直接 return，R4 保持）；② 接口契约写死（`@pre` 由调用方保证，UpdateHoverState 不做验证）；③ IsWidgetInTree 注释精确化（"沿 Parent 链上溯，只有最终可达当前 Window 的 RootWidget 才视为属于当前 Window"）；④ 销毁自动兜底措辞严谨化（裸指针随 Window 消失，无显式清理）；⑤ 新增 §4.3 回调重入边界（不定义 Enter/Leave 回调期间 Widget Tree 修改的行为）；⑥ 测试合并：R4-SX 并入 R4-S7（去重）；⑦ R4-S3 顺序测试强化（事件序列 vector 断言，非仅计数）；⑧ R4-S5 强调 R4 不定义 Z 序规则，直接消费既有 HitTest 结果。
