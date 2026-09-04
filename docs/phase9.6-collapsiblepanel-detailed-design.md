# Phase 9.6 CollapsiblePanel 详细设计（v1.3）

> 阶段：详细设计（五阶段法 ③）
> 日期：2026-08-30
> 状态：已通过（GPT 评审 + 用户确认），v1.3 增量修订已实现
> 前置：初步设计 v1.1 已通过（GPT 评审 + 用户确认）
> v1.1：补测试 12/13（GPT 评审建议——Content 跟随验证 + 多次折叠记忆语义）
> v1.2：**默认收起变更**（详设：phase9.6-panel-container-semantics-detailed-design.md v1.1）——m_expanded=false / 收起态 SetSize = 定义展开基准（非动画轴保持、动画轴呈现 0）/ ApplyGeometry 直调防递归 / SetExpandDirection 升格「须在首次 SetSize 前设置」（初始化约定，无运行时 assert）/ Toggle 首次语义反转（默认收起下首次 = 展开）
> v1.3：**位置锚定参照改为当前几何**（Showcase 实测：CollapsiblePanel 首次放入 Layout 容器——VerticalLayout 排位后当前位置 ≠ m_expandedRect 的 SetSize 时刻位置，展开动画把面板拉回陈旧位置、覆盖其他控件致无法交互）——ApplyGeometry 位置部分用 GetX/GetY（当前几何），尺寸部分仍用 m_expandedRect（w0/h0 基准）；锚定边固定语义不变；测试全兼容（所有用例 SetPosition 先于 SetSize，两参照等价）

## 1. 目标与范围

`CollapsiblePanel`：继承 `Panel` 的可折叠内容容器。四向折叠（Down/Up/Left/Right），header 外部自组，内容统一进内部容器 `m_content`（裸 Widget）。折叠 = 沿锚定边 resize 收缩到 0 + 内容容器隐藏；展开 = 内容先显示 + 恢复展开基准几何。复用 per-Window `AnimationManager`（9.6）。

**本设计冻结 GPT 评审提出的 7 个歧义点**，实现不得弱化。

## 2. 硬契约（冻结语义）

### 2.1 方向模型与几何公式（冻结点 1）

动画只驱动「尺寸轴值 s」（float），位置是 s 的纯函数。**位置锚定参照 = 当前几何（cx, cy）**，尺寸基准 = `m_expandedRect = (w0, h0)`（v1.3：Layout 排位后当前位置 ≠ SetSize 时刻的 m_expandedRect 位置，故位置部分必须取当前几何；绝对定位下两者相同）。

```
Down:  pos(cx,       cy)         size(w0, s)      // top 锚定，位置不动
Up:    pos(cx,       cy+h0-s)    size(w0, s)      // bottom 锚定，y 随 s 推导
Right: pos(cx,       cy)         size(s,  h0)     // left 锚定，位置不动
Left:  pos(cx+w0-s,  cy)         size(s,  h0)     // right 锚定，x 随 s 推导
```

- 折叠：s: 当前轴值 → 0；展开：s: 当前轴值 → 目标轴值（Down/Up = h0，Left/Right = w0）。
- **单 token**（m_sizeToken）：位置与尺寸同帧由同一 s 推导，无 x/y/w/h 独立动画状态组合。
- s=0 时：Down/Right 面板退化为位置不变的零高/零宽矩形；Up 顶边收敛于 cy+h0；Left 左边收敛于 cx+w0——锚定边恒不动。

### 2.2 展开基准记忆语义（冻结点 2）

> **`m_expandedRect` = 最后一次「从展开态进入折叠态」时的完整展开几何，不是永久原始几何。**

- 更新时机：`SetExpanded(false)` 且当前 `m_expanded == true`（仅展开态→折叠的跃迁点）→ `m_expandedRect = GetGeometry()`。
- 折叠中再次折叠（幂等 no-op）、折叠中 Toggle 回展开、展开中 Toggle 折叠——均按此规则：**只有进入折叠的那一次**记忆。
- 效果：展开 → 折叠（记 100×300）→ 展开 → 外部改为 120×400 → 折叠（**记 120×400**）→ 展开恢复 120×400。✓ 外部布局改动会被下一次折叠吸收。
- 展开路径**不更新** m_expandedRect。

