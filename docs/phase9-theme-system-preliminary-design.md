# Phase 9 主题系统 — 初步设计

> 状态：v1.1（2026-08-25）｜初步设计待审（GPT 评审整合）
> 前序：Phase 9 职责确认 v1.1（GPT 评审通过）/ Phase 8 渲染能力 ✅ / Phase 8.5 文本系统 2.0 ✅
> 相关：phase9-theme-system-requirements.md（职责确认 v1.1）/ Core/Color.h / Render/RenderingBackend.h / Widget/TextWidget.h / Widget/Button.h

## 1. 设计目标

在 Phase 9 职责确认 v1.1 基础上，解决 5 个边界问题，为详细设计铺路。

## 2. 边界问题解答（5 点）

### B1 Theme 抽象接口形态

**问题**：`Theme` 抽象基类接口具体长什么样？

**解答**：**纯样式查询虚方法（不持有基础属性——避免 Style 与基础属性双重真相）**

```cpp
class Theme{
public:
    virtual ~Theme() = default;

    // ── 控件样式查询（纯虚——子类必须提供默认值；Style 是唯一视觉属性来源）──────
    virtual ButtonStyle   GetButtonStyle() const = 0;
    virtual LabelStyle    GetLabelStyle() const = 0;
    virtual TextBoxStyle  GetTextBoxStyle() const = 0;
    virtual CheckBoxStyle GetCheckBoxStyle() const = 0;
    virtual RadioStyle    GetRadioStyle() const = 0;
    virtual PanelStyle    GetPanelStyle() const = 0;
};
```

**理由**：
1. **Style 是唯一视觉属性来源**——删除 `GetForegroundColor()`/`GetBorderWidth()`/`GetCornerRadius()`/`GetPadding()` 等基础属性查询——避免 Theme 同时存在"基础属性"和"Style 内嵌属性"两套真相（如 `theme.GetBorderWidth()` 返回 1.0f 但 `theme.GetButtonStyle().borderWidth` 返回 2.0f 的矛盾）
2. 每个控件样式结构（`ButtonStyle`/`TextBoxStyle` 等）是独立结构体——Theme 提供默认值，Widget 持有自己的副本（D6）
3. 纯虚方法强制子类提供完整默认值——避免某控件样式遗漏无默认行为

**数据层 vs 决策层**：Theme 只提供"默认视觉规范"（决策层），不管理运行时状态。Widget 的 `m_style` 是运行时状态（数据层）。

### B2 Style 结构如何持有 + Override 记录（D7 落地）

**问题**：`SetStyle()` 如何记录 override，让 `ApplyTheme()` 只更新未覆盖属性？

**解答**：**Style 结构 = 值 + overridden 标志位（`StyleField<T>` 泛型）**

```cpp
template<typename T>
struct StyleField{
    T value{};                       ///< 当前值
    bool overridden = false;         ///< true = SetStyle 覆盖过（ApplyTheme 不再更新）

    void Set(const T& v)             { value = v; overridden = true; }
    void Apply(const T& themeValue)  { if (!overridden) value = themeValue; }
    void Reset()                     { overridden = false; }
};

struct ButtonStyle{
    StyleField<Color> background;         ///< 背景色
    StyleField<Color> foreground;         ///< 文本色
    StyleField<Color> border;             ///< 边框色
    StyleField<float> borderWidth;        ///< 边框宽度
    StyleField<float> cornerRadius;       ///< 圆角半径
    StyleField<Color> pressedBackground;  ///< 按下态背景色（Widget 状态 + Style 状态属性决定）
};
```

**外部 API 边界**：外部不直接接触 `StyleField<T>`——通过 `ButtonStyleOverride` 结构：

