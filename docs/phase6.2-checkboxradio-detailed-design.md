# Phase 6.2 CheckBox / Radio 详细设计

> 状态：v1.2（2026-08-25）｜详细设计定稿（GPT 最终审：**✅ APPROVED — 可以进入实现**；无必改项）
> 前序：Phase 6.2 职责确认 v1.1（D1-D5 架构对齐）/ Phase 8 渲染增强 ✅（DrawLine/DrawRoundedRect）/ Phase 9 主题系统 ✅（StyleField/Theme/DefaultTheme——CheckBoxStyle/RadioStyle 直接纳入）
> 相关：phase6.2-checkboxradio-requirements.md（职责确认 v1.1）/ phase6.2-checkboxradio-preliminary-design.md（初步设计 v1.0：P1-P8）/ phase9-theme-system-detailed-design.md（Phase 9 主题 v1.4——StyleField/ApplyTheme/SetStyle 机制）

## 1. 范围

```
6.2 状态控件：StateWidget 基类（行为复用）+ CheckBox + Radio
实现：状态逻辑（SetChecked/OnCheckedChanged/键鼠共享）+ 互斥（Radio 同父）+ 真实勾/圆绘制（Phase 8 能力）
+ 主题接入（CheckBoxStyle/RadioStyle 进 Theme——Phase 9 机制直接消费，GPT 路线 B/D1）
+ 7.2 TestCase（CheckBoxTests）
```

**与 Phase 9 关系（GPT 澄清）**：GPT 评审时假设 Phase 9 未实现（路线 A：6.2 先做控件、Phase 9 再迁移）——但 **Phase 9 已于今日落地（commit 2aca7be）**，故采用**路线 B/D1**：CheckBoxStyle/RadioStyle 直接进 Theme（职责确认 v1.1 D1 已定），无迁移往返。

## 2. 文件改动清单（原子授权）

| # | 文件 | 操作 | 改动内容 |
|---|---|---|---|
| 1 | `include/ECDI/Theme/CheckBoxStyle.h` | **新增** | `CheckBoxStyle` + `CheckBoxStyleOverride`（border/borderWidth/cornerRadius/background/checkedBackground/checkmark/focusBorder/boxSize） |
| 2 | `include/ECDI/Theme/RadioStyle.h` | **新增** | `RadioStyle` + `RadioStyleOverride`（border/borderWidth/background/dot/focusBorder/circleSize） |
| 3 | `include/ECDI/Theme/Theme.h` | **修改** | `GetCheckBoxStyle()` / `GetRadioStyle()` 纯虚（Phase 9 v1.4 预留注释兑现） |
| 4 | `include/ECDI/Theme/DefaultTheme.h` | **修改** | 2 个 override 声明 |
| 5 | `src/Theme/DefaultTheme.cpp` | **修改** | 2 个实现（默认值） |
| 6 | `include/ECDI/Widget/StateWidget.h` | **新增** | 状态控件基类（行为复用：checked 状态/SetChecked/OnCheckedChanged/OnClickToggle/键鼠/Space/回调） |
| 7 | `src/Widget/StateWidget.cpp` | **新增** | 实现 |
| 8 | `include/ECDI/Widget/CheckBox.h` | **新增** | CheckBox（CheckBoxStyle + 勾绘制） |
| 9 | `src/Widget/CheckBox.cpp` | **新增** | 实现 |
| 10 | `include/ECDI/Widget/Radio.h` | **新增** | Radio（RadioStyle + 圆绘制 + 同父互斥） |
| 11 | `src/Widget/Radio.cpp` | **新增** | 实现 |
| 12 | `src/Tests/CheckBoxTests.cpp` | **新增** | S1-S10（7.2 体系） |
| 13 | `src/Tests/RunAllTests.h` | **修改** | `RegisterCheckBoxTests()` 声明 |
| 14 | `src/Tests/RunAllTests.cpp` | **修改** | 注册调用 |
| 15 | `ECDI.vcxproj` | **修改** | +8 新增源/头 + CheckBoxTests.cpp |

**CMakeLists 零改动**（GLOB 收 .cpp，头文件自动可见——8.5.1 已验证机制）；**main.cpp 不动**（skill 2；demo 由用户自行添加）。

