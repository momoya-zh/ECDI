# Phase 45 · styled spans 表达力扩展（斜体 + 混字号，#89）· 详细设计稿

> 状态：**v1.2（2026-11-09）：详设评审放行进批 0（v1.1 复审 P0×0 / P1×0）；P2×3 已入稿（§6）；批 0 待开工指令**。（v1.1 要点如下。）v1.1：v1.0 详设评审吸收毕（P1×5 / P2×7，§6）；待复审放行批 0**。（v1.0 初稿要点如下。）v1.0：初稿，待详设评审。★ **条 74：状态由评审结论决定**。
> 上游：需求稿 **v1.4** `phase45-styled-spans-expressiveness-requirements.md` · 初设稿 **v1.8**（`phase45-styled-spans-expressiveness-preliminary-design.md`；初设评审放行；闸门 2 / 4 / 5 **用户已授权签字**，见初设 §6）。
> ★ 产品码自 `008daff` 零改动（实施前以 `git diff 008daff..HEAD -- ECDI/` 为空复核；非空则本稿全部行号须重验）。基线：用例 **599** · 公共头 **102** · 设计文档 **218 → 219**（含本稿）。
> ★ **行号约定**：除特别注明，行号均指 `008daff` 状态；落地批内以符号名为准、行号仅作定位。
> ★★ **门禁机制（沿 Phase 44 定稿）**：新稿须先 `git add` 再跑 `python tools/doc_lint.py --select L03,L04,L05,L06,L07,L08,L09,L11,L13`（列举式，勿用 `..`）；0 defect 须配阳性对照才算结论（条 76）。本稿落盘编码 = **UTF-8 + BOM、纯 LF**（与 dev 文档主流一致）。

**红线继承**（需求稿四条全文有效）：零 span 逐位等价 · 测画同源 · 命令流与 DrawText 执行体零改动（豁免面 = 字体实例化 / face 选择层 + LOGFONT 位段）· 编辑器通路不动（L44-1）。

---

## §0 范围、已签裁决与实施口径

### 0.1 已签裁决（实施依据；初设 §6 签字记录为准）

| 闸门 | 裁决 | 落点 |
|---|---|---|
| 1 | 斜体重启确认（2026-11-08 已签） | — |
| 2 | **ⓑ**：GDI 合成斜切 + GL 真斜体面 / 无则回落常规面；direct 路径回落 face0（L45-1） | §1.5 `FaceIdFor` |
| 3 | 收认混字号，圈选 ③-1（2026-11-08 已签） | 验收映射 |
| 4 | **ⓐ**：块级统一行框（A′ 最大字体 + 仅 size 覆盖触发）；含 L45-7 / 光标高可见影响 / J1 行为变更 | §1.3 / §1.4 |
| 5 | **ⓐ**：`TextMeasurer::Ascent(const Font&)` 纯虚，返回 DIP（= A′），源兼容破坏记 L45-6 | §1.2 |

### 0.2 本稿对初设的**细化与订正**（均不改变已签裁决，评审时请逐条确认）

| # | 项 | 说明 |
|---|---|---|
| D45-11 | **size 覆盖有效性谓词** | 初设未定义非法 size。本稿定义 `IsEffectiveSizeOverride(const SpanStyle&)` = `size.has_value() && std::isfinite(*size) && *size > 0`。`ResolveRunFont` 与行框守卫**共用同一谓词**（否则会出现「守卫触发但字体未改」或反之）。非法值 = 视同 nullopt（静默忽略，不告警；与 C36-1 clamp 边界精神一致）。 |
| D45-12 | **`BuildTextLayout` 不替换局部 `font`** | 初设 §3.1 写「`:422` 改读助手」。实际 `font`（`:422`）在非 spans 分支被 `FitText(font, …)` 使用（`:555` 起），**必须仍为基字体**。本稿改为：**保留 `font`，新增 `lineBoxFont`**，`lineHeight = measurer.LineHeight(lineBoxFont)`。语义与初设等价（初设意图 = 仅行高取助手），实现更安全。 |
| D45-13 | **助手只在判活时遍历** | `EffectiveLineBoxFont` 先判 `SpansActiveForLayout()`（O(1)），再判「是否存在有效 size 覆盖」（O(R) 纯比较、无测量），两者皆真才进入 O(R) 次 `Ascent`。无 size 覆盖的存量路径热路径零额外测量。 |
| D45-14 | **`GetLineHeight` 热路径** | 7 个消费点每帧调用 ⇒ 助手须廉价：守卫不触发时仅 1 次虚调用 + 1 次 span 扫描（无 span 时 `m_spans.empty()` 即返回）。size 覆盖存在性可缓存为 `m_spans` 变更时更新的 `bool`——**批 2 视性能探针决定是否加成员**（不加则 O(R) 纯比较，R 通常 < 20）。 |

### 0.3 范围

落 `TextWidget` 基类 + 只读 `TextBox`；编辑器通路（光标/选区编辑/IME/Undo）× 新维度仍归 L44-1；行内 code 底片 / 下划线 / 删除线（L44-3/4）不动。

---

## §1 逐文件修改清单

### 1.1 `ECDI/include/ECDI/Core/Font.h` · `TextSpan.h`（公共面 +3 字段）

**`Font.h`**：`weight` 之后**末位**追加（C7560：designated-init 顺序 + ABI 布局，`:23` 注释同族）：

```cpp
bool italic = false;   ///< 斜体（默认 false = 既有行为逐位不变）。
                       ///< 语义（Phase 45，闸门 2 ⓑ）：GDI = LOGFONT `lfItalic`（无真斜体面的族由 GDI **合成斜切**，
                       ///< 宽度增量 0.00%）；GL = 选 `OS/2.fsSelection` bit0 的真斜体面，**族内无真斜体面 ⇒ 回落常规面**
                       ///< （GL 不合成，C36-4 同族已知分歧；CJK 族真斜体面为零，O45-1）。★ 声明序：必须位于 `weight` 之后。
```

**`TextSpan.h`**：`SpanStyle` 在 `foreground` 之后追加（既有 designated-init 仅用 family/weight/foreground 子集且升序 ⇒ 追加安全）；`:13` 契约注释**解禁并改写**：