### 2.3 动画中 Toggle / SetDirection 行为（冻结点 3）

- **动画中 Toggle**：替换式重启（AnimationManager token 替换键）——旧动画静默移除（不调 onFinished），from = 当前呈现轴值（`GetHeight()` / `GetWidth()` 按新目标方向取轴），to = 新目标。与 demo 同构，无跳变。
- **动画中 SetDirection**：立即生效于**下一次**动画；若动画进行中，替换式重启 with from = **新方向动画轴的当前呈现值**（方向切换不改 m_expandedRect）。v0.1 不为此做跨轴换算（Down→Left 时 from 取当前宽度而非高度换算）——语义自然、实现简单，文档记限制。
- **动画中外部 SetPosition/SetSize**：**不是支持场景**（冻结点 6）——不设同步机制；外部修改可能被下一帧 ApplyGeometry 覆盖。文档明示。

### 2.4 Content 与面板几何同步顺序（冻结点 4）

**顺序：先面板、后容器。** 实现载体 = **`SetSize` override**（真实消费者驱动，非投机抽象）：

```cpp
void CollapsiblePanel::SetSize(int w, int h) override{
    Panel::SetSize(w, h);          // ① 先面板
    if (m_content){
        m_content->SetSize(w, h);  // ② 后容器（铺满面板）
    }
}
```

- 一次覆盖三个场景：初始 `panel->SetSize(...)`（内容容器立刻铺满，内容可见）、折叠/展开动画（ApplyGeometry 内 SetSize → 容器随动）、外部改尺寸（容器跟随）。
- `ApplyGeometry(s)` 内**不再单独**同步 m_content——SetSize override 已隐含。
- 位置：容器恒 (0,0)（面板局部坐标），随面板锚定不动（面板平移只影响绝对坐标，容器相对不变）。

### 2.5 visible 与 HitTest 时序（冻结点 5）

**不需要为 CollapsiblePanel 实现「折叠态禁止命中」的特殊 HitTest**——直接利用两层现有机制：

```
折叠完成（动画 onFinished）
  ├─ 面板自身：尺寸轴 s=0 → ContainsPoint 必 false（自身不可命中）
  └─ 内容容器：SetContentVisible(false) → 子树整体跳过 Paint/HitTest（Widget 基类统一 visible 语义）
```

| 动作 | 时序 |
|------|------|
| 折叠 | `SetExpanded(false)` →（若从展开态进入）记忆 m_expandedRect → 启动动画 s: 当前→0（EaseIn）→ **onFinished**：`SetContentVisible(false)` + Invalidate |
| 展开 | `SetExpanded(true)` → `SetContentVisible(true)` + Invalidate → 启动动画 s: 当前→目标轴值（EaseOut） |

- 折叠动画期间内容**保持 visible**：尺寸渐缩、父 Clip（R1）天然裁切——视觉自然，**不自行在绘制路径另做裁剪**。
- 展开动画前先显示：避免「空面板长高、内容突现」断层。
- 折叠态内容区 HitTest 双保险：s=0（面板自身）+ invisible（容器子树）。

### 2.6 Header 外部自组（冻结点 7）

- CollapsiblePanel **完全不管 header**：header 是用户在 Widget 树中另行 AddChild 的外部兄弟节点，与 CollapsiblePanel 无任何引用关系。
- 方向 = 用户通过 `SetExpandDirection` 告知的语义（header 在反侧锚定），控件不做 header 定位/跟随/隐藏。
- 折叠到 0 时 header 不受影响（它不在面板内）——符合「header 锚定边固定」的直觉：header 钉在原位，面板向对侧收缩。

### 2.7 动画配置不开放（GPT 评审 ②）

**不提供** `SetAnimationDuration / SetEasing / SetAnimationEnabled / SetHeaderHeight / SetCollapseSize`——200ms、EaseIn/EaseOut 为内部常量（`kToggleDurationMs = 200`），无真实消费者，YAGNI。

## 3. 接口定义（精确签名）

