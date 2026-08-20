# Phase 7.5 事件回调（std::function 回调注册 API）详细设计

> 状态：v1.0（2026-08-20）｜详细设计待审
> 前序：Phase 7.1 平台抽象 ✅ / Phase 7.2 无窗口单元测试体系 ✅ / 职责确认 v1.2 ✅ / 初步设计 v1.2 ✅
> 相关文档：phase7-callback-requirements.md（职责确认）/ phase7-callback-preliminary.md（初步设计）/ phase6-checkboxradio-requirements.md（C4 契约）

---

## 1. 设计概览

### 1.1 统一模式（RaiseXxx 三段式，职责确认 v1.2 D4 定案）

```
状态变化（编辑操作 / 鼠标事件 / 键盘事件）
        ↓
RaiseXxx()             [private 非虚，内部唯一入口]
        ↓
OnXxx()                [protected virtual，子类扩展钩子]
        ↓
m_xxxCallback          [std::function，独立通道]
```

### 1.2 涉及文件清单（相对初步设计的调整：5 个文件，非 6 个）

| 文件 | 改动类型 | 说明 |
|---|---|---|
| `ECDI/include/ECDI/Widget/Button.h` | **修改** | 加 ClickCallback typedef / SetOnClick / RaiseClick / m_onClick |
| `ECDI/src/Widget/Button.cpp` | **修改** | OnMouseButtonUp 改调 RaiseClick；新增 RaiseClick/SetOnClick 实现 |
| `ECDI/include/ECDI/Widget/TextBox.h` | **修改** | 加 TextChangedCallback typedef / SetOnTextChanged / RaiseTextChanged / OnTextChanged / m_onTextChanged |
| `ECDI/src/Widget/TextBox.cpp` | **修改** | 三个编辑操作加 RaiseTextChanged；新增 RaiseTextChanged/OnTextChanged/SetOnTextChanged 实现 |
| `ECDI/src/Tests/TextBoxTests.cpp` | **修改** | 新增 TestTextBoxCallback（TC1-TC4）+ RunTextBoxTests 内调用 |

> ⚠️ **详细设计对初步设计的调整（1 处）**：
> 初步设计 v1.2 计划新增 `RunTextBoxCallbackTests()` 独立入口（改 RunAllTests.h/.cpp）。
> 详细设计**改为并入现有 `RunTextBoxTests()`**——测试函数 `TestTextBoxCallback` 作为其内部调用，
> **RunAllTests.h / RunAllTests.cpp 零改动**。
> 理由：同模块测试并入同一入口（7.2 既有模式——RunTextBoxTests 内已调用 4 个测试函数）；
> 避免入口膨胀；文件改动范围更小（原子授权更轻）。如不认可此调整请指出，我可改回独立入口。

> ⚠️ 原子授权提醒：本次改动涉及 **5 个文件**（Button.h/.cpp、TextBox.h/.cpp、TextBoxTests.cpp）；
> RunAllTests.h / RunAllTests.cpp **明确不修改**。确认后我统一修改。

---

## 2. Button 详细设计（R2）

### 2.1 Button.h —— 完整改动