```cpp
/// @details 允许覆盖字体族、字重、斜体、字号与前景色。size 覆盖 = DIP 绝对值（与 Font.size 同语义，不跨换算）；
///          无效值（非有限 / ≤ 0）视同 nullopt（D45-11）。
struct SpanStyle {
    std::optional<std::string>   family     = std::nullopt;
    std::optional<std::uint16_t> weight     = std::nullopt;
    std::optional<Color>         foreground = std::nullopt;
    std::optional<bool>          italic     = std::nullopt;   ///< Phase 45
    std::optional<float>         size       = std::nullopt;   ///< Phase 45，DIP 绝对值
    bool operator==(const SpanStyle&) const noexcept = default;
};
```

⚠ 注：`defaulted operator==` 对 `optional<float>` 为逐位比较语义（NaN ≠ NaN）——span 表比较仅用于 revision/去抖，NaN 入表被 D45-11 视同无效，不影响正确性；T45 不测 NaN 的相等性。

### 1.2 `TextMeasurer.h` + 11 子类（闸门 5 ⓐ；纯虚 = 源兼容破坏 L45-6）

```cpp
/// @brief 该字体的**链内整数像素基线偏移**之 DIP 折算（A′，Phase 45）
/// @details 与 LineHeight（行框口径，otmMac* / hhea 盒）**分属两个口径**。DIP 单位：
///          A′ = AscentIntPx × 96 / dpi（同一 measurer 实例、同一 dpi）。
virtual float Ascent(const Font& font) = 0;
```

**11 个直接子类（批 0 一次性补齐；v1.1 按评审 P1-5 整体重数，逐点给类声明行；以批 0 编译错误清单为终准）**：

| 类别 | 类（类声明行） | `Ascent` 处置 |
|---|---|---|
| 产品 2 | `GDITextMeasurer`（`GDITextMeasurer.h:27`）· `FreeTypeTextMeasurer`（`FreeTypeTextMeasurer.h:18`） | 真实实现（§1.2 下） |
| 门面 1 | `RecordingBackend`（`RecordingBackend.h:21`，`LineHeight` 内联在 `:139`） | 内联固定值（与 `LineHeight` 的 `14.0f` 同风格，建议 `11.0f`） |
| 测试直接子类 8 | `FakeTextMeasurer`（`TextBoxTests.cpp:40`）· `FracMeasurer`（`:435`，`ResolveMeasurer()` 覆写夹具）· `CountingFakeMeasurer`（`:559`）· `RecordingFakeMeasurer`（`:2170`）· `FixedWidthMeasurer`（`TextMeasurerTests.cpp:208`）· `FakeTextMeasurer`（`WidgetTests.cpp:467`）· `CountingMeasurer`（`:714`，内嵌 `m_inner` ⇒ 透传 `m_inner->Ascent`）· `StyledSpansFakeMeasurer`（`:1915`，Phase 44 新增） | 显式常数 / 透传 |
| **传递继承 4（无需改，自动经基类）** | `ProportionalMeasurer`（`ClipTests.cpp:33`）· `WrapMeasurer`（`WidgetTests.cpp:809`）· `ParamLineHeightMeasurer`（`:1380`）——均 `: public RecordingBackend`；`CountingStyledMeasurer`（`:1976`）`: public StyledSpansFakeMeasurer` | 继承基类实现；如需自定义值再覆写 |

⚠ **订正**：v1.0 把 `PaintContext` 列为「子类/透传新增」属混计——`PaintContext`（`PaintContext.h:22`）是持有 `m_measurer` 的门面、**不是** `TextMeasurer` 子类，`PaintContext::LineHeight`（`.cpp:128-131`）全仓零调用 ⇒ **不改**（不加 `Ascent` 转发，零风险项；评审 P2）。v1.0 引用的测试行号指向 `LineHeight` 体而非类声明，现已全部订正。评审所述「共 12」与其自列名单（产品 2 + `RecordingBackend` + 测试 11 含传递继承）口径不同；**直接子类 = 11（本表），传递继承另计 4**，两种口径下编译错误清单都以 11 处直接实现为准。

**假件显式值纪律（P2 限定适用范围）**：旧假件 `Ascent` 返回常数（如 `12.0f`）**仅对「从不调用 `Ascent` 的旧用例」无害**——旧用例无 size 覆盖 ⇒ 守卫不触发 ⇒ 不调 `Ascent`；该句**不**推及 T45-9 的真实读数相（FT 口径 `floor(ascender/64) × 96/dpi` 任何常数假件都满足不了，T45-9 对真实 measurer 断言，对假件只断言「旧用例零调用」）。新夹具 `FontSizeScaledMeasurer` / `CrossFamilyMeasurer`（§2）为新增类，直接继承 `RecordingBackend` 或 `TextMeasurer` 均可（建议继承 `RecordingBackend` 复用命令录制）。

**GDI 实现**（`GDITextMeasurer.cpp`，沿 `LineHeight` `:405` 同款取字面路径）：

```cpp
float GDITextMeasurer::Ascent(const Font& font) {
    int dpi = GetDpiForWindow(m_hwnd); if (dpi <= 0) dpi = 96;
    const auto key = std::make_tuple(font.weight, font.size, font.family, font.italic, dpi);
    if (auto it = m_ascentCache.find(key); it != m_ascentCache.end()) return it->second;
    float result = 0.0f;
    if (HFONT h = GetOrCreateFont(font, dpi)) {           // 同 LineHeight 的取字面路径
        HDC dc = GetDC(nullptr);
        if (dc) { HGDIOBJ old = SelectObject(dc, h);
                  TEXTMETRICW tm{}; if (GetTextMetricsW(dc, &tm))
                      result = static_cast<float>(tm.tmAscent) * 96.0f / static_cast<float>(dpi);
                  SelectObject(dc, old); ReleaseDC(nullptr, dc); }
    }
    m_ascentCache.emplace(key, result);   // 失败结果也缓存（与 lineHeight 缓存同纪律，批 1 核对）
    return result;
}
```

> 实施注：`GetOrCreateFont` 的 dpi 参数形态以 `GDITextMeasurer.cpp:45-84` 现签名为准（上为 sketch）；`m_ascentCache` 新增成员，键含 italic 维（K45-6 纪律）。

**FT 实现**（`FreeTypeTextMeasurer`，薄包装；**不得**直接转发 `FontEngine::Ascent`——`FontEngine.cpp:664` 原体无 96/dpi 换算）。**v1.1 定死（评审 P1-4）：`FreeTypeTextMeasurer` 自己记 dpi**——`FreeTypeTextMeasurer.h` 现仅有 `std::shared_ptr<FontEngine> m_engine`（`:46`），`Initialize`（`.cpp:16-32`）把 dpi 直接 `m_engine->SetDpi(dpi)` 后丢弃，`FontEngine` 无 getter：

