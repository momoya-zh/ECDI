# Phase 7.5 事件回调（std::function 回调注册 API）初步设计

> 状态：v1.2（2026-08-19）｜初步设计待审（GPT 二轮评审修复）
> 前序：Phase 7.1 平台抽象 ✅ / Phase 7.2 无窗口单元测试体系 ✅ / 职责确认 v1.2 ✅
> 相关文档：phase7.5-callback-requirements.md（职责确认）/ phase7.2-testing-requirements.md（测试边界）/ phase6.2-checkboxradio-requirements.md（C4 契约）

---

## 1. 设计概览

### 1.1 统一模式（RaiseXxx 三段式）

职责确认 v1.2 D4 定案：**任何控件的状态变化通知统一为三件套**：

```
状态变化（编辑操作 / 鼠标事件 / 键盘事件）
        ↓
RaiseXxx()             [private 非虚，内部唯一入口]
        ↓
OnXxx()                [protected virtual，子类扩展钩子]
        ↓
m_xxxCallback          [std::function，独立通道]
```

### 1.2 涉及文件清单

| 文件 | 改动类型 | 职责确认对应 |
|---|---|---|
| `ECDI/include/ECDI/Widget/Button.h` | **修改**：加回调成员/注册 API/RaiseClick | R1, R2, D9 |
| `ECDI/src/Widget/Button.cpp` | **修改**：OnMouseButtonUp 调 RaiseClick；实现 RaiseClick | R1, R2, D4 |
| `ECDI/include/ECDI/Widget/TextBox.h` | **修改**：加回调成员/注册 API/RaiseTextChanged/OnTextChanged 钩子 | R1, R3, D3, D9 |
| `ECDI/src/Widget/TextBox.cpp` | **修改**：编辑操作调 RaiseTextChanged；实现 RaiseTextChanged | R1, R3, D4, D7 |
| `ECDI/src/Tests/TextBoxTests.cpp` | **修改**：新增 TextBoxCallbackTests | R5 |
| `ECDI/src/Tests/RunAllTests.h` | **修改**：声明 RunTextBoxCallbackTests | R5 |

> ⚠️ 原子授权提醒：本次改动涉及 **6 个文件**。如需授权，请全部确认后我再统一修改。
>
> ⚠️ Button 回调测试**不在本次范围**——推迟到集成测试阶段（GPT v1.2 决策，见 §5.1）。

---

## 2. Button 改动（R2）

### 2.1 Button.h 修改

```cpp
#pragma once

#include "ECDI/Core/Point.h"
#include "ECDI/Widget/TextWidget.h"

#include <functional>    // ← 新增
#include <string>

namespace ECDI{

class MouseButtonDownEvent;
class MouseButtonUpEvent;

class Button: public TextWidget{

public:

    Button() = default;
    explicit Button(const std::string& text);
    explicit Button(std::string&& text);

    bool CanFocus() const noexcept override { return true; }

    // ── 回调注册（7.5 新增：业务便利层）──────────────────────

    /// @brief 点击回调类型
    using ClickCallback = std::function<void()>;

    /// @brief 注册点击回调（覆盖式：后注册的覆盖前者；传空 = 解除注册）
    /// @details 回调在 RaiseClick() 内、OnClick() 虚方法之后调用——
    ///  子类 override OnClick 不影响回调触发（D4 RaiseXxx 分离模式）
    void SetOnClick(ClickCallback callback);

protected:

    Point CalculateTextPosition(int x, int y, float textWidth, float lineHeight) const override;

    void OnMouseButtonDown(const MouseButtonDownEvent&) override;
    void OnMouseButtonUp(const MouseButtonUpEvent&) override;

    /// @brief 点击虚方法（子类可 override 扩展行为）
    /// @details 调用链：OnMouseButtonUp → RaiseClick → OnClick() + m_onClick()
    virtual void OnClick();

    void OnPaint(PaintContext& ctx, int x, int y) override;

private:

    /// @brief 点击通知入口（非虚，内部唯一入口——D9 契约）
    /// @details 内部先调 OnClick() 虚方法，再调 m_onClick() 回调——彼此独立
    void RaiseClick();

    bool m_pressed = false;

    ClickCallback m_onClick;   ///< 点击回调（7.5 新增：业务便利层）

};

}
```