```cpp
#pragma once

#include "ECDI/Core/Point.h"
#include "ECDI/Widget/TextWidget.h"

#include <functional>
#include <string>

namespace ECDI{

class MouseButtonDownEvent;
class MouseButtonUpEvent;

/// @brief 按钮控件（5.3：文本完整化；蓝底白字、水平垂直居中）
/// @details 点击行为：OnMouseButtonDown/Up 管理 m_pressed + RaiseClick；
/// 按下态视觉（m_pressed 用于 OnPaint 变色）归 5.4（Invalidate 未实现）。
class Button: public TextWidget{

public:

	Button() = default;

	explicit Button(const std::string& text);

	explicit Button(std::string&& text);

	bool CanFocus() const noexcept override { return true; }

	// ── 回调注册（7.5 新增：业务便利层）────────────────

	using ClickCallback = std::function<void()>;   ///< 点击回调类型

	/// @brief 注册点击回调（覆盖式：后注册覆盖前者；传空 = 解除注册）
	/// @details 回调在 RaiseClick() 内、OnClick() 虚方法之后调用——
	/// 子类 override OnClick 不影响回调触发（D4 RaiseXxx 分离模式）
	void SetOnClick(ClickCallback callback);

protected:

	/// @brief P3：Button 水平居中 + 垂直居中（override 对齐策略）
	Point CalculateTextPosition(int x, int y, float textWidth, float lineHeight) const override;

	void OnMouseButtonDown(const MouseButtonDownEvent&)override;

	void OnMouseButtonUp(const MouseButtonUpEvent&)override;

	/// @brief 点击虚方法（子类可 override 扩展行为；空实现）
	/// @details 调用链：OnMouseButtonUp → RaiseClick → OnClick() + m_onClick()
	virtual void OnClick();

	void OnPaint(PaintContext& ctx,int x,int y) override;

private:

	/// @brief 点击通知入口（非虚，内部唯一入口——D9 契约）
	/// @details 先调 OnClick() 虚方法，再调 m_onClick() 回调——彼此独立
	void RaiseClick();

	bool m_pressed = false;

	ClickCallback m_onClick;   ///< 点击回调（7.5 新增：业务便利层）

};

}
```

### 2.2 Button.cpp —— 完整改动

改动点 1：`OnMouseButtonUp` 内 `OnClick();` → `RaiseClick();`

```cpp
void Button::OnMouseButtonUp(const MouseButtonUpEvent& event){

	// 5.4.5：I6 修正——拖出释放取消点击（Up 时鼠标在自身内才 OnClick）
	const Point abs = GetAbsolutePosition();

	const float mx = static_cast<float>(event.GetMouseX());

	const float my = static_cast<float>(event.GetMouseY());

	const bool inside =
		mx >= abs.x && mx < abs.x + static_cast<float>(GetWidth()) &&
		my >= abs.y && my < abs.y + static_cast<float>(GetHeight());

	// D5 GPT 修正：先恢复视觉（m_pressed=false + 重绘）再 OnClick（用户直觉）
	m_pressed = false;

	Invalidate();

	if (inside){

		RaiseClick();   // ← 改动：原来 OnClick()，现在 RaiseClick()

	}

}
```

改动点 2：新增 RaiseClick + SetOnClick 实现（放在 `OnClick()` 定义之后）

```cpp
void Button::OnClick(){}

// ── 回调通知（7.5：D4 三段式）──────────────

void Button::RaiseClick(){

	OnClick();                    // ① 虚方法（子类可 override 扩展）

	if (m_onClick)                // ② 回调（独立通道，override 无法吞掉）

		m_onClick();

}

void Button::SetOnClick(ClickCallback callback){

	m_onClick = std::move(callback);

}
```

> `#include <utility>` 已存在于 Button.cpp（std::move 可用，L6 已含）。

---

## 3. TextBox 详细设计（R3）

### 3.1 TextBox.h —— 完整改动

