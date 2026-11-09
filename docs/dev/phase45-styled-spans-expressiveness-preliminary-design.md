# Phase 45 · styled spans 表达力扩展（斜体 + 混字号，#89）· 初步设计稿

> 状态：**v1.8（2026-11-09）：闸门 2 / 4 / 5 已由用户授权签字落记录（§6、§8 v1.8）；初设评审放行，进详设（详设 v1.0 已起草，待评审）**。（v1.7 要点如下。）v1.7（2026-11-09）：初设评审放行进详设（v1.6 评审 P0×0）；随手带入 P1×1 / P2×3；闸门 2 / 4 / 5 评审均「可签」，待用户签字（条 74 / 签字纪律）**。（v1.6 要点如下。）v1.6（2026-11-09）：v1.5 评审吸收毕（P0×1 / P1×1 / P2×3）——判据单点化落到布局入口（`:498` 等 4+3 站换钩子，含行为变更登记）、`ResolveRunFont` 语义定序；闸门 2 = 可签 ⓑ、闸门 4 = 待评审复核、闸门 5 = 可签 ⓐ**。（v1.5 要点如下。）v1.5（2026-11-09）：v1.4 评审吸收毕（P0×1 / P1×2 / P2×3）——§3.1 全仓 LineHeight 实参穷举表（补 `TextBox.cpp:203`）、`ResolveRunFont` 选 (b)、I44-7 由本 Phase 取代；闸门 2 = 可签 ⓑ、闸门 4 = 待评审复核、闸门 5 = 可签 ⓐ**。（v1.4 要点如下。）v1.4（2026-11-09）：v1.3 评审吸收毕（P0×1 / P1×2）——§3.1 加「仅 size 覆盖触发」维度守卫（italic/weight/family/color 不触发）、替换面订正为 6 实参 + 3 下游消费、五站同源相注明依赖与负对照；闸门 2 = 可签 ⓑ、闸门 4 = 待评审复核（评审预告：修 P0-1 后可签）、闸门 5 = 可签 ⓐ**。（v1.3 要点如下。）v1.3（2026-11-09）：v1.2 评审吸收毕（P0×2 / P1×2 / P2×3，§8 v1.3）——五站恢复入改动面（P0-1）、行框字体判据改 A′ 最大（P0-2）、`GetLineHeight` 无窗口兜底保持 16.0f（P1-1）、判据一致相换成有判别力的假件（P1-2）；闸门 2 = 可签 ⓑ、闸门 4 = 待评审复核（口径已重写）、闸门 5 = 可签 ⓐ**。（v1.2：评审 v1.0 吸收毕 + 补增 P1 激活判据单点化。）★ **条 74：状态由评审结论决定**。上游 = 需求稿 **v1.4**（评审二轮放行；闸门 1/3 已签；O45-1..5 勘察落盘）。★ 产品码自 `008daff` 零改动（`git diff 008daff..HEAD -- ECDI/` 为空）⇒ 需求稿 K45-1..14 行号快照有效。基线：用例 **599** · 公共头 **102** · 设计文档 **218**。

**红线继承**（需求稿四条全文有效）：零 span 逐位等价 · 测画同源 · 命令流与 DrawText 执行体零改动（豁免面 = 字体实例化 / face 选择层 + LOGFONT 位段）· 编辑器通路不动（L44-1）。

---

## §1 三个闸门的裁决材料（v1.1 修订）

### 1.1 闸门 2 · 斜体选面政策 → **ⓑ 可签**（D45-7；v1.1 修 P1-1 direct 相）

勘察读数：**512 face 全扫中 CJK 族 `fsSelection` bit0 全 0**（真斜体面仅在拉丁族）；GDI 合成斜切实锤且宽度增量 +0.00%；真斜体面改 advance（±0.4~2.8%）。ⓐ 拒（CJK 斜体全静默，与已签 ③-1 矛盾）；ⓒ 维持出局（红线 3）。**ⓑ = GDI 合成 + GL 真斜体面/无则回落常规（C36-4 同族扩容）**。

- **非 direct 相**：候选过滤「bit0 出局」→「请求 italic ⇒ 只收 bit0 面；池空 ⇒ 回落常规池（= italic=false 结果）」。
- **direct 相（v1.1 按评审 P1-1 修订——v1.0 承诺了做不到的事）**：`Win32FontSource.cpp:308-315` 的直查探针只 push **单 face（索引 0）** ⇒ direct 族候选池恒 1 面，「按 bit0 优先」**结构上不可达**。处置 = **如实收窄**：direct 路径斜体请求**回落 face0**（Phase 36「直查豁免」语义保持，`Win32FontSource.cpp` 不入改动面）；登记入 L45-1（重启条件 = direct 文件消费者出现斜体需求，届时扩 numFaces 枚举）。**T45-2 的 direct 相删除**。
- **回落语义统一收在 `FaceIdFor` 内部、绝不向 GL 返回 0** ⇒ GL 执行体零改动（`GLRenderer.cpp:240` 传参即走——红线 3）。

### 1.2 闸门 4 · 行框口径 → **ⓐ 块级统一行框，随本版可签**（D45-8；C45-3 修订申明）

全文行高 = `A′ 最大的 run 字体`（v1.3 改判据，P0-2；v1.2 为「LineHeight 最大」）之行框（**块级统一**，所有行恒同高）：

