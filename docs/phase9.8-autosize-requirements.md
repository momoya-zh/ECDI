# Phase 9.8 AutoSize 需求确认（v1.5）

> 阶段：需求确认（五阶段法 ①）
> 日期：2026-09-01（v1.5 修订 2026-09-02）
> 状态：**v1.5 定稿**（v1.4 GPT 职责审查通过；v1.5 修正 R5 语义与初设 §4 的矛盾——GPT 初设评审 2026-09-02 指出，见修订记录；**配合初设 v1.1 进入详细设计**）
> 前置：ModelProbe P1 实操暴露（用户观察：「我们其实缺少自适应尺寸的功能」）
> 文档目录：框架能力 → docs/（与 phase9.6-* 同级；ModelProbe demo 文档在 model-probe-docs/ 不受影响）
> **边界一句话（GPT 审查定调）**：9.8 = 「**让控件知道自己需要多大，并允许调用方显式让它调整到这个尺寸**」——不是「建立完整的 GUI 尺寸协商系统」（sizeHint/minSize/maxSize/policy/alignment/negotiation 全部不做）。
> **编号变更（v1.1）**：原 9.7 → **9.8**（用户裁决 2026-09-01）——9.7 编号归「自适应布局」；本文档「内容→尺寸」与 9.7「窗口→尺寸分配」正交（§7）。
> **v1.2 重新评估（2026-09-02）**：9.7 落地后语境变化——剩余需求收敛为动态文本、§2 R5 尺寸来源三分、§3.5 交互语义冻结。
> **v1.4 GPT 职责审查**：R5「SetSize 是操作不是状态」→ 改「尺寸意图三分」；§3.5 no-op 语义澄清（调用时判断非永久关闭）；§3.2 padding 冻结原则；§3.6 垂直居中降级为验证项；R4 删除自动挂钩；新增 §3.7 AutoSize 副作用边界。

---

## 1. 背景与动机

**现状事实**（v1.2 按代码事实更新——9.7 落地后）：

| 事实 | 状态 | 位置 |
|---|---|---|
| 全框架无 `GetPreferredSize` / `AutoSize` | **仍成立**（本阶段主缺口） | `Core/Size.h` 仅注释「未来 GetPreferredSize() 使用」 |
| Layout 排位只读 `GetWidth()` 累加、不修改子尺寸 | **已过时**——9.7 落地：Layout 分配尺寸走 `SetSize()` 虚分派（契约 10 修订），stretch 子参与剩余空间分配 | `HorizontalLayout.cpp`（9.7 后） |
| 控件尺寸硬编码 | **大幅缓解**——9.7 `fillCrossAxis=true` 已覆盖跨轴宽度（ModelProbe 手写 600 宽被跨轴填充取代）；`spacing=10` 取代 MakeSpacer（spacer 函数已删）；主轴剩余由 `SetStretch(1)` 分配（list） | `ModelProbe.cpp:185/232/367` |
| 剩余缺口 | **内容驱动尺寸**：动态文本控件的宽度不随文本变化 | `m_statLabel`（statRow H 布局内固定 180 宽——「共 X 个模型 · 已选 Y」/错误消息长度变化会截断）；模型行 Label（动态 id/meta） |

**动机（v1.2 收敛）**：9.7 解决了「窗口 → 尺寸分配」，剩余缺口收敛为「**内容 → 尺寸**」——动态文本控件的尺寸不随文本变化。最真实场景 = `m_statLabel`：状态消息在「共 0 个模型 · 已选 0」（约 20 字符宽）与「后端进程意外退出（未收到 DONE）」（更长）间切换，固定 180 宽要么截断要么浪费。框架缺少「内容 → 尺寸」的自适应能力，这是 GUI 框架的基础能力缺口。

**本次不解决的问题**（明确边界）：窗口尺寸自适应（拉伸窗口 → 剩余空间分配）**9.7 已完成**；多行高度自适应（§3.4 挂账）；模型列表高度等应用层组合问题。

---

## 2. 需求条目

### R1：尺寸协商查询（核心）

`Widget` 新增虚方法 `GetPreferredSize()`：

