# Phase 9.7 自适应布局 需求确认（v1.0）

> 阶段：职责确认（五阶段法 ①）
> 日期：2026-09-01
> 状态：待用户确认（决策 1/2/3 已由 GPT 评审定死倾向，其余决策点待逐项拍板）
> 前置分析：Zcode 三层断点诊断 + GPT 评审通过（2026-09-01——"总体方向通过，职责确认把三个决策定死后进初步设计"）

## 背景

**需求来源**：ModelProbe demo（`src/Demo/ModelProbe.cpp`，仿照用户自有 Qt 项目 model-probe-gui）暴露的布局能力缺口——Qt 版用 `addWidget(w, 1)`（伸缩）+ `setFixedWidth`（固定）+ `addStretch`（弹性空隙）+ 嵌套 H/V 表达自适应；ECDI 版只能全部硬编码 `SetSize(600, 32)`，间距靠透明 Widget 撑高（`ModelProbe.cpp:49` 注释自认"VerticalLayout 无间距——透明 Widget 撑高"——用控件模拟布局属性，布局系统缺陷信号）。

**现状三大断点**（诊断结论，GPT 确认）：

1. **触发链断**：`Window::OnResized` 只 `RootWidget->SetSize(w, h)`，不触发 Arrange——窗口拉伸整棵树纹丝不动（Arrange 仅 main.cpp 启动时手动调一次）。
2. **Layout 不碰尺寸**：V/H Layout 被契约 10 锁死"只排 Position 不改 Size"——子控件尺寸全手写。
3. **无 stretch/spacing/sizeHint**：Qt 自适应四原语（伸缩因子/固定尺寸/弹性空隙/嵌套组合）ECDI 只有嵌套组合。

**阶段定位**（GPT 定调）：让 ECDI **第一次具备真正意义上的窗口尺寸自适应能力**——不是"实现完整自适应布局系统"。ProgressBar 补齐的是"控件能力"，本阶段补齐的是"控件组合能力"，两者落地后 ECDI 从"可以把控件画出来"跨到"可以用这些控件组装真正能用的 GUI"。

## 职责确认决策

| # | 决策点 | 结论 |
|---|--------|------|
| 1 | **stretch 归属与语义**（GPT 定死） | **属于 Widget**：`SetStretch(int)`，默认 0。语义 = **"父 Layout 分配剩余空间时，我按此权重参与分配"**——⚠️ `stretch=0` ≠ 固定尺寸，= **不参与剩余空间分配**；固定尺寸由 `SetSize` 表达（语义干净：尺寸来源二分——自己 SetSize 的 + 从剩余空间分得的）。分配实现必须走 `child->SetSize()` 虚分派（保 TextBox override 的 EnsureCaretVisible / CollapsiblePanel 收起态语义——契约 10 修订为"Layout 分配尺寸须走 SetSize 虚分派，不直写 geometry"） |
| 2 | **spacing 归属**（GPT 定死） | **属于 Layout**（V/H 各自持有，默认 0）。**v1.0 必须有**——优先级高于 minSize（透明 Widget 撑间距 hack 每多一个 demo 就多一批垃圾代码） |
| 3 | **minSize / sizeHint 不做**（GPT 定死） | 挂账。不引入 QSizePolicy 枚举（Fixed/Preferred/Expanding… Qt 20 年包袱）——否则 Box Layout 很快变成第二套 QLayout |
| 4 | **布局模型** | Qt Box 最小集：**固定尺寸 + 剩余空间 + stretch 比例 + spacing + H/V 嵌套**。跟随 Qt 心智（参照项目与 demo 均按此写） |
| 5 | **触发链接通** | `OnResized` 补 `Arrange()`——`ArrangeInternal` 渐进语义天然兼容：有 Layout 的节点重排、无 Layout 的节点保持手写 position（递归继续下钻），存量手摆代码零破坏 |
| 6 | **向后兼容硬契约** | **全 stretch=0 + spacing=0 时，V/H Layout 行为与现状逐字节一致**（只排位置、不碰尺寸 = stretch 全 0 的特例）——既有 demo/Layout 测试/CollapsiblePanel 零改动 |
| 7 | **验收载体** | **ModelProbe（Qt 版行为基准）**：拖大窗口 → BaseURL/Key 输入框跟随变宽、按钮保持合理尺寸、Models 列表吃剩余空间——不另造布局测试 demo，行为对照有真实基准 |
| 8 | **LinearLayout 抽象重启** | 9.5 R2 关闭该抽象时留了重启条件"第三布局立项时重启"——本阶段即触发。V/H 将出现共享"剩余空间分配"逻辑，届时评估提取（预判：纯函数 `DistributeSpace` 级别，非类层次；初步设计回答） |

## 边界（明确不做——全部挂账）

