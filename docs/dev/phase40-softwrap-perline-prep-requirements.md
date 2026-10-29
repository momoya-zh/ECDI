# Phase 40 · 折行支每行「整串预备」O(L·N) 的消除（deferred #83 ← #78 性能尾巴）

> 状态：**v1.4**（2026-10-29 **实施收口**：新增 §5.1 验收实测——A40-1..7 逐条结论，四构建链 **546 / 546 全绿**，R40-3 负对照 544/2 后还原全绿；§8 的 v1.4 条目登记三条判据边界）
> **基线**：HEAD `a377026`（`docs(phase39): batch 2 -- requirements v1.2, roadmap v2.61 (#78 done), README anchors`）
> **账目**：`roadmap-deferred.md` **#83**（v2.61 已登记为「#83 ②-附 = #78 的性能尾巴·折行支 O(L·N)」，Phase 39 D39-3 明确不并入、留本 Phase）
> **继承**：本稿是 Phase 39 需求稿 §1.2（B）/ §2（K7–K9）/ §6（建议新立 #83）的**收口与扩面**——所有行号已按 HEAD `a377026` 重新亲核（P39-R1/R2 改过 `TextBox.cpp` / `TextMeasurer.h` / `UTF8.h` / `UTF8.cpp`，K7/K8/K9 在 `TextBox.cpp` 内 +5 漂移已订正；B 的 `FitText` 接缝未被 Phase 39 触及故行号不变）。
> ★ **事实纪律（本稿自约束）**：行号 = 2026-10-29 对 HEAD `a377026` 逐条 `grep -n` 亲核，不转抄 Phase 39 原文行号。
> ★ **数字纪律（沿 Phase 28 / 38 / 39）**：只承诺**复杂度** + **分配次数** + 改前/改后**记录制读数**；不写「提升 X 倍」口号、不设性能阈值。
> ★ **诚实边界**：Phase 39 探针读数（表 1/2/3）测的是 `Core/UTF8` 的换算/解码函数，本 Phase **不碰这些函数** ⇒ 读数**仍然成立**，本稿在 §1.3 直接继承并重引。

---

## §1 输入与问题陈述

### 1.1 Phase 39 留下的尾巴（逐条继承 + 重核）

Phase 39（#78）只消除了**非折行支单趟循环**的 O(N²)（P39-R1），并明确把下面三条作为**同族尾巴**留给本 Phase（#83）：

- **B**（Phase 39 §1.2）：折行时 `FitText` **每行一次整串预备** ⇒ O(L·N)。
- **K7 / K8**（P39-N2）：`OnPaint` / `DrawTextContent` 的**每行整串换算** ⇒ 与 B 同量级、同解；Phase 39 原话「单修一节等于拆半」。
- **K9**（Phase 39 §6 附注）：`CaretIndexFromLineX` **行内逐码点换算** ⇒ 非折行长单行下 O(N²) + N 次 `MeasureText`，属同族。

本稿把三者**收口为同一根因下的同一修复**（详见 §1.2 与决策 D40-1）。

### 1.2 ★ 统一根因：跨「行 / 交互」的整串级换算被反复以 O(X) 调用

现网四处站点（B / K7 / K8 / K9）的共同本质：

- `CodepointIndexToByteOffset(text, X)` 从 0 逐字节推进到第 X 个码点 ⇒ **O(X)**（`Core/UTF8.cpp:35-43`）；
- `ByteOffsetToCodepointIndex(text, X)` 同理 O(X)（`:45-53`）；
- 它们被以 **X 跨全串**反复调用——**每行一次**（B / K7 / K8）、**每次交互一次**（K9 端点）、**行内逐码点一次**（K9 循环）。

⇒ 在「L 行 × 平均绝对码点位置 O(N)」下，总代价 **O(N²)**。B 的 O(L·N)、K7/K8 的每帧 O(L·N)、K9 非折行长单行的 O(N²) **全部源自同一处**：没有一次性偏移表，于是每次都从串首重扫。

★ **关键观察（重核 `BuildTextLayout` 后新增）**：`TextWidget.cpp:211` 在 `BuildTextLayout` 里**已经** `DecodeUtf8(text)` 整串解码一次并持有 `cps`（`:242` 用它扫 `\n`）——但 `FitText`（`TextWidget.cpp:270`）不去复用这个 `cps`，反而让 FT 在 `FontEngine.cpp:529` 又 `DecodeUtf8(text)` 一遍、让 GDI 在 `GDITextMeasurer.cpp:153` 又 `ByteOffsetToCodepointIndex(text, text.size())` 数一遍。**这是 B 的真冗余源**：同一个串、同一个 `totalCp`，被 L 次重建。⇒ 偏移表与既有 `cps` **同趟产出、一次构建、四站共用**，正是消除该冗余的干净形态。

### 1.3 现状量化（继承 Phase 39 §1.3，读数仍成立）

表 1（非折行支循环，已被 P39-R1 消除，仅作对照）、表 2（单点换算线性度）、**表 3（B 的每行固定成本，本 Phase 目标）**：

| 量 | 读数（20k CJK，mean 200） | 备注 |
|---|---|---|
| `ByteOffsetToCodepointIndex(text, size)` | **13.8–14.9 µs** | GDI / FT `FitText` 每行各一次 ⇒ L≈700 下 ≈ **10.4 ms** |
| `DecodeUtf8(text)` | **38.7 µs + 1 次分配**（reserve 3N = 240 KB） | FT 每行一次 ⇒ ≈ **27 ms** + **168 MB churn** |
| ★ GDI 尾串拷贝 `UTF8ToWide(text.substr(byteStart))` | **未计入（下界）** | 拷贝**整条尾部**（byteStart→end），20k 串早期行 ≈ 60 KB/行 × 700 ≈ **42 MB memcpy / 次重建** |

