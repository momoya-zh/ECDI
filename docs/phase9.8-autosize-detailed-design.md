# Phase 9.8 AutoSize 详细设计（v1.1）

> 阶段：详细设计（五阶段法 ③）
> 日期：2026-09-02（v1.1 修订 2026-09-02）
> 状态：**v1.1 GPT 详设评审通过**（2026-09-02——「通过，允许进入实现阶段」；两处文字决策修订 + DoMeasureText 定 private，见修订记录）
> 前置：phase9.8-autosize-requirements.md v1.5（R5「后调用者赢」定稿）/ phase9.8-autosize-preliminary-design.md v1.1（GPT 初设评审通过）
> 边界一句话：**让控件知道自己需要多大，并允许调用方显式让它调整到这个尺寸**——不是尺寸协商系统

---

## 1. 目标与范围

需求/初设全部冻结语义直接落成实现，本文不重开任何已冻结决策。范围 = Widget/TextWidget/TextBox 三个类的尺寸能力 + FakeTextMeasurer 测试 + ModelProbe statLabel 验收接线。

**参考既有先例**（实现纪律）：`GetLineHeight` 的 const_cast + 无窗口兜底（TextBox.cpp:371/373）、`ResolveAnimationManager` 测试接缝（ProgressBar——不扩散约束）、`SetSize` 虚分派（9.7 契约 10 修订）。

## 2. 硬契约（冻结语义——不重开）

引用锚点（细节在各文档）：R1/R2（GetPreferredSize/TextWidget 测量）、R4（显式 AutoSize 唯一触发）、**R5 v1.5（后调用者赢：SetSize 立即设置 / AutoSize 立即按 preferred / stretch>0 no-op；优先级不约束 API 调用顺序）**、§3.2（padding 冻结：非 TextBox 0 inset）、§3.4（多行挂账）、§3.5（stretch 调用时判断 / fillCrossAxis 布局覆盖 / spacing 无冲突）、§3.6（垂直居中验证项不做 API）、§3.7（AutoSize 纯 geometry operation——不 Arrange 不 Invalidate）。

## 3. 精确签名与实现

### 3.1 `Widget::GetPreferredSize()`（查询——默认当前尺寸）

```cpp
// Widget.h —— public 区，紧跟 SetStretch/GetStretch（9.7/9.8 API 相邻；SetStretch 现于 :92）
/// @brief 控件「希望」的尺寸（内容驱动；未 override = 当前尺寸——零回归；兑现 Size.h 注释）
/// @details 9.8：让控件知道自己需要多大。默认返回当前尺寸；TextWidget override 返回内容测量值。
[[nodiscard]] virtual Size GetPreferredSize() const;
```

```cpp
// Widget.cpp
Size Widget::GetPreferredSize() const{
    return Size{ static_cast<float>(GetWidth()), static_cast<float>(GetHeight()) };
}
```

### 3.2 `Widget::AutoSize()`（动作——实现体冻结，§3.7 纯度由此保证）

```cpp
// Widget.h —— public 区（GetPreferredSize 之后）
/// @brief 按 GetPreferredSize() 调整自身尺寸（显式命令——后调用者赢，需求 R5 v1.5）
/// @return true = 实际调用了 SetSize；false = stretch 互斥 no-op 或尺寸未变化
/// @note §3.7 纯 geometry operation：内部只调 SetSize（虚分派——TextBox override 既有语义）；
///       不 Arrange、不 Invalidate、不挂钩 SetText——布局/重绘由调用方负责
bool AutoSize();
```

```cpp
// Widget.cpp —— 唯一实现（冻结——§3.7 纯度靠实现体保证，测试做行为验证）
bool Widget::AutoSize(){
    if (GetStretch() > 0)
        return false;                       // §3.5 条 1：调用时判断（SetStretch(0) 后重新生效）
    const Size preferred = GetPreferredSize();
    const int w = static_cast<int>(preferred.width);   // float → int 截断（v1.1：向零截断——见下）
    const int h = static_cast<int>(preferred.height);
    if (w == GetWidth() && h == GetHeight())
        return false;                       // 同尺寸 no-op
    SetSize(w, h);                          // 虚分派——TextBox::SetSize 的 EnsureCaretVisible+Invalidate 为 SetSize 既有语义
    return true;
}
```

> **v1.1 float → int 策略冻结（GPT 详设评审建议②）**：`AutoSize()` 将 preferred 的浮点分量经 `static_cast<int>` **向零截断**为 Widget 的整数像素尺寸（40.9 → 40）；**v1 不引入 rounding policy**（不 std::round）——Widget 几何 API 本就是整数尺寸，截断是最小变化方案。Size 保持 float（测量层精度），转换只在 AutoSize 边界发生一次。

