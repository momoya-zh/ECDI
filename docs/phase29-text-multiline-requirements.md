# Phase 29 · 文本多行能力（Label word-wrap）—— 需求稿（v1.1）

> 来源：`roadmap-deferred.md` **#52**（**harness 高保真模型评估的最大缺口**——「Label 无 word-wrap：长文本一行流出气泡被裁」；2026-10-05 登记后用户 2026-10-06 拍板立项）。★ 定位先行：**能力新增**——与 Phase 28（#50 测量/绘制成本）**互补不合并**：28 让测量便宜 + 行级绘制基建，29 是新能力。
> 状态：**v1.1**（2026-10-06）——**✅ 评审通过（PASS → Preliminary Design）**（外部评审 2026-10-06：15 项分级 = **9 ✅ PASS + 6 🟡 初设必须解决**——四个关键语义问题：① wrap 宽度来源 ② AutoSize+wrap 循环 ③ **TextLayout 共享**（评审唯一「强烈建议初设解决」架构点）④ TextMeasurer Fit API 三方案比较；**默认关升格最强红线**（`wrap=false` 走 100% 原路径，非仅像素相等）；吸收明细见 §7 v1.1）
> 定位：**显示型文本的自动折行能力**——`TextWidget`/`Label` 增 word-wrap，多行 preferred 高度语义，宽度约束测量原语。

---

## 1. 背景

### 1.1 消费者场景（A5 端到端验收的锚）

`ECDI_fake_harness` 高保真模型：聊天气泡卡片——**固定宽度**的 Label、长文本折成多行、卡片高度随行数自适应。现状：Label 单行绘制，长文本一行流出气泡被裁，只能应用侧手工截断。这是 9 项消费侧缺口之首，也是唯一无法应用侧廉价新方案替代的（#54 动画可 Timer 模拟、#51③ 对齐可子类化，唯独断行算法必须框架提供）。

### 1.2 问题的精确定义

全框架**没有任何宽度断行（wrap）代码**（K2）；唯一的"多行"是 TextBox 的**显式 `\n` 分段**（K3——无断行）。display-only 控件（Label/Button/自定义 TextWidget 子类）只有单行一条路。本 Phase 新增：**显示型文本的自动折行**——给定宽度约束，把文本折成多行绘制，preferred 高度 = 行数 × 行高。

### 1.3 与相邻条目的边界（互补不重复）

| 条目 | 关系 |
|---|---|
| **Phase 28**（#50 长文本测量/绘制成本） | **基建互补**：28 的 per-glyph advance memo 让断行所需的逐字形宽度查询变便宜、行级绘制基建可参照；**交互点**：wrap 后 preferred 依赖**宽度约束** ⇒ 28 的指纹键需扩展（K6——28 评审 §14 已预告 width constraint 进键） |
| **#51③**（Button 对齐，快修轮） | 独立小项；**交互点**：wrap 后"对齐"按行生效（每行独立水平对齐）——语义边界初设划清（D2） |
| **#53**（渲染表现力群） | 非目标（富文本/shaping 另立） |
| **#33**（按需建控件） | 无关（控件数量 vs 文本形态） |
| **#21**（脏区合帧） | 受益不实现 |

---

## 2. 现状勘察（K1–K8，全部带行号实测 2026-10-06）

