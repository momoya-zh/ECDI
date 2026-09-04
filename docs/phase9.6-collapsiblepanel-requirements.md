# Phase 9.6 CollapsiblePanel 需求确认（v1.1）

> 阶段：职责确认（五阶段法 ①）
> 日期：2026-08-30
> 状态：已收敛，进入初步设计（v1.1 补四向决策）

## 背景

9.6 S2 的 `CollapsiblePanelDemo`（demo 容器承载）已验证「动画系统作为 GUI 体系正常消费者」——高度 → Geometry → Layout → Invalidate → 重 Paint 全链路。按 9.6 立项定位（demo 作正式控件实现参考），本次将其**产品化升格为正式控件** `CollapsiblePanel`。

## 职责确认决策（用户逐项确认）

| # | 决策点 | 结论 |
|---|--------|------|
| 1 | 类层次 | **继承 Panel**（继承背景色/布局等 Panel 能力，加折叠语义） |
| 2 | 折叠态内容处理 | **隐藏 + 跳过命中**（内容区整体不参与 Paint 与 HitTest） |
| 3 | Header 形态 | **外部自组**（面板只做内容容器 + 折叠语义，不内置标题按钮） |
| 4 | 折叠目标尺寸 | **收缩到 0**（完全收起；外部自组 header 场景下最自然；不预置可配置字段——YAGNI） |
| 5 | 可见性机制落点 | **Widget 基类现有 visible 机制**（事实核对修正：`Widget::SetVisible/IsVisible` 已存在，`Paint`/`HitTest` 均已检查 `IsVisible()`——**零新增**） |
| 6 | **折叠方向（v1.1 新增）** | **四向（含水平）**：Down / Up / Left / Right；语义 = **header 锚定边固定、内容向对侧 resize 展开** |
| 7 | **内容承载（v1.1 新增）** | **引入内容容器 `m_content`**（裸 Widget 不画背景）——四向锚定需要统一载体做收缩 + 隐藏 |

## 边界（明确不做）

- **不做**内置 header / 标题按钮（决策 3——外部自组）
- **不做**可配置折叠尺寸字段（决策 4——先固定 0）
- **不做**全局 HitTest 裁剪修复（折叠态命中问题由 visible 机制在控件层规避；「HitTest 不校验父级裁剪」是独立架构债，另立账）
- **不做**内容容器内自动布局（用户自行给 m_content 设 Layout 或手动摆位——容器只是裸 Widget）
- **不做**「整体位移滑出（translate）」语义——折叠 = resize 收缩（对侧边向锚定边收缩），非面板平移滑出；translate 型动画（抽屉/侧滑）若未来有需求另立控件

## 关联

- 动画：复用 per-Window `AnimationManager`（9.6 落地，`Window::GetAnimationManager()`）；四向需要**位置 + 尺寸双变化**（锚定边不动、对侧边收缩）——动画回调内同帧 SetPosition + SetSize
- 样式：继承 `PanelStyle`（background）——无新样式字段，无 `CollapsiblePanelStyle`（YAGNI）
- 测试：Phase7.2 测试框架承载

## 遗留待初步设计解决

1. 展开尺寸的来源与记忆语义（收缩到 0 后展开回哪个值；锚定边坐标记忆）
2. 内容可见性切换的时序（动画开始前 / 结束后）
3. 无窗口场景的行为（测试可测性）
4. 四向时「展开尺寸记忆」与「锚定边坐标」的推导（方向影响动画哪个轴 + 位置怎么跟）