```cpp
// FreeTypeTextMeasurer.h（private，m_engine 旁）
int m_dpi = 96;
// FreeTypeTextMeasurer.cpp Initialize：在 m_engine->SetDpi(dpi) 之前（同一处）
m_dpi = (dpi > 0) ? dpi : 96;      // 与 GDI 的 fail-safe 同口径；SetDpi 仍传原 dpi（零行为变化）

float FreeTypeTextMeasurer::Ascent(const Font& font) {
    if (!m_engine) return font.size;   // 与 LineHeight 的无引擎兜底同口径
    // ★ floor：批 0 时 FontEngine::Ascent（:664）仍返回 ascender/64.0f（未整数化）；批 1 整数化后 floor 为 no-op。
    //   ⇒ 本包装在两个状态下都满足 A′ = floor(ascender/64) × 96/dpi（T45-9 FT 相批 0 即绿，无需等批 1）。
    return std::floor(m_engine->Ascent(font)) * 96.0f / static_cast<float>(m_dpi);
}
```

> 不动 `FontEngine` 公开面（备选「`FontEngine::GetDpi()`」动豁免面头，不采）。`m_dpi` 仅一行成员 + 一行赋值。
> **依赖线（评审 P1-4 显式标注）**：T45-9（批 0）以 `floor(ascender/64) × 96/dpi` 为口径，由上面包装内的 `floor` 保证；**`FontEngine::Ascent` 整数化（§1.5）排批 1**，与批 0 解耦——批 0 不依赖整数化，批 1 整数化后 T45-9 仍绿（floor no-op）。

### 1.3 `TextWidget.h/.cpp`（行框 / 基线 / 判据单点化）

#### 1.3.1 新增成员（`TextWidget.h`，protected 区，`HasActiveSpans` 邻近）

```cpp
protected:
    /// 布局/绘制 span 激活判据的**单一真相源**（Phase 45 J1–J3）。基类 ≡ HasActiveSpans()。
    [[nodiscard]] virtual bool SpansActiveForLayout() const { return HasActiveSpans(); }

    /// span → 字形字体（唯一组装点；RunSlice.font 与行框集合共用）。size 覆盖仅在 IsEffectiveSizeOverride 时生效。
    [[nodiscard]] static Font ResolveRunFont(const Font& base, const SpanStyle& s);

    /// 行框字体（A′ 最大者；仅 size 覆盖触发，否则 = 基字体）。**不是** ResolveRunFont 的产物，
    /// 而是同一 run 字体集合上的兄弟概念（不同选取规则）。
    [[nodiscard]] Font EffectiveLineBoxFont(TextMeasurer& measurer) const;
```

> `IsEffectiveSizeOverride` 作为 `TextSpan.h` 内 inline 自由函数（`inline bool IsEffectiveSizeOverride(const SpanStyle&) noexcept`），**不增公共头**（既有头内改）；是否暴露为公共 API 由评审定（倾向：放 `TextSpan.h` 的 `detail`-风格注释声明「内部」，公共面 +0 方法）。

#### 1.3.2 实现（`TextWidget.cpp`）

```cpp
Font TextWidget::ResolveRunFont(const Font& base, const SpanStyle& s) {
    Font f = base;
    if (s.family.has_value()) f.family = *s.family;
    if (s.weight.has_value()) f.weight = *s.weight;
    if (s.italic.has_value()) f.italic = *s.italic;
    if (IsEffectiveSizeOverride(s)) f.size = *s.size;
    return f;
}

Font TextWidget::EffectiveLineBoxFont(TextMeasurer& m) const {
    const Font& base = m_style.font.value;
    if (!SpansActiveForLayout()) return base;                       // 判活（O(1)）
    bool anySize = false;
    for (const auto& sp : m_spans) {
        if (sp.cpCount != 0 && IsEffectiveSizeOverride(sp.style)) { anySize = true; break; }
    }
    if (!anySize) return base;                                       // size 守卫（纯比较）
    constexpr float kEps = 1e-3f;
    Font  best   = base;
    float bestA  = m.Ascent(base);
    float bestLH = m.LineHeight(base);   // 惰性：仅并列时才需要 → 实现可延迟求值（批 2 优化）
    for (const auto& sp : m_spans) {
        if (sp.cpCount == 0) continue;                               // 零宽 span 不产生 run
        const Font f = ResolveRunFont(base, sp.style);
        const float a = m.Ascent(f);
        if (a > bestA + kEps) { best = f; bestA = a; bestLH = m.LineHeight(f); }
        else if (std::fabs(a - bestA) <= kEps) {                     // 并列：LineHeight 大者；再并列保留先到者（基字体居先）
            const float lh = m.LineHeight(f);
            if (lh > bestLH) { best = f; bestA = a; bestLH = lh; }
        }
    }
    return best;
}
```

> 集合 = 基字体 + 各 span 物化字体（含非 size 的 span 其字号 = 基字号，已参与比较——size 守卫已触发时它们属于「新维度已激活的块」）。**span 区间（v1.1 订正，评审 P2）**：`ClampAndValidateSpans`（`TextWidget.cpp:252-289`）已在 `SetStyledText`（`:848` / `:869`）入口裁剪越界 span，`startCp ≥ 码点数` 的 span 经公开 API **不可达**行框判定；助手循环里**真正生效**的过滤只有 `cpCount != 0`（零宽 span 不产 run）。T45-3 ⑨ 据此改述为「零宽 span 不参与行框」。

#### 1.3.3 `BuildRunsForRange`：`:374-381` 改调 `ResolveRunFont`

```cpp
if (rEnd > rStart) {
    const Font rFont = ResolveRunFont(baseFont, span.style);   // 原 8 行四分支 → 单调用（规则单一真相源）
    ...
}
```

零 span / 旧 span（仅 family/weight/color）⇒ `ResolveRunFont` 与原组装逐字段等价 ⇒ C45-2。

#### 1.3.4 `BuildTextLayout`（`:418-`）：D45-12

```cpp
const Font& font = m_style.font.value;                        // 保持：FitText / 非 spans 分支
const Font lineBoxFont = EffectiveLineBoxFont(measurer);      // 值语义（助手可能返回局部字体）
const float lineHeight = measurer.LineHeight(lineBoxFont);    // :423 ← 站 1
...
const bool hasSpans = SpansActiveForLayout() && (text == m_text);   // :498 ← J1
```