```cpp
struct ButtonStyleOverride{
    std::optional<Color> background;
    std::optional<Color> foreground;
    std::optional<Color> border;
    std::optional<float> borderWidth;
    std::optional<float> cornerRadius;
    std::optional<Color> pressedBackground;
};

void Button::SetStyle(ButtonStyleOverride override){
    if (override.background)      m_style.background.Set(*override.background);
    if (override.foreground)      m_style.foreground.Set(*override.foreground);
    if (override.border)          m_style.border.Set(*override.border);
    if (override.borderWidth)     m_style.borderWidth.Set(*override.borderWidth);
    if (override.cornerRadius)    m_style.cornerRadius.Set(*override.cornerRadius);
    if (override.pressedBackground) m_style.pressedBackground.Set(*override.pressedBackground);
    Invalidate();
}
```

**ApplyTheme 语义（D7 契约落地）**：

```cpp
void Button::ApplyTheme(const Theme& theme){
    ButtonStyle defaults = theme.GetButtonStyle();
    m_style.background.Apply(defaults.background.value);
    m_style.foreground.Apply(defaults.foreground.value);
    m_style.border.Apply(defaults.border.value);
    m_style.borderWidth.Apply(defaults.borderWidth.value);
    m_style.cornerRadius.Apply(defaults.cornerRadius.value);
    m_style.pressedBackground.Apply(defaults.pressedBackground.value);
    Invalidate();   // 主题变更 → 重绘
}
```

**理由**：
1. `StyleField<T>` 泛型——避免每个属性写一对 `{value, overridden}`，DRY
2. `Apply()` = 条件更新（只有 !overridden 才更新）——D7 契约的代码级保证
3. `Set()` = 强制更新 + 标记 overridden = true
4. `ButtonStyleOverride` 用 `std::optional`——外部 API 不暴露 `StyleField` 内部机制

**Style 纯数据结构原则**（锁死）：Style 只描述视觉参数，不负责绘制。禁止在 Style 结构中出现 `Draw()`/`Paint()`/`Render()`/`ApplyToBackend()`/`CreateBrush()`/`CreatePen()` 等方法。

### B3 DefaultTheme 继承 Theme（必修）

**问题**：如何提供默认主题实例，同时让 `ApplyTheme(const Theme&)` 能接受它？

**解答**：**DefaultTheme 继承 Theme + 自由函数 `GetDefaultTheme()` 返回 static local**

```cpp
// DefaultTheme.h
class DefaultTheme : public Theme{
public:
    DefaultTheme();   ///< 构造填充全部默认值（硬编码在 cpp 内）

    // Theme 接口实现
    ButtonStyle   GetButtonStyle() const override;
    LabelStyle    GetLabelStyle() const override;
    TextBoxStyle  GetTextBoxStyle() const override;
    CheckBoxStyle GetCheckBoxStyle() const override;
    RadioStyle    GetRadioStyle() const override;
    PanelStyle    GetPanelStyle() const override;
};

// 自由函数（非 static 成员 = 不引入全局 Singleton 类）
/// @brief 获取当前默认主题实例（static local = 首次调用构造，非程序启动）
/// @details 不引入 ThemeManager 全局状态；未来可改为参数传入或 Window 持有
const DefaultTheme& GetDefaultTheme();
```

**理由**：
1. **继承 Theme**——`ApplyTheme(const Theme& theme)` 自然接受 `GetDefaultTheme()` 返回值（修复 v1.0 的编译错误）
2. **自由函数 + static local**——首次调用构造（非程序启动时），无全局初始化顺序问题；未来改为 Window 持有时只需改这一处
3. **不是 Singleton 类**——没有 `Instance()` 方法、没有全局访问点——外部代码依赖 `GetDefaultTheme()` 而非 `DefaultTheme::Instance()`
4. 不锁死多窗口多主题（D1）——未来 Window 可持有所需 Theme 子类

### B4 ApplyTheme 与 SetStyle 覆盖规则（D7 最终形态）

**问题**：ApplyTheme 只更新未覆盖属性——MVP 语义与 Reset 顺序？