★ 口径重述（与 Phase 39 §1.2 一致，重核后 B 接缝行号不变）：行宽 ~400 DIP / 14px ≈ 28.6 字/行 ⇒ **L ≈ 700**（区间 625–770）；报区间不报单点；尾拷贝未计入 ⇒ 表 3 是**下界**。

### 1.4 触发面与用户可感知后果（扩面到 K7/K8/K9）

| 场景 | 走的站点 | 每次代价（20k CJK） |
|---|---|---|
| 折行长文本**每次按键重建** | B（`FitText` ×L） | GDI ≈ 10 ms / FT ≈ 27–30 ms（FT 多一次整串 `DecodeUtf8`）+ 150–185 MB churn（下界，尾拷贝未计） |
| 折行长文本**每帧重绘** | K7（`OnPaint` × 可见行）+ K8（`DrawTextContent`） | 每行 1–4 对 O(N) 换算 ⇒ 全量重绘 O(L·N) ≈ 同量级 |
| 非折行长单行**点击一次定位** | K9（`CaretIndexFromLineX`） | 行内 O(N²) + N 次 `MeasureText`（非折行时行长 = 整段 = 20k） |
| 短文本（聊天框） | 全部 | 可忽略（< 0.1 ms） |

★ **为何「单修 B 等于拆半」（继承 P39-N2 + 附注）**：折行长文本卡顿 = 重建（B）+ 重绘（K7/K8）两条链路同时付 O(L·N)；只修 B，帧率仍被 K7/K8 拖死。⇒ 本 Phase 把 B + K7 + K8 + K9 作为**同一根因下的同一修复**一并收口。

---

## §2 现状勘察（S1–S8 本 Phase 自勘；B / K7 / K8 / K9 继承 Phase 39；本 Phase 新立 K13）

S1–S8 为本 Phase 自勘站点（与 Phase 39 的 B / K7 / K8 / K9 编号体系**互不重叠**）；B / K7 / K8 / K9 保留 Phase 39 继承语义（见 §1.1 / §1.2）；**K13** 为本 Phase 新立（布局 owner 与引擎共用，见 S5）。

| # | 勘察项 | 事实（行号已重核） |
|---|---|---|
| **S1** | `FitText` 的 L 次整串预备（B） | `TextWidget.cpp:270` `measurer.FitText(font, text, scan, limit)` 每段迭代一次，传**整串** `text`（`scan` = 全串绝对码点索引）；`text` 即 `:211` `DecodeUtf8` 已解的同一串。生产实现：GDI `GDITextMeasurer.cpp:148`（`:153` 数 `totalCp`、`:170-171` 算 `byteStart` + 尾串 `UTF8ToWide`）；FT `FontEngine.cpp:505`（`:509` 数 `totalCp`、`:529` `DecodeUtf8` 整串）；fallback `TextMeasurer.h:68`（`:77` 数 `totalCp`）；无 engine 兜底 `FreeTypeTextMeasurer.cpp:48`（`ByteOffsetToCodepointIndex`）。 |
| **S2** | K7：`OnPaint` 每行换算（TextBox 路径） | `TextBox.cpp:1489-1490`（主）、`:1502-1503`（选区，仅当有选区）、`:1526-1527`（组合下划线，仅当组合）各一对 `CodepointIndexToByteOffset(textSource, X)`，`textSource` = `m_text`（Normal 零拷贝）或 `BuildDisplayText()`（echo）（`:1450`）。每可见行 × 最多 4 对 ⇒ 全量重绘 O(L·N)。 |
| **S3** | K8：`DrawTextContent` 每行换算（Label / Button / StateWidget 路径） | `TextWidget.cpp:405-406` 每视觉行一对 `CodepointIndexToByteOffset(m_text, line.startCp / +cpCount)`；`:229-230` `emitLine` 回退分支（仅 `end != fitEnd`）再一对。`emitLine` 的 `lineStart`/`end` 来自 `:211` 已解的 `cps`，本就可 O(1)，但当前仍走 O(X) 换算。 |
| **S4** | K9：`CaretIndexFromLineX` 行内逐码点 | `TextBox.cpp:890`：`display = BuildDisplayText()`（`:893`）；`:895-896` 两端 `CodepointIndexToByteOffset(display, lineStartCp/lineEndCp)`（各 O(N)）；`:899` `ByteOffsetToCodepointIndex(lineText, lineText.size())`（O(行内)）；`:905` / `:910` 行内循环每次 `CodepointIndexToByteOffset(lineText, i[+1])` + `MeasureText(lineText.substr(0, byte))`。非折行长单行 ⇒ 行内 = 整段 = 20k ⇒ O(N²) + N 测量。 |
| **S5** | ★ 新增 **K13**：布局 owner 与引擎共用 | `TextBox::RecalculateLines` 折行支 `:466` `m_lines = BuildTextLayout(*measurer, BuildDisplayText(), wrapWidth).lines;` —— **TextBox 折行也走 `BuildTextLayout`**（与 Label `TextWidget.cpp:342` `m_wrapLayout = BuildTextLayout(...)` 同一引擎）。非折行支 `:475-496` 是 P39-R1 的单趟循环（已 O(N)，不依赖偏移表）。⇒ 偏移表只需在 `BuildTextLayout`（wrap）与「显示串准备」两处构建，`TextBox` / `TextWidget` 共用基类 `TextWidget`（`TextBox: public TextWidget`；`Label` / `Button` / `StateWidget: public TextWidget`）。 |
| **S6** | `DecodeUtf8` 的 3× 过预留（P39-N6 / K9 账） | `ECDI/src/Render/Utf8Decode.h:20` `out.reserve(utf8.size())` ⇒ CJK（每码点 3 字节）实际只需 N 容量却留 3N；FT 每 `FitText` 一次 ⇒ L 次 × 240 KB churn。 |
| **S7** | 编译与规模面 | 改 `TextMeasurer.h` / `TextLayout.h` / `UTF8.h`（公共头）⇒ 几乎全部 TU 重编（Phase 39 K12：冷全量 35 min 量级）；若新 `DecodedText` 类型入 `UTF8.h`，与 Phase 39 D39-7 同理须与任何其它头改动**同批编一次**。 |
| **S8** | 测试面能否守住「逐位相同」 | 现有 **538** 条用例；`FakeTextMeasurer` / `CountingFakeMeasurer`（`WidgetTests.cpp:650` 显式转发默认体）/ `kWrapSamples`（23 行）/ `RowCountForTest` / `OwnForTest` / `DrawForTest` 可直接复用；golden `TextFit` 常量表可用批 0 纪律采。⇒ 行为判据可直接复用。 |

