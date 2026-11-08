# Phase 45 · styled spans 表达力扩展（斜体 + 混字号，#89）· 初步设计稿

> 状态：**v1.0（2026-11-08）：初稿落盘待评审**。★ **条 74：状态由评审结论决定**。上游 = 需求稿 **v1.4**（`phase45-styled-spans-expressiveness-requirements.md`——评审二轮放行进初设；**闸门 1/3 已签**（§6.1 签字纪律）；O45-1..5 勘察读数已落盘（需求稿 §6.3 + `.workbuddy/spike/p45/`））。本稿职责 = 把闸门 2/4/5 从「勘察后裁」推进为**带读数的推荐裁决页**（§6，待用户签字），并钉死接口形状 / 行框口径 / 等价性结构。
> ★ **源码行号**对 HEAD `008daff` 亲核（2026-11-08）——**产品码自 `008daff` 起未变**（其后五个提交全为文档批，`git log 008daff..HEAD --stat` 实证），需求稿 K45-1..14 行号快照全部有效。
> ★ 基线：用例 **599** · 公共头 **102** · 设计文档 **217 → 218**（含本稿）。门禁纪律沿需求稿（`git add` → 列举式 `--select L04,L09,L11,L13` → 阳性对照）。

**红线继承**（需求稿四条全文有效，本稿不重述）：零 span 逐位等价 · 测画同源 · 命令流与 DrawText 执行体零改动（豁免面 = 字体实例化 / face 选择层 + LOGFONT 位段）· 编辑器通路不动（L44-1）。

---

## §1 三个闸门的裁决材料（勘察读数驱动；推荐落字，签字页见 §6）

### 1.1 闸门 2 · 斜体选面政策 → **推荐 ⓑ**（D45-7）

勘察读数（需求稿 §6.3）：**CJK 族真斜体面 = 0**（512 face 全扫）；GDI 合成斜切实锤且**宽度增量 +0.00%**；真斜体面改 advance（±0.4~2.8%，等宽 Consolas = 0）。

- **ⓐ 限真斜体面 拒**：CJK 真斜体面为零 ⇒ 中文 markdown 斜体**全静默不斜**——REQ-20 判据③「斜体真渲染」对主消费者（中文转录）空转；闸门 3 已签 ③-1（引用块/列表块内嵌标题行，中文场景）⇒ ⓐ 与已签闸门自相矛盾。
- **ⓑ GDI 合成 + GL 真斜体面/无则回落常规 —— 采纳推荐**：GDI 侧 = 平台原生行为（用户在系统应用里看到的斜体就是合成斜切），且**零宽度增量** ⇒ 测宽 / 折行 / 布局零影响；GL 侧 = 有真斜体面族用真面、无则**回落常规面（不合成）**——与 **C36-4（粗体：GDI 合成 / GL 不合成）完全同构的政策扩容**，已知分歧记录法现成（L45-1 扩容）。
- **ⓒ GL 合成斜切 维持出局**：动字形栅格化 = 破红线 3（需求稿稿首）。
- **两相裁决面**（评审 P1-4 要求）：非 direct（注册表候选）与 direct（文件直查，今日不排除斜体面）在 §2.4 同一回落语义下分别钉死；**回落语义统一 = FaceIdFor 内部回落常规 face（返回可用 id，绝不向 GL 返回 0）** ⇒ GL 执行体零改动（红线 3 的前提）。

### 1.2 闸门 4 · 行框口径 → **推荐 ⓐ 块级统一行框**（D45-8；含 C45-3 修订申明）

**修订申明（相对需求稿 C45-3「逐行行高 = 该行 run 行高 max」）**：本稿推荐改为**块级统一**——全文行高 = `max(全部 run 行框)`（取「行框最大的那个 run 字体」的行框），所有行恒同高：

- **P0-4 浮点守卫结构性成立**：恒 `lineHeight × 行数` 乘法原式，Σ 累加路径**不存在**——需求稿评审 P0-4 的 52.4% 逐位不等风险整体消除。
- **`TextBox` 8.5.2 固定行高契约零改动**（P0-3 处置 = ⓐ 候选）：`GetLineHeight()`（`TextBox.cpp:586`，13 处文本命中）的**取值语义**从「基字体行框」改为「有效行框字体（spans 存在且含 size 覆盖时 = 全文 max 行框字体；否则 = 基字体）的行框」——**Y/lineH 算术、13 处消费点、8.5.2 注释（`:587`）全部原样**；ⓑ（逐行化 + 行顶 Y 表）与 ⓒ（混字号不进 TextBox）出局：ⓒ 直接掏空已签的 ③-1（引用块/列表块 = TextBox），ⓑ 的 13 站回归面与契约冲突不成比例（记 **L45-5**）。
- **视觉代价（如实）**：块内一行大标题 ⇒ 其余行行距撑到同高。③-1 场景内标题行稀疏，可接受；若消费期对行距紧凑提出真实需求 ⇒ L45-5 重启。
- **C45-2 兼容**：无 size 覆盖 ⇒ max = 基字体 ⇒ 行框逐位 = 今日。
- FT 侧佐证（O45-3）：同族各 face hhea 一致 ⇒ max 行框的计算只随 **size/family/weight** 变（斜体零贡献）——行框字体的判定与斜体正交。

