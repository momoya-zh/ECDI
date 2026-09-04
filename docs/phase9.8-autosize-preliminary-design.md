# Phase 9.8 AutoSize 初步设计（v1.1）

> 阶段：初步设计（五阶段法 ②）
> 日期：2026-09-02（v1.1 修订 2026-09-02）
> 状态：**v1.1 GPT 初设评审通过**（2026-09-02——7 条意见全采纳，见修订记录；**可进入详细设计**）
> 前置：phase9.8-autosize-requirements.md **v1.5 定稿**（R5「后调用者赢」语义修正，与初设 v1.1 一致）
> 边界一句话（继承需求 v1.4）：**让控件知道自己需要多大，并允许调用方显式让它调整到这个尺寸**——不是尺寸协商系统

---

## 1. 目标与范围

**做**：

- `Widget::GetPreferredSize()` 虚方法（默认 = 当前尺寸；TextWidget override = 内容测量）
- `Widget::AutoSize()` 显式动作（按 preferred 调整自身尺寸）
- 需求 §3.5 交互语义 + §3.7 副作用边界的实现落点
- 验收载体 = ModelProbe `m_statLabel`（动态状态文本宽度自适应）

**不做**（需求冻结，此处只列不论证）：布局期协商（R3）、SetText 挂钩（R4）、VerticalCentered API（§3.6）、多行高度自适应（§3.4）、Padding/Style 系统扩展（§3.2）。

## 2. API 设计

### 2.1 查询：`GetPreferredSize()`

```cpp
// Widget.h（虚——默认实现返回当前尺寸，零回归）
[[nodiscard]] virtual Size GetPreferredSize() const;
```

- 默认实现（Widget.cpp）：`return Size{ static_cast<float>(GetWidth()), static_cast<float>(GetHeight()) };`
- 兑现 `Core/Size.h` 注释「未来 GetPreferredSize() 使用」——语义归位

### 2.2 动作：`AutoSize()`

```cpp
// Widget.h（非虚——内部走 SetSize 虚分派，TextBox override 的 EnsureCaretVisible 语义自然保留）
/// @return 是否实际调整（false = stretch 互斥 no-op / preferred == 当前尺寸）
bool AutoSize();
```

实现骨架（Widget.cpp）：

```cpp
bool Widget::AutoSize(){
    // §3.5 条 1：stretch 互斥——调用时判断（非永久关闭；SetStretch(0) 后重新生效）
    if (GetStretch() > 0)
        return false;
    const Size preferred = GetPreferredSize();
    const int w = static_cast<int>(preferred.width);
    const int h = static_cast<int>(preferred.height);
    if (w == GetWidth() && h == GetHeight())
        return false;   // 同尺寸 no-op
    SetSize(w, h);      // 虚分派——TextBox::SetSize 的 EnsureCaretVisible + Invalidate 为 SetSize 既有语义（§3.7 不新增）
    return true;
}
```

- **§3.7 落地**：AutoSize 只做「检查 → 测量 → SetSize」——不调 Arrange、不额外 Invalidate（SetSize 内部既有行为不算 AutoSize 新增）
- **§3.5 条 2/3 零代码**：fillCrossAxis 是 Layout 排位时的覆盖（自然压过 AutoSize 结果）；spacing 排位只读 `GetWidth/Height`（AutoSize 改尺寸后排位照常）——两者无需任何机制

## 3. TextWidget 内容测量（R2 落地）

### 3.1 override 实现

```cpp
// TextWidget.h（protected 延续——测量依赖 m_style.font， TextStyle 已在 protected）
[[nodiscard]] Size GetPreferredSize() const override;
```

- 单行文本：`width = MeasureText(m_text).width + horizontalInset`；`height = GetLineHeightText() + verticalInset`
- 测量路径（GetLineHeight 同款先例——TextBox.cpp:371 const_cast + :373 无窗口兜底）：

```cpp
Size TextWidget::GetPreferredSize() const{
    if (TextMeasurer* m = ResolveMeasurer())
        return MeasureWith(*m);
    return Widget::GetPreferredSize();   // 无窗口（测量不可用）→ 当前尺寸兜底
}
```

> **v1.1 澄清（GPT 初设评审 #4）——两条通道职责不同，勿混淆**：
> - **`ResolveMeasurer()` = 测试接缝**：正常运行返回 Window 的 TextMeasurer；测试经 TestableTextWidget override 返回 FakeTextMeasurer。
> - **无窗口 + 无注入 → 当前尺寸兜底 = 运行时 fallback**（防御路径，非测试机制）——测试不走这条道（走接缝）。

### 3.2 内边距（需求 §3.2 冻结原则落地——v1.1 GPT 评审 #2 冻结确认）

> **冻结：9.8 的 TextWidget preferred size 只负责文本内容测量；非 TextBox 控件默认不引入新的 padding 语义。**