（B / K7 / K8 / K9 的完整读数见 Phase 39 §1.3 表 1–3，本稿继承，未重测——探针测的是未改动的 `Core/UTF8` 函数。）

---

## §3 范围与非目标

### 范围（R1–R4）

- **R1（主体 · B）** `BuildTextLayout` 构建**一次性** codepoint↔byte 偏移表（★ v1.2：**两趟**——`DecodeUtf8` 产 `cps` + `BuildByteOffsets` 产表，**非**同趟：两套解码语义不能混，见 D40-J），`FitText` 改用该表：`totalCp` 取 **`byteOffsets.size() - 1`**（★ v1.2 订正，原写 `cps.size()`——脏字节上二者不等，见 D40-E/M3）、`byteStart` / `byteEnd` 取表项（O(1)）、FT 复用传入 `cps`（**不再** `DecodeUtf8` 整串）、GDI 仅转**倍增窗口**（**不再** 尾串整拷，见 D40-D）。行表（`startCp` / `cpCount` / `width` / `ownedEnd` / `totalHeight`）**逐位不变**（含 overlong 同源，见 A40-7）。
- **R2（K7 / K8 · paint）** `OnPaint`（TextBox）与 `DrawTextContent`（TextWidget 派生）的每行 `CodepointIndexToByteOffset(textSource, X)` 改为表项 O(1) 查表；`lineText` 子串逐位不变。
- **R3（K9 · hit-test）** `CaretIndexFromLineX` 的 `:895-896` / `:899` / `:905` / `:910` 换算全部改表项查表；行内 `MeasureText` 前缀循环**保留**（测量不可省），仅字节偏移免费。
- **R4（P39-N6 顺手）★ v1.2 de-scope（详设 D40-M）** ~~`Utf8Decode.h:20` 的 `reserve(utf8.size())` 3× 过预留~~ —— **本 Phase 不动 `Utf8Decode.h`**。理由：`reserve(utf8.size())` 是**上界估计**（CJK 实际只需 N 容量，收紧须多跑一趟 O(N) 计数才能得精确 cap），其危害**全部来自「L 次重复」**；而 R1 的 D40-E 删掉 FT 的 L 次 `DecodeUtf8` 之后，剩余调用点只有 `BuildTextLayout:211`（每 rebuild 1 次）与 `FontEngine::MeasureText:474`（`measureCache` 命中，`:497-501`）⇒ **无放大**，不构成同族性能问题。★ 这是**范围变更**，已获用户显式授权（2026-10-29）。

### 非目标（N1–N7）

| # | 不做 | 理由 |
|---|---|---|
| **N1** | 给 `Core/UTF8` 加「码点→字节」随机访问索引表并**全局缓存** | YAGNI 解除见 D40-6：本 Phase 的偏移表是**布局/绘制本地**的一次性结构（跟随显示串 revision 失效），不是全局缓存；若初设发现需全局共享再议。 |
| **N2** | 改 `FitText` 既有 4 参签名（破坏自定义 measurer 零改动契约，`TextMeasurer.h:62-67`） | 表感知路径走**纯新增重载 / 默认参**（D40-7），既有签名与默认 fallback 一字不动。 |
| **N3** | 非折行 `RecalculateLines` 单趟循环（P39-R1，已 O(N)）改用偏移表 | 已最优；表由「显示串准备」独立构建（D40-3 / D40-8），非折行重算不依赖它。 |
| **N4** | `BuildDisplayText()` 整串拷贝（K11 类，20k≈2 µs） | 比目标小数量级；不在本链路热点。 |
| **N5** | 单点换算（caret / 选区 / 命中 / IME 锚点的一次 O(N)，≤ 13 µs @20k） | 每次交互一次，非放大项（同 P39-N5）。 |
| **N6** | 性能阈值式「X 倍」承诺 | 沿 Phase 28 / 39 纪律：只承诺复杂度 + 分配次数 + 记录制读数。 |
| **N7** | 动 CMake 目标 / 新增源文件 | 偏移表构建落 `UTF8.cpp`（既有 TU）或 `BuildTextLayout` 内联；无新增 `.cpp`。 |

---

## §4 决策点 D40-1..9（倾向 ≠ 定案；初设必答）

