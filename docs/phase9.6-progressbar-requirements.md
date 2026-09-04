| 项目 | 内容 |
|---|---|
| 版本 | v1.1 |
| 日期 | 2026-08-30 |
| 状态 | **已收敛（2026-08-30），进入初步设计（建议方案全过）** |
| 前置 | Phase 9.6 Animation ✅（per-Window AnimationManager + easing + 单值驱动 token）/ Phase 9 Theme ✅（Style/ApplyTheme/SetStyle D7） |

---

## 1. 背景与动机

9.6 挂账项，全仓唯一未实现控件。挂账理由：当时 AnimationManager 未落地，进度条的值跳变没有平滑过渡基建，做了也是硬切。

现在 `AnimationManager` + easing + 单值驱动 token 已全部落地（S1 Button 过渡、S2 CollapsiblePanel 消费其能力）。进度条是第三个天然消费者——`SetValue` 平滑过渡直接复用现有机制，几乎零新增基建。

用户提出「等待加载动效」需求时，第一落点即此控件。

## 2. 目标

提供一个水平（v0.1）ProgressBar 控件，展示任务进度并支持平滑过渡。

## 3. 需求清单（待逐项确认）

### 3.1 模式：determinate only ✅

- **R-A**：v0.1 仅实现 determinate（确定进度）——进度由外部 `0..1` / `0..100` 设置，条形填充比例对应变化
- indeterminate（不确定进度的循环动画 / 转圈）不在 v0.1 范围——它需要 DrawArc / PushTransform（渲染能力挂账），且无既有的循环动画 token 模式（当前 AnimationManager 仅支持有起止值的单次插值）。挂账另案

### 3.2 值模型（待确认）

- **R-B** 建议方案：内部持有 `float m_progress ∈ [0,1]`，API 两层：
  - `void SetProgress(float p)` —— 规范化 clamp 到 `[0,1]`
  - `void SetPercent(int 0..100)` —— 便捷包装，内部 `/100.0f`
  - `[[nodiscard]] float GetProgress() const`
- 理由：float 0..1 是动画插值的自然域（`AnimationManager.Start(from, to, ...)` 直接消费）；int 0..100 是用户直觉接口（demo 里直接写 `40` 比 `0.4f` 可读）

> 📌 **待确认**：是否同时保留两层 API，还是只选一种？

### 3.3 动画行为（待确认）

- **R-C** 建议方案：
  - `SetValue` 默认平滑过渡（可配置开关 / 可选时长）
  - 构造期 `SetProgress(0)` 无动画（初始态瞬时）
  - 动画中再次 `SetValue` = 替换式重启（from = 当前呈现值——与 CollapsiblePanel S2 同构）
- 复用 `AnimationToken` RAII 模式 + 现有 `Easing` 枚举（EaseOut？线性？待定）

> 📌 **待确认**：是否默认开平滑过渡？平滑时长是否开放（v0.1 建议常数、不开放）？

### 3.4 视觉结构（待确认）

- **R-D** 建议字段（`ProgressBarStyle`）：

  | 字段 | 类型 | 说明 |
  |---|---|---|
  | `trackColor` | `Color` | 轨道（空填充）背景色 |
  | `fillColor` | `Color` | 填充（进度）色 |
  | `height` | `float` | 进度条像素高度（默认 ~8） |
  | `cornerRadius` | `float` | 圆角半径（0 = 直角；建议默认 = height/2 全圆角） |

- 绘制 = 两层 DrawRect：
  1. 轨道色填满 `[0, w] × [0, h]`
  2. 填充色填满 `[0, w*s] × [0, h]`（s = 当前插值进度）
- 不涉及 DrawImage / DrawText（文本百分比见 §3.6）

> 📌 **待确认**：字段集是否足够？高度是否走 Geometry（`SetSize(w, h)`）而 Style 只存视觉（trackColor / fillColor / cornerRadius）？

### 3.5 主题集成

