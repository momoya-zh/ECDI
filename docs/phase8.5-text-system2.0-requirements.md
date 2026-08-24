# Phase 8.5 文本系统 2.0 职责确认

> 状态：v1.0（2026-08-21）｜职责确认待审
> 前序：Phase 8 渲染增强 ✅ / Phase 5.6 IME 候选跟随 ✅
> 相关：phase5-textbox-requirements.md（5.5）/ phase5-ime-requirements.md（5.6）

## 1. 动机

Phase 8 渲染增强完成后，文本系统 2.0 的基础能力已就绪。当前 TextBox 处于 MVP 状态（单行、静态光标、无 Undo/Redo、无剪贴板、IME 组合串未内嵌），需要升级为生产级文本编辑器。

Phase 8.5 的目标是**将 TextBox 从 MVP 提升为功能完整的文本编辑控件**，覆盖：
- IME 组合串内嵌（拼音半成品显示在 TextBox 内）
- 剪贴板子系统（Ctrl+A/C/V/X）
- 多行文本 + 滚动
- 光标闪烁
- 双击选词
- Undo/Redo
- AutoSize 评估
- SetFont（字体设置）

## 2. 范围内（8 项）

| # | 内容 | 优先级 | 备注 |
|---|---|---|---|
| T1 | **IME 组合串内嵌**：TextBox 内显示拼音半成品，与已确认汉字叠加 | P0 | Phase 5.6 否决的 B 方案，现升级为正式功能 |
| T2 | **剪贴板子系统**：Ctrl+A 全选 / Ctrl+C 复制 / Ctrl+X 剪切 / Ctrl+V 粘贴 | P0 | 依赖 Selection（5.5.2 已实现） |
| T3 | **多行文本**：TextBox 支持换行符（\n）输入与显示，行高计算 | P1 | 扩展现有单行模型 |
| T4 | **滚动支持**：文本超出可视区时垂直滚动，光标跟随 | P1 | 依赖多行文本 |
| T5 | **光标闪烁**：焦点时竖线闪烁（~500ms） | P2 | Phase 5.6 选择静态光标，现升级为闪烁 |
| T6 | **双击选词**：鼠标双击选中当前单词 | P2 | 依赖 Selection |
| T7 | **Undo/Redo**：文本编辑操作的撤销/重做 | P2 | 独立子系统 |
| T8 | **AutoSize 评估**：TextBox 根据内容自动调整高度（多行）或宽度（单行） | P3 | 评估是否纳入 Phase 8.5 或推迟 |

## 3. 关键决策点（含倾向）

### D1 IME 组合串内嵌：B 方案升级

- Phase 5.6 否决 B 方案（组合串内嵌），理由是"6 个问题，全是编辑系统升级"——Phase 8.5 正是编辑系统升级
- **倾向**：**采用 B 方案**，但分阶段实现：
  - Phase 8.5.1：组合串显示（拼音半成品渲染，不参与编辑）
  - Phase 8.5.2：组合串交互（Backspace 删除拼音、候选切换、取消组合）
- 理由：组合串是文本的一部分，内嵌显示是生产级文本框的必备能力

### D2 剪贴板：系统剪贴板 vs 内部缓冲区

- Windows 系统剪贴板（CF_TEXT/CF_UNICODETEXT）是标准方案，但涉及平台依赖
- **倾向**：**系统剪贴板**，通过 PlatformWindow 抽象层访问（与 IME 相同模式）
- 理由：用户期望跨应用复制粘贴；内部缓冲区无法与外部应用交互

### D3 多行文本：滚动容器 vs 内建滚动

- 多行文本需要滚动，滚动有通用 ScrollArea 控件（Phase 6 规划）
- **倾向**：**TextBox 内建垂直滚动**（不依赖外部 ScrollArea）
- 理由：文本编辑是紧密耦合操作（光标定位、选择高亮、滚动跟随），外部容器难以精确协调

### D4 光标闪烁：定时器方案（GPT 评审修正）

