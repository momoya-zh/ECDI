# ECDI 延期事项排期总表（roadmap-deferred）

> 状态：v1.0（2026-08-15）｜用户确认排期
> 作用：汇总全部"记账/延期/TODO/推迟"决策 → 对应实现阶段。README 技术债表的完整展开。

## 1. Phase 7 平台抽象 + 测试体系（v1.0 转库前硬性前置；2026-08-15 用户定拆 7.1/7.2）

### 1.1 Phase 7.1 平台抽象

| # | 延期项 | 来源 | 备注 |
|---|---|---|---|
| 1 | Window 平台抽离（PlatformWindow + Backend 注入） | MEMORY.md 正式决策 | 主任务；Platform×Backend 二维独立 |
| 2 | 输入层抽象（TextInputInterface/TextInputContext） | README 技术债 | UpdateTextInputCaret 半抽象 → 契约层 + Win32IME adapter（与 Renderer→Backend 同构） |
| 3 | Imm/caret 平台代码下沉 | 5.6 文档 | NotifyIMEComposition 的 Imm → PlatformWindow::SetIMECompositionPosition；WindowMessageHandler → Win32PlatformWindow |
| 4 | 编辑操作可见性（TextBox 临时 public → 两层结构） | README 技术债 | 公开高层 API（InsertText/Clear/SetCaret）+ protected 底层原语 |
| 5 | Invalidate 两层结构（Internal + API 解耦） | README 技术债 | 批量编辑解耦 |
| 6 | 多窗口焦点语义评估（应用级 vs 窗口级，R3） | 架构回顾 | 全局快捷键/IME 全局态需要时 |
| 7 | 系统 caret 多窗口场景 | 5.6 记账 | 懒创建 + 单窗口单 caret 评估 |
| 8 | DPI 感知评估 | README 技术债 | 框架当前无 DPI 缩放，IME 坐标逻辑像素 |

### 1.2 Phase 7.2 测试体系

| # | 延期项 | 来源 | 备注 |
|---|---|---|---|
| 28 | 测试体系（单元测试框架 + 历史欠账补测） | 5.5.2 详细设计 P8（GPT 要求②"不彻底放弃断言"） | 补 Selection 单元测试（选中 cd + 输入"中" → ab中e 等外部行为断言）；Phase 10 转库前测试保障 |

## 2. Phase 7.5 事件回调（用户定：插 7-8 之间）

| # | 延期项 | 来源 | 备注 |
|---|---|---|---|
| 9 | std::function 回调注册（Button::SetOnClick / CheckBox::SetOnCheckedChanged 等） | 用户 2026-08-15 | 继承 override 基座 + 回调便利层并存；Phase 7 解耦后做（稳定接口） |

## 3. Phase 8 渲染增强（能力层）

| # | 延期项 | 来源 | 备注 |
|---|---|---|---|
| 10 | 文本裁切裁剪区域（PushClip/clipRect 替代字符串截断） | TextBox TODO | O(n²) 截断 → 裁剪区域 |
| 11 | 焦点框虚线框 | 5.4 文档 | 现 DrawRect 两命令实线 |
| 12 | 渲染浮点化/亚像素 | 5.3/5.4 决策 | DrawTextContent int 参数等 |
| 13 | 6.2 CheckBox/Radio（勾/圆） | 用户 2026-08-15 | 消费 DrawLine/DrawRoundedRect；设计已定稿存档 |

## 4. Phase 8.5 文本系统 2.0（用户定：插 8-9 之间）

| # | 延期项 | 来源 | 备注 |
|---|---|---|---|
| 14 | IME 组合串内嵌（B 方案） | 5.6 文档 | 6 问题论证（组合串/Selection/Backspace/Delete/取消/候选切换） |
| 15 | 剪贴板子系统（Ctrl+A/C/V/X + Copy-Cut-Paste） | 5.5.2 推迟 | GPT 用户预期一致性（Ctrl+A 是剪贴板时代入口） |
| 16 | TextBox 多行/滚动 | 5.5 记账 | EnsureCaretVisible/自动滚动 |
| 17 | TextBox 双击/三击/选词/选行（m_dragSelecting） | 5.5.2 记账 | |
| 18 | 光标闪烁 | TextBox 记账 | m_showCaret 闪烁状态另立 |
| 19 | AutoSize / GetPreferredSize | 架构债务 | v0.1 不做（YAGNI），文本系统 2.0 评估 |
| 20 | Undo-Redo | 文本系统 2.0 | |
| 26 | SetFont()（TextWidget m_font 一行接入） | 5.1-5.3 预留 | 用户 2026-08-15 定入 8.5 |

## 5. Phase 9 主题系统（决策层）

| # | 延期项 | 来源 | 备注 |
|---|---|---|---|
| 27 | 主题系统（样式硬编码 → ApplyTheme/SetStyle） | 路线图 | 消费 Phase 8 能力 |

## 6. Phase 9.5 收尾补充（用户定：插 9-10 之间；原触发式项，v1.0 前择机完成）

| # | 延期项 | 来源 | 备注 |
|---|---|---|---|
| 21 | 局部更新/裁剪系统（Clip/Dirty Region/Partial Redraw） | Phase 4 决策 28 | 原"真实瓶颈出现时"，现排 9.5 |
| 22 | LinearLayout 抽象 | 6.1 契约 | 原"Wrap/Grid/Flex 出现时"，现排 9.5 |
| 23 | WM_MOVE 场景（移动中候选窗错位） | 5.6 记账 | 现排 9.5 |
| 24 | Hover / DoubleClick / MouseEnter / Leave | 5.4 架构债务 | 现排 9.5 |
| 25 | Shortcut System / 键盘入口统一 / InputManager | 架构回顾 R2 + 触发条件 | 现排 9.5 |

## 7. 修订记录

- v1.0（2026-08-15）总表定稿：全部延期项分组到阶段（7/7.5/8/8.5/9/9.5）；用户确认 SetFont 入 8.5、21-25 入 9.5。
- v1.1（2026-08-15）**Phase 7 拆 7.1/7.2**（用户决策）：测试体系入 7.2（新增 #28——5.5.2 P8 承诺的 Selection 单元测试补测；Phase 10 转库前测试保障）。
