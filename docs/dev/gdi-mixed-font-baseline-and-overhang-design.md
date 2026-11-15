# ECDI 方案细化：GDI 混合字体基线对齐与下伸部溢出防护（#91 / #92 / 联动 #93）

> **状态**：**v1.2 · 闸门已拍板（`1ⓐ + 2ⓑ + 3ⓐ`）· 已实现并通过**（2026-11-15）——见 §9 修订记录；v1.2 补入闸门 1ⓐ 的真实代价实测与 `I44-7` 契约破坏登记。
> **关联账本**：`docs/dev/roadmap-deferred.md` **#91**（`REQ-23`）、**#92**（`REQ-24`）、**#93**（`REQ-22`）；外部需求源 = `D:\Project\ECDI_fake_harness\docs\ecdi-capability-requests.md:935-989`。

---

## 1. 问题背景与范围界定

### 1.1 三个关联请求

1. **#91（`REQ-23`）—— GDI 链混合字体 `RunSlice` 未按基线对齐**：在正文为 `Noto Sans SC @ 15`、行内 code 仅覆盖 `SpanStyle::family = "Consolas"` 与 `foreground`（不设 `size`）时，GDI 链下 `Consolas` 相对中文正文视觉上浮约 3px（125% DPI 下约 2.4 DIP）；同夹具在 `--backend gl` 下基线基本对齐。
2. **#92（`REQ-24`）—— GDI 链 `tmHeight > LineHeight` 导致 `AutoSize` + `PushClip` 裁切字母下伸部**：使用 `Noto Sans SC @ 15`（96 DPI 下 `otmMacAscent=15, otmMacDescent=-3, lh_mac=18`，而 `tmAscent=17, tmDescent=4, tmHeight=21`）经 `AutoSize()` 定高后，单行/多行 `Label` 与只读 `TextBox` 中 `g/j/p/q/y` 下伸部底部被裁平 3px；同夹具在 `--backend gl` 下无裁切，在 `Microsoft YaHei @ 15` / `Consolas @ 15` 下无裁切。
3. **#93（`REQ-22`）—— `Widget::AutoSize` 浮点向零截断导致亚像素裁切风险**：`Widget::AutoSize`（`Widget.cpp:580-581`）使用 `static_cast<int>` 向零截断 `preferred.width/height`。在非 100% DPI（125% / 150%）下，即使补足了 #92 的下伸保护余量，带小数的 DIP 行高/包络高仍可能被截断 1 像素导致 `PushClip` 自裁。

### 1.2 非目标（红线边界）

- **不回退 Phase 31 行高口径（`REQ-05`）**：不把 `GDITextMeasurer::LineHeight` 改回 `tmHeight`，保持多行行距与 FreeType/GL 链逐字一致。
- **不全局偏移 GDI 基线（`Phase 31 O10` / `REQ-06`）**：不将 `GDIBackend::DrawTextImpl` 从 `TA_TOP` 改为 `otmMacAscent` 对齐，不破坏 `Test45AscentInterfaceContract`（`GDITextMeasurer::Ascent == tmAscent * 96 / dpi`）。
- **不扩大 `Widget::Paint` 裁剪越界**：任何控件仍严格在其几何盒 `self` 内通过 `ctx.PushClip(self)` 绘制（Phase 27 视口剔除安全性前提不动摇）。
- **单字体与零 span 路径逐位零改动**：当文本未触发字体覆盖（`LineBoxGuardActive() == false`）且字体不存在 `Ascent + Descent > LineHeight` 的下伸溢出（例如 `Microsoft YaHei`、`Consolas`、`SimSun`、GL/FreeType 全部字体）时，preferred 尺寸、布局高度、绘制坐标必须与现状逐位一致。

---

## 2. 事实勘察矩阵（全部经源码与本机实测亲核）