## 3. StateWidget 基类（行为复用——不持有视觉 Style）

### 3.1 接口（include/ECDI/Widget/StateWidget.h）

```cpp
#pragma once

#include "ECDI/Widget/TextWidget.h"

#include <functional>

namespace ECDI{

class KeyDownEvent;
class MouseButtonDownEvent;

/// @brief 状态控件基类（6.2；第二个状态控件出现时抽取——TextWidget 抽取先例）
/// @details **行为复用，非视觉复用**：checked 状态/键鼠共享/通知机制在本类；
/// CheckBox/Radio 各自持有专属 Style（CheckBoxStyle/RadioStyle）并实现绘制。
/// 文字视觉（foreground/font）继承自 TextWidget::m_style（TextStyle——单一真相，不重复定义）。
class StateWidget: public TextWidget{

public:

	StateWidget();                                   // 空文本（TextWidget() 已注入 TextStyle）
	explicit StateWidget(const std::string& text);   // 文本（TextWidget(text) 已注入 TextStyle）

	bool CanFocus() const noexcept override { return true; }

	/// @brief 设置选中状态（唯一状态入口——契约 2；🔴 v1.1：**virtual**——Radio override 扩展互斥必须，否则编译失败）
	/// @details 值变化才触发通知（m_checked != checked 时：OnCheckedChanged + 回调 + Invalidate）；
	/// 相同值 no-op（Radio 交互不可取消的保证基础）。
	/// 架构含义精化（GPT v1.1）："唯一入口" = **所有状态修改必须最终经过 StateWidget::SetChecked()**——
	/// 互斥内部 `sibling->StateWidget::SetChecked(false)` 显式限定基类也符合此契约（强制取消，不重入互斥策略）。
	virtual void SetChecked(bool checked);

	bool IsChecked() const noexcept { return m_checked; }

	// ── 回调（7.5 两套并存——D2：继承 override 基座 + 回调业务便利层）──

	using CheckedChangedCallback = std::function<void(bool)>;   ///< 回调参数 = 新状态

	/// @brief 注册选中状态变化回调（覆盖式；传空 = 解除注册）
	/// @details 回调在 RaiseCheckedChanged() 内、OnCheckedChanged() 虚方法之后调用——
	/// 子类 override OnCheckedChanged 不影响回调触发（D4 RaiseXxx 分离模式）
	void SetOnCheckedChanged(CheckedChangedCallback callback);

protected:

	/// @brief 选中状态变化虚方法（契约 3；子类可 override 扩展；带新状态参数）
	virtual void OnCheckedChanged(bool /*checked*/){}

	/// @brief 键鼠共享切换逻辑（契约 6——虚方法隔离 CheckBox/Radio 差异）
	/// @details 默认 = CheckBox 语义：SetChecked(!m_checked)；Radio override = SetChecked(true)（非反转）
	virtual void OnClickToggle();

	void OnMouseButtonDown(const MouseButtonDownEvent&) override;   // 点击 → OnClickToggle
	void OnKeyDown(const KeyDownEvent&) override;                    // Space → OnClickToggle

private:

	/// @brief 状态变化通知入口（非虚，内部唯一入口——D9 契约）
	/// @details 先 OnCheckedChanged() 虚方法，再 m_onCheckedChanged() 回调——彼此独立
	void RaiseCheckedChanged();

	bool m_checked = false;   ///< 选中状态（契约 1：状态属控件自身）

	CheckedChangedCallback m_onCheckedChanged;   ///< 回调（7.5 业务便利层）

};

}
```

### 3.2 实现要点（StateWidget.cpp）