- 语义：控件「希望」的尺寸（内容驱动；未 override = 当前尺寸——零回归）
- 默认实现：返回当前 `GetWidth()/GetHeight()`
- `TextWidget` override：文本测量 → `(文本宽 + 内边距, 行高)`（`TextMeasurer` 非 Paint 期访问能力 5.5 已解）

### R2：文本控件自适应

`Label` / `Button` / `TextBox` 经 `TextWidget` 获得内容感知的 preferred 尺寸：

- 单行文本：宽 = 测量文本宽 + padding；高 = 行高 + padding
- 多行（TextBox 8.5.2）：宽 = 最长行宽；高 = 行数 × 行高（**v1 挂账**——多行高度自适应复杂，见 §4 边界）

### R3：布局消费（**v1 明确不做**——GPT 审查定调）

`VerticalLayout` / `HorizontalLayout` 排位时消费 preferred 尺寸（布局期协商）——**v1 明确不做**（§3.1 方案 B 挂账：会重造 LinearLayout 提取压力 + 验收场景不需要）。v1 的 Layout 排位逻辑零改动。

### R4：触发方式（v1.4 重写——删除自动挂钩）

- **显式调用 `AutoSize()`**——唯一触发方式
- **不挂钩 `SetText()`**（内容变化自动重算 v1 不做——后续若需要，单独立项「内容变化触发链」）
- **不在 `Arrange()` 中自动协商**（Layout 零感知）

### R5：尺寸意图三分与优先级（v1.5 修正——「后调用者赢」冻结，消除与初设 §4 的矛盾）

**三分语义模型（保留——描述布局/自适应机制间的关系）**：

| 优先级 | 意图来源 | 语义 |
|---|---|---|
| 1（最高） | **Explicit Size** | 用户主动声明尺寸 |
| 2 | **Stretch** | 用户主动声明参与父布局分配（9.7 opt-in）——**与 AutoSize 互斥**（§3.5） |
| 3（兜底） | **AutoSize** | 用户未声明前两者时的内容自适应 |

**调用顺序冻结（v1.5——GPT 初设评审定调）**：

> **尺寸来源优先级只描述布局/自适应机制之间的关系，不构成对显式 API 调用顺序的强制约束。**

```text
SetSize()      = 立即设置尺寸
AutoSize()     = 立即按 preferred 设置尺寸
stretch > 0    = AutoSize() no-op
```

- **后调用者赢**：`SetSize(500,100)` 后调 `AutoSize()` → 覆盖为内容尺寸；`AutoSize()` 后调 `SetSize` → 覆盖为显式尺寸——`AutoSize()` 本身就是显式命令，无需 `m_hasExplicitSize` 追踪「尺寸是谁设置的」（与初设 §4 约定式一致，需求层同步修正）
- **唯一强制约束 = stretch 互斥**：`stretch > 0` 时 `AutoSize()` no-op（§3.5 条 1）——这是机制不是约定
- 实现机制归详细设计（约定式为初设倾向）

---

## 3. 开放决策点（评审重点）

### 3.1 Layout 集成方式

| 方案 | 做法 | 优点 | 缺点 |
|---|---|---|---|
| **A. 独立 AutoSize() 方法**（最 YAGNI） | Widget 加 `AutoSize()`：未显式 SetSize 时按 preferred 自调尺寸；**Layout 不感知、契约 10 不动** | 零布局契约变更；单点实现；现有 Layout/测试零回归 | 尺寸在 AutoSize() 调用点定死，之后文本变化不自动跟（需再调） |
| **B. 布局期尺寸协商** | Layout 排位时子控件未显式 SetSize → 用 preferred（需「显式尺寸标志」区分 0 与未设） | 真正自动——加控件就适配 | 改 Layout 契约 10；需给 Widget 加尺寸来源标志；影响面大 |
| **C. 双向** | A + 文本变化自动重算（SetText → AutoSize） | 全自动 | 复杂；重算触发面广（焦点/IME 组合期）易抖动 |

> **9.7 先例（v1.1 吸收）**：方案 B 的「显式尺寸标志」难题，9.7 已给出绕行先例——stretch>0 = **显式 opt-in** 参与剩余空间分配（无需猜测意图）；9.8 若未来走方案 B，「参与协商的控件显式声明」同构可复用（见 §7 关系节第 3 点）。