`:430`（空文本）/ `:584`（totalHeight = `lineHeight × lines.size()`，**原乘法式不动**）= 下游消费，零改动。空文本相：此时 `HasActiveSpans()` 为真意味着 `m_styledTextIdentity == m_text` 且 spans 非空——空文本无合法 span 覆盖 ⇒ 助手守卫 `cpCount != 0` 保证至多返回基字体；**T45-3 空文本相钉**。

#### 1.3.5 逐 run 基线发射（绘制 wrap `:658-` / 非 wrap `:718-` / `:737-`）

三站点实参替换（站 2/3/4）：

```cpp
const Font lineBoxFont = EffectiveLineBoxFont(measurer);     // 每绘制函数入口一次
const float lineHeight = measurer.LineHeight(lineBoxFont);    // :658 / :718 / :737 实参
```

逐 run 发射（现状每 run `pos = {x, lineY}`；改后）：

```cpp
const bool lineBoxActive = /* lineBoxFont 与基字体不同 或 判活且有 size 覆盖——用 EffectiveLineBoxFont 的守卫结果 */;
const float boxA = measurer.Ascent(lineBoxFont);   // 仅 lineBoxActive 时调用（守卫外零新增测量）
for (const RunSlice& run : line.runs) {
    float y = lineY;
    if (lineBoxActive)
        y = lineY + boxA - measurer.Ascent(run.font);       // A′(box) − A′(run) ≥ 0 （§3.1 定理）
    ctx.DrawText({x, y}, run.font, run.text, color);        // DrawText 执行体零改动
    x += run.width;
}
```

* `lineBoxActive` 判据 = 与助手**同一守卫**（`SpansActiveForLayout() && anySize`）——实现上助手内部拆出 `bool LineBoxGuardActive() const` 供两处共用，防「行高守卫」与「基线守卫」分叉。
* 守卫不触发 ⇒ `y == lineY` 恒成立 ⇒ 命令流逐位今日（C45-1/C45-2）。

#### 1.3.6 preferred（`DoMeasureText` `:824` / `:829`，站 5/6）

```cpp
// I44-7（Phase 45 取代）：行高恒来自同一个助手 EffectiveLineBoxFont（单源性保留，源由基字体改为有效行框字体）；
// 原意「不得由整段 MeasureText 高度反推」仍满足。
const float lineHeight = measurer.LineHeight(EffectiveLineBoxFont(measurer));
```

`:823` 注释按上改写（1 行）；`:829` 同改。★ **开销注（评审 P2）**：非 spans 分支（`:814` 判活为假）调用助手时，助手首句 `SpansActiveForLayout()` 即 O(1) 返回基字体，**不进** O(R) 的 size 存在性扫描；判活为真且无 size 覆盖时才付一次 O(R) 纯比较（无测量），可接受；若批 2 探针显示热点，用 D45-14 的缓存布尔收掉，不另设守卫分支。

#### 1.3.7 判据站点 J2：`:652` / `:774` / `:814`

`HasActiveSpans()` → `SpansActiveForLayout()`（基类逐位等价；TextWidget / Label 零行为变化）。`:774` 邻近的 `m_prefRevision == m_textRevision` 自检短路经评审复核无缓存假命中（组合态 `MarkTextChanged` 递增 revision）。

#### 1.3.8 指纹三站扩 italic（K45-7；v1.1 订正「两站」→ 三站）

三站 = wrap `TextWidget.cpp:602-604`（`m_wrapFont`）· preferred `:777-779`（`m_prefFont`）· TextBox 行表 `TextBox.cpp:506-508`（§1.4 已列）。**仅比较处**逐字段追加 `&& m_xxxFont.italic == m_style.font.value.italic`。★ **不需要**「缓存成员赋值处同步存 italic」（评审 P2）：`m_wrapFont` / `m_prefFont` / `m_linesFont` 均为**整结构体赋值**（`TextWidget.cpp:611`、`:791`、`TextBox.cpp:531`），`Font` 加字段后自动带上；`Font` 无 `operator==`（`Font.h` 仅排序注释），所以只有这 3 处比较要改，**不得**多写赋值行（死代码）。`m_prefRuns`（`:704`）键缺字体维 = 既有缺陷，**不在本 Phase 修**（L45-8）。

### 1.4 `TextBox.h/.cpp`（只读显示面接入）

| 项 | 位置 | 改动 |
|---|---|---|
| 钩子覆写 | `TextBox.h` **protected 区（`:191` 起，`:319` private 之前）** | `bool SpansActiveForLayout() const override { return HasActiveSpans() && m_echoMode == EchoMode::Normal && !m_isComposing; }` |
| J3 三站点 | `TextBox.cpp:495 / :526 / :1536` | `activeSpans = SpansActiveForLayout();`（逐字同一表达式，行为中性） |
| `GetLineHeight` | `:586-593` | 窗口分支取值来源换助手；**无窗口分支原样 `return 16.0f`**（不改走 `ResolveMeasurer()`）：<br>`if (Window* w = const_cast<Window*>(GetWindow())) { TextMeasurer& m = w->GetTextMeasurer(); return m.LineHeight(EffectiveLineBoxFont(m)); }` |
| `MultilinePreferred` | `:203`（站 8） | `const float lineH = measurer->LineHeight(EffectiveLineBoxFont(*measurer));`（**measurer 身份不变**——`TextBoxTests.cpp:435` `FracMeasurer`（`:432` 为其 `ResolveMeasurer()` 覆写）夹具无窗口 19.2f 假件不被 16.0f 兜底带歪） |
| 行表键维 2/3/4 | `:506-508` | font 三字段比较追加 `italic`（维 12 = 复用 font 维加 italic 比较，不新增键位） |
| 逐 run 基线 | TextBox 绘制 `:1536` 起 spans 分支 | 同 §1.3.5：`lineBoxActive` 时 `y = lineY + boxA − A′(run)`（measurer 来自绘制上下文） |

`EffectiveLineBoxFont` 在 `TextBox` 内以 `TextWidget::EffectiveLineBoxFont` 调用（`protected` 可达）；`TextBox.cpp` 其余 `GetLineHeight()` 消费点（`:612/:628/:645/:790/:813/:878/:1471`）与 `lineH` 下游 19 处**零改动**，批 2 以回归扫描清单逐处复核（光标高 `:813` 随块内 max 变大 = 闸门 4 已签可见影响）。