```cpp
void StateWidget::SetChecked(bool checked){
	if (m_checked == checked)
		return;   // 相同值 no-op——Radio 交互不可取消的保证（已选中再 SetChecked(true) 无通知）
	m_checked = checked;
	Invalidate();
	RaiseCheckedChanged();
}

void StateWidget::RaiseCheckedChanged(){
	OnCheckedChanged(m_checked);              // ① 虚方法（子类可 override 扩展）
	if (m_onCheckedChanged)                   // ② 回调（独立通道）
		m_onCheckedChanged(m_checked);
}

void StateWidget::OnClickToggle(){
	SetChecked(!m_checked);   // CheckBox 默认语义（反转）
}

void StateWidget::OnMouseButtonDown(const MouseButtonDownEvent&){
	// 焦点获取由 Application 前置处理（同 Button——CanFocus 已 true）
	OnClickToggle();
}

void StateWidget::OnKeyDown(const KeyDownEvent& event){
	if (event.GetKeyCode() == KeyCode::Space)
		OnClickToggle();
}
```

**构造**：StateWidget()/StateWidget(text) 都走 TextWidget 构造（TextStyle 已注入）；**本类不调 ApplyTheme**（无专属 Style——CheckBox/Radio 构造再注入）。

## 4. CheckBox（StateWidget + CheckBoxStyle）

### 4.1 接口（include/ECDI/Widget/CheckBox.h）

```cpp
#pragma once

#include "ECDI/Theme/CheckBoxStyle.h"
#include "ECDI/Widget/StateWidget.h"

namespace ECDI{

class PaintContext;

/// @brief 复选按钮（6.2；方框 + 勾）
/// @details 独立状态（不互斥）；OnClickToggle 用基类默认（反转）。
/// 文字视觉来自 TextWidget::m_style（TextStyle）；框/勾视觉来自 CheckBoxStyle。
class CheckBox: public StateWidget{

public:

	CheckBox();                                   // 默认构造——注入 CheckBoxStyle
	explicit CheckBox(const std::string& text);

	using TextWidget::SetStyle;   // 防名字隐藏——保留 TextStyle 设置（StateWidget 未定义 SetStyle，直接引用 TextWidget 基类）

	void ApplyTheme(const Theme& theme) override;   // TextWidget::ApplyTheme（TextStyle）+ CheckBoxStyle
	void SetStyle(CheckBoxStyleOverride override);  // CheckBox 专属覆盖

protected:

	/// @brief CheckBox 专属样式（protected——测试派生类可访问；不含 foreground——TextStyle 是文字唯一来源）
	CheckBoxStyle m_style;

	void OnPaint(PaintContext& ctx, int x, int y) override;

};

}
```

### 4.2 OnPaint 绘制（勾的几何）

```
┌────────┬─────────────────────┐
│  16x16 │  文本（框右侧，垂直居中）│
│  状态框 │                     │
└────────┴─────────────────────┘
```

```cpp
void CheckBox::OnPaint(PaintContext& ctx, int x, int y){
	// 状态框：控件左上角，boxSize 边长（默认 16）
	// 🟠 v1.2 几何输入防御（GPT）：size/bw 都是用户可改 Style——负值会产生非法 Rect
	const float size = (std::max)(0.0f, m_style.boxSize.value);
	if (size <= 0.0f)
		return;   // 0 尺寸直接跳过（不产生 0×0 RenderCommand）
	const float bw   = (std::max)(0.0f, m_style.borderWidth.value);
	// 焦点态边框色（focusBorder）vs 普通（border）
	const Color border = HasFocus() ? m_style.focusBorder.value : m_style.border.value;
	// 🟠 v1.1 几何防御（GPT）：innerSize 可能为负（borderWidth > size/2——Style 用户可改）——
	// 与 Radio/Button v1.4 同级防御
	const float innerSize = (std::max)(0.0f, size - 2.0f * bw);

	if (m_style.cornerRadius.value > 0.0f){
		ctx.DrawRoundedRect(Rect{ (float)x, (float)y, size, size },
			m_style.cornerRadius.value, border);
	}
	else{
		ctx.DrawRect(Rect{ (float)x, (float)y, size, size }, border);
	}
	// 🟠 v1.1 内背景（GPT）：cornerRadius>0 时内层也必须 DrawRoundedRect（圆角随内缩缩小）——
	// 否则方形填充会越界到圆角边框区（与 Radio 统一）
	const Color innerColor = IsChecked() ? m_style.checkedBackground.value : m_style.background.value;
	const float innerRadius = (std::max)(0.0f, m_style.cornerRadius.value - bw);
	if (m_style.cornerRadius.value > 0.0f){
		ctx.DrawRoundedRect(Rect{ (float)x + bw, (float)y + bw, innerSize, innerSize },
			innerRadius, innerColor);
	}
	else{
		ctx.DrawRect(Rect{ (float)x + bw, (float)y + bw, innerSize, innerSize }, innerColor);
	}

	// 选中：画勾（DrawLine 两段折线——左下 → 中 → 右上；比例固定）
	if (IsChecked()){
		const float s = size;
		ctx.DrawLine(Point{ (float)x + s*0.25f, (float)y + s*0.55f },
		             Point{ (float)x + s*0.45f, (float)y + s*0.75f }, bw, m_style.checkmark.value);
		ctx.DrawLine(Point{ (float)x + s*0.45f, (float)y + s*0.75f },
		             Point{ (float)x + s*0.78f, (float)y + s*0.28f }, bw, m_style.checkmark.value);
	}

	// 文本：框右侧偏移（boxSize + 4px 间距），垂直居中
	// （DrawTextContent(ctx, x, y) 的 x 是相对控件原点的绝对偏移——传 x+size+4 即"控件内部自定义文字位置"，TextWidget 接口支持）
	DrawTextContent(ctx, x + static_cast<int>(size) + 4, y);
}
```

