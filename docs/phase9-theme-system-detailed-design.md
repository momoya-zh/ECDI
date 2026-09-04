# Phase 9 主题系统 — 详细设计

> 状态：v1.8（2026-08-31）｜v1.4 定稿后增量修订（v1.5 9.6 收尾回写 / v1.6 Panel 设色 / v1.7 Panel 容器语义变更 / v1.8 ProgressBar 主题接入——修订记录见 §10）
> 前序：Phase 9 初步设计 v1.1（GPT 评审通过）/ Phase 8 渲染能力 ✅ / Phase 8.5 文本系统 2.0 ✅
> 相关：phase9-theme-system-preliminary-design.md（初步设计 v1.1）/ Core/Color.h / Core/Font.h / Render/RenderingBackend.h / Widget/Button.cpp / Widget/Panel.cpp / Widget/TextWidget.cpp / Widget/Label.h / Widget/TextBox.cpp

## 1. 范围

**Phase 9 本阶段覆盖**：主题基础设施（`Theme`/`DefaultTheme`/`StyleField`）+ 控件样式迁移（TextWidget/Label/Button/TextBox/Panel）+ 单元测试（T-F01-T-F10）。

**明确移出**：CheckBox / Radio——**控件本身尚未实现**（Phase 6.2 待做），Phase 6.2 实现时直接带主题系统（`CheckBoxStyle`/`RadioStyle` 届时新增）。职责确认 T4 中"CheckBox/Radio"的措辞修正为"Phase 6.2 落地时纳入"。

**单一视觉真相来源原则**（锁死）：某一种视觉属性只存在一个权威来源——文字颜色/字体统一来自 `TextStyle`（TextWidget 持有）；背景/边框/圆角等来自各控件专属 Style。**禁止 `ButtonStyle.foreground` 与 `TextStyle.foreground` 并存**（双重真相 → SetStyle 后文字不变色的隐性 Bug）。

## 2. 文件改动清单（原子授权）

| # | 文件 | 操作 | 改动内容 |
|---|---|---|---|
| 1 | `include/ECDI/Theme/StyleField.h` | **新增** | `StyleField<T>` 泛型模板 |
| 2 | `include/ECDI/Theme/TextStyle.h` | **新增** | `TextStyle` + `TextStyleOverride`（foreground + font） |
| 3 | `include/ECDI/Theme/ButtonStyle.h` | **新增** | `ButtonStyle` + `ButtonStyleOverride`（无 foreground——用 TextStyle） |
| 4 | `include/ECDI/Theme/TextBoxStyle.h` | **新增** | `TextBoxStyle` + `TextBoxStyleOverride`（无 foreground——用 TextStyle） |
| 5 | `include/ECDI/Theme/PanelStyle.h` | **新增** | `PanelStyle`（无 Override——MVP 主动限制） |
| 6 | `include/ECDI/Theme/Theme.h` | **新增** | `Theme` 抽象基类（`GetTextStyle`/`GetButtonStyle`/`GetTextBoxStyle`/`GetPanelStyle`） |
| 7 | `include/ECDI/Theme/DefaultTheme.h` | **新增** | `DefaultTheme` 类（继承 Theme）+ `GetDefaultTheme()` 自由函数 |
| 8 | `src/ECDI/Theme/DefaultTheme.cpp` | **新增** | `DefaultTheme` 实现（复刻当前硬编码值） |
| 9 | `include/ECDI/Widget/TextWidget.h` | **修改** | 加 `m_style`（`TextStyle`）+ `ApplyTheme()` + `SetStyle()`；删 `m_textColor`；`SetTextColor` 保留（内部转发 SetStyle）；`m_font` 迁移进 TextStyle |
| 10 | `src/ECDI/Widget/TextWidget.cpp` | **修改** | 构造函数调 `ApplyTheme(GetDefaultTheme())`；`SetTextColor` 转发；`DrawTextContent` 用 `m_style.foreground.value`/`m_style.font.value` |
| 11 | `include/ECDI/Widget/Label.h` | **修改** | 零改动（继承 TextWidget 的 TextStyle——无额外 Style；仅在注释更新） |
| 12 | `src/ECDI/Widget/Label.cpp` | **修改** | 构造函数**不重复** ApplyTheme（TextWidget 构造已完成） |
| 13 | `include/ECDI/Widget/Button.h` | **修改** | 加 `m_style`（`ButtonStyle`）+ `ApplyTheme()` override + `SetStyle()` |
| 14 | `src/ECDI/Widget/Button.cpp` | **修改** | 构造函数调 `ApplyTheme`；OnPaint 用 `m_style.xxx.value`；删硬编码颜色常量 |
| 15 | `include/ECDI/Widget/TextBox.h` | **修改** | 加 `m_style`（`TextBoxStyle`）+ `ApplyTheme()` override + `SetStyle()` |
| 16 | `src/ECDI/Widget/TextBox.cpp` | **修改** | 构造函数调 `ApplyTheme`；OnPaint 用 `m_style.xxx.value`；焦点框内缩改用 `m_style.padding.value` |
| 17 | `include/ECDI/Widget/Panel.h` | **修改** | 加 `m_style`（`PanelStyle`）+ `ApplyTheme()` |
| 18 | `src/ECDI/Widget/Panel.cpp` | **修改** | 构造函数调 `ApplyTheme`；OnPaint 用 `m_style.background.value` |
| 19 | `src/Tests/ThemeTests.cpp` | **新增** | T-F01-T-F10 单元测试（含 TestableButton） |
| 20 | `ECDI.vcxproj` | **修改** | 新增 Theme 头文件 ×7 + ThemeTests.cpp |
| 21 | `CMakeLists.txt` | **修改** | 新增 ThemeTests.cpp（GLOB 收 .cpp；头文件自动可见） |

**main.cpp 不动**（skill 2）。

## 3. 数据结构设计

### 3.1 StyleField\<T\>（核心机制）