**解答**：**ApplyTheme 是"主题默认值同步"，不清除运行时 Override**

| 场景 | 行为 |
|---|---|
| 初始状态 | Widget.m_style = Theme 默认值（overridden=false） |
| `SetStyle(.background=Red)` | background.overridden=true, value=Red |
| `ApplyTheme(newTheme)` | background 保持 Red（overridden=true 不更新）；其余属性更新为新主题默认值 |
| 再次 `ApplyTheme(sameTheme)` | 同上（幂等——重复应用无副作用） |

**Reset 语义**（MVP 不实现，留 Phase 9+）：
- 未来"重置单个控件到主题默认"需求出现时，加 `ResetStyleToTheme(theme)` = `ClearOverrides()` + `ApplyTheme(theme)`（先清后 Apply，确保新主题值写入）
- 禁止反向顺序（`ApplyTheme` + `ClearOverrides`）——会清除标志但不重新读取当前主题值
- MVP 场景通过重建 Widget 实现 Reset（YAGNI）

### B5 控件迁移策略（D5 一次性）

**问题**：Button / Label / TextBox / CheckBox / Radio / Panel 如何从硬编码迁移？

**解答**：**构造函数末尾调 `ApplyTheme(GetDefaultTheme())`——MVP 阶段可接受（Widget 短暂依赖 DefaultTheme）；详细设计必须明确这是架构妥协**

```cpp
// Button.cpp（迁移后，MVP）
Button::Button(const std::string& text){
    SetText(text);
    // 不再硬编码 m_textColor = White() 等
    ApplyTheme(GetDefaultTheme());   // 从主题注入默认样式
}
```

**架构演进方向**：
- **MVP**：构造函数直接调 `ApplyTheme(GetDefaultTheme())`（可接受——Phase 9 内 DefaultTheme 是稳定的具体类）
- **Phase 9+**：由 Window / Widget 初始化流程统一调 `ApplyTheme`，Widget 构造函数完全不知道 Theme——此时 Widget 只依赖 `Theme` 抽象接口

**迁移步骤**（每控件）：
1. 从 `DefaultTheme::GetXxxStyle()` 提取当前硬编码值
2. 删除构造函数内硬编码赋值
3. 构造末尾加 `ApplyTheme(GetDefaultTheme())`
4. OnPaint 改用 `m_style.background.value` 等（不再直接 `Color::FromRGBA8(...)`）
5. 删除硬编码常量（如 `kPressedBackground`）

**特殊处理**：
- **TextBox 焦点框内缩**：`padding = HasFocus() ? GetDefaultTheme().GetTextBoxStyle().padding.value : 0.0f`（焦点态是 Widget 状态，非主题管理）
- **Button 按下态背景色**：`m_style.pressedBackground.value`（主题提供，运行时由 Widget::m_pressed 状态选择绘制哪个颜色，不调用 SetStyle）

## 3. Style 使用场景澄清（D2 例子修正）

**`SetStyle()` 适用场景**：运行时用户自定义样式（如业务代码 `button.SetStyle({.background=Red})`）

**`SetStyle()` 不适用场景**：Hover/Pressed 等瞬时状态——由 Widget 内部状态 + Style 中对应状态属性决定（如 `m_pressed ? m_style.pressedBackground.value : m_style.background.value`）

**SetStyle 产生的是 Widget 生命周期级 Runtime Override**（一次性状态覆盖不是 hover 消失后恢复的理由）——直到显式 `ResetStyleToTheme` 或重建 Widget。

## 4. 架构边界（锁死）