**注意**：DrawLine 参数 `(Point start, Point end, float width, Color)`——实现时以 PaintContext.h 实际签名为准（已核实存在 DrawLine(const Point&, const Point&, 省略, const Color&)）。

## 5. Radio（StateWidget + RadioStyle + 互斥）

### 5.1 接口（include/ECDI/Widget/Radio.h）

```cpp
#pragma once

#include "ECDI/Theme/RadioStyle.h"
#include "ECDI/Widget/StateWidget.h"

namespace ECDI{

class PaintContext;

/// @brief 单选按钮（6.2；外圆 + 选中圆点；同父互斥）
/// @details 互斥范围 = 直接父 Widget 的直接子节点中类型为 Radio 的控件（契约 4——
/// 不跨 Container 嵌套；不引入 RadioGroup——YAGNI）。
/// 程序 API 可取消（SetChecked(false)）；用户交互不可取消（OnClickToggle = SetChecked(true)）。
class Radio: public StateWidget{

public:

	Radio();                                   // 默认构造——注入 RadioStyle
	explicit Radio(const std::string& text);

	using TextWidget::SetStyle;   // 防名字隐藏

	void ApplyTheme(const Theme& theme) override;
	void SetStyle(RadioStyleOverride override);

	/// @brief 设置选中状态（override——checked=true 时同父互斥：先取消兄弟 Radio 再选中自身）
	void SetChecked(bool checked) override;

protected:

	/// @brief Radio 专属样式（protected——测试派生类可访问；不含 foreground——TextStyle 是文字唯一来源）
	RadioStyle m_style;

	void OnClickToggle() override;   // SetChecked(true)（非反转——契约 5）

	void OnPaint(PaintContext& ctx, int x, int y) override;

private:

	/// @brief 取消同父兄弟 Radio（互斥实现——遍历 GetParent() 直接子节点中 Radio ≠ this）
	void UncheckSiblings();

};

}
```

### 5.2 互斥实现（SetChecked override）

```cpp
void Radio::SetChecked(bool checked){
	if (checked)
		UncheckSiblings();   // 先取消兄弟（互斥——契约 4）
	StateWidget::SetChecked(checked);   // 再设自身（相同值 no-op 保持：交互不可取消）
}

void Radio::UncheckSiblings(){
	if (Widget* parent = GetParent()){
		const size_t count = parent->GetChildCount();
		for (size_t i = 0; i < count; ++i){
			if (Radio* sibling = dynamic_cast<Radio*>(parent->GetChildAt(i))){
				if (sibling != this && sibling->IsChecked())
					sibling->StateWidget::SetChecked(false);   // 直接调基类——不触发自身互斥递归
			}
		}
	}
}
```