```cpp
// ECDI/include/ECDI/Widget/CollapsiblePanel.h
#pragma once

#include "ECDI/Animation/AnimationToken.h"
#include "ECDI/Widget/Panel.h"

namespace ECDI{

/// @brief 展开方向（内容向对侧展开；header 锚定在反侧）
enum class ExpandDirection{
    Down,    ///< 内容向下展开（header 在上；默认）
    Up,      ///< 内容向上展开（header 在下）
    Right,   ///< 内容向右展开（header 在左）
    Left,    ///< 内容向左展开（header 在右）
};

/// @brief 可折叠面板（9.6 S2 demo 产品化升格）
class CollapsiblePanel: public Panel{
public:

    CollapsiblePanel();

    ~CollapsiblePanel() override = default;

    // ── 方向 ──────────────────────────────────────

    void SetExpandDirection(ExpandDirection dir) noexcept{ m_direction = dir; }

    [[nodiscard]] ExpandDirection GetExpandDirection() const noexcept{ return m_direction; }

    // ── 折叠状态 ──────────────────────────────────

    void SetExpanded(bool expanded);

    void Toggle();

    [[nodiscard]] bool IsExpanded() const noexcept{ return m_expanded; }

    // ── 内容容器 ──────────────────────────────────

    [[nodiscard]] Widget* GetContent() noexcept{ return m_content; }

protected:

    void SetSize(int w, int h) override;   // 面板 + 内容容器同步（冻结点 4）

private:

    /// @brief 动画回调：按方向推导位置 + 尺寸（单动画值驱动；冻结点 1 公式）
    void ApplyGeometry(float s);

    /// @brief 内容容器可见性统一切换（折叠完成隐藏 / 展开开始前显示）
    void SetContentVisible(bool visible) noexcept;

    static constexpr int kToggleDurationMs = 200;              ///< 尺寸过渡时长（内部常量，不开放）
    static constexpr int kToggleTickIntervalMs = 16;           ///< （manager 统一 16ms——仅记录）

    ExpandDirection m_direction = ExpandDirection::Down;       ///< 展开方向
    bool m_expanded = false;                                   ///< 展开状态（v1.2：默认收起；触发点翻转）
    Rect m_expandedRect{};                                     ///< 展开基准几何（v1.2：默认收起下由首次收起态 SetSize 定义；此后冻结点 2 语义）
    Widget* m_content = nullptr;                               ///< 内容容器（裸 Widget；树拥有，非拥有指针）
    AnimationToken m_sizeToken;                                ///< 尺寸动画令牌（RAII）
};

}
```

**v1.2 接口变更**：`SetSize` override 保持 public（基类即 public virtual）；语义扩展见 §4.4。

**依赖 include**：`AnimationToken.h`（token）、`Widget.h`（经 Panel.h 传递）、`Rect`（经 Widget.h 传递）。cpp 需 `AnimationManager.h`、`Window.h`、`ECDI/Core/ECDIAssert.h`。

## 4. 实现细节

### 4.1 构造

```cpp
CollapsiblePanel::CollapsiblePanel(){
    auto content = std::make_unique<Widget>();
    m_content = content.get();
    AddChild(std::move(content));   // 容器进树（面板最底层子节点）
    SetContentVisible(false);       // v1.2：默认收起——内容初始隐藏；展开基准由首次收起态 SetSize 定义
}
```

### 4.2 ApplyGeometry（精确实现）

```cpp
void CollapsiblePanel::ApplyGeometry(float s){
    // v1.3：位置锚定参照 = 当前几何（Layout 排位后 ≠ m_expandedRect 的 SetSize 时刻位置）
    const int cx = GetX();
    const int cy = GetY();
    const float w0 = m_expandedRect.width;
    const float h0 = m_expandedRect.height;

    switch (m_direction){
    // v1.2：SetSize 直调 Panel::SetSize + 容器同步（不走 override 虚分派）——
    // 否则收起态 SetSize override 会以 (w,0) 重定义 m_expandedRect（递归 + 基准污染）
    case ExpandDirection::Down:
        SetPosition(cx, cy);
        Panel::SetSize(static_cast<int>(w0), static_cast<int>(s));
        break;
    case ExpandDirection::Up:
        SetPosition(cx, cy + static_cast<int>(h0 - s));
        Panel::SetSize(static_cast<int>(w0), static_cast<int>(s));
        break;
    case ExpandDirection::Right:
        SetPosition(cx, cy);
        Panel::SetSize(static_cast<int>(s), static_cast<int>(h0));
        break;
    case ExpandDirection::Left:
        SetPosition(cx + static_cast<int>(w0 - s), cy);
        Panel::SetSize(static_cast<int>(s), static_cast<int>(h0));
        break;
    }
    if (m_content){
        // v1.2：容器尺寸 = 基准非动画轴 × 动画轴呈现值 s（与面板呈现一致）
        const bool vertical = (m_direction == ExpandDirection::Down || m_direction == ExpandDirection::Up);
        m_content->SetSize(vertical ? static_cast<int>(w0) : static_cast<int>(s),
                           vertical ? static_cast<int>(s)  : static_cast<int>(h0));
    }
}
```