- **K1 单行绘制是唯一路径**：`TextWidget::DrawTextContent` → `ctx.DrawText(整串)`（`TextWidget.cpp:107-119`）；Label/Button 均无 override——所有 display 文本一行到底。
- **K2 全库零断行代码**：`grep -in wrap ECDI/{src,include}` **零命中**（2026-10-06 实测）——框架内没有任何可复用的断行算法。
- **K3 TextBox 行模型 = 显式 `\n` 分段**：`RecalculateLines` 只扫 `\n`（`TextBox.cpp:388-401`，`m_lineStarts` 码点索引）；横向溢出走「Clip + viewX」（`TextBox.cpp:1321`——「超宽/滚动由 Clip + viewX 处理」）⇒ **无宽度断行**；行绘制 = 逐可视行 `DrawText`（`TextBox.cpp:1318+`）。
- **K4 多行 preferred 挂账**：`TextBox::GetPreferredSize` 多行分支返回当前尺寸（`TextBox.cpp:164-166`——「多行高度 v1 手工 SetSize，不参与 AutoSize」）⇒ **多行高度语义**是既有挂账，本 Phase 需定义。
- **K5 测量接口无宽度约束原语**：`TextMeasurer` = `Initialize` / `MeasureText(全串)` / `LineHeight`（`TextMeasurer.h:26/32/36`）——**没有**「给定 maxExtent 求 fit 字符数」的查询；GDI 链原生 `GetTextExtentExPointW` 已有批三产品码使用先例（`GDIBackend.cpp` 批三 DrawText）；FT 链需要实现（初设必答 D3）。
- **K6 Phase 28 指纹键无宽度约束**：`TextWidget` 指纹 = (revision, font.size, font.family, dpi)（`TextWidget.h` 批二成员）——Label 无宽度约束故够用；**wrap 后 preferred = f(宽度约束)** ⇒ 指纹键必须扩展（Phase 28 评审 §14 预告项兑现）。
- **K7 消费者证据**：#52①（首位）；harness 夹具 `realistic` 的 Markdown 内容 + 7KB tool 输出即典型折行对象（`realistic_texts.txt` 语料可复用作测试语料）。
- **K8 初设待勘察**：断行规则细节（拉丁按词 / CJK 逐字可断 / 超长无空格词回退按字、空白折叠策略——**是否引入 UAX #14 简化集**）、wrap 行宽测量的复杂度形态与 Phase 28 advance memo 的复用方式、行内对齐与 #51③ 的交互。

---

## 3. 范围

### 3.1 做（倾向）

1. **TextWidget/Label 增 word-wrap**——**显式开启、默认关**（现状零回归的根基；开关形态 D2）。
2. **多行 preferred 高度语义**：行数 × 行高（K4 挂账的定义兑现）；宽度约束进指纹键（K6/D4）。
3. **宽度约束测量原语**（D3）：TextMeasurer 层 fit 查询（GDI 原生 / FT 实现）——供断行与 GDI 绘制共用。

### 3.2 非目标

- **TextBox 编辑器内 wrap**：编辑光标/选择/IME 的码点↔视觉映射在折行下复杂度倍增——**v1 明确不做**（独立挂账，行号/选择模型不动）。
- 富文本（#53）/ shaping / 双向文本——`FontEngine` 冻结边界维持。
- 逐字动画（#54）/ 垂直排版 / 自动缩字（fit-to-width 缩放）。
- 改变**未开 wrap** 的任何既有行为（A2 逐位等价红线）。

---

## 4. 待决点（倾向已给，待评审）