### 1.3 闸门 5 · `TextMeasurer` 接口 → **推荐 ⓐ +1 纯虚 `Ascent`**（D45-9）

- **ⓑ（GDI 内部表、接口 +0）出局机理**：per-run y 由**调用方**（`TextWidget` / `TextBox` 的发射站）计算 `run y = baseline − ascent(runFont)`，调用方唯一可达的测量缝 = 公共 `TextMeasurer`——没有公共 `Ascent`，基线对齐**无通路**（需求稿 K45-13 的缺口就是这条）。
- **纯虚**（评审 P1-3）：默认体必错值且静默。**波及面如实列 = 11 子类**：产品 2（`FreeTypeTextMeasurer.h:18` / `GDITextMeasurer.h:27`）+ 门面 1（`RecordingBackend.h:21`）+ 测试假件 8（`WidgetTests.cpp:467/:714/:1915`、`TextMeasurerTests.cpp:208`、`TextBoxTests.cpp:40/:435/:559/:2170`）——假件给显式常数即可（断言不依赖其值）。
- **★ GDI 口径订正（相对需求稿 v1.2「GDI 实现 = otmMacAscent」）**：GDI 绘制锚 = `TextOutW`（`GDIBackend.cpp:919` 口径注释），基线 = `y + tmAscent` ⇒ **`Ascent()` 的 GDI 实现必须返回 `tmAscent`**；`otmMac*` 是**行框**口径（`GDITextMeasurer.cpp:405-440` 实现头注释明写「otmMac* = hhea 缩放、不再用 tmHeight」）——行框与基线是两个口径，初设分开钉死，详设探针验证（T45-9）。
- FT/GL 实现 = 转发既有 `FontEngine::Ascent`（`FontEngine.cpp:650-668`，`face->size->metrics.ascender/64`，与栅格化 D26-2 同 face 同 pixelSize 同源）。

---

## §2 接口设计（公共面）

### 2.1 `Font.h` +italic（末尾追加）

`std::uint8_t italic = 0;`（0 = 常规 = 今日逐位；1 = 斜体请求）。C7560：声明序 = size → family → weight → **italic（末尾）**；聚合初始化面普查（O45-4）= 8 处全在测试 ⇒ 追加末尾安全。GDI 消费 = LOGFONT `lfItalic`（两站 K45-5）；GL 消费 = `FaceIdFor` 第三参。

### 2.2 `TextSpan.h`：`SpanStyle` +2 override + 契约注释解禁

`std::optional<std::uint8_t> italic`、`std::optional<float> size`（DIP 绝对值，与 `Font.size` 同语义）；`:10` 的「严禁包含 size 或斜体」注释改为「size / italic 为 Phase 45 新增覆盖维度；nullopt = 继承」。物化点 = K45-8 单站（`TextWidget.cpp:374-381` `rFont` 组装 +2 分支）。

### 2.3 `TextMeasurer.h` +1 纯虚

`virtual float Ascent(const Font& font) = 0;`——语义 = **该链绘制基线相对绘制原点的距离**（GDI = `tmAscent`；FT = `FontEngine::Ascent`）。GDI 侧实现沿 font 键缓存（沿 GDITextMeasurer 既有双键范式，键扩 italic 维）。

### 2.4 `FaceIdFor` 线程化（D45-10；P0-2 / P1-4 落地面）

- 签名：`FaceIdFor(const std::string& family, std::uint16_t weight, bool italic = false)`（默认值保兼容，`FontEngine.h:96` 先例）；**11 处调用 + 定义**（`FontEngine.cpp:286` 等 12 行）随 `Font` 传 `font.italic`。
- **`familyCache` 键扩维**：`(family, weight)` → `(family, weight, italic)`（`FontEngine.cpp:114` / `:294-298`——P0-2：不扩则斜体首次调用后静默失效）。其余 6 键族 + 3 指纹站同批扩（K45-6/K45-7 清单 = O45-4 实测无漂移）。
- **两相选面 + 统一内部回落**：非 direct = 候选过滤从「bit0 一律出局」改为「请求 italic ⇒ 只收 bit0 面；**池空 ⇒ 回落常规池**（= italic=false 结果）」；direct = 同文件 numFaces 扫描按 bit0 优先、无 bit0 用 face0。两相回落语义一致 ⇒ **GL 侧零特殊分支**（`GLRenderer.cpp:240` 传参即走，执行体不动——红线 3）。