| 编号 | 核查对象 | 核实位置 | 已验证事实 |
|---|---|---|---|
| **K1** | 行框守卫条件 | `ECDI/src/Widget/TextWidget.cpp:347-355` | `LineBoxGuardActive()` 仅检查是否存在 `sp.cpCount != 0 && IsEffectiveSizeOverride(sp.style)`（即 `size.has_value() && *size > 0.0f`，`TextSpan.h`）。仅设 `family` / `weight` / `italic` / `foreground` 时一律返回 `false`。 |
| **K2** | 有效行框字体选择 | `ECDI/src/Widget/TextWidget.cpp:357-382` | `EffectiveLineBoxFont(TextMeasurer& m)` 在 `!LineBoxGuardActive()` 时直接返回 `m_style.font.value`；否则在 `{base} ∪ {非空 span}` 中取最大 `m.Ascent(f)`（平手在 `1e-3f` 容差内取最大 `m.LineHeight(f)`）。 |
| **K3** | 逐 run 基线发射点（3 处） | `TextWidget.cpp:717-738`（wrap）、`:771-787`（单行）、`TextBox.cpp:1545-1565`（只读 `OnPaint`） | 三站点共享完全相同的算式：`boxA = lineBoxActive ? measurer.Ascent(EffectiveLineBoxFont(measurer)) : 0.0f`，`runY = lineBoxActive ? (lineY + boxA - measurer.Ascent(run.font)) : lineY`。机制已完备，仅被 **K1** 门控挡住。 |
| **K4** | `T45-3 ④` 守卫用例行为 | `ECDI/src/Tests/WidgetTests.cpp:2733-2752` | 使用 `FontSizeScaledMeasurer`（`LineHeight = 1.2 * size`、`Ascent = 0.8 * size`，对 `family`/`weight`/`italic` 恒等），断言仅设 `family`/`weight`/`italic` 时行高不变、设 `size` 时才变。注释写明「均不触发守卫」，需按闸门 1 结果决定注释/实现策略。 |
| **K5** | `GDITextMeasurer` 缓存结构 | `GDITextMeasurer.h:98-111`、`GDITextMeasurer.cpp:469-526` | `m_measureCache` 与 `m_ascentCache` 键为 `(weight, size, family, italic, dpi)`；但 **`LineHeight` 无缓存**（每次调 `GetOrCreateFont` + `GetDC` + `GetOutlineTextMetricsW`），且 `m_ascentCache` 未做 `kMaxMeasureCache`（4096）上限清理；`SetOutlineTextMetricsForTests`（`.h:83-85`）是 `T31-3` OTM 失败注入缝。 |
| **K6** | GDI vs GL 度量与落墨裂口 | `GDITextMeasurer.cpp:469-489, 517`、`GDIBackend.cpp`、`GLRenderer.cpp`、`FontEngine.cpp` | GDI `LineHeight` = `otmMacAscent - otmMacDescent + otmMacLineGap`；`Ascent` = `tmAscent * 96 / dpi`；`DrawTextImpl` 调 `TextOutW(..., TA_TOP)`，基线落在 `drawY + tmAscent`，墨迹延伸至 `drawY + tmAscent + tmDescent = drawY + tmHeight`。GL `DrawText` 基线落在 `floor(pos.y) + floor(Ascent)`（`hhea ascender/64`），与 `LineHeight`（`hhea height/64`）同源闭环。 |
| **K7** | 本机 `Noto Sans SC` 实测 | Windows Registry + GDI OTM（96 DPI） | 系统安装字体为 `Noto Sans SC (TrueType) : NotoSansSC-VF.ttf`。96 DPI 下 `Noto Sans SC @ 15`：`lh_mac = 15 - (-3) + 0 = 18`，`tmAscent=17, tmDescent=4, tmHeight=21`（溢出 **3px**）；`@13/14/16` 溢出 **3/3/5px**；`Consolas @ 15`：`tmAscent=14, tmDescent=4, tmHeight=18, lh_mac=18`（溢出 **0px**）。 |
| **K8** | `AutoSize` 与 `PushClip` 链 | `Widget.cpp:574-588, 603-610`、`TextBox.cpp:180-210, 1491-1494` | `Widget::AutoSize` 取 `static_cast<int>(GetPreferredSize())` 定高；`Widget::Paint` 对 `self` 调用 `ctx.PushClip(self)`；`TextBox::OnPaint` 以 `GetTextAreaHeight() = GetHeight() - 2*padding` 裁剪。`TextBox::MultilinePreferred`（`TextBox.cpp:209`）已采用 `std::ceil(contentH)`，但 `TextWidget::DoMeasureText` 与 `Widget::AutoSize` 尚未 `ceil`。 |
| **K9** | **垂直居中反吃余量陷阱** | `TextWidget.cpp:674-683`、`TextBox.cpp:605-622` | `CalculateTextPosition` 使用 `offsetY = (GetHeight() - lineHeight) / 2.0f`；`TextBox::GetTextTopInset` 在 `Center` 态使用 `offset = max(0, (GetTextAreaHeight() - lines * lineH) / 2.0f)`。若只给 `AutoSize` 高度加 `+overhang`（如 `18 -> 21`）而不调整传给居中计算的视觉高，居中会下推 `+1.5px`，重新把底边推进裁切区。 |
| **K10** | `TextMeasurer` 子类面与测试 | `TextMeasurer.h`、`TextBox.cpp:587-596, 629-634`、`TextBoxTests.cpp:1795-1812` | 若新增**纯虚**方法会迫使全仓 11 个子类全部修改（Phase 45 `L45-6` 先例）；若新增**带安全默认实现的虚方法**，8 个测试假件与 `RecordingBackend` 零改动。另：`TextBox::GetLineHeight()` 在无窗口时固定返回 `16.0f`（测试不变量）。 |

---

## 3. 耦合机理分析（为何 #91 与 #92 必须联合设计）

以 harness 转录区实际字体组合（`base = Noto Sans SC @ 15`，行内 code span = `Consolas @ 15`）为例（96 DPI）：

1. **当前未修状态**：
   - `LineBoxGuardActive() == false` ⇒ `Noto Sans SC`（`tmAscent=17`）与 `Consolas`（`tmAscent=14`）均以 `runY = lineY` 落笔 ⇒ `Consolas` 基线落在 `lineY + 14`，正文基线落在 `lineY + 17`，**行内 code 偏高 3px**（`REQ-23`）。
   - `AutoSize()` 仅按 `lh_mac = 18` 定高，正文墨迹 `[lineY, lineY + 21]` 底部 `[18, 21]` 被 `PushClip` 裁掉 **3px**（`REQ-24`）。此时 `Consolas` `[lineY, lineY + 18]` 恰因“偏高 3px”而侥幸未被底裁。
2. **仅修 #91 而不修 #92 的后果**：
   - 修复 #91 后，`boxA = 17`，`Consolas` 的 `runY` 变为 `lineY + 17 - 14 = lineY + 3`，其墨迹区间变为 `[lineY + 3, lineY + 21]`。
   - 若 #92 未修（盒高与裁剪仍停在 `18`），**连 `Consolas` 的 `g/j/p/q/y` 下伸部也会一起被裁掉 3px**。
3. **非 100% DPI 下的 #93 联动**：
   - 在 125% DPI 下，即使 `LineHeight + overhang` 精确算出了覆盖全部墨迹的 DIP 包络高（例如 `20.8f`），`Widget::AutoSize`（`Widget.cpp:580-581`）的 `static_cast<int>(20.8f)` 仍会将其截断为 `20` DIP（换算回物理像素约 `25px < 26px`），重新引入 1px 底裁。