### 3.3 `TextWidget`：preferred 测量 + ResolveMeasurer 接缝

```cpp
// TextWidget.h
// public 区（GetText/SetFont 附近）：
/// @brief 内容测量 preferred（override——单行文本宽 + 行高；经 ResolveMeasurer 拿测量器）
[[nodiscard]] Size GetPreferredSize() const override;

// protected 区（m_style 前）：
/// @brief 测量器解析接缝（ProgressBar ResolveAnimationManager 同构——仅服务 preferred，不扩散）
/// @details 正常运行 = Window 的 TextMeasurer；测试派生类 override 返回 FakeTextMeasurer
[[nodiscard]] virtual TextMeasurer* ResolveMeasurer() const;
```

```cpp
// TextWidget.cpp
Size TextWidget::GetPreferredSize() const{
    if (TextMeasurer* measurer = ResolveMeasurer())
        return DoMeasureText(*measurer);                       // 有测量器 → 内容测量
    return Widget::GetPreferredSize();                         // 运行时 fallback：无窗口+无注入 → 当前尺寸
}

TextMeasurer* TextWidget::ResolveMeasurer() const{
    if (Window* window = const_cast<Window*>(GetWindow()))     // const 方法内 const_cast（GetLineHeight 同款先例）
        return &window->GetTextMeasurer();
    return nullptr;
}

// private 成员（v1.1 定案——GPT 详设评审：preferred 测量是 TextWidget 语义组成部分，归 private 非匿名 namespace）
Size TextWidget::DoMeasureText(TextMeasurer& measurer) const{
    const Size textSize = measurer.MeasureText(m_style.font.value, m_text);
    const float lineHeight = measurer.LineHeight(m_style.font.value);
    return Size{ textSize.width, lineHeight };                 // Label/Button 0 inset（§3.2 冻结）
}
```

- **空文本**：`MeasureText("")` 返回 `{0,0}`（TextBox GetLineHeight 注释确认）→ preferred = `{0, lineH}`——宽 0 诚实（无文本不占宽）；调用方对空文本调 AutoSize 得 0 宽可接受
- **测量与 Paint 同构**：同一 `m_style.font.value` + `m_text`（TextWidget.cpp:107 同款输入）——preferred 与绘制不漂移

### 3.4 `TextBox::GetPreferredSize()`（多行拦截 + padding 修正）

```cpp
// TextBox.h —— public 区（SetSingleLine 附近）
/// @brief override：多行 → 当前尺寸（§3.4 挂账——多行高度 v1 手工 SetSize）；单行 → 内容测量 + padding×2
[[nodiscard]] Size GetPreferredSize() const override;
```

```cpp
// TextBox.cpp
Size TextBox::GetPreferredSize() const{
    if (m_text.find('\n') != std::string::npos)
        return Widget::GetPreferredSize();   // 多行——v1 不参与（需求 §3.4；初设 §3.3 确认）
    // 单行：TextWidget 测量（{文本宽, lineH}，经 ResolveMeasurer）→ 修正 padding 后返回
    const Size measured = TextWidget::GetPreferredSize();
    const float pad = m_style.padding.value;
    return Size{ measured.width + pad * 2.0f, measured.height + pad * 2.0f };
}
```

- **§3.6 验证项自动成立**：单行 preferred height = `lineH + padding×2` → AutoSize 后 textY = padding 上下对称 → 视觉居中（无需 VerticalCentered API）
- `TextWidget::GetPreferredSize()` 显式限定调用——不递归回 TextBox override

### 3.5 头文件插入位置汇总（代码事实锚定）

| 文件 | 位置 | 内容 |
|---|---|---|
| Widget.h | public 区 :92 SetStretch 之后 | `GetPreferredSize()` 虚 + `AutoSize()` |
| TextWidget.h | public 区（GetText/SetFont 区）+ protected 区（m_style:67 前） | override + `ResolveMeasurer()` |
| TextBox.h | public 区（SetSingleLine 后） | override |

（Widget.h 访问区：public :39 / private :217 / protected :243——新 API 全在 public :39-:217 区间。）

## 4. FakeTextMeasurer（测试接缝承载）

