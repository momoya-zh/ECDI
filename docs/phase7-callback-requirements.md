# Phase 7.5 事件回调（std::function 回调注册 API）职责确认

> 状态：v1.2（2026-08-19）｜职责确认待审（GPT 二轮评审整合）
> 前序：Phase 7.1 平台抽象 ✅ / Phase 7.2 无窗口单元测试体系 ✅
> 相关文档：phase6-checkboxradio-requirements.md（C4 契约）/ phase7-testing-requirements.md（测试边界）/ roadmap-deferred.md（#9）

## 1. 目标与动机

在 Phase 7 平台解耦（7.1）与测试体系（7.2）完成后，为控件提供 **std::function 回调注册 API**——业务代码不再必须通过"继承 + override 虚方法"绑定交互（DemoButton 先例），可以就地注册回调（适合表单/数据绑定、Lambda 场景）。

MEMORY 决策原文（2026-08-15）：
> std::function 回调注册 API（Button::SetOnClick / CheckBox::SetOnCheckedChanged 等），两套并存（继承 override 基座 + 回调业务便利层）——在 Phase 7 解耦后做（回调 API 依赖解耦后稳定接口）；触发 = 表单/数据绑定需求

## 2. 现状盘点

| 项 | 现状 |
|---|---|
| Button | `OnClick()` 是 **protected virtual 空实现**（Button.h L37）；`OnMouseButtonUp` 内"拖出取消 + 恢复视觉后"调用（Button.cpp L61-65） |
| 业务绑定 | 继承 + override（DemoButton 先例，main.cpp L27-39） |
| TextBox | 编辑 API public（InsertCodepoint/DeleteBackward/DeleteForward/MoveCaret×3），文本变化点清晰，**无窗口可测**（7.2 已证明） |
| TextWidget::SetText | public **非 virtual**（TextWidget.h L27-29）——TextBox 程序性设值走它 |
| CheckBox/Radio | **未实现**（Phase 8 后，勾/圆依赖渲染能力）；职责确认已定稿，C4 契约：`virtual void OnCheckedChanged(bool checked){}` 虚方法，并注明"回调（std::function）推迟——表单/数据绑定时代再评估"——**7.5 正是该时代** |
| 测试体系 | 7.2 就绪：无窗口单元测试 + `src/Tests/` 目录 + 每模块 `RunXxxTests()` 入口 |

## 3. 范围界定

### 3.1 范围内

| # | 内容 | 优先级 |
|---|---|---|
| R1 | **回调注册机制模式确立（三件套）**：① std::function 值成员 ② 公开 SetXxx 注册 ③ RaiseXxx 入口——事件/状态变化点调 RaiseXxx，其内部**先调 OnXxx 虚方法、再调回调**（彼此独立，见 D4） | P0 |
| R2 | **Button::SetOnClick(std::function<void()>)**——第一个消费者（既有虚方法 OnClick 基座） | P0 |
| R3 | **TextBox::SetOnTextChanged(std::function<void(const std::string&)>)**——表单/数据绑定核心需求（文本变化通知）；触发点 = 编辑操作（InsertCodepoint/DeleteBackward/DeleteForward）实际改变文本时 | P1 |
| R4 | **CheckBox::SetOnCheckedChanged 模式预留**——控件未实现，7.5 不写代码；在文档中把模式写清，Phase 8 后 CheckBox 实现时按 R1 同模式落地（与 phase6 C4 契约衔接） | P2（文档） |
| R5 | **回调测试**（7.2 体系扩展）：Button 注册→触发→断言；TextBox 编辑→回调收到新文本→断言 | P0~P1 |

### 3.2 范围外