结论：**#91 负责把 `Consolas` 下移到共享基线，#92 负责护住 `[lineHeight, boxA + maxDescent]` 的底部墨迹不遭 `AutoSize`/`PushClip`/`ScrollMax`/`居中` 误裁，#93 负责防止非 100% DPI 下的 `float → int` 截断吞掉保护余量。**

---

## 4. 候选方案对比与待拍板闸门

### 闸门 1（#91 / REQ-23）：基线对齐的触发口径

| 选项 | 机制描述 | 优点 | 代价 / 风险 |
|---|---|---|---|
| **ⓐ 扩维 `LineBoxGuardActive()`（推荐）** | 将 `LineBoxGuardActive()` 从「存在有效 `size` 覆盖」扩为「存在改变解析后 `Font` 的有效 span」（即 `ResolveRunFont(base, sp.style)` 与 `base` 在 `family` / `weight` / `italic` / `size` 任一维不同；纯 `foreground` 覆盖仍返回 `false`）。三处绘制站点与 `EffectiveLineBoxFont` 完全复用，零新接口。 | 单一真相源（`LineBoxGuardActive` + `EffectiveLineBoxFont`）；不仅对齐 `Ascent`，当内联切换为更小字号基字体的高行框字体（如基 `Consolas@15` 内嵌 `Microsoft YaHei@15`）时，行框也会同步保护。 | ① 对变更 `family`/`weight`/`italic` 而不设 `size` 的 span，若 `EffectiveLineBoxFont` 选中了 `LineHeight` 更大的字体，行高会随之抬高——**这是对 Phase 44 不变量 `I44-7` 的契约破坏**（详见下方「★ 代价①的实测校准」）；② `GDITextMeasurer::LineHeight` 必须先补缓存否则高频调用 `GetOutlineTextMetricsW`；③ 需更新 `T45-3 ④` 注释口径（`FontSizeScaledMeasurer` 用例断言仍全绿）。 |
| **ⓑ 分离「行框字号守卫」与「基线对齐守卫」** | `LineBoxGuardActive()` 保持 `size`-only（`LineHeight` 不因纯 `family/weight/italic` 变化）；新增独立判定 `BaselineAlignActive()` 与 `EffectiveBaselineAscent(m)`，在三处绘制站点分别算 `boxA` 与 `lineHeight`。 | 100% 维持 Phase 45「无 `size` 覆盖时不改变行高」语义，`T45-3 ④` 注释与语义零调整。 | 增加第二套遍历/选取助手与第二条心智路径；且基为小行高字体、内联为大 `LineHeight` 同字号异族字体时，`lineHeight` 不抬高需完全依赖 #92 下伸余量兜底。 |

> **★ 代价①的实测校准（v1.2 补，2026-11-15）**
>
> v1.1 原文只举「`Noto Sans SC@15` + `Consolas@15` 两者 `lh_mac=18` 不变」这一个**恰好不变**的组合，
> 易被读作「扩维对行高总是无害」。实测（`.workbuddy/spike/p91/probe.cpp`，96 DPI 屏幕 DC，
> LOGFONT 口径与 `GDITextMeasurer::GetOrCreateFont` 逐位对齐）给出硬读数：
>
> | 字体 @16 | weight 400 | weight 700 | italic on |
> |---|---|---|---|
> | Noto Sans SC | 19 | 19 | 19 |
> | Microsoft YaHei | 21 | 21 | 21 |
> | Consolas | 19 | 19 | 19 |
> | SimSun | 18 | 18 | 18 |
> | Segoe UI | 21 | 21 | 21 |
>
> （表内 = `otmMacAscent - otmMacDescent + otmMacLineGap`，DIP）⇒ **`LineHeight` 只随 `family` 变化，
> `weight` 与 `italic` 逐位不变**。故：
>
> 1. **闸门 1ⓐ 的真实收益全部来自 `family`**（即 `REQ-23` 的原始诉求）；`weight`/`italic` 的扩维
>    在真实 GDI 下几何影响近乎为零，只多付一次 `EffectiveLineBoxFont` 遍历；
> 2. **真实污染幅度上限 = 1px**：唯一可观测项是 `Microsoft YaHei @ 16` 粗体的 `tmDescent 4→5`
>    （overhang `0→1`），其余 19 组污染恒为 `0`；
> 3. ⚠ **`Test44` 的 `24.0f` 不是本表的行为**：该值来自测试假件 `StyledSpansFakeMeasurer`，其原注释
>    写明「行高判别力：常规体 16px，粗体 24px（**若施工期错误取用 run 的最大行高，即刻飘红**）」
>    ⇒ 那是**故意设的毒饵**，把真实污染夸大了 25 倍。该断言已改用 `max(LineHeight over runs)` 复算，
>    并在注释中登记 **`I44-7` 作废** + 「不得读作粗体抬高行高 50%」。
> 4. **裁决（ⓑ）**：既然真实代价仅此，仍**保留** `1ⓐ` 全维扩维（单一真相源 `LineBoxGuardActive` 的好处
>    大于 1px 代价），但必须把 `I44-7` 作为**契约破坏**显式登记，而非以「合同更新」轻描淡写。

### 闸门 2（#92 / REQ-24）：闭合 GDI `tmHeight` 与 `hhea LineHeight` 裂口的方式