```cpp
// include/ECDI/Theme/StyleField.h
#pragma once

namespace ECDI{

/// @brief 主题样式字段（值 + Override 标志位）
/// @details D7 契约的代码级保证：Apply() 只在 !overridden 时更新值
template<typename T>
struct StyleField{
    T value{};                       ///< 当前值（默认构造）
    bool overridden = false;         ///< true = SetStyle 覆盖过

    /// @brief 运行时覆盖（标记 overridden = true）
    void Set(const T& v)             { value = v; overridden = true; }

    /// @brief 主题默认值同步（只在未 Override 时更新）
    void Apply(const T& themeValue)  { if (!overridden) value = themeValue; }

    /// @brief 清除 Override 标志（值不变，下次 Apply 可更新）
    /// @details Reset() 不恢复 Theme 值，仅清除标志——恢复 Theme 默认值需对对应字段 Reset() 后重新 ApplyTheme()。
    /// Phase 9 MVP 不提供 Widget 层 Override 清除 API（m_style 私有内部状态；Reset 场景通过重建 Widget 实现——YAGNI）。
    /// @note 🔴 v1.4: **Reset() 是 StyleField 层原语，不构成 Widget 的运行时样式 API。**
    /// Widget 层是否允许清除 Override 由具体 Widget API 决定——Phase 9 MVP 一律不提供（不要据此推断"Button 应该提供 ClearStyle()"）。
    void Reset()                     { overridden = false; }
};

}
```

**生命周期契约**（锁死）：
- 默认构造的 `StyleField` 处于"未初始化视觉状态"（`value = T{}`, `overridden = false`）
- **任何 Style 在正式进入绘制路径之前必须完成 `ApplyTheme()` 初始化**
- Widget 构造函数末尾必须调用 `ApplyTheme(GetDefaultTheme())`——否则可能出现默认构造值直接进入绘制

### 3.2 TextStyle（文本控件共享）

```cpp
// include/ECDI/Theme/TextStyle.h
#pragma once
#include "ECDI/Theme/StyleField.h"
#include "ECDI/Core/Color.h"
#include "ECDI/Core/Font.h"
#include <optional>

namespace ECDI{

/// @brief 文本控件共享样式（TextWidget/Label/Button/TextBox 的文字视觉——唯一来源）
struct TextStyle{
    StyleField<Color> foreground;    ///< 文字颜色
    StyleField<Font> font;           ///< 字体
};

/// @brief 文本样式运行时覆盖
struct TextStyleOverride{
    std::optional<Color> foreground;
    std::optional<Font> font;
};

}
```

**派生控件 SetStyle 名字隐藏规则（锁死）**：Button/TextBox 提供专属 `SetStyle()` 时**必须 `using TextWidget::SetStyle;`**——否则 C++ 名字隐藏会使 `TextWidget::SetStyle(TextStyleOverride)` 无法从派生控件直接调用（Button 拥有 TextStyle + ButtonStyle 两个 Style，需能分别设置）。见 §5.3/§5.4。

**重载歧义契约（v1.3）**：Button/TextBox 同时暴露 `SetStyle(TextStyleOverride)` 与 `SetStyle(专属StyleOverride)` 两个重载（`using` 合并）。**空初始化列表 `SetStyle({})` 可能产生重载歧义**（两个 Override 类型都能从 `{}` 构造）——调用方必须使用明确类型的 Override 对象（如 `TextStyleOverride text; text.foreground = ...; button.SetStyle(text);`）。此歧义是 Phase 9 API 的主动接受，不为消除歧义引入额外接口（YAGNI）。

### 3.3 各控件专属 Style（不含 foreground——单一真相）

```cpp
// include/ECDI/Theme/ButtonStyle.h
#pragma once
#include "ECDI/Theme/StyleField.h"
#include "ECDI/Core/Color.h"
#include <optional>

namespace ECDI{

/// @brief Button 专属样式（文字颜色/字体统一来自 TextStyle——禁止在此重复）
struct ButtonStyle{
    StyleField<Color> background;         ///< 背景色（正常态）
    StyleField<Color> border;             ///< 焦点边框色
    StyleField<float> borderWidth;        ///< 边框宽度（内缩量）
    StyleField<float> cornerRadius;       ///< 圆角半径（0 = 直角）
    StyleField<Color> pressedBackground;  ///< 按下态背景色
};

struct ButtonStyleOverride{
    std::optional<Color> background;
    std::optional<Color> border;
    std::optional<float> borderWidth;
    std::optional<float> cornerRadius;
    std::optional<Color> pressedBackground;
};

}
```

```cpp
// include/ECDI/Theme/TextBoxStyle.h
#pragma once
#include "ECDI/Theme/StyleField.h"
#include "ECDI/Core/Color.h"
#include <optional>

namespace ECDI{

/// @brief TextBox 专属样式（文字颜色/字体统一来自 TextStyle——禁止在此重复）
struct TextBoxStyle{
    StyleField<Color> background;    ///< 背景色
    StyleField<Color> border;        ///< 焦点边框色
    StyleField<float> borderWidth;   ///< 边框宽度（内缩量）
    StyleField<Color> selection;     ///< 选区高亮色
    StyleField<Color> composition;   ///< 组合串下划线色
    StyleField<float> caretWidth;    ///< 光标竖线宽
    StyleField<float> padding;       ///< 焦点态内缩量
};

struct TextBoxStyleOverride{
    std::optional<Color> background;
    std::optional<Color> border;
    std::optional<float> borderWidth;
    std::optional<Color> selection;
    std::optional<Color> composition;
    std::optional<float> caretWidth;
    std::optional<float> padding;
};

}
```

```cpp
// include/ECDI/Theme/PanelStyle.h
#pragma once
#include "ECDI/Theme/StyleField.h"
#include "ECDI/Core/Color.h"

namespace ECDI{

/// @brief Panel 专属样式
/// @details 样式字段均为 StyleField（含 override 标志位，D7 契约）。
/// 单实例覆盖经 Panel::SetStyle(PanelStyleOverride)——2026-08-29 需求落地
/// （此前 MVP 主动限制无 Override，归 Phase 9 范围控制 YAGNI；需求出现即补，与 Button/TextBox 同构）。
struct PanelStyle{
    StyleField<Color> background;    ///< 背景色
};

/// @brief Panel 样式运行时覆盖（仅 background——Panel 当前唯一样式字段）
struct PanelStyleOverride{
    std::optional<Color> background;
};

}
```

### 3.4 Style 与 Widget 的组合关系（锁死）

> ⚠️ v1.4 措辞修正：Style 之间**无继承关系**（各 Style 是独立值对象），Widget 与 Style 是**组合关系**——"Button 拥有 TextStyle + ButtonStyle" 是**包含（has-a）**，不是 **is-a**。此图仅表达"Theme 提供哪种 Style 给哪个 Widget"。