```
                  ┌─────────────────┐
                  │      Theme      │
                  │ 默认视觉规范     │
                  │ (GetXxxStyle)   │
                  └────────┬────────┘
                           │
                    ApplyTheme()
                           │
                           ▼
                  ┌─────────────────┐
                  │ Widget::Style   │
                  │                 │
                  │ StyleField<T>   │
                  │  value          │
                  │  overridden     │
                  └────────┬────────┘
                           │
                     Widget 状态
                  (normal/pressed/focused...)
                           │
                           ▼
                  ┌─────────────────┐
                  │      Paint      │
                  │ Style → Command │
                  └────────┬────────┘
                           │
                           ▼
                  ┌─────────────────┐
                  │    Renderer     │
                  └────────┬────────┘
                           │
                           ▼
                  ┌─────────────────┐
                  │ RenderingBackend│
                  │                 │
                  │ Phase 8 能力    │
                  └─────────────────┘
```

**层级回答的问题**（职责不混）：
- Theme 回答："默认应该长什么样？"
- Widget Style 回答："我现在实际长什么样？"
- Renderer 回答："把这个视觉状态变成什么绘制命令？"
- Backend 回答："我能不能把这个命令画出来？"

## 5. 模块划分

```
include/ECDI/Theme/
├── Theme.h                    ///< Theme 抽象基类（纯样式查询虚方法）
├── DefaultTheme.h             ///< DefaultTheme 类（继承 Theme）+ GetDefaultTheme() 自由函数
├── StyleField.h               ///< StyleField<T> 泛型模板
├── ButtonStyle.h              ///< ButtonStyle 结构 + ButtonStyleOverride
├── LabelStyle.h               ///< LabelStyle 结构 + LabelStyleOverride
├── TextBoxStyle.h             ///< TextBoxStyle 结构 + TextBoxStyleOverride
├── CheckBoxStyle.h            ///< CheckBoxStyle 结构 + CheckBoxStyleOverride
├── RadioStyle.h               ///< RadioStyle 结构 + RadioStyleOverride
└── PanelStyle.h               ///< PanelStyle 结构 + PanelStyleOverride（2026-08-29 落地——此前 MVP 主动无 Override，归 YAGNI）

src/ECDI/Theme/
└── DefaultTheme.cpp           ///< DefaultTheme 实现（填充当前硬编码值为默认值）

src/ECDI/Widget/
├── Button.cpp                 ///< 硬编码删除 + ApplyTheme + SetStyle
├── TextWidget.cpp             ///< 硬编码删除 + ApplyTheme + SetStyle（TextWidget/Label 共享）
├── TextBox.cpp                ///< 硬编码删除 + ApplyTheme + SetStyle
└── Panel.cpp                  ///< 硬编码删除 + ApplyTheme
```

## 6. 实施顺序

```
Phase 9.1 主题基础设施（B1-B5）
  ① StyleField.h（泛型模板）
  ② 各 *Style.h 结构体（ButtonStyle/LabelStyle/TextBoxStyle/CheckBoxStyle/RadioStyle/PanelStyle + Override 结构）
  ③ Theme.h（抽象基类）
  ④ DefaultTheme.h/.cpp（默认值填充——当前硬编码值迁移到这里）
  → 验证：编译通过 + 纯单元测试（StyleField/DefaultTheme 不依赖窗口）

Phase 9.2 控件样式迁移（B5）
  ⑤ TextWidget.h/.cpp（SetForegroundColor/SetFont → SetStyle + ApplyTheme）
  ⑥ Label.h/.cpp（硬编码删除 + ApplyTheme）
  ⑦ Button.h/.cpp（硬编码删除 + ApplyTheme + SetStyle）
  ⑧ TextBox.h/.cpp（硬编码删除 + ApplyTheme + SetStyle）
  ⑨ Panel.h/.cpp（硬编码删除 + ApplyTheme）
  → 验证：视觉对比（迁移前后应一致）

Phase 9.3 Alpha 消费（T3）
  ⑩ 评估是否需要——若 Phase 8 Alpha 已在 Button/Panel 背景消费，则本项跳过
  → 验证：半透明视觉效果（Alpha Blend 消费端）
```

## 7. Phase 9 TestCase（无窗口单元测试，7.2 体系）