### 2.2 Button.cpp 修改

```cpp
// ── OnMouseButtonUp 改动 ──
void Button::OnMouseButtonUp(const MouseButtonUpEvent& event){
    // ... 坐标计算不变 ...

    m_pressed = false;
    Invalidate();

    if (inside){
        RaiseClick();   // ← 改：原来 OnClick()，现在 RaiseClick()
    }
}

// ── RaiseClick 新增（7.5：D4 三段式）──
void Button::RaiseClick(){
    OnClick();                          // ① 虚方法（子类可 override）
    if (m_onClick)                      // ② 回调（独立通道，override 无法吞掉）
        m_onClick();
}

// ── SetOnClick 新增 ──
void Button::SetOnClick(ClickCallback callback){
    m_onClick = std::move(callback);
}

// OnClick() 虚方法体不变（保持空实现）
```

---

## 3. TextBox 改动（R3）

### 3.1 TextBox.h 修改

```cpp
#pragma once

#include "ECDI/Widget/TextWidget.h"
#include "ECDI/Widget/CaretGeometry.h"

#include <functional>    // ← 新增
#include <optional>
#include <string>

namespace ECDI{

class TextBox: public TextWidget{

public:

    TextBox() = default;
    explicit TextBox(const std::string& text);

    bool CanFocus() const noexcept override { return true; }

    // ── 回调注册（7.5 新增：表单/数据绑定核心需求）──────────────

    /// @brief 文本变化回调类型
    using TextChangedCallback = std::function<void(const std::string&)>;

    /// @brief 注册文本变化回调（覆盖式：传空 = 解除注册）
    /// @details 触发点 = 编辑操作（InsertCodepoint/DeleteBackward/DeleteForward）
    ///   实际改变文本时（D7：SetText 不触发——避免初始化误报）。
    ///   回调在 RaiseTextChanged() 内、OnTextChanged() 虚方法之后调用——
    ///   子类 override OnTextChanged 不影响回调触发（D4 RaiseXxx 分离模式）
    /// @param callback 参数 = 新文本（UTF-8）
    void SetOnTextChanged(TextChangedCallback callback);

    // ... 编辑操作、选择查询、IME 位置 不变 ...

protected:

    // ... OnFocusGained/Lost/OnMouseButtonDown/OnMouseMove/OnMouseButtonUp/OnKeyDown/CharInput/OnPaint 不变 ...

    /// @brief 文本变化虚方法（子类可 override 扩展行为）
    /// @details 调用链：编辑操作 → RaiseTextChanged → OnTextChanged() + m_onTextChanged()
    ///   保护可见性：仅子类/自身可调（D3 GPT 修订）
    virtual void OnTextChanged(const std::string& text);

private:

    /// @brief 文本变化通知入口（非虚，内部唯一入口——D9 契约）
    /// @details 先调 OnTextChanged() 虚方法，再调 m_onTextChanged() 回调——彼此独立
    void RaiseTextChanged();

    size_t m_caret = 0;
    bool m_showCaret = false;
    size_t m_selectionAnchor = 0;
    bool m_mouseDown = false;

    TextChangedCallback m_onTextChanged;   ///< 文本变化回调（7.5 新增：表单/数据绑定核心需求）

    // ... GetCodepointCount/CaretIndexFromX/Selection 辅助/光标几何 不变 ...
};

}
```

### 3.2 TextBox.cpp 修改

#### 3.2.1 InsertCodepoint 改动（D7：仅编辑操作触发）