**v1.2 方案重估（9.7 落地后）**：

- **方案 B 的架构成本已下降**——9.7 已触碰契约 10（「Layout 不碰尺寸」历史包袱消失，Layout 现在会经 SetSize 虚分派改子尺寸），再叠「协商」语义的技术惊吓度低于 v1.1 评估。
- **但方案 B 引入新复杂度**：V/H 将同时承担「分配」（stretch 剩余空间）+「协商」（preferred 内容期望）两种尺寸逻辑——且 9.7 刚结论「LinearLayout 不提取」（共享逻辑未达类层次），方案 B 的分配+协商交织会重新制造提取压力。
- **验收场景不需要 B**：v1.2 收敛后的核心场景（`m_statLabel` 动态文本宽度）在 H 布局内 `AutoSize()` 一次调用即可满足——不需要布局期反复协商。

**建议 v1 仍取方案 A**（理由已从「碰不得契约」更新为「增量最小 + 与 9.7 opt-in 精神一致 + 验收场景不需要协商」）：`AutoSize()` 显式方法 + §3.5 交互语义冻结。

### 3.2 内边距来源（v1.4 GPT 审查冻结原则——初设只做实现）

**问题**：Label / Button / TextBox 的 padding 来源不统一（TextBoxStyle 有 padding 字段，ButtonStyle/Label 侧没有）——若不冻结，`GetPreferredSize()` 语义三分裂（text + ? 各不相同）。

**冻结原则（GPT 审查定调）**：

> **9.8 v1 的 preferred size 必须包含控件自身已经存在的有效内边距；没有独立 padding 机制的控件暂不新增完整 Style API，使用当前控件已有的内边距语义。**

- TextBox：用既有 `TextBoxStyle.padding`（四边同源已核实）
- Label / Button：用当前已有的内边距语义（具体归初步设计——可能为 0 或最小常量）
- **不要为 AutoSize 顺手搞一套完整 Padding/Style 系统**（YAGNI——Button 是否补 padding 字段归初步设计单独评估）

### 3.3 命名与 API 形态

- `Widget::AutoSize()`（动作：按 preferred 调整自身）
- `Widget::GetPreferredSize() const`（查询：内容期望尺寸）
- 是否分开两个方法（查询 + 动作）还是合一

### 3.4 多行文本高度（v1 边界）

多行 AutoSize（按行数算高）在 v1 是否做：

- 做：TextBox 多行高度自适应（ModelProbe 预览框可能用）
- 不做（挂账）：v1 只做单行文本宽自适应；多行高度仍手工 SetSize

### 3.5 与 9.7 的交互语义（v1.2 新增；v1.4 澄清 no-op 语义）

v1.1 完全没有覆盖这组决策——9.7 落地后 AutoSize 不再面对「纯显式尺寸世界」：

| # | 交互场景 | 冻结语义 | 理由 |
|---|---|---|---|
| 1 | **stretch > 0 的控件调 AutoSize** | **互斥：AutoSize no-op**（分配优先）——**是调用时判断，非永久关闭**（v1.4 澄清）：`SetStretch(0)` 后再调 `AutoSize()` 应重新生效 | stretch 语义 = 「我要随窗口伸缩」已完整表达尺寸意图；内容测量与之冲突无解；opt-in 精神——退出 opt-in（stretch 归 0）即恢复兜底能力，与「调用时检查 `GetStretch()`」的实现自然对应 |
| 2 | **fillCrossAxis = true 布局内的跨轴 AutoSize** | **布局声明优先：跨轴 AutoSize 无效**（跨轴仍铺满） | fillCrossAxis 是布局级显式声明（所有子跨轴铺满）；单子 AutoSize 破坏一致性；与「显式优先」原则同构 |
| 3 | **spacing 与 AutoSize 主轴排位** | **无冲突：AutoSize 改子尺寸后 Layout 正常排位**（spacing 照加） | 主轴 AutoSize 高（V 布局）/宽（H 布局）只是改变了 `GetWidth/Height` 读数——排位逻辑不变；AutoSize 后需重新 Arrange（§3.7） |