- **P0-4 浮点守卫结构性成立**：totalHeight 恒 `lineHeight × 行数`（`TextWidget.cpp:584` 原式），Σ 路径不存在。
- **TextBox 8.5.2 固定行高契约零改动**（P0-3 处置 = ⓐ）：`GetLineHeight()`（`TextBox.cpp:586-593`）**算术原样**（含 `:587` 注释与 7 处 `GetLineHeight` 消费点（v1.5 订正计数口径：13 = 1 定义 + 7 调用 + 5 注释；另 `TextBox.cpp:203` 为不经 GetLineHeight 的独立直取，见 §3.1 穷举表）），仅**取值来源**从「基字体」改为「有效行框字体」——机制见 §3.1（v1.0 的「在 BuildRunsForRange 产物上取 max」被评审 P0-3 证伪：行框在 `:423` 定死、runs 在 `:501` 逐行才产生，且 `TextBox.cpp:443` 喂 `BuildDisplayText()` + `TextWidget.cpp:497-498` 串身份护栏 ⇒ TextBox 路径 spans 恒不可达——v1.1 已改走成员助手通路）。
- **★ 已知可见影响（闸门 4 签字项，v1.1 按评审 P1-4 显式列出）**：块内出现大字号 run ⇒ **全块**行框/选区 rect/**光标高**随 max 变大——`TextMeasurer.h:186-190` 契约明写「光标高 = 行框高」、`TextBox.cpp:813` 光标高直取 `GetLineHeight()`。这是只读显示面上的真实可见变化（非 L44-1 编辑通路）；**是否接受 = 闸门 4 签字内容**（拒绝则需改「光标高 = LineHeight(基字体)」放弃 I31 口径——同为口径变更，二选一，不沉默）。③-1 场景内标题行稀疏 ⇒ 行距撑高的视觉代价**由用户在签字时确认**（v1.0 替用户下结论，评审 P2-2 订正）。
- ⓒ（混字号不进 TextBox）出局：掏空已签 ③-1；ⓑ（逐行 max）记 **L45-5**。

### 1.3 闸门 5 · `TextMeasurer` 接口 → **ⓐ +1 纯虚 `Ascent`（DIP 契约），修后可签**（D45-9）

- **ⓑ 否证补全（v1.1 按评审 P1-2——v1.0 的「无通路」是循环论证）**：最强竞争方案 = `RunSlice` 已携带完整 `Font`（`TextWidget.h:128-134`），在唯一物化点（`TextWidget.cpp:374-381`）算 `baselineOffset = A′(box) − A′(run)` 挂进 `RunSlice` ⇒ 消费侧接口 +0。**否证点 = 该方案只搬运消费侧、不解决生产侧**：ascent 值仍必须从链上产生，widget 层唯一测量缝 = 公共 `TextMeasurer`（backend 内部态不可达）⇒ 任何 +0 变体都需要一个非公共的产生机制；且 `baselineOffset` 冻结 DIP 值于测量时 = A′ 的冗余拷贝。ⓐ 成立。
- **★ 单位契约钉死（v1.1 按评审 P0-2——v1.0 两实现都返回裸物理像素，DPI ≠ 96 时公式两边单位不同）**：`Ascent(const Font&)` 返回 **DIP**，且返回的是**链内整数像素 ascent 的 DIP 折算**（A′，定义见 §3.2——这是 P0-1 整数化方案的承接面）：GDI 实现 = `tmAscent × 96 / dpi`（`tmAscent` 本身整数）；FT 实现 = `floor(face->size->metrics.ascender / 64) × 96 / dpi` **薄包装**（v1.0「直接转发 FontEngine::Ascent」作废——`:664` 原体无 96/dpi 换算，对比同文件 `:634` LineHeight 的换算先例）。T45-9 判据改为「同一 measurer 实例、同一 dpi 下 `Ascent(font) == tmAscent × 96/dpi`（±1e-3）」。
- 波及面 = **11 子类**（产品 2 + RecordingBackend 门面 + 测试假件 8，行号清单见 v1.0 §1.3，评审 ✅1 复核无误）；★ **纯虚 = 源兼容破坏**（下游任何自定义子类编译不过）——记 §7 L45-6（评审 P1-5）。

---

## §2 接口设计（公共面）

### 2.1 `Font.h` +italic（v1.1 按评审 P1-3 改 **bool**）

`bool italic = false;`（v1.0 的 `uint8_t` 作废：与全仓 `uint16_t weight` 口径不符、隐式窄化有严格告警风险、非 0/1 语义未定义；与「默认值 = 今日逐位」契约一致）。声明序 = size → family → weight → **italic（置于 weight 之后，末位——`Font.h:23`「weight 必须位于 family 之后」硬约束同族；designated-init 顺序与 ABI 布局双重要求，v1.7 显式写明）**；C7560 聚合初始化面 8 处全在测试（O45-4）⇒ 追加安全。GDI 消费 = LOGFONT `lfItalic`；GL 消费 = `FaceIdFor` 第三参 `bool`。

### 2.2 `TextSpan.h`：`SpanStyle` +2 override

`std::optional<bool> italic`、`std::optional<float> size`（DIP 绝对值）；`:10` 契约注释解禁。物化点 = `TextWidget.cpp:374-381` `rFont` 组装 +2 分支。★ v1.6：该组装抽为 `static ResolveRunFont`，语义 = 「该 span 的字形字体」；行框字体是同集合上的兄弟概念（§3.1），非其产物。

### 2.3 `TextMeasurer.h` +1 纯虚

`virtual float Ascent(const Font& font) = 0;`——返回 **A′（DIP，§3.2 定义）**；语义注释 =「该链整数像素基线偏移的 DIP 折算——与 LineHeight（行框口径，`otmMac*`）**分属两个口径**」。GDI 侧实现沿 font 键缓存（键扩 italic 维）；**FT 侧（v1.5 补）**：`FontEngine::Ascent` 缓存键 = `{FaceIdFor(family, weight, italic), px}`（`FontEngine.cpp:650/:657/:667`），`FaceIdFor` 加 italic 维后 faceId 自动分档 ⇒ FT 缓存**无需另扩键**，T45-9 FT 相即以此为缓存依据。

### 2.4 `FaceIdFor` 线程化（D45-10）

签名 `FaceIdFor(const std::string& family, std::uint16_t weight, bool italic = false)`；**`familyCache` 键扩维** `(family, weight, italic)`（P0-2）；**11 个调用点**（v1.7 订正计数：`FontEngine.cpp:427/468/517/580/629/650/657/667/680/699` 共 10 + `GLRenderer.cpp:240` 共 1；声明 `FontEngine.h:96` 与定义 `FontEngine.cpp:286` 不计调用）随 `Font.italic` 传参。★ **其中 4 个度量点详设逐个点名传 italic（评审 v1.6 P1-1）**：`FontEngine.cpp:629`（`LineHeight` 内 `FaceOf(FaceIdFor(...))`）、`:657`（`Ascent` 内同句）、`:650` / `:667`（`ascentCache` 的 find / emplace 键）——漏传则斜体基字体在 GL 侧解析到常规 face 的 hhea/ascender，而 GDI 侧 LOGFONT 带斜体位 ⇒ 两后端行高/基线分叉，与 T45-8「每链内整数基线重合」对立。`lineHeightCache`（`:621`）键扩 italic 维、`ascentCache` 键靠 faceId 自动分档（§4 已覆盖）。选面两相 + 回落 = §1.1。

---

## §3 行框与基线状态机（v1.1 重写 §3.1 取数机制 + §3.2 像素空间整数化）

### 3.1 行框字体的获取机制（v1.1 按评审 P0-3 重写——v1.0 的「产物上取 max」不可实现）

**修法 = 成员助手（评审 P0-3 修法 (b) 的无签名变体）**：`TextWidget` 新增 protected

```
// 判据单点化（v1.2 新增虚钩子——评审补增 P1）:
protected:
    virtual bool SpansActiveForLayout() const { return HasActiveSpans(); }  // 基类 = 逐位今日
    // TextBox 覆写 = 绘制侧三站点同一表达式（TextBox.cpp:495 / :526 / :1536）:
    //   return HasActiveSpans() && m_echoMode == EchoMode::Normal && !m_isComposing;

Font EffectiveLineBoxFont(TextMeasurer&) const;
// ★ v1.4 维度守卫（评审 v1.3 P0-1）：**仅「SpansActiveForLayout() 为真 且 至少一个 span 的 size 覆盖非 nullopt」才进入 A′ 比较**；
//   否则（spans 判死 / 纯 family / 纯 weight / 纯 color / italic-only）⇒ 返回 m_style.font.value（逐位今日）。
//   理由：纯 family span 是 Phase 44 已交付能力（TextBox 用例 :3561 `{.family="Consolas"}`），跨族行框差异巨大
//   （O45-3：Consolas lineGap≈17% vs Segoe 0；GDI Segoe 14px macAsc15/tmH19 vs Consolas macAsc10/tmH17）——
//   无守卫则基字体=Consolas、span 覆盖 family=Segoe（size 恒 nullopt）时行高变化，撞 C45-2，
//   且经 GetLineHeight() 的 7 个调用点漂移滚动量程/光标/Y 定行。
//   italic 不触发：O45-3 同族 regular/bold/italic hhea 完全一致，进判据取不到新值、徒增 O(R) 次 Ascent。
//   weight 同属 Phase 44 已交付维度，不触发。
// 守卫通过后 ⇒ 对「基字体 + 各 span 覆盖物化出的 run 字体集合（含非 size span 的 run，其字号=基字号）」取 **A′（Ascent）最大者**（v1.3）；
//   并列（|ΔA′| < 1e-3）⇒ 取 LineHeight 较大者；再并列 ⇒ 取基字体（确定性定序）
//   （O(R) 次 Ascent，测量走既有缓存，首次 miss O(R)）
// 行高 = measurer.LineHeight(EffectiveLineBoxFont(...))——单一字体同时给出「基线锚」与「行高」。
```

**P0-2 定序（v1.3，评审 v1.2 P0-2 反例成立：LineHeight 取 `otmMac*`/hhea 盒口径，Ascent 取 `tmAscent`/ascender 口径，两套度量族，「盒最大 ⇒ ascent 最大」不成立，反例 run(asc25,desc0) vs box(asc20,desc10,gap10) ⇒ `pos.y = lineTop − 5`）**：

- **定理（§3.2 前提）**：box 取 A′ 最大者 ⇒ 对任意 run，`A′(run) ≤ A′(box)` **按构造成立**（不再依赖跨度量族假设）⇒ `pos.y = lineTop + A′(box) − A′(run) ≥ lineTop`，字形上沿恒不越行框顶。
- **代价（如实登记 L45-7）**：行高取 box 一个字体的 `LineHeight`，另一 run 若 descent/lineGap 更大，其下沿**可能超出行框底**（上例：box=run 字体、另一字体 descent 10）——仅在「ascent 最大者与 descent 最大者分属不同字体」的极端混排出现；不影响基线共享与不压上一行；是否升级为「行高 = max(LineHeight(box), A′max + max descent)」需新增 `Descent` 接口，**不在本期**，重启条件 = 真实下沿裁切反馈。
- 零 span / 无 size 覆盖（含纯 family/weight/color/italic）⇒ 守卫不进 ⇒ box = 基字体 ⇒ 逐位今日（C45-2 结构性成立）。

**五站恢复入改动面（v1.3，评审 P0-1：v1.1 重写 §3.1 时误删 v1.0「TextWidget 五站」枚举，布局/绘制/首选行距不同源）**——`EffectiveLineBoxFont(measurer)` 逐点替换以下 **6 个** `LineHeight(m_style.font.value)` 实参（`:423` / `:658` / `:718` / `:737` / `:824` / `:829`）；**`:430` / `:584` / `:666` 为下游消费式，零改动、随源自动同源**（v1.4 按评审 P1-1 订正：v1.3 把 `:666` 当独立站点，实际它吃 `:658` 的 lineHeight，无 LineHeight 实参可换）：

| 站 | 位置（`TextWidget.cpp`，`008daff`） | 现状 | 改后 |
|---|---|---|---|
| 布局入口 + 空文本 | `:423` / `:430` | 基字体 | `EffectiveLineBoxFont` |
| totalHeight | `:584` | `lineHeight × 行数` | 原式，lineHeight 来自助手 |
| 绘制 wrap | **`:658` 实参替换**；`:666`（`lineY = block.y + row × lineHeight`）= 消费式，随 `:658` 自动同源 | 基字体 | 同一助手值（**否则末行到不了块底、与 totalHeight 失配**） |
| 绘制非 wrap | `:718` / `:737` | 基字体 | 同上 |
| preferred（DoMeasureText） | `:824` / `:829` | 基字体 | 同上（字号放大后首选高随之变） |

⇒ 同一函数、同一帧内 totalHeight 与逐行推进**恒同源**（红线 2「测画同源」）；**+0 接口**；T45-3 增「五站同源相」（见 §5）。

**全仓产品码 `LineHeight` 实参穷举表（v1.5 治本：枚举式改动面用表关死，详设照表打勾；2026-11-09 对 `008daff` 后 HEAD 全文 grep，排除测试与定义/转发体）**

| # | 位置 | 性质 | 处置 |
|---|---|---|---|
| 1 | `TextWidget.cpp:423` | 实参（布局入口，局部 `font` 由 `:422` 取） | **改**：`:422` 改读 `EffectiveLineBoxFont(measurer)` |
| 2 | `TextWidget.cpp:658` | 实参（绘制 wrap） | **改** |
| 3 | `TextWidget.cpp:718` | 实参（绘制非 wrap） | **改** |
| 4 | `TextWidget.cpp:737` | 实参（绘制非 wrap） | **改** |
| 5 | `TextWidget.cpp:824` | 实参（preferred） | **改**（I44-7 处置见下） |
| 6 | `TextWidget.cpp:829` | 实参（preferred） | **改** |
| 7 | `TextBox.cpp:591` | 实参（`GetLineHeight` 体内，窗口分支） | **改**（无窗口 `:592` 16.0f 原样） |
| 8 | **`TextBox.cpp:203`** | 实参（`MultilinePreferred`，**不经 GetLineHeight**；v1.4 漏） | **改**，见下「:203 处置」 |
| — | `TextWidget.cpp:430` / `:584` / `:666` | 下游消费：`:430` 吃 #1 的 lineHeight；`:584` 吃 #1 的 lineHeight；`:666` 吃 #2 的 lineHeight | 不改 |
| — | `TextBox.cpp:612/:628/:645/:790/:813/:878/:1471` | `GetLineHeight()` 7 消费点 | 不改（随 #7） |
| — | `PaintContext.cpp:131` / `RecordingBackend.h:139` / `FreeTypeTextMeasurer.cpp:75` / `GDITextMeasurer.cpp:405` / `FontEngine.cpp:616` | 转发 / 实现体 | 不改（RecordingBackend 门面随 `Ascent` 纯虚补实现，§2.3） |

- **:203 处置（v1.5，评审 P0-1；对评审建议的一处偏离，理由如实写明）**：该点与 `:628`（`GetMaxScrollOffset`）若一新一旧，将复现 `TextBox.cpp:166-172` 注释警告的「preferred < 内容高 ⇒ AutoSize 截断后冒出内部滚动条、末行被切」，且打断 `:161-164` 的「行高与 GetLineHeight/绘制/光标同源」声明——**必须同源，评审成立**。但**不采「改调 `GetLineHeight()`」**：`:192` 判空用的是 `ResolveMeasurer()`（虚接缝，`TextBoxTests.cpp:432` 夹具返回 19.2f 假件、**无窗口**），而 `GetLineHeight()` 无窗口恒 16.0f ⇒ 该夹具 preferred 高 19.2→16，撞红线 1（与 P1-1 同型）。**改为 `measurer->LineHeight(EffectiveLineBoxFont(*measurer))`**：measurer 身份不变（零 span 逐位今日，存量夹具零影响），spans+size 激活时与 `:628` 同取有效行框字体 ⇒ 同源恢复。T45-3 增「TextBox preferred 高 = `lineH × 行数 + pad×2` 且 = `GetMaxScrollOffset` 内容高」相（专用假件 + 窗口相）。
- **助手取「run 字体集合」的机制（v1.5，评审 P1-1）：选 (b)**——助手内遍历 `m_spans` 组装 run 字体，**不调 `BuildRunsForRange`**（`:423` 时 byteOffsets 未建、且会引入 3 次测宽，违背「测量走既有缓存」）。为避免与 `:374-381` 形成第二处 span→font 组装点：**抽 `static Font ResolveRunFont(const Font& baseFont, const SpanStyle&)`**（family/weight/italic/size 四分支，唯一真相源），`BuildRunsForRange` 的 `:374-381` 与助手**共同调用**，规则不双写。改动面 +10（TextWidget.cpp，含入 +125 预算）。
- **I44-7 处置（v1.5，评审 P1-2）**：`TextWidget.cpp:823`「行高必须且仅来自基控件字体」**由本 Phase 取代**：新口径 = **「行高恒来自同一个助手 `EffectiveLineBoxFont`」**（单源性保留，源由基字体改为有效行框字体）。I44-7 原意（对照 `:783` 同款措辞）= 行高不得由整段 MeasureText 高度反推——助手仍满足；零 span / 无 size 覆盖时助手恒返回基字体 ⇒ 逐位今日。详设改 `:824` 时同步改写 `:823` 注释为「I44-7（Phase 45 取代）」，**不视为违约**。登记 §7 L44 结账行。

**span 激活判据站点穷举表（v1.6，评审 v1.5 P0-1：判据单点化须落到布局入口，不止行高助手）**——选 **(a)**：下列站点的 `HasActiveSpans()` 一律换读钩子 `SpansActiveForLayout()`，使「选分支」与「取行高」恒同源：

| # | 位置 | 现状 | 处置 |
|---|---|---|---|
| J1 | **`TextWidget.cpp:498`** `hasSpans = HasActiveSpans() && (text == m_text)` | 布局「spans 分支 / 基字体 FitText 分支」选择 | **改**：`SpansActiveForLayout() && (text == m_text)`（串身份护栏 `text == m_text` 保留） |
| J2 | `TextWidget.cpp:652` / `:774` / `:814` `activeSpans / if (HasActiveSpans())` | 绘制 / preferred 选分支 | **改**为钩子（基类钩子 ≡ `HasActiveSpans()` ⇒ TextWidget/Label 逐位等价；TextBox 自绘通路 `TextBox.cpp:495/:526/:1536` 本就是三条件版，行为中性，与钩子同表达式） |
| J3 | `TextBox.cpp:495` / `:526` / `:1536` | 三条件表达式 | 改调钩子（原可选 → 本版**纳入**，单一真相源） |

- **反例（评审成立，已核 `TextBox.cpp:212-214`）**：组合期（`m_isComposing`，`EchoMode::Normal`）`BuildDisplayText()` 直通 `m_text` ⇒ `text == m_text` 成立；旧 `:498` 判活（折行按 run 字体）、钩子判死（行高取基字体、绘制三站点判死）⇒ **折行按大字号、行距按基字号、字形按基字号**的三方分叉。(a) 令三者同进同出。
- **★ 行为变更声明（如实登记，红线 1 边界）**：J1 改后，**「TextBox 组合期 × 已设 spans」的布局**由「spans 分支」变「基字体分支」——这是 **Phase 44 既有布局/绘制不一致（布局判活、绘制判死）的订正**，仅限该角落；零 span、非组合态、密码态（`text != m_text`，两版皆死）、TextWidget/Label 全部逐位不变。**批 0 前置核查（评审 v1.6 已给结果，降级为事后确认）**：全仓 `SetStyledText/SetSpans` 在 `TextBoxTests.cpp` 仅 `:3563`（T44-10），该用例 `:3556` 即 `SetReadOnly(true)`、无任何组合调用，且 `TextBox::UpdateComposition`（`:1006-1007`）对只读直接 return ⇒ J1 行为变更对存量 599 用例零冲击。（原文：全库测试中「组合期 + spans」夹具（`TextBoxTests.cpp` 仅 1 处 `SetStyledText`，需逐个确认是否进组合态）——若有存量断言依赖旧行为，须改走闸门（不得静默改期望值）。（若不接受该订正，则替代 = 方案 (b)：钩子放宽到与 `:498` 同宽，但会让三绘制站点也判活 ⇒ 绘制侧行为变更更大，**不推荐**。）
- **T45-3 判据一致相扩一路（v1.6）**：专用假件 + TextBox 组合态 + size 放大 spans ⇒ **折行行数 = 基字体折行（与非 spans 一致）且行高 = 基字体**；对照相：Normal 非组合 ⇒ 折行与行高同按有效字体（证明两路同进同出，抓「只改行高路」的实现）。
- **`ResolveRunFont` 语义定序（v1.6，评审 P1-1）**：`ResolveRunFont(base, style)` 的语义 = **「该 span 的字形字体」**（产物进 `RunSlice.font` → MeasureText/FitText/DrawText）；**行框字体是它的兄弟概念——同一 run 字体集合上、不同选取规则（A′ 最大 + size 守卫）的结果，不是它的产物**。助手遍历集合时对每个 span 调 `ResolveRunFont` 得字形字体，再按 A′ 选取；详设**不得**为行框另写 size 分支。size 的 `float` DIP 绝对值与 `Font.size` 同语义同单位（§2.2 / D45-1），本期字形字号与行高基准恰同一量，**该同一性由 `LineHeight/Ascent` 都是 `font.size` 的函数保证，视为契约而非巧合**；若未来出现「行高基准 ≠ 字形字号」需求须另立。

- `BuildTextLayout` **签名零改动**（它本就是成员函数，`:422` 的 `const Font& font = m_style.font.value` 改读 `EffectiveLineBoxFont(measurer)`——`:423` 行框、`:584` 乘法原式、`:430` 空文本相全部原样；两个调用点 `TextBox.cpp:443` / `TextWidget.cpp:609` 零改动）。
- **TextBox 判据一致性（v1.2 按评审补增 P1 重写——v1.1 把激活判据说反了）**：绘制侧三站点（`TextBox.cpp:495` / `:526` / `:1536`）的激活判据 = `HasActiveSpans() && m_echoMode == Normal && !m_isComposing`，**比 `HasActiveSpans()`（`TextWidget.h:199-201`）多两道门**；v1.1 的助手只看后者 ⇒ 密码态/组合期设了 spans 时「布局判活（按 max span 字体抬行高）、绘制判死（掩码点全基字体）」＝ 行距被撑大而字形不变（一片空白）。修法 = **判据单点化**：`SpansActiveForLayout()` 虚钩子（基类 = `HasActiveSpans()`，对 TextWidget/Label 逐位等价 ⇒ C45-1/C45-2 不受扰；TextBox 覆写三条件版），`EffectiveLineBoxFont` 改读钩子 ⇒ **布局与绘制的激活判据恒同一份**；`GetLineHeight()`（`:589-593`）取值来源换读 `EffectiveLineBoxFont(...)`——算术与 7 处 `GetLineHeight` 消费点（v1.5 订正计数口径：13 = 1 定义 + 7 调用 + 5 注释；另 `TextBox.cpp:203` 为不经 GetLineHeight 的独立直取，见 §3.1 穷举表）零改动。★ **v1.3 无 measurer 兜底（评审 P1-1）**：`GetLineHeight()` 现结构 = `if (Window* w = const_cast<Window*>(GetWindow())) return w->GetTextMeasurer().LineHeight(font); return 16.0f;`——**无窗口分支没有 measurer，助手不可调**。处置 = **窗口分支内**取 `w->GetTextMeasurer()` 传助手（`LineHeight(EffectiveLineBoxFont(w->GetTextMeasurer()))`），**无窗口分支原样 `return 16.0f`**（保持今日兜底，**不改走 `ResolveMeasurer()`**——`TextBoxTests.cpp:432` 夹具 `ResolveMeasurer()` 返回 19.2f 的假件，改走会让无头行高 16→19.2、撞红线 1）。⇒ **存量用例受影响清单 = 空**（无窗口恒 16.0f；有窗口的存量夹具零 span 时助手返回基字体，逐位今日）。实施注记：详设批 1 以「`GetLineHeight` 无窗口相」入存量护栏断言（`TextBoxTests.cpp:2026-2028` 注释所述语义）。★ 详设可选实施注记：三绘制站点可改调钩子（行为中性、单一真相源），批 2 定。★ **声明位置（评审 P2-2）**：`SpansActiveForLayout()` 覆写必须声明在 `TextBox.h` 的 **protected: 区（`:191` 起，`:319` private 之前）**，否则派生类侧不可达。
- 助手本身即评审 P0-3 要求的「取数机制显式化」：既非「产物上取」（不可达），也非改 `BuildTextLayout` 签名（成员函数无需）；`TextWidget.h:147` totalHeight 注释同步补「行框字体 = EffectiveLineBoxFont」。

### 3.2 per-run 基线发射（v1.1 按评审 P0-1 重写——像素空间整数化，两层 floor/截断闭合）

**A′ 定义**：`A′(font) = AscentIntPx(font) × 96 / dpi`（DIP）。其中 **AscentIntPx**（链内整数像素 ascent）：GDI = `tmAscent`（TEXTMETRIC，本就是整数设备像素）；GL/FT = `floor(face->size->metrics.ascender / 64)`（★ **`FontEngine::Ascent` 返回值语义修订为整数像素**——其唯一消费点 `GLRenderer.cpp:254` 的外层 `floor(...)` 变 no-op，**:254 一行不动**（红线 3）；连带 = GL 全行基线上移 <1px 子像素余量 + **T44-11 基线锚随 L44-9 结账重锚**（显式登记，§7））。

**发射公式（全 DIP）**（`lineTop` = 行框顶 = 块顶 + row × lineHeight，即 `TextWidget.cpp:666` 的 `lineY`，v1.5 锚定）：`pos.y = lineTop + A′(boxFont) − A′(runFont)`，其中 **boxFont = A′ 最大者（§3.1 定理 ⇒ A′(run) ≤ A′(box)，位移恒 ≥ 0；v1.3 补前提）**；`baseline = pos.y + A′(runFont) = lineTop + A′(boxFont)`（DIP 恒等式——语义为 A′）。★ **float32 断言口径（v1.3 评审 P2-1）**：DIP 域逐 run 加法回到基线在 float32 下 dpi≠96 有 0.1%–0.5% 精确不等，且 `floor((lineTop + Δ)·scale)` 在 dpi 120/144/168 实测各有 1/2/4 例/6 万与 run 相关 ⇒ **T45-4 判据 = DIP 恒等式 ±1e-3 容差 + 链上整数基线行断言 96dpi 精确、非 96dpi 允许 ±1px**；评审建议的中点变体 `pos.y = (floor(scale·lineTop) + Abox_px − Arun_px + 0.5)/scale`（实测 0/6 万失配）需 widget 层可见 scale，**批 1 以 spike 决定是否采纳**（采纳则 T45-4 改精确相等；不采纳则维持 ±1px），不在初设钉死。

**两链闭合证明（整数位移与量化可交换）**：`Renderer::ExecuteCommand` 只折 pos（`Renderer.cpp:69-71`，font 原样）⇒ 链上收到 `pos.y_px = scale·lineTop_px + (AscentIntPx(box) − AscentIntPx(run))`——第二项为**精确整数**（GDI `tmAscent` 整数；GL floor 后整数；|值| < 2^24 float 精确表示）⇒
- GDI：`drawY = static_cast<LONG>(pos.y_px)`（`:922-923` 向零截断）⇒ `trunc(x + Δint) = trunc(x) + Δint` ⇒ `baseline_eff = trunc(scale·lineTop_px) + AscentIntPx(box)`——**与 run 无关，同行恒等**；
- GL：`floor(pos.y_px) + floor(Ascent_px(run))`（`:254`，外层 floor 对整数 no-op）⇒ `floor(scale·lineTop_px) + AscentIntPx(box)`——**同行恒等**。

⇒ 同线 run 基线落在**同一整数像素行**（评审同型建模 10 万组 0% 不等）；v1.0 的「33.4% 差 1px / 1.99px」由两层 floor 与非整数 A′ 的组合产生，本公式消解。跨链基线-行顶差 = 各链既有 ascent 口径差（今日单字体整行同偏，非新增）——验收 = **每链内重合为主断言**（T45-4），跨链沿既有容差。

### 3.3 等价性结构（C45-1 / C45-2 / P0-4）

零 span ⇒ `EffectiveLineBoxFont` 返回基字体、`rFont` 组装、`FaceIdFor(false)`、LOGFONT 位段全现状 ⇒ 逐位；Phase 44 spans ⇒ max = 基字体 ⇒ 全链逐位（T45-5 golden 双相）；行框恒乘法 ⇒ P0-4 无 Σ 路径。指纹：`m_prefFont`/`m_wrapFont` 逐字段比较加 `italic`（K45-7）；行表键 font 维 +italic（`:506-508`）；span revision 维已覆盖 span 侧。

---

## §4 改动清单与规模预算（v1.3 修订：五站入表、GetLineHeight 兜底、TextBox.h 钩子声明）

| 文件 | 改动 | 预估 |
|---|---|---|
| `Font.h` / `TextSpan.h` / `TextMeasurer.h` | +bool italic / +2 override / +Ascent 纯虚（§2） | +25 |
| `FontEngine.h/.cpp` | FaceIdFor 三参 + 12 行线程化 + familyCache 键 + 两相回落 + **Ascent 语义修订（整数像素）** + ascent/lineHeight/measure 键扩维 | +65 |
| `GDIBackend.cpp` / `GDITextMeasurer.cpp` | LOGFONT 位段 ×2 + HFONT/双键扩维 + **Ascent 实现（tmAscent × 96/dpi + 缓存）** | +40 |
| `GLRenderer.cpp` | `:240` 传参（执行体 `:254` 不动） | +1 |
| `TextWidget.h/.cpp` | rFont +2 分支 + **EffectiveLineBoxFont 助手（A′ 最大判据）+ `SpansActiveForLayout()` 虚钩子 + **6 个实参**（`:423` · `:658` · `:718` · `:737` · `:824` · `:829`）替换 + 3 处下游消费（`:430/:584/:666`）零改动（v1.4）+ **size 守卫** + **`:823` 注释改写（1 行，I44-7）** + **判据换钩子 J1/J2：`:498/:652/:774/:814`（v1.6）**** + per-run y + 指纹 2 站扩维 + `:147` 注释 | +125 |
| `TextBox.cpp` / `TextBox.h` | GetLineHeight 取值来源（**窗口分支内；无窗口 16.0f 原样**）+ **`TextBox.h` protected 区覆写钩子声明（v1.3）** + per-run y + 行表键 +italic；★ **7 处 `GetLineHeight` 直取 + `:203` 独立直取（v1.5）+ `lineH` 19 处下游（选区/光标/滚动/行号映射）复核**（评审 P1-5） | **+55 ~ +95** |
| `Tests/`（3 文件 11 子类） | 8 假件 +Ascent 显式值（源兼容破坏的仓内面）+ T45-1..9 | +380 |
| `Win32FontSource.cpp` | **不入改动面**（direct 相回落 face0，§1.1——评审 P1-1） | 0 |
| `CMakeLists.txt` | **0** | 0 |

**预算**：公共头 **102 → 102** · API **+1 方法（纯虚 = 源兼容破坏，L45-6）+3 字段** · 用例 **599 → 608**（T45-1..9；607 显式改锚兑现）· 批计划三批不变。

---

## §5 测试规划（T45-1..9；★ = v1.1 修订）

| # | 用例 | 判据 |
|---|---|---|
| T45-1 | 斜体 run 命令流 | 每 run 一条；命中 run `font.italic==true`；C45-2 相 |
| T45-2 ★ | 斜体选面（**direct 相删除**） | 有真斜体面族 `FaceIdFor(f,w,true)` ⇒ 斜体 face；无斜体面族 ⇒ **返回常规 face（内部回落，非 0）**；`italic=false` 全表现状逐位 |
| T45-3 ★ | 行框（块级统一 + **max 归属判别相 + 判据一致相**） | 混字号 ⇒ totalHeight = `LineHeight(EffectiveLineBoxFont) × 行数` 恒乘法（逐位 vs 手算）；无 size 覆盖 ⇒ 与 Phase 44 逐位；★ **归属相：大字号 run 在第二行 ⇒ 首行行高同涨**（防「取首行字体」实现骗过）；★ **判据一致相（v1.3 重写，评审 P1-2——v1.2 版在常数 LineHeight 假件下恒绿无判别力）：采用「按 `font.size` 返回行高与 ascent」的专用假件**（如 `LineHeight = size × 1.2`、`Ascent = size × 0.8`；新增夹具，不动 8 个既有常数假件）：TextBox 密码态 + spans(size 放大) ⇒ `GetLineHeight()` = 基字体行高（钩子判死）；对照相：Normal 态同 spans ⇒ `GetLineHeight()` = 放大字号行高（证明相有判别力：钩子判活才变）；组合态同钩子覆盖。★ **TextBox preferred 同源相（v1.5，`:203`）**：专用假件 + 窗口相：size 覆盖激活 ⇒ `MultilinePreferred().height` = `GetMaxScrollOffset` 内容高（无幽灵滚动条）；无窗口 + `TextBoxTests.cpp:432` 夹具 ⇒ preferred 高仍 = 19.2 系逐位（不被 16.0f 兜底带歪）。★ **size 守卫相（v1.4，评审 v1.3 P0-1）**：基字体 Consolas + span `{family="Segoe UI"}`（size 恒 nullopt；专用假件两族行高/ascent 不同）⇒ `GetLineHeight()` / totalHeight / preferred 高与 Phase 44 逐位相等；weight-only / italic-only 同断言；对照相：加一个 size 覆盖 ⇒ 才变。★ **五站同源相（v1.3，P0-1；v1.4 注：本相依赖「6 实参替换」落地——替换前 `:584` 与 `:658` 两边同错成基字体而恒绿，须配负对照：回退 `:658` 实参 ⇒ 本相红）**：混字号 wrap 多行 ⇒ 末行 `lineY + lineHeight` = `block.y + totalHeight`（绘制逐行推进与 totalHeight 同源）；非 wrap / preferred 高同取助手字体行高（专用假件）。★ **A′ 归属相（v1.3，P0-2）**：构造 run(asc25,desc0,gap0) 与 box(asc20,desc10,gap10) 的异度量族假件 ⇒ box 判据取 A′ 最大者（run 字体），`pos.y ≥ lineTop`（无负位移）；防「按 LineHeight 取 max」实现骗过。 |
| T45-4 ★ | 基线共享（**整数基线判据**） | 同线 run 满足 `pos.y + A′(run.font)` = 常数（DIP，公共 `Ascent` 无头观测）**且经 §3.2 证明落同一整数基线行**；GL 无头 `glReadPixels` 相 |
| T45-5 | 等价护栏双相 | golden 快照（批 0 HEAD 录制 = Phase 44 基线）；两相逐位 |
| T45-6 | 负对照①判别力 | 回退 italic 覆盖 ⇒ 恰 T45-1/2 断言红 |
| T45-7 | 负对照②键完备投毒 | 投毒面含 **familyCache**；miss ⇒ 重算 |
| T45-8 | 两链一致（分相） | GDI 相：真面族基线重合 + 合成观感；GL 相：真面斜体 + 回落相；主断言 = 每链内整数基线重合；跨链沿既有容差 |
| T45-9 ★ | `Ascent` 接口相（**DIP 判据**） | 同一 measurer 实例、同一 dpi：GDI `Ascent(font) == tmAscent × 96/dpi`（**±1e-3**）；FT `Ascent(font) == floor(ascender/64) × 96/dpi`；假件显式值无害 |

---

## §6 闸门 2/4/5 裁决页（v1.1 依评审建议改判）

- [x] **闸门 2 · 斜体选面政策** — ✅ **已签（2026-11-09，用户授权）**：日期 2026-11-09 · 结论「**ⓑ**」（GDI 合成斜切 + GL 真斜体面 / 无则回落常规面；direct 相回落 face0，L45-1）· 圈选项 = ⓑ。以下为裁决材料原文：**ⓑ 可签**（评审原判可签；附条件 = P1-1 direct 相改对——v1.1 已改为 face0 回落声明并删 T45-2 direct 相）。
- [x] **闸门 4 · 行框口径** — ✅ **已签（2026-11-09，用户授权）**：日期 2026-11-09 · 结论「**ⓐ 块级统一行框**」（A′ 最大字体 + 仅 size 覆盖触发）· 圈选项 = ⓐ · 签字内容已含 L45-7 下沿可能超框、光标高/行距可见影响、J1 行为变更。以下为裁决材料原文：**可签 ⓐ（块级统一行框；评审 v1.6 放行）**——签字内容含 **L45-7 下沿可能超框的已知代价** 与 **P1-4 光标高/行距可见影响确认**；另含 **J1 行为变更**（TextBox 组合期×spans 布局订正，见 §3.1）。
- [x] **闸门 5 · `TextMeasurer::Ascent`** — ✅ **已签（2026-11-09，用户授权）**：日期 2026-11-09 · 结论「**ⓐ +1 纯虚 `Ascent`（DIP 契约）**」· 圈选项 = ⓐ · 含 L45-6 源兼容破坏。以下为裁决材料原文：**修后可签 ⓐ**（评审原判缓——v1.1 已补 ⓑ 竞争方案 RunSlice.baselineOffset 的正面否证（§1.3）+ 单位契约钉死 DIP（P0-2）+ T45-9 判据 ±1e-3）。

---

## §7 记账更新（v1.1 修订）

| # | 项 | 处置 |
|---|---|---|
| L45-1 | GL 不合成斜切（C36-4 同族扩容） | **v1.1 并入：direct 相斜体请求回落 face0**（单面池，`Win32FontSource.cpp:308-315`；重启条件 = direct 消费者出现斜体需求）；被拒项「GL 合成」与本条合并（评审 P2-6，不重复记） |
| **L45-6 新** | **`TextMeasurer` 纯虚 = 源兼容破坏** | 下游自定义 `TextMeasurer` 子类须补 `Ascent` 实现才能编译；对内 11 子类随批落地；对外在 usage.md/交付说明标注（评审 P1-5） |
| L45-8 新 | 既有缺陷：`TextWidget.cpp:704` 单行 `m_prefRuns` 缓存键仅含 `m_styledTextRevision / m_prefRunsIdentity / m_prefRunsDpi`，不含任何字体字段（改基字体 weight/family/size 不失效，今天即有；加 italic 多一个同症状入口） | **非本 Phase 引入、不在本 Phase 修**（不计入 v1.7 回归）；重启条件 = 该缓存被实测命中陈旧字体 |
| L44 I44-7 结账 | 「行高必须且仅来自基控件字体」（`TextWidget.cpp:823`） | 由本 Phase 取代 = 行高恒来自同一助手 `EffectiveLineBoxFont`（§3.1）；详设改注释 |
| L45-5 | 逐行行高 | 维持（重启条件 = 行距紧凑真实需求） |
| L45-7 新 | 行高取 A′ 最大字体的 `LineHeight`，descent 更大的另一 run 下沿可能超行框底（§3.1 代价） | 记账；重启条件 = 真实下沿裁切反馈（届时评估新增 `Descent` 接口，行高 = max(LineHeight(box), A′max + max descent)） |
| L44-9 结账连带 | **T44-11 基线锚重锚** | `FontEngine::Ascent` 语义修订（整数像素）改变 GL 基线子像素余量——T44-11 断言值随 L44-9 结账重锚（详设批内执行，显式登记） |
| L45-2/3/4 | 维持需求稿口径 | — |

---

## §8 修订记录

- **v1.8（2026-11-09）★ 闸门 2/4/5 签字落记录（签字纪律）**：用户原话「按照闸门可签的来做吧，我授权签字，开始详细设计吧」——授权按评审「可签」结论逐闸门落记录：闸门 2 = ⓑ、闸门 4 = ⓐ、闸门 5 = ⓐ（§6 已勾）。初设稿进入详设阶段，详设稿 = `phase45-styled-spans-expressiveness-detailed-design.md`（v1.0 待评审）。产品码与测试零改动。

- **v1.7（2026-11-09）★ v1.6 评审放行（P0×0 / P1×1 / P2×4）——随手带入，无机制变更**：① P1-1：FaceIdFor 计数订正 11 调用点，4 个度量点（`FontEngine.cpp:629/:657/:650/:667`）逐个点名传 italic（§2.4）。② P2-1：`:704` `m_prefRuns` 键缺字体维 = 既有缺陷，记 **L45-8**（不在本 Phase 修）。③ P2-2：italic 位置写明「weight 之后末位」（§2.1）。④ P2-3：闸门 4 改「可签 ⓐ」（§6）。⑤ P2-4：`docs/dev/README.md:98` 仍未续订（GBK，提交批处理）。⑥ 批 0 组合期夹具核查结果落入 §3.1（存量零冲击）。**闸门签字仍须用户落「日期+结论+圈选项」记录（签字纪律），评审结论不代行拍板。**

- **v1.6（2026-11-09）★ v1.5 评审吸收（不放行；P0×1 / P1×1 / P2×3；v1.5 全部处置评审认可）**：① **P0-1（`:498` 与钩子分叉）**：已核 `TextWidget.cpp:498` 仅 `HasActiveSpans() && text == m_text`、`TextBox.cpp:212-214` Normal 态直通 `m_text` ⇒ 组合期布局判活而钩子判死，折行/行高/字形三方分叉成立。选 (a)：J1 `:498` + J2 `:652/:774/:814` 换钩子、J3 TextBox 三站纳入，新增 §3.1 **判据站点穷举表**；**如实登记行为变更**（TextBox 组合期×spans 布局由 spans 分支变基字体分支 = 订正 Phase 44 既有布局/绘制不一致，仅限该角落）并立**批 0 存量夹具核查**前置；T45-3 判据一致相扩布局折行一路。② **P1-1（ResolveRunFont 语义）**：定序「字形字体 vs 行框字体 = 同集合兄弟概念」，字形字号与行高基准同一性升为契约。③ **P2-1**：§4 TextWidget 行补 `:823` 注释改写；**P2-2**：下游消费行写成 `:430`/`:584` 吃 #1、`:666` 吃 #2；**P2-3**：`docs/dev/README.md:24/:98` 仍未改（GBK 编码，须提交批以编码安全方式续订至 v1.6）。

- **v1.5（2026-11-09）★ v1.4 评审吸收（不放行；P0×1 / P1×2 / P2×3；v1.4 的 size 守卫等处置评审全认可）**：① **P0-1（漏 `TextBox.cpp:203`）**：全仓 grep 复核成立——`MultilinePreferred` 独立直取基字体，与 `:628` 新旧不同源会复现 `:166-172` 警告的幽灵滚动条 ⇒ §3.1 新增**全仓 8 实参 + 下游/转发穷举表**（治本：三次同类缺陷改表关死）。**处置偏离评审建议**：不改调 `GetLineHeight()`（无窗口恒 16.0f，而 `:192` 判空用 `ResolveMeasurer()`，`TextBoxTests.cpp:432` 夹具 19.2f 无窗口 ⇒ 会 19.2→16 撞红线 1），改 `measurer->LineHeight(EffectiveLineBoxFont(*measurer))`（measurer 身份不变）；T45-3 增 preferred=内容高相。② **P1-1（run 字体集合机制）**：选 (b) 助手遍历 `m_spans`，抽 `static ResolveRunFont` 与 `:374-381` 共用，规则单一真相源；§2.3 补 FT `Ascent` 缓存键依据（faceId 含 italic 自动分档）。③ **P1-2（I44-7）**：登记「由 Phase 45 取代：行高恒来自同一助手」，原意不违，详设同步改 `:823` 注释。④ **P2-1**：§1.2 的「13 处消费点」订正为 7 消费点口径；**P2-2**：§3.2 锚定 `lineTop = 块顶 + row×lineHeight`（`:666`）；**P2-3**：`docs/dev/README.md:24/:98` 仍为 GBK 编码，**本版未改**，须在提交批以编码安全方式续订到 v1.5。⑤ 预告带入：8 个测试假件批 1 一次性补 `Ascent`。

- **v1.4（2026-11-09）★ v1.3 评审吸收（不放行；P0×1 / P1×2；v1.3 的 7 条处置评审复核全部认可）**：① **P0-1（A′ 判据丢 size 守卫）**：纯 family span（Phase 44 已交付，`TextBoxTests.cpp:3561`）在跨族行框差异下会改行高，撞 C45-2，并经 `GetLineHeight()` 7 调用点漂移滚动/光标/定行 ⇒ §3.1 `EffectiveLineBoxFont` 进入条件 = `SpansActiveForLayout() && 至少一个 span size 覆盖非 nullopt`，其余维度（family/weight/color/italic）全不触发（italic：O45-3 同族 hhea 一致，进判据无新值）；§3.1 不变式补 C45-2 结构性成立；T45-3 增 size 守卫相（含 weight-only/italic-only 与加 size 对照）。② **P1-1（替换面）**：`:666` 是 `:658` 的消费式，真正替换面 = 6 实参（`:423/:658/:718/:737/:824/:829`），`:430/:584/:666` 下游零改动 ⇒ §3.1 表头、`:666` 行、§4 表同步订正。③ **P1-2（五站同源相）**：注明依赖 6 实参替换落地，配负对照（回退 `:658` ⇒ 红），防重演恒绿。④ **带入详设的预告**：8 个测试假件须批 1 一次性补 `Ascent`；`docs/dev/README.md:24/:98` 仍写初设稿 v1.0，随提交批续订。

- **v1.3（2026-11-09）★ v1.2 评审吸收（结论 = 不放行；P0×2 / P1×2 / P2×3；v1.2 补增 P1 评审确认修对）**：① **P0-1（五站漏出）**：v1.1 重写 §3.1 误删 v1.0「TextWidget 五站」枚举（评审 grep `658|718|737|824|829` 零命中；已核 `TextWidget.cpp:658/:666` 仍 `measurer.LineHeight(m_style.font.value)` 与 totalHeight 失配）⇒ §3.1 恢复五站表（实参逐点替换为助手值，+0 接口），T45-3 增五站同源相。② **P0-2（两套度量族）**：LineHeight（otmMac*/hhea）与 Ascent（tmAscent/ascender）不同族，「盒最大 ⇒ ascent 最大」不成立（评审反例 + 建模 0.44–0.88% 失配）⇒ **box 判据改 A′ 最大者**（并列取 LineHeight 大者再基字体），§3.2 前提写成定理（A′(run) ≤ A′(box) 按构造），代价登记 **L45-7**（下沿可能超行框底，`Descent` 接口不入本期），T45-3 增 A′ 归属相。③ **P1-1（无 measurer 分支）**：已核 `TextBox.cpp:586-593`——助手只在**窗口分支内**调用，无窗口 `return 16.0f` 原样，不走 `ResolveMeasurer()`（避免 `TextBoxTests.cpp:432` 夹具 16→19.2）；存量受影响用例清单 = 空。④ **P1-2（判据一致相恒绿）**：改用按 `font.size` 返回行高/ascent 的专用假件 + Normal 态对照相。⑤ **P2-1**：T45-4 判据 = DIP ±1e-3 + 非 96dpi ±1px，评审中点变体留批 1 spike 决定。⑥ **P2-2**：钩子覆写声明于 `TextBox.h` protected 区（`:191` 起）。⑦ **P2-3**：§4 标题订正；`docs/dev/README.md:24/:98`（仍写初设稿 v1.0）**随提交批续订，本版未改 README**。⑧ **预算**：TextWidget +115→+125、TextBox +50~+90→+55~+95；用例 608 不变（新相并入既有 T45-3）。⑨ **闸门**：2 = 可签 ⓑ / 5 = 可签 ⓐ（维持）/ 4 = 口径已重写（A′ 判据 + 五站），**待评审复核后可签**。