---

## §3 行框与基线状态机

### 3.1 行框统一（块级 max）

- `TextWidget` 五站（K45-9：布局入口 `:423` / totalHeight `:430` / `:584` / 绘制 `:658/:718/:737` / preferred `:824/:829`）与 `TextBox` `GetLineHeight()`——**算术零改动**，仅「行框字体的取值」从基字体改为：`spans 空 或 无 size 覆盖 ⇒ 基字体（逐位今日）；否则 ⇒ max 行框字体`。max 的判定在 `BuildRunsForRange` 产物上取（run 字体集合已就绪，零新增测量——`LineHeight(maxFont)` 一次调用，走既有缓存）。
- **P0-4 守卫结构性成立**：totalHeight 恒 `lineHeight × 行数`（`:584` 原式不动）。
- `CalculateTextPosition`（`:632-637`）垂直居中随新行框值自动成立（算术不变）。

### 3.2 per-run 基线发射（C45-8）

- 行 baseline（每链内）= `lineTop + Ascent(行框字体)`；run `pos.y = baseline − Ascent(run.font)`——两链各自的 `Ascent` 与各自的绘制原点同口径（GDI：`TextOutW` 基线 = y + tmAscent；GL：基线 = `floor(pos.y) + floor(Ascent(font))`，`GLRenderer.cpp:254`）。
- **同族 run 基线严格重合**（Ascent 同值 ⇒ y 差 = 0）；跨族 run 共享行基线 ⇒ **L44-9 的跨族 7px 顶边偏置被基线对齐取代并结账**（需求稿 D45-4）。
- **跨链**：基线相对行顶的距离 = 各链既有 ascent 口径差——今日单字体路径已存在（整行一起偏），spans 不新增 ⇒ 验收口径 = **每链内基线重合为主断言**（T45-4），跨链沿 Phase 31 ≤1px 既有容差。
- 发射站：wrap 逐 run（`:681`）/ 非 wrap 逐 run（`:729`）——命令字段零变化，仅 pos.y 逐 run（Phase 44 已逐 run 发射，x 累积同源；y 从「行统一值」变「逐 run 值」——同线零 size 差时 y 逐位 = 今日）。

### 3.3 等价性结构（C45-1 / C45-2 / P0-4）

零 span ⇒ `rFont` 组装、行框取值、`FaceIdFor(false)`、LOGFONT 位段全现状 ⇒ 逐位；Phase 44 spans（italic/size 全空）⇒ max = 基字体 ⇒ 全链逐位（T45-5 golden 双相，批 0 在 HEAD 录制 = Phase 44 基线，O45-5 建议）；行框恒乘法 ⇒ P0-4 无 Σ 路径。

---

## §4 改动清单与规模预算

| 文件 | 改动 | 预估 |
|---|---|---|
| `Font.h` / `TextSpan.h` / `TextMeasurer.h` | +italic 字段 / +2 override / +Ascent 纯虚（§2） | +25 |
| `FontEngine.h/.cpp` | FaceIdFor + 参 + 12 行线程化 + familyCache 键 + 两相回落 + ascent/lineHeight/measure 键扩维 | +60 |
| `GDIBackend.cpp` / `GDITextMeasurer.cpp` | LOGFONT 位段 ×2 + HFONT/双键扩维 + **Ascent 实现（tmAscent + 缓存）** | +35 |
| `GLRenderer.cpp` | `:240` 传参（执行体不动） | +1 |
| `TextWidget.cpp` | rFont +2 分支 + 行框字体判定 + per-run y + 指纹 2 站扩维 | +90 |
| `TextBox.cpp` | GetLineHeight 取值语义 + per-run y + 行表键 font 维 +italic | +50 |
| `Tests/`（3 文件 11 子类） | 8 假件 +Ascent 显式值 + T45-1..9 | +380 |
| `CMakeLists.txt` | **0** | 0 |

**预算**：公共头 **102 → 102** · 公共 API **+1 方法（Ascent 纯虚，闸门 5）+3 字段** · 用例 **599 → 608**（T45-1..9；★ 需求稿 v1.2 P2-3 钉的 **607** 因闸门 5 推荐收 +1 相而**显式改锚 608**——此即该条预留的「初设显式改锚并回写续订义务」）· 设计文档 **217 → 218** · 批计划 = 批 1 公共契约与选面（T45-2/7/9）/ 批 2 TextWidget 行框与发射（T45-1/3/4/5/6）/ 批 3 TextBox 接入与两链验收（T45-8）+ 收口。

