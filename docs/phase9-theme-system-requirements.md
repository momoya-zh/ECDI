# Phase 9 主题系统 — 职责确认

> 状态：v1.1（2026-08-25）｜职责确认待审（GPT 评审整合）
> 前序：Phase 8 渲染能力 ✅（Alpha 混合 + 顶降 DIB + PushClip/PopClip）/ Phase 8.5 文本系统 2.0 ✅
> 相关：phase8-rendering-enhancement-requirements.md（Phase 8 能力层）/ phase5-button-requirements.md（Button 样式硬编码先例）

## 1. 范围（5 项）

| # | 项 | 说明 |
|---|---|---|
| T1 | **主题类结构** | `Theme` 抽象基类 + `DefaultTheme` 默认实现（颜色/字体/圆角/边框/间距） |
| T2 | **样式注入机制** | `ApplyTheme()`（主题变更时整体重应用）+ `SetStyle()`（运行时单属性覆盖） |
| T3 | **Alpha Blending 消费** | Theme/Widget 使用 Phase 8 已提供的 `AlphaBlend`/`DrawImage` 能力实现带透明度的控件视觉效果 |
| T4 | **控件迁移** | Button / Label / TextBox / CheckBox / Radio / Panel——构造函数硬编码 → 主题注入 |
| T5 | **主题与渲染能力集成** | Theme 提供的颜色/圆角/边框等 Style 参数通过**既有** RenderingBackend 能力完成绘制（Phase 8 能力层已完备，Phase 9 只消费不新增后端能力） |

## 2. 关键决策点（7 项）

| # | 决策点 | 倾向 | 理由 |
|---|---|---|---|
| **D1** | 主题类结构 | **独立 Theme 类 + DefaultTheme 默认实现**（不要求 Singleton 语义，提供默认主题实例即可） | YAGNI——当前无多主题需求（深色/浅色切换留二期）；避免 Singleton 锁死未来多窗口多主题架构 |
| **D2** | 样式注入时机 | **两套并存**：`ApplyTheme()` 整体应用 + `SetStyle()` 单属性覆盖 | ApplyTheme 用于主题切换；SetStyle 用于运行时动态高亮（如 Button hover） |
| **D3** | Alpha 混合实现 | **预乘 BGRA + 顶降 DIB**（`biHeight < 0`） | `AC_SRC_ALPHA` 要求 RGB 已预乘 alpha；顶降 DIB 是 Win32 标准（与 Phase 8 能力层对齐） |
| **D4** | 颜色格式 | **主题层 float RGBA**（与 `Color` 一致），渲染边界转 BGRA | 决策层用浮点（精度/插值友好），BGRA 转换推迟到 `RenderingBackend::AlphaBlend` 入口 |
| **D5** | 控件迁移策略 | **一次性全迁移**（v0.1 硬编码全部移除） | 避免样式散落两处（构造函数 + 主题）；一次性迁移比逐步迁移更不易出错 |
| **D6** | Theme 与 Style 关系 | **Theme = 默认视觉规范；Style = 某个 Widget 实际使用的视觉属性**（如 `ButtonStyle` / `TextBoxStyle`）；`ApplyTheme` = Theme → Widget.m_style；`SetStyle` = 修改 Widget.m_style（不直接修改 Theme） | 现代 UI 框架标准分层：Theme 提供默认值，Widget 持有自己的 Style 副本，运行时修改不影响 Theme |
| **D7** | ApplyTheme 与 SetStyle 覆盖规则 | **ApplyTheme 只更新未被 SetStyle 覆盖的属性**（现代 UI 框架方式）；例如 `SetStyle(.background=Red)` 后 `ApplyTheme(darkTheme)` 仍保持 `background=Red`，其余属性（border/text/radius）更新为 darkTheme 默认值 | 用户局部覆盖优先于主题默认值；避免 ApplyTheme 覆盖用户运行时自定义样式 |

## 3. 不在范围（YAGNI 硬约束）

- 多主题切换（深色/浅色/自定义）——留 Phase 9+ 或二期
- 主题序列化/配置文件——留二期
- 动画过渡（主题渐变）——留 Phase 9.5 收尾
- 控件层级样式继承（子控件继承父控件主题）——留二期
- 多窗口多主题（每个 Window 独立主题）——留二期（D1 不锁死 Singleton）

## 4. 架构边界（锁死）

```
┌──────────────────────────┐
│          Theme           │
│                          │
│ 默认视觉规范              │
│ Color / Font / Radius    │
│ Border / Padding / ...   │
└────────────┬─────────────┘
             │ ApplyTheme
             ↓
┌──────────────────────────┐
│          Widget          │
│                          │
│ 实际 Style                │
│ + Runtime Override       │
└────────────┬─────────────┘
             │
             ↓
┌──────────────────────────┐
│         Renderer         │
│                          │
│ 将 Style 转成绘制命令      │
└────────────┬─────────────┘
             │
             ↓
┌──────────────────────────┐
│    RenderingBackend      │
│                          │
│ DrawRect                 │
│ DrawRoundedRect          │
│ DrawImage / AlphaBlend   │
│ PushClip / PopClip       │
└──────────────────────────┘
```

## 5. 修订记录

- v1.1（2026-08-25）GPT 评审整合：T3/T5 重新措辞（避免重复描述 AlphaBlend 能力，明确 Phase 8 只提供能力、Phase 9 只消费）；**D1 取消 Singleton 锁死**（默认实现即可，避免未来多窗口多主题受限）；**新增 D6**（Theme 与 Style 关系：Theme=默认规范，Style=Widget 实际属性）；**新增 D7**（ApplyTheme 与 SetStyle 覆盖规则：局部覆盖优先，ApplyTheme 只更新未被覆盖的属性）。
- v1.0（2026-08-25）职责确认初稿：T1-T5 范围定义，D1-D5 决策点与倾向。
