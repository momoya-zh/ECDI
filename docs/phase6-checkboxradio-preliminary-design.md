# Phase 6.2 CheckBox / Radio 初步设计

> 状态：v1.0（2026-08-15）｜初步设计（待用户确认后实现）
> 相关：phase6-checkboxradio-requirements.md（职责确认 v1.0：StateWidget 契约 6 条 + C1-C8）

## 1. 定稿决策（P1-P8）

### P1 抽 StateWidget 基类 —— A ✅（TextWidget 抽取先例）

**第二个状态控件出现 → 抽基类**（与 5.3"第二个文本控件出现时抽 TextWidget"同构，skill 20 YAGNI 的抽取得法）：

```cpp
// StateWidget.h（新）：状态控件基类
class StateWidget : public TextWidget{   // 文本复用（TextWidget）
public:
	bool CanFocus() const noexcept override { return true; }
	void SetChecked(bool checked);       // 唯一状态入口（契约 2）：设值 + OnCheckedChanged + Invalidate
	bool IsChecked() const noexcept { return m_checked; }
protected:
	virtual void OnCheckedChanged(bool checked){}   // 契约 3（带参，GPT 修订）
	virtual void OnClickToggle();        // 键鼠共享切换逻辑（契约 6）：默认 CheckBox 语义 = SetChecked(!m_checked)
	void OnMouseButtonDown(const MouseButtonDownEvent&) override;   // 调 OnClickToggle
	void OnKeyDown(const KeyDownEvent&) override;                    // Space → OnClickToggle
private:
	bool m_checked = false;              // 状态属控件自身（契约 1）
};

// CheckBox : StateWidget（仅差异：绘制矩形框 + 填充）
// Radio    : StateWidget（差异：OnClickToggle=SetChecked(true) + SetChecked 同父互斥 + 绘制）
```

### P2 接口形态（无 Toggle——GPT 硬约束）

```cpp
// StateWidget：
void SetChecked(bool checked);      // CheckBox 程序化设值 / Radio override 加互斥
bool IsChecked() const noexcept;
// 无 Toggle()——Radio 不能取反（GPT）；键鼠都经 OnClickToggle 虚方法（契约 6）

// CheckBox：继承基类（OnClickToggle 默认反转）
// Radio：
void SetChecked(bool checked) override;   // checked=true 时：遍历同父 children 找 Radio ≠ this → SetChecked(false)（契约 4 同父互斥 + 契约 5 不可取消）
void OnClickToggle() override;            // SetChecked(true)（非反转）
```

### P3 ⚠️ 渲染现实约束（关键决策）—— 勾/圆等 Phase 8

**当前 GDIBackend 只有 DrawRect + DrawText**（DrawLine/DrawRoundedRect 是 Phase 8 渲染增强）：

| 视觉元素 | 需要的能力 | 现状 | v0.1 方案 |
|---|---|---|---|
| CheckBox 的勾 | DrawLine（斜线） | ❌ Phase 8 | **选中 = 框内填充色块** |
| Radio 的圆/圆点 | DrawRoundedRect/椭圆 | ❌ Phase 8 | **选中 = 框内填充色块**（方形框，与 CheckBox 同形状） |

- **v0.1 视觉**：方框 + 选中填充（空 vs 填充区分状态）——非标准勾/圆，**Phase 8 渲染增强后精化**（TODO 记账，与 TextBox 裁剪 TODO 同类）
- 形状区分：CheckBox/Radio v0.1 同形（方框），靠**互斥语义**区分；Phase 8 后 CheckBox 方框勾 / Radio 圆点

### P4 绘制结构（OnPaint）

```
┌──────┬─────────────────────┐
│ 10x10 │ 文本（框右侧，垂直居中）│
│ 状态框 │                     │
└──────┴─────────────────────┘
```
- 状态框：控件左上 10x10 矩形（黑框 + 选中填充）——DrawRect
- 文本：框右侧偏移，垂直居中（复用 TextWidget 文本 + CalculateTextPosition 思路）
- 焦点框：HasFocus 时状态框变蓝（与 Button/TextBox 焦点视觉风格一致）

### P5 键盘 — Space 切换（C5）✅
CanFocus（基类 true）+ OnKeyDown(Space) → OnClickToggle（与鼠标共享，契约 6；验证 键盘→焦点→状态控件 全链）。

### P6 main.cpp 断言（不依赖窗口）

```cpp
// CheckBox：SetChecked/IsChecked + OnCheckedChanged 回调（子类记录）
class AssertCheckBox : public ECDI::CheckBox{
public:
	bool lastChanged = false; bool changedCalled = false;
protected:
	void OnCheckedChanged(bool checked) override{ lastChanged = checked; changedCalled = true; }
};
// 断言：SetChecked(true) → IsChecked && changedCalled && lastChanged==true

// Radio 互斥：同父 3 个 Radio
// 断言：r2.SetChecked(true) → r2.IsChecked && !r1.IsChecked && !r3.IsChecked
// 断言：r2.SetChecked(false) 程序化可取消；但 OnClickToggle 路径不取消（点击已选中 → 不变）
```

### P7 main.cpp 交互（demo）

win1 加一组（VerticalLayout panel 或直接根）：
- CheckBox 组（2-3 个）+ Radio 组（3 个）——点击互斥/切换 + OnCheckedChanged Logger（DemoCheckBox/DemoRadio override）

### P8 构建 —— A ✅
vcxproj 注册 6 新文件（StateWidget/CheckBox/Radio × .h/.cpp）；CMake GLOB 自动。

## 2. 修订记录

- v1.0（2026-08-15）初步设计定稿：P1-P8。核心：抽 StateWidget 基类（TextWidget 抽取先例）+ **渲染现实约束（勾/圆等 Phase 8，v0.1 填充色块）** + Radio 互斥（SetChecked override 同父遍历）。
- **v1.0.1（2026-08-15）延期**：CheckBox/Radio **整体延期 Phase 8 后**（用户决策——勾/圆依赖 Phase 8 渲染能力，不做填充版）。本设计存档，Phase 8 后直接进详细设计（P3 渲染约束届时自动消解：真实勾/圆）。
