# Phase 6.2 CheckBox / Radio 职责确认

> 状态：v1.1（2026-08-25）｜职责确认对齐更新（Phase 9 主题系统 + 7.5 回调 + 7.2 测试体系已落地）
> 相关：phase6.1-horizontallayout-requirements.md（Phase 6 布局系统边界）/ phase3-focus-design.md（Focus/键盘链）/ phase9-theme-system-requirements.md（Phase 9 主题——CheckBox/Radio 样式直接纳入）/ phase7.2 测试体系（7.2 完结）

## 0. v1.1 对齐更新（2026-08-25——三个 Phase 落地后的架构对齐）

> v1.0（08-15）定稿时 Phase 9/7.2/7.5 未落地，以下决策点需对齐新架构。**6 条 StateWidget 契约 + C1-C8 全部保留不变**（互斥范围/不可取消/Space/键鼠共享等语义未受新架构影响）。

| # | v1.0 决策 | 对齐更新 | 依据 |
|---|---|---|---|
| **D1** | 样式"颜色沿用 Button 模式，Phase 9 主题替换"（v1.0 §4） | **直接带 CheckBoxStyle/RadioStyle 进 Theme**——Theme 新增 `GetCheckBoxStyle()/GetRadioStyle()`，DefaultTheme 提供默认值；StateWidget 持有 Style（Phase 9 StyleField 机制：ApplyTheme/SetStyle） | Phase 9 已落地（GPT v1.4 明确："CheckBox/Radio 落地时直接带 CheckBoxStyle/RadioStyle"） |
| **D2** | 业务绑定 = 仅继承 override 虚方法；"回调（std::function）推迟"（v1.0 §5） | **纳入 `SetOnCheckedChanged` 回调**——7.5 已确立"继承 override 基座 + 回调业务便利层"两套并存模式（Button::SetOnClick / TextBox::SetOnTextChanged 先例）；CheckBox/Radio 是表单核心控件，回调是刚需 | Phase 7.5 事件回调已落地（插 7-8 之间） |
| **D3** | 验证 = main.cpp 断言（v1.0 §6） | **改 7.2 TestCase**——CheckBoxTests.cpp（无窗口纯逻辑：SetChecked/互斥/不可取消/回调触发）；main.cpp 只放 demo 交互 | Phase 7.2 测试体系已完结（26+ 测试全绿） |
| **D4** | 勾/圆等 Phase 8，v0.1 填充色块（v1.0 初步设计 P3） | **渲染约束自动消解——真实勾/圆直接做**：CheckBox 勾 = `DrawLine`（斜线）、Radio 圆 = `DrawRoundedRect`（cornerRadius=边长/2）+ 圆点；不做填充版 | Phase 8 渲染增强已落地（DrawLine/DrawRoundedRect 能力层） |
| **D5** | StateWidget 基类继承 TextWidget（P1 已定） | **保留**——TextWidget 经 Phase 9 持有 TextStyle（foreground/font），StateWidget 继承自动获得文字视觉 | Phase 9 TextWidget 架构（v1.1+） |

**影响**：v1.0 的 §3 接口形态增加 `SetOnCheckedChanged` 回调（D2）；§4 绘制改为 Style 驱动（D1）；§6 验证改为 TestCase（D3）。

## 1. StateWidget 设计契约（GPT 建议，写死）

1. **状态属于控件自身**（m_checked 私有成员；WidgetState 不动）
2. **状态变化必须经过 SetChecked()**（唯一入口）
3. **状态变化统一触发 OnCheckedChanged(bool)**（虚方法，带新状态参数）
4. **Radio 的互斥范围是同一父 Widget**（不跨父容器——SubPanel 内 Radio 与上层 Radio 不互斥）
5. **Radio 不能被取消**（已选中再点 → 无变化；只能换选）
6. **键盘和鼠标共享同一状态切换逻辑**（Space/点击都走 SetChecked）

## 2. 范围界定

### C1 范围 — 决策点 a：**CheckBox + Radio 一起做** ✅
两者同属 StateWidget（90% 代码共享：文本/焦点/点击/键盘/状态/OnCheckedChanged），唯一差异 = CheckBox 独立状态 / Radio 互斥状态。不拆阶段。

### C2 Radio 互斥机制 — 决策点 b：**同父容器自动成组** ✅（+ 两条硬限制）
- 互斥范围 = **同一父 Widget**（遍历 Parent->Children 找 Radio，取消其他）——SubPanel 嵌套不跨级（契约 4）
- **Radio 不可取消**（契约 5：SetChecked(true) 语义，无 Toggle 反转）
- 显式 RadioGroup 容器推迟（无 v0.1 需求）

### C3 状态模型 — 决策点 c：**m_checked 私有成员** ✅
`bool m_checked = false` 属于控件自身（CheckBox/Radio 独有，Panel/Button/TextBox 无 Checked——WidgetState 不扩展）。