| 选项 | 机制描述 | 优点 | 代价 / 风险 |
|---|---|---|---|
| **ⓐ GDI 绘制侧上移对齐 `otmMacAscent`（不推荐）** | 在 `GDIBackend::DrawTextImpl` 将 `TextOutW` 坐标按 `tmAscent - otmMacAscent` 上移。 | 不改任何 `AutoSize` 高度。 | **实测数理上不成立**：① 对 `Noto Sans SC @ 15` 上移 2px（`17->15`）后，顶部 `15-17 = -2` 越出 `PushClip` 上边界被**倒裁顶部**，且底部 `15+4 = 19 > 18` **仍多出 1px 继续裁底部**；② 对 `Consolas @ 15`（`tmAscent=14, otmMacAscent=11`）上移 3px 会将本身不裁的英文**全部顶裁 3px**；③ 直接推翻 `Phase 31 O10` 冻结结论与 `Test45AscentInterfaceContract`（`Ascent == tmAscent * 96 / dpi`）。 |
| **ⓑ `BottomOverhang` 下伸保护余量（推荐）** | 保持 `LineHeight()` = `hhea`（行间距不变）与 `Ascent()` = `tmAscent`（基线契约不变），在 `TextMeasurer` 增加**带默认实现**的 `virtual float Descent(const Font&)`（默认 `max(0.0f, LineHeight(f) - Ascent(f))`）；`GDITextMeasurer::Descent` 返回 `tmDescent * 96 / dpi`。定义行内下伸溢出 `overhang = max(0.0f, boxA + maxRunDescent - lineHeight)`，仅在**外包 preferred 高、居中参考总高、`TextBox` 最大滚动高**三处计入 `+ overhang`。 | ① 数学上同时覆盖单字体（`Noto Sans SC` 21-18=3px）与混排基线对齐下移（`Consolas` +3+18-18=3px 或大 `tmDescent` run）；② 单行内行距 step 不变，保住 `REQ-05`；③ `Microsoft YaHei` / `Consolas` / `SimSun` / GL 全部算得 `overhang == 0`，逐位不变；④ 非纯虚默认实现使 8 个测试假件与 `RecordingBackend` **零改动**。 | ① 调用过 `AutoSize()` 的 `Noto Sans SC` 标签/文本框高度增加 3px（这是消除裁切的必要几何高度）；② 若调用方显式 `SetSize(w, 18)` 不调 `AutoSize()`，`Widget::Paint` 的 `PushClip(self)` 仍按调用方给定的 18px 裁切（遵循不越界绘制红线）；③ 需在 `TextWidget::DrawTextContent` 与 `TextBox::GetTextTopInset(Center)` 同步传视觉高，避开 **K9** 居中陷阱。 |

### 闸门 3（#93 / REQ-22 联动）：`Widget::AutoSize` 浮点向正无穷取整

| 选项 | 机制描述 | 优点 | 代价 / 风险 |
|---|---|---|---|
| **ⓐ 随本批收口 `Widget::AutoSize` `ceil` 策略（推荐）** | 将 `Widget.cpp:580-581` 改为带 `1e-4f` 浮点误差容忍的 `std::ceil`（`static_cast<int>(std::ceil((std::max)(0.0f, v - 1e-4f)))`），消除 125%/150% DPI 下 `px * (96.0f / dpi)` 与浮点累计导致的亚像素截断，一并收口 `REQ-22`。 | 彻底打通 125% / 150% / 175% DPI 下 `AutoSize` 的无裁切闭环（`Noto Sans SC @ 15` 在 125% DPI 下不会因 `20.8f -> 20` 漏掉 1px）；整数尺寸（含 `1e-5` 噪声）保持原整数值不变，与 `TextBox.cpp:209` 既有策略对齐。 | 会使所有小数 `GetPreferredSize()` 的控件在 `AutoSize()` 时向上取整 1px（符合 `REQ-22` 验收判据 ①）；若某既有测试依赖非整数测量假件被截断而非进位，需同步检查。 |
| **ⓑ 本批仅动 `TextWidget/TextBox`，`#93` 独立延后** | `Widget::AutoSize` 保持 `static_cast<int>`；仅 `TextBox::MultilinePreferred` 已有 `ceil`。 | 影响面最小（不触及非文本控件 `AutoSize`）。 | 在 125% / 150% DPI 下，`Label` 与单行 `TextBox` 的 `AutoSize` 仍会随机出现 1px 底部裁切，`REQ-24` 跨 DPI 场景无法完全闭环。 |

---

## 5. 推荐定案（闸门 1ⓐ + 闸门 2ⓑ + 闸门 3ⓐ）的详细修改清单

以下施工清单以 **推荐选型（1ⓐ + 2ⓑ + 3ⓐ）** 展开；若用户在闸门中选 `1ⓑ` 或 `3ⓑ`，仅局部增删对应块即可。

### `△1` `ECDI/include/ECDI/Render/TextMeasurer.h` — 新增默认虚方法 `Descent`

- 在 `Ascent(const Font& font)` 之后新增：
  ```cpp
  /// @brief 字体基线以下像素/DIP 下伸深度（用于底部溢出防护；默认假设盒高闭合，即 max(0, LineHeight - Ascent)）
  virtual float Descent(const Font& font)
  {
      return (std::max)(0.0f, LineHeight(font) - Ascent(font));
  }
  ```
- **兼容性**：非纯虚 + 头内内联（仅依赖已有 `<algorithm>`、`LineHeight`、`Ascent`），公共头自包含检查无新增 include，全仓 8 个测试假件、`RecordingBackend`、`FreeTypeTextMeasurer` 无需改动即可保持 `Ascent + Descent == LineHeight`（当 `LineHeight >= Ascent` 时 `overhang == 0`）。
- **边界注记**：`FreeTypeTextMeasurer` 在 `Noto Sans SC @ 15` 实机验证无裁切，故沿用默认实现保持 GL 链路逐位等价；若未来遇到个别 CFF/变体字形实际轮廓越出 FreeType `metrics.height`，可在 `FreeTypeTextMeasurer` 独立覆写 `Descent` 而不改接口。