| # | 测试 | 断言点 | 说明 |
|---|---|---|---|
| T-F01 | StyleField 初始值 | value=默认, overridden=false | 基础契约 |
| T-F02 | Set() 修改 value + overridden=true | Set 后标志位置 true | D7 |
| T-F03 | Apply() 未 override 时更新 | overridden=false 时 Apply 写入 | D7 核心 |
| T-F04 | Apply() 已 override 时不更新 | overridden=true 时 Apply 不修改 value | D7 核心 |
| T-F05 | 多次 Apply 幂等 | 多次 Apply 结果一致 | 幂等性 |
| T-F06 | DefaultTheme 所有 Style 均有合法默认值 | 各字段非空/非垃圾值 | 默认值完备 |
| T-F07 | Button ApplyTheme（无 Override） | 各字段写入默认值 | 控件集成 |
| T-F08 | SetStyle 后 ApplyTheme 不覆盖 | SetStyle 字段保持，其余更新 | D7 契约 |
| T-F09 | DefaultTheme 自身一致性 | 不同 GetXxxStyle 调用返回相同值 | 稳定性 |

## 8. 与既有约束的对齐

| 约束 | 对齐方式 |
|---|---|
| skill 15 分层 | Theme 是决策层（零 Win32 类型）；消费在 Widget/Renderer 层 |
| skill 19 能力/决策正交 | Backend 提供能力（Phase 8）/ Theme 提供决策（Phase 9）——换主题后端不动 |
| skill 21 YAGNI | 无 ThemeManager / StyleSheet / CSS / 动画 / 层级继承；DefaultTheme 不引入全局状态 |
| skill 22 分层论证 | 契约语言描述（"ApplyTheme = 同步主题默认值到 Widget Style，只更新未 Override 属性"） |
| 资源类禁复制禁移动 | StyleField 值语义（可复制）/ DefaultTheme 值语义 |
| 测试由用户做 | 视觉验证由用户编译运行确认；**D7 StyleField 纯逻辑由 TestCase 自动覆盖**（无窗口依赖） |
| 五阶段法 | 本文档 = 初步设计；确认后进详细设计 |
| 文档约定 | 更新 `docs/phase9-theme-system-preliminary-design.md` v1.0 → v1.1 |

## 9. 修订记录

- v1.1（2026-08-25）GPT 评审整合（"小修后进入详细设计"——5 项必修）：**① B3 改为 `DefaultTheme : public Theme`**（修复 v1.0 编译错误：ApplyTheme(const Theme&) 无法接受 GetDefaultTheme() 返回值）；**② B1 删除 Theme 中 GetForegroundColor/GetBorderWidth/GetCornerRadius/GetPadding 基础属性查询**（避免 Style 与基础属性双重真相——Style 是唯一视觉属性来源）；**③ B4 明确 Reset 顺序 = ClearOverrides() + ApplyTheme(theme)**（MVP 不实现，禁止反向）；**④ B5 修正 SetStyle 不适用 hover 例子**（改为"运行时用户样式覆盖"，Hover/Pressed 由 Widget 状态 + Style 状态属性决定）；**⑤ §7 新增 Phase 9 TestCase（T-F01-T-F09）**覆盖 StyleField/DefaultTheme/D7 契约（无窗口单元测试）；新增 §4 架构边界图 + Style 纯数据结构原则；新增 §3 SetStyle 场景澄清（生命周期级 Override）。
- v1.0（2026-08-25）初步设计初稿：B1 Theme 接口（属性查询纯虚）/ B2 StyleField\<T\> 泛型 + D7 落地（Apply/Set 语义）/ B3 DefaultTheme 非 Singleton（自由函数 + static local）/ B4 ApplyTheme 不清除 Override / B5 控件迁移策略（构造函数 + ApplyTheme）/ 模块划分 / 实施顺序（9.1/9.2/9.3）。