| 控件 | horizontalInset / verticalInset | 依据 |
|---|---|---|
| TextBox | `m_style.padding.value`（四边同源——TextBox.cpp:1109/1110/453/385 已核实） | 既有字段 |
| Label | **0**（当前文字从 bounds 起点绘制，无内边距语义） | 诚实原则 |
| Button | **0（v1 不特化——冻结）** | YAGNI——按钮文字余量需求未出现；将来需要「文字+icon+padding+最小宽」再单独扩展 |

- **不新增任何 Style 字段**（需求 §3.2 硬约束）
- TextBox 需要 override 吗？——需要（见 3.3）：inset 来源是 `TextBoxStyle.padding`（TextWidget 拿不到）

### 3.3 TextBox 的多行防护（§3.4 挂账的边界处理）

多行文本（含 `\n`）调 AutoSize 会得到**单行高**——错误行为。防护（需求 §3.4「多行高度仍手工 SetSize」的落地）：

```cpp
// TextBox.h——override 拦截多行
[[nodiscard]] Size GetPreferredSize() const override;
// TextBox.cpp：m_text 含 '\n' → return Widget::GetPreferredSize()（当前尺寸——不参与 v1）；
//             单行 → TextWidget::GetPreferredSize() + m_style.padding.value×2 修正
```

- ModelProbe 预览框（多行只读）不会被误 AutoSize——调用方对它不调即可，框架层再兜底
- 多行高度自适应（行数 × lineH）挂账不变

## 4. 尺寸意图机制（v1.1 冻结——需求 R5 v1.5 已同步「后调用者赢」）

**冻结：约定式（无标志位）**——v1 不引入 `m_hasExplicitSize`（GPT 初设评审 #1 定调，需求 R5 已随 v1.5 同步）：

```text
SetSize()      = 立即设置尺寸
AutoSize()     = 立即按 preferred 设置尺寸（后调用者赢）
stretch > 0    = AutoSize() no-op（唯一强制机制）
```

| 论点 | 内容 |
|---|---|
| AutoSize 本身是显式动作 | 调用方调 `AutoSize()` = 表达「我要内容尺寸」——后调者赢，与 `SetSize` 后调覆盖前调一致；需求 R5 的「优先级」只描述机制关系，**不构成对 API 调用顺序的强制约束** |
| 无 m_hasExplicitSize | 追踪「尺寸是谁设置的」需要区分用户/内部调用（Layout 分配也走 SetSize 虚分派）——复杂度不值当（GPT：AutoSize 是显式命令，无需追踪） |
| 唯一机制 = stretch 互斥 | `GetStretch() > 0` 检查（§2.2 已落）——9.7 opt-in 语义的对称实现 |
| 详细设计兜底 | 若详设发现必须机制化（防误用），另行评审——初设与需求 v1.5 均按约定式冻结 |

## 5. 测试接缝（无窗口测量——ProgressBar ResolveAnimationManager 同构）

**问题**：无窗口测试环境拿不到 TextMeasurer → `GetPreferredSize` 走当前尺寸兜底 → 测量断言不可能。

**接缝设计（参照 ProgressBar `ResolveAnimationManager` protected virtual 先例）**：

```cpp
// TextWidget.h（protected virtual——测试派生类 override 注入假测量器）
[[nodiscard]] virtual TextMeasurer* ResolveMeasurer() const;
// 默认实现：GetWindow() ? &GetWindow()->GetTextMeasurer() : nullptr
```

- `GetPreferredSize` 改经 `ResolveMeasurer()`（逻辑不变，测量来源可替换）
- 测试：`FakeTextMeasurer`（固定字宽/行高——如每码点 8.0f 宽、行高 16.0f）+ TestableTextWidget override 返回
- **接缝不扩散约束**（ProgressBar 先例纪律）：ResolveMeasurer 仅服务于 preferred 测量，不开放给其他用途

## 6. ModelProbe 消费（验收场景——v1.1 顺序明确）

`m_statLabel` 三处 SetText（错误消息）+ UpdateStat（统计文本）统一走封装：

```cpp
// ModelProbePage::RefreshStatText(const std::string& msg)  —— private 封装（v1.1：顺序显式化）
m_statLabel->SetText(msg);      // ① 更新文本
m_statLabel->AutoSize();        // ② 宽度按内容调整（后调用者赢——§4）
static_cast<Panel*>(m_statRow)->Arrange();   // ③ statRow 重新排位——statLabel 变宽后 searchBox/allBtn/noneBtn 随动
Invalidate();                   // ④ 请求重绘
```

- **顺序是硬要求**（GPT 初设评审 #6）：statLabel 宽度变化**不会**自动挪动兄弟控件——只靠 AutoSize 不够，必须 statRow->Arrange()（H 布局按新 GetWidth 累加）→ Invalidate
- statRow 是 H 布局：statLabel 宽度变化 → 后续 searchBox/allBtn/noneBtn 位置右移/左移——正是验收要的效果
- demo 改动仅 ModelProbe.cpp（main.cpp 不动）

## 7. 测试方案（需求 §5 展开）