### `△2` `ECDI/src/Render/GDITextMeasurer.h` & `.cpp` — 合并缓存 `Ascent`/`Descent` 与缓存 `LineHeight`

1. 将 `m_ascentCache` 升级为 `m_tmMetricsCache`（结构体 `struct TmMetrics { float ascent = 0.0f; float descent = 0.0f; };`，键仍为 `std::tuple<std::uint16_t, float, std::string, bool, int>`）：
   - 抽取私有辅助 `TmMetrics QueryTextMetrics(const Font& font)`：一次 `GetTextMetricsW` 同时计算 `tmAscent * 96.0f / dpi` 与 `tmDescent * 96.0f / dpi`，写入 `m_tmMetricsCache`；插入前检查 `m_tmMetricsCache.size() >= kMaxMeasureCache` 时 `clear()`（修复 Phase 45 遗留的无界增长隐患）。
   - `Ascent(font)` 返回 `QueryTextMetrics(font).ascent`（行为与数值与改前逐位相同，`Test45AscentInterfaceContract` 零影响）。
   - `Descent(font)` 覆写返回 `QueryTextMetrics(font).descent`（温缓存 `O(log K)`，零额外 GDI 调用）。
2. 新增 `m_lineHeightCache`（同键类型，受 `kMaxMeasureCache` 4096 上限保护）：
   - 在 `GDITextMeasurer::LineHeight(font)` 中，**仅当 `m_outlineTextMetrics == &::GetOutlineTextMetricsW`（生产真实路径）时**查/写 `m_lineHeightCache`；当注入测试替代函数时绕过缓存，且 `SetOutlineTextMetricsForTests` 调用时顺带 `m_lineHeightCache.clear()`，保证 `T31-3` OTM 失败注入完全确定性。

### `△3` `ECDI/include/ECDI/Widget/TextWidget.h` & `.cpp` — 守卫扩维、`BottomOverhang` 与视觉居中分离

1. **字体比较与守卫扩维（`TextWidget.cpp`）**：
   - 新增内部辅助 `FontsEqual(const Font& a, const Font& b)`（逐字段比较 `size`、`family`、`weight`、`italic`，由于 `Font.h` 未定义 `operator==`）。
   - 修改 `TextWidget::LineBoxGuardActive()`：
     ```cpp
     bool TextWidget::LineBoxGuardActive() const {
         if (!SpansActiveForLayout()) return false;
         const Font& base = m_style.font.value;
         for (const auto& sp : m_spans) {
             if (sp.cpCount != 0 && !FontsEqual(ResolveRunFont(base, sp.style), base)) {
                 return true;
             }
         }
         return false;
     }
     ```
   - 分析：若用户传 `SpanStyle{ .family = "Noto Sans SC" }` 而基字体已是 `"Noto Sans SC"` 或仅传 `SpanStyle{ .foreground = color }`，`ResolveRunFont` 后字体与 `base` 完全相等，守卫仍为 `false`（零额外测量开销）；只有真发生 `family`/`weight`/`italic`/`size` 变化时才激活。
2. **新增 `protected` 辅助 `EffectiveBottomOverhang(TextMeasurer& measurer) const`**：
   - 若 `!LineBoxGuardActive()`：
     ```cpp
     const Font& base = m_style.font.value;
     const float boxH = measurer.LineHeight(base);
     const float inkH = measurer.Ascent(base) + measurer.Descent(base);
     return (std::max)(0.0f, inkH - boxH);
     ```
   - 若 `LineBoxGuardActive()`：
     ```cpp
     const Font& base = m_style.font.value;
     const Font boxFont = EffectiveLineBoxFont(measurer);
     const float boxA = measurer.Ascent(boxFont);
     const float boxH = measurer.LineHeight(boxFont);
     float maxDesc = measurer.Descent(base);
     for (const auto& sp : m_spans) {
         if (sp.cpCount == 0) continue;
         maxDesc = (std::max)(maxDesc, measurer.Descent(ResolveRunFont(base, sp.style)));
     }
     return (std::max)(0.0f, boxA + maxDesc - boxH);
     ```
   - **关键属性**：对 `Consolas` / `Microsoft YaHei` / GL 链路单字体或同等行框混排，`EffectiveBottomOverhang` 恒等于 `0.0f`。
3. **消费点 ① —— `TextWidget::DoMeasureText(TextMeasurer&)`**：
   - Wrap 分支：返回 `Size{ blockW, layout.totalHeight + EffectiveBottomOverhang(measurer) }`（注意 `TextLayout::totalHeight` 本体维持 `lines.size() * lineHeight`，不破坏 `T45-3 ⑤` 及任何行步进断言）。
   - 单行分支（含 spans 与无 spans）：返回 `Size{ textW, lineHeight + EffectiveBottomOverhang(measurer) }`。
   - **缓存自洽性**：`m_prefCache` 既已校验 `(m_textRevision, m_style.font.value, dpi, m_styledTextRevision)`（当文本、字体、DPI 或 spans 改变时失效），返回结果含 overhang 无需额外键维。