- 闪烁需要周期性触发，框架目前无 Timer/Scheduler/Animation 基础设施
- **倾向**：**平台层 Timer 产生 → Window/EventRouter 翻译 → TextBox 响应**（保持 Phase 7.1 平台边界）
- 理由：YAGNI——不为光标闪烁引入通用定时器系统；Phase 9.5 收尾时可升级为通用 Timer
- **GPT 评审修正**：详细设计必须明确 Timer 在哪里被翻译和消费，不能让 TextBox 直接碰 WM_TIMER 打穿平台边界

### D5 双击选词：分词策略（GPT 评审修正）

- 英文按空格/标点分词，中文按 Unicode code point 边界（无空格分隔）
- **倾向**：**简单分词**（英文空格/标点，中文按 Unicode code point 边界）——不做复杂 NLP 分词
- 理由：YAGNI——简单分词满足 90% 场景；复杂分词是独立能力，二次用例出现再抽象
- **GPT 评审修正**：明确"Unicode code point 边界"而非"字符边界"，与 TextBox 内部索引单位统一

### D6 Undo/Redo：命令模式 vs 快照模式

- 命令模式（记录操作）内存高效但实现复杂，快照模式（记录完整状态）简单但内存开销大
- **倾向**：**快照模式**（记录文本 + 光标位置）——实现简单，文本编辑器内存开销可接受
- 理由：MVP 先行；命令模式可作为优化在 Phase 9+ 引入

### D7 AutoSize：是否纳入 Phase 8.5

- AutoSize 依赖布局系统（测量 + 布局通知），当前布局系统尚未完善
- **倾向**：**推迟到 Phase 9.5**（LinearLayout 抽象后，AutoSize 有明确消费方）
- 理由：YAGNI——无布局系统支撑，AutoSize 是孤立功能；Phase 9.5 有 LinearLayout 触发点

### D8 SetFont：接口设计

- SetFont 是字体设置，涉及 TextMeasurer 重新测量 + 控件重绘
- **倾向**：**TextBox::SetFont(Font font)** + 内部更新 TextMeasurer + Invalidate
- 理由：最小化接口；字体资源由后端管理（GDIBackend::CreateFont）

## 4. 范围外

- 水平滚动（Phase 10+）
- 自定义候选框 UI（系统 IME 候选框足够）
- 右键菜单（Phase 10+）
- 语法高亮（独立能力）
- 富文本编辑（独立控件）
- 打字机效果 / 动画（Phase 10+）

## 5. 与既有约束的对齐

| 约束 | 对齐方式 |
|---|---|
| skill 15 分层 | IME/剪贴板平台依赖经 PlatformWindow 抽象，TextBox 零平台类型 |
| skill 16 Event 原则 | 剪贴板事件（Copy/Paste）是已发生事实，TextBox 响应 |
| skill 21 YAGNI | 简单分词、快照 Undo、内建滚动——不做投机性抽象 |
| skill 22 分层论证 | 接口设计用契约语言，不引用 Win32 API 细节 |
| 资源类禁复制禁移动 | TextBox 值语义（m_text std::string） |
| 测试由用户做 | 新功能测试由用户编译运行验证 |
| 五阶段法 | 本文档 = 职责确认；确认后进初步设计 |

## 6. 实施顺序（预估）

```
Phase 8.5.1 核心升级
  ① T1 IME 组合串显示（Phase 5.6 B 方案升级）
  ② T2 剪贴板（Ctrl+A/C/V/X）
  ③ T5 光标闪烁（WM_TIMER）
  → 验证：拼音输入显示、复制粘贴、光标闪烁

Phase 8.5.2 多行与滚动
  ④ T3 多行文本（换行符处理、行高计算）
  ⑤ T4 滚动支持（垂直滚动、光标跟随）
  ⑥ T6 双击选词
  → 验证：多行输入、滚动、双击选词

Phase 8.5.3 高级功能
  ⑦ T7 Undo/Redo（快照模式）
  ⑧ T8 AutoSize 评估（决定是否纳入）
  ⑨ D8 SetFont
  → 验证：撤销重做、字体设置
```