| # | 决策 | 倾向 | 理由 / 初设必答 |
|---|---|---|---|
| **D40-1** | 范围：B 仅 / B+K7+K8 / +K9 | **B+K7+K8+K9 全收** | 同一根因（无一次性偏移表）+ 同修复（表 + 查表）；「单修 B 等于拆半」（P39-N2 / 附注）。K9 非折行长单行是 O(N²) 同族，纳入。 |
| **D40-2** | 架构：ⓐ measurer 内按 `(data,size)` 缓存 / ⓑ 控件持有偏移表并随 `DecodedText` 视图传入 `FitText`、paint / hit-test 复用 / ⓒ 自由函数表助手 | **ⓑ** | ⓐ 只解决 B，**不解决 K7/K8/K9**（后者调的是自由函数 `CodepointIndexToByteOffset`，不走 measurer）；且引入「同串反复传入」隐式假设（失效风险）。ⓑ 一处构建、四站共用，干净闭合。 |
| **D40-3** | 表构建与存储位置 | **`TextWidget` 基类持有**（`DecodedText` / `CpByteTable` 成员），由「显示串准备」入口（`RecalculateLines` 非折行外的统一点 / `BuildTextLayout` wrap）构建，**失效键 = `TextBox::LineKeyMatches`（`TextBox.cpp:509`）实际比较 12 项**——`font.size/family/weight`（3）+ `m_isComposing/m_compositionStart/m_compositionLength`（3）+ `revision`/`dpi`/`wrapWidth`/`hasMeasurer`/`wrapRequested`/`echo`（各 1）；本稿原 6 高层键**漏 `hasMeasurer`/`wrapWidth`**，须补全。并须 ⊇ `TextWidget::GetTextLayout` 的 wrap 指纹六元组（`TextWidget.cpp:332`：`revision`+`font`×3+`dpi`+`width`，Label 无 composing/echo/hasMeasurer）。**`dpi` 算式在基类只算一次、两缓存共用**（回答 `TextBox.cpp:511-512`「逐字同源」警告），与 `m_lines`/`m_wrapLayout` 同批计算 | `TextBox` / `Label` / `Button` / `StateWidget` 皆派生自 `TextWidget`（S5），基类持有即全覆盖；paint / hit-test 需表时键若变化则同批重建（不得「行有效、表失效」半状态）。 |
| **D40-4** | GDI 尾拷贝修法 | **窗口长度截取**：`UTF8ToWide(text.substr(byteStart, byteEnd - byteStart))`，`byteEnd` 取表项 O(1) | 须证明不再为「整尾」拷贝（`:171` 现拷贝 byteStart→end）；且 `byteEnd` 计算不得 re-introduce O(N)（用表即 O(1)）。 |
| **D40-5** | FT 冗余解码修法 | **复用传入 `cps`**：`FontEngine::FitText` 不再 `DecodeUtf8(text)`（`:529` 删），改索引传入的 `cps`（从 `startCp` 起）；`reserve` 过预留随 P39-N6 消除 | 须证明 FT 的 `cps[i]` 与 `BuildTextLayout:211` 的 `cps` **逐位相同**（同源）；缺字形消费语义（`:539` 注释）保持。 |
| **D40-6** | YAGNI：P39-N3 拒过的偏移表，本 Phase 是否解除 | **解除** | N3 拒的是「单趟非折行循环根本不需要随机访问」；本 Phase 有 **≥4 个真实消费者**（B×L、K7×L、K8×L、K9×1）且 wrap 引擎**本来就在 `BuildTextLayout` 解 `cps` 一次**——边际成本 O(N) 一次替换 O(N²) 总代价，是「第二次（第 N 次）真实消费者出现」的教科书情形。 |
| **D40-7** | 自定义 measurer 兼容 | **既有 4 参 `FitText` 签名 + 默认 fallback 一字不动**；表感知路径 = **纯新增重载**（如 `FitText(font, text, startCp, maxWidth, const DecodedText&)`）或默认参；现有 `FakeTextMeasurer` / `CountingFakeMeasurer`（`WidgetTests.cpp:650` 显式转发默认体）零改动 | `TextMeasurer.h:62-67` 明确保证「自定义 measurer 零改动即可工作」⇒ 破此契约需你授权；本 Phase 不破。 |
| **D40-8** | 非折行 `RecalculateLines`（P39-R1 已 O(N)）与表的关系 | **表独立构建、非折行重算不消费表**；但 paint / hit-test 在**非折行**下仍需表（K7 / K9 非折行态同样走 `textSource` / `CaretIndexFromLineX`）⇒ 表由「显示串准备」统一入口构建，与 wrap 分支无关 | 初设须给：表构建入口在哪、非折行时如何零成本拿到表（revision 命中即复用）。 |
| **D40-9** | 公共头改动编译代价 | `DecodedText` / `TextLayout.byteOffsets` 若入 `UTF8.h` / `TextLayout.h` ⇒ 近全量重编（K7）；须与任何其它头改动**同批编一次**，别分两轮验 | 初设给：哪些头改、是否触发全量、批 0 采集与编译批次绑定。 |

---

## §5 验收 A40-1..7