- **R-E**：`ProgressBarStyle` 由 `DefaultTheme::GetProgressBarStyle()` 提供默认值
- `ProgressBar` 构造期调用 `ApplyTheme(GetDefaultTheme())`
- 单实例覆盖经 `SetStyle(ProgressBarStyleOverride)`（与 Button/TextBox/Panel 同构，D7 契约）
- `ProgressBarStyleOverride` 用 `std::optional` 字段（同构）

### 3.6 文本百分比显示（待确认）

- 建议 v0.1 不做内置文本——ProgressBar 只画条，百分比 Label 由用户外部组合（与 CollapsiblePanel header 外部自组同构——单一职责，不管文字）
- 未来可做 `ShowPercent(bool)` 开关（挂账）

> 📌 **待确认**：v0.1 不内置文本 OK？

### 3.7 接口草案（v0.1 最小集）

```cpp
class ProgressBar : public Widget{
public:
    ProgressBar();

    void SetProgress(float p);          // 0..1，clamp；默认平滑过渡
    void SetPercent(int p);             // 0..100 包装（可选，见 §3.2）
    [[nodiscard]] float GetProgress() const noexcept;

    void SetStyle(ProgressBarStyleOverride override);
    void ApplyTheme(const Theme& theme);

protected:
    void OnPaint(PaintContext& ctx, int x, int y) override;

private:
    float m_progress = 0.0f;            // 当前呈现值（插值结果）
    float m_target = 0.0f;              // 动画目标值
    AnimationToken m_animToken;
    ProgressBarStyle m_style;
};
```

## 4. 与现有控件的对比

| 控件 | 继承 | 值驱动 | 动画 |
|---|---|---|---|
| Button | TextWidget | 无状态值 | 无（S1 未实装） |
| CheckBox / Radio | StateWidget | `bool m_checked` | 无 |
| CollapsiblePanel | Panel | `bool m_expanded` + 单轴几何动画 | 尺寸 200ms |
| **ProgressBar** | **Widget** | `float m_progress` | **值 200ms** |

ProgressBar 是第一个**连续值动画**控件（之前只有二值状态 + 几何动画）。

## 5. 非目标（v0.1 排除）

- 垂直方向（v0.1 只做水平；转 90° 时再做——YAGNI）
- 分段 / 条纹（DrawImage 能力未解锁）
- indeterminate 模式（DrawArc / PushTransform 未解锁）
- 内置文本百分比（外部组合）
- 样式高度脱离 Geometry（Style 只存视觉、尺寸 = Widget 几何）

## 6. 实施路径概览

1. 需求确认（本节 3.x → 用户评审）
2. 初设（类结构 + 样式字段 + 接口 + 测试草案）
3. 详设（OnPaint 双层绘制 + 动画 token 管理 + 边界 clamp）
4. 实现（ProgressBar.h/.cpp + ProgressBarStyle.h + ProgressBarTests.cpp）
5. 验证（编译 + 测试绿）

## 7. 待确认决策点（汇总）

| # | 议题 | 建议 |
|---|---|---|
| 1 | 值模型：float 0..1 + int 0..100 双层？ | 双层（内 float、外 int 包装） |
| 2 | 平滑过渡默认开？时长开放？ | 默认开；时长常数（不开放） |
| 3 | 高度走 Geometry 还是 Style？ | Geometry（`SetSize(w,h)` 既设宽度也设高度） |
| 4 | 内置文本百分比？ | 不做（外部 Label 组合） |
| 5 | 圆角默认值？ | `height / 2`（全圆角 = 视觉现代感） |
| 6 | Easing 选哪种？ | EaseOut（进度追赶感） |

## 8. 修订记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v1.0 | 2026-08-30 | 初版：determinate only / float 0..1 + int 0..100 双层 / 默认平滑过渡（时长常数）/ 高度走 Geometry / 不内置文本 / 圆角 height/2 / EaseOut |
| v1.1 | 2026-08-30 | 用户评审全过：① 双层 API（float + int 包装）② 默认平滑（常数时长）③ Geometry（`SetSize(w,h)` 设宽高）④ 无内置文本（外部 Label）⑤ 圆角 height/2 全圆角 ⑥ EaseOut。进入初步设计 |