**互斥范围精确语义（GPT 修正）**：`直接父 Widget 的直接子节点中类型为 Radio 的控件`——嵌套 Container 内的 Radio 不跨级（B 与 A/C 不同组）。`sibling->StateWidget::SetChecked(false)` 显式限定基类——避免 Radio::SetChecked(false) 走 override（false 分支无互斥副作用，但显式限定语义更清晰）。

**通知顺序契约（🟠 v1.1 冻结——GPT）**：`Radio::SetChecked(true)` 时兄弟先取消、自身后选中：

```
r2.SetChecked(true)
    ↓
① UncheckSiblings()：兄弟 r1 取消 → r1 的 OnCheckedChanged(false) / callback(false) 先触发
    ↓
② StateWidget::SetChecked(true)：自身选中 → r2 的 OnCheckedChanged(true) / callback(true) 后触发
```

**固定顺序 = 先释放旧选择、再建立新选择**（互斥必须先释放再建立）。此顺序是**冻结的行为契约**——回调内若查询整个 Radio 组状态，看到的是"新选择已建立后"的中间态。测试/Demo 不得假设相反顺序（r2 true 先于 r1 false）。

### 5.3 OnPaint 绘制（外圆 + 圆点）

```cpp
void Radio::OnPaint(PaintContext& ctx, int x, int y){
	// 外圆：DrawRoundedRect(cornerRadius = circleSize/2)（Phase 8 能力——真实圆）
	// 🟠 v1.2 几何输入防御（GPT）：同 CheckBox——负值/0 尺寸直接跳过
	const float size = (std::max)(0.0f, m_style.circleSize.value);
	if (size <= 0.0f)
		return;
	const float radius = size / 2.0f;
	const float bw = (std::max)(0.0f, m_style.borderWidth.value);
	const Color border = HasFocus() ? m_style.focusBorder.value : m_style.border.value;

	ctx.DrawRoundedRect(Rect{ (float)x, (float)y, size, size }, radius, border);
	ctx.DrawRoundedRect(Rect{ (float)x + bw, (float)y + bw, size - 2.0f*bw, size - 2.0f*bw },
		(std::max)(0.0f, radius - bw), m_style.background.value);   // 几何防御（同 Button v1.4）

	// 选中：内圆点（circleSize/4）
	if (IsChecked()){
		const float dot = size * 0.4f;
		const float dotOffset = (size - dot) / 2.0f;
		ctx.DrawRoundedRect(Rect{ (float)x + dotOffset, (float)y + dotOffset, dot, dot },
			dot / 2.0f, m_style.dot.value);
	}

	// 文本：圆右侧偏移
	DrawTextContent(ctx, x + static_cast<int>(size) + 4, y);
}
```

## 6. CheckBoxStyle / RadioStyle（Phase 9 机制——新增 Theme 接口）

### 6.1 结构（include/ECDI/Theme/CheckBoxStyle.h）

```cpp
#pragma once

#include "ECDI/Theme/StyleField.h"
#include "ECDI/Core/Color.h"

#include <optional>

namespace ECDI{

/// @brief CheckBox 专属样式（文字颜色/字体统一来自 TextStyle——禁止在此重复，单一视觉真相）
struct CheckBoxStyle{
	StyleField<Color> border;            ///< 状态框边框色
	StyleField<float> borderWidth;       ///< 边框宽
	StyleField<float> cornerRadius;      ///< 圆角（0 = 直角；>0 用 DrawRoundedRect）
	StyleField<Color> background;        ///< 状态框背景（未选中）
	StyleField<Color> checkedBackground; ///< 状态框背景（选中）
	StyleField<Color> checkmark;         ///< 勾色
	StyleField<Color> focusBorder;       ///< 焦点态边框色
	StyleField<float> boxSize;           ///< 状态框边长（默认 16）
};

struct CheckBoxStyleOverride{
	std::optional<Color> border;
	std::optional<float> borderWidth;
	std::optional<float> cornerRadius;
	std::optional<Color> background;
	std::optional<Color> checkedBackground;
	std::optional<Color> checkmark;
	std::optional<Color> focusBorder;
	std::optional<float> boxSize;
};

}
```

### 6.2 RadioStyle（include/ECDI/Theme/RadioStyle.h）