4. **消费点 ② —— `TextWidget::DrawTextContent(PaintContext&, int, int)`（解决 K9 陷阱）**：
   - 计算 `const float overhang = EffectiveBottomOverhang(measurer);`。
   - Wrap 分支调用 `CalculateTextPosition(x, y, BlockWidth(layout), layout.totalHeight + overhang)`；逐行推进仍用 `lineY += lineHeight`（绝不把 `overhang` 乘进每行步进！）。
   - 单行分支调用 `CalculateTextPosition(x, y, textW, lineHeight + overhang)`。
   - **效果验证**：当 `AutoSize()` 使 `GetHeight() == layout.totalHeight + overhang`（如 1 行 `Noto Sans SC @ 15`：`18 + 3 = 21`）时，`offsetY = (21 - 21) / 2.0f = 0.0f`，行 0 从 `y + 0` 起笔，墨迹覆盖 `[y, y + 21]`，与 `PushClip(self)` 严丝合缝。对于固定高 `Button`（如 `H = 32`），`offsetY = (32 - 21) / 2 = 5.5f`，墨迹重心居中且距顶底各有 `5.5px` 安全裕度。

### `△4` `ECDI/src/Widget/TextBox.cpp` — 多行首选高、垂直居中、最大滚动同步

1. **新增私有/受保护辅助 `GetBottomOverhang() const`**：
   - 参考 `TextBox::GetLineHeight()`（`:587-596`）：
     ```cpp
     float TextBox::GetBottomOverhang() const {
         if (Window* window = const_cast<Window*>(GetWindow())) {
             return EffectiveBottomOverhang(window->GetTextMeasurer());
         }
         return 0.0f;
     }
     ```
2. **`TextBox::MultilinePreferred()`（`:203-209`）**：
   - 取 `measurer = ResolveMeasurer()`（已有指针）：
     `const float overhang = EffectiveBottomOverhang(*measurer);`
     `const float contentH = static_cast<float>(lines) * lineH + overhang + pad * 2.0f;`
   - 返回 `Size{ static_cast<float>(GetWidth()), static_cast<float>(std::ceil(contentH)) }`。
   - 单行 `TextBox::GetPreferredSize()`（`:181`）直接委托 `TextWidget::GetPreferredSize()`，已自动含 `overhang`。
3. **`TextBox::GetTextTopInset()`（`:615-621`，`Center` 分支）**：
   - `Top` 快路径（`:609-610`）保持零改动。
   - `Center` 分支：
     `const float contentH = static_cast<float>(m_lines.size()) * lineH + GetBottomOverhang();`
     `const float offset = (std::max)(0.0f, (GetTextAreaHeight() - contentH) / 2.0f);`
     避免 `Center` 态的 `AutoSize` 单/多行 `TextBox` 下滑 `overhang / 2` 后自裁底部。
4. **`TextBox::GetMaxScrollOffset()`（`:629-634`）**：
   - `const float contentHeight = static_cast<float>(m_lines.size()) * GetLineHeight() + GetBottomOverhang();`
   - 效果：固定高度的滚动 `TextBox` 滚至最底端时可把末行 `tmDescent` 完整滚入视口；而经 `AutoSize()` 定高的 `TextBox` 因 `GetTextAreaHeight() >= contentHeight`，`GetMaxScrollOffset()` 仍为 `0.0f`（不冒滚动条）。

### `△5` `ECDI/src/Widget/Widget.cpp`（若闸门 3 选 ⓐ）— `AutoSize` 进位策略

- `Widget::AutoSize()`（`Widget.cpp:580-581`）：
  ```cpp
  constexpr float kCeilEps = 1e-4f;
  const int w = (preferred.width > 0.0f)
      ? static_cast<int>(std::ceil((std::max)(0.0f, preferred.width - kCeilEps)))
      : 0;
  const int h = (preferred.height > 0.0f)
      ? static_cast<int>(std::ceil((std::max)(0.0f, preferred.height - kCeilEps)))
      : 0;
  ```
- 注：`1e-4f` 守卫可避免 `float32` 累加/换算产生 `18.000002f` 时误进位成 `19`，同时使真实小数部分（如 `20.4f`、`20.8f`、`61.2f`）稳定向上进位，并在 `<cmath>` 下保持零边界问题。

### `△6`–`△7` 既有代码与测试对账

- `TextWidget.cpp:717` / `:771` 与 `TextBox.cpp:1545`：调用 `EffectiveLineBoxFont(measurer)` 在温路径中每行可能触发若干次 `Ascent`/`LineHeight`；`△2` 为两表补齐缓存后，温路径纯 `std::map` 查询，无任何 GDI 对象或 DC 分配。
- `WidgetTests.cpp:2733-2752`（`T45-3 ④`）：若采纳闸门 1ⓐ，将注释从「纯 family/weight/italic 均不触发守卫」更新为「`FontSizeScaledMeasurer` 同字号不同族不改变行高；真 GDI 字体改变解析 `Font` 时激活行框/基线守卫」。

---

## 6. 设计契约与红线约束（C1–C8）

