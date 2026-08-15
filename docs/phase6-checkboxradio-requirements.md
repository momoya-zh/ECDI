# Phase 6.2 CheckBox / Radio 职责确认

> 状态：v1.0（2026-08-15）｜职责确认定稿（GPT 评审 + 用户确认），待初步设计
> 相关：phase6-horizontallayout-requirements.md（Phase 6 布局系统边界）/ phase3-focus-design.md（Focus/键盘链）

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
// CheckBox / Radio 共享（不抽 StateWidget 基类——YAGNI，两个类镜像如 Layout 先例？详细设计定）
void SetChecked(bool checked);   // 唯一状态入口（契约 2）；CheckBox 内部语义 = 设值，点击时传 !m_checked
bool IsChecked() const noexcept;
protected:
virtual void OnCheckedChanged(bool checked){}   // 契约 3
// 不暴露 Toggle()——Radio 不能取反（GPT：Toggle 接口对 Radio 语义错误）
```

## 4. 绘制与交互（初定）

- 复用 TextWidget（文本）+ 自绘状态图形：CheckBox 矩形框内勾 / Radio 圆形内点（颜色沿用 Button 模式，Phase 9 主题替换）
- 点击：OnMouseButtonDown → SetChecked（CheckBox 传 !m_checked / Radio 传 true + 同父互斥取消）

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

## 6. 验证（C8）

- 断言（main.cpp）：CheckBox SetChecked 状态切换可编程；Radio 同父互斥（选 B → A 自动取消）+ 不可取消（选中再点无变化）
- 交互：demo 加 CheckBox 组 + Radio 组（互斥 + OnCheckedChanged Logger + Space 切换）

## 7. 修订记录

- v1.0（2026-08-15）职责确认定稿：C1-C8 + 6 条 StateWidget 契约 + 绑定机制说明。GPT 修订全采纳：C4 带参 OnCheckedChanged(bool) / 无 Toggle / C2 互斥范围 + 不可取消写死。
- **v1.0.1（2026-08-15）延期**：CheckBox/Radio **整体延期 Phase 8 后**（用户决策——勾/圆依赖 Phase 8 渲染能力，不做填充版将就）。本设计定稿存档，Phase 8 后直接进初步设计（已完成的 preliminary 同步存档）。