### 1.5 `FontEngine.h/.cpp` · `GDIBackend.cpp` · `GDITextMeasurer.cpp` · `GLRenderer.cpp`（豁免面）

**`FaceIdFor` 三参**（`FontEngine.h:96` / `.cpp:286`）：`FaceId FaceIdFor(const std::string& family, std::uint16_t weight, bool italic = false);`

**`familyCache` 键**（`.cpp:114` 声明 / `:296` 键 / `:308` `:315` `:421` 三处 emplace 含失败缓存）：`std::map<std::pair<std::string,uint16_t>,FaceId>` → `std::map<std::tuple<std::string,uint16_t,bool>,FaceId>`；`famKey = std::make_tuple(family, weight, italic)`。

**选面两相**（`:397-403`）：

```cpp
// ④⑤ 扫描 + 过滤。italic=false ⇒ 原语义逐位（非 direct 且 e->italic 出局）。
// italic=true ⇒ 非 direct：只收 e->italic 面；direct：沿 Phase 36 豁免（单面池 face0，不排除）。
const bool nameMismatch = (e->nameId1En != family);
if (!g.direct && (nameMismatch || (italic ? !e->italic : e->italic))) continue;
...
// ⑥ 全灭回退：italic=true 且池空 ⇒ **回落常规池**（= italic=false 的选面结果），绝不返回 0（GL 执行体零改动）：
if (tiers.empty()) {
    if (italic) return FaceIdFor(family, weight, false);   // 递归一次：内部回落，结果同样缓存到 (family,weight,false)；
                                                          // 本键 (family,weight,true) 也 emplace 同值，防每帧重试
    ... 原告警 + fallbackToDefault()
}
```

> **回落写缓存纪律**：italic=true 回落得到的 `id` 须 `familyCache.emplace(famKey(true), id)`，避免每帧递归与重复告警（T26-13 防刷屏锚的同族要求）。无真斜体面族的「回落」**不告警**（预期行为，非错误）；找不到族的告警沿原路径（italic=false 递归时已打一次）。
> direct 相：`Win32FontSource.cpp:308-315` 单 face 池，italic 请求**回落 face0**（L45-1）；`Win32FontSource.cpp` **不入改动面**。

**11 个调用点逐点传 italic**（`FontEngine.cpp` 10：`:427 / :468 / :517 / :580 / :629 / :650 / :657 / :667 / :680 / :699` + `GLRenderer.cpp:240`）：均改为 `FaceIdFor(font.family, font.weight, font.italic)`。★ **4 个度量点须逐个点名核对**：`:629`（`LineHeight` 内 `FaceOf(FaceIdFor…)`）、`:657`（`Ascent` 内同句）、`:650` / `:667`（`ascentCache` 的 find / emplace 键 `{faceId, px}`——faceId 已含 italic 分档，**键结构不改**）。漏传 ⇒ GL 侧斜体基字体解析到常规 face 的 hhea，GDI 侧 LOGFONT 带斜体位 ⇒ 两链行高/基线分叉。

**其余缓存键扩 italic 维**：`lineHeightCache`（`:621`，键 `(weight,size,family,dpi)` → 加 `italic`）· measure 键（`:459`）· `GDIBackend.cpp:1628` HFONT 键 · `GDITextMeasurer.cpp:52` font 键 / `:98` measure 键 · 新增 `m_ascentCache` 键。**`GlyphKey` / glyphCache / advanceCache 三族不动**（faceId 天然隔离，`:699` 构造现场可证；防实施误扩）。

**`FontEngine::Ascent` 语义修订（整数像素）**（`:657-667` 体）：

```cpp
// 修订前：返回 face->size->metrics.ascender / 64.0f（浮点）
// 修订后：返回 floor(face->size->metrics.ascender / 64.0f)（整数像素，float 承载）
// GLRenderer.cpp:254 的外层 floor(...) 变 no-op，该行一字不动（红线 3）。
```

连带：GL 全行基线上移 <1px 子像素余量；**T44-11 基线锚重锚**（L44-9 结账，批内显式执行，见 §4 批 1 步骤）。

**LOGFONT 位段 ×2**：`GDIBackend.cpp:1640` 与 `GDITextMeasurer.cpp:64` 之后各加 `lf.lfItalic = font.italic ? TRUE : FALSE;`（零初始化默认 0 ⇒ `italic=false` 逐位今日）。

**GL 执行体**：`GLRenderer.cpp:240` 的 `FaceIdFor(...)` 传 `font.italic`；`:254` 起基线行不动。

### 1.6 改动面总表（对初设 §4 的落地核对）

| 文件 | 预估 | 备注 |
|---|---|---|
| `Font.h` / `TextSpan.h` / `TextMeasurer.h` | +25 | §1.1 / §1.2 |
| `FontEngine.h/.cpp` | +65 | §1.5 |
| `GDIBackend.cpp` / `GDITextMeasurer.cpp/.h` / `FreeTypeTextMeasurer.cpp/.h`（+ `m_dpi`）/ `RecordingBackend.h` | +60 | Ascent 产品面 2 + 门面 1 + 位段 + 键（**`PaintContext.*` 不改**，v1.1） |
| `GLRenderer.cpp` | +1 | `:240` |
| `TextWidget.h/.cpp` | +125 | §1.3 |
| `TextBox.h/.cpp` | +55 ~ +95 | §1.4 |
| `Tests/` 3–4 文件 + 8 个直接测试假件补 `Ascent`（另 3 个经 `RecordingBackend`、1 个经 `StyledSpansFakeMeasurer` 传递，免改） | +380 | §2 |
| （可选）批 2 GL 读回装置 | 不计入预算 | 仓内无 GL 上下文测试装置；T45-4 主断言已改走 `RecordingBackend` 观测（§2），GL 真读回仅为批 2 可选加强相，无 GL 环境跳过 |
| `Win32FontSource.cpp` / `CMakeLists.txt` | 0 | 不入 / GLOB |

---

## §2 测试设计（599 → 608，T45-1..9）

**文件分配**：`WidgetTests.cpp`（T45-1 / 3 / 4 / 5 / 6）· `FontWeightTests.cpp`（T45-2 / 7）· `TextBoxTests.cpp`（T45-3 TextBox 相、判据一致相）· `TextMeasurerTests.cpp`（T45-9）· `T45-8` 两链相落真窗口 / 无头 GL 探针既有文件（沿 T44-9）。注册语句沿 Phase 44 详设 §2.3 口径（`TEST(...)` 宏 + 套件名随文件）。