- **CheckBox/Radio 控件实现本身**（Phase 8 后，勾/圆依赖渲染能力——既有决策，不变）
- **Widget 基类通用回调**（如 SetOnMouseMove 等事件级回调）——YAGNI：无消费者（表单/数据绑定需要的是业务语义回调：点击/状态/文本变化，不是原始事件）
- **信号槽系统**（Qt 风格 connect/disconnect/多回调/自动断开）——YAGNI，单一回调 + 覆盖式注册即可
- **带事件参数的回调**（如 `std::function<void(const MouseButtonUpEvent&)>`）——回调签名与虚方法签名一致（见 D2），需要事件细节时用继承 override
- **程序性 SetText 是否触发回调**——见 D7（决策点）
- **Async/跨线程回调**——回调同步调用（事件处理线程内）

## 4. 关键决策点（含倾向）

### D1 回调机制放哪层？
- 方案 A：**控件级各自声明**（Button::SetOnClick、TextBox::SetOnTextChanged……每个控件声明自己的业务回调）
- 方案 B：Widget 基类统一回调注册（SetEventHandler 之类）
- **倾向 A**：YAGNI（skill 21——不做无消费者的抽象）；控件业务回调天然不同签名（无参/bool/string），统一抽象收益低

### D2 回调签名？
- **倾向：回调签名与既有虚方法签名一致**（最小惊讶）：
  - Button::SetOnClick → `std::function<void()>`（OnClick 无参）
  - TextBox::SetOnTextChanged → `std::function<void(const std::string&)>`（新文本，UTF-8）
  - CheckBox::SetOnCheckedChanged → `std::function<void(bool)>`（与 phase6 C4 虚方法签名一致，新状态参数——外部无需再查 IsChecked）

### D3 TextBox 是否建虚方法基座？【GPT v1.2 修订：统一 RaiseXxx/OnXxx 命名】
- TextBox 现**无**文本变化虚方法（不同于 Button 已有 OnClick 基座）
- 方案 A：只加回调（SetOnTextChanged），不建虚方法——YAGNI
- 方案 B（**GPT 建议，倾向 B**）：建 **protected virtual** 钩子，遵循与 Button 完全一致的 `RaiseXxx → OnXxx → callback` 三段式：

  ```cpp
  // TextBox 内部（编辑操作调用）
  void TextBox::RaiseTextChanged(){     // 内部入口（private，非虚）
      OnTextChanged(m_text);            // ① 保护虚方法（子类可 override 扩展）
      if (m_onTextChanged)              // ② 回调（独立通道，override 无法吞掉）
          m_onTextChanged(m_text);
  }
  protected:
  virtual void OnTextChanged(const std::string& text);   // 保护虚方法钩子
  ```

  调用链：`InsertCodepoint → RaiseTextChanged → OnTextChanged() → m_onTextChanged()`——**override 不吞回调**

- **理由（GPT）**：命名统一性——Button 是 `RaiseClick` + `OnClick`，TextBox 必须是 `RaiseTextChanged` + `OnTextChanged`，不能出现 `NotifyTextChanged` 这种"异类命名"（违反一致性原则）。未来 CheckBox/Radio 也是 `RaiseCheckedChanged` + `OnCheckedChanged`，整个框架形成统一范式：

  ```
  状态变化
       ↓
  RaiseXxx()        [private，内部入口]
       ↓
  OnXxx()           [protected virtual，子类扩展钩子]
       ↓
  callback          [std::function，独立通道]
  ```

- ⚠️ 命名修正（v1.2）：`NotifyTextChanged()` → `RaiseTextChanged()` + `OnTextChanged()`——保证 RaiseXxx 内部入口与 OnXxx 钩子名称完全对齐（GPT 二轮评审明确要求）