```
                     Theme
                       │
        ┌──────────────┼───────────────┐
        ▼              ▼               ▼
   TextStyle      ButtonStyle      TextBoxStyle    PanelStyle
        │              │               │            │
        │（组合持有）    │（组合持有）     │（组合持有）  │（组合持有）
        ▼              ▼               ▼            ▼
  TextWidget        Button          TextBox       Panel
    ├── Label       ├── TextWidget::m_style（TextStyle，继承自基类）
    │               └── Button::m_style（ButtonStyle，本类持有）
    └── Button      ├── TextWidget::m_style（TextStyle）
    └── TextBox     └── TextBox::m_style（TextBoxStyle）
```

**字段分配（组合）**：
- `TextStyle`：foreground、font——所有文本控件共享（TextWidget 持有）
- `ButtonStyle`：background、border、borderWidth、cornerRadius、pressedBackground（Button 持有）
- `TextBoxStyle`：background、border、~~borderWidth~~（2026-08-29 删除——见 v1.5）、selection、composition、caretWidth、padding（TextBox 持有）
- `PanelStyle`：background（Panel 持有）

## 4. 类接口设计

### 4.1 Theme 抽象基类

```cpp
// include/ECDI/Theme/Theme.h
#pragma once
#include "ECDI/Theme/TextStyle.h"
#include "ECDI/Theme/ButtonStyle.h"
#include "ECDI/Theme/TextBoxStyle.h"
#include "ECDI/Theme/PanelStyle.h"

namespace ECDI{

/// @brief 主题抽象基类（默认视觉规范提供者——Style 是唯一视觉属性来源）
class Theme{
public:
    virtual ~Theme() = default;

    virtual TextStyle    GetTextStyle() const = 0;    ///< 文本控件共享样式
    virtual ButtonStyle  GetButtonStyle() const = 0;
    virtual TextBoxStyle GetTextBoxStyle() const = 0;
    virtual PanelStyle   GetPanelStyle() const = 0;
    // CheckBoxStyle/RadioStyle：Phase 6.2 控件实现时新增
};

}
```

**返回值语义**（锁死）：**返回值**（非引用）——`virtual ButtonStyle GetButtonStyle() const = 0;`
- Theme 是"默认视觉规范提供者"，Style 是轻量值对象
- 返回值语义让 Theme 实现自由组织内部数据（静态局部、成员、计算生成）
- 不暴露 Theme 内部存储

### 4.2 DefaultTheme

```cpp
// include/ECDI/Theme/DefaultTheme.h
#pragma once
#include "ECDI/Theme/Theme.h"

namespace ECDI{

class DefaultTheme : public Theme{
public:
    DefaultTheme();

    TextStyle    GetTextStyle() const override;
    ButtonStyle  GetButtonStyle() const override;
    TextBoxStyle GetTextBoxStyle() const override;
    PanelStyle   GetPanelStyle() const override;
};

/// @brief 获取当前默认主题实例（static local = 首次调用构造，非程序启动）
/// @details 不引入 ThemeManager 全局状态；未来可改为参数传入或 Window 持有
const DefaultTheme& GetDefaultTheme();

}
```

### 4.3 DefaultTheme 默认值（复刻当前硬编码）

```cpp
// src/ECDI/Theme/DefaultTheme.cpp
#include "ECDI/Theme/DefaultTheme.h"

namespace ECDI{

DefaultTheme::DefaultTheme(){}

TextStyle DefaultTheme::GetTextStyle() const{
    TextStyle s;
    s.foreground.value = Color::Black();   // Label/TextBox 默认黑字（当前 TextWidget 默认 Color() = 黑）
    s.font.value        = Font{};           // 默认字体（14.0f + 系统默认——Core/Font.h 默认值）
    return s;
}

ButtonStyle DefaultTheme::GetButtonStyle() const{
    ButtonStyle s;
    s.background.value         = Color::FromRGBA8(80, 120, 220);   // 当前 Button 正常态蓝
    s.border.value             = Color::White();                     // 当前 Button 焦点白框
    s.borderWidth.value        = 2.0f;                               // 当前 Button 内缩 2px
    s.cornerRadius.value       = 0.0f;                               // 当前 Button 无圆角
    s.pressedBackground.value  = Color::FromRGBA8(60, 90, 180);     // 当前 Button 按下深蓝
    return s;
}

TextBoxStyle DefaultTheme::GetTextBoxStyle() const{
    TextBoxStyle s;
    s.background.value  = Color::White();                       // 当前 TextBox 白底
    s.border.value      = Color::FromRGBA8(80, 120, 220);      // 当前 TextBox 焦点蓝框
    s.borderWidth.value = 2.0f;                                 // 当前 TextBox 内缩 2px
    s.selection.value   = Color::FromRGBA8(173, 216, 230);     // 当前 TextBox 选区浅蓝
    s.composition.value = Color::FromRGBA8(80, 120, 220);      // 当前 TextBox 组合串下划线蓝
    s.caretWidth.value  = 2.0f;                                 // 当前 TextBox 光标宽 2px
    s.padding.value     = 2.0f;                                 // 当前 TextBox 焦点内缩 2px
    return s;
}

PanelStyle DefaultTheme::GetPanelStyle() const{
    PanelStyle s;
    // v1.7（2026-08-30，phase9.6-panel-container-semantics v1.1）：默认透明——镶板 = 隐形布局容器，
    // 需要底色的实例经 SetStyle 显式设色（原默认 Color::Gray()）
    s.background.value = Color::FromRGBA8(0, 0, 0, 0);
    return s;
}

const DefaultTheme& GetDefaultTheme(){
    static DefaultTheme instance;   // static local = 首次调用构造，非程序启动
    return instance;
}

}
```

**注**：Theme 内的 StyleField `.overridden` 恒为 false（Theme 是默认值来源，不产生 Override）——接受此冗余（结构简单可工作，不值得为消除它重新设计——YAGNI）。

## 5. 控件迁移实现

### 5.1 TextWidget（TextStyle 持有者）

```cpp
// include/ECDI/Widget/TextWidget.h 改动
class TextWidget : public Widget{
public:
    // ... 现有构造函数不变 ...

    /// @brief 应用主题（同步默认值到 m_style，只更新未 Override 属性——D7）
    /// @details virtual——Button/TextBox override 扩展（先调基类注入 TextStyle，再注入专属 Style）
    virtual void ApplyTheme(const Theme& theme);

    /// @brief 运行时文本样式覆盖
    void SetStyle(TextStyleOverride override);

    /// @brief 设置文本颜色（旧 API 保留——内部转发 SetStyle，单一状态来源）
    void SetTextColor(const Color& color);

    /// @brief 设置字体（旧 API 保留——内部转发 SetStyle）
    void SetFont(const Font& font);

protected:
    TextStyle m_style;   ///< 文本样式（foreground + font）——所有文本控件的文字视觉唯一来源

private:
    // 删除 m_textColor（迁移到 m_style.foreground）
    // 删除 m_font（迁移到 m_style.font）
};
```