| # | 判据 | 形态 |
|---|---|---|
| **A40-1** | **行表逐位相同**：现有 **538** 条用例全绿（重点：Phase 38 T38-1..18、Phase 29 T29-WRAP-*、多行 / IME / Undo / AutoSize、Phase 39 新增的 overlong `U+000A` 族）。wrap / 非折行 / `m_lines` / `m_wrapLayout.lines` 三处表口径不变。 | 测试 |
| **A40-2** | **分配 churn 归零**：wrapped 长文本「一次重建 + 一次全量重绘」期间，FT 的每 `FitText` `vector` + `reserve(3N)` 分配 → **0**；GDI 尾 `substr` 拷贝 → 窗口长度（不再整尾）。 | 探针（全局 `operator new` 计数包住重建 + 重绘） |
| **A40-3** | **复杂度**：wrapped 重建（B）与全量重绘（K7）的「n → 4n」耗时比从 ≈ L·N 降到 ≈ N（总 O(N)）；K9 非折行长单行点击从 O(N²) 降到 O(N) + 行内测量。 | 探针四档（1k / 5k / 10k / 20k），记录制 |
| **A40-4** | **FitText 输出等价**：同批 `(text, startCp, maxWidth)` 下，改前/改后 `TextFit{fitCp, width}` 逐位相同；基线常量 = **批 0** 在 HEAD 上先采（沿 Phase 38 / 39 批 0 纪律）。 | 测试 + 批 0 常量表 |
| **A40-5** | **机检（函数体口径，批 0 先采 baseline）**：`CodepointIndexToByteOffset` / `ByteOffsetToCodepointIndex` 在四个目标函数体内的出现次数，沿**函数签名锚定**（`awk '/^<Sig>/{f=1} f&&/^}/{f=0} f'` 截取函数体再 `grep -c`），**不锚行号**（全文件口径会差很多倍，沿 Phase 39 A4）。**HEAD 基线（本稿亲采，2026-10-29，a377026）**：`BuildTextLayout` = 2（`:229-230` emitLine 回退）、`DrawTextContent` = 2（`:405-406`）、`OnPaint` = 6（`:1489-1490 / :1502-1503 / :1526-1527`）、`CaretIndexFromLineX` = 5（`:895-896 / :899 / :905 / :910`）；**目标 = 0**（四函数体内不再出现该 O(X) 换算，全部改表项 O(1) 查表；初设若保留断言性调用须在批 0 显式备案）。**批 0 纪律**：baseline 须在 HEAD 上、改代码前采完并随机检脚本同批入库；采不到不得动 R1。 | 机检 C40-x |
| **A40-6** | **K9 命中等价**：对 wrap / 非折行两模式下一组 `(lineIndex, innerX)` 样本，`CaretIndexFromLineX` 返回码点索引逐位相同。 | 测试（样本表驱动） |
| **A40-7** | **跨支同源（overlong `U+000A`）**：偏移表构建须复刻 `DecodeFirstCodepoint` / `DecodeUtf8` 的 overlong 接受语义（`C0 8A` / `E0 80 8A` / `F0 80 80 8A` 仍断行），与 `BuildTextLayout:243` 的 `cps[i] == U'\n'` 判据**逐位一致**——表与 `cps` 是**两趟**（v1.2）但必须同源，须断言（防初设分叉）。★ **v1.3 补样本类**：样本集**必须覆盖 `cps > count`** 的「**合法前导 + 非法续字节**」形态（`"\xE0\x41\x41"` → `cps=2 / count=1`；`"\xF0\x41\x41\x41"` → `cps=3 / count=1`，详设探针实证）——旧样本（`"\x80\x8A"` / `"A\xC0"` / `"\xC0\x41\x41"`）**全部满足 `cps ≤ count`**，测不出消费侧的表索引越界。 | 测试（overlong + 脏字节样本表） |

### 5.1 验收实测（2026-10-29 收口，四构建链）

| # | 结论 | 证据 |
|---|---|---|
| **A40-1** | ✅ **通过** | **四构建链 546 / 546 全绿**（538 存量 + 8 新用例），行表逐位不变 |
| **A40-2** | ✅ **通过（算法层 + 机检，非端到端计时）** | 探针复刻路径：表版 **1 次分配 / 240 KB** vs 改前 **715 次 / 171.6 MB**（20k CJK）；FT 侧不再整串 `DecodeUtf8`、GDI 侧改倍增窗口。★ **诚实边界**：未做产品端到端 `operator new` 计数（paint 的 `substr` 主源本 Phase 不改） |
| **A40-3** | ✅ **通过（算法层）** | 探针 5000 → 20000：改前 **18.6×**（超线性）/ 表版 **4.1×**（线性）；20k CJK 47.395 ms → **0.037 ms**。★ 不承诺 X 倍，只记读数 |
| **A40-4** | ✅ **通过** | `Text.FitText.TableAwareEqualsLegacy`（4 参 vs 5 参逐位，含 `kFitGolden` 8 组 + 两类 `cps > count` 脏串）；GDI 侧另由 `Text.FitText.GdiWindowedEqualsLegacy` 覆盖 |
| **A40-5** | ✅ **通过** | 四个目标函数体内换算 **2 / 2 / 6 / 5 → 0 / 0 / 0 / 0**（命令须排除注释，见详设 §6.2.2 的 I-2） |
| **A40-6** | ✅ **通过（但判据面收窄，如实登记）** | `Text.Caret.LineXBitIdentical` 在**无窗口**下只能验「行首 + 极端 X 不越界」——行内 X 落点需真窗口，列 R40-7 人工核对项，本 Phase 不假装验了 |
| **A40-7** | ✅ **通过** | `Text.Wrap.OverlongSameSource`（三档 overlong 仍 2 行）+ `Text.CpByteTable.MatchesSequenceLength` 的两类 `cps > count` 样本 + `Text.Wrap.LineTableBitIdentical` 的脏字节 × 5 档折行宽 |

★ **R40-3 负对照（判据有牙的唯一证明）已做**：把 `BuildByteOffsets` 的步进临时换成 `DecodeUtf8` 语义 ⇒ **`544 passed / 2 failed / 546`**，红的是 `Text.CpByteTable.MatchesSequenceLength` 与 `Text.Paint.WrapBytesBitIdentical`；还原后 **546 全绿**。