### 4.2.1 SetSize override 收起态语义（v1.2 新增，硬契约）

收起态下外部 `SetSize` = **定义展开基准**，实际呈现保持收缩。硬契约：**非动画轴保持 SetSize 给出值（Down/Up→宽=w；Left/Right→高=h），动画轴呈现 0**。

```cpp
void CollapsiblePanel::SetSize(int w, int h){
    if (!m_expanded){
        m_expandedRect = Rect{(float)GetX(), (float)GetY(), (float)w, (float)h};  // 位置取当前值
        ApplyGeometry(0.0f);                                                       // 呈现收缩（防递归前提：§4.2 直调）
        return;
    }
    Panel::SetSize(w, h);              // 冻结点 4：先面板后容器
    if (m_content) m_content->SetSize(w, h);
}
```

**初始化顺序约定**（文档约定、无运行时 assert）：`SetExpandDirection → SetPosition → SetSize`。方向决定收缩呈现的作用轴；SetPosition 先于 SetSize（基准位置取 GetX/GetY）。

### 4.3 SetExpanded（完整流程）

```cpp
void CollapsiblePanel::SetExpanded(bool expanded){
    if (expanded == m_expanded) return;                       // 幂等

    Window* window = GetWindow();

    if (expanded){
        m_expanded = true;
        SetContentVisible(true);
        Invalidate();
        if (window == nullptr){
            ApplyGeometry(AxisTarget());                       // 无窗口：瞬时展开
            Invalidate();
            return;
        }
        const float from = CurrentAxisValue();
        const float to   = AxisTarget();
        window->GetAnimationManager().Start(
            m_sizeToken, from, to,
            std::chrono::milliseconds(kToggleDurationMs),
            Easing::EaseOut,
            [this](float s){ ApplyGeometry(s); });             // onValue 内不显式 Invalidate（manager 聚合）
    }
    else{
        if (m_expanded){
            m_expandedRect = GetGeometry();                    // 冻结点 2：仅展开态→折叠跃迁记忆
        }
        m_expanded = false;
        if (window == nullptr){
            ApplyGeometry(0.0f);                               // 无窗口：瞬时折叠
            SetContentVisible(false);
            Invalidate();
            return;
        }
        const float from = CurrentAxisValue();
        window->GetAnimationManager().Start(
            m_sizeToken, from, 0.0f,
            std::chrono::milliseconds(kToggleDurationMs),
            Easing::EaseIn,
            [this](float s){ ApplyGeometry(s); },
            [this]{
                SetContentVisible(false);                      // onFinished：动画结束才隐藏
                Invalidate();
            });
    }
}
```

辅助（私有，未列于头——实现文件内匿名函数或私有方法）：

```cpp
float CollapsiblePanel::CurrentAxisValue() const{   // 按方向取当前轴呈现值
    return (m_direction == ExpandDirection::Down || m_direction == ExpandDirection::Up)
         ? static_cast<float>(GetHeight())
         : static_cast<float>(GetWidth());
}
float CollapsiblePanel::AxisTarget() const{         // 展开目标轴值
    return (m_direction == ExpandDirection::Down || m_direction == ExpandDirection::Up)
         ? m_expandedRect.height
         : m_expandedRect.width;
}
```