```cpp
// WidgetTests.cpp 匿名 namespace（与 TestableTextWidget 同区——TextBoxTests 如需经 TestFramework 共享可提公共头，v1 同文件够用）
/// @brief 假测量器：每码点 8.0f 宽、行高 16.0f（确定性——测量断言前提）
class FakeTextMeasurer : public ECDI::TextMeasurer{
public:
    ECDI::Size MeasureText(const ECDI::Font&, const std::string& text) override{
        return ECDI::Size{ static_cast<float>(CountCodepoints(text)) * 8.0f, 16.0f };
    }
    float LineHeight(const ECDI::Font&) override{ return 16.0f; }
private:
    static size_t CountCodepoints(const std::string& s){
        // v1.1 措辞：按 UTF-8 非 continuation byte（0xC0 判法）计数，用于构造确定性的测试宽度；
        // 不承担 UTF-8 合法性验证（非完整解码器——前导字节合法性/续字节数量/overlong/surrogate/范围均不校验）
        size_t count = 0;
        for (size_t i = 0; i < s.size(); ++i)
            if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80)   // 非续字节 = 码点起点
                ++count;
        return count;
    }
};

/// @brief 可测 Label 子类：注入 FakeTextMeasurer（ResolveMeasurer 接缝——测试派生类 override）
class TestableLabel : public ECDI::Label{
public:
    using ECDI::Label::Label;
protected:
    ECDI::TextMeasurer* ResolveMeasurer() const override{ return &ms_fake; }
private:
    static FakeTextMeasurer ms_fake;
};
```

- 确定性数据：`"Hello"` = 5 码点 → 宽 40.0f；行高恒 16.0f
- TextBox 单行/多行用例经 TestableTextBox + 同 fake（TextBoxTests 内自带 fake 或共享——v1 各文件一份最小 fake，不抽公共（复用需求出现再抽））

## 5. 测试用例（精确数据——初设 §7 落定）

| # | 用例 | 构造 | 断言 |
|---|---|---|---|
| 1 | `PreferredSize.Default` | 裸 Widget + SetSize(100,30) | `GetPreferredSize() == {100,30}` |
| 2 | `Label.PreferredMeasured` | TestableLabel("Hello") + SetSize(999,999) | preferred == `{40.0f, 16.0f}`（0 inset——显式 SetSize 后仍返回内容测量值） |
| 3 | `Label.AutoSizeResizes` | TestableLabel("Hello") + SetSize(999,999) | `AutoSize()==true`；GetWidth/Height == 40/16 |
| 4 | `AutoSize.StretchMutex` | TestableLabel("Hello") + SetStretch(1) | AutoSize()==false 尺寸不变；`SetStretch(0)` 后 AutoSize()==true |
| 5 | `AutoSize.SameSizeNoOp` | TestableLabel("Hello") AutoSize 两次 | 第二次 == false（尺寸已 == preferred） |
| 6 | `TextBox.MultilineNotParticipating` | TestableTextBox("ab\ncd") + SetSize(100,30) | preferred == `{100,30}`（当前尺寸） |
| 7 | `TextBox.SingleLinePaddingPreferred` | TestableTextBox("Hi") + SetStyle padding=4 + SetSize(999,999) | preferred == `{8+8+8=24? — 2码点×8=16 + pad4×2=8 → {24, 16+8=24}}`——`{24.0f, 24.0f}`（§3.6 验证项：height == lineH + padding×2 ✓） |
| 8 | `AutoSize.PureGeometry` | TestableLabel("Hello") AutoSize | 尺寸正确变化 + 无崩溃（§3.7：Invalidate/Arrange 非虚且无窗口 no-op——**纯度由 3.2 实现体冻结保证（代码审查性），本用例做行为层验证**；这是初设开放点 5 的回答） |
| 9 | `AutoSize.LastCallWins` | TestableLabel("Hello")：SetSize(500,100) → AutoSize | 尺寸 == `{40,16}`（后调用者赢——AutoSize 覆盖 SetSize；需求 R5 v1.5） |
| 10 | `ModelProbe.StatLabelFlow` | ModelProbePage + fake（既有 MakePage）：RefreshStatText 长/短消息 | statLabel GetWidth 随文本变（40 → 更长）且兄弟控件位置随动（statRow Arrange 后 searchBox x 变化） |

（用例 7 复核：「Hi」2 码点 × 8 = 16；padding=4 → 宽 16+8=24；高 16+8=24 ✓ 数据自洽）

## 6. ModelProbe 消费（RefreshStatText——验收接线）

```cpp
// ModelProbe.h private 区：
void RefreshStatText(const std::string& message);   ///< 状态消息统一出口：SetText → AutoSize → statRow Arrange → Invalidate
Panel* m_statRow = nullptr;                          ///< 状态行容器（构造函数抓取——statRow 现在无成员指针，需补）

// ModelProbe.cpp：
void ModelProbePage::RefreshStatText(const std::string& message){
    m_statLabel->SetText(message);       // ① 更新文本
    m_statLabel->AutoSize();             // ② 宽度按内容（后调用者赢——§4）
    m_statRow->Arrange();                // ③ statRow 重排——statLabel 变宽后 searchBox/allBtn/noneBtn 随动（H 布局累加）
    Invalidate();                        // ④ 请求重绘
}
```