---

## §6 影响面

| 项 | 预估 |
|---|---|
| 源码 | **9 个文件**（★ v1.2 订正：原列 8 个且把 `Utf8Decode.h` / `Core/UTF8.cpp` 算进改动面——两者实际**零改动**；漏了 `Widget/TextWidget.h` 与 `Widget/TextBox.h`）：`ECDI/include/ECDI/Widget/TextWidget.h`（表设施 +4 protected）· `ECDI/include/ECDI/Widget/TextBox.h`（`CurrentByteMapKey` override 声明）· `ECDI/include/ECDI/Render/TextMeasurer.h`（+5 参 `FitText` + 默认体）· `ECDI/src/Widget/TextWidget.cpp`（`BuildByteOffsets` + 三设施 + `BuildTextLayout` 局部表 + `DrawTextContent` 查表 · `:211 / :270 / :405-406`）· `ECDI/src/Widget/TextBox.cpp`（override 定义 + `OnPaint` `:1489-1490 / :1502-1503 / :1526-1527` + `CaretIndexFromLineX` `:895-896 / :899 / :905 / :910`）· `ECDI/src/Render/GDITextMeasurer.{h,cpp}`（+5 参、倍增窗口）· `ECDI/src/Render/FontEngine.{h,cpp}`（+5 参、复用 `cps`、删 `:529`）· `ECDI/src/Render/FreeTypeTextMeasurer.{h,cpp}`（+5 参转发） |
| 公共头 | **100 → 100**（实测 `git ls-files 'ECDI/include/*.h'` = 100；无新增头文件；`GDITextMeasurer.h` / `FontEngine.h` / `FreeTypeTextMeasurer.h` 均在 `src/Render/` = src 私有头，不计入） |
| public API | **+1**（表感知 `FitText` 重载；既有 4 参签名不动、默认 fallback 不动） |
| CMake | **0**（无新增源文件） |
| 用例 | **538 → 546（+8，详设定稿；★ v1.2 定数，原写 ~546）**：T40-1 表项等价 · T40-2 `FitText` 4 参 vs 5 参逐位 · T40-3 行表逐位 · T40-4 命中逐位 · T40-5 overlong 同源 · T40-6 paint 字节逐位 · T40-7 GDI 窗口化自比 · T40-8 表随键重建 |
| docs | **201 → 204**（★ v1.2 订正：本文档 202 · 初设 203 · 详设 204；实测 `git ls-files 'docs/*.md'` = **201** 已含 Phase 39 三份） |

---

## §7 初设闸门（移交初设的必答项）

1. **偏移表结构形状与单趟构建**：`DecodedText{ const std::string& text; const std::vector<char32_t>& cps; const std::vector<uint32_t>& byteOffsets; }`？`byteOffsets[i]` = 第 i 个码点首字节偏移、`byteOffsets[N] = text.size()`？构建 = 单趟扫 `text` 同时填 `cps` 与 `byteOffsets`（O(N)），**必须逐位复刻** `Core/UTF8.cpp` 的 `SequenceLength`（非法前导按 1 字节）/ `DecodeFirstCodepoint`（剩余长度不足回退 `lead`）/ overlong 接受（`U+000A`）语义（A40-7 基础）。
   ★ **v1.2 已答（详设 §1.1 / §1.2）**：**不引入 `DecodedText` 新类型**（D40-K）——5 参 = `cps` + `byteOffsets` 两个独立参数；表结构 = `std::vector<std::uint32_t>`，`m_cpByteOffsets[k]` = 第 k 码点首字节、`size() == count+1`、末项 `== text.size()`（末段截断时刻意可**越过** size，与 `CodepointIndexToByteOffset` 同形）；构建 = **两趟**（`DecodeUtf8` 产 `cps` + `BuildByteOffsets` 产表，D40-J），表用 `DecodeCodepointAt` 步进（其返回值即 `SequenceLength`，**跨 TU 契约** R40-12）、逐位等价由 T1 + T40-1 守。
2. **`FitText` 重载落点**：新增重载签名；`totalCp` / `byteStart` / `byteEnd` 如何从表 O(1) 取；FT 路径如何索引传入 `cps`（从 `startCp` 起，保持 `:539` 缺字形消费语义）；GDI 路径 `byteEnd` 窗口截取（D40-4）。默认 fallback（`:68`）**不接表**、保持原样（自定义 measurer 零改动）。
3. **表存储与失效键（同源收口）**：`TextWidget` 基类成员形状；**失效键 = `TextBox::LineKeyMatches` 12 项（见 D40-3）与 `TextWidget::GetTextLayout` wrap 指纹六元组的并集**（paint / hit-test 非折行态也需表，故键须含非折行显示串的 revision/echo/composing 分量，独立于 wrapWidth）；**`dpi` 算式在基类只算一次、两缓存共用**（回答 `TextBox.cpp:511-512`「逐字同源」警告）；证明「行有效 ⇒ 表有效」或二者同批计算（无半状态）。
   ★ **v1.2 已答（详设 §1.1 / §1.4 / §0.1-D-1）**：**键收窄为 5 维**（revision / echo / composing×3，D40-C）——`byteOffsets` 是纯 UTF-8 **结构量**，与 font / dpi / wrapWidth / hasMeasurer / wrapRequested **全无关**（这些只影响**宽度**），故「12 项并集」是对口径的过度保守；**`dpi` 不进键 ⇒ 「基类只算一次」这条要求对表不适用（更彻底：不需要算）**。★ 5 维的**充分性前提**是「单写者」（D40-L）：表只由 `EnsureCpByteOffsets` 写、`BuildTextLayout` 只用局部表，命中时做 O(1) 串长自检 ⇒「键唯一决定串」。「无半状态」由「单写者 + 行表各自键独立有效」保证（详设 §1.2b）。