```cpp
struct RadioStyle{
	StyleField<Color> border;        ///< 外圆边框色
	StyleField<float> borderWidth;   ///< 边框宽
	StyleField<Color> background;    ///< 外圆背景
	StyleField<Color> dot;           ///< 选中圆点色
	StyleField<Color> focusBorder;   ///< 焦点态边框色
	StyleField<float> circleSize;    ///< 外圆直径（默认 16）
};

struct RadioStyleOverride{
	std::optional<Color> border;
	std::optional<float> borderWidth;
	std::optional<Color> background;
	std::optional<Color> dot;
	std::optional<Color> focusBorder;
	std::optional<float> circleSize;
};
```

### 6.3 Theme 接口扩展（Theme.h / DefaultTheme.h / DefaultTheme.cpp）

```cpp
// Theme.h——Phase 9 v1.4 预留注释兑现（GPT："Phase 6.2 控件实现时新增"）
	virtual CheckBoxStyle GetCheckBoxStyle() const = 0;
	virtual RadioStyle    GetRadioStyle() const = 0;

// DefaultTheme.cpp——v0.1 默认值（黑框白底黑勾/黑圈白底黑点）
CheckBoxStyle DefaultTheme::GetCheckBoxStyle() const{
	CheckBoxStyle s;
	s.border.value            = Color::Black();
	s.borderWidth.value       = 1.0f;
	s.cornerRadius.value      = 0.0f;
	s.background.value        = Color::White();
	s.checkedBackground.value = Color::White();
	s.checkmark.value         = Color::Black();
	s.focusBorder.value       = Color::FromRGBA8(80, 120, 220);   // 焦点蓝（与 Button/TextBox 焦点色一致）
	s.boxSize.value           = 16.0f;
	return s;
}

RadioStyle DefaultTheme::GetRadioStyle() const{
	RadioStyle s;
	s.border.value       = Color::Black();
	s.borderWidth.value  = 1.0f;
	s.background.value   = Color::White();
	s.dot.value          = Color::Black();
	s.focusBorder.value  = Color::FromRGBA8(80, 120, 220);
	s.circleSize.value   = 16.0f;
	return s;
}
```

### 6.4 控件 ApplyTheme/SetStyle（CheckBox 同构，Radio 同理）

```cpp
void CheckBox::ApplyTheme(const Theme& theme){
	TextWidget::ApplyTheme(theme);   // ① TextStyle（文字）
	CheckBoxStyle defaults = theme.GetCheckBoxStyle();   // ② CheckBoxStyle
	m_style.border.Apply(defaults.border.value);
	m_style.borderWidth.Apply(defaults.borderWidth.value);
	m_style.cornerRadius.Apply(defaults.cornerRadius.value);
	m_style.background.Apply(defaults.background.value);
	m_style.checkedBackground.Apply(defaults.checkedBackground.value);
	m_style.checkmark.Apply(defaults.checkmark.value);
	m_style.focusBorder.Apply(defaults.focusBorder.value);
	m_style.boxSize.Apply(defaults.boxSize.value);
	Invalidate();
}

void CheckBox::SetStyle(CheckBoxStyleOverride override){
	if (override.border)            m_style.border.Set(*override.border);
	if (override.borderWidth)       m_style.borderWidth.Set(*override.borderWidth);
	if (override.cornerRadius)      m_style.cornerRadius.Set(*override.cornerRadius);
	if (override.background)        m_style.background.Set(*override.background);
	if (override.checkedBackground) m_style.checkedBackground.Set(*override.checkedBackground);
	if (override.checkmark)         m_style.checkmark.Set(*override.checkmark);
	if (override.focusBorder)       m_style.focusBorder.Set(*override.focusBorder);
	if (override.boxSize)           m_style.boxSize.Set(*override.boxSize);
	Invalidate();
}
```

**构造链（生命周期契约）**：`CheckBox()/CheckBox(text)` → StateWidget → TextWidget 构造（TextStyle 注入）→ CheckBox 构造体 `ApplyTheme(GetDefaultTheme())`（CheckBoxStyle 注入）——同 Button v1.4 模式。Radio 同理。

## 7. 事件传播与生命周期