> 三项均为**行为冻结**（需求层定死，初步设计不重开）；实现路径归初步设计。

### 3.6 TextBox 垂直对齐（v1.4 GPT 审查降级——从决策点改为验证项，v1 不做 API）

**现状（已核实源码）**：TextBox 的 `padding` 字段**四边同源**——`textX/textY = f + inset`（TextBox.cpp:1109/1110）、`TextArea W/H = size − padding×2`（:453/:385）、光标 Y / 点击定位 Y / 滚动上限全套同源（:477）。

**冻结结论（v1.4）**：

> **9.8 v1 不新增 VerticalCentered API。单行 TextBox 通过 preferred height = `lineH + padding×2` 获得自然垂直居中（上下 padding 对称 → 文字居中）；多行/显式大尺寸 TextBox 的垂直对齐作为后续需求观察项**（若将来确认需要，单独立项，再评估 `GetTextTopInset()` 单一真相机制）。

**理由**：AutoSize 高度方向落地后「高度从哪来」前提改变——布局按内容高定框，padding 上下对称天然居中，Center 机制需求消解大半；现在引入 `SetVerticalCentered/GetTextTopInset` 是 YAGNI（GPT 审查 + Zcode「现在做可能白做」判断一致）。

**验证项（测试断言）**：

```text
TextBox AutoSize 后：
    height == lineHeight + padding×2
    textY   == padding        （上下对称 → 视觉居中）
```

### 3.7 AutoSize 副作用边界（v1.4 新增——GPT 审查：保护 9.7 Arrange 纯度）

**冻结语义**：

> **`AutoSize()` 只修改自身尺寸——不自动递归父布局（不调 `GetParent()->Arrange()`）、不负责 Invalidate。**

- 调用方负责后续动作：`widget->AutoSize(); parent->Arrange(); window/root->Invalidate();`
- 理由：9.7 已把 **Arrange 纯度**定死（Arrange 不 Invalidate，Window 层负责）——AutoSize 若内部偷偷 `SetSize + Arrange + Invalidate` 会把职责重新搅混
- 未来若需要统一触发链，单独立项（不在 9.8 范围）

---

## 4. 非目标（YAGNI 明确不做）

- 最大/最小尺寸约束（MinSize/MaxSize——需求未出现）
- **百分比/权重尺寸**（Layout 特性——**9.7 已落地**：`SetStretch(int)` 剩余空间分配；本阶段不做）
- Grid/Flow 布局（Phase 6 之后未排期）
- 滚动容器自动尺寸
- **内容变化自动重算**（`SetText` 不挂钩 AutoSize——R4 冻结；后续若需要单独立项「内容变化触发链」）
- **布局期尺寸协商**（§3.1 方案 B——v1 明确不做；会重造 LinearLayout 提取压力）
- **VerticalCentered API**（§3.6——v1 不做；单行 TextBox 经 AutoSize 自然居中，大尺寸场景留观察项）
- **为 AutoSize 新增完整 Padding/Style 系统**（§3.2——只用各控件已有内边距语义）

---

## 5. 测试方向（需求层）

- `GetPreferredSize`：TextWidget 测量断言（文本 → 期望宽高）
- `AutoSize()`：调用后尺寸 = preferred；显式尺寸意图的控件不被覆盖（R5）
- **§3.5 交互冻结断言**：stretch>0 控件 AutoSize no-op（尺寸保持分配值）；**`SetStretch(0)` 后 AutoSize 重新生效**（v1.4——调用时判断非永久关闭）；fillCrossAxis 布局内跨轴 AutoSize 无效（仍铺满）
- **§3.6 验证项断言**：单行 TextBox AutoSize 后 `height == lineHeight + padding×2`、`textY == padding`（自然垂直居中）
- **§3.7 副作用断言**：AutoSize 后父 Arrange 状态不变、无自动 Invalidate（纯度保护）
- Layout 零回归：现有 Horizontal/Vertical 测试全过（方案 A 不触碰 Layout 排位逻辑）
- Widget 默认行为：非文本控件 GetPreferredSize = 当前尺寸

---

## 6. 影响面（预判）