```cpp
#pragma once

#include "ECDI/Widget/TextWidget.h"
#include "ECDI/Widget/CaretGeometry.h"

#include <functional>
#include <optional>
#include <string>

namespace ECDI{

/// @brief 单行文本框（5.5；第三个文本控件，继承 TextWidget）
/// @details 职责：码点级文本编辑（光标/插入/删除/移动）+ 绘制（白底/文本/光标/焦点框）。
/// 编辑操作与事件解耦：OnKeyDown/OnCharInput 只做"事件 → 操作"映射，
/// 逻辑集中在 InsertCodepoint/DeleteBackward 等——可编程、可测试（main.cpp 断言不依赖窗口）。
class TextBox: public TextWidget{

public:

	TextBox() = default;

	explicit TextBox(const std::string& text);

	bool CanFocus() const noexcept override { return true; }

	// ── 回调注册（7.5 新增：表单/数据绑定核心需求）──────────

	using TextChangedCallback = std::function<void(const std::string&)>;   ///< 文本变化回调类型

	/// @brief 注册文本变化回调（覆盖式：传空 = 解除注册）
	/// @details 触发点 = 编辑操作（InsertCodepoint/DeleteBackward/DeleteForward）
	/// 实际改变文本时（D7：SetText 不触发——避免初始化误报）。
	/// 回调在 RaiseTextChanged() 内、OnTextChanged() 虚方法之后调用——
	/// 子类 override OnTextChanged 不影响回调触发（D4 RaiseXxx 分离模式）
	/// @param callback 参数 = 新文本（UTF-8）
	void SetOnTextChanged(TextChangedCallback callback);

	// ── 光标方向（类型即文档：MoveCaret(-1) 的 -1 是什么？——可读性）──
	enum class CaretDirection{ Left, Right };

	// ── 编辑操作（⚠️ 临时 public：5.5.1 阶段为可测试；Phase 7 API 审查重新定可见性，
	//    届时可能是"公开高层 API（InsertText/Clear/SetCaret）+ protected 底层原语"两层结构）──
	void InsertCodepoint(char32_t codepoint);   // 光标处插入（5.5.2：有 Selection 先删选中区）
	void DeleteBackward();                      // Backspace：删光标前一码点
	void DeleteForward();                       // Delete：删光标后一码点
	void MoveCaret(CaretDirection direction);   // ←→（边界钳制）
	void MoveCaretToStart();                    // Home
	void MoveCaretToEnd();                      // End
	size_t GetCaret() const noexcept;           // 码点索引

	// ── 选择查询（7.2 新增：只读，无副作用——Phase 10 集成测试前置）──────────

	/// @brief 选择区间（start <= end；码点索引，非字节偏移）
	struct SelectionRange {
		size_t start;
		size_t end;
	};

	/// @brief 获取当前选中区（如果存在）
	/// @return 无选中区 → nullopt；有选中区 → SelectionRange{min, anchor}
	/// @details 只读查询——不修改内部状态；供调试/测试/序列化使用。
	std::optional<SelectionRange> GetSelection() const;

	// ── IME 位置（5.6；7.1.3 升级 CaretGeometry）────────────────
	/// @brief 光标客户区几何（文本输入插入点——系统 caret/IME 候选窗锚点）
	/// @details 纯几何查询：GetAbsolutePosition + CalculateCaretPosition（与光标绘制同源）。
	/// 返回值 = 窗口客户区坐标（与 GetAbsolutePosition/事件 GetMouseX 同一坐标系——
	/// 非屏幕坐标、非控件相对坐标；命名保留决议 2026-08-14：项目内 Client == 窗口客户区已统一）。
	/// 7.1.3 改名（GPT 二轮）：返回值已是 CaretGeometry（rect + 逻辑可见性），Position 名不副实。
	/// 平台转换是平台职责（TextBox 零平台依赖，只输出客户区几何）。
	/// 非 const：测量需经 GetWindow()->GetTextMeasurer()（Window 接口非 const，与 OnMouseButtonDown 同性质）。
	CaretGeometry GetCaretClientGeometry();

protected:

	void OnFocusGained() override;              // 显示光标
	void OnFocusLost() override;                // 隐藏光标
	void OnMouseButtonDown(const MouseButtonDownEvent&) override;   // 点击定位 + 拖选锚点（5.5.1.4/5.5.2）
	void OnMouseMove(const MouseMoveEvent&) override;               // 拖选扩展 active 端（5.5.2）
	void OnMouseButtonUp(const MouseButtonUpEvent&) override;       // 结束拖选（5.5.2）
	void OnKeyDown(const KeyDownEvent&) override;    // 编辑键映射（5.5.1.3）+ Shift+方向键扩展（5.5.2）
	void OnCharInput(const CharInputEvent&) override; // 字符插入（5.5.1.3）
	void OnPaint(PaintContext& ctx, int x, int y) override;

	/// @brief 文本变化虚方法（子类可 override 扩展行为；空实现）
	/// @details 调用链：编辑操作 → RaiseTextChanged → OnTextChanged() + m_onTextChanged()
	/// 保护可见性：仅子类/自身可调（D3 GPT 修订）
	virtual void OnTextChanged(const std::string& text);

private:

	/// @brief 文本变化通知入口（非虚，内部唯一入口——D9 契约）
	/// @details 先调 OnTextChanged() 虚方法，再调 m_onTextChanged() 回调——彼此独立
	void RaiseTextChanged();

	size_t m_caret = 0;          ///< 光标位置（码点索引；构造后默认文本起始，不自动跳末尾——标准行为）
	bool m_showCaret = false;    ///< 是否显示光标（焦点时 true；闪烁状态未来另立）

	size_t m_selectionAnchor = 0;   ///< 选择锚点（固定端；无选择时无意义——Selection 扩张/收缩的核心）
	bool m_mouseDown = false;       ///< 鼠标按下（拖选进行中——Capture ≠ 拖选中，两状态分离；未来双击/三击升级 m_dragSelecting 6.x）

	TextChangedCallback m_onTextChanged;   ///< 文本变化回调（7.5 新增：表单/数据绑定核心需求）

	/// @brief 码点总数（私有辅助——消除 DeleteForward/MoveCaret/MoveCaretToEnd 多处重复统计）
	size_t GetCodepointCount() const;

	/// @brief 文本内 x 偏移 → 最近码点索引（点击定位算法——5.5.2 Selection 拖选/双击/Shift+单击复用）
	/// @param innerX 相对文本起点的 x（与绘制同源：OnMouseButtonDown 经 CalculateTextPosition 计算）
	size_t CaretIndexFromX(TextMeasurer& measurer, float innerX) const;

	// ── Selection 辅助（5.5.2；全 private——内部算法不暴露，Phase 7 测试体系补测）──

	bool HasSelection() const noexcept;         // anchor != caret
	size_t GetSelectionMin() const noexcept;    // min(anchor, caret)——绘制/删除用
	size_t GetSelectionMax() const noexcept;    // max(anchor, caret)
	size_t DeleteSelection();                   // 删选中区 + 返回新光标位置（min 处）
	void ClearSelection() noexcept;             // anchor = caret（无效化）

	// ── 光标几何（5.6 提取：消灭三处漂移——点击定位/光标绘制/IME 同源）──

	/// @brief 可视文本宽度（控件宽 − 焦点框内缩 2px×2）——文本裁切/Selection 高亮/光标 共用
	float GetTextAreaWidth() const noexcept;

	/// @brief 光标像素位置（光标**顶部**：textPos.y；相对文本框左上角——不含绝对窗口偏移；含可视钳制）
	/// @param measurer 测量器——调用方决定来源（OnPaint 经 GetWindow()->GetTextMeasurer()，
	/// PaintContext 封装不暴露 measurer；与点击定位 CaretIndexFromX 同源但不合并——方向相反）
	/// @details v1.0.3：统一顶部锚点（系统 caret 语义=caret 左上角；OnPaint 竖线直接用它）
	Point CalculateCaretPosition(TextMeasurer& measurer) const;

	/// @brief 同步文本输入插入点（5.6 v1.0.3：光标变动 → Window::UpdateTextInputCaret 双通道）
	/// @details 11 个调用点统一入口：焦点获/失、点击、拖选、方向键、编辑操作。
	/// 失焦用 DestroyTextInputCaret 不走本方法。
	void SyncTextInputCaret();

};

}
```