| 项 | 契约 |
|---|---|
| 点击 | Application HitTest → CheckBox/Radio::OnMouseButtonDown → OnClickToggle（键鼠共享，契约 6） |
| 键盘 | 焦点控件 → OnKeyDown → `KeyCode::Space` → OnClickToggle（Tab 进焦点由 Window 处理——同 Button） |
| CheckBox OnClickToggle | 基类默认 `SetChecked(!m_checked)`（反转） |
| Radio OnClickToggle | `SetChecked(true)`（非反转——契约 5 交互不可取消） |
| Focus | `CanFocus() = true`（StateWidget）；OnFocusGained/Lost 基类默认（无额外行为） |
| 状态通知 | 值变化才触发（SetChecked 相同值 no-op）→ RaiseCheckedChanged（虚方法 + 回调） |

## 8. TestCase（CheckBoxTests.cpp，7.2 体系）

| # | 测试 | 断言点 |
|---|---|---|
| S1 | CheckBox 状态切换 | SetChecked(true) → IsChecked；再 SetChecked(false) → 取消；相同值 no-op（无通知） |
| S2 | OnCheckedChanged 虚方法（子类 override 被调用） | 子类记录 lastChanged——SetChecked(true) → override 带 true |
| S3 | SetOnCheckedChanged 用户回调（std::function 被调用） | 注册回调 → SetChecked → 回调触发（与虚方法并存） |
| S4 | Space 切换 | OnKeyDown(Space) → 状态反转（键鼠共享） |
| S5 | 鼠标点击切换 | OnMouseButtonDown → 状态反转 |
| S6 | Radio 同父互斥 | 同父 3 个 Radio：r2.SetChecked(true) → r2 选中 + r1/r3 取消 |
| S7 | Radio 交互不可取消 | r2 已选中 → OnClickToggle（SetChecked(true)）→ 仍选中、无额外通知 |
| S8 | Radio 程序可取消 | SetChecked(false) → 取消（程序 API 允许） |
| S9 | Radio 跨父不互斥 | 两个父容器各 1 个 Radio——选 B 不影响 A |
| S10 | CheckBox ApplyTheme/SetStyle | TestableCheckBox 暴露 m_style——构造注入默认值 + SetStyle 后 ApplyTheme 不覆盖（D7） |

**说明**：S1-S9 纯状态/互斥逻辑无窗口可测（GetParent 树结构测试同 WidgetTests 先例——需手动 AddChild 建树）；S10 同 ThemeTests T-F08 模式。绘制（勾/圆几何）留视觉验证。

**回调生命周期契约（GPT v1.2 注明——沿用 7.5 既有规则，6.2 不新增）**：`SetChecked → RaiseCheckedChanged` 回调执行期间仍处于调用栈中——**回调契约不承诺"回调中可以销毁当前 Widget"**（与 7.5 SetOnClick/SetOnTextChanged 同款约束；`delete this`/`RemoveChild(this)` 在回调内属未定义行为，用户需延迟销毁）。此约束是 ECDI Event/Callback 生命周期模型的全局规则，非 CheckBox 特有问题。

## 9. 与既有约束的对齐

| 约束 | 对齐方式 |
|---|---|
| skill 15 分层 | 状态/互斥/绘制全在 Widget 层；无 Win32 类型 |
| skill 16 Event 原则 | KeyDown 是事实，Space→SetChecked 是 StateWidget 语义解释 |
| skill 17 唯一入口 | SetChecked 唯一状态入口（契约 2）；RaiseCheckedChanged 唯一通知入口 |
| skill 19 能力/决策正交 | DrawLine/DrawRoundedRect 能力（Phase 8）+ CheckBoxStyle/RadioStyle 决策（Phase 9/6.2 消费） |
| skill 21 YAGNI | 不引入 RadioGroup / Toggle() / ThemeManager / 三态 |
| Phase 9 单一真相 | 文字颜色/字体只走 TextStyle（TextWidget::m_style）；CheckBoxStyle/RadioStyle 禁 foreground |
| Phase 9 名字隐藏 | CheckBox/Radio `using TextWidget::SetStyle`（v1.2 教训） |
| 7.5 回调模式 | OnCheckedChanged 虚方法基座 + SetOnCheckedChanged 回调便利层（D4 分离） |
| 资源类禁复制 | StateWidget/CheckBox/Radio 树节点地址稳定（禁移动，同 Widget 先例） |
| 测试由用户做 | S1-S10 自动（7.2 体系）+ 勾/圆视觉验证用户确认 |
| 五阶段法 | 本文档 = 详细设计；确认后进入实现 |

