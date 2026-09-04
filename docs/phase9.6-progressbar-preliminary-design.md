# Phase 9.6 ProgressBar 初步设计（v1.2）

> 阶段：初步设计（五阶段法 ②）
> 日期：2026-08-30（v1.2 修订 2026-08-31）
> 状态：评审通过（GPT 全项授权，2026-08-31）——待用户确认进入详细设计
> 前置：需求确认 v1.1（已收敛）/ Animation ✅ / Theme ✅

---

## 1. 概述

`ProgressBar`：水平 determinate 进度条。继承 `Widget`，值驱动 `float m_progress ∈ [0,1]`（目标/逻辑状态），平滑过渡消费 per-Window `AnimationManager`（单 token 插值），当前呈现值 `m_displayProgress`（视觉状态）。

## 2. 类结构

```
Widget
  └── ProgressBar

Theme
  └── DefaultTheme::GetProgressBarStyle() → ProgressBarStyle

样式（Theme 体系同构）
  ProgressBarStyle{ trackColor, fillColor, cornerRadius }
  ProgressBarStyleOverride{ std::optional<Color> trackColor, fillColor, cornerRadius }
```

继承 Widget（不继承 TextWidget/Label/Panel——零文本、无子容器语义，纯视觉控件）。

## 3. 状态模型（GPT v1.1 吸收）

| 成员 | 语义 | 写者 | 读者 |
|---|---|---|---|
| `m_progress` | 目标值（逻辑状态） | `SetProgress` / `SetPercent` | 业务查询 |
| `m_displayProgress` | 当前呈现值（视觉状态） | `AnimationManager` 回调 | `OnPaint` |

分离原则（与 CollapsiblePanel 的 `m_expanded` / `s` 同构，**GPT v1.2 冻结**）：**动画不产生状态，只平滑改变视觉状态、把视觉推向逻辑状态**。`SetProgress(0.8)` 只改目标；`Tick()` 驱动呈现趋近目标；`OnPaint` 用呈现值画填充宽。

## 4. 接口

```cpp
class ProgressBar : public Widget{
public:
    ProgressBar();

    // ── 值 ────────────────────────────────────────
    void SetProgress(float p);              // clamp [0,1]；默认平滑过渡（构造期瞬时）；同目标 no-op
    void SetPercent(int p);                 // clamp [0,100]；包装 SetProgress(p/100.0f)
    [[nodiscard]] float GetProgress() const noexcept;          // 目标值

    // ── 样式（D7 契约）─────────────────────────────
    void ApplyTheme(const Theme& theme);
    void SetStyle(ProgressBarStyleOverride override);

protected:
    void OnPaint(PaintContext& ctx, int x, int y) override;

private:
    /// @brief 启动到 target 的过渡动画（**只启动动画、不改逻辑状态**——m_progress 赋值由 SetProgress 负责）
    /// @details 职责分离（GPT v1.2 冻结）：SetProgress 改 m_progress（逻辑状态）→ AnimateTo 只消费
    /// 目标值启动 AnimationManager 插值（onValue 驱动 m_displayProgress）。违反此分工 = 违反「动画不产生状态」。
    void AnimateTo(float target);

    float m_progress = 0.0f;                // 目标值（逻辑状态）
    float m_displayProgress = 0.0f;         // 当前呈现值（动画驱动；静态时 == m_progress）
    ProgressBarStyle m_style;
    AnimationToken m_animToken;
};
```

## 5. 绘制管线（OnPaint）

```
命令流 = PushClip(控件边界) → DrawRoundedRect(轨道) → DrawRoundedRect(填充) → PopClip
```

**填充策略（GPT v1.1 方案 C——最 YAGNI）**：
- **轨道**：`DrawRoundedRect`（`cornerRadius` 圆角，=0 降级 `DrawRect`）
- **填充**：`DrawRect`（矩形，无圆角）——⚠️ **已被详设 v1.3 变更**：demo 实测高进度时直角填充盖满圆角轨道 → bar 呈纯矩形，用户确认改方案 D（填充 `DrawRoundedRect` 同心圆角，见详设 v1.3 修订记录）

理由：
- 最简单、最常见（主流 GUI 框架默认如此）
- 零新增能力（`DrawRect` + `DrawRoundedRect` Phase 8 已解锁）
- 低进度时无视觉 artifact（fill 宽 < 直径时仍是规整矩形，无极端圆角形变）
- 未来可升级 fill 圆角语义（挂账，需求出现再做）——**该挂账已于详设 v1.3 兑现（方案 D）**