4. **K7 / K8 paint 重写**：`OnPaint` / `DrawTextContent` 的 `CodepointIndexToByteOffset(textSource, X)` → `table.byteOffsetOf(X)`；`lineText` 子串逐位不变证明（特别是选区 / 组合求交的 `hlStartByte` / `ulStartByte` 相对偏移）。
5. **K9 重写**：`:895-896` / `:899` / `:905` / `:910` 全部改查表；行内 `MeasureText` 前缀循环保留；命中 cp 索引逐位相同证明（wrap / 非折行两模式）。
6. **分配 churn 探针形态**：全局 `operator new` 计数包住「wrapped 20k 重建 + 全量重绘」，断言 FT `vector` 分配与 GDI 整尾 `substr` 归零；命令 + flag + 机器入档（记录制，不设阈值）。
7. **批 0 基准**：A40-4 的 `TextFit` 常量表须在 HEAD 上、改代码前采完；采不到不得动 R1。
   ★ **v1.2 已答（详设 §0.1-D-5 / §6.1）**：**无需新采**——`kFitGolden`（`TextBoxTests.cpp:147`，8 组含 `startCp≠0` / overlong / 越界 / 空串）已是 HEAD 行为快照（P39 批 0 采、P39 收口后 4 参行为未变），A40-5 基线 **2/2/6/5** 已实采入库（详设 §5）。⇒ 独立批 0 合并为「**批 1 = 基础设施（零行为变化）** / **批 2 = 切换消费**」；★ 但**保留批 0 的实质纪律**：批 1 提交前先跑一次 538 确认基线绿，否则不开始。
8. **编译批次**：哪些公共头改（`UTF8.h` / `TextLayout.h` / `TextMeasurer.h`）⇒ 是否触发近全量；与任何其它头改动同批编一次（D40-9）。

---

## §8 修订记录

- **v1.0（2026-10-29）** 立项 + 需求稿落盘。继承 Phase 39 需求稿 §1.2（B）/ §2（K7–K9）/ §6（建议新立 #83）并**收口扩面**：① 全部行号按 HEAD `a377026` 重新亲核（P39-R1/R2 改过 `TextBox.cpp` / `TextMeasurer.h` / `UTF8.h` / `UTF8.cpp`，K7/K8/K9 在 `TextBox.cpp` 内 +5 漂移已订正，B 的 `FitText` 接缝未被 Phase 39 触及故行号不变）；② 重核 `BuildTextLayout`（`TextWidget.cpp:211` 已整串 `DecodeUtf8` 一次却未被 `FitText` 复用）⇒ 新增 **K13**（布局 owner 与引擎共用，TextBox 折行也走 `BuildTextLayout`）；③ 明确 #83 范围 = **B+K7+K8+K9 同根因同修复**（「单修 B 等于拆半」）；④ 决策点 D40-1..9（架构 ⓑ 控件持有偏移表、YAGNI 解除 D40-6、自定义 measurer 兼容 D40-7）；⑤ 验收 A40-1..7（含跨支 overlong 同源 A40-7）；⑥ 影响面 / 初设闸门落档。Phase 39 的探针读数（表 1–3）因测的是未改动的 `Core/UTF8` 函数，本稿直接继承。

