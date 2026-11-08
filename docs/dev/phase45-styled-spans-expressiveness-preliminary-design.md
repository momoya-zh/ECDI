# Phase 45 · styled spans 表达力扩展（斜体 + 混字号，#89）· 初步设计稿

> 状态：**v1.2（2026-11-08）：初设评审 v1.0 吸收毕（P0×3 / P1×5 / P2×6）+ 评审补增 P1（P0-3 修法引入的激活判据不一致）已修（§3.1 / §8 v1.2）；闸门 2 = 可签 ⓑ、闸门 4 = 可签、闸门 5 = 可签 ⓐ**。★ **条 74：状态由评审结论决定**。上游 = 需求稿 **v1.4**（评审二轮放行；闸门 1/3 已签；O45-1..5 勘察落盘）。★ 产品码自 `008daff` 零改动（`git diff 008daff..HEAD -- ECDI/` 为空）⇒ 需求稿 K45-1..14 行号快照有效。基线：用例 **599** · 公共头 **102** · 设计文档 **218**。

**红线继承**（需求稿四条全文有效）：零 span 逐位等价 · 测画同源 · 命令流与 DrawText 执行体零改动（豁免面 = 字体实例化 / face 选择层 + LOGFONT 位段）· 编辑器通路不动（L44-1）。

---

## §1 三个闸门的裁决材料（v1.1 修订）

### 1.1 闸门 2 · 斜体选面政策 → **ⓑ 可签**（D45-7；v1.1 修 P1-1 direct 相）

勘察读数：**512 face 全扫中 CJK 族 `fsSelection` bit0 全 0**（真斜体面仅在拉丁族）；GDI 合成斜切实锤且宽度增量 +0.00%；真斜体面改 advance（±0.4~2.8%）。ⓐ 拒（CJK 斜体全静默，与已签 ③-1 矛盾）；ⓒ 维持出局（红线 3）。**ⓑ = GDI 合成 + GL 真斜体面/无则回落常规（C36-4 同族扩容）**。

- **非 direct 相**：候选过滤「bit0 出局」→「请求 italic ⇒ 只收 bit0 面；池空 ⇒ 回落常规池（= italic=false 结果）」。
- **direct 相（v1.1 按评审 P1-1 修订——v1.0 承诺了做不到的事）**：`Win32FontSource.cpp:308-315` 的直查探针只 push **单 face（索引 0）** ⇒ direct 族候选池恒 1 面，「按 bit0 优先」**结构上不可达**。处置 = **如实收窄**：direct 路径斜体请求**回落 face0**（Phase 36「直查豁免」语义保持，`Win32FontSource.cpp` 不入改动面）；登记入 L45-1（重启条件 = direct 文件消费者出现斜体需求，届时扩 numFaces 枚举）。**T45-2 的 direct 相删除**。
- **回落语义统一收在 `FaceIdFor` 内部、绝不向 GL 返回 0** ⇒ GL 执行体零改动（`GLRenderer.cpp:240` 传参即走——红线 3）。

### 1.2 闸门 4 · 行框口径 → **ⓐ 块级统一行框，随本版可签**（D45-8；C45-3 修订申明）

全文行高 = `A′ 行框最大的 run 字体`之行框（**块级统一**，所有行恒同高）：