- **v1.2（2026-11-08）★ 评审补增 P1 吸收（P0-3 修法引入的激活判据不一致——v1.1 §3.1 把判据说反了）**：① **缺陷**：`EffectiveLineBoxFont` 判据 = `HasActiveSpans()`（`TextWidget.h:199-201`，无 echo/composing 条件），而 TextBox 绘制侧三站点（`TextBox.cpp:495` / `:526` / `:1536`）= `HasActiveSpans() && m_echoMode == Normal && !m_isComposing` ⇒ 密码态/组合期设 spans 时「布局判活（按 max span 字体抬行高）、绘制判死（掩码点全基字体）」＝ 行距撑大 + 字形不变 = 一片空白。② **修法 = 判据单点化**：新增 protected 虚钩子 `SpansActiveForLayout()`（基类 = `HasActiveSpans()`，对 TextWidget/Label 逐位等价 ⇒ C45-1/C45-2 不受扰）；TextBox 覆写为三条件版（与绘制侧逐字同一表达式）⇒ 布局与绘制恒同一份判据；详设可选把三绘制站点改调钩子（行为中性、单一真相源，批 2 定）。③ **T45-3 补判据一致相**：密码态 + spans ⇒ `GetLineHeight()` 仍 = 基字体行高（组合态同钩子覆盖）。④ TextWidget 预算 +110 → +115（虚钩子）。
- **v1.1（2026-11-08）★ 初设评审吸收（结论 = 不放行进详设；P0×3 / P1×5 / P2×6 全部逐条回码亲核后处置）**：
  ① **P0-1（基线公式两链不闭合）**：GL 双层 floor（`GLRenderer.cpp:254` + `Renderer.cpp:69-71` 只折 pos）+ GDI `static_cast<LONG>` 截断（`:922-923`）使 DIP 域逐 run 减法必然失配（评审建模 33.4%/1.99px）。**修法 = 像素空间整数化**：定义 `A′ = AscentIntPx × 96/dpi`（GDI = tmAscent 整数；GL = floor(ascender/64)），发射式 `pos.y = lineTop + A′(box) − A′(run)`——整数位移与截断/floor 可交换 ⇒ 同行恒同一整数基线（§3.2 闭合证明）。`FontEngine::Ascent` 语义修订（整数像素）+ T44-11 重锚登记。
  ② **P0-2（Ascent 单位自相矛盾）**：`:664` 无 96/dpi 换算 vs `:634` LineHeight 有 ⇒ **Ascent 契约钉死 DIP**；GDI = `tmAscent × 96/dpi`、FT = 薄包装（v1.0 直接转发作废）；T45-9 改 ±1e-3 判据。
  ③ **P0-3（行框字体不可达）**：`:423` 先于 `:501`、`TextBox.cpp:443` 喂 `BuildDisplayText()` + `:497-498` 身份护栏 ⇒ v1.0「产物上取 max」不可实现且 TextBox 恒空集。**修法 = protected 助手 `EffectiveLineBoxFont(TextMeasurer&)`**（成员函数无签名改动、两调用点零波及、掩码态由 Phase 44 已有的 echo 判据覆盖）——§3.1 重写；`:147` 注释同步。
  ④ **P1-1（direct 相空转）**：`Win32FontSource.cpp:308-315` 单面池 ⇒ 「bit0 优先」不可达。处置 = **如实收窄为 face0 回落**（不入改动面），T45-2 删 direct 相，L45-1 并入登记。
  ⑤ **P1-2（ⓑ 否证循环论证）**：补第三方案 `RunSlice.baselineOffset`（`TextWidget.h:128-134` 物化点挂载）的正面否证——只搬运消费侧、不解决生产侧（ascent 产生仍须公共缝）⇒ ⓐ 成立（§1.3）。
  ⑥ **P1-3（uint8_t 口径）**：改 **`bool italic = false`**（Font.h / SpanStyle optional<bool> / FaceIdFor bool）。
  ⑦ **P1-4（光标高可见回归）**：闸门 4 签字页显式列入「块内大字号 ⇒ 全块行框/选区/光标高随 max」（`TextMeasurer.h:186-190` 契约 + `TextBox.cpp:813`）；接受与否 = 签字内容，不沉默。
  ⑧ **P1-5（预算与源兼容）**：TextBox 改 **+50 ~ +90**（含 lineH 下游复核注记）；API 行 + §7 新增 **L45-6 源兼容破坏**登记。
  ⑨ **P2×6**：512 face 措辞订正（CJK 为子集表述）· 视觉代价改「用户签字时确认」· L44-9 结账引用 D45-4 条件（7px 非恒值）· T45-3 补 **max 归属判别相**（第二行大字号 ⇒ 首行同涨）· 「零新增测量」改「零新增接口，测量走既有缓存（首次 miss O(R)）」· L45-1 与被拒项合并。
  ⑩ **闸门改判**：2 = 可签（附条件已满足）/ 4 = 随本版可签（含 P1-4 确认项）/ 5 = 修后可签 ⓐ——签字页 §6。
  ⑪ **保留项（评审 ✅1-✅5 实证确认）**：11 子类行号清单 · GDI 两口径分离方向 · 块级统一消解 P0-3/P0-4 的结构 · 608 改锚 · `008daff` 零改动与基线 599/102/218。

---

> 初设稿 v1.8（2026-11-09）：**初设放行进详设；闸门 2/4/5 已由用户授权签字；详设已放行进批 0**（条 74）。