```cpp
// src/ECDI/Widget/TextWidget.cpp 改动
TextWidget::TextWidget(const std::string& text) : m_text(text){
    ApplyTheme(GetDefaultTheme());   // 从主题注入默认样式（注：虚函数在基类构造期静态派发到 TextWidget::ApplyTheme——Button/TextBox 派生类构造函数需再次调用）
}

void TextWidget::ApplyTheme(const Theme& theme){
    TextStyle defaults = theme.GetTextStyle();   // 消费 GetTextStyle（非 GetLabelStyle——v1.1 修正）
    m_style.foreground.Apply(defaults.foreground.value);
    m_style.font.Apply(defaults.font.value);
    Invalidate();
}

void TextWidget::SetStyle(TextStyleOverride override){
    if (override.foreground) m_style.foreground.Set(*override.foreground);
    if (override.font)        m_style.font.Set(*override.font);
    Invalidate();
}

void TextWidget::SetTextColor(const Color& color){
    // 旧 API 转发（单一状态来源——经 SetStyle 产生 Override 标记）
    TextStyleOverride style;
    style.foreground = color;
    SetStyle(style);
}

void TextWidget::SetFont(const Font& font){
    TextStyleOverride style;
    style.font = font;
    SetStyle(style);
}

void TextWidget::DrawTextContent(PaintContext& ctx, int x, int y){
    if (m_text.empty()) return;
    const Size textSize = ctx.MeasureText(m_style.font.value, m_text);
    ctx.DrawText(
        CalculateTextPosition(x, y, textSize.width, textSize.height),
        m_text, m_style.foreground.value, m_style.font.value
    );
}
```

### 5.2 Label（零 Style——纯继承 TextWidget）

```cpp
// src/ECDI/Widget/Label.cpp 改动
Label::Label(const std::string& text) : TextWidget(text){
    // 不重复 ApplyTheme——TextWidget 构造已完成 TextStyle 注入（v1.1 修正：Label 无专属 Style）
}

void Label::OnPaint(PaintContext& ctx, int x, int y){
    DrawTextContent(ctx, x, y);   // 透明背景——只画文本（TextStyle）
}
```

### 5.3 Button（TextStyle + ButtonStyle）

```cpp
// include/ECDI/Widget/Button.h 改动
class Button : public TextWidget{
public:
    // ... 现有构造函数不变 ...

    using TextWidget::SetStyle;   // 🔴 v1.2: 防名字隐藏——保留基类 SetStyle(TextStyleOverride)，Button 可分别设置 TextStyle + ButtonStyle

    void ApplyTheme(const Theme& theme) override;   // 扩展：TextStyle + ButtonStyle
    void SetStyle(ButtonStyleOverride override);    // Button 专属覆盖（文字样式经 TextWidget::SetStyle）

protected:
    ButtonStyle m_style;   ///< Button 专属样式（protected——测试派生类 TestableButton 可访问；不含 foreground——TextStyle 是文字唯一来源）

private:
    bool m_pressed = false;
};
```

```cpp
// src/ECDI/Widget/Button.cpp 改动
Button::Button(const std::string& text) : TextWidget(text){
    // TextWidget 构造已注入 TextStyle；Button 再注入 ButtonStyle
    // （基类构造期虚函数静态派发——必须在此重新调用以覆盖 Button::ApplyTheme）
    ApplyTheme(GetDefaultTheme());
}

void Button::ApplyTheme(const Theme& theme){
    TextWidget::ApplyTheme(theme);   // ① 先注入 TextStyle（foreground/font）
    ButtonStyle defaults = theme.GetButtonStyle();   // ② 再注入 ButtonStyle
    m_style.background.Apply(defaults.background.value);
    m_style.border.Apply(defaults.border.value);
    m_style.borderWidth.Apply(defaults.borderWidth.value);
    m_style.cornerRadius.Apply(defaults.cornerRadius.value);
    m_style.pressedBackground.Apply(defaults.pressedBackground.value);
    Invalidate();
}

void Button::SetStyle(ButtonStyleOverride override){
    if (override.background)         m_style.background.Set(*override.background);
    if (override.border)             m_style.border.Set(*override.border);
    if (override.borderWidth)        m_style.borderWidth.Set(*override.borderWidth);
    if (override.cornerRadius)       m_style.cornerRadius.Set(*override.cornerRadius);
    if (override.pressedBackground)  m_style.pressedBackground.Set(*override.pressedBackground);
    Invalidate();
}

void Button::OnPaint(PaintContext& ctx, int x, int y){
    const Color background = m_pressed
        ? m_style.pressedBackground.value
        : m_style.background.value;

    // 🔴 v1.2: cornerRadius 真正消费——Phase 8 DrawRoundedRect 能力在此接入
    // （不允许 StyleField 存在但不被 Renderer 消费——"存了但没用"的字段会误导 SetStyle 用户）
    const float radius = m_style.cornerRadius.value;

    // 🔴 v1.4: 内框尺寸几何防御——borderWidth 是用户可改值（SetStyle），
    // 尺寸 - 2×borderWidth 可能为负（如 width=3, borderWidth=2 → -1 无效 Rect）。
    // 不做复杂 Clamp 工具（YAGNI），局部 max 保护即可。
    const float borderWidth = m_style.borderWidth.value;
    const float innerWidth  = (std::max)(0.0f, (float)GetWidth() - 2.0f * borderWidth);
    const float innerHeight = (std::max)(0.0f, (float)GetHeight() - 2.0f * borderWidth);
    const float innerRadius = (std::max)(0.0f, radius - borderWidth);   // 内框圆角随内缩缩小

    if (HasFocus()){
        // 焦点边框：外框 border 色（圆角随 radius），内缩 borderWidth
        if (radius > 0.0f){
            ctx.DrawRoundedRect(Rect{ (float)x, (float)y, (float)GetWidth(), (float)GetHeight() },
                                radius, m_style.border.value);
            ctx.DrawRoundedRect(
                Rect{ (float)x + borderWidth, (float)y + borderWidth, innerWidth, innerHeight },
                innerRadius,
                background);
        } else {
            ctx.DrawRect(Rect{ (float)x, (float)y, (float)GetWidth(), (float)GetHeight() },
                         m_style.border.value);
            ctx.DrawRect(Rect{ (float)x + borderWidth, (float)y + borderWidth, innerWidth, innerHeight },
                         background);
        }
    } else {
        if (radius > 0.0f){
            ctx.DrawRoundedRect(Rect{ (float)x, (float)y, (float)GetWidth(), (float)GetHeight() },
                                radius, background);
        } else {
            ctx.DrawRect(Rect{ (float)x, (float)y, (float)GetWidth(), (float)GetHeight() },
                         background);
        }
    }

    DrawTextContent(ctx, x, y);   // 文字颜色来自 TextStyle（TextWidget::m_style.foreground）
}
```