- **D1** ★★ **断行算法与 CJK 语义**（**初设第一优先级**；评审 §5–§8 细化）：拉丁按空格/连字符断词——**连字符为合法断点且保留在前行**（"super-" / "long-word"，禁 "-" 独占下行）；CJK **主体字符逐字可断**（评审 §8：不承诺完整 UAX #14——v1 只挑少量最明显标点规则，如开括号禁行尾/闭括号禁行首，防「你好（」式断法）；**空白语义**（评审 §6）：v1 不做 whitespace collapsing、保留原文本语义，但断行处的分隔空格不属于任何一行（"Hello world" → "Hello"/"world"）；双空格等边缘初设定案；**确定性**（禁 locale/NLS 依赖——A4 可测性）；复杂度 **O(n)，n = Unicode code points**（评审 §18；Phase 28 advance memo 复用）。
- **D2** ★ 落点与开关形态：倾向 `TextStyle` 增 wrap 字段（`StyleField<bool>` 同构——默认 false）或独立 setter；与 #51③ 行内对齐的交互边界。
- **D3** ★★ **测量原语形态与 API 口径**：`TextMeasurer` 增 fit 查询虚函数（公共头方法 +1——**API 账如实记**）vs 派生类内部实现；GDI `GetTextExtentExPointW` 原生 vs FT 逐码点 + Phase 28 memo 复用；**双链一致性**（同输入同 fit——T27-10 口径）。★ **评审 §11：初设必须做三方案比较**——方案 A `TextMeasurer::FitText(...)` / 方案 B TextMeasurer 内部逐 glyph advance 查询 / 方案 C 独立内部 WrapEngine（**不扩公共 API**）；「不要因 GDI 有 `GetTextExtentExPointW` 就自动把它升格为 ECDI 公共抽象」。★ **若落 FitText：返回值 = Unicode code point index**（评审 §12——禁 UTF-8 byte offset / wchar index；TextBox `m_lineStarts` 已是码点索引同源）。
- **D4** ★ 指纹键扩展：宽度约束进 Phase 28 指纹（`m_prefMaxWidth` 或等价）——向后兼容（未开 wrap 时约束 = 无穷，键语义不变）。
- **D5** 多行 preferred / AutoSize 语义：行数 × 行高；与 K4 的 TextBox 挂账是否统一收口。★ **评审 §3/§4 冻结倾向**：**v1 wrap 宽度 = TextWidget 当前内容区域宽度**（现无 padding ⇒ 即 widget.width；将来加 padding 变 contentRect.width）；**拒绝**「独立 wrap-width 公共状态」（防 `SetWidth`/`SetWrapWidth` 双宽度竞争）；**wrap=true + 无有限宽度 ⇒ 退化单行**（wrap 不凭空制造宽度约束 ⇒ AutoSize 循环消解）。
- **D6** ★★★ **共享 TextLayout 结果**（评审 §9/§21——唯一「强烈建议初设解决」的架构点）：**一份**行布局结果（`Line{start, length, width}` + 总尺寸）同时供 `GetPreferredSize`（行数/总高）与 `OnPaint`（逐行 DrawText）消费——**禁止**两条路径各自 WrapText（防「preferred 5 行 / 实绘 6 行」类恶疾 + 双倍测量）；不必是公共类型（内部概念即可）；行宽进布局结果（#51③ 行内对齐自然接入——对齐按行独立算 x，非整块一次）。
- **4.1 初设必答**（评审给定，v1.1 定稿：四条红线 + 四个强约束）：① ★★★ **默认关 = 100% 原路径保留**（评审 §16/§17 升格——不只是像素相等：`wrap=false` **不得进入** wrap layout engine；同时保证像素/命令流/性能/Phase 28 缓存行为四不变）；② ★★ **测量与绘制共享一份 TextLayout 结果**（评审 §9/§21 升格——禁止 `GetPreferredSize` 与 `OnPaint` 各自 WrapText）；③ ★★ **n = Unicode code points**，内部行表示用 `[start, length]`、禁反复 substr（评审 §18——布局阶段禁隐式 O(n²)，UTF-8 只在 DrawText 时生成）；④ 断行**确定性**（同一输入恒同一切分——禁 locale/系统 NLS）。**强约束**：宽度动态测试维度（评审 §20——width 300→5 行 / 250→6 行 / 400→4 行，preferred 同步变；直接验证 D4 指纹宽度键：text/font/dpi 不变而 width 变 ⇒ 必 miss）；若落 FitText 则返回值 = 码点索引（评审 §12）；`Line{start,length,width}` 进布局结果（评审 §14）；v1 高度 = 行数×LineHeight（评审 §13——不加 leading/段距）。

---

## 5. 验收方向（A1–A5，待初设细化）

- **A1** 存量 **335** 全绿——默认关 ⇒ 既有 330 + T28 五条零回归。
- **A2** **像素等价 + 旧路径保留**（评审 §16/§17 升格）：未开 wrap 的既有场景逐位不变（T27-10 口径）**且** `wrap=false` 不进入 wrap layout engine（结构性判据——可经测量调用计数验证，非仅比像素）。
- **A3** wrap 功能正确性：拉丁按词 / CJK 逐字 / 混排 / 超长无空格词 / 恰好贴边 / 空串与单字符——行切分与 preferred 高度断言（`RecordingBackend` 命令流：**每行一条 DrawText、文本为该行内容**——评审 §15 认可此粒度，禁多行合并进单命令扩张 RenderCommand 语义）。
- **A4** 性能有界 + **宽度动态**（评审 §20 新增维度）：重排整体 O(n)（n = 码点；内部 `[start, length]` 禁 substr）；**宽度变化 ⇒ 指纹必失效 ⇒ 行数/preferred 同步变**（width 300→5 行 / 250→6 行 / 400→4 行直测场景——D4 键扩展的验收锚）。
- **A5** harness 气泡场景端到端（消费闭环——固定宽折行 + 高度自适应 + `realistic` 语料；评审 §19：验证的是「能力真的能被消费者使用」而非单测通过）。

---