### 3.2 TextBox.cpp —— 完整改动

改动点 1：`InsertCodepoint` 末尾加 `RaiseTextChanged();`

```cpp
void TextBox::InsertCodepoint(char32_t codepoint){
	// 5.5.2：编辑操作自包含——有 Selection 先删选中区（IME 上屏/Ctrl+V/程序/脚本 全走这里）
	if (HasSelection())
		m_caret = DeleteSelection();   // DeleteSelection 内部已同步 m_caret + anchor
	const size_t byte = CodepointIndexToByteOffset(m_text, m_caret);
	m_text.insert(byte, EncodeUTF8(codepoint));
	++m_caret;
	ClearSelection();   // 5.5.2：插入后同步 anchor——否则产生幽灵选择（anchor=插入前caret, caret=+1）
	Invalidate();
	SyncTextInputCaret();   // 5.6 v1.0.3：光标位置变化 → 更新插入点
	RaiseTextChanged();     // 7.5：编辑操作实际改文本 → 通知回调（D7：仅编辑操作触发）
}
```

改动点 2：`DeleteBackward` 两处加 `RaiseTextChanged();`

```cpp
void TextBox::DeleteBackward(){
	// 5.5.2：有 Selection 删整个选中区（一次，非单字符）
	if (HasSelection()){
		m_caret = DeleteSelection();
		Invalidate();
		SyncTextInputCaret();   // 5.6 v1.0.3：光标位置变化 → 更新插入点
		RaiseTextChanged();     // 7.5：删选中区 → 文本变化 → 通知
		return;
	}
	if (m_caret == 0)
		return;                                   // 头边界：空操作 → 不触发（D7 边界语义）
	const size_t cur = CodepointIndexToByteOffset(m_text, m_caret);
	const size_t prev = CodepointIndexToByteOffset(m_text, m_caret - 1);
	m_text.erase(prev, cur - prev);               // 删前一个码点的字节区间
	--m_caret;
	ClearSelection();   // 5.5.2：删除后同步 anchor（防之前残留幽灵选择）
	Invalidate();
	SyncTextInputCaret();   // 5.6 v1.0.3：光标位置变化 → 更新插入点
	RaiseTextChanged();     // 7.5：实际删字符 → 通知
}
```