### D4 虚方法基座与回调的调用关系？【GPT v1.2 修订：统一 RaiseXxx/OnXxx 命名】
- ~~方案 A：虚方法内部调用回调~~（`Button::OnClick(){ if(m_onClick) m_onClick(); }`）——**❌ 否决（GPT 指出危险）**：子类 override OnClick 不调基类 → 回调静默失效。用户直觉"SetOnClick 后点击一定执行"，而非"除非继承链上有人忘了调基类"
- **方案 B（GPT 建议，采用）：RaiseXxx 分离模式——事件 → RaiseXxx → 虚方法 → 回调，四者独立**：
  ```cpp
  // Button 内部（OnMouseButtonUp 调用）
  void Button::RaiseClick(){        // 内部入口（private，非虚）
      OnClick();                    // ① 虚方法（子类可 override 扩展/拦截）
      if (m_onClick) m_onClick();   // ② 回调（独立通道，override 无法吞掉）
  }

  // TextBox 内部（InsertCodepoint/DeleteBackward/DeleteForward 调用）
  void TextBox::RaiseTextChanged(){     // 内部入口（private，非虚）
      OnTextChanged(m_text);            // ① 保护虚方法（子类可 override）
      if (m_onTextChanged)              // ② 回调（独立通道）
          m_onTextChanged(m_text);
  }
  ```
  调用链：
  - Button: `MouseButtonUp → RaiseClick → OnClick() → m_onClick()`
  - TextBox: `InsertCodepoint → RaiseTextChanged → OnTextChanged() → m_onTextChanged()`
  - **override 不吞回调**（与 Qt 信号/槽"与继承无关"同构）

- **统一范式**（整个框架任何控件全按此——GPT v1.2 核心洞察）：
  ```
  状态变化
       ↓
  RaiseXxx()        [private，内部入口——非虚、非公开]
       ↓
  OnXxx()           [protected virtual，子类扩展钩子]
       ↓
  callback          [std::function，独立通道——override 无法关闭]
  ```
  | 控件 | RaiseXxx | OnXxx | callback |
  |---|---|---|---|
  | Button | `RaiseClick()` | `OnClick()` | `m_onClick` |
  | TextBox | `RaiseTextChanged()` | `OnTextChanged(text)` | `m_onTextChanged` |
  | CheckBox | `RaiseCheckedChanged()` | `OnCheckedChanged(bool)` | `m_onCheckedChanged` |

- **与 5.5.1.4 先例的关系**（GPT 提问澄清）：不冲突。5.5.1.4 是 Application override `OnCharInput` **吞掉框架事件派发**（事件入口虚方法 = 派发链，override 需负责转发）；控件层 `RaiseXxx` 的 `OnXxx` 是**业务钩子 + 独立回调通道**（虚方法扩展不影响回调消费）。场景不同，各自成立

### D5 回调生命周期与线程？
- std::function 为控件值成员——控件析构自动释放（无泄漏）
- 回调捕获外部对象 → **外部对象生命周期由注册方负责**（悬空风险用户承担——std::function 通用语义，文档约定）
- 回调**同步调用**（消息循环线程内），无队列/无异步
- **倾向：以上即默认语义，文档写明即可，不做额外机制**

### D6 回调注册/解除？
- `SetOnClick({})` / `SetOnTextChanged({})` 传空 std::function 即**解除注册**
- **倾向：不需要专门 Clear 接口**（空 function 即解除，文档注明）

### D7 程序性 SetText 是否触发 OnTextChanged？（新增决策点）
- 触发点候选：① 编辑操作（InsertCodepoint/DeleteBackward/DeleteForward 实际改文本时）② TextWidget::SetText（程序设值）
- 主流框架语义：Qt QLineEdit::textChanged 在 setText 时也发；WinForms TextChanged 赋值也触发
- 但 ECDI 的 TextBox 文本变化**核心场景是用户输入**（编辑操作），SetText 常用于程序初始化
- 方案 A：仅编辑操作触发（SetText 不触发——程序设值不算"用户修改"）
- 方案 B：编辑操作 + SetText 都触发（与主流框架一致；需 SetText 变 virtual 或 TextBox override——**TextWidget::SetText 现非 virtual**，是接口改动）
- **倾向 A（7.5 先做编辑操作触发）**：SetText 触发涉及 TextWidget 接口改动（非 virtual → virtual），范围扩大；且 SetText 初始化场景触发回调易造成"初始化误报"。B 留待未来需要时评估
- ⚠️ 边界语义：**文本实际变化才触发**（如 DeleteBackward 头边界空操作——不触发；最小惊讶）