```cpp
void TextBox::InsertCodepoint(char32_t codepoint){
    if (HasSelection())
        m_caret = DeleteSelection();
    const size_t byte = CodepointIndexToByteOffset(m_text, m_caret);
    m_text.insert(byte, EncodeUTF8(codepoint));
    ++m_caret;
    ClearSelection();
    Invalidate();
    SyncTextInputCaret();
    RaiseTextChanged();   // ← 新增：编辑操作实际改文本 → 通知回调
}
```

#### 3.2.2 DeleteBackward 改动

```cpp
void TextBox::DeleteBackward(){
    if (HasSelection()){
        m_caret = DeleteSelection();
        Invalidate();
        SyncTextInputCaret();
        RaiseTextChanged();   // ← 新增：删选中区 → 文本变化 → 通知
        return;
    }
    if (m_caret == 0)
        return;   // 头边界：空操作 → 不触发（D7 边界语义）
    const size_t cur = CodepointIndexToByteOffset(m_text, m_caret);
    const size_t prev = CodepointIndexToByteOffset(m_text, m_caret - 1);
    m_text.erase(prev, cur - prev);
    --m_caret;
    ClearSelection();
    Invalidate();
    SyncTextInputCaret();
    RaiseTextChanged();   // ← 新增：实际删字符 → 通知
}
```

#### 3.2.3 DeleteForward 改动

```cpp
void TextBox::DeleteForward(){
    if (HasSelection()){
        m_caret = DeleteSelection();
        Invalidate();
        SyncTextInputCaret();
        RaiseTextChanged();   // ← 新增：删选中区 → 通知
        return;
    }
    if (m_caret >= GetCodepointCount())
        return;   // 尾边界：空操作 → 不触发（D7 边界语义）
    const size_t cur = CodepointIndexToByteOffset(m_text, m_caret);
    const size_t next = CodepointIndexToByteOffset(m_text, m_caret + 1);
    m_text.erase(cur, next - cur);
    ClearSelection();
    Invalidate();
    SyncTextInputCaret();
    RaiseTextChanged();   // ← 新增：实际删字符 → 通知
}
```

#### 3.2.4 RaiseTextChanged + SetOnTextChanged 新增

```cpp
// ── RaiseTextChanged 新增（7.5：D4 三段式）──
void TextBox::RaiseTextChanged(){
    OnTextChanged(m_text);              // ① 虚方法（子类可 override）
    if (m_onTextChanged)                // ② 回调（独立通道，override 无法吞掉）
        m_onTextChanged(m_text);
}

// ── OnTextChanged 新增（保护虚方法钩子）──
void TextBox::OnTextChanged(const std::string& /*text*/){}

// ── SetOnTextChanged 新增 ──
void TextBox::SetOnTextChanged(TextChangedCallback callback){
    m_onTextChanged = std::move(callback);
}
```

> ⚠️ MoveCaret / MoveCaretToStart / MoveCaretToEnd **不触发** RaiseTextChanged（仅光标移动，文本未变）。

---

## 4. CheckBox / Radio 模式预留（R4，仅文档）

职责确认 R4 定案：**CheckBox 控件 Phase 8 后才实现，7.5 不写代码**。本节把模式写清，Phase 8 实现时按此落地。

```cpp
// Phase 8 CheckBox 实现时按以下模式（与 Button/TextBox 完全统一）：

class CheckBox: public TextWidget{

public:

    /// @brief 状态变化回调类型
    using CheckedChangedCallback = std::function<void(bool)>;

    void SetOnCheckedChanged(CheckedChangedCallback callback);

protected:

    virtual void OnCheckedChanged(bool checked);   // ← 与 phase6 C4 契约同签名

private:

    void RaiseCheckedChanged();                    // private 入口（D9）

    CheckedChangedCallback m_onCheckedChanged;

    bool m_checked = false;
};

// 调用链（与 Button/TextBox 同源）：
// SetChecked() → RaiseCheckedChanged() → OnCheckedChanged(bool) → m_onCheckedChanged(bool)
```

---

## 5. 测试设计（R5）

### 5.1 测试策略（GPT v1.2 决策：Button 回调测试推迟集成测试）

**7.5 只做 TextBox 回调测试；Button 回调测试推迟到集成测试阶段。**