| 区 | 文件 | 说明 |
|---|---|---|
| Widget 基类 | `Widget.h/.cpp` | 加 `GetPreferredSize()` 虚方法（默认当前尺寸）+ `AutoSize()`（§3.5 互斥判断在此收口——stretch>0 no-op） |
| TextWidget | `TextWidget.h/.cpp` | override preferred（文本测量——TextMeasurer 访问已解） |
| Label/Button | 无改动（继承 TextWidget） | — |
| TextBox | 可能少量 | 多行高度（若 §3.4 做） |
| Layout | **方案 A：零改动** | 排位逻辑不动（9.7 后 Layout 已会改尺寸，但 AutoSize 是控件侧自调，不经 Layout） |
| 测试 | WidgetTests/TextBoxTests + 新增 | 新用例 + §3.5 交互冻结断言 + 零回归 |

---

## 7. 与 9.7 自适应布局的关系（v1.2 更新——从「正交澄清」到「落地后交互冻结」）

**两阶段分工**（9.7 已落地，维度正交）：

| | 9.7 自适应布局（Zcode，✅ 已完成） | 9.8 AutoSize（本文档） |
|---|---|---|
| 方向 | **窗口 → 尺寸分配**（stretch/spacing/fillCrossAxis/触发链 OnResized→Arrange） | **内容 → 尺寸**（GetPreferredSize/AutoSize） |
| 语义 | 剩余空间按 `SetStretch(int)` 权重分 | 内容测量期望尺寸（文本宽 + padding） |
| Layout 关系 | 触碰契约 10（分配走 SetSize 虚分派）——**已落地** | 方案 A 零触碰（独立 AutoSize 方法，控件侧自调） |
| 验收 | ModelProbe 拖窗自适应 ✅（keyBox/list SetStretch(1)） | m_statLabel 动态文本宽度适配 |

**三点吸收**（Zcode 对照本文档 v1.0 后给出，v1.1 已并入）：

1. **sizeHint 挂账理由修正**：原「TextMeasurer 非 Paint 期拿不到测量」已过时——该债务 5.5 已解（TextBox 点击定位/GetCaretClientGeometry 非 Paint 期经 `GetWindow()->GetTextMeasurer()` 测量，代码先例在）。9.8 落地**无技术拦路虎**，挂账真实理由 = v1 需求未出现（demo 全固定文案）。
2. **显式尺寸优先同构**：本文档 R5「显式 SetSize 不被 AutoSize 覆盖」与 9.7 stretch 语义互为印证——两文档独立收敛到同一原则：**尺寸来源二分（显式 SetSize 优先 / 分配或自适应兜底）**。
3. **stretch opt-in 先例**：9.7 用 `stretch>0` 显式声明参与分配、绕开「尺寸来源标志」难题；9.8 未来若升级方案 B（布局期协商），「参与协商的控件显式声明」同构复用。

**v1.2 升级**：9.7 落地使「二分」变「三分」（§2 R5），交互语义从「两文档互不干扰的正交」升级为「**必须冻结的三项行为**」（§3.5——stretch 互斥 / fillCrossAxis 优先 / spacing 无冲突）——这是本次重新评估的最核心产出。

---

## 8. 修订记录