## 6. 影响面预算（初估，待初设校准）

| 项 | 预算 |
|---|---|
| 公共头文件数量 | **94 → 94**（`TextMeasurer.h` 扩员不加文件；若断行引擎独立成头则 95——初设定） |
| 公共 API | **+1 倾向**（`TextMeasurer` fit 查询虚函数；若 D3 选内部实现则 +0——如实记） |
| 用例 | **335 → ~341**（+6 倾向：功能 ×3 / 指纹键扩展 ×1 / 像素等价 ×1 / 性能锚 ×1） |
| CMake | **0 改动** |
| 风险 | **中**——断行 CJK 语义（D1）+ 指纹键演进（D4 触碰 Phase 28 新码）+ 测量原语双链一致性（D3）；缓释 = 默认关 + A2 逐位等价红线 + Phase 28 memo 复用 |

---

## 7. 修订记录

- **v1.1**（2026-10-06）评审吸收。**结论：「Phase 29 Requirements v1.0：PASS → Preliminary Design」**——15 项分级 = **9 ✅ PASS + 6 🟡 初设必须解决**（CJK/Latin 方向、宽度约束来源、AutoSize+wrap 关系、TextLayout 共享、TextMeasurer Fit API、性能 O(n) 定义）；评审定位 =「D1～D5 正好就是初设应该解决的问题」，并预警 **Phase 29 是 ECDI 第一次出现「文本布局」概念**（此边界划得好坏直接影响将来富文本/shaping/Bidi 是否推倒重来）。评审认可：TextBox editor wrap 拆分正确（列其八项复杂度：光标/selection/点击映射/IME/上下移动/滚动/line index/visual↔logical）；**Phase 28 缓存是可演进缓存而非写死设计**（K6/D4 提前兑现宽度约束）；断行方向合理且**不建议 v1 引入完整 UAX #14**；A5 选得好（验证「能力能被消费者使用」而非单测通过）。**吸收（评审 §3–§21 逐条）**：① **D1 细化** = 连字符保留前行（禁 "-" 独占下行）· CJK 不承诺逐字任意断（v1 = 主体字符逐字 + 少量明显标点规则，防「你好（」式断法）· **空白语义** = 不做 collapsing、分隔空格不属于任何一行 · n = 码点；② **D6 新增（宽度约束来源 + AutoSize 循环）** = v1 wrap 宽度 = 内容区域宽度（现即 widget.width）、**拒绝独立 wrap-width 公共状态**、**wrap=true 无有限宽度 ⇒ 退化单行**；③ **D7 新增（共享 TextLayout——评审唯一「强烈建议」架构点）** = 一份 `Line{start,length,width}`+总尺寸供双消费端，禁各自 WrapText；④ **D3 强制三方案比较**（FitText / 内部 advance 查询 / 独立 WrapEngine 不扩 API；「勿因 GDI 有 fit 原语就升格公共抽象」）+ **FitText 返回 = 码点索引**；⑤ **§4.1 定稿** = 四条红线（**默认关 = 100% 原路径**升格最强红线 / 共享 TextLayout / n=码点+禁 substr / 确定性）+ 四个强约束（宽度动态测试、码点索引返回、Line 三元组、高度=行数×LineHeight 无 leading）；⑥ **A2 升格** = 像素等价**且**旧路径保留（可经调用计数验证）；**A4 增宽度动态维度**；A3 维持每行一条 DrawText（禁命令语义扩张）。未采纳：无。
- **v1.0**（2026-10-06）初稿。**输入**：#52①（harness 高保真模型评估最大缺口——消费者场景 = 聊天气泡固定宽折行 + 高度自适应）+ 勘察 K1–K8 带行号（全库零 wrap 代码 · TextBox 显式 `\n` 分段无断行 · 多行 preferred 挂账 · 测量接口无 fit 原语 · Phase 28 指纹键无宽度约束）+ Phase 28 交互点预置（D4 指纹键扩展 = 28 评审 §14 预告项兑现）。范围 = TextWidget/Label 显示型 wrap（**默认关**）+ 多行 preferred 语义 + 宽度约束测量原语；**非目标 = TextBox 编辑器内 wrap**（独立挂账）；待决 D1–D5 · 验收 A1–A5 · 影响面 94→94 / API +1 倾向 / 335→~341 / CMake 0 / 风险 中。