理由（GPT v1.2 核心洞察）：
- Button 回调的唯一真实触发路径 = `OnMouseButtonUp`（protected 事件方法）→ 构造 `MouseButtonUpEvent` → 依赖坐标/命中判断/Window——**把回调测试变成了鼠标事件 + 命中 + 布局 + 坐标的耦合测试**，违背 7.2"无窗口单元测试"的初衷
- TextBox 回调的触发点 = `InsertCodepoint`/`DeleteBackward`/`DeleteForward`——**全部是 public 编辑 API，天然适合 7.2 无窗口测试体系**
- Button 与 TextBox **共用同一套 RaiseXxx 机制**（结构完全相同）——机制正确性（注册 → 触发 → 回调收到值；override 不吞回调）由 TextBox 测试覆盖即可证明；Button 特有的"OnMouseButtonUp → RaiseClick"接线属于**事件交互**，归集成测试阶段
- 不做"测试辅助类暴露 private 成员"（TestableButton 之类）——破坏封装，GPT 亦不推荐

> 📌 推迟项记录：Button 回调注册/触发/override 语义 → 集成测试清单（Phase 10 或未来集成测试阶段补充，roadmap-deferred.md 记一笔）。

### 5.2 D9 契约（写死，全局生效）

```
契约：
① 任何导致状态变化的代码，禁止直接调用 OnXxx() 或 m_xxxCallback()
② 必须统一调用 RaiseXxx()——唯一合法入口
③ RaiseXxx() 私有（private），外部不可调、不可 override、测试不直接调
④ 未来新增功能（Paste/Cut/ReplaceSelection/Undo/Redo）必须走 RaiseTextChanged()，不得绕过
```

### 5.3 测试用例清单（TextBox 回调，全部无窗口）

| # | 测试用例 | 验证目标 | 对应决策 |
|---|---|---|---|
| TC1 | 注册回调 → InsertCodepoint → 断言回调收到正确新文本 | 编辑操作触发回调 + 新文本正确 | R3, D7 |
| TC2 | DeleteBackward 头边界空操作 → 不触发回调 | D7 边界语义（空操作不触发） | D7 |
| TC3 | SetText 程序设值 → 不触发回调 | D7 核心语义：SetText 不触发 | D7 |
| TC4 | 子类 override OnTextChanged **不调基类** + 注册回调 → 回调仍触发 | **D4 核心语义：override 不吞回调** | D4, D3 |

### 5.4 测试伪代码（TextBoxTests.cpp）

```cpp
// TC1: InsertCodepoint 触发回调 + 新文本正确
{
    TextBox box("abc");
    std::string lastText;
    box.SetOnTextChanged([&lastText](const std::string& text){ lastText = text; });
    box.MoveCaretToEnd();
    box.InsertCodepoint(U'd');
    FRAMEWORK_ASSERT(lastText == "abcd");   // ✅ 收到新文本
}

// TC2: DeleteBackward 头边界空操作 → 不触发
{
    TextBox box("abc");
    int count = 0;
    box.SetOnTextChanged([&count](const std::string&){ ++count; });
    box.MoveCaretToStart();
    box.DeleteBackward();   // 头边界：空操作
    FRAMEWORK_ASSERT(count == 0);   // ✅ 不触发
}

// TC3: SetText 不触发回调（D7 核心）
{
    TextBox box("abc");
    int count = 0;
    box.SetOnTextChanged([&count](const std::string&){ ++count; });
    box.SetText("xyz");   // 程序设值
    FRAMEWORK_ASSERT(count == 0);   // ✅ 不触发
}

// TC4: override OnTextChanged 不吞回调（D4 核心语义）
{
    class MyTextBox : public TextBox{
    public:
        bool baseCalled = false;   // 类成员变量——lambda/override 可访问（GPT v1.1 修复）

    protected:
        void OnTextChanged(const std::string&) override{
            baseCalled = true;
            // 不调 TextBox::OnTextChanged() —— 模拟忘记调基类
        }
    };

    MyTextBox box;
    bool callbackCalled = false;
    box.SetOnTextChanged([&callbackCalled](const std::string&){ callbackCalled = true; });
    box.InsertCodepoint(U'X');   // 触发 RaiseTextChanged

    FRAMEWORK_ASSERT(box.baseCalled);     // ✅ 虚方法被调用
    FRAMEWORK_ASSERT(callbackCalled);     // ✅ 回调仍被调用（D4 核心收益！）
}
```