## 7. GPT 评审回应（v1.1）

> GPT 评审时间：2026-08-24 20:15
> 评审结论：方向正确，但还不建议直接进入详细设计。

### 1. 7.2 定位明确（保留）
- **GPT 意见**：7.2 做的不是"为了测试而测试"，而是把以后开发 TextBox、Event、IME 等功能时的基础验证能力补起来。
- **回应**：**保留**。Phase 7.2 已完结，测试体系（TestCase/Registry/Runner/Assert/Summary）+ P0 Selection + P1 Event/7.1 回归已就位，为 Phase 8.5 提供测试落点。

### 2. T2 剪贴板边界吻合（保留）
- **GPT 意见**：7.2 不偷偷创造需求，8.5 功能落地后再测试。
- **回应**：**保留**。Phase 7.2 明确标记"Ctrl+A/C/V/X 目前根本没实现"，未硬塞进 P0 Selection 测试；Phase 8.5 T2 实现剪贴板后，再增加对应测试。

### 3. IME 重新定位认可（保留）
- **GPT 意见**：Phase 5.6 推迟的 B 方案现在是合适阶段。
- **回应**：**保留**。Phase 8.5 T1 采用 B 方案（组合串内嵌），分阶段实现（8.5.1 显示 / 8.5.2 交互）；Phase 7.1 平台解耦后，IME Composition State 归属 TextBox 更清晰。

### 4. D4 光标闪烁分层（修正）
- **GPT 意见**：WM_TIMER 必须在平台层翻译和消费，不能打穿 Phase 7.1 平台边界。
- **回应**：**修正**。D4 倾向改为"平台层 Timer 产生 → Window/EventRouter 翻译 → TextBox 响应"；详细设计必须明确 Timer 在哪里被翻译和消费。

### 5. D6 快照 Undo/Redo 认可（保留）
- **GPT 意见**：符合项目阶段，不做过度工程。
- **回应**：**保留**。快照模式（记录文本 + 光标位置）实现简单，文本编辑器内存开销可接受；命令模式可作为优化在 Phase 9+ 引入。

### 6. D5 双击选词措辞修正（修正）
- **GPT 意见**：明确"Unicode code point 边界"而非"字符边界"。
- **回应**：**修正**。D5 倾向改为"简单分词（英文空格/标点，中文按 Unicode code point 边界）"；与 TextBox 内部索引单位统一（code point）。

### 7. 视觉测试保留（保留）
- **GPT 意见**：自动化测试验证行为契约；视觉测试验证最终 GUI 表现。两者互补，不以自动化测试替代视觉验收。
- **回应**：**保留**。ECDI 测试体系 = 自动测试（逻辑/状态/契约）+ 视觉测试（实际 GUI 表现）；Phase 8.5 新增功能（IME 拼音显示、光标闪烁、多行文本排版、滚动跟随、双击选词、字体变化、中文/emoji 混排）均需视觉验收。

### 8. 总体评价（回应）
- **GPT 意见**：Phase 7.2 可进入实现前最终审查；Phase 8.5 方向正确，但需先钉死职责边界（IME Composition State 归属、剪贴板 PlatformWindow 接口、Timer 层级、多行换行模型、滚动状态归属、Undo Snapshot 状态、双击 code point 边界、SetFont 与 TextMeasurer 所有权）。
- **回应**：**Phase 7.2 已完结**（用户 20:09 确认运行正常）；Phase 8.5 职责确认 v1.1 已更新（D4/D5 修正），下一步是**Phase 8.5 初步设计**，在初步设计中钉死上述 8 个边界。

## 8. 修订记录

- v1.1（2026-08-24）GPT 评审整合：D4 光标闪烁分层修正（平台层 Timer → Window/EventRouter → TextBox）；D5 双击选词措辞修正（Unicode code point 边界）；新增 §7 GPT 评审回应（8 点保留/修正分类）。
- v1.0（2026-08-21）职责确认初稿：T1-T8 范围定义，D1-D8 决策点与倾向。