- **v1.1（2026-10-29）** 收 Qoder 外部审查，修 3 处 must-fix + 3 处 suggested（均编辑性、不动决策倾向 D40-1..9）：① **§6 账目纠错**——`docs` 影响面 `199 → 200` 改 **`201 → 202`**（git ls-files docs/*.md = 201 已含 Phase 39 的 req+prelim+detailed 三份，v1.0 误把 Phase 39「198→199」直接 +1 漏加其 +2，违反稿头第 7 行「不转抄」自约束）；② **K 编号双轨消解**——§2 自勘表 K1–K8 改 **S1–S8**（与 Phase 39 继承的 B/K7/K8/K9 互不重叠），K13 明确为本 Phase 新立；③ **N / R 编号双轨消解**——跨引 Phase 39 的 N2/N3/N6、R1 一律加 **P39-** 前缀（本稿自有 N2=不改签名、N6=不设 X 倍承诺、R1=B 不被污染）；④ **suggested-1（D40-3 / §7-3 失效键）**——补全 `LineKeyMatches` 实为 **12 项**（原 6 高层键漏 `hasMeasurer`/`wrapWidth`），并显式要求与 `TextWidget::GetTextLayout` wrap 指纹（六元组，`:332`）做并集 + `dpi` 算式在基类只算一次（回答 `TextBox.cpp:511-512` 同源警告）；⑤ **suggested-2（A40-5 机检）**——补 HEAD 四函数体基线计数 `BuildTextLayout`=2 / `DrawTextContent`=2 / `OnPaint`=6 / `CaretIndexFromLineX`=5、目标=0，并加「批 0 采 baseline、采不到不得动 R1」纪律（对齐 A40-4）；⑥ **suggested-3（§1.4 区间）**——`≈10–30 ms` 改 `GDI ≈ 10 ms / FT ≈ 27–30 ms`（区间跨引擎，非同链路误差带）。

- **v1.2（2026-10-29，详设回写）** 与 `phase40-softwrap-perline-prep-detailed-design.md` **v1.0** 对齐，**共 11 条**（另在 **§7-1 / §7-3 / §7-7** 三处闸门就地标注「已答」）。一律**只订正口径与范围，不动 A40-1..7 任何判据**：
  1. **§3 R1 `totalCp`**：`cps.size()` → **`byteOffsets.size() - 1`**（D40-E / M3：`cps.size()` 是 `DecodeUtf8` 空间、`totalCp` 是 `SequenceLength` 空间，脏字节上不等 ⇒ 会漂 `FontEngine.cpp:510` 守卫与 `:523` 无 face 兜底）。
  2. **§3 R1「同趟产出」**：→ **两趟**（`DecodeUtf8` 产 `cps` + `BuildByteOffsets` 产表，D40-J）。一个 `DecodeUtf8` 单趟只能产出一种语义的码点序列；要同时得两种就得两趟（或一趟内分叉，更易错）。总复杂度仍 O(N)。
  3. **§3 R4 de-scope**（D40-M，用户已授权）：**不动 `Utf8Decode.h`**。3× 过预留是上界估计，收紧需多一趟 O(N)；危害全在「L 次重复」，而 D40-E 删掉 FT 的 L 次 `DecodeUtf8` 后剩余调用点无放大。
  4. **§6 源码影响面**：`Utf8Decode.h` 与 `ECDI/src/Core/UTF8.cpp` **零改动**（表构建落 `src/Widget/TextWidget.cpp` 的匿名 namespace）；**补上** `ECDI/include/ECDI/Widget/TextWidget.h` 与 `ECDI/include/ECDI/Widget/TextBox.h`（后者是初设 §6 的漏项——`CurrentByteMapKey` 是 protected virtual，override 必须在派生类头声明）⇒ 改动文件 8 → **9**。
  5. **§6 docs**：`201 → 202` → **`201 → 204`**（本文档 202 · 初设 203 · 详设 204）。
  6. **§6 用例**：`~546` → **`546`**（定数；批 1 加 4、批 2 加 4）。
  7. **§7-1** 已答（表结构 / 两趟 / 无 `DecodedText` 新类型 / 末项可越过 size）。
  8. **§7-3** 已答（键 **5 维**非 12 项并集；**dpi 不进键 ⇒ 对表不需要算**；5 维充分性前提 = 单写者）。
  9. **§7-1「`DecodedText` 视图」**：不引入新类型（D40-K）。
  10. **§5 A40-5**：目标 0 不变，基线 2/2/6/5 已实采入库（详设 §5）。
  11. **§5 A40-2 的「GDI 尾拷贝 → 窗口长度」**：详设为**倍增窗口**（D40-D），并钉死「`W == remaining` 时逐字复刻 4 参分支」——这是逐位等价的结构性保证。

- **v1.3（2026-10-29，详设 v1.1 回写）** 单条实质订正（Qoder 审核 + 探针实证带出）：
  - **§5 A40-7 的样本集补 `cps > count` 类**。根因：`DecodeUtf8` 对「前导合法 + 续字节非法」只 `++i` **一个字节**（`Utf8Decode.h:52-55`），而 `SequenceLength` 一次跳过声称的 3–4 字节 ⇒ 同一段字节被切成**不同数量**的码点，**`cps.size()` 可以大于 `count`**（探针：`"\xE0\x41\x41"` → `cps=2 / count=1`；`"\xF0\x41\x41\x41"` → `cps=3 / count=1`）。后果：行表 / `cps` 是 `DecodeUtf8` 空间、表只有 `count+1` 项 ⇒ 消费侧（`emitLine` / `DrawTextContent` / `OnPaint` / `CaretIndexFromLineX`）会给出越界索引。
  - **本 v1.0/v1.1/v1.2 的旧样本全部满足 `cps ≤ count`**（`"\x80\x8A"` 是反向的 `cps=0 < count=2`），**测不出该洞** ⇒ 必须补 `"\xE0\x41\x41"` / `"\xF0\x41\x41\x41"`。
  - 处置（详设 D-9 / D-10 / C40-9 / R40-13）：表的读取**一律钳制**到末项（`min(k, size()-1)`），且**钳制逐位等价**于旧行为（任意 `k ≥ count` 时 `CodepointIndexToByteOffset(text,k)` 恒 == `offsets[count]`，探针 8/8 验证）；`m_cpByteTextSize` 取代 `back()` 作串身份自检。★ **判据数不变**：仍 538 → **546**，只是 T40-1/T40-3 的样本集扩充。

- **v1.4（2026-10-29，实施收口回写）** 新增 **§5.1 验收实测**（A40-1..7 逐条结论 + 证据），§5 表下登记负对照。三条**如实登记的边界**（不把「验了什么」说大）：
  - **A40-6 的判据面收窄**：`CaretIndexFromLineX` 的行内测量循环被 `GetWindow() != nullptr` 守卫短路 ⇒ **无窗口下只能验「行首 + 极端 X 不越界」**。行内 X 落点需真窗口，列 R40-7 人工核对项——**不把「验了行首」写成「验了行内定位」**。
  - **A40-2 / A40-3 是算法层读数，不是产品端到端**：paint 的分配主源是 `textSource.substr`（每行一次），**本 Phase 不改它**；K7 的收益是**换算次数归零**，由 C40-1..4 机检承担——比计时更硬。
  - **A40-5 的命令必须排除注释**（`grep -v '^[[:space:]]*//'`）：否则「把改动写好（加说明注释）」本身会让判据从 0 变成 1。已在 HEAD 版本上复核，**新口径下基线仍是 2/2/6/5**。