### 5.4 TextBox（TextStyle + TextBoxStyle）

```cpp
// include/ECDI/Widget/TextBox.h 改动
class TextBox : public TextWidget{
public:
    // ... 现有构造函数不变 ...

    using TextWidget::SetStyle;   // 🔴 v1.2: 防名字隐藏——保留基类 SetStyle(TextStyleOverride)

    void ApplyTheme(const Theme& theme) override;
    void SetStyle(TextBoxStyleOverride override);

protected:
    TextBoxStyle m_style;   ///< TextBox 专属样式（protected——测试派生类可访问；不含 foreground——TextStyle 是文字唯一来源）
    // ... 现有成员不变 ...
};
```

```cpp
// src/ECDI/Widget/TextBox.cpp 改动（关键部分）
TextBox::TextBox(const std::string& text) : TextWidget(text){
    ApplyTheme(GetDefaultTheme());   // 注入 TextBoxStyle（TextStyle 已由 TextWidget 构造注入）
}

void TextBox::ApplyTheme(const Theme& theme){
    TextWidget::ApplyTheme(theme);   // ① TextStyle
    TextBoxStyle defaults = theme.GetTextBoxStyle();   // ② TextBoxStyle
    m_style.background.Apply(defaults.background.value);
    m_style.border.Apply(defaults.border.value);
    m_style.borderWidth.Apply(defaults.borderWidth.value);
    m_style.selection.Apply(defaults.selection.value);
    m_style.composition.Apply(defaults.composition.value);
    m_style.caretWidth.Apply(defaults.caretWidth.value);
    m_style.padding.Apply(defaults.padding.value);
    Invalidate();
}

void TextBox::SetStyle(TextBoxStyleOverride override){
    if (override.background)    m_style.background.Set(*override.background);
    if (override.border)        m_style.border.Set(*override.border);
    if (override.borderWidth)   m_style.borderWidth.Set(*override.borderWidth);
    if (override.selection)     m_style.selection.Set(*override.selection);
    if (override.composition)   m_style.composition.Set(*override.composition);
    if (override.caretWidth)    m_style.caretWidth.Set(*override.caretWidth);
    if (override.padding)       m_style.padding.Set(*override.padding);
    Invalidate();
}

// OnPaint 中替换硬编码：
// - 焦点框内缩 2.0f        → m_style.padding.value
// - kSelectionColor        → m_style.selection.value
// - kCompositionUnderline  → m_style.composition.value
// - kCaretWidth            → m_style.caretWidth.value
// - 焦点框 Color::FromRGBA8(80,120,220) → m_style.border.value
// - 背景 Color::White()    → m_style.background.value
// - 文字 m_textColor       → m_style.foreground.value（TextWidget TextStyle）
```

### 5.5 Panel

```cpp
// src/ECDI/Widget/Panel.cpp 改动（v1.7 现状，2026-08-30）
Panel::Panel(){
    ApplyTheme(GetDefaultTheme());
}

void Panel::ApplyTheme(const Theme& theme){
    PanelStyle defaults = theme.GetPanelStyle();
    m_style.background.Apply(defaults.background.value);
    Invalidate();
}

void Panel::SetStyle(PanelStyleOverride override){
    // v1.6（2026-08-29）：单实例覆盖——仅覆盖传入字段，未传字段保持原值/D7 主题值
    if (override.background) m_style.background.Set(*override.background);
    Invalidate();
}

// v1.7（2026-08-30，phase9.6-panel-container-semantics v1.1）：输入透传——
// 镶板 = 纯容器，自身永不参与命中；鼠标事件只由子控件接收（HitTest 子优先递归）
bool Panel::ContainsPoint(int x, int y) const noexcept{
    (void)x; (void)y;
    return false;
}

void Panel::OnPaint(PaintContext& ctx, int x, int y){
    // v1.7：默认透明（a==0）时短路跳过 DrawRect——性能优化；
    // PushClip/PopClip 仍由 Widget::Paint 基类管线发出（命令流 size 2）
    if (m_style.background.value.a > 0.0f){
        ctx.DrawRect(Rect{ (float)x, (float)y, (float)GetWidth(), (float)GetHeight() },
                     m_style.background.value);
    }
}
```

## 6. 单元测试（T-F01-T-F10）