**专用假件 `FontSizeScaledMeasurer`**（新夹具；**不改既有假件**，含 Phase 44 的 `StyledSpansFakeMeasurer` / `CountingMeasurer`）：`LineHeight(f) = f.size × 1.2`、`Ascent(f) = f.size × 0.8`、`MeasureText` 宽 = 码点数 × `f.size × 0.5`、`FitText` 依其宽。另备 **`CrossFamilyMeasurer`**：按 family 名返回 `(asc, desc, gap)` 表，`LineHeight = asc + desc + gap`、`Ascent = asc`。★ **v1.1 订正（评审 P1-3，v1.0 标签写反）**：合法夹具 = **box(25,0,0)（ascent 最大者，LineHeight 25）vs run(20,10,10)（LineHeight 40）**——`A′(box)=25 ≥ A′(run)=20` ⇒ `pos.y = lineTop + 5` 合法；初设 `run(25,0,0) vs box(20,10,10)` 是用来**证明旧「LineHeight 最大」方案错误**的非法算例（`pos.y = lineTop − 5`），**不是**断言夹具，不得移植。该合法对同时是唯一有信息量的组合：旧规则（按 LineHeight 取 max）会选 run 字体 ⇒ 本夹具能区分两种实现，并把 **L45-7 变成可观测量**（见 T45-3 ③）。