改动点 3：`DeleteForward` 两处加 `RaiseTextChanged();`

```cpp
void TextBox::DeleteForward(){
	// 5.5.2：有 Selection 删整个选中区
	if (HasSelection()){
		m_caret = DeleteSelection();
		Invalidate();
		SyncTextInputCaret();   // 5.6 v1.0.3：光标位置变化 → 更新插入点
		RaiseTextChanged();     // 7.5：删选中区 → 通知
		return;
	}
	if (m_caret >= GetCodepointCount())
		return;                                   // 尾边界：空操作 → 不触发（D7 边界语义）
	const size_t cur = CodepointIndexToByteOffset(m_text, m_caret);
	const size_t next = CodepointIndexToByteOffset(m_text, m_caret + 1);
	m_text.erase(cur, next - cur);
	ClearSelection();   // 5.5.2：DeleteForward 光标不动但同步 anchor（防之前残留）
	Invalidate();
	SyncTextInputCaret();   // 5.6 v1.0.3：文本变化（光标可能越界）→ 更新插入点
	RaiseTextChanged();     // 7.5：实际删字符 → 通知
}
```

改动点 4：新增 RaiseTextChanged / OnTextChanged / SetOnTextChanged 实现

放置位置：`GetCaret()` 定义之后（"── 纯数据访问 ──" 段末）、"── 事件映射 ──" 段之前。

```cpp
// ── 回调通知（7.5：D4 三段式）──────────────

void TextBox::RaiseTextChanged(){

	OnTextChanged(m_text);            // ① 虚方法（子类可 override 扩展）

	if (m_onTextChanged)              // ② 回调（独立通道，override 无法吞掉）

		m_onTextChanged(m_text);

}

void TextBox::OnTextChanged(const std::string& /*text*/){}

void TextBox::SetOnTextChanged(TextChangedCallback callback){

	m_onTextChanged = std::move(callback);

}
```

> ⚠️ `MoveCaret` / `MoveCaretToStart` / `MoveCaretToEnd` **不触发** RaiseTextChanged（仅光标移动，文本未变）。
> ⚠️ `SetText`（TextWidget 继承）**不触发**——D7 决策：程序设值不算用户修改（初始化误报防护）。

#### 3.2.5 通知契约（D7 补充规则，GPT 评审确认）