```cpp
// src/Tests/ThemeTests.cpp
#include "RunAllTests.h"
#include "TestFramework.h"
#include "ECDI/Theme/StyleField.h"
#include "ECDI/Theme/DefaultTheme.h"
#include "ECDI/Widget/Button.h"

namespace{

// T-F01: StyleField 初始值
void TestStyleFieldInitial(){
    StyleField<Color> field;
    EXPECT_EQ(field.value, Color{});
    EXPECT_FALSE(field.overridden);
}

// T-F02: Set() 修改 value + overridden=true
void TestStyleFieldSet(){
    StyleField<Color> field;
    field.Set(Color::Red());
    EXPECT_EQ(field.value, Color::Red());
    EXPECT_TRUE(field.overridden);
}

// T-F03: Apply() 未 override 时更新
void TestStyleFieldApplyNotOverridden(){
    StyleField<Color> field;
    field.Apply(Color::Blue());
    EXPECT_EQ(field.value, Color::Blue());
    EXPECT_FALSE(field.overridden);   // Apply 不改变标志位
}

// T-F04: Apply() 已 override 时不更新（D7 核心）
void TestStyleFieldApplyOverridden(){
    StyleField<Color> field;
    field.Set(Color::Red());
    field.Apply(Color::Blue());
    EXPECT_EQ(field.value, Color::Red());   // 保持 Red
    EXPECT_TRUE(field.overridden);
}

// T-F05: 多次 Apply 幂等
void TestStyleFieldApplyIdempotent(){
    StyleField<Color> field;
    field.Apply(Color::Green());
    field.Apply(Color::Green());
    field.Apply(Color::Green());
    EXPECT_EQ(field.value, Color::Green());
}

// T-F06: DefaultTheme 所有字段默认值（完整复刻当前视觉——迁移前后一致）
void TestDefaultThemeValues(){
    const DefaultTheme& theme = GetDefaultTheme();

    // TextStyle
    auto ts = theme.GetTextStyle();
    EXPECT_EQ(ts.foreground.value, Color::Black());
    EXPECT_EQ(ts.font.value.size, 14.0f);

    // ButtonStyle（全部字段）
    auto btn = theme.GetButtonStyle();
    EXPECT_EQ(btn.background.value, Color::FromRGBA8(80, 120, 220));
    EXPECT_EQ(btn.border.value, Color::White());
    EXPECT_EQ(btn.borderWidth.value, 2.0f);
    EXPECT_EQ(btn.cornerRadius.value, 0.0f);
    EXPECT_EQ(btn.pressedBackground.value, Color::FromRGBA8(60, 90, 180));

    // TextBoxStyle（全部字段）
    auto tb = theme.GetTextBoxStyle();
    EXPECT_EQ(tb.background.value, Color::White());
    EXPECT_EQ(tb.border.value, Color::FromRGBA8(80, 120, 220));
    EXPECT_EQ(tb.borderWidth.value, 2.0f);
    EXPECT_EQ(tb.selection.value, Color::FromRGBA8(173, 216, 230));
    EXPECT_EQ(tb.composition.value, Color::FromRGBA8(80, 120, 220));
    EXPECT_EQ(tb.caretWidth.value, 2.0f);
    EXPECT_EQ(tb.padding.value, 2.0f);

    // PanelStyle
    auto panel = theme.GetPanelStyle();
    EXPECT_EQ(panel.background.value, Color::Gray());
}

// T-F07: Button 构造后样式已注入（真实集成测试——TestableButton 暴露 m_style）
class TestableButton : public Button{
public:
    using Button::Button;
    const ButtonStyle& GetButtonStyleForTest() const { return m_style; }
};

void TestButtonApplyTheme(){
    TestableButton button("Test");
    const auto& style = button.GetButtonStyleForTest();
    EXPECT_EQ(style.background.value, Color::FromRGBA8(80, 120, 220));
    EXPECT_EQ(style.border.value, Color::White());
    EXPECT_EQ(style.borderWidth.value, 2.0f);
    EXPECT_EQ(style.pressedBackground.value, Color::FromRGBA8(60, 90, 180));
}

// T-F08: SetStyle 后 ApplyTheme 不覆盖（D7 契约）
void TestSetStyleThenApplyTheme(){
    TestableButton button("Test");
    ButtonStyleOverride override;
    override.background = Color::Red();
    button.SetStyle(override);              // 用户覆盖 background = Red

    button.ApplyTheme(GetDefaultTheme());   // 主题重新应用

    const auto& style = button.GetButtonStyleForTest();
    EXPECT_EQ(style.background.value, Color::Red());   // 保持用户值（D7）
    EXPECT_EQ(style.border.value, Color::White());     // 未覆盖的属性恢复主题默认值
}

// T-F09: DefaultTheme 自身一致性
void TestDefaultThemeConsistency(){
    const DefaultTheme& theme = GetDefaultTheme();
    auto btn1 = theme.GetButtonStyle();
    auto btn2 = theme.GetButtonStyle();
    EXPECT_EQ(btn1.background.value, btn2.background.value);
    EXPECT_EQ(btn1.pressedBackground.value, btn2.pressedBackground.value);
}

// T-F10: TextStyle 与 ButtonStyle 独立覆盖共存（v1.2——验证 using SetStyle + D7）
//        场景：TextStyle.foreground=Red + ButtonStyle.background=Blue → ApplyTheme
//        → 两处 Override 都保留，未覆盖字段回主题默认
void TestButtonTextAndButtonStyleIndependent(){
    TestableButton button("Test");

    TextStyleOverride textOverride;
    textOverride.foreground = Color::Red();
    button.SetStyle(textOverride);          // 经 using 暴露的基类 SetStyle

    ButtonStyleOverride buttonOverride;
    buttonOverride.background = Color::Blue();
    button.SetStyle(buttonOverride);        // Button 专属 SetStyle

    button.ApplyTheme(GetDefaultTheme());   // 主题重新应用——不破坏任何 Override

    // ButtonStyle: background Override 保留
    EXPECT_EQ(button.GetButtonStyleForTest().background.value, Color::Blue());
    // ButtonStyle: 未覆盖的 border 回主题默认
    EXPECT_EQ(button.GetButtonStyleForTest().border.value, Color::White());
    // TextStyle: foreground Override 保留（需暴露 m_style——TestableButton 补 GetTextStyleForTest 或经 GetTextColor）
    EXPECT_EQ(button.GetTextColor(), Color::Red());
}

void RegisterThemeTests(){
    GetTestRegistry().Add("Theme.StyleFieldInitial",        &TestStyleFieldInitial);
    GetTestRegistry().Add("Theme.StyleFieldSet",            &TestStyleFieldSet);
    GetTestRegistry().Add("Theme.StyleFieldApply",          &TestStyleFieldApplyNotOverridden);
    GetTestRegistry().Add("Theme.StyleFieldApplyNoOverride", &TestStyleFieldApplyOverridden);
    GetTestRegistry().Add("Theme.StyleFieldApplyIdempotent", &TestStyleFieldApplyIdempotent);
    GetTestRegistry().Add("Theme.DefaultThemeValues",       &TestDefaultThemeValues);
    GetTestRegistry().Add("Theme.ButtonApplyTheme",         &TestButtonApplyTheme);
    GetTestRegistry().Add("Theme.SetStyleThenApply",        &TestSetStyleThenApplyTheme);
    GetTestRegistry().Add("Theme.DefaultThemeConsistent",   &TestDefaultThemeConsistency);
    GetTestRegistry().Add("Theme.ButtonTextAndButtonStyle", &TestButtonTextAndButtonStyleIndependent);   // v1.2
}

}
```

**注意**：ThemeTests 的注册方式需与现有 7.2 测试体系对齐（看 RunAllTests.cpp 的既有模式——可能是集中注册而非自动注册结构体）。

## 7. 实施顺序