- 调用点：原三处 `m_statLabel->SetText(...)`（:489/:496/:511/:520 错误消息）+ `UpdateStat()`（:618 统计文本）统一改调 `RefreshStatText(...)`
- 构造函数 statRow 处补 `m_statRow = statRow.get();`（AddChild 前抓裸指针——树地址稳定先例）

## 7. 影响清单

| 区 | 文件 | 改动 |
|---|---|---|
| Widget | `Widget.h/.cpp` | + GetPreferredSize 虚 + AutoSize（4 行冻结实现） |
| TextWidget | `TextWidget.h/.cpp` | + override + ResolveMeasurer + DoMeasureText helper |
| TextBox | `TextBox.h/.cpp` | + override（多行拦截 + padding） |
| Label / Button | 零改动 | 继承 TextWidget |
| Layout | **零改动** | 方案 A |
| demo | `ModelProbe.h/.cpp` | + RefreshStatText + m_statRow + 5 处调用改接 |
| 测试 | `WidgetTests.cpp` / `TextBoxTests.cpp` | FakeTextMeasurer + TestableLabel + 9 用例（框架级） |
| vcxproj | 零改动 | 无新文件（fake/测试类进既有测试文件） |
| main.cpp | 不动 | — |

**零回归保证**：Widget 默认 GetPreferredSize 返回当前尺寸（无调用方受影响）；AutoSize 是新 API（无既有调用）；Layout/TextBox 既有路径零触碰。

## 8. 评审请求

请评审：① 3.2 AutoSize 实现体冻结（§3.7 纯度靠实现结构保证 + 行为测试——是否接受「审查性保证」而非可测计数？这是初设开放点 5 的答案）② 3.3 DoMeasureText 私有 helper 形态 ③ 3.4 TextBox 单行走 `TextWidget::GetPreferredSize()` 显式限定调用（多态正确性）④ 4 FakeTextMeasurer 码点计数规则（0xC0 前导字节判法）⑤ 5 用例 7 数据自洽性 ⑥ 6 RefreshStatText 调用点迁移（5 处 SetText → 统一出口——错误消息与统计消息语义合并是否影响 UpdateStat 现有逻辑）。

> **已评审（GPT，2026-09-02）**：「通过，允许进入实现阶段」——① 审查性保证接受（AutoSize 实现体冻结 + 结果/副作用测试）② DoMeasureText 定 private ③ 显式限定正确 ④ 措辞修订见 v1.1 ⑤ 用例 7 自洽 ⑥ UpdateStat 调用链留实现阶段检查（若 UpdateStat 自带布局/状态更新则避免机械迁移造成重复 Arrange）。实现阶段检查点：UpdateStat 是否依赖 m_statLabel 尺寸/Arrange 副作用。

## 9. 修订记录

- v1.0（2026-09-02）详细设计初稿：需求 v1.5 / 初设 v1.1 冻结语义落成精确签名与实现（Widget GetPreferredSize 虚默认当前尺寸 + AutoSize 4 行冻结实现；TextWidget override + ResolveMeasurer 接缝 + DoMeasureText helper + 空文本宽 0；TextBox override 多行拦截 + padding×2——§3.6 验证项自动成立）；FakeTextMeasurer（码点×8 宽 / 行高 16——UTF-8 前导字节计数）+ TestableLabel；测试 10 用例精确数据（用例 7 复核 {24,24} 自洽）；用例 8 断言方式定案（实现体冻结 + 行为验证——Invalidate/Arrange 非虚 + 无窗口 no-op 使计数不可行）；ModelProbe RefreshStatText 统一出口（+m_statRow 指针）+ 5 处调用点迁移；影响清单（Layout/main.cpp/vcxproj 零改动）。待评审。
- v1.1（2026-09-02）**GPT 详设评审通过**（「通过，允许进入实现阶段」）——两处文字决策 + 一处实现定案：① §4 FakeTextMeasurer 措辞修订（非 continuation byte 计数 = 测试测量模型，**不承担 UTF-8 合法性验证**）；② §3.2 float → int **向零截断策略冻结**（40.9 → 40；v1 不引入 rounding policy，不 std::round——Widget 几何本就是整数，截断是最小变化）；③ §3.3 `DoMeasureText()` 定案 **private 成员**（preferred 测量是 TextWidget 语义组成部分，非 cpp 匿名辅助函数）。评审确认不改项：AutoSize 不 Arrange/Invalidate、不做副作用计数测试（实现体冻结 + 行为验证）、TextBox 显式限定基类调用、多行 fallback、Button 不加 padding、Layout 零改动、RefreshStatText 顺序、无 m_hasExplicitSize、bool 返回值保留。