- **sizeHint / 内容测量**（按钮随文字变宽级别）——技术能力已具备（TextMeasurer 非 Paint 期访问 5.5 已解，见"与 9.8 AutoSize 的关系"§1）；v1.0 不做的真实理由 = demo 全固定文案无需求 + 独立立项 9.8
- **minSize / maxSize**——stretch 控件缩到 0 被 R1 Clip 裁掉是可接受退化
- **wrap content / 自动换行 / constraint / flex-grow·flex-shrink 全套 / responsive breakpoint**
- **alignment（对齐）**——V 布局子高度不满时的对齐策略 v0.1 不做（顶部对齐 + 拉伸两态已覆盖 demo）
- **QSizePolicy 式策略枚举**（决策 3）
- **脏区/重排性能优化**——拖拽 resize 高频全树重排 + 重绘，几十控件规模无感（Qt 亦同步重排）；记账
- **改 main.cpp**——demo 接线需单独授权（skill 条 2）

## 关联

- **契约 10 修订**（决策 1）："Layout 不修改子尺寸" → "Layout 尺寸分配走 `SetSize()` 虚分派"——TextBox/CollapsiblePanel 的 SetSize override 语义全保留
- **动画冲突边界**：CollapsiblePanel 冻结点 6"动画中外部 SetSize 不是支持场景"不变——布局 SetSize 与其一致
- **9.5 R2 重启**（决策 8）：LinearLayout 抽象评估重开，结论落初步设计
- **测试**：Phase7.2 框架承载（无窗口可测——Layout 是纯计算，SetSize/SetStretch 后 Arrange 断言几何）

## 与 9.8 AutoSize 的关系（2026-09-01 对照 phase9.8-autosize-requirements.md 后新增）

**同名 9.7 双文档澄清**：WorkBuddy 同日另立 `phase9.8-autosize-requirements.md`（原 9.7，**用户裁决升 9.8**，2026-09-01——ModelProbe P1 实操暴露的"内容→尺寸"自适应：GetPreferredSize/AutoSize）——与本阶段"窗口→尺寸分配"（stretch/spacing/触发链）**正交**；其 §1 边界声明也明确把"窗口自适应布局"划出需求外。两文档互补，**本阶段保留 9.7 编号（GPT 三决策评审基于本 scope），AutoSize 改挂 9.8**（已裁决，改名完成）。

从中吸收的三点：

1. **事实修正（本页"sizeHint 挂账"理由更新）**：原引用 skill 条 20"TextMeasurer 非 Paint 时刻拿不到测量"已过时——该债务 5.5 已解（TextBox 点击定位/GetCaretClientGeometry 均在非 Paint 期经 `GetWindow()->GetTextMeasurer()` 测量，代码先例在）。sizeHint 挂账的**真实理由**从"能力不具备"修正为"能力已具备、v1.0 需求未出现（demo 全固定文案）"——9.8 落地时无技术拦路虎。
2. **显式尺寸优先级同构**（autosize R5"显式 SetSize 不被 AutoSize 覆盖"）：与本阶段 stretch 语义互为印证——两文档独立收敛到同一原则：**尺寸来源二分（显式 SetSize 优先 / 分配或自适应兜底）**。
3. **stretch opt-in 绕开"尺寸来源标志"**：autosize 方案 B（布局期协商）需给 Widget 加"是否显式 SetSize 过"标志（geometry 默认 0×0 无法区分未设置）；本阶段 stretch>0 = 显式 opt-in 参与分配，**无需标志位**——该模式是 9.8 方案 B 未来升级的现成先例（参与协商的控件显式声明，而非猜测意图）。

⚠️ 顺带记录：autosize 文档 §4"百分比/权重尺寸（Layout 特性——**需求未出现**）"一句已过时——权重尺寸（stretch）需求已随 ModelProbe 出现并立项本阶段；该文档评审升 9.8 时应同步修正。


## 遗留待初步设计解决

1. **spacing API 形态**：构造参数 vs `SetSpacing` vs 两者（倾向：构造参数带默认 0 + 无 setter——YAGNI）
2. **分配算法细节**：剩余空间 < 0 时怎么退化（压缩 stretch 控件到 0 + Clip 裁？）；整数权重的取整规则（累计误差处理）；stretch 控件在 V 布局（高）与 H 布局（宽）同一 int 按所在布局轴解读（子只有一个父——单字段够）
3. **Arrange 触发点精确位置**：`OnResized` 内直接调 vs `RootWidget::SetSize` override 挂钩；重排后 Invalidate 归谁（Window 还是 Arrange 链路自带）
4. **根容器跟随窗口**（真实缺口，诊断时未展开）：ModelProbe 根容器是 RootWidget 的子，RootWidget 自身无 Layout——根容器怎么吃满窗口？方向：给 RootWidget 设 V Layout + 根容器 stretch=1（新系统自然表达，零特例）——初步设计验证
5. **Layout 测试基线更新**：Layout.HorizontalLayout/VerticalLayout 既有期望在 stretch=0/spacing=0 下不变（决策 6 兜底）——新增 stretch/spacing 分配用例清单