```
Phase 9.1 主题基础设施
  ① StyleField.h
  ② TextStyle.h / ButtonStyle.h / TextBoxStyle.h / PanelStyle.h
  ③ Theme.h（含 GetTextStyle）
  ④ DefaultTheme.h/.cpp
  ⑤ ThemeTests.cpp（T-F01-T-F10）
  → 编译 + 运行 ThemeTests 全绿

Phase 9.2 控件样式迁移
  ⑥ TextWidget.h/.cpp（加 TextStyle + ApplyTheme + SetStyle；删 m_textColor/m_font 独立成员）
  ⑦ Label.cpp（构造函数不重复 ApplyTheme——零改动确认）
  ⑧ Button.h/.cpp（加 ButtonStyle + ApplyTheme + SetStyle；删硬编码颜色）
  ⑨ TextBox.h/.cpp（加 TextBoxStyle + ApplyTheme + SetStyle；硬编码常量改 Style 引用）
  ⑩ Panel.h/.cpp（加 PanelStyle + ApplyTheme）
  → 编译 + 视觉验证（迁移前后一致）

Phase 9.3 透明主题值验证（可选——v1.2 改名：Alpha 能力属 Phase 8，Phase 9 只消费主题值）
  ⑪ 如 DefaultTheme 引入带 Alpha 的颜色（如半透明背景），视觉验证透明效果
  → 验证：半透明主题值在控件上正确呈现（能力已备，仅为消费确认）
```

## 8. 与既有约束的对齐

| 约束 | 对齐方式 |
|---|---|
| skill 15 分层 | Theme 是决策层（零 Win32 类型）；消费在 Widget/Renderer 层 |
| skill 19 能力/决策正交 | Backend 提供能力（Phase 8）/ Theme 提供决策（Phase 9）——换主题后端不动 |
| skill 21 YAGNI | 无 ThemeManager / StyleSheet / CSS / 动画 / 层级继承；CheckBox/Radio 移出（Phase 6.2 纳入） |
| skill 22 分层论证 | 契约语言描述（"ApplyTheme = 同步主题默认值到 Widget Style，只更新未 Override 属性"） |
| 资源类禁复制禁移动 | StyleField 值语义（可复制）/ DefaultTheme 值语义 |
| 测试由用户做 | 视觉验证由用户编译运行确认；**D7 StyleField 纯逻辑由 ThemeTests 自动覆盖** |
| 五阶段法 | 本文档 = 详细设计；确认后进入实现 |
| 文档约定 | 更新 `docs/phase9-theme-system-detailed-design.md` v1.0 → v1.1 |

## 9. GPT 评审回应（v1.1）

| # | GPT 反馈 | 处理 |
|---|---|---|
| 🔴 1 | TextStyle 没进 Theme 接口；TextWidget 消费 GetLabelStyle() 类型不匹配 | **新增 `Theme::GetTextStyle()`** + `TextStyle.h`；TextWidget 改消费 `GetTextStyle()`；**删除 LabelStyle**（Label 无独立视觉属性） |
| 🔴 2 | ButtonStyle/TextBoxStyle 的 foreground 与 TextStyle.foreground 双重真相——SetStyle 后文字不变色 | **删除 ButtonStyle.foreground / TextBoxStyle.foreground**——文字颜色统一来自 TextStyle（TextWidget 持有）；单一视觉真相原则锁死 |
| 🔴 3 | CheckBox/Radio 在职责确认有但详细设计消失 | **明确移出 Phase 9**（控件本身尚未实现——Phase 6.2 落地时直接带 CheckBoxStyle/RadioStyle） |
| 🟠 4 | Label 重复调 ApplyTheme | **Label 构造函数不重复调用**（TextWidget 构造已完成；Label 无专属 Style） |
| 🟠 5 | TextStyle.h 漏出文件清单 | **加入 §2 文件清单**（#2）+ vcxproj + 实施顺序 |
| 🟡 6 | T-F07 是占位假测试 | **改为真实 TestableButton 集成测试**（暴露 m_style 验证构造后注入） |
| 观察 | Theme 内 StyleField.overridden 恒 false 冗余 | 接受（结构简单可工作——YAGNI，注明 §4.3） |
| 观察 | PanelStyle 无 Override | 注明"MVP 主动限制，非 StyleField 不支持"（§3.3） |
| 观察 | StyleField::Reset() 语义 | 锁死："不恢复 Theme 值，仅清除标志"（§3.1） |
| 观察 | SetTextColor 保留兼容 | **旧 API 保留 + 内部转发 SetStyle**（单一状态来源，§5.1） |
| 观察 | T-F06 字段不全 | **补全**：Button 加 cornerRadius；TextBox 加 border/borderWidth/composition |

## 10. 修订记录