1. **一次逻辑编辑操作 = 一次通知**：无论内部修改多少个字符（如删除整个 Selection），一次完整的逻辑编辑操作**最多产生一次** `RaiseTextChanged()`。未来 Paste/Cut/ReplaceSelection/Undo/Redo 保持同语义。
2. **callback 执行时机**：`TextChangedCallback` 执行时，TextBox 已完成本次编辑操作及其内部状态同步（m_text / m_caret / 视觉重绘 / IME 光标均已就绪），callback 接收到的是**修改后的完整 UTF-8 文本**——不会观察到"修改中"的中间态。

---

## 4. 测试详细设计（R5）

### 4.1 测试策略（初步设计 v1.2 定案）

- 7.5 只做 **TextBox 回调测试**（编辑 API public，天然无窗口可测）
- **Button 回调测试推迟到集成测试**（唯一真实触发路径 OnMouseButtonUp 依赖事件链，违背 7.2 无窗口初衷）
- 机制验证边界（GPT 评审收紧表述）：**TextBox 测试验证 RaiseXxx 三段式在实际控件中的实现语义**（注册 → 触发 → 回调收到值；override 不吞回调）；**Button 采用相同代码结构（由结构保证），其事件链验证留待集成测试**——不宣称"TextBox 测试覆盖证明 Button 正确"
- 测试写入 `TextBoxTests.cpp`，并入 `RunTextBoxTests()`（详细设计调整，见 §1.2）

### 4.2 TextBoxTests.cpp —— 新增测试函数

在匿名 namespace 内新增（放在 `TestTextBoxBoundary` 之后、`} // anonymous namespace` 之前）：

```cpp
void TestTextBoxCallback()
{
	// ── 7.5：回调注册（D4 RaiseXxx 分离模式 / D7 仅编辑操作触发）──

	// TC1: InsertCodepoint 触发回调 + 新文本正确
	{
		TextBox box("abc");
		std::string lastText;
		box.SetOnTextChanged([&lastText](const std::string& text){ lastText = text; });
		box.MoveCaretToEnd();
		box.InsertCodepoint(U'd');
		FRAMEWORK_ASSERT(box.GetText() == "abcd");
		FRAMEWORK_ASSERT(lastText == "abcd");
	}

	// TC1b: DeleteBackward 触发回调（普通删除路径）
	{
		TextBox box("abc");
		int count = 0;
		box.SetOnTextChanged([&count](const std::string&){ ++count; });
		box.MoveCaretToEnd();
		box.DeleteBackward();
		FRAMEWORK_ASSERT(box.GetText() == "ab");
		FRAMEWORK_ASSERT(count == 1);
	}

	// TC1c: DeleteForward 触发回调
	{
		TextBox box("abc");
		int count = 0;
		box.SetOnTextChanged([&count](const std::string&){ ++count; });
		box.MoveCaretToStart();
		box.DeleteForward();
		FRAMEWORK_ASSERT(box.GetText() == "bc");
		FRAMEWORK_ASSERT(count == 1);
	}

	// TC2: DeleteBackward 头边界空操作 → 不触发（D7 边界语义）
	{
		TextBox box("abc");
		int count = 0;
		box.SetOnTextChanged([&count](const std::string&){ ++count; });
		box.MoveCaretToStart();
		box.DeleteBackward();
		FRAMEWORK_ASSERT(count == 0);
	}

	// TC2b: DeleteForward 尾边界空操作 → 不触发
	{
		TextBox box("abc");
		int count = 0;
		box.SetOnTextChanged([&count](const std::string&){ ++count; });
		box.MoveCaretToEnd();
		box.DeleteForward();
		FRAMEWORK_ASSERT(count == 0);
	}

	// TC3: SetText 不触发回调（D7 核心：程序设值不算用户修改）
	{
		TextBox box("abc");
		int count = 0;
		box.SetOnTextChanged([&count](const std::string&){ ++count; });
		box.SetText("xyz");
		FRAMEWORK_ASSERT(box.GetText() == "xyz");
		FRAMEWORK_ASSERT(count == 0);
	}

	// TC3b: MoveCaret 系列不触发回调（仅光标移动，文本未变）
	{
		TextBox box("abc");
		int count = 0;
		box.SetOnTextChanged([&count](const std::string&){ ++count; });
		box.MoveCaretToStart();
		box.MoveCaretToEnd();
		box.MoveCaret(TextBox::CaretDirection::Left);
		FRAMEWORK_ASSERT(count == 0);
	}

	// TC3c: 空回调 + 编辑 → 不崩溃（空 std::function 安全性，D5）
	{
		TextBox box("abc");
		box.SetOnTextChanged({});
		box.MoveCaretToEnd();
		box.InsertCodepoint(U'd');
		FRAMEWORK_ASSERT(box.GetText() == "abcd");
	}

	// TC4: override OnTextChanged 不吞回调（D4 核心语义）
	{
		class MyTextBox : public TextBox{
		public:
			bool hookCalled = false;   // 类成员变量——override 内可访问（GPT v1.1 修复；hook 而非 base：未调用基类）

		protected:
			void OnTextChanged(const std::string&) override{
				hookCalled = true;
				// 不调 TextBox::OnTextChanged() —— 模拟"忘记调基类"
			}
		};

		MyTextBox box;
		bool callbackCalled = false;
		box.SetOnTextChanged([&callbackCalled](const std::string&){ callbackCalled = true; });
		box.InsertCodepoint(U'X');

		FRAMEWORK_ASSERT(box.hookCalled);     // 虚方法被调用
		FRAMEWORK_ASSERT(callbackCalled);     // 回调仍被调用（D4 核心收益：override 不吞回调）
	}
}
```

