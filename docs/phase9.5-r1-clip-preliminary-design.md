# Phase 9.5 R1 Clip 栈消费层（Clip/Dirty Region/Partial Redraw）——初步设计

> 状态：v1.1（2026-08-27）｜五阶段法第 2 步（职责确认 ✅ v1.1 定稿 → **初步设计** → 详细设计 → 实现 → 测试）
> 升级记录：v1.0 GPT 评审**有条件通过**（不要求重改，13 条全采纳无推翻）——补 §5 深度计数不变量 + §6.5 详细设计必审清单（5 项）
> 承接：phase9.5-wrapup-requirements.md v1.1（R1 = Clip 栈机制消费层——① Widget 绘制阶段 Clip 生命周期规则 ② TextBox 裁切迁移；**不是 Dirty Region 系统**）
> 相关：phase4-renderer-design.md（决策 28 局部重绘记账）/ phase8-renderer-design.md（PushClip/PopClip 能力 §8.5）/ phase8.5.2-multiline-textbox（O(n²) 裁切债务来源）

## 0. 背景与目标

- **现状事实**（已核实源码）：
  - `Widget::Paint`（Widget.cpp:192）：`IsVisible → OnPaint → 遍历 children Paint`——**无任何裁剪**，子控件越界父边界仍可见
  - `PushClip/PopClip` 能力层完整：RenderCommand（PushClipCommand/PopClipCommand）+ PaintContext + GDIBackend（SaveDC/IntersectClipRect + RestoreDC，空栈防御已有）+ RecordingBackend 记录——**Phase 8 已就绪，零改动**
  - TextBox 绘制裁切（TextBox.cpp:965-984）：超宽行**逐码点 MeasureText + substr 截断**（O(n²)），Selection 高亮同样 clamp（959-960）
- **目标**：① 建立 Paint 管线的 Clip 生命周期规则（统一、可配对、防越界）；② TextBox 绘制路径裁切迁移到 Clip（O(n²) → O(1)）

## 1. 方案总览：管线统一 Clip（D1）

```
Widget::Paint(ctx, offsetX, offsetY):
    if (!IsVisible) return
    x = offsetX + m_geometry.x; y = offsetY + m_geometry.y

    ctx.PushClip(Rect{x, y, w, h})     // ① 自身边界入栈（绝对坐标，同 OnPaint 的 x/y 来源）
    OnPaint(ctx, x, y)                  // ② 画自己（受自身边界裁剪）
    for child: child->Paint(ctx, x, y)  // ③ 子级——继承父裁剪，嵌套自身裁剪（交集）
    ctx.PopClip()                       // ④ 出栈（严格配对）
```

- **嵌套交集语义**：PushClip = 当前 clip ∩ 新 rect（GDI `IntersectClipRect` 天然语义）——子控件被"父边界 ∩ 自身边界"自动裁剪，**无需任何子级感知**
- **覆盖范围**：所有控件统一获得边界裁剪——TextBox 超宽文本、子控件越界绘制（行为修正）、未来动画（9.6 展开/缩放过程防溢出）
- **代价**：每控件每帧一次 SaveDC/IntersectClipRect/RestoreDC——v1.0 规模（几十控件）可接受；性能记账（§5 R3）

## 2. 决策点（初步设计收敛）

| # | 决策 | 倾向结论 |
|---|---|---|
| **D1** | 管线统一 vs 控件自愿 | **统一管线规则**——一处实现全局生效、防越界绘制、控件零负担；控件自愿会漏（TextBox 迁移后无其他控件保证） |
| **D2** | TextBox 裁切迁移范围 | **绘制路径三处手动截断全删**（② 行文本 972-983、① Selection clamp 959-960、③ 组合下划线同款），依赖 Clip；**交互路径不动**（CaretIndexFromPosition/Selection 几何——非绘制） |
| **D3** | Push/Pop 配对契约 | **契约约定成对**（控件内自 Push 必须自 Pop）+ GDIBackend 空栈防御兜底（已有）；不引入 RAII 包装（YAGNI——首个用例是管线自身，控件自 Push 场景尚未出现） |
| **D4** | Invalidate 语义 | **保持全窗布尔不变**（职责确认 d3 确定）——不做 Dirty Region/区域追踪 |

## 3. 关键机制与语义

### 3.1 Clip 边界开闭区间（详细设计必审）
- DrawRect/DrawText 契约 = `[x, x+width)` 开区间（决策 24/25）；**PushClip 的 rect 必须同约定**——焦点框 1px 边缘点线、Radio 圆点（radius=boxSize/2）是否被自身裁剪"吃掉半个像素"，详细设计需核实 GDIBackend::PushClip 的 IntersectClipRect 传参并冻结

### 3.2 行为修正：越界子控件
- 现状：子控件画出父边界 = 可见（无裁剪）
- 修正后：越界部分被裁（标准 GUI 语义）
- 详细设计需**核查无既有消费场景依赖越界可见**（demo 控件均在 layout 内、Radio 圆点/Button 焦点框/TextBox 光标均在自身边界内——预期无影响，但需列清单确认）

### 3.3 0 尺寸防御
- 未 SetSize 的控件（0×0）→ IntersectClipRect 空区域 → 自身与子级全部不可见（期望行为）
- 详细设计确认空区域 IntersectClipRect 无副作用（不崩溃、不影响后续 PopClip）