- **替换式重启**：m_sizeToken 同 token 再 Start = 旧动画静默移除（不调 onFinished——折叠中途 Toggle 展开不会提前触发隐藏），from = 当前呈现值（无跳变）。
- **无窗口降级**：不依赖 Window/AnimationManager——纯几何 + visible 瞬时切换（测试可测性；动画时序由 AnimationTests 独立覆盖）。
- **onValue 不显式 Invalidate**：AnimationManager::Tick 已聚合（d4 契约）——与 demo 显式调用不同，正式控件依赖 manager 保证，避免重复。

### 4.4 与 AnimationManager 的交互契约

- 依赖仅：`GetWindow()->GetAnimationManager()`（per-Window 组合，9.6 决策）。
- 无 timer 启停直调——manager 内部处理（活动动画 → StartTimer / 归零 → StopTimer，空闲零开销）。
- Token 析构保护：CollapsiblePanel 析构（含被移除出树）→ m_sizeToken 标脏 → 动画移除且不调 onFinished——回调永不打在死对象上（AnimationToken 生命周期不变量）。

## 5. 测试设计（Phase7.2，无窗口）

新增 `ECDI/src/Tests/CollapsiblePanelTests.cpp`，`RegisterCollapsiblePanelTests()` 注册。全部无 Window 场景（无窗口降级路径 = 瞬时切换，可确定性断言）。

| # | 用例 | 断言 |
|---|------|------|
| 1 | `CollapsiblePanel.DefaultExpanded` | 构造即展开（IsExpanded()==true）、方向默认 Down |
| 2 | `CollapsiblePanel.DownCollapse` | Down 折叠 → h=0、y 不变、内容容器 invisible、容器内子控件 HitTest(局部坐标) 返回 nullptr |
| 3 | `CollapsiblePanel.UpCollapse` | Up 折叠 → h=0、底边保持（y==y0+h0）、内容 invisible（v1.2：方向先于 SetSize） |
| 4 | `CollapsiblePanel.RightCollapse` | Right 折叠 → w=0、x 不变、内容 invisible（v1.2：含非动画轴 h==400 断言） |
| 5 | `CollapsiblePanel.LeftCollapse` | Left 折叠 → w=0、右边保持（x==x0+w0）、内容 invisible（v1.2：方向先于 SetSize） |
| 6 | `CollapsiblePanel.ExpandRestoresRect` | 折叠后展开 → 几何 == m_expandedRect（按方向）、内容可见 |
| 7 | `CollapsiblePanel.ExpandAfterResize` | 折叠(记 A) → 展开 → SetSize 改 B → 再折叠 → 展开回 B（冻结点 2 语义验证） |
| 8 | `CollapsiblePanel.Idempotent` | 同状态重复 SetExpanded 无副作用 |
| 9 | `CollapsiblePanel.ToggleFlipsState` | Toggle 翻转状态（v1.2：默认收起——首次 Toggle = 展开） |
| 10 | `CollapsiblePanel.ContentContainer` | GetContent() 非空、可 AddChild、**展开态** SetSize 后容器尺寸跟随（冻结点 4 验证） |
| 11 | `CollapsiblePanel.CollapseHitTestPanelSelf` | 面板自身坐标 HitTest 返回 nullptr（v1.2：Panel 永不命中——ContainsPoint 恒 false） |
| 12 | `CollapsiblePanel.ContentFollowsGeometry`（GPT 建议） | panel (100,200,300,400) → content position==(0,0) size==(300,400)；SetSize(500,600) → content position==(0,0) size==(500,600)——防 SetSize override 同步逻辑被改坏（v1.2：几何断言在展开态查） |
| 13 | `CollapsiblePanel.RepeatedCollapseMemory`（GPT 建议） | 展开→折叠→展开→折叠：m_expandedRect 保持「最近一次进入折叠前的完整尺寸」（无窗口瞬时路径，成本极低） |
| 14 | `CollapsiblePanel.DefaultCollapsedPresentation`（v1.2 新增） | 默认收起呈现 + §4.2.1 硬契约：Down 下 SetSize(300,400) → w==300 保持 / h==0；SetExpanded(true) 恢复基准全尺寸；Right 对称断言（h==400 保持 / w==0 / x 锚定） |

测试脚手架：`HitTest` 可在无 Window 树调用（基类不依赖 Window）；断言几何用 `GetGeometry()` 浮点比较（EXPECT_NEAR）；内容容器子控件用 `GetContent()->AddChild` 后按绝对/相对坐标 HitTest。