### 4.3 TextBoxTests.cpp —— RunTextBoxTests 入口更新

```cpp
void ECDI::Test::RunTextBoxTests()
{
    TestTextBoxInsertDelete();
    TestTextBoxCaretMovement();
    TestTextBoxGetSelection();
    TestTextBoxBoundary();
    TestTextBoxCallback();   // ← 7.5 新增
}
```

### 4.4 测试期望值静态自查（AI 侧核对，skill 第 1 条）

| 用例 | 操作序列 | 期望 | 依据 |
|---|---|---|---|
| TC1 | box("abc") → MoveCaretToEnd → Insert 'd' | text=="abcd"、回调收到 "abcd" | InsertCodepoint 末尾插入；回调参数 = m_text 当前值 |
| TC1b | box("abc") → MoveCaretToEnd → DeleteBackward | text=="ab"、count==1 | 删末尾一码点；实际删字符 → 触发一次 |
| TC1c | box("abc") → MoveCaretToStart → DeleteForward | text=="bc"、count==1 | 删开头一码点；实际删字符 → 触发一次 |
| TC2 | box("abc") → MoveCaretToStart → DeleteBackward | count==0 | 头边界空操作 return，不达 RaiseTextChanged |
| TC2b | box("abc") → MoveCaretToEnd → DeleteForward | count==0 | 尾边界空操作 return，不达 RaiseTextChanged |
| TC3 | box("abc") → SetText("xyz") | text=="xyz"、count==0 | SetText 走 TextWidget（非 virtual），无 RaiseTextChanged |
| TC3b | MoveCaretToStart/End/Left | count==0 | 光标移动无 RaiseTextChanged 调用点 |
| TC3c | SetOnTextChanged({}) → Insert 'd' | text=="abcd"、无崩溃 | 空 std::function 调用安全（if 判空） |
| TC4 | MyTextBox override OnTextChanged（不调基类）→ Insert 'X' | hookCalled==true、callbackCalled==true | RaiseTextChanged 先 OnTextChanged 后回调——独立通道 |

---

## 5. 实现顺序（落地顺序 + 依赖）

| 步骤 | 内容 | 依赖 |
|---|---|---|
| 1 | Button.h（typedef/SetOnClick/RaiseClick/m_onClick） | 无 |
| 2 | Button.cpp（OnMouseButtonUp 改调 + 新实现） | 步骤 1 |
| 3 | TextBox.h（typedef/SetOnTextChanged/RaiseTextChanged/OnTextChanged/m_onTextChanged） | 无 |
| 4 | TextBox.cpp（三编辑操作 + 新实现） | 步骤 3 |
| 5 | TextBoxTests.cpp（TestTextBoxCallback + 入口） | 步骤 4 |
| 6 | 用户编译验证（VS）+ 运行测试 | 全部 |