| # | 用例 | 断言 |
|---|---|---|
| 1 | `PreferredSize.Default` | 非文本控件（裸 Widget/Panel）= 当前尺寸 |
| 2 | `Label.PreferredMeasured` | FakeTextMeasurer 下 = 文本宽（码点数 × 8）+ 0 inset、高 = 16 |
| 3 | `Label.AutoSizeResizes` | AutoSize() 返回 true；GetWidth/Height == preferred |
| 4 | `AutoSize.StretchMutex` | stretch=1 → AutoSize 返回 false、尺寸不变；SetStretch(0) 后重调返回 true（§3.5 条 1——调用时判断非永久关闭） |
| 5 | `AutoSize.SameSizeNoOp` | preferred == 当前尺寸 → 返回 false |
| 6 | `TextBox.MultilineNotParticipating` | 含 `\n` 的 preferred = 当前尺寸（v1 不参与） |
| 7 | `TextBox.SingleLinePreferred` | 单行 = 文本宽 + padding×2、高 = lineH + padding×2（§3.6 验证项断言） |
| 8 | `AutoSize.NoArrangeNoInvalidate` | AutoSize 后 Arrange 计数 / 脏标记不变（§3.7）——断言方式归详设（Invalidate 无窗口防御行为确认） |
| 9 | `AutoSize.LastCallWins` | **后调用者赢**（需求 R5 v1.5 冻结）：`SetSize(500,100)` → `AutoSize()` 覆盖为内容尺寸；`AutoSize()` → `SetSize` 覆盖为显式尺寸 |
| 10 | `ModelProbe.StatLabelFlow` | fake 流程：RefreshStatText → statLabel 宽随文本变化 + 兄弟控件随动（页面级行为） |
| 11 | 零回归 | **既有测试全部通过，新增用例全部通过**（v1.1：不写死数字——测试数随 Phase 增长） |

## 8. 影响面

| 区 | 文件 | 改动 |
|---|---|---|
| Widget | `Widget.h/.cpp` | + `GetPreferredSize()` 虚（默认当前尺寸）+ `AutoSize()`（stretch 检查 + SetSize 虚分派） |
| TextWidget | `TextWidget.h/.cpp` | + override preferred（测量 + ResolveMeasurer 接缝）+ `ResolveMeasurer()` protected virtual |
| TextBox | `TextBox.h/.cpp` | + override preferred（多行拦截 + padding 修正）——小 |
| Label / Button | 零改动（继承） | — |
| Layout | **零改动** | 方案 A（需求冻结） |
| demo | `ModelProbe.cpp` | statLabel 接线（RefreshStatText 封装）——**demo 文件，授权范围内** |
| 测试 | `WidgetTests`/`TextBoxTests` + FakeTextMeasurer | 9 条新用例 + 零回归 |
| main.cpp | **不动** | — |

## 9. 开放决策点（归详细设计）

1. ~~意图机制深度~~——**已冻结（v1.1）**：约定式无标志（需求 R5 v1.5 同步「后调用者赢」）；仅当详设发现必须防误用时才重新评审
2. **Button preferred 特化**：v1 冻结不做（0 inset 继承——GPT 评审 #B 确认）；补最小常量属未来需求
3. **AutoSize 返回值**：bool（初设倾向——no-op 可观测）vs void
4. **FakeTextMeasurer 形态**：固定字宽（码点 × 8）vs 可配置——测试灵活性
5. **用例 8 的 Invalidate 断言方式**：Widget::Invalidate 无窗口行为确认后定

## 10. 修订记录

- v1.0（2026-09-02）初步设计初稿：需求 v1.4 冻结语义落成 API 设计（GetPreferredSize 虚 / AutoSize bool 动作 + stretch 互斥检查）+ TextWidget 测量（const_cast 先例 + 无窗口兜底 + ResolveMeasurer 接缝——ProgressBar 同构）+ TextBox 多行防护 + padding 落地表（TextBox 既有字段 / Label·Button 0 inset）+ 意图机制倾向约定式（无标志）+ ModelProbe statLabel 验收接线 + 测试 9 用例 + 影响面（Layout/main.cpp 零改动）。待评审。
- v1.1（2026-09-02）**吸收 GPT 初设评审 7 条（配套需求 v1.5）**：① §4 意图机制**从倾向升格冻结**（约定式无标志 + 后调用者赢——与需求 R5 v1.5 同步修正矛盾）② §3.2 **冻结**「TextWidget preferred 只负责文本内容测量；非 TextBox 不引入新 padding 语义」③ §3.3 多行返回当前尺寸确认（GPT 认可不做多行）④ §3.1 澄清 **ResolveMeasurer = 测试接缝 / 无窗口兜底 = 运行时 fallback** 两条通道职责 ⑤ §2.2 AutoSize 纯 geometry operation **冻结确认**（不 Arrange/Invalidate/SetText）⑥ §6 RefreshStatText **顺序显式化**（SetText → AutoSize → statRow Arrange → Invalidate——兄弟控件随动硬要求）⑦ §7 测试 #11「既有 + 新增全通过」替代写死 141 条；用例表补 #9 LastCallWins（后调用者赢）+ 原 #9 顺延 #10。开放点收敛（意图机制深度从开放列表移除）。