### C4 状态变化通知 — 决策点 d：**OnCheckedChanged(bool checked)** ✅（GPT 修订）
```cpp
virtual void OnCheckedChanged(bool checked){}   // 带新状态参数——外部无需再查 IsChecked
```
与 Button::OnClick 同款继承 override 模式（业务绑定 = 子类化 override）。

### C5 键盘 — 决策点 e：**Space 切换** ✅
CanFocus + OnKeyDown(Space) → SetChecked（与鼠标共享逻辑，契约 6；验证 键盘→焦点→状态控件 全链）。

## 3. 接口形态（初步，详细设计细化）

```cpp
// CheckBox / Radio 共享基类 StateWidget（P1：第二个状态控件出现 → 抽基类，TextWidget 抽取先例）
void SetChecked(bool checked);   // 唯一状态入口（契约 2）；CheckBox 内部语义 = 设值，点击时传 !m_checked
bool IsChecked() const noexcept;
// v1.1（D2）：7.5 回调模式纳入——表单核心控件
using CheckedChangedCallback = std::function<void(bool)>;   ///< 回调参数 = 新状态
void SetOnCheckedChanged(CheckedChangedCallback callback);   ///< 覆盖式注册（7.5 先例：Button::SetOnClick）
// 两套并存：继承 override OnCheckedChanged(bool) 基座 + 回调业务便利层（D4 RaiseXxx 分离模式）
protected:
virtual void OnCheckedChanged(bool checked){}   // 契约 3
// 不暴露 Toggle()——Radio 不能取反（GPT：Toggle 接口对 Radio 语义错误）
```

## 4. 绘制与交互（v1.1 对齐：Style 驱动 + 真实勾/圆）

- 复用 TextWidget（文本）+ 自绘状态图形，**全部颜色/尺寸来自 CheckBoxStyle / RadioStyle**（D1——Phase 9 主题机制，构造 ApplyTheme + SetStyle 覆盖）
- **CheckBox 选中勾**：DrawLine 斜线（Phase 8 能力——D4 约束消解）
- **Radio 圆**：DrawRoundedRect（cornerRadius = 边长/2）+ 选中圆点（DrawRect/DrawRoundedRect）
- 点击：OnMouseButtonDown → OnClickToggle（CheckBox 传 !m_checked / Radio 传 true + 同父互斥取消）
- 焦点框：HasFocus 时状态框视觉变化（Style 字段控制）

## 5. 业务绑定机制（GPT 提问回答，记入）

业务代码绑定 = **继承 + override 虚方法**（ECDI 既有模式，DemoButton::OnClick 先例）：
```cpp
class DemoCheckBox : public ECDI::CheckBox{
protected:
    void OnCheckedChanged(bool checked) override{
        Logger::Log(LogLevel::Info, checked ? L"Checked" : L"Unchecked");
    }
};
```
回调（std::function）推迟——表单/数据绑定时代再评估。

## 6. 验证（v1.1 对齐：7.2 TestCase 替代 main.cpp 断言）

- **CheckBoxTests.cpp**（7.2 体系，无窗口纯逻辑——D3）：
  - CheckBox：SetChecked/IsChecked 状态切换 + OnCheckedChanged 虚方法触发 + SetOnCheckedChanged 回调触发（两套并存）
  - Radio：同父互斥（选 B → A 自动取消）+ 不可取消（已选中再 SetChecked(true) 无变化）+ 跨父容器不互斥（契约 4）
  - 键鼠共享：Space → 切换；点击 → 切换（事件构造同 TextBoxTests 先例）
- demo（main.cpp）：CheckBox 组 + Radio 组（互斥 + OnCheckedChanged Logger + Space 切换）——视觉验证（真实勾/圆/焦点框/主题）

## 7. 修订记录

- v1.1（2026-08-25）**架构对齐更新**（Phase 9 主题 + 7.5 回调 + 7.2 测试 + Phase 8 渲染全部落地）：D1 样式直接带 CheckBoxStyle/RadioStyle 进 Theme（StyleField 机制）；D2 纳入 SetOnCheckedChanged 回调（7.5 两套并存先例）；D3 验证改 7.2 TestCase；D4 真实勾/圆（DrawLine/DrawRoundedRect 约束消解）；D5 StateWidget 基类保留（继承 Phase 9 TextWidget）。6 条 StateWidget 契约 + C1-C8 全部保留。
- v1.0（2026-08-15）职责确认定稿：C1-C8 + 6 条 StateWidget 契约 + 绑定机制说明。GPT 修订全采纳：C4 带参 OnCheckedChanged(bool) / 无 Toggle / C2 互斥范围 + 不可取消写死。
- **v1.0.1（2026-08-15）延期**：CheckBox/Radio **整体延期 Phase 8 后**（用户决策——勾/圆依赖 Phase 8 渲染能力，不做填充版将就）。本设计定稿存档，Phase 8 后直接进初步设计（已完成的 preliminary 同步存档）。