| 契约 | 内容 |
|---|---|
| **C1（行距不变性）** | 对一切字体，`TextLayout::totalHeight == lines.size() * LineHeight(boxFont)`，且逐行发射步进 `dy == LineHeight(boxFont)`，GDI 与 GL 多行行距与 `REQ-05` 保持逐位一致。 |
| **C2（基线与 `Ascent` 不变性）** | `GDITextMeasurer::Ascent(font) == tmAscent * 96.0f / dpi` 不变；同族或跨族混排下，所有非空 run 在行内共享 `boxA = max(Ascent(boxFont))` 对应的物理基线。 |
| **C3（下伸保护闭环性）** | 经 `AutoSize()` 定高的单行/多行 `Label` 与 `TextBox`（含 `Top` 与 `Center` 对齐），在任意 `tmAscent + tmDescent >= LineHeight` 的字体与任意混排下，`PushClip` 下界 ≥ 末行 `boxA + maxRunDescent`。 |
| **C4（居中不吞余量）** | `CalculateTextPosition` 与 `TextBox::GetTextTopInset(Center)` 均以含 `overhang` 的总视觉高度做垂直居中计算，保证 `AutoSize` 后 `offsetY == 0`（无余量被吃）。 |
| **C5（零覆盖零溢出字体零退化）** | 当 `!LineBoxGuardActive()` 且 `Ascent(base) + Descent(base) <= LineHeight(base)`（如 `Consolas`、`Microsoft YaHei`、`SimSun`、GL 全部字体）时，`overhang ≡ 0.0f`，`DoMeasureText`、`GetPreferredSize`、`DrawTextContent` 与 `TextBox` 滚动上限全部逐位不变。 |
| **C6（纯前景色 zero-cost）** | 仅覆写 `SpanStyle::foreground` 且不改变 `Font` 四字段的 span 不激活 `LineBoxGuardActive()`。 |
| **C7（测试 Seam 确定性）** | `GDITextMeasurer::SetOutlineTextMetricsForTests` 注入非系统 OTM 函数时必须清空并绕过 `m_lineHeightCache`。 |
| **C8（缓存有界）** | `GDITextMeasurer` 新增的 `m_lineHeightCache` 与合并后的 `m_tmMetricsCache` 均受 `kMaxMeasureCache`（4096）上限约束，防止动态字号/DPI 场景无界增长。 |

---

## 7. 验证与负对照计划（T1–T6）

| 用例 / 探针 | 覆盖目的 | 断言判据 | 负对照 / 判别力验证 |
|---|---|---|---|
| **T1（REQ-23 命令流）** | 仅 `family` 覆盖（无 `size`）的跨族基线对齐 | 构造 `RecordingBackend` 或派生 `CrossFamilyAscentMeasurer`（`BaseFamily: asc=17, lh=18`; `CodeFamily: asc=14, lh=18`），分别在单行 `Label`、折行 `Label`、只读 `TextBox` 上设 `SpanStyle{ .family = "CodeFamily", .foreground = ... }`，断言 `pos.y(Code) - pos.y(Base) == 3.0f ± 1e-3f`。 | 还原 `LineBoxGuardActive()` 只认 `size` 时，3 站点同时红（差值为 `0.0f`）。 |
| **T2（REQ-23 + REQ-24 真实 GDI 光栅探针）** | 真实 GDI 渲染下 `Noto Sans SC @ 15` + `Consolas @ 15` 基线共线 & 下伸无裁切 | 使用 `GDITextMeasurer` + `GDIBackend`（离屏 DIB 或真窗口探针）渲染含 `"gjpqy"` 与行内 ``"`code`"`` 的 `AutoSize` 单行/多行 `Label` 与 `TextBox`：末行底缘（如行尾再画一条贴底横线或读 DIB 像素包络）检测到下伸区第 `18..20` 行像素非背景色（不再于第 18 行被裁断），且中英文基线/墨迹底缘差 ≤ 1px。 | ① `Consolas @ 15` 纯文本：`AutoSize` 高度仍为 `18`；② `Microsoft YaHei @ 15` 纯文本：高度仍为 `20`（负对照不动）；③ 若临时把 `EffectiveBottomOverhang` 置 0，第 `18..20` 行墨迹立刻变空（恰红）。 |
| **T3（K9 居中陷阱守卫）** | `Center` 态与 `Button` 居中不反吃余量 | 对 `OverhangMeasurer`（`lh=18, asc=17, desc=4`, overhang=3）：① `Label::AutoSize()` 后 `pos.y == 0.0f`（而非 `1.5f`）；② `TextBox(VerticalAlignment::Center)::AutoSize()` 后 `GetTextTopInset() == pad`（而非 `pad + 1.5f`）；③ 固定高 `H=32` 居中时 `offsetY = (32 - 21) / 2 = 5.5f`。 | 还原 `DrawTextContent` 仅传 `lineHeight` 时 `pos.y` 报错读出 `1.5f`。 |
| **T4（多行滚动与行距守卫）** | 行步进不掺入 `overhang`，仅滚动总高计入 | ① `Noto Sans SC @ 15` 3 行折行 `Label`：`AutoSize()` 高度 = `54 + 3 = 57`，但 3 条 `DrawTextCommand.pos.y` 依次为 `0, 18, 36`（相邻差严格为 `18`，不变成 `21`）；② 固定高 `50` 的可滚 `TextBox`：`GetMaxScrollOffset() == 57 - 50 = 7`。 | 若误改 `LineHeight` 为 `21`，`pos.y` 差变为 `21` 且高变为 `63`，断言立刻报错。 |
| **T5（REQ-22 125% DPI 小数进位）** | 闸门 3ⓐ 进位策略与浮点噪声免疫 | ① `preferred.height = 20.8f`（如 125% DPI）经 `AutoSize()` 得 `21`（非 `20`）；② `preferred.height = 61.2f` 得 `62`（满足 `REQ-22`）；③ `18.00001f`（< `kCeilEps`）保持 `18`，`18.0f` 保持 `18`。 | 回退 `Widget::AutoSize` 为 `static_cast<int>` 时 `20.8f` 得 `20`、`61.2f` 得 `61`，恰红。 |
| **T6（GDI 缓存与失败注入）** | `LineHeight` / `TmMetrics` 缓存与 `T31-3` 安全 | `GDITextMeasurer` 连续查询 `LineHeight`/`Ascent`/`Descent` 后，注入失败 `SetOutlineTextMetricsForTests` 仍能命中回退分支且不读脏缓存；`kMaxMeasureCache` 越界不崩。 | 不清 `m_lineHeightCache` 时，`T31-3` 或注入失败用例变红。 |

---

## 8. 预算与影响面评估