### 5.5 测试归属与运行

- 测试代码写入 `TextBoxTests.cpp`（与既有 TextBox 测试同文件——跟随模块）
- 函数命名：`TestTextBoxCallback`
- 入口声明：`RunAllTests.h` 新增 `RunTextBoxCallbackTests()`；`RunAllTests.cpp` 加调用
- 运行方式：用户编译后在 VS 中运行（skill 第 1 条——AI 不自行运行测试）

---

## 6. 生命周期与线程（D5 实现确认）

| 项 | 实现 |
|---|---|
| std::function 析构 | 控件析构时自动释放（值成员，无泄漏） |
| 悬空风险 | 文档约定"外部对象生命周期由注册方负责" |
| 调用线程 | 同步调用（消息循环线程内） |
| 跨线程 | 不支持（范围外） |

---

## 7. 与既有约束的对齐

| 约束 | 对齐方式 |
|---|---|
| skill 8 BOM | 无新文件（仅改现有 .h/.cpp + 测试文件），无需 BOM 验证 |
| skill 11 UTF-8 | TextBox 回调参数 `const std::string&`（UTF-8，与 GetText 一致）——零 wchar_t |
| skill 12 namespace | 所有新代码在 `namespace ECDI` 内 |
| skill 14 禁复制禁移动 | std::function 成员无影响（控件不可复制移动，回调随控件走） |
| skill 15 分层 | 回调 API 零 Win32 类型——纯框架层；RaiseXxx 是控件内部业务通知入口 |
| skill 16 Event 原则 | 回调是"业务便利层"，不是事件——不改事件系统，不改 EventRouter |
| skill 21 YAGNI | 不做 Widget 基类通用回调；TextBox 补保护虚方法钩子是"微成本高回报"的对称性投资 |
| skill 22 分层论证 | D4 用契约语言论证（"回调独立于虚方法"），不引用平台实现 |
| 资源类禁复制禁移动 | 同 skill 14 |
| 原子授权 | 6 文件全部授权后再改（skill 3） |
| 五阶段法 | 本文档 = 初步设计；确认后进详细设计 |

---

## 8. 修订记录

- **v1.2（2026-08-19）整合 GPT 二轮评审（3 问题 + 1 建议）**：
  1. **问题 1/2（TC1-TC4 无法编译——前后矛盾）**：统一测试策略——**删除 Button 回调测试**（原 TC1-TC4），7.5 只保留 TextBox 回调测试（现 TC1-TC4）
  2. **问题 3（Button 测试耦合 MouseEvent）**：Button 回调唯一真实路径依赖事件链（OnMouseButtonUp → 坐标/命中/Window），违背 7.2 无窗口测试初衷——**Button 回调测试推迟到集成测试**；机制正确性由 TextBox 测试覆盖（同一套 RaiseXxx）
  3. **小建议（采纳）**：引入 `using ClickCallback` / `using TextChangedCallback` typedef，Phase 8 CheckBox 预留 `CheckedChangedCallback`——统一可读性
  4. 文档同步：文件清单 7 → 6（去掉 WidgetTests.cpp）；删除 §6 FireClickEvent 测试方案；D9 契约简化（不再需要测试协调）
- v1.1（2026-08-19）整合 GPT 一轮评审：测试文件归属（Button→WidgetTests）、TC1 冗余变量、TC2/TC8 局部变量编译错误、RaiseClick private 测试协调、D9 契约明确化
- v1.0（2026-08-19）初步设计初稿（职责确认 v1.2 后的完整接口设计）