- v1.0（2026-09-01）需求确认初稿：ModelProbe 实操暴露（用户观察）→ 现状核实（无 GetPreferredSize/Layout 契约 10）→ R1-R5 需求条目 + §3 决策点（Layout 集成 A/B/C、padding 来源、命名、多行边界）+ §4 非目标 + §5 测试方向 + §6 影响面。待评审。
- v1.1（2026-09-01）**编号变更 9.7 → 9.8**（用户裁决——9.7 编号归自适应布局 Zcode 方案）：文档头加编号变更说明；§1 边界对齐 9.7（窗口自适应 = 9.7 立项）；§3.1 方案 B 补 stretch opt-in 先例；§4 修正「权重尺寸需求未出现」→ 已由 9.7 立项；新增 §7 关系节（双文档正交 + 三点吸收：sizeHint 能力已具备 / 显式尺寸优先同构 / stretch opt-in 先例）；修订记录顺延 §8。
- v1.2（2026-09-02）**9.7 落地后的重新评估**（用户：「需要根据我们做完的 9.7 重新评估一下 9.8」）：① §1 现状更新——「Layout 不碰尺寸」已过时（9.7 分配走 SetSize 虚分派）、宽度手工写死已被 fillCrossAxis 解决大半、spacing 取代 MakeSpacer（spacer 已删）、**剩余需求收敛为动态文本**（m_statLabel 状态消息固定 180 宽截断 = 最真实场景）；② §2 R5 升格「**尺寸来源三分**」（显式 SetSize > stretch 分配 > AutoSize 兜底）——v1.1 只处理二分；③ §3.1 方案重估——B 的架构成本因 9.7 触碰契约 10 而下降，但「分配+协商」交织会重造 LinearLayout 提取压力，v1 仍取 A（理由更新）；④ **新增 §3.5 交互语义三项冻结**（stretch>0 与 AutoSize 互斥 / fillCrossAxis 跨轴优先 / spacing 无冲突）——v1.1 完全缺失；⑤ §4 非目标加「布局期协商不做」；⑥ §5 测试补 §3.5 冻结断言；⑦ §7 关系节升级「正交澄清 → 落地后交互冻结」。
- v1.3（2026-09-02）**新增 §3.6 TextBox 垂直对齐**（Zcode 提案整合——用户转发其分析）：现状核实 padding 四边同源（textX/textY/TextArea W/H/光标 Y 全套，TextBox.cpp:1109/1110/453/385/477）；问题 = padding 是「贴顶+内边距」非垂直居中（Qt QLineEdit 风格）；选项 A 机制化（SetVerticalCentered + GetTextTopInset 单一真相）/ B 凑（padding=(height−lineH)/2）；**评估意见 = 挂 9.8 一起定**（AutoSize 高度落地后「高度从哪来」前提改变，padding 上下对称天然居中，Center 需求消解大半——现在做 A 可能白做）；决策点归初步设计。§3.4 多行边界与 §3.6 联动（单行 AutoSize 高 = lineH + padding×2 是消解前提）。
- v1.4（2026-09-02）**GPT 职责审查通过——5 条修订 + 1 新增决策点全部采纳，可进入初步设计**：① R5 改写「SetSize 是操作不是状态」→ **尺寸意图三分**（Explicit Size / Stretch / AutoSize——区分「用户 SetSize」与「Layout 内部 SetSize」的机制归详设，需求层不冻结字段）；② §3.5 条 1 澄清 **no-op 是调用时判断非永久关闭**（SetStretch(0) 后 AutoSize 重新生效——opt-in 精神）；③ §3.2 冻结 **padding 原则**（preferred 含控件已有有效内边距；无独立 padding 机制的控件不新增完整 Style API——Button 归初设评估）；④ §3.6 垂直居中**从决策点降级为验证项**（v1 不做 VerticalCentered API；单行经 preferred height = lineH + padding×2 自然居中；大尺寸场景留观察项）；⑤ R4 **删除 SetText 自动挂钩**（消除与 §4 的文档内部冲突——触发方式 = 显式 AutoSize() 唯一）；⑥ **新增 §3.7 AutoSize 副作用边界**（只改自身尺寸、不递归 Arrange、不负责 Invalidate——保护 9.7 Arrange 纯度）；⑦ 文档头加「边界一句话」定调（让控件知道自己需要多大 + 调用方显式调整 ≠ 完整尺寸协商系统）；⑧ §4/§5 同步。
- v1.5（2026-09-02）**R5 语义修正**（GPT 初设评审指出——「唯一必须改的语义问题」）：v1.4 R5「显式 SetSize 优先（不被 AutoSize 覆盖）」与初设 §4「约定式无标志、AutoSize 显式动作」存在矛盾（`SetSize(500,100)` 后 `AutoSize()` 按需求应不覆盖、按初设会覆盖）。**采纳 GPT 建议冻结「后调用者赢」**：R5 重写——三分表保留（描述机制间关系）+ 新增「调用顺序冻结」（SetSize = 立即设置 / AutoSize = 立即按 preferred / stretch>0 = no-op；不构成对显式 API 调用顺序的强制约束）；GPT 赞成约定式简化（AutoSize 本身是显式命令，无需 m_hasExplicitSize 追踪）。配合初设 v1.1 进详细设计。