- **头文件改动**：`ECDI/include/ECDI/Render/TextMeasurer.h`（+1 非纯虚带默认实现方法 `Descent`）、`ECDI/include/ECDI/Widget/TextWidget.h`（+1 `protected` 辅助 `EffectiveBottomOverhang`）、`ECDI/include/ECDI/Widget/TextBox.h`（+1 `protected/private` 辅助 `GetBottomOverhang`）。**公共头文件总数：102 → 102（无新增头）；CMake：0 改动。**
- **公共 API 增量**：`TextMeasurer::Descent(const Font&)` +1 默认虚方法；**既有派生类源码 100% 向后兼容**。
- **实现文件**：`GDITextMeasurer.h/.cpp`、`TextWidget.cpp`、`TextBox.cpp`、`Widget.cpp`（若闸门 3ⓐ），预计净增约 +80～+120 行。
- **测试套件**：在 `WidgetTests.cpp`、`TextBoxTests.cpp`、`TextMeasurerTests.cpp` 中预计新增 **5–6 条用例**（`608 → ~614`）+ 1 项真实 GDI 光栅探针或 headless DIB 用例。
- **验证要求**：MinGW / Clang / ClangCL / MSVC 四工具链 `ecdi_tests` 全绿 + harness 仓重编运行 `protocol/fixtures/hostile/seq-gap.jsonl` 与行内 code 夹具目视/像素双验收。

---

## 9. 修订记录

- **v1.0**（2026-11-15）：初稿落盘，完成 #91（`REQ-23`）、#92（`REQ-24`）、#93（`REQ-22`）现状对码核验、耦合机理分析、三道待用户拍板闸门（推荐 `1ⓐ + 2ⓑ + 3ⓐ`）、`△1`–`△7` 详细改动方案、`C1`–`C8` 契约与 `T1`–`T6` 测试/负对照计划。
- **v1.1**（2026-11-15）：用户拍板采纳推荐方案 **`1ⓐ + 2ⓑ + 3ⓐ`**，完成源码（`TextMeasurer.h`、`GDITextMeasurer.h/.cpp`、`TextWidget.h/.cpp`、`TextBox.h/.cpp`、`Widget.cpp`）与测试（`WidgetTests.cpp`、`TextBoxTests.cpp`、`TextMeasurerTests.cpp`）落地；对账更新 `Test44StyledSpansMeasurement`（粗体 `weight=700` 现按 `REQ-23` 计入行高 `24.0f`，纯 `foreground` 保持 `16.0f`）及 `Test45StyledSpansLineBoxMultiPhase ④` 注释，并新增 7 条测试（用例 `608 → 615`：`TextWidget.MixedFamilyBaselineAndGuard`、`TextWidget.OverhangCenteringAndWrap`、`Widget.AutoSizeCeilPrecision`、`TextBox.MixedFamilyBaseline`、`TextBox.OverhangInsetScrollAndPreferred`、`TextMeasurer.GdiDescentAndCacheContract`、`TextMeasurer.GdiOverhangInkRasterization`）。**验证结果**：清理原有产物后从头构建 `cmake-build-debug-mingw`（GCC 16.1.0）与 `cmake-build-debug-clang`（Clang 22.1.8，MSVC ABI），含 `ecdi_public_header_test` 均 0 错误且本次改动文件 0 告警；两条工具链全量运行 `ecdi_tests.exe` 均实测 **`615 passed / 0 failed / 615 total`**（**v1.2 订正**：v1.1 原记 `611 passed / 4 failed`，那 4 项系 agent 沙箱环境限制——`ChildProcess.TerminateTimeout` 因禁网致 `ping 127.0.0.1` 立即退出、`DesktopLayer.*` 因未开放 Win32 Z 序——在用户机上双链复核均全绿，**非代码问题**）；全部 `TextWidget`、`TextBox`、`TextMeasurer`、`Widget` 与新增 7 条用例全绿；MSBuild（`cmake-build-vs`）在沙箱内未能启动构建；外部 harness 仓复验待后续独立构建运行。
- **v1.2**（2026-11-15）：**外部评审（Miao）验收 + 裁决 ⓑ 落账**。① ★★ **`I44-7` 契约破坏显式登记**——Phase 44 不变量「行高恒等于基字体行高、绝不受 span 污染」自本批**作废**，新口径 = `max(LineHeight over 全部非空 run)`；`Test44StyledSpansMeasurement` 断言由硬编码 `16.0f`（v1.1 曾改为硬编码 `24.0f`）改为**按 max 口径复算** `(std::max)({hBase, hBold, hMono})`，并在注释中标明该假件的 `24.0f` 是**故意毒饵**而非真实行为。② **闸门 1ⓐ 代价补实测**（§4 新增「★ 代价①的实测校准」）：五族 × weight × italic 共 20 组，`LineHeight` 只随 `family` 变；真实污染上限 1px（YaHei@16 粗体 `tmDescent 4→5`）。③ **T2 补 harness 现场方向 ④**（基 `Noto Sans SC` + span `Consolas`，覆盖 REQ-23 原始 REPRO；v1.1 的 ③ 为反向）。④ **注释订正两处**：`TextWidget.h` 的 `LineBoxGuardActive` 仍写「size 覆盖」（旧口径残留）、`TextMeasurer.h` 的 `Descent` 注释 `tmDescent`/`tmHeight` 的 `t` 被写成制表符（`\t` 泄漏）。⑤ **验证记录订正**：v1.1 的 `611 passed / 4 failed` → 实测 **`615 / 0 / 615`**（那 4 项为 agent 沙箱环境限制）。⑥ `doc_lint`：`tests=615 headers=102 docs=220`；`g++ -fsyntax-only` 两 TU 零告警。