- **P0-4 浮点守卫结构性成立**：totalHeight 恒 `lineHeight × 行数`（`TextWidget.cpp:584` 原式），Σ 路径不存在。
- **TextBox 8.5.2 固定行高契约零改动**（P0-3 处置 = ⓐ）：`GetLineHeight()`（`TextBox.cpp:586-593`）**算术原样**（含 `:587` 注释与 13 处消费点），仅**取值来源**从「基字体」改为「有效行框字体」——机制见 §3.1（v1.0 的「在 BuildRunsForRange 产物上取 max」被评审 P0-3 证伪：行框在 `:423` 定死、runs 在 `:501` 逐行才产生，且 `TextBox.cpp:443` 喂 `BuildDisplayText()` + `TextWidget.cpp:497-498` 串身份护栏 ⇒ TextBox 路径 spans 恒不可达——v1.1 已改走成员助手通路）。
- **★ 已知可见影响（闸门 4 签字项，v1.1 按评审 P1-4 显式列出）**：块内出现大字号 run ⇒ **全块**行框/选区 rect/**光标高**随 max 变大——`TextMeasurer.h:186-190` 契约明写「光标高 = 行框高」、`TextBox.cpp:813` 光标高直取 `GetLineHeight()`。这是只读显示面上的真实可见变化（非 L44-1 编辑通路）；**是否接受 = 闸门 4 签字内容**（拒绝则需改「光标高 = LineHeight(基字体)」放弃 I31 口径——同为口径变更，二选一，不沉默）。③-1 场景内标题行稀疏 ⇒ 行距撑高的视觉代价**由用户在签字时确认**（v1.0 替用户下结论，评审 P2-2 订正）。
- ⓒ（混字号不进 TextBox）出局：掏空已签 ③-1；ⓑ（逐行 max）记 **L45-5**。

### 1.3 闸门 5 · `TextMeasurer` 接口 → **ⓐ +1 纯虚 `Ascent`（DIP 契约），修后可签**（D45-9）

- **ⓑ 否证补全（v1.1 按评审 P1-2——v1.0 的「无通路」是循环论证）**：最强竞争方案 = `RunSlice` 已携带完整 `Font`（`TextWidget.h:128-134`），在唯一物化点（`TextWidget.cpp:374-381`）算 `baselineOffset = A′(box) − A′(run)` 挂进 `RunSlice` ⇒ 消费侧接口 +0。**否证点 = 该方案只搬运消费侧、不解决生产侧**：ascent 值仍必须从链上产生，widget 层唯一测量缝 = 公共 `TextMeasurer`（backend 内部态不可达）⇒ 任何 +0 变体都需要一个非公共的产生机制；且 `baselineOffset` 冻结 DIP 值于测量时 = A′ 的冗余拷贝。ⓐ 成立。
- **★ 单位契约钉死（v1.1 按评审 P0-2——v1.0 两实现都返回裸物理像素，DPI ≠ 96 时公式两边单位不同）**：`Ascent(const Font&)` 返回 **DIP**，且返回的是**链内整数像素 ascent 的 DIP 折算**（A′，定义见 §3.2——这是 P0-1 整数化方案的承接面）：GDI 实现 = `tmAscent × 96 / dpi`（`tmAscent` 本身整数）；FT 实现 = `floor(face->size->metrics.ascender / 64) × 96 / dpi` **薄包装**（v1.0「直接转发 FontEngine::Ascent」作废——`:664` 原体无 96/dpi 换算，对比同文件 `:634` LineHeight 的换算先例）。T45-9 判据改为「同一 measurer 实例、同一 dpi 下 `Ascent(font) == tmAscent × 96/dpi`（±1e-3）」。
- 波及面 = **11 子类**（产品 2 + RecordingBackend 门面 + 测试假件 8，行号清单见 v1.0 §1.3，评审 ✅1 复核无误）；★ **纯虚 = 源兼容破坏**（下游任何自定义子类编译不过）——记 §7 L45-6（评审 P1-5）。

---

## §2 接口设计（公共面）

### 2.1 `Font.h` +italic（v1.1 按评审 P1-3 改 **bool**）

`bool italic = false;`（v1.0 的 `uint8_t` 作废：与全仓 `uint16_t weight` 口径不符、隐式窄化有严格告警风险、非 0/1 语义未定义；与「默认值 = 今日逐位」契约一致）。声明序 = size → family → weight → **italic（末尾）**；C7560 聚合初始化面 8 处全在测试（O45-4）⇒ 追加安全。GDI 消费 = LOGFONT `lfItalic`；GL 消费 = `FaceIdFor` 第三参 `bool`。

### 2.2 `TextSpan.h`：`SpanStyle` +2 override

`std::optional<bool> italic`、`std::optional<float> size`（DIP 绝对值）；`:10` 契约注释解禁。物化点 = `TextWidget.cpp:374-381` `rFont` 组装 +2 分支。

### 2.3 `TextMeasurer.h` +1 纯虚

`virtual float Ascent(const Font& font) = 0;`——返回 **A′（DIP，§3.2 定义）**；语义注释 =「该链整数像素基线偏移的 DIP 折算——与 LineHeight（行框口径，`otmMac*`）**分属两个口径**」。GDI 侧实现沿 font 键缓存（键扩 italic 维）。

### 2.4 `FaceIdFor` 线程化（D45-10）

签名 `FaceIdFor(const std::string& family, std::uint16_t weight, bool italic = false)`；**`familyCache` 键扩维** `(family, weight, italic)`（P0-2）；12 行调用点随 `Font.italic` 传参。选面两相 + 回落 = §1.1。

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
// SpansActiveForLayout() 为假 ⇒ 返回 m_style.font.value（逐位今日）；
// 为真 ⇒ 对「基字体 + 各 span 覆盖物化出的 run 字体集合」取 LineHeight 最大者
//   （O(R) 次 LineHeight——零新增接口，测量走既有缓存，首次 miss O(R)）
```

- `BuildTextLayout` **签名零改动**（它本就是成员函数，`:422` 的 `const Font& font = m_style.font.value` 改读 `EffectiveLineBoxFont(measurer)`——`:423` 行框、`:584` 乘法原式、`:430` 空文本相全部原样；两个调用点 `TextBox.cpp:443` / `TextWidget.cpp:609` 零改动）。
- **TextBox 判据一致性（v1.2 按评审补增 P1 重写——v1.1 把激活判据说反了）**：绘制侧三站点（`TextBox.cpp:495` / `:526` / `:1536`）的激活判据 = `HasActiveSpans() && m_echoMode == Normal && !m_isComposing`，**比 `HasActiveSpans()`（`TextWidget.h:199-201`）多两道门**；v1.1 的助手只看后者 ⇒ 密码态/组合期设了 spans 时「布局判活（按 max span 字体抬行高）、绘制判死（掩码点全基字体）」＝ 行距被撑大而字形不变（一片空白）。修法 = **判据单点化**：`SpansActiveForLayout()` 虚钩子（基类 = `HasActiveSpans()`，对 TextWidget/Label 逐位等价 ⇒ C45-1/C45-2 不受扰；TextBox 覆写三条件版），`EffectiveLineBoxFont` 改读钩子 ⇒ **布局与绘制的激活判据恒同一份**；`GetLineHeight()`（`:589-593`）换读 `EffectiveLineBoxFont(...)`——算术与 13 处消费点零改动。★ 详设可选实施注记：三绘制站点可改调钩子（行为中性、单一真相源），批 2 定。
- 助手本身即评审 P0-3 要求的「取数机制显式化」：既非「产物上取」（不可达），也非改 `BuildTextLayout` 签名（成员函数无需）；`TextWidget.h:147` totalHeight 注释同步补「行框字体 = EffectiveLineBoxFont」。

### 3.2 per-run 基线发射（v1.1 按评审 P0-1 重写——像素空间整数化，两层 floor/截断闭合）

**A′ 定义**：`A′(font) = AscentIntPx(font) × 96 / dpi`（DIP）。其中 **AscentIntPx**（链内整数像素 ascent）：GDI = `tmAscent`（TEXTMETRIC，本就是整数设备像素）；GL/FT = `floor(face->size->metrics.ascender / 64)`（★ **`FontEngine::Ascent` 返回值语义修订为整数像素**——其唯一消费点 `GLRenderer.cpp:254` 的外层 `floor(...)` 变 no-op，**:254 一行不动**（红线 3）；连带 = GL 全行基线上移 <1px 子像素余量 + **T44-11 基线锚随 L44-9 结账重锚**（显式登记，§7））。

**发射公式（全 DIP）**：`pos.y = lineTop + A′(boxFont) − A′(runFont)`；`baseline = pos.y + A′(runFont) = lineTop + A′(boxFont)`（DIP 恒等式——T45-4 判据形式不变，语义变为 A′）。

**两链闭合证明（整数位移与量化可交换）**：`Renderer::ExecuteCommand` 只折 pos（`Renderer.cpp:69-71`，font 原样）⇒ 链上收到 `pos.y_px = scale·lineTop_px + (AscentIntPx(box) − AscentIntPx(run))`——第二项为**精确整数**（GDI `tmAscent` 整数；GL floor 后整数；|值| < 2^24 float 精确表示）⇒
- GDI：`drawY = static_cast<LONG>(pos.y_px)`（`:922-923` 向零截断）⇒ `trunc(x + Δint) = trunc(x) + Δint` ⇒ `baseline_eff = trunc(scale·lineTop_px) + AscentIntPx(box)`——**与 run 无关，同行恒等**；
- GL：`floor(pos.y_px) + floor(Ascent_px(run))`（`:254`，外层 floor 对整数 no-op）⇒ `floor(scale·lineTop_px) + AscentIntPx(box)`——**同行恒等**。

⇒ 同线 run 基线落在**同一整数像素行**（评审同型建模 10 万组 0% 不等）；v1.0 的「33.4% 差 1px / 1.99px」由两层 floor 与非整数 A′ 的组合产生，本公式消解。跨链基线-行顶差 = 各链既有 ascent 口径差（今日单字体整行同偏，非新增）——验收 = **每链内重合为主断言**（T45-4），跨链沿既有容差。

### 3.3 等价性结构（C45-1 / C45-2 / P0-4）

零 span ⇒ `EffectiveLineBoxFont` 返回基字体、`rFont` 组装、`FaceIdFor(false)`、LOGFONT 位段全现状 ⇒ 逐位；Phase 44 spans ⇒ max = 基字体 ⇒ 全链逐位（T45-5 golden 双相）；行框恒乘法 ⇒ P0-4 无 Σ 路径。指纹：`m_prefFont`/`m_wrapFont` 逐字段比较加 `italic`（K45-7）；行表键 font 维 +italic（`:506-508`）；span revision 维已覆盖 span 侧。

---

## §4 改动清单与规模预算（v1.1 修订：+EffectiveLineBoxFont、TextBox 区间放宽、源兼容破坏入账）

| 文件 | 改动 | 预估 |
|---|---|---|
| `Font.h` / `TextSpan.h` / `TextMeasurer.h` | +bool italic / +2 override / +Ascent 纯虚（§2） | +25 |
| `FontEngine.h/.cpp` | FaceIdFor 三参 + 12 行线程化 + familyCache 键 + 两相回落 + **Ascent 语义修订（整数像素）** + ascent/lineHeight/measure 键扩维 | +65 |
| `GDIBackend.cpp` / `GDITextMeasurer.cpp` | LOGFONT 位段 ×2 + HFONT/双键扩维 + **Ascent 实现（tmAscent × 96/dpi + 缓存）** | +40 |
| `GLRenderer.cpp` | `:240` 传参（执行体 `:254` 不动） | +1 |
| `TextWidget.h/.cpp` | rFont +2 分支 + **EffectiveLineBoxFont 助手 + `SpansActiveForLayout()` 虚钩子（v1.2）** + per-run y + 指纹 2 站扩维 + `:147` 注释 | +115 |
| `TextBox.cpp` | GetLineHeight 取值来源 + per-run y + 行表键 +italic；★ **7 处 `GetLineHeight` 直取 + `lineH` 19 处下游（选区/光标/滚动/行号映射）复核**（评审 P1-5） | **+50 ~ +90** |
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
| T45-3 ★ | 行框（块级统一 + **max 归属判别相 + 判据一致相**） | 混字号 ⇒ totalHeight = `LineHeight(EffectiveLineBoxFont) × 行数` 恒乘法（逐位 vs 手算）；无 size 覆盖 ⇒ 与 Phase 44 逐位；★ **归属相：大字号 run 在第二行 ⇒ 首行行高同涨**（防「取首行字体」实现骗过）；★ **判据一致相（v1.2）：TextBox 密码态 + spans ⇒ `GetLineHeight()` 仍 = 基字体行高**（钩子判死 ⇒ 无空白撑高；组合态由同一钩子单点化覆盖） |
| T45-4 ★ | 基线共享（**整数基线判据**） | 同线 run 满足 `pos.y + A′(run.font)` = 常数（DIP，公共 `Ascent` 无头观测）**且经 §3.2 证明落同一整数基线行**；GL 无头 `glReadPixels` 相 |
| T45-5 | 等价护栏双相 | golden 快照（批 0 HEAD 录制 = Phase 44 基线）；两相逐位 |
| T45-6 | 负对照①判别力 | 回退 italic 覆盖 ⇒ 恰 T45-1/2 断言红 |
| T45-7 | 负对照②键完备投毒 | 投毒面含 **familyCache**；miss ⇒ 重算 |
| T45-8 | 两链一致（分相） | GDI 相：真面族基线重合 + 合成观感；GL 相：真面斜体 + 回落相；主断言 = 每链内整数基线重合；跨链沿既有容差 |
| T45-9 ★ | `Ascent` 接口相（**DIP 判据**） | 同一 measurer 实例、同一 dpi：GDI `Ascent(font) == tmAscent × 96/dpi`（**±1e-3**）；FT `Ascent(font) == floor(ascender/64) × 96/dpi`；假件显式值无害 |

---

## §6 闸门 2/4/5 裁决页（v1.1 依评审建议改判）

- [ ] **闸门 2 · 斜体选面政策**：**ⓑ 可签**（评审原判可签；附条件 = P1-1 direct 相改对——v1.1 已改为 face0 回落声明并删 T45-2 direct 相）。
- [ ] **闸门 4 · 行框口径**：**随本版可签**（评审原判「P0-3 未解不宜签」——v1.1 已给取数机制 §3.1；★ 签字内容含 **P1-4 光标高/行距可见影响确认**）。
- [ ] **闸门 5 · `TextMeasurer::Ascent`**：**修后可签 ⓐ**（评审原判缓——v1.1 已补 ⓑ 竞争方案 RunSlice.baselineOffset 的正面否证（§1.3）+ 单位契约钉死 DIP（P0-2）+ T45-9 判据 ±1e-3）。

---

## §7 记账更新（v1.1 修订）

| # | 项 | 处置 |
|---|---|---|
| L45-1 | GL 不合成斜切（C36-4 同族扩容） | **v1.1 并入：direct 相斜体请求回落 face0**（单面池，`Win32FontSource.cpp:308-315`；重启条件 = direct 消费者出现斜体需求）；被拒项「GL 合成」与本条合并（评审 P2-6，不重复记） |
| **L45-6 新** | **`TextMeasurer` 纯虚 = 源兼容破坏** | 下游自定义 `TextMeasurer` 子类须补 `Ascent` 实现才能编译；对内 11 子类随批落地；对外在 usage.md/交付说明标注（评审 P1-5） |
| L45-5 | 逐行行高 | 维持（重启条件 = 行距紧凑真实需求） |
| L44-9 结账连带 | **T44-11 基线锚重锚** | `FontEngine::Ascent` 语义修订（整数像素）改变 GL 基线子像素余量——T44-11 断言值随 L44-9 结账重锚（详设批内执行，显式登记） |
| L45-2/3/4 | 维持需求稿口径 | — |

---

## §8 修订记录

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

> 初设稿 v1.2（2026-11-08）：**初设评审吸收毕（含补增 P1）；闸门 2/4/5 就绪待签**——评审通过 + 三闸门签字后进详设（条 74）。