### 3.4 TextBox 迁移后的边界
- `maxTextWidth` 绘制用途删除后，**交互路径的可视宽计算（TextBox.cpp:335 同款逻辑）保留不动**——两者独立
- 组合串下划线、光标竖线绘制不受影响（均在自身边界内，Clip 不误伤）

## 4. 明确不做（YAGNI 边界）

- Dirty Region / 区域追踪 / InvalidateRect 复杂语义（Invalidate 保持全窗布尔）
- 嵌套裁剪优化（无瓶颈证据）
- Clip RAII 包装（首个用例不存在）
- PushTransform、圆角裁剪区域（Clip 只有矩形——圆角控件按包围盒裁剪，圆角外 1px 角落可越界绘制，接受）
- 局部重绘性能收益（那是 Dirty Region 的事，不是 Clip）

## 5. 测试策略（RecordingBackend 命令断言）

| 用例 | 断言 |
|---|---|
| **R1-S1 管线配对（深度计数）** | 绘制 3 层树（Panel→Button→Label）→ PushClip/PopClip **深度序列** `0→1→2→3→2→1→0`（终值 0——比 Push==Pop 更强）+ **嵌套顺序**（Push A→Push B→Pop B→Pop A，禁止 Pop A→Pop B） |
| **R1-S2 裁剪矩形正确** | 控件绝对坐标 PushClip 的 rect = offsetX+geometry.x/y + 宽高（Panel 嵌套验证坐标累加） |
| **R1-S3 TextBox 超宽不截断** | 超宽行 → DrawText 命令文本 = **整行**（不再 substr）+ 该行绘制位于控件 PushClip 之后 |
| **R1-S4 Selection 高亮不 clamp** | 超宽行 Selection → 高亮 DrawRect 画满（rect 不截断到 maxTextWidth）——由 Clip 裁 |
| **R1-S5 越界子控件** | 子控件部分超出父边界 → 命令流含父边界 PushClip（绘制约束由后端保证） |
| **R1-S6 不可见不裁剪** | IsVisible=false 的控件 → 无 PushClip（Paint 提前 return） |

## 6. 风险登记（详细设计重点审）

| # | 风险 | 处置 |
|---|---|---|
| **R1** | Clip 开闭区间与绘制契约不一致 → 边缘像素被误裁 | 详细设计核实 IntersectClipRect 传参 + 焦点框/Radio 圆点视觉核查 |
| **R2** | 越界可见行为被修正 → 潜在依赖 | 详细设计列既有消费清单确认（预期无） |
| **R3** | 每控件 SaveDC/RestoreDC 性能 | v1.0 规模可接受；记账——出现瓶颈时后端优化（如仅需裁控件才 Push 或后端批处理），**不在本期做** |

### 6.5 详细设计必审清单（v1.1 新增——GPT 评审 checklist，5 项）

| # | 必审项 | 内容 |
|---|---|---|
| **1** | 坐标系 | 冻结转换链：`Widget Geometry（父相对）→ Paint offset 累加 → PushClip Rect（Window 客户区绝对坐标）→ RenderCommand`——后端零感知 Widget Tree；防"Panel(100,100)+Button(20,20)"把 Clip 误当 (0,0,100,50) |
| **2** | Clip 与 Paint 包围关系 | 冻结：**所有 Widget 统一生命周期** `PushClip → OnPaint → children Paint → PopClip`（无例外路径；IsVisible 提前 return 不 Push） |
| **3** | Clip 边界映射 | 核实 `[x, x+w)` 契约 → `IntersectClipRect(hdc, l, t, r, b)` 传参；实际核查 1px 焦点框 / Radio 圆点 / Button 边框 / Caret / TextBox 边缘像素不被误裁 |
| **4** | TextBox 三处截断逐项确认 | ① Selection clamp（959-960）② 行文本 substr（972-983）③ 组合下划线同款——**逐项列出删除**；同时核查 Selection 超出部分是有意几何（Clip 正合适）还是既有逻辑 bug（不能靠 Clip 掩盖） |
| **5** | 既有越界绘制消费者 | **实际搜索** Paint/Draw 路径有无控件故意绘制到自身边界外；无 → 接受行为修正；有 → 单独处理（不为它否掉统一 Clip） |

## 7. 修订记录

- v1.0（2026-08-27）初稿：方案总览（管线统一 PushClip/PopClip 配对）+ D1-D4 决策点 + 关键机制（开闭区间/越界修正/0 尺寸防御）+ 测试策略 6 例 + 风险登记 3 项。
- v1.1（2026-08-27）GPT 评审整合（13 条全采纳无推翻，结论"有条件通过，不要求重改"）：① R1-S1 强化为深度计数 + 嵌套顺序断言（0→1→2→1→0 终值 0）；② 新增 §6.5 详细设计必审清单 5 项（坐标系 / 包围关系 / 边界映射 / TextBox 三处截断逐项 / 越界消费者搜索）；③ 明确 0×0 控件不做 Widget 层 early return（让 Clip 自然处理空交集）；④ 确认不提前塞 Transform/动画（9.6 消费，R1 保持小而硬）。