## 10. 与 Phase 9 接口边界（GPT 7 项收口）

1. ✅ CheckBoxStyle/RadioStyle **归 Theme**（本次新增 GetXxxStyle 虚方法 + DefaultTheme 默认值）——Phase 9 v1.4 预留注释兑现
2. ✅ 不重复 foreground——文字视觉统一 TextStyle（TextWidget::m_style）
3. ✅ 不引入 RadioGroup（同父直接子节点遍历即互斥——YAGNI）
4. ✅ 不引入 ThemeManager（GetDefaultTheme() 自由函数——Phase 9 定案）
5. ✅ StateWidget 只做行为复用，不持有视觉 Style
6. ✅ Radio 程序可取消 / 交互不可取消（OnClickToggle 虚方法隔离差异）
7. ✅ 互斥范围精确语义（直接父的直接子节点中 Radio）

## 11. 修订记录

- v1.2（2026-08-25）GPT 最终审（**✅ APPROVED — 可以进入实现**，无必改）：**🟠 几何输入防御补全**——CheckBox/Radio OnPaint 入口 `size = max(0, style.size)` + `if (size <= 0.0f) return`（负值/0 尺寸直接跳过，不产生 0×0 RenderCommand）+ `bw = max(0, style.borderWidth)`（防负 borderWidth 使内层大于外层）——Style 是用户可改 API，绘制入口统一防御；**S2/S3 测试名称精确化**（OnCheckedChanged 虚方法 vs SetOnCheckedChanged 用户回调区分）；**回调生命周期契约注明**（回调中销毁 Widget 未定义——沿用 7.5 全局规则，6.2 不新增）；拒绝清单重申（RadioGroup/Toggle/StateWidgetStyle/dotSize/三态/ThemeManager/Path 系统/S11 全不加）。
- v1.1（2026-08-25）GPT 评审整合（"修掉 virtual 硬错误 + 补几何防御 + 明确回调顺序后可进入实现"）：**🔴 StateWidget::SetChecked 改 virtual**（Radio override 编译必须——否则 C2259）；**🟠 CheckBox OnPaint 内背景圆角化**（cornerRadius>0 时内层 DrawRoundedRect(max(0, radius-bw))——否则方形填充越界圆角边框区，与 Radio 统一）+ **innerSize 几何防御**（max(0, size-2*bw)——borderWidth 用户可改）；**🟠 冻结 Radio 通知顺序契约**（SetChecked(true) → 兄弟先 OnCheckedChanged(false) → 自身后 OnCheckedChanged(true)——先释放旧选择再建立新选择）；"唯一入口"措辞精化（所有状态修改最终经过 StateWidget::SetChecked——sibling->StateWidget::SetChecked(false) 显式基类限定符合契约）；**S11 明确不加**（SetChecked(true) 自身 no-op 已被 S1 覆盖 + S7 交互路径覆盖——GPT 判定可选）；DrawTextContent 偏移布局确认（x 参数 = 相对控件原点绝对偏移，"控件内部自定义文字位置"场景 TextWidget 接口支持）。
- v1.0（2026-08-25）详细设计初稿（**此前从未有 detailed-design**——6.2 因 7.2 优先级挂起时只到初步设计）：StateWidget 行为复用基类（无视觉 Style）+ CheckBox/Radio 专属 Style（Phase 9 机制直接消费——路线 B/D1，GPT 路线 A 讨论已过时：Phase 9 今日落地）+ 真实勾（DrawLine）/圆（DrawRoundedRect）绘制（Phase 8 约束消解）+ Radio 同父互斥精确语义（直接父直接子节点）+ 程序可取消/交互不可取消 + CheckBoxStyle/RadioStyle 进 Theme + S1-S10 TestCase。