---

## §5 测试规划修订（T45-1..9；★ = 相对需求稿的修订）

| # | 用例 | 判据 |
|---|---|---|
| T45-1 | 斜体 run 命令流 | 每 run 一条；命中 run `font.italic==1`；C45-2 相 |
| T45-2 ★ | 斜体选面（**含回落**） | 有真斜体面族 `FaceIdFor(f,w,true)` ⇒ 斜体 face；**无斜体面族 ⇒ 返回常规 face（内部回落，非 0）**；`italic=false` 全表现状逐位 |
| T45-3 ★ | 行框（**块级统一重写**） | 混字号 ⇒ totalHeight = `LineHeight(max 行框字体) × 行数` **恒乘法**（逐位 vs 手算）；无 size 覆盖 ⇒ 与 Phase 44 路径逐位（C45-2） |
| T45-4 ★ | 基线共享（**观测面定型**） | 同线 run 满足「pos.y + Ascent(run.font) = 常数」（`Ascent` 公共后可无头观测）；GL 无头 `glReadPixels` 相（SwapBuffers 前） |
| T45-5 | 等价护栏双相 | golden 快照（批 0 HEAD 录制 = Phase 44 基线）；两相逐位 |
| T45-6 | 负对照①判别力 | 回退 italic 覆盖 ⇒ 恰 T45-1/2 断言红 |
| T45-7 | 负对照②键完备投毒 | 投毒面含 **familyCache**；miss ⇒ 重算 |
| T45-8 ★ | 两链一致（**分相**） | GDI 相：真面族基线重合 + 合成观感；GL 相：真面斜体 + 回落相；基线主断言 = **每链内重合**；跨链沿 ≤1px 既有容差 |
| T45-9 ★ 新 | `Ascent` 接口相 | GDI `Ascent(font) == tmAscent`（同字体 TEXTMETRIC，无头）；FT 转发 == `FontEngine::Ascent`；假件显式值无害 |

---

## §6 闸门 2/4/5 裁决页（待用户签字；签字纪律 = 日期 + 结论 + 圈选项）

- [ ] **闸门 2 · 斜体选面政策**：推荐 **ⓑ**（GDI 合成 + GL 真面/回落常规 = C36-4 同族扩容；读数支撑 §1.1）。
- [ ] **闸门 4 · 行框口径**：推荐 **ⓐ 块级统一行框**（C45-3 修订申明 §1.2；ⓒ 出局因掏空 ③-1；ⓑ 记 L45-5）。
- [ ] **闸门 5 · `TextMeasurer::Ascent`**：推荐 **ⓐ 纯虚 +1**（§1.3；波及 11 子类如实列；GDI 口径 = tmAscent 订正）。

---

## §7 记账更新

| # | 项 | 处置 |
|---|---|---|
| L45-1 | **GL 不合成斜切**（闸门 2 ⓑ 落地后的具体形态） | C36-4 同族已知分歧扩容；回落常规面语义 §2.4；T45-8 GL 相覆盖 |
| **L45-5 新** | 逐行行高（ⓑ 被拒记账） | 重启条件 = 消费者对行距紧凑的真实需求；届时重盘点 13 处 `GetLineHeight` 消费 + 行顶 Y 表 |
| L45-2/3/4 | 编辑器通路并 L44-1 / 背景底片 / harness 回写 | 维持需求稿口径 |

**被拒项**：ⓐ 限真斜体面（闸门 2——CJK 全静默，与 ③-1 矛盾）· GL 合成（红线 3）· 行框逐行 max（闸门 4 ⓑ——回归面）· Ascent 默认体（P1-3）。

---

## §8 修订记录

- **v1.0（2026-11-08）** 初稿：三闸门推荐裁决页（ⓑ / ⓐ 块级统一 / ⓐ 纯虚 Ascent——全部有勘察读数支撑）· C45-3 修订申明（块级统一行框取代逐行 max——P0-4 守卫结构性成立 + TextBox 8.5.2 契约零改动）· **GDI Ascent 口径订正 = tmAscent**（`TextOutW` 基线实证 `GDIBackend.cpp:919`；`otmMac*` 为行框口径两分）· FaceIdFor 线程化 + familyCache 扩维 + 两相统一内部回落（GL 执行体零改动前提）· 11 子类波及如实列 · T45-1..9 修订（T45-3 块级重写 / T45-8 分相 / T45-9 新）· 用例锚 **607 → 608 显式改锚**（闸门 5 收 +1 相，兑现需求稿 P2-3 预留）· 批计划三批。

---

> 初设稿 v1.0（2026-11-08）：**初稿待评审；闸门 2/4/5 推荐已落字、待用户签字**——评审通过 + 三闸门签字后进详设（条 74）。