## 6. 文件影响清单

| 文件 | 动作 |
|------|------|
| `ECDI/include/ECDI/Widget/CollapsiblePanel.h` | 新增（ExpandDirection + CollapsiblePanel） |
| `ECDI/src/Widget/CollapsiblePanel.cpp` | 新增 |
| `ECDI/src/Tests/CollapsiblePanelTests.cpp` | 新增 |
| `ECDI/src/Tests/RunAllTests.h/.cpp` | RegisterCollapsiblePanelTests 声明 + 注册调用 |
| `ECDI/ECDI.vcxproj` | 新文件加入工程 |
| `docs/phase9.6-collapsiblepanel-detailed-design.md` | 本文档 |
| `main.cpp` | **待单独授权**——四向 demo 演示（实现阶段另行确认） |

## 7. 已知限制（v0.1 冻结）

1. **动画中外部 SetSize**：不是支持场景（冻结点 6）——尺寸被 s 强制覆盖；**动画中/折叠态外部 SetPosition 已生效**（v1.3：ApplyGeometry 位置锚定当前几何，读 GetX/GetY 而非陈旧基准）。
2. **动画中 SetDirection 跨轴**：from 取新方向轴当前值，不做跨轴换算。
3. **折叠态改尺寸不同步展开基准**：~~SetSize override 只同步容器尺寸，不更新 m_expandedRect~~ **v1.2 变更**：收起态 SetSize 现已定义为「更新展开基准 + 保持收缩呈现」（§4.2.1）。**v1.3 再变更**：展开位置锚定当前几何，「先 SetSize 后 SetPosition 导致展开位置跳回」问题消失（§4.2.1 初始化顺序约定保留为文档建议——基准位置一致性的最佳实践，非硬性要求）。
4. **translate 型动画（抽屉/侧滑）**：不做（另立控件）。

## 8. 评审请求

请评审：① 冻结点 2（m_expandedRect 记忆语义）② SetSize override（内容容器跟随载体）③ onValue 不显式 Invalidate（依赖 manager 聚合）④ 无窗口降级（测试可测性）⑤ 测试 11 条覆盖是否齐全。

> v1.2 注：①② 已随默认收起变更扩展（§4.2.1 收起态语义）；评审已通过（GPT 全项授权 + 用户确认实施，2026-08-30）。

## 9. 修订记录

- v1.0（2026-08-30）详细设计初稿：7 歧义点冻结 / 接口 / 几何公式 / 11 测试。
- v1.1（2026-08-30）补测试 12/13（GPT 评审建议）。
- v1.2（2026-08-30）**默认收起变更**（详设：phase9.6-panel-container-semantics-detailed-design.md v1.1，GPT 全项授权）：m_expanded=false + 构造 SetContentVisible(false)；**收起态 SetSize = 定义展开基准**（§4.2.1 硬契约——非动画轴保持给出值、动画轴呈现 0）；**ApplyGeometry 直调 Panel::SetSize + 容器同步**（防 override 递归/基准污染）；SetExpandDirection 升格「须在首次 SetSize 前设置」（初始化约定、无运行时 assert）；测试更新（DefaultCollapsed 改名、Up/Right/LeftCollapse 方向前置、ToggleFlipsState 反转、ContentContainer/ContentFollowsGeometry 移展开态断言）+ 新增 DefaultCollapsedPresentation（14 条）。
- v1.3（2026-08-31）**位置锚定参照改当前几何**（Showcase 实测发现：CollapsiblePanel 首次放入 Layout 容器——VerticalLayout 排位后当前位置 ≠ m_expandedRect 的 SetSize 时刻位置，展开动画把面板拉回陈旧位置、覆盖 Toggle 按钮致「展开后无法收起」）：§2.1 四向公式位置部分 x0/y0 → **GetX()/GetY()（当前几何）**，尺寸部分仍用 m_expandedRect（w0/h0 基准）；§4.2 伪码同步；§7 限制 1/3 更新（折叠态外部 SetPosition 生效、「先 SetSize 后 SetPosition 跳回」消失）；测试 14 条全兼容无需改（所有用例 SetPosition 先于 SetSize，两参照等价）；用户确认方案后实施，BOM ✓
