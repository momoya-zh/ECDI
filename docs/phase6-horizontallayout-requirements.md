# Phase 6 布局系统完善 — HorizontalLayout 职责确认

> 状态：v1.0（2026-08-15）｜职责确认定稿（GPT 评审 + 用户确认），待初步设计
> 相关：phase3-layout-design.md（5.4 布局）/ VerticalLayout（已实现）
> 命名说明：原路线图"Phase 6 控件完善"精化为"**Phase 6 布局系统完善**"——本轮实际范围 = VerticalLayout → HorizontalLayout（GPT 2026-08-15 建议，控件生态名称名不副实）

## 1. Layout 边界原则（GPT 建议，写死）

**Layout 只负责坐标计算**——不负责：

| 不负责 | 归属（职责外推） |
|---|---|
| AutoSize | 未来 Widget 尺寸策略（v0.1 不做，YAGNI） |
| 裁切 | Widget/渲染层（TextBox 已有 maxTextWidth 裁切先例） |
| 滚动 | Widget/Window（Phase 6 未来，TextBox EnsureCaretVisible） |
| 换行 | 未来多行文本系统 |
| 对齐策略 | 各 Widget 自身（CalculateTextPosition 先例） |
| 父尺寸管理 | Window/Arrange 调用方 |

> 未来 GridLayout/WrapLayout/FlexLayout 不破坏此边界——Layout 永远是"给每个子控件算一个 (x,y)"。

## 2. 范围界定

### H1 范围：HorizontalLayout（v1.0 必需）
VerticalLayout 的**水平镜像**：子控件顶部对齐 + 水平流（y=0，x 从左到右累加宽度）。

```text
VerticalLayout:                     HorizontalLayout:
+-------------------+               +-------------------+
|A                  |               |ABC                |
|B                  |               +-------------------+
|C                  |               y=0
+-------------------+               x+=childWidth
x=0
y+=childHeight
```

### H2 尺寸策略
与 VerticalLayout 对称：子控件宽高由 `SetSize` 固定，x 累加**子控件宽度**；**不做 AutoSize**（YAGNI）。

### H3 对齐 — 决策点 a：**顶部对齐（y=0）** ✅
- 与 VerticalLayout 的"x=0 左对齐"**完全镜像**，零额外计算
- 垂直居中需父高度参与 → 违反"Layout 不碰父尺寸管理"原则，不做

### H4 spacing — 决策点 b：**不加** ✅
- 与 VerticalLayout 一致（紧贴排列）；spacing 会引出 SetSpacing/InvalidateLayout 连锁问题
- 未来有消费者再加（独立增强，不破坏本轮对称）

### H5 换行/溢出
**不做换行**（单行水平流）；子控件总宽超父宽 → **溢出不处理**（与 VerticalLayout 超高行为对称——父/裁剪层负责）。

### H6 与 VerticalLayout 关系 — 决策点 c：**两个独立镜像类** ✅
- 各约 10 行（VerticalLayout 累加 y / HorizontalLayout 累加 x），差异一行代码
- 不抽 `LinearLayout(Direction)`：`if/else + 状态` 反而复杂（GPT 论证）
- 触发条件：第三个方向（Wrap/Grid/Flex）出现 → 再评估 LinearLayout 基类

### H7 CheckBox / Radio — 决策点 d：**本轮不做** ✅（GPT 强烈建议）
- CheckBox/Radio 是**状态控件**（Checked/Unchecked/RadioGroup 互斥——Radio 需知道兄弟/父容器，引入 Group 概念）
- 与 HorizontalLayout（纯坐标计算）**不同功能域**；无 v1.0 硬需求 → 推迟

### H8 验证
- **断言**（main.cpp）：HorizontalLayout 排列后子控件 x/y 位置可编程断言（与 VerticalLayout 同款）
- **交互**：demo 窗口加一组水平排列控件可见

## 3. 范围纪律（GPT 拆分建议的处理）

GPT 提议 Phase 6 拆 6.1-6.4（HorizontalLayout / Clipboard / Copy-Cut-Paste / Shortcut）。**本轮只做 6.1 布局**——Clipboard/Copy-Paste/Shortcut 是**原路线图外的新需求**，各属独立功能域，**不并入本轮**（避免范围膨胀）。记入路线图未来候选，各自独立走五阶段法。

## 4. 修订记录

- v1.0（2026-08-15）职责确认定稿：H1-H8 + 决策点 a/b/c/d（GPT 全赞成）+ Layout 纯坐标原则 + 命名精化"布局系统完善" + 范围纪律（Clipboard 等另立）。