填充宽 = `GetWidth() * m_displayProgress`

## 6. 动画集成（消费 AnimationManager）

```
SetProgress(target):
    if (fabs(target - m_progress) < kEpsilon) return;          // 同目标 no-op（避免无意义重启）
    m_progress = target;
    if (无 Window) → m_displayProgress = target; Invalidate(); return;   // 降级瞬时（测试可测性）
    AnimateTo(target);

AnimateTo(target):    // 只启动动画——不改 m_progress（逻辑状态归 SetProgress）
    AnimationManager::Start(m_animToken, m_displayProgress, target, 200ms, EaseOut,
        onValue: m_displayProgress = v; Invalidate();
    onFinished: 无（RAII token 自动）
```

- **no-op 判断键（GPT v1.2 明确）**：恒为 `target ↔ m_progress`（目标 vs 目标），**绝不**用 `m_displayProgress` 判断目标是否变化——动画进行中 `m_displayProgress` 是中间态，拿它比较会把「目标未变」误判成「需要重启」
- Easing：`EaseOut`——进度追赶感
- 替换式重启（动画中再调 SetProgress = 替换式重启，from = 当前呈现值）——与 S2 CollapsiblePanel 同构
- 构造期 `SetProgress(0)`：Window 尚未绑定，走无 Window 分支——瞬时、无动画

## 7. 主题集成

```cpp
ProgressBarStyle DefaultTheme::GetProgressBarStyle() const{
    ProgressBarStyle s;
    s.trackColor.value   = Color::FromRGBA8(220, 220, 230);   // 浅灰轨道
    s.fillColor.value    = Color::FromRGBA8(80, 120, 220);    // 主题蓝填充（同 Button/TextBox 焦点色协调）
    s.cornerRadius.value = 0.0f;                               // 覆盖由 ApplyTheme 设置；
                                                                // 默认值在 ProgressBar 构造期按 height/2 计算
    return s;
}
```

**cornerRadius 语义（GPT v1.2 冻结）**：`0 = 自动圆角（height/2）`——不区分「真正的 0 圆角」与「未指定」。理由：当前无「用户要求 ProgressBar 无圆角」的需求，为此引入 optional 语义违反 YAGNI；`effectiveRadius` 的 min(cornerRadius, height/2) 钳制由该规则自然覆盖。若未来出现「真实 0 圆角」需求，届时以新哨兵值或 optional 重议（挂账）。

构造期 `ApplyTheme` → 读 Style → 若 `cornerRadius == 0` 则按 `GetHeight() / 2` 计算全圆角。

## 8. 已知限制与记账

1. v0.1 不做 fill 圆角语义（外部组合 Label 显示百分比）——挂账
2. 垂直方向不做——挂账（转 90° 场景出现再做，YAGNI）
3. indeterminate（不确定进度循环动画）——挂账（DrawArc/PushTransform 未解锁，且无循环 token 模式）
4. 不内置文本百分比（外部 Label 组合）
5. 动画中外部 SetSize 不是支持场景（同 S2 CollapsiblePanel 限制，冻结点延伸）
6. fill 宽极端小（< 2×cornerRadius）视觉规整——因 fill 是矩形，无 artifact

## 9. 测试计划（11 条，GPT v1.1 吸收）