### D8 测试策略（7.2 体系扩展）？【随 D4 v1.2 更新，GPT 点赞】
- Button 回调：测试子类暴露触发入口（`class TestButton : public Button { public: void SimulateClick(){ RaiseClick(); } };`），注册回调 → SimulateClick → 断言回调触发
- **核心语义测试（GPT v1.2 明确要求）**：
  - 子类 override OnClick **不调基类** + 注册回调 + SimulateClick → **回调仍触发**（验证"override 不吞回调"——D4 修改的核心收益）
  - 空回调 + SimulateClick → 不崩溃（空 std::function 安全性）
  - SetOnClick(nullptr) 后 SimulateClick → 不触发、不崩溃（解除注册语义）
- TextBox 回调：编辑 API public 无窗口可测——注册 → InsertCodepoint/DeleteBackward → 断言回调收到正确新文本 + 触发次数；空串编辑（DeleteBackward 头边界空操作）→ 不触发回调
- 测试归属：ButtonTests/TextBoxTests 各自模块文件（7.2 结构沿用）；测试运行/验证由用户做（skill 第 1 条）

### D9 RaiseXxx 入口可见性规范？【GPT v1.2 明确要求】
- **核心约束：`RaiseXxx()` 必须是 `private`**，禁止作为公开 API
  ```cpp
  class Button {
  private:
      void RaiseClick();     // 内部入口——外部不可调
  protected:
      virtual void OnClick(); // 子类扩展钩子
  public:
      void SetOnClick(...);   // 公开注册
  };
  ```
- **理由（GPT v1.2）**：
  - `RaiseXxx()` 本质是内部通知机制，不是公开 API
  - 如果 public：`button.RaiseClick();` 直接触发 → 绕过正常的鼠标事件处理链 → 破坏一致性
  - 测试需要暴露触发点 → 用 `protected` 的子类暴露方案（TestButton 继承暴露）比 public 更安全
- **统一规范**：
  | 成员 | 可见性 |
  |---|---|
  | `RaiseClick()` | `private` |
  | `OnClick()` | `protected virtual` |
  | `SetOnClick(...)` | `public` |
- 此规范适用于所有控件的 RaiseXxx 入口

## 5. 与既有约束的对齐

| 约束 | 对齐方式 |
|---|---|
| skill 15 分层 | 回调 API 零 Win32 类型（void/bool/string）——纯框架层 |
| skill 16 Event 原则 | 回调是"业务便利层"，不是事件——不改事件系统，不改 EventRouter；RaiseXxx 是控件内部业务通知入口，与事件派发链（Application→Widget OnXxx）正交 |
| skill 21 YAGNI | 范围外清单：不做 Widget 基类通用回调/信号槽/带事件参数回调；TextBox 补保护虚方法钩子（D3）是"微成本高回报"的对称性投资，非抽象膨胀 |
| 资源类禁复制禁移动 | std::function 成员无影响（控件不可复制移动，回调随控件走） |
| 测试由用户做 | 实现后提醒用户编译 + 运行验证（7.2 已立规矩） |
| 五阶段法 | 本文档 = 职责确认；确认后进初步设计 |

## 6. 修订记录

- **v1.2（2026-08-19）整合 GPT 二轮评审**：
  - **D3 修订**：`NotifyTextChanged()` → `RaiseTextChanged()` + `OnTextChanged()`（统一命名，与 Button/CheckBox 完全对齐）
  - **D4 统一范式**：`RaiseXxx`(private) → `OnXxx`(protected virtual) → `callback`(独立通道) 三段式，整个框架统一
  - **D9 新增**：RaiseXxx 必须是 `private`，禁止作为公开 API（测试用 TestButton 子类暴露）
  - D8 核心语义测试细化：增加空回调安全性 + 解除注册语义测试
- v1.1（2026-08-19）整合 GPT 一轮评审：D4 改向（RaiseXxx 分离模式）、D3 改向（TextBox 补虚钩子）
- v1.0（2026-08-17）职责确认初稿