| # | 用例 | 判据（要点；全部可无头） |
|---|---|---|
| **T45-1** | 斜体 run 命令流 | `RecordingBackend`：每 run 恰一条 DrawText；命中 italic 的 run `font.italic==true`、未命中 false；x 累积同源（C44-2）；C45-2 相（italic 未置 ⇒ 全 false） |
| **T45-2** | 斜体选面（无 direct 相） | 有真斜体面族（`consolai` / Segoe 样本）`FaceIdFor(f,w,true)` ≠ `FaceIdFor(f,w,false)` 且为斜体 face；无斜体面族（CJK 样本）`FaceIdFor(f,w,true)` == `(f,w,false)` 且 **≠ 0**；`italic=false` 全表现状逐位；回落**不重复告警**（日志计数断言） |
| **T45-3** | 行框（多相） | ① **同高相**：无 size 覆盖 ⇒ totalHeight 与 Phase 44 逐位（`lineHeight × 行数`，浮点乘法）；② **归属相**：大字号 run 在第二行 ⇒ 首行行高同涨（防取首行字体）；③ **A′ 归属相 + L45-7 可观测量**：`CrossFamilyMeasurer`，box(25,0,0) 与 run(20,10,10) 同块 ⇒ 选出的行框字体 = A′ 最大者（box），**即使其 LineHeight(25) < run 的 LineHeight(40)**（防「按 LineHeight 取 max」实现骗过）；`lineHeight == 25`；run 的 `pos.y == lineTop + 5 ≥ lineTop`（无负位移）；**L45-7 断言**：`run 盒下沿 − 行框下沿 = (A′box − A′run) + LineHeight(run) − lineHeight = 5 + 40 − 25 = 20 > 0`（`EXPECT_NEAR`，已签的已知代价以断言形式固化，而非仅文字登记）；④ **size 守卫相**：基 Consolas + `{family="Segoe UI"}`（无 size）⇒ `GetLineHeight()` / totalHeight / preferred 逐位相等；weight-only / italic-only 同断言；加 size 覆盖 ⇒ 才变；⑤ **五站同源相**：**★ v1.1（评审 P1-1）断言式改 `EXPECT_NEAR(..., kEpsilon)`（沿 `WidgetTests.cpp:39` 的 `kEpsilon = 0.001f`），不用浮点 `==`**——末行 `lineY + lineHeight`（= `block.y + (N−1)×lh + lh`，`:666` 路径）与 `block.y + totalHeight`（`:584` 乘法式）在 float32 下舍入路径不同，评审模型化 13.33% 不等（与 A′ 无关的假红）；同源语义靠「同一 `lineHeight` 局部变量」结构 + 负对照保证（回退 `:658` 实参 ⇒ 本相红，差异远大于 ε），不靠位相等；非 wrap / preferred 同取助手字体；（① 的 `lineHeight × 行数` 两边同为乘法，安全，仍逐位）；⑥ **TextBox preferred 同源**：size 激活 ⇒ `MultilinePreferred().height == GetMaxScrollOffset 内容高`（二者同乘 ceil 前的同一 `lineH`，仍用 `EXPECT_NEAR`）；无窗口 + `TextBoxTests.cpp:435` 夹具（`FracMeasurer`，v1.0 误写 `:432`）⇒ preferred 高仍 19.2 系；⑦ **判据一致相**（`FontSizeScaledMeasurer` + TextBox）：密码态 / 组合态 + size spans ⇒ 折行与 `GetLineHeight()` 均 = 基字体；Normal 态对照 ⇒ 两者同按有效字体；⑧ **空文本相**：`m_text` 空 ⇒ 基字体行高；⑨ **零宽 span 相**（v1.1 改述，评审 P2：越界 span 经 `ClampAndValidateSpans` 公开 API 不可达）：`cpCount == 0` 的 size 覆盖 span 不触发守卫、不参与行框（若要测越界，须经内部受控构造并写明，默认不测） |
| **T45-4** | 基线共享 | **主断言（v1.1 明确主次；v1.2 改载体，评审 P2-1）= 整数基线逐 run 相等，由 `RecordingBackend` 可观测的 `pos.y` 经「测试内复现两链量化」得出**：测试按 `Renderer.cpp:23-26` 折 scale（`y_px = scale·pos.y`）后，GDI 链取 `trunc`（`GDIBackend.cpp:922-923`）、GL 链取 `floor`（`GLRenderer.cpp:254`），断言 `quant(y_px) + AscentIntPx(run)` 同线逐 run **整数相等**（`AscentIntPx = round(A′·dpi/96)`，A′ 来自公共 `Ascent`）；96dpi 精确、非 96dpi 允许 ±1px（评审真链模型 3 万组/档整数基线不等率 < 0.01%、最大差 1px，属浮点边界）。**不新建 GL 上下文装置**——仓内测试零 `wglCreateContext/HGLRC/glReadPixels`，`GLBackendTests.cpp:24-27` 明写「本组不需要 GL 环境」；**GL 真读回 = 批 2 可选装置**（无 GL 环境时跳过），§1.6 已登记，不属本期必选变更面；中点变体批 2 spike 后可收紧；**辅助相** = DIP 域 `pos.y + A′(run.font)` == 常数 **±1e-3**（公共 `Ascent` 无头观测；评审 float32 满跑 20k/dpi 最大偏差 7.6e-6，该容差可靠，但该等式被 floor 掩盖，不作主断言） |
| **T45-5** | 等价护栏双相 | **v1.1 改载体（评审 P1-2）：golden 内嵌为 C++ 原始字符串字面量，零文件 I/O、零 CMake 改动**——仓内测试无读盘先例（`ECDI/src/Tests` 下 `ifstream/ofstream/fopen/std::filesystem` 零命中）、`CMakeLists.txt:162-163` 仅 `GLOB_RECURSE *.cpp` 且无数据文件部署规则，故**不落 `golden/*.txt`、不加 `ECDI_SOURCE_DIR` 编译定义**（评审倾向的「进程内双相」不可单独成立：同构建内 Phase 44 实现已被覆盖，无法产生 HEAD 基线）。做法：批 0 在 **HEAD（未改产品码）** 一次性跑录制例程，把「零 spans」「Phase 44 spans（weight/family/color）」两份命令流 dump 粘贴为 `static constexpr const char* kGoldenZeroSpan / kGoldenPhase44Spans` 入测试 `.cpp`，**录制例程不入库**。★ **序列化格式规范（v1.2，评审 P2-2）**：每个 `DrawText` 命令一行，字段序固定 = `x\|y\|size\|family\|weight\|[italic]\|r\|g\|b\|a\|text`（`TextDraw` 的 pos 2 float + color 4 float，`RecordingBackend.h:33-39` / `Color.h:16-19`，加 `Font` 的 size/family/weight）；所有 float 用 `std::snprintf(buf, sizeof buf, "%a", v)`（**C99 十六进制浮点，仅需 `<cstdio>`，不引入 `<iomanip>/<sstream>`**，保位精确）；`family`/`text` 为 UTF-8 原样，其中 `\|`、`\`、换行转义为 `\\\|`、`\\`、`\n`；★ **`italic` 字段仅当为 `true` 时输出 `\|italic=1`**（HEAD 无该字段，golden 在 HEAD 录制、字面量不含它；批 1 之后 `italic=false` 的 dump 与 HEAD 字面量逐字节一致，这是 C45-1/C45-2 的比较基础）。用例把新构建下同夹具的 dump 与字面量逐字节比较。补充进程内相（零基础设施）：零 spans ≡ 全为 `SpanStyle{}` 的 span（新旧维度全空）⇒ 命令流逐位相等。负对照（改新路径）恰红 |
| **T45-6** | 负对照① | 回退 italic 覆盖（`span.italic` 置回继承）⇒ **恰** T45-1 italic 断言 + T45-2 红、其余绿（施工期臂，入库形式 = 用例内「禁用 italic 物化」测试桩开关） |
| **T45-7** | 负对照② 键完备投毒 | 投毒面含 **`familyCache`** / lineHeightCache / ascent / GDI 双键 / 指纹：改 `italic` 位不改键 ⇒ miss 重算 |
| **T45-8** | 两链一致（分相） | GDI 相：真面族基线重合 + 合成斜切观感；GL 相：真斜体面 + 回落相；每链内整数基线重合为主断言；跨链沿既有容差 |
| **T45-9** | `Ascent` 接口相 | 同一 measurer 实例、同一 dpi：GDI `Ascent(f) == tmAscent × 96/dpi`（±1e-3）；FT `== floor(ascender/64) × 96/dpi`（**批 0 即绿**：由 `FreeTypeTextMeasurer::Ascent` 内的 `floor` 保证，不依赖批 1 的 `FontEngine::Ascent` 整数化，见 §1.2 依赖线）；**旧假件仅断言「旧用例零调用 `Ascent`」**（计数假件 `CountingMeasurer` 等验证 `Ascent` 调用计数为 0——v1.1 限定适用范围，常数假件满足不了真实读数口径）；`Ascent` 缓存键随 italic 分档（italic 真面族 ascent 与常规面 hhea 一致，O45-3，断言「不分叉」；该相在批 1 italic 落地后补入，批 0 仅占位注释） |

**存量护栏**：`TextBoxTests` 的 `GetLineHeight` 无窗口相（`:2026-2028` 注释语义，恒 16.0f）作为批 0 断言入库；`T44-11` 基线锚重锚在批 1 执行并留差异记录。

---

## §3 不变式追溯（C45-1..6 / P0-3 / P0-4 / I44-7）

| 不变式 | 结构性保证 | 钉住 |
|---|---|---|
| C45-1 零 span 逐位 | `SpansActiveForLayout()` 假 ⇒ 助手返回基字体；`rFont`=基字体；`FaceIdFor(...,false)` 原语义；LOGFONT 位段零 | T45-5 ① |
| C45-2 旧 spans 逐位 | 无 size 覆盖 ⇒ 守卫不进；`ResolveRunFont` 逐字段等价；y 偏移恒 0 | T45-5 ② + T45-3 ④ |
| C45-3 行高 | 块级统一 ⇒ `totalHeight = lineHeight × 行数`（原乘法式，无 Σ 路径） | T45-3 ①②⑤ |
| C45-4 基线共享 | §3.1 定理 + 整数位移与截断可交换（初设 §3.2 证明） | T45-4 / 8 |
| C45-5 键完备 | 七族键扩维 + 指纹三站 | T45-7 |
| C45-6 选面一致 | 两相选面 + 回落统一收 `FaceIdFor` | T45-2 / 8 |
| I44-7 | 由本 Phase 取代：行高恒来自同一助手 | T45-3 ⑤ |

---

## §4 实施计划（3 批；每批收口跑全量 + 门禁）

| 批 | 内容 | 用例 | 收口判据 |
|---|---|---|---|
| **批 0 · 基线与接口** | ① 复核 `008daff` 零改动；② **golden 录制（v1.1：在 HEAD 跑录制例程，两份 dump 内嵌为 `.cpp` 字符串字面量；零 I/O、零 CMake）**；③ `TextMeasurer::Ascent` 纯虚 + **11 个直接子类一次性补齐**（GDI 实现含 `m_ascentCache`；FT 实现含 `m_dpi` + `floor` 包装；`RecordingBackend` 内联；8 个测试假件，类声明行见 §1.2 表；传递继承 4 个自动经基类，`PaintContext` 不改）；④ T45-9（批 0 口径，不含 italic 分档相）；⑤ `GetLineHeight` 无窗口相护栏 | 599 → **601**（T45-5、T45-9） | 599 存量零改动全绿 + 2 新绿；golden 字面量与 HEAD 逐字节一致 |
| **批 1 · 斜体维度** | `Font.italic` · `SpanStyle.italic` · `ResolveRunFont`（含 size 分支但守卫尚未接线时 size 无调用）· `FaceIdFor` 三参 + `familyCache` 键 + 两相回落 + 11 调用点 + **4 度量点** · 七族键扩维 + 指纹 · **`FontEngine.cpp:121` `ascentCache`（`:650` find / `:667` emplace）是唯一无 `kMax*` 上界的缓存，键空间随 italic 翻倍（键结构因 faceId 含 italic 不变）⇒ 同批补上界，复用 `:30-33` 兄弟缓存的 `kMax*` 策略（评审 P2-3）** · LOGFONT 位段 · `FontEngine::Ascent` 整数化 + **T44-11 重锚（差异记录入档）** | 601 → **605**（T45-1/2/6/7） | 全绿；T45-5 golden 仍逐位（italic 全空路径） |
| **批 2 · 混字号行框** | `SpanStyle.size` · `SpansActiveForLayout` 钩子 + J1/J2/J3 · `EffectiveLineBoxFont` + `LineBoxGuardActive` · 6 实参 + `:203` · TextBox `GetLineHeight` · 逐 run 基线 · I44-7 注释 · 中点变体 spike · 回归扫描（光标/选区/滚动/行号 `lineH` 下游 19 处） | 605 → **608**（T45-3/4/8） | 608 全绿；T45-5 golden 逐位；负对照各自恰红 |

**批收口项**：落地锚点续订义务（`README.md:64` / `docs/dev/README.md:24`：用例 599→收口值；设计文档 218→219；`docs/dev/README.md`「🔥 当前」迁「✅ 已完成」）。

**回滚点**：每批独立提交；批 2 失败可回退至批 1（斜体维度独立可用）。

---

## §5 已知风险与登记

| 项 | 风险 / 处置 |
|---|---|
| L45-7 | 行高取 A′ 最大字体；descent 更大的另一 run 下沿可能超框（闸门 4 签字内容） |
| J1 行为变更 | TextBox 组合期 × spans 的布局由 spans 分支变基字体分支（订正 Phase 44 不一致）；存量夹具核查结果 = 零冲击（TextBoxTests 仅 `:3563`，只读无组合） |
| L45-6 | `Ascent` 纯虚 = 源兼容破坏；usage.md / 交付说明标注 |
| L45-8 | `m_prefRuns` 键缺字体维为既有缺陷，不在本 Phase 修 |
| D45-14 | `GetLineHeight` 热路径性能——批 2 探针后决定是否缓存 size 覆盖存在性 |
| FT 侧 CJK 行框 | O45-3 遗留（TTC hhea upem=1 异常）：批 2 用 ECDI `FontEngine` 探针复测 CJK 行框，读数落 §6 |

---

## §6 修订记录

- **v1.2（2026-11-09）★ v1.1 复审放行批 0（P0×0 / P1×0 / P2×3，上轮 12 条评审逐条核验吸收）；P2 随手入稿**：**P2-1** T45-4 主断言载体改 `RecordingBackend` 观测 + 测试内复现两链量化（仓内无 GL 上下文装置），GL 真读回降为批 2 可选相并登记 §1.6；**P2-2** T45-5 补序列化格式规范（字段序、`%a` 十六进制浮点仅需 `<cstdio>`、转义规则、`italic` 仅为 true 时输出以保 HEAD 字面量有效）；**P2-3** 批 1 加 `ascentCache` 上界（复用 `kMax*` 策略）；措辞：§1.6 假件行写全「8 直接 + 3 经 RecordingBackend + 1 经 StyledSpansFakeMeasurer 传递」。产品码零改动。

- **v1.1（2026-11-09）★ v1.0 详设评审吸收（退回修订；P0×0 / P1×5 / P2×7；骨架/定理/豁免面评审确认）**：**P1-1** T45-3 ⑤ 浮点 `==` → `EXPECT_NEAR(kEpsilon)`（同源靠同一变量 + 负对照）。**P1-2** golden 载体：不落文件，改**内嵌字符串字面量**（仓内无读盘先例、CMake 仅 GLOB `*.cpp`）；未采评审倾向的「进程内双相」作**唯一**载体——同构建内无 HEAD 基线，故以字面量为主、进程内「零 span ≡ 全 `SpanStyle{}`」为补充。**P1-3** `CrossFamilyMeasurer` 夹具标签订正为合法对 box(25,0,0) vs run(20,10,10)，并把 **L45-7 固化为断言**（run 盒下沿 − 行框下沿 = 5+40−25 = **20**，按 LineHeight 盒口径；评审口述 15 与此口径不同，以公式为准）。**P1-4** FT：`FreeTypeTextMeasurer` 增 `int m_dpi`（`Initialize` 一行赋值）、`Ascent` 内 `floor` 包装，批 0 即绿，与批 1 `FontEngine::Ascent` 整数化解耦；T45-9 依赖线显式标注。**P1-5** 子类整体重数：**直接子类 11**（产品 2 + `RecordingBackend` + 测试 8）+ 传递继承 4 + `PaintContext` 门面不改，逐点给类声明行；评审所述「12」与其自列名单口径不一致，已在 §1.2 说明。**P2**：指纹三站且删多余赋值行 · `PaintContext` 降级为不改 · 非 spans 分支开销注 · ⑨ 改零宽 span 相（越界 span 公开 API 不可达）· T45-4 主辅断言分明 · T45-9 旧假件限定为「零调用」。
- **v1.0（2026-11-09）**：初稿。基于初设 v1.8（评审放行 + 闸门 2/4/5 用户授权签字）。新增细化 D45-11..14；全部行号对 `008daff` 重读。

---

> 详设 v1.2（2026-11-09）：**详设评审放行进批 0**——批 0 = 零行为变化 + 11 个直接子类补 `Ascent` + golden 内嵌 + T45-5/T45-9（条 74）。