| # | 名 | 断言 |
|---|---|---|
| 1 | `ProgressBar.DefaultValues` | `GetProgress()==0` / `GetHeight()` 构造默认值 |
| 2 | `ProgressBar.SetProgressClamp` | `SetProgress(2.0f)` → `GetProgress()==1.0f` / `SetProgress(-1.0f)` → `==0.0f` |
| 3 | `ProgressBar.SetPercent` | `SetPercent(50)` → `GetProgress()≈0.5f`（EXPECT_NEAR） |
| 4 | `ProgressBar.PaintTwoLayers` | 命令流 size==4（PushClip+DrawRoundedRect+DrawRect+PopClip）；填充宽 == w × m_displayProgress |
| 5 | `ProgressBar.PaintZeroProgress` | `SetProgress(0)` → 填充 DrawRect 宽 == 0 |
| 6 | `ProgressBar.SetStyle` | SetStyle 设色 → 填充色立即反映；ApplyTheme 不回退（D7） |
| 7 | `ProgressBar.ApplyTheme` | 构造后 ApplyTheme → trackColor / fillColor == DefaultTheme 默认值 |
| 8 | **`ProgressBar.AnimationProgresses`**（v1.1 新增） | 有 Window 下 `SetProgress(1)` → Tick(50ms) → `m_displayProgress > 0 && < 1` → 继续 Tick → `m_displayProgress == 1`。**只断言区间/终值，不断言中间具体数值**（v1.2 冻结——不把 EaseOut 具体曲线绑死，未来换三次曲线无需改测试） |
| 9 | **`ProgressBar.AnimationReplacement`**（v1.1 新增） | 动画进行中 `SetProgress(0.5)` → 替换式重启（from = 当前呈现值，无跳回 1 或 0） |
| 10 | **`ProgressBar.IdempotentTarget`**（v1.1 新增） | 同目标 `SetProgress(0.5)` 二次调用 = no-op（无新动画启动） |
| 11 | **`ProgressBar.ResizeFillGeometry`**（v1.1 新增） | `SetSize(400, h)` 后填充宽 = 400 × m_displayProgress（填充宽度跟随 Widget 宽度） |

## 10. 文件影响清单（实现阶段授权）

| # | 文件 | 动作 |
|---|---|---|
| 1 | `include/ECDI/Theme/ProgressBarStyle.h` | 新增（ProgressBarStyle + ProgressBarStyleOverride） |
| 2 | `include/ECDI/Widget/ProgressBar.h` | 新增 |
| 3 | `src/Widget/ProgressBar.cpp` | 新增 |
| 4 | `src/Theme/DefaultTheme.cpp` | 新增 `GetProgressBarStyle()` |
| 5 | `src/Tests/ProgressBarTests.cpp` | 新增（11 条） |
| 6 | `src/Tests/RunAllTests.h/.cpp` | 注册 `RegisterProgressBarTests` |
| 7 | `docs/phase9-theme-system-detailed-design.md` | v1.7 → v1.8（增 ProgressBarStyle） |

不改动：ProgressBar 不继承 Panel/TextWidget → 不影响 Panel/TextWidget 测试；不触及 Animation 框架（只消费）。

## 11. 评审结论（GPT 2026-08-31）

全项通过——继承 Widget / 状态分离（冻结）/ 双层 API / 同目标 no-op / Fill 矩形 / Track 圆角 / EaseOut / 单 Token / 不改 AnimationManager / 挂账合理 / 11 条测试基本完整。**可以进入详细设计。**

v1.2 吸收的实现级注意点：① no-op 判断键恒为 `target ↔ m_progress`；② `cornerRadius == 0` 冻结为「自动圆角（height/2）」；③ 动画测试不绑定具体 easing 数值；④ `AnimateTo` 只启动动画不改逻辑状态。

## 12. 修订记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v1.0 | 2026-08-30 | 初版：继承 Widget / float 0..1 + int 0..100 双层 API / 双层 DrawRect 绘制 / AnimationManager 消费 / 8 条测试 |
| v1.1 | 2026-08-30 | 吸收 GPT 评审：**fill 方案 C**（track rounded + fill rect——最 YAGNI、零新增能力、低进度无 artifact）；**状态分离**（`m_progress` 目标/逻辑状态 + `m_displayProgress` 呈现/视觉状态——与 CollapsiblePanel m_expanded/s 同构）；**测试 8→11**（新增 AnimationProgresses / AnimationReplacement / IdempotentTarget / ResizeFillGeometry）；**同目标 no-op**（fabs epsilon 比较，避免无意义重启） |
| v1.2 | 2026-08-31 | 吸收 GPT 终审：**cornerRadius 语义冻结**（`0 = 自动圆角 height/2`，不引入 optional——YAGNI）；**no-op 判断键明确**（恒为 `target ↔ m_progress`，绝不用 m_displayProgress）；**动画测试不绑 easing 数值**（只断言区间/终值）；**AnimateTo 职责写死**（只启动动画、不改逻辑状态）；状态分离原则冻结；状态更新为评审通过 |
| v1.3 | 2026-08-31 | **方案 C → D 变更标注**（用户授权，详见详设 v1.3）：填充 `DrawRect` → `DrawRoundedRect` 同心圆角——demo 实测「高进度 fill 盖满圆角轨道 → bar 呈矩形」观感缺陷修复；§5 填充策略段同步标注；fill 圆角挂账兑现 |