- v1.8（2026-08-31）**ProgressBar 主题接入**（详设：phase9.6-progressbar-detailed-design.md v1.1）：`Theme` 抽象基类新增 `virtual ProgressBarStyle GetProgressBarStyle() const = 0;`（+`#include ProgressBarStyle.h`；全仓唯一派生类 `DefaultTheme` 同步实现，闭环）；`DefaultTheme::GetProgressBarStyle()` 返回默认值——trackColor `(220,220,230)` 浅灰轨道 / fillColor `(80,120,220)` 主题蓝填充（同 Button/TextBox 焦点色协调）/ cornerRadius `0.0f`（= 自动圆角 height/2，详设 §2.5 冻结语义）；`ProgressBarStyle{ trackColor, fillColor, cornerRadius }` + `ProgressBarStyleOverride{ optional }` 定义于新增 `ProgressBarStyle.h`（cornerRadius 注释明确"0 = 自动圆角非真实 0 圆角"）。无既有控件测试波及。
- v1.7（2026-08-30）**Panel 容器语义变更落地**（详设：phase9.6-panel-container-semantics-detailed-design.md v1.1）：**Panel 默认背景透明**（`DefaultTheme::GetPanelStyle` 灰底 → `Color::FromRGBA8(0,0,0,0)`——镶板 = 隐形布局容器，需底色经 `SetStyle` 显式设色）；**Panel 输入透传**（`Panel::ContainsPoint` override 恒 false——镶板自身永不参与命中，鼠标事件只由子控件接收；HitTest 子优先递归天然支持；固定语义无开关——YAGNI）；**OnPaint alpha 短路**（`a==0` 跳过 DrawRect——性能优化，命令流 PushClip→PopClip size 2）；测试更新（`Widget.PanelPaint` 默认透明断言 + 设色场景恢复 / `Widget.PanelSetStyle` 覆盖前断言改透明 / 新增 `Widget.PanelInputPassThrough`）。触发 = demo 美化前置需求（Panel 默认透明 + 默认跳过鼠标 + CollapsiblePanel 默认收起）。
- v1.6（2026-08-29）**Panel 单实例设色落地**：新增 `PanelStyleOverride{ std::optional<Color> background }`（`PanelStyle.h`，补 `<optional>`）；`Panel` 新增 `public: void SetStyle(PanelStyleOverride)`（`Panel.h` 声明 + `Panel.cpp` 实现——`if (override.background) m_style.background.Set(...); Invalidate();`，与 Button/TextBox 同构）；`PanelStyle.h` / `Panel.h` / `Panel.cpp` 注释同步（删除「MVP 无 Override」旧陈述）；新增测试 `Widget.PanelSetStyle`（`WidgetTests.cpp`：覆盖前灰底 → 覆盖后自定义色生效 → 覆盖后 `ApplyTheme` 不再回退，验证 D7 overridden 契约）。触发 = 用户需求「镶板能否设色」——此前归 Phase 9 YAGNI，需求出现即按既有模式补（非过度设计：Panel 唯一样式字段即 background）。
- v1.5（2026-08-29）9.6 收尾回写（**TextBox 焦点内缩 bug 修复 · 方案 B**）：**删除 `TextBoxStyle.borderWidth`**（旧语义 = 焦点内缩量，方案 B 改用 `DrawFocusRect` 后无任何消费者——遵守「StyleField 存了但没用会误导 SetStyle 用户」纪律；同步删除 `TextBoxStyleOverride.borderWidth` / `TextBox::ApplyTheme`·`SetStyle` 赋值 / `DefaultTheme` 默认值 / `ThemeTests` 断言，全仓 grep 确认无其他引用）；**`TextBoxStyle.padding` 语义变更**（焦点专用内缩 → 常驻「样式内边距」，默认 2.0→**0.0**；连带 `GetTextLeftInset/GetTextAreaWidth/GetTextAreaHeight` 去掉 `HasFocus()` 判断——修复焦点切换时文本位移 2px、滚动上限跳变、同一像素点击定位随焦点变化三个问题）；**TextBox 焦点视觉改 `DrawFocusRect` 点线框**（与 Button 同构，直角——TextBoxStyle 无 cornerRadius 字段），并纳入 9.6 新增的 `Widget::SetShowFocusRect/ShowFocusRect` 开关门控。
  - 注：本版**仅标注**历史快照正文（§3 字段列表等）的差异，不改写 v1.4 及其之前的设计正文——字段真相以代码为准。
- v1.4（2026-08-25）GPT 评审整合 v1.4（"修掉几何边界问题后即可锁版，不需要再继续设计"）：**🔴 Button 内框尺寸几何防御**（borderWidth 是用户可改值——`尺寸 - 2×borderWidth` 可能为负（width=3/borderWidth=2 → -1 无效 Rect）；局部 `innerWidth/innerHeight/innerRadius = max(0, ...)` 保护，不做复杂 Clamp 工具——YAGNI）；**🟠 §3.4 改名"Style 与 Widget 的组合关系"**（明确 has-a 非 is-a——各 Style 是独立值对象无继承关系，图改为组合持有标注）；**🟠 StyleField::Reset() 补契约注释**（Reset 是 StyleField 层原语，不构成 Widget 层运行时样式 API——不要据此推断应提供 ClearStyle()）。
- v1.3（2026-08-25）GPT 评审整合 v1.3（"修完 3 个明确问题后可锁版进入 Phase 9.1 实现"）：**🔴 Button/TextBox 的 `m_style` 改 protected**（TestableButton 测试派生类可访问——原 private 无法在派生类访问，编译错误）；**🟠 测试编号统一 T-F01-T-F10**（原文档多处仍写 T-F09，实际已 10 个）；**🟠 Theme 头文件 ×6 → ×7**（新增 TextStyle.h 后未更新计数）；**🟢 明确 SetStyle({}) 重载歧义契约**（Button/TextBox 双 Override 重载共存时 `{}` 有歧义——调用方须用明确类型 Override 对象，主动接受不引入额外接口）。
- v1.2（2026-08-25）GPT 评审整合 v1.2（"修完 2 个 🔴 后可进入实现"）：**🔴 Button/TextBox 补 `using TextWidget::SetStyle`**（防 C++ 名字隐藏——否则 TextStyleOverride 无法从派生控件设置；§5.3/§5.4 + §3.2 锁死规则）；**🔴 Button::OnPaint 真正消费 `cornerRadius`**（radius==0 → DrawRect / radius>0 → DrawRoundedRect——Phase 8 能力接入，禁死字段；焦点内外框圆角随内缩对应缩小）；**🟠 删除不存在的 ClearOverrides() 引用**（统一 Reset()，明确 Phase 9 MVP 不提供 Widget 层 Override 清除 API）；**🟠 新增 T-F10**（TextStyle + ButtonStyle 独立覆盖共存——using SetStyle + D7 联动验证）；**🟢 Phase 9.3 改名**（"Alpha 消费"→"透明主题值验证（可选）"——职责归位：Alpha 能力属 Phase 8，Phase 9 只消费主题值）；单一视觉真相上升为总规则（"任何视觉属性只能存在一个权威 StyleField"）。
- v1.1（2026-08-25）GPT 评审整合（3 🔴 + 2 🟠 + 1 🟡 全部落地）：**新增 TextStyle 进 Theme 接口**（GetTextStyle + TextStyle.h；删除 LabelStyle）；**删除 ButtonStyle/TextBoxStyle 的 foreground**（单一视觉真相——文字颜色统一 TextStyle）；**CheckBox/Radio 明确移出**（Phase 6.2 纳入）；Label 不重复 ApplyTheme；TextStyle.h 入文件清单；T-F07 改 TestableButton 真实测试；T-F06 补全字段；SetTextColor/SetFont 保留旧 API 内部转发；StyleField::Reset() 语义锁死；PanelStyle 无 Override 注明主动限制。
- v1.0（2026-08-25）详细设计初稿：文件清单（21 文件）/ StyleField\<T\> + 生命周期契约 / 各控件 Style 结构 + Override 结构 / Theme 抽象基类（返回值语义）/ DefaultTheme 继承 Theme + 默认值复刻 / 控件迁移实现 / 单元测试 T-F01-T-F09 / 实施顺序（9.1/9.2/9.3）。