> 注：无新文件（5 个文件全为修改），无需 BOM 验证；编译验证由用户在 VS 完成（skill 第 1 条）。

---

## 6. 与既有约束的对齐

| 约束 | 对齐方式 |
|---|---|
| skill 8 风格 | 沿用现有文件风格（K&R 大括号、`///<` 注释、include 绝对路径）；无新文件 |
| skill 11 UTF-8 | 回调参数 `const std::string&`（UTF-8）——零 wchar_t |
| skill 12 namespace | 新代码全部在 `namespace ECDI` 内 |
| skill 14 禁复制禁移动 | std::function 值成员无影响（控件禁复制移动，回调随控件走） |
| skill 15 分层 | 回调 API 零 Win32 类型；RaiseXxx 是控件内部业务通知入口（纯框架层） |
| skill 16 Event 原则 | 回调是"业务便利层"，不是事件——不改事件系统/EventRouter |
| skill 21 YAGNI | 不做 Widget 基类通用回调/信号槽；TextBox 补虚方法钩子是"微成本高回报"对称性投资 |
| skill 22 分层论证 | D4 用契约语言（"回调独立于虚方法"）论证 |
| 原子授权 | 5 文件全部授权后再改（skill 3）；RunAllTests.h/.cpp 明确不动 |
| 生命周期（GPT 补充） | Callback **所捕获对象**的生命周期由注册方负责；框架不对 lambda 捕获的外部对象提供生命周期管理（如 `[&obj]` 捕获的 obj 先于控件析构 → 悬空引用，属普通 C++ 责任，非框架 bug） |
| 异常策略（GPT 确认） | 回调**同步调用，异常按普通 C++ 调用规则传播**——7.5 不做 try/catch 包裹（那是"框架异常边界策略"另一个设计问题，YAGNI） |
| 五阶段法 | 本文档 = 详细设计；确认后进实现 |

---

## 7. 修订记录

- **v1.1（2026-08-20）整合 GPT 评审**：
  - **必改①**：文件数量修正 4 → **5**（Button.h/.cpp、TextBox.h/.cpp、TextBoxTests.cpp；RunAllTests.h/.cpp 明确不修改）——§1.2 / §5 / §6 全部同步
  - **必改②**：TC1b 注释修正——"删选中区路径" → "普通删除路径"（实际无 Selection，走单码点删除）
  - **建议③**：`baseCalled` → `hookCalled`（未调基类，语义更准确；TC4 + 自查表）
  - **建议④**：D4 测试覆盖表述收紧——"TextBox 测试验证三段式实际行为；Button 相同结构由代码保证，事件链验证留待集成测试"（不再宣称 TextBox 测试覆盖证明 Button）
  - **建议⑤**：新增 §3.2.5 通知契约——① 一次逻辑编辑操作 = 一次通知（Selection 删除只触发一次；未来 Paste/Cut/ReplaceSelection/Undo/Redo 同语义）② callback 执行时机（TextBox 已完成状态同步，收到完整 UTF-8 文本）
  - **建议⑥**：§6 生命周期补句（捕获对象生命周期由注册方负责；悬空引用属普通 C++ 责任）+ 异常策略明确（同步传播，不做 try/catch——YAGNI）
- v1.0（2026-08-20）详细设计初稿（初步设计 v1.2 的落地细化）
  - 调整 1 处：测试入口并入 RunTextBoxTests（RunAllTests.h/.cpp 零改动，文件清单 6 → 5）
  - 测试用例细化：TC1 扩展为 TC1/TC1b/TC1c（三个编辑操作各测）、TC2 扩展 TC2b（尾边界）、TC3 扩展 TC3b/TC3c（光标移动不触发 + 空回调安全）——覆盖更全
